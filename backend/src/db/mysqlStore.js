const { randomUUID } = require("crypto");
const { ROLE_LABELS } = require("../config/roles");
const { parseMysqlDatabaseUrl } = require("../config/database");
const {
  canAcknowledge,
  inferProcessState,
  mapDeviceCommand,
  mapDeviceEvent,
  publicStoreError
} = require("./deviceControl");

function loadMysql() {
  try {
    return require("mysql2/promise");
  } catch (error) {
    return null;
  }
}

function createMysqlStore(databaseUrl) {
  const mysql = loadMysql();
  if (!mysql || !databaseUrl) return null;

  const pool = mysql.createPool(parseMysqlDatabaseUrl(databaseUrl));
  let deviceControlSchemaPromise;
  const ensureControlSchema = () => {
    if (!deviceControlSchemaPromise) {
      deviceControlSchemaPromise = ensureDeviceControlSchema(pool).catch((error) => {
        deviceControlSchemaPromise = null;
        throw error;
      });
    }
    return deviceControlSchemaPromise;
  };

  return {
    mode: "MySQL",
    configured: true,
    async close() {
      await pool.end();
    },
    async login(email, password, role) {
      await ensureUserProfileColumns(pool);
      const roleFilter = role || "";
      const [rows] = await pool.execute(
        `SELECT id, full_name, email, role, barangay, profile_image_url
         FROM app_users
         WHERE LOWER(email) = ?
           AND (? = '' OR role = ?)
           AND is_active = TRUE
           AND password_hash = SHA2(?, 256)
         LIMIT 1`,
        [email, roleFilter, roleFilter, password]
      );

      if (!rows.length) return null;

      const row = rows[0];
      return {
        id: row.id,
        fullName: row.full_name,
        email: row.email,
        role: row.role,
        roleLabel: ROLE_LABELS[row.role],
        barangay: row.barangay,
        profileImageUrl: row.profile_image_url || ""
      };
    },
    async signup(signup) {
      const connection = await pool.getConnection();
      const id = randomUUID();

      try {
        await connection.beginTransaction();
        await connection.execute(
          `INSERT INTO app_users (id, full_name, email, role, password_hash, barangay, is_active)
           VALUES (?, ?, ?, ?, SHA2(?, 256), ?, TRUE)`,
          [id, signup.fullName, signup.email, signup.role, signup.password, signup.barangay]
        );

        await connection.execute(
          `INSERT INTO contributors (user_id, display_name, phone, barangay)
           VALUES (?, ?, ?, ?)
           ON DUPLICATE KEY UPDATE
             user_id = COALESCE(user_id, VALUES(user_id)),
             phone = COALESCE(NULLIF(VALUES(phone), ''), phone)`,
          [id, signup.fullName, signup.phone || null, signup.barangay]
        );

        await connection.commit();
        return {
          id,
          fullName: signup.fullName,
          email: signup.email,
          role: signup.role,
          roleLabel: ROLE_LABELS[signup.role],
          barangay: signup.barangay
        };
      } catch (error) {
        await connection.rollback();
        if (error && error.code === "ER_DUP_ENTRY") {
          error.statusCode = 409;
          error.publicMessage = "An account with that email already exists.";
        }
        throw error;
      } finally {
        connection.release();
      }
    },
    async getStaffAccounts() {
      await ensureUserProfileColumns(pool);
      const [rows] = await pool.execute(
        `SELECT id, full_name, email, barangay, profile_image_url, is_active, created_at
         FROM app_users
         WHERE role = 'barangay_staff'
         ORDER BY created_at DESC, full_name ASC`
      );

      return rows.map((row) => ({
        id: row.id,
        fullName: row.full_name,
        email: row.email,
        role: "barangay_staff",
        roleLabel: ROLE_LABELS.barangay_staff,
        barangay: row.barangay,
        profileImageUrl: row.profile_image_url || "",
        isActive: Boolean(row.is_active),
        createdAt: row.created_at
      }));
    },
    async createStaffAccount(staff) {
      const id = randomUUID();

      try {
        await pool.execute(
          `INSERT INTO app_users (id, full_name, email, role, password_hash, barangay, is_active)
           VALUES (?, ?, ?, 'barangay_staff', SHA2(?, 256), ?, TRUE)`,
          [id, staff.fullName, staff.email, staff.password, staff.barangay]
        );
      } catch (error) {
        if (error && error.code === "ER_DUP_ENTRY") {
          error.statusCode = 409;
          error.publicMessage = "An account with that email already exists.";
        }
        throw error;
      }

      return {
        id,
        fullName: staff.fullName,
        email: staff.email,
        role: "barangay_staff",
        roleLabel: ROLE_LABELS.barangay_staff,
        barangay: staff.barangay,
        profileImageUrl: "",
        isActive: true,
        createdAt: new Date().toISOString()
      };
    },
    async updateProfileImage(profile) {
      await ensureUserProfileColumns(pool);
      const [result] = await pool.execute(
        `UPDATE app_users
         SET profile_image_url = ?, updated_at = CURRENT_TIMESTAMP
         WHERE LOWER(email) = ?
           AND role = ?
           AND is_active = TRUE`,
        [profile.imageData, profile.email, profile.role]
      );

      if (!result.affectedRows) {
        const error = new Error("Staff account not found.");
        error.statusCode = 404;
        error.publicMessage = "Staff account not found.";
        throw error;
      }

      const [rows] = await pool.execute(
        `SELECT id, full_name, email, role, barangay, profile_image_url
         FROM app_users
         WHERE LOWER(email) = ?
           AND role = ?
         LIMIT 1`,
        [profile.email, profile.role]
      );
      const row = rows[0];

      return {
        id: row.id,
        fullName: row.full_name,
        email: row.email,
        role: row.role,
        roleLabel: ROLE_LABELS[row.role],
        barangay: row.barangay,
        profileImageUrl: row.profile_image_url || ""
      };
    },
    async getSummary() {
      return withSchemaFallback(async () => {
        const [totals, collections, batches] = await Promise.all([
          pool.query("SELECT paper_kg, briquettes, contributors, co2_kg_avoided FROM dashboard_totals"),
          pool.query(
            `SELECT barangay,
                    paper_weight_kg,
                    status,
                    DATE_FORMAT(collected_at, '%b %d, %h:%i %p') AS collected_at
             FROM collection_events
             ORDER BY collected_at DESC
             LIMIT 4`
          ),
          pool.query(
            `SELECT batch_code, briquettes_produced, quality_status
             FROM briquette_batches
             ORDER BY produced_at DESC
             LIMIT 3`
          )
        ]);

        const totalRow = totals[0][0] || {};
        return {
          generatedAt: new Date().toISOString(),
          displayTime: formatDateTime(new Date()),
          totals: {
            paperKg: Number(totalRow.paper_kg || 0),
            briquettes: Number(totalRow.briquettes || 0),
            contributors: Number(totalRow.contributors || 0),
            co2KgAvoided: Number(totalRow.co2_kg_avoided || 0)
          },
          collections: collections[0].map((row) => ({
            barangay: row.barangay,
            paperWeightKg: Number(row.paper_weight_kg),
            status: row.status,
            collectedAt: row.collected_at
          })),
          batches: batches[0].map((row) => ({
            batchCode: row.batch_code,
            briquettesProduced: Number(row.briquettes_produced),
            qualityStatus: row.quality_status
          }))
        };
      }, emptySummary);
    },
    async getDashboard() {
      return withSchemaFallback(async () => {
        const [summary, steps, logs, latestRun, latestSensors] = await Promise.all([
          this.getSummary(),
          getProcessSteps(pool),
          getProcessLogs(pool),
          getLatestProcessRun(pool),
          getLatestSensorMap(pool)
        ]);

        return buildDashboardSnapshot(summary, steps, logs, latestRun, latestSensors);
      }, () => buildDashboardSnapshot(emptySummary(), [], [], null, new Map()));
    },
    async getProcess() {
      return withSchemaFallback(async () => {
        const active = await getLatestProcessRun(pool);
        return {
          activeBatch: active ? mapProcessRun(active) : null,
          steps: await getProcessSteps(pool),
          qualityChecks: await getQualityChecks(pool),
          logs: await getProcessLogs(pool)
        };
      }, () => ({ activeBatch: null, steps: [], qualityChecks: [], logs: [] }));
    },
    async createProcessAction(action) {
      const id = randomUUID();
      await pool.execute(
        `INSERT INTO process_actions (id, action, payload)
         VALUES (?, ?, ?)`,
        [id, String(action.action || "start"), JSON.stringify(action)]
      );
      return {
        id,
        action: action.action || "start",
        status: "accepted",
        created_at: new Date().toISOString()
      };
    },
    async createDeviceCommand(input) {
      await ensureControlSchema();
      const id = randomUUID();
      const createdAt = new Date();
      const expiresAt = new Date(createdAt.getTime() + input.ttlSeconds * 1000);

      try {
        await pool.execute(
          `INSERT INTO device_commands
             (id, device_id, command, status, idempotency_key, message, created_at, expires_at, updated_at)
           VALUES (?, ?, ?, 'queued', ?, '', ?, ?, ?)`,
          [id, input.deviceId, input.command, input.idempotencyKey || null, createdAt, expiresAt, createdAt]
        );
      } catch (error) {
        if (error?.code !== "ER_DUP_ENTRY" || !input.idempotencyKey) throw error;
        const [existing] = await pool.execute(
          `SELECT id, device_id, command, status, message, created_at, expires_at,
                  delivered_at, completed_at
           FROM device_commands
           WHERE device_id = ? AND idempotency_key = ?
           LIMIT 1`,
          [input.deviceId, input.idempotencyKey]
        );
        if (existing[0]) return mapDeviceCommand(existing[0]);
        throw error;
      }

      return mapDeviceCommand({
        id,
        deviceId: input.deviceId,
        command: input.command,
        status: "queued",
        message: "",
        createdAt,
        expiresAt,
        deliveredAt: null,
        completedAt: null
      });
    },
    async getNextDeviceCommand(deviceId) {
      await ensureControlSchema();
      const now = new Date();
      await expireMysqlCommands(pool, now);
      await touchMysqlDevice(pool, deviceId, now);
      const [rows] = await pool.execute(
        `SELECT id, device_id, command, status, message, created_at, expires_at,
                delivered_at, completed_at
         FROM device_commands
         WHERE device_id = ? AND status = 'queued'
         ORDER BY created_at ASC, id ASC
         LIMIT 1`,
        [deviceId]
      );
      return mapDeviceCommand(rows[0]);
    },
    async acknowledgeDeviceCommand(id, acknowledgement) {
      await ensureControlSchema();
      const connection = await pool.getConnection();
      const now = new Date();
      try {
        await connection.beginTransaction();
        await expireMysqlCommands(connection, now);
        await touchMysqlDevice(connection, acknowledgement.deviceId, now);
        const [rows] = await connection.execute(
          `SELECT id, device_id, command, status, message, created_at, expires_at,
                  delivered_at, completed_at
           FROM device_commands
           WHERE id = ? AND device_id = ?
           FOR UPDATE`,
          [id, acknowledgement.deviceId]
        );
        const current = rows[0];
        if (!current) throw publicStoreError(404, "Device command not found.");
        if (!canAcknowledge(current.status, acknowledgement.status)) {
          throw publicStoreError(409, `Command cannot move from ${current.status} to ${acknowledgement.status}.`);
        }
        if (current.status !== acknowledgement.status) {
          const deliveredAt = acknowledgement.status === "delivered"
            ? now
            : (["completed", "failed"].includes(acknowledgement.status) ? (current.delivered_at || now) : current.delivered_at);
          const completedAt = ["completed", "failed"].includes(acknowledgement.status) ? now : current.completed_at;
          await connection.execute(
            `UPDATE device_commands
             SET status = ?, message = ?, delivered_at = ?, completed_at = ?, updated_at = ?
             WHERE id = ?`,
            [
              acknowledgement.status,
              acknowledgement.message || current.message || "",
              deliveredAt,
              completedAt,
              now,
              id
            ]
          );
          Object.assign(current, {
            status: acknowledgement.status,
            message: acknowledgement.message || current.message || "",
            delivered_at: deliveredAt,
            completed_at: completedAt
          });
        }
        await connection.commit();
        return mapDeviceCommand(current);
      } catch (error) {
        await connection.rollback();
        throw error;
      } finally {
        connection.release();
      }
    },
    async recordDeviceEvent(input) {
      await ensureControlSchema();
      const id = randomUUID();
      const now = new Date();
      await touchMysqlDevice(pool, input.deviceId, now);
      try {
        await pool.execute(
          `INSERT INTO device_events
             (id, device_id, event_key, event_type, severity, component, message,
              process_state, command_id, metadata, created_at)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
          [
            id,
            input.deviceId,
            input.eventKey || null,
            input.type,
            input.severity,
            input.component,
            input.message,
            input.processState || null,
            input.commandId || null,
            JSON.stringify(input.metadata || {}),
            now
          ]
        );
      } catch (error) {
        if (error?.code !== "ER_DUP_ENTRY" || !input.eventKey) throw error;
        const [existing] = await pool.execute(
          `SELECT id, device_id, event_type, severity, component, message,
                  process_state, command_id, metadata, created_at
           FROM device_events
           WHERE device_id = ? AND event_key = ?
           LIMIT 1`,
          [input.deviceId, input.eventKey]
        );
        if (existing[0]) return mapDeviceEvent(existing[0]);
        throw error;
      }
      return mapDeviceEvent({ ...input, id, createdAt: now });
    },
    async getDeviceControlStatus(deviceId, onlineAfter) {
      await ensureControlSchema();
      await expireMysqlCommands(pool, new Date());
      const [[commandRows], [eventRows], [presenceRows]] = await Promise.all([
        pool.execute(
          `SELECT id, device_id, command, status, message, created_at, expires_at,
                  delivered_at, completed_at
           FROM device_commands
           WHERE device_id = ?
           ORDER BY created_at DESC, id DESC
           LIMIT 12`,
          [deviceId]
        ),
        pool.execute(
          `SELECT id, device_id, event_type, severity, component, message,
                  process_state, command_id, metadata, created_at
           FROM device_events
           WHERE device_id = ?
           ORDER BY created_at DESC, id DESC
           LIMIT 20`,
          [deviceId]
        ),
        pool.execute(
          `SELECT last_seen_at
           FROM device_control_presence
           WHERE device_id = ?
           LIMIT 1`,
          [deviceId]
        )
      ]);
      const commands = commandRows.map(mapDeviceCommand);
      const events = eventRows.map(mapDeviceEvent);
      const lastSeenAt = presenceRows[0]?.last_seen_at || null;
      return {
        deviceId,
        processState: inferProcessState(events, commands),
        device: {
          connected: Boolean(lastSeenAt && new Date(lastSeenAt) >= onlineAfter),
          lastSeenAt: lastSeenAt ? new Date(lastSeenAt).toISOString() : null,
          lastEventAt: events[0]?.createdAt || null
        },
        commands,
        events
      };
    },
    async getIotMonitoring() {
      return withSchemaFallback(async () => {
        const [sensors] = await pool.query(
          `SELECT sensor_name, reading_value, reading_unit, status, recorded_at
           FROM iot_sensor_readings
           ORDER BY recorded_at DESC
           LIMIT 160`
        );
        const [[devices], [alerts], [diagnostics], latestRun] = await Promise.all([
          pool.query(
            `SELECT device_name, device_value, status, updated_at
             FROM device_statuses
             ORDER BY display_order ASC`
          ),
          pool.query(
            `SELECT title, severity, message, created_at
             FROM system_alerts
             ORDER BY created_at DESC
             LIMIT 8`
          ),
          pool.query(
            `SELECT item, status
             FROM diagnostics
             ORDER BY display_order ASC`
          ),
          getLatestProcessRun(pool)
        ]);

        return buildIotSnapshot(sensors, devices, alerts, diagnostics, latestRun);
      }, () => buildIotSnapshot([], [], [], [], null));
    },
    async recordIotTelemetry(telemetry) {
      const connection = await pool.getConnection();
      try {
        await connection.beginTransaction();
        for (const reading of telemetry.readings) {
          await connection.execute(
            `INSERT INTO iot_sensor_readings (sensor_name, reading_value, reading_unit, status, recorded_at)
             VALUES (?, ?, ?, ?, NOW())`,
            [reading.name, reading.value, reading.unit, reading.status]
          );
        }

        const hardwareLabels = {
          dht22: "DHT22",
          irSensor: "IR Sensor",
          tftDisplay: "TFT Display",
          floatSwitch: "Float Switch"
        };
        let displayOrder = 20;
        for (const [key, label] of Object.entries(hardwareLabels)) {
          if (!(key in telemetry.hardware)) continue;
          await connection.execute(
            `INSERT INTO diagnostics (item, status, display_order)
             VALUES (?, ?, ?)
             ON DUPLICATE KEY UPDATE status = VALUES(status)`,
            [label, String(telemetry.hardware[key] || "Unknown"), displayOrder++]
          );
        }
        await connection.commit();
        return {
          deviceId: telemetry.deviceId,
          readingsStored: telemetry.readings.length,
          recordedAt: new Date().toISOString()
        };
      } catch (error) {
        await connection.rollback();
        throw error;
      } finally {
        connection.release();
      }
    },
    async getContributions() {
      return withSchemaFallback(async () => {
        const [[contributors], [weeklyPaper], [weeklyBatches], latestRun] = await Promise.all([
          pool.query(
            `SELECT c.display_name,
                    c.barangay,
                    COALESCE(SUM(e.paper_weight_kg), 0) AS paper_kg,
                    ROUND(COALESCE(SUM(e.paper_weight_kg), 0) * 10) AS points,
                    MAX(e.status) AS scan_status
             FROM contributors c
             LEFT JOIN collection_events e ON e.contributor_id = c.id
             GROUP BY c.id, c.display_name, c.barangay
             ORDER BY paper_kg DESC, c.display_name ASC
             LIMIT 20`
          ),
          pool.query(
            `SELECT COALESCE(SUM(paper_weight_kg), 0) AS paper_kg
             FROM collection_events
             WHERE collected_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)`
          ),
          pool.query(
            `SELECT COALESCE(SUM(briquettes_produced), 0) AS briquettes
             FROM briquette_batches
             WHERE produced_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)`
          ),
          getLatestProcessRun(pool)
        ]);

        const list = contributors.map((row) => ({
          name: row.display_name,
          barangay: row.barangay,
          paperDonatedKg: Number(row.paper_kg),
          points: Number(row.points),
          status: row.scan_status || "No scans"
        }));

        return {
          metrics: {
            totalContributors: list.length,
            paperDonatedThisWeekKg: Number(weeklyPaper[0]?.paper_kg || 0),
            briquettesProducedThisWeek: Number(weeklyBatches[0]?.briquettes || 0),
            weeklyTarget: Number(latestRun?.target_briquettes || 0)
          },
          contributors: list
        };
      }, () => ({
        metrics: {
          totalContributors: 0,
          paperDonatedThisWeekKg: 0,
          briquettesProducedThisWeek: 0,
          weeklyTarget: 0
        },
        contributors: []
      }));
    },
    async getRewards() {
      return withSchemaFallback(async () => {
        await ensureRewardMetadataColumns(pool);
        const [[rewards], [history], [redemptionStats], [pointTotals]] = await Promise.all([
          pool.query(
            `SELECT reward_code,
                    reward_name,
                    points_cost,
                    category,
                    is_active,
                    reward_image_url,
                    description,
                    stock_quantity,
                    created_by_role,
                    created_by_name,
                    created_at
             FROM rewards
             WHERE is_active = TRUE
             ORDER BY created_at DESC, points_cost ASC`
          ),
          pool.query(
            `SELECT points, transaction_type, title, DATE_FORMAT(happened_at, '%b %d, %Y %h:%i %p') AS happened_at
             FROM point_transactions
             ORDER BY happened_at DESC
             LIMIT 8`
          ),
          pool.query(
            `SELECT status, COUNT(*) AS total, COALESCE(SUM(points_spent), 0) AS points
             FROM reward_redemptions
             GROUP BY status`
          ),
          pool.query(
            `SELECT
               COALESCE(SUM(points), 0) AS total_points,
               COALESCE(SUM(CASE WHEN points > 0 AND happened_at >= DATE_FORMAT(CURRENT_DATE, '%Y-%m-01') THEN points ELSE 0 END), 0) AS earned_month,
               COALESCE(SUM(CASE WHEN points < 0 AND happened_at >= DATE_FORMAT(CURRENT_DATE, '%Y-%m-01') THEN ABS(points) ELSE 0 END), 0) AS redeemed_month
             FROM point_transactions`
          )
        ]);
        const totals = pointTotals[0] || {};

        return buildRewardMonitor({
          totals: {
            totalPoints: Number(totals.total_points || 0),
            pointsEarnedThisMonth: Number(totals.earned_month || 0),
            pointsRedeemedThisMonth: Number(totals.redeemed_month || 0),
            availablePoints: Number(totals.total_points || 0),
            currentStreakDays: 0
          },
          history: history.map((row) => ({
            points: Number(row.points),
            type: row.transaction_type,
            title: row.title,
            happenedAt: row.happened_at
          })),
          rewards: rewards.map((row) => ({
            id: row.reward_code,
            name: row.reward_name,
            pointsCost: Number(row.points_cost),
            category: row.category,
            description: row.description || "",
            stockQuantity: Number(row.stock_quantity || 0),
            imageUrl: row.reward_image_url || "",
            createdByRole: row.created_by_role || "",
            createdByName: row.created_by_name || "",
            createdAt: row.created_at
          })),
          redemptionStats: redemptionStats.map((row) => ({
            status: row.status,
            total: Number(row.total),
            points: Number(row.points)
          }))
        });
      }, () => buildRewardMonitor(emptyRewards()));
    },
    async createRewardItem(reward) {
      await ensureRewardMetadataColumns(pool);
      const rewardCode = reward.id || createRewardCode(reward.name);
      await pool.execute(
        `INSERT INTO rewards (
           id,
           reward_code,
           reward_name,
           points_cost,
           category,
           reward_image_url,
           description,
           stock_quantity,
           created_by_role,
           created_by_name,
           is_active
         )
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, TRUE)`,
        [
          randomUUID(),
          rewardCode,
          reward.name,
          Number(reward.pointsCost || 0),
          reward.category || "Barangay Reward",
          reward.imageUrl || null,
          reward.description || null,
          Number(reward.stockQuantity || 0),
          reward.creatorRole || "barangay_staff",
          reward.creatorName || "Barangay Staff"
        ]
      );

      return {
        id: rewardCode,
        name: reward.name,
        pointsCost: Number(reward.pointsCost || 0),
        category: reward.category || "Barangay Reward",
        description: reward.description || "",
        stockQuantity: Number(reward.stockQuantity || 0),
        imageUrl: reward.imageUrl || "",
        createdByRole: reward.creatorRole || "barangay_staff",
        createdByName: reward.creatorName || "Barangay Staff",
        createdAt: new Date().toISOString()
      };
    },
    async redeemReward(request) {
      const connection = await pool.getConnection();
      const id = randomUUID();

      try {
        await connection.beginTransaction();
        const [rewards] = await connection.execute(
          `SELECT reward_code, reward_name, points_cost, stock_quantity
           FROM rewards
           WHERE reward_code = ?
             AND is_active = TRUE
           LIMIT 1
           FOR UPDATE`,
          [request.rewardId]
        );
        const reward = rewards[0];
        if (!reward) {
          const error = new Error("Reward item not found.");
          error.statusCode = 404;
          error.publicMessage = "Reward item not found.";
          throw error;
        }

        if (Number(reward.stock_quantity || 0) <= 0) {
          const error = new Error("Reward item is out of stock.");
          error.statusCode = 409;
          error.publicMessage = "Reward item is out of stock.";
          throw error;
        }

        await connection.execute(
          `INSERT INTO reward_redemptions (id, reward_code, contributor_email, points_spent, status)
           VALUES (?, ?, ?, ?, 'redeemed')`,
          [id, reward.reward_code, request.email || null, Number(reward.points_cost || 0)]
        );

        await connection.execute(
          `UPDATE rewards
           SET stock_quantity = GREATEST(stock_quantity - 1, 0)
           WHERE reward_code = ?`,
          [reward.reward_code]
        );

        await connection.execute(
          `INSERT INTO point_transactions (contributor_email, points, transaction_type, title)
           VALUES (?, ?, 'redeemed', ?)`,
          [
            request.email || null,
            -Math.abs(Number(reward.points_cost || 0)),
            `Redeemed reward - ${reward.reward_name}`.slice(0, 180)
          ]
        );

        await connection.commit();
        return {
          id,
          rewardId: request.rewardId,
          rewardName: reward.reward_name,
          pointsSpent: Number(reward.points_cost || 0),
          status: "redeemed",
          created_at: new Date().toISOString()
        };
      } catch (error) {
        await connection.rollback();
        throw error;
      } finally {
        connection.release();
      }
    },
    async getReports() {
      return withSchemaFallback(async () => {
        const summary = await this.getSummary();
        const [barangays, dailyCollections, dailyBatches, previousCollections, previousBatches] = await Promise.all([
          pool.query(
            `SELECT barangay,
                    SUM(paper_weight_kg) AS paper_kg,
                    COUNT(DISTINCT contributor_id) AS contributors
             FROM collection_events
             GROUP BY barangay
             ORDER BY paper_kg DESC
             LIMIT 5`
          ),
          pool.query(
            `SELECT DATE_FORMAT(collected_at, '%b %d') AS label,
                    DATE(collected_at) AS report_date,
                    SUM(paper_weight_kg) AS paper_kg
             FROM collection_events
             WHERE collected_at >= DATE_SUB(CURRENT_DATE, INTERVAL 13 DAY)
             GROUP BY DATE(collected_at), label
             ORDER BY report_date ASC`
          ),
          pool.query(
            `SELECT DATE_FORMAT(produced_at, '%b %d') AS label,
                    DATE(produced_at) AS report_date,
                    SUM(briquettes_produced) AS briquettes
             FROM briquette_batches
             WHERE produced_at >= DATE_SUB(CURRENT_DATE, INTERVAL 13 DAY)
             GROUP BY DATE(produced_at), label
             ORDER BY report_date ASC`
          ),
          pool.query(
            `SELECT COALESCE(SUM(paper_weight_kg), 0) AS paper_kg
             FROM collection_events
             WHERE collected_at >= DATE_SUB(CURRENT_DATE, INTERVAL 27 DAY)
               AND collected_at < DATE_SUB(CURRENT_DATE, INTERVAL 13 DAY)`
          ),
          pool.query(
            `SELECT COALESCE(SUM(briquettes_produced), 0) AS briquettes
             FROM briquette_batches
             WHERE produced_at >= DATE_SUB(CURRENT_DATE, INTERVAL 27 DAY)
               AND produced_at < DATE_SUB(CURRENT_DATE, INTERVAL 13 DAY)`
          )
        ]);
        const collectionRows = dailyCollections[0] || [];
        const batchRows = dailyBatches[0] || [];
        const paperSeries = collectionRows.map((row) => Number(row.paper_kg));
        const batchSeries = batchRows.map((row) => Number(row.briquettes));
        const paperKg = Number(summary.totals.paperKg || 0);
        const briquettes = Number(summary.totals.briquettes || 0);
        const waterSavedL = Math.round(paperKg * 6.8);
        const co2Kg = Number(summary.totals.co2KgAvoided || 0);

        return {
          generatedAt: new Date().toISOString(),
          displayTime: formatDateTime(new Date()),
          nextPollMs: 7000,
          period: `Database report as of ${formatDateTime(new Date())}`,
          cards: [
            {
              label: "Paper Collected",
              value: `${formatNumber(paperKg)} kg`,
              change: formatPercentChange(paperKg, Number(previousCollections[0][0]?.paper_kg || 0)),
              percent: percentOf(paperKg, Math.max(paperKg, Number(previousCollections[0][0]?.paper_kg || 0), 1))
            },
            {
              label: "Briquettes Produced",
              value: `${formatNumber(briquettes)} pcs`,
              change: formatPercentChange(briquettes, Number(previousBatches[0][0]?.briquettes || 0)),
              percent: percentOf(briquettes, Math.max(briquettes, Number(previousBatches[0][0]?.briquettes || 0), 1))
            },
            {
              label: "Water Saved",
              value: `${formatNumber(waterSavedL)} L`,
              change: "",
              percent: percentOf(waterSavedL, Math.max(waterSavedL, 1))
            },
            {
              label: "Active Contributors",
              value: formatNumber(summary.totals.contributors || 0),
              change: "",
              percent: percentOf(summary.totals.contributors || 0, Math.max(summary.totals.contributors || 0, 1))
            }
          ],
          charts: {
            paperCollected: paperSeries,
            briquettesProduced: batchSeries,
            waterSaved: paperSeries.map((value) => Math.round(Number(value) * 6.8))
          },
          chartMeta: {
            labels: collectionRows.map((row) => row.label),
            source: "MySQL daily records"
          },
          topBarangays: barangays[0].map((row) => ({
            barangay: row.barangay,
            paperKg: Number(row.paper_kg),
            contributors: Number(row.contributors)
          })),
          impact: [
            { label: "Trees Saved", value: formatNumber(Math.max(0, Math.round(paperKg / 69))) },
            { label: "CO2 Reduced", value: `${formatNumber(co2Kg)} kg` },
            { label: "Energy Generated", value: `${formatNumber(Number((briquettes * 0.22).toFixed(1)))} kWh` },
            { label: "Landfill Diversion", value: `${formatNumber(paperKg)} kg` }
          ],
          summary: {
            updatedAt: formatDateTime(new Date()),
            note: "Report values are read from MySQL records."
          }
        };
      }, () => emptyReports());
    },
    async getHistory() {
      return withSchemaFallback(async () => {
        const [[events], [stats]] = await Promise.all([
          pool.query(
            `SELECT DATE_FORMAT(event_time, '%h:%i %p') AS time,
                    title,
                    description,
                    category
             FROM system_events
             ORDER BY event_time DESC
             LIMIT 30`
          ),
          pool.query(
            `SELECT
               COUNT(*) AS total_sessions,
               SUM(CASE WHEN status = 'completed' THEN 1 ELSE 0 END) AS completed_runs,
               COALESCE(SUM(TIMESTAMPDIFF(MINUTE, started_at, COALESCE(completed_at, NOW()))), 0) AS runtime_minutes
             FROM process_runs`
          )
        ]);
        const statRow = stats[0] || {};
        const totalSessions = Number(statRow.total_sessions || 0);
        const completedRuns = Number(statRow.completed_runs || 0);
        return {
          stats: {
            totalSessions,
            completedRuns,
            successRate: totalSessions ? `${Math.round((completedRuns / totalSessions) * 100)}%` : "0%",
            totalRuntime: formatRuntime(Number(statRow.runtime_minutes || 0))
          },
          events
        };
      }, () => ({
        stats: { totalSessions: 0, completedRuns: 0, successRate: "0%", totalRuntime: "0 mins" },
        events: []
      }));
    },
    async getSettings() {
      return withSchemaFallback(async () => {
        const [rows] = await pool.query("SELECT settings_json FROM app_settings WHERE settings_key = 'default' LIMIT 1");
        if (!rows.length) return {};
        return typeof rows[0].settings_json === "string" ? JSON.parse(rows[0].settings_json) : rows[0].settings_json;
      }, () => ({}));
    },
    async updateSettings(settings) {
      await pool.execute(
        `INSERT INTO app_settings (settings_key, settings_json)
         VALUES ('default', ?)
         ON DUPLICATE KEY UPDATE settings_json = VALUES(settings_json), updated_at = CURRENT_TIMESTAMP`,
        [JSON.stringify(settings)]
      );
      return {
        settings,
        updated_at: new Date().toISOString()
      };
    },
    async getQrScans() {
      return withSchemaFallback(async () => {
        const [rows] = await pool.query(
          `SELECT scan_time,
                  contributor_name,
                  barangay,
                  weight_kg,
                  points_earned,
                  status
           FROM qr_scans
           ORDER BY scan_time DESC
           LIMIT 20`
        );
        const recent = rows.map((row) => ({
          time: formatDate(row.scan_time),
          contributor: row.contributor_name,
          barangay: row.barangay,
          weightKg: Number(row.weight_kg),
          points: Number(row.points_earned),
          status: row.status
        }));
        const latest = recent[0];
        return {
          scanStatus: latest
            ? { status: String(latest.status || "").toLowerCase(), message: `${latest.contributor} scan recorded.` }
            : { status: "waiting", message: "No QR scans recorded yet." },
          contributor: latest
            ? {
                name: latest.contributor,
                barangay: latest.barangay,
                memberSince: "",
                totalContributions: recent.filter((item) => item.contributor === latest.contributor).length,
                pointsEarned: latest.points
              }
            : null,
          tips: [],
          recent
        };
      }, () => ({
        scanStatus: { status: "waiting", message: "No QR scans recorded yet." },
        contributor: null,
        tips: [],
        recent: []
      }));
    },
    async createQrScan(scan) {
      const id = randomUUID();
      const code = String(scan.code || "").trim();
      const weightKg = Number(scan.weightKg || 0);
      if (!code || !Number.isFinite(weightKg) || weightKg <= 0) {
        const error = new Error("Contributor QR code and paper weight are required.");
        error.statusCode = 400;
        error.publicMessage = "Contributor QR code and paper weight are required.";
        throw error;
      }

      const contributorName = String(scan.contributor || code).trim();
      const barangay = String(scan.barangay || "Unassigned").trim();
      const points = Number.isFinite(Number(scan.pointsEarned)) && Number(scan.pointsEarned) > 0
        ? Number(scan.pointsEarned)
        : Math.round(weightKg * 10);
      const eventLabel = `QR scan donation ${code}`.slice(0, 120);
      const pointsTitle = `QR scan donation - ${contributorName} (${code})`.slice(0, 180);
      const connection = await pool.getConnection();

      try {
        await connection.beginTransaction();
        await connection.execute(
          `INSERT INTO qr_scans (id, qr_code, contributor_name, barangay, weight_kg, points_earned, status)
           VALUES (?, ?, ?, ?, ?, ?, 'Success')`,
          [id, code, contributorName, barangay, weightKg, points]
        );

        await connection.execute(
          `INSERT INTO contributors (display_name, phone, barangay)
           VALUES (?, NULL, ?)
           ON DUPLICATE KEY UPDATE
             display_name = VALUES(display_name),
             barangay = VALUES(barangay)`,
          [contributorName, barangay]
        );

        const [contributors] = await connection.execute(
          `SELECT id
           FROM contributors
           WHERE display_name = ?
             AND barangay = ?
           LIMIT 1`,
          [contributorName, barangay]
        );
        const contributorId = contributors[0]?.id || null;

        await connection.execute(
          `INSERT INTO collection_events (contributor_id, paper_weight_kg, paper_grade, barangay, status)
           VALUES (?, ?, ?, ?, 'scanned')`,
          [contributorId, weightKg, eventLabel, barangay]
        );

        await connection.execute(
          `INSERT INTO point_transactions (contributor_email, points, transaction_type, title)
           VALUES (?, ?, 'earned', ?)`,
          [String(scan.email || "").trim() || null, points, pointsTitle]
        );

        await connection.commit();
        return {
          id,
          code,
          contributor: contributorName,
          barangay,
          weightKg,
          status: "success",
          pointsEarned: points,
          created_at: new Date().toISOString()
        };
      } catch (error) {
        await connection.rollback();
        throw error;
      } finally {
        connection.release();
      }
    },
    async createAccessRequest(request) {
      const id = randomUUID();
      await pool.execute(
        `INSERT INTO access_requests (id, full_name, email, requested_role, barangay, message)
         VALUES (?, ?, ?, ?, ?, ?)`,
        [id, request.fullName, request.email, request.requestedRole, request.barangay, request.message]
      );
      return {
        id,
        status: "pending",
        created_at: new Date().toISOString()
      };
    }
  };
}

async function getProcessSteps(pool) {
  const [rows] = await pool.query(
    `SELECT step_name, progress_percent, status, detail
     FROM process_steps
     ORDER BY display_order ASC`
  );
  return rows.length
    ? rows.map((row) => ({
        name: row.step_name,
        step: row.step_name,
        progress: Number(row.progress_percent),
        status: row.status,
        detail: row.detail
      }))
    : [];
}

async function getProcessLogs(pool) {
  const [rows] = await pool.query(
    `SELECT DATE_FORMAT(log_time, '%h:%i %p') AS time, event, status
     FROM process_logs
     ORDER BY log_time DESC
     LIMIT 12`
  );
  return rows;
}

async function getQualityChecks(pool) {
  const [rows] = await pool.query(
    `SELECT check_label, check_value
     FROM quality_checks
     ORDER BY display_order ASC`
  );
  return rows.length
    ? rows.map((row) => ({ label: row.check_label, value: row.check_value }))
    : [];
}

async function ensureRewardMetadataColumns(pool) {
  const [columns] = await pool.query(
    `SELECT COLUMN_NAME
     FROM INFORMATION_SCHEMA.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE()
       AND TABLE_NAME = 'rewards'`
  );
  const existing = new Set(columns.map((column) => column.COLUMN_NAME));
  const additions = [
    ["reward_image_url", "ALTER TABLE rewards ADD COLUMN reward_image_url MEDIUMTEXT NULL"],
    ["description", "ALTER TABLE rewards ADD COLUMN description TEXT NULL"],
    ["stock_quantity", "ALTER TABLE rewards ADD COLUMN stock_quantity INT NOT NULL DEFAULT 0"],
    ["created_by_role", "ALTER TABLE rewards ADD COLUMN created_by_role VARCHAR(40) NULL"],
    ["created_by_name", "ALTER TABLE rewards ADD COLUMN created_by_name VARCHAR(180) NULL"],
    ["created_at", "ALTER TABLE rewards ADD COLUMN created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"]
  ];

  for (const [columnName, sql] of additions) {
    if (!existing.has(columnName)) {
      await pool.execute(sql);
    }
  }
}

async function ensureUserProfileColumns(pool) {
  const [columns] = await pool.query(
    `SELECT COLUMN_NAME
     FROM INFORMATION_SCHEMA.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE()
       AND TABLE_NAME = 'app_users'`
  );
  const existing = new Set(columns.map((column) => column.COLUMN_NAME));
  if (!existing.has("profile_image_url")) {
    await pool.execute("ALTER TABLE app_users ADD COLUMN profile_image_url MEDIUMTEXT NULL AFTER barangay");
  }
}

function buildRewardMonitor(rewardsData) {
  const rewards = rewardsData.rewards || [];
  const history = rewardsData.history || [];
  const redemptionStats = rewardsData.redemptionStats || [];
  const pointsRedeemed = redemptionStats.length
    ? redemptionStats.reduce((sum, item) => sum + Number(item.points || 0), 0)
    : history
      .filter((item) => item.type === "redeemed" || Number(item.points) < 0)
      .reduce((sum, item) => sum + Math.abs(Number(item.points || 0)), 0);
  const pendingClaims = redemptionStats
    .filter((item) => ["pending", "queued", "requested"].includes(String(item.status).toLowerCase()))
    .reduce((sum, item) => sum + Number(item.total || 0), 0);

  return {
    ...rewardsData,
    monitor: {
      rewardItems: rewards.length,
      activeItems: rewards.length,
      totalStock: rewards.reduce((sum, item) => sum + Number(item.stockQuantity || 0), 0),
      pointsRedeemed,
      pendingClaims,
      lowStockItems: rewards.filter((item) => Number(item.stockQuantity || 0) <= 5).length
    }
  };
}

function createRewardCode(name) {
  const slug = String(name || "reward")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 48) || "reward";
  return `${slug}-${randomUUID().slice(0, 8)}`;
}

async function getLatestProcessRun(pool) {
  const [runs] = await pool.query(
    `SELECT batch_code, status, overall_progress, estimated_time_remaining,
            target_briquettes, completed_briquettes
     FROM process_runs
     ORDER BY started_at DESC
     LIMIT 1`
  );
  return runs[0] || null;
}

function mapProcessRun(row) {
  return {
    batchCode: row.batch_code,
    status: row.status,
    overallProgress: Number(row.overall_progress),
    estimatedTimeRemaining: row.estimated_time_remaining,
    targetBriquettes: Number(row.target_briquettes),
    completedBriquettes: Number(row.completed_briquettes)
  };
}

async function getLatestSensorMap(pool) {
  const [rows] = await pool.query(
    `SELECT sensor_name, reading_value, reading_unit, status, recorded_at
     FROM iot_sensor_readings
     ORDER BY recorded_at DESC
     LIMIT 160`
  );
  const sensors = new Map();
  for (const row of rows) {
    if (!sensors.has(row.sensor_name)) sensors.set(row.sensor_name, row);
  }
  return sensors;
}

function buildDashboardSnapshot(summary, steps, logs, latestRun, latestSensors) {
  const now = new Date();
  const progress = latestRun
    ? Number(latestRun.overall_progress || 0)
    : averageProgress(steps);
  const rainwaterLevel = sensorNumber(latestSensors, "Rainwater Level", now, 1);
  const batteryLevel = sensorNumber(latestSensors, "Battery Level", now, 2);
  const temperatureC = sensorNumber(latestSensors, "Temperature", now, 2);
  const humidityPercent = sensorNumber(latestSensors, "Humidity", now, 3);
  const powerOutputWatts = sensorNumber(latestSensors, "Power Output", now, 3);
  const paperKg = Number(summary?.totals?.paperKg || 0);

  return {
    generatedAt: now.toISOString(),
    displayTime: formatDateTime(now),
    summary,
    overallProgress: progress,
    systemStatus: {
      status: latestRun?.status || (latestSensors.size ? "Telemetry online" : "No machine data"),
      message: latestRun
        ? `${latestRun.batch_code} is ${String(latestRun.status || "recorded").replace(/_/g, " ")}.`
        : "Waiting for process records from MySQL.",
      rainwaterLevel,
      batteryLevel,
      temperatureC,
      humidityPercent,
      powerOutputWatts
    },
    processProgress: steps.slice(0, 5),
    processLog: logs.slice(0, 5),
    weeklySummary: {
      paperWasteKg: paperKg,
      briquettesProduced: Number(latestRun?.completed_briquettes || summary?.totals?.briquettes || 0),
      briquetteTarget: Number(latestRun?.target_briquettes || 0),
      waterSavedL: Math.round(paperKg * 6.8),
      activeContributors: Number(summary?.totals?.contributors || 0)
    }
  };
}

function buildIotSnapshot(sensorRows, deviceRows, alertRows, diagnosticRows, latestRun) {
  const now = new Date();
  const groupedSensors = new Map();
  for (const row of sensorRows) {
    const group = groupedSensors.get(row.sensor_name) || [];
    group.push(row);
    groupedSensors.set(row.sensor_name, group);
  }
  const sensors = Array.from(groupedSensors.entries()).map(([name, rows], index) => {
    const latest = rows[0];
    const value = storedSensorNumber(latest);
    const previousValue = storedSensorNumber(rows[1] || latest);
    return {
      name,
      value,
      unit: latest.reading_unit,
      status: liveSensorStatus(name, value, latest.status),
      trend: rows.slice(0, 12).reverse().map(storedSensorNumber),
      delta: Number((value - previousValue).toFixed(2)),
      updatedAt: formatDateTime(latest.recorded_at)
    };
  });
  const latestSensorTime = sensorRows[0]?.recorded_at ? new Date(sensorRows[0].recorded_at) : null;
  const warningCount = sensors.filter((sensor) => String(sensor.status).toLowerCase() === "warning").length;
  const onlineDevices = deviceRows.filter((device) => !/off|offline|error/i.test(`${device.device_value} ${device.status}`)).length;
  const okDiagnostics = diagnosticRows.filter((diagnostic) => /^ok$/i.test(diagnostic.status)).length;
  const packetSuccess = diagnosticRows.length ? `${Math.round((okDiagnostics / diagnosticRows.length) * 100)}%` : "--";
  const diagnosticStatus = (name, fallback = "No data") => (
    diagnosticRows.find((item) => String(item.item).toLowerCase() === name.toLowerCase())?.status || fallback
  );
  const checkedAt = formatDateTime(now);

  return {
    generatedAt: now.toISOString(),
    displayTime: formatDateTime(now),
    freshnessSeconds: latestSensorTime ? Math.max(0, Math.round((now.getTime() - latestSensorTime.getTime()) / 1000)) : null,
    nextPollMs: 4000,
    machine: {
      id: latestRun?.batch_code || "",
      name: latestRun?.batch_code || "",
      state: latestRun?.status || (sensors.length ? "Telemetry online" : "No telemetry"),
      barangay: "",
      cycle: latestRun?.batch_code || "",
      health: warningCount ? `${warningCount} warning${warningCount > 1 ? "s" : ""}` : "No warnings"
    },
    sensors,
    devices: deviceRows.map((row) => ({
      name: row.device_name,
      value: row.device_value,
      status: row.status
    })),
    alerts: alertRows.map((row) => ({
      title: row.title,
      severity: row.severity,
      message: row.message,
      createdAt: row.created_at
    })),
    diagnostics: diagnosticRows.map((row) => ({
      item: row.item,
      status: row.status,
      checkedAt: formatDateTime(now)
    })),
    hardware: [
      { name: "DHT22", detail: "Temperature and humidity sensor", status: diagnosticStatus("DHT22"), checkedAt },
      { name: "IR Sensor", detail: "Briquette detection sensor", status: diagnosticStatus("IR Sensor"), checkedAt },
      { name: "TFT Display", detail: "Local monitoring display", status: diagnosticStatus("TFT Display"), checkedAt },
      { name: "Float Switch", detail: "Rainwater level switch", status: diagnosticStatus("Float Switch"), checkedAt }
    ],
    network: {
      onlineDevices: deviceRows.length ? `${onlineDevices} / ${deviceRows.length}` : "--",
      packetSuccess,
      responseTimeMs: null,
      lastDataSync: latestSensorTime ? formatDateTime(latestSensorTime) : "",
      transport: "",
      signal: ""
    }
  };
}

function emptySummary() {
  return {
    generatedAt: new Date().toISOString(),
    displayTime: formatDateTime(new Date()),
    totals: {
      paperKg: 0,
      briquettes: 0,
      contributors: 0,
      co2KgAvoided: 0
    },
    collections: [],
    batches: []
  };
}

function emptyRewards() {
  return {
    totals: {
      totalPoints: 0,
      pointsEarnedThisMonth: 0,
      pointsRedeemedThisMonth: 0,
      availablePoints: 0,
      currentStreakDays: 0
    },
    history: [],
    rewards: [],
    redemptionStats: []
  };
}

function emptyReports() {
  const now = new Date();
  return {
    generatedAt: now.toISOString(),
    displayTime: formatDateTime(now),
    nextPollMs: 7000,
    period: `Database report as of ${formatDateTime(now)}`,
    cards: [],
    charts: {
      paperCollected: [],
      briquettesProduced: [],
      waterSaved: []
    },
    chartMeta: {
      labels: [],
      source: "MySQL daily records"
    },
    topBarangays: [],
    impact: [],
    summary: {
      updatedAt: formatDateTime(now),
      note: "No report records found in MySQL."
    }
  };
}

function withSchemaFallback(producer, fallbackProducer) {
  return producer().catch((error) => {
    if (isMissingSchemaError(error)) return fallbackProducer();
    throw error;
  });
}

function isMissingSchemaError(error) {
  return ["ER_NO_SUCH_TABLE", "ER_BAD_FIELD_ERROR", "ER_SP_DOES_NOT_EXIST"].includes(error && error.code);
}

function formatDate(value) {
  if (!value) return "";
  return new Intl.DateTimeFormat("en-PH", {
    hour: "2-digit",
    minute: "2-digit"
  }).format(new Date(value));
}

function formatDateTime(value) {
  if (!value) return "";
  return new Intl.DateTimeFormat("en-PH", {
    timeZone: "Asia/Manila",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  }).format(new Date(value));
}

function formatNumber(value) {
  return new Intl.NumberFormat("en-PH", {
    maximumFractionDigits: Number(value) % 1 === 0 ? 0 : 2
  }).format(Number(value) || 0);
}

function formatPercentChange(current, previous) {
  if (!previous) return "";
  const change = ((Number(current || 0) - Number(previous || 0)) / Math.max(1, Number(previous))) * 100;
  const sign = change > 0 ? "+" : "";
  return `${sign}${Math.round(change)}%`;
}

function percentOf(value, target) {
  return Math.min(100, Math.max(0, Math.round((Number(value || 0) / Math.max(1, Number(target || 0))) * 100)));
}

function averageProgress(steps) {
  const values = steps.map((step) => Number(step.progress)).filter((value) => Number.isFinite(value));
  if (!values.length) return 0;
  return Math.round(values.reduce((sum, value) => sum + value, 0) / values.length);
}

function sensorNumber(sensorMap, name, now, offset) {
  const row = sensorMap.get(name);
  return row ? storedSensorNumber(row) : 0;
}

function storedSensorNumber(row) {
  const value = Number(row?.reading_value || 0);
  return Number.isFinite(value) ? value : 0;
}

function formatRuntime(minutes) {
  const safeMinutes = Math.max(0, Math.round(Number(minutes || 0)));
  const hours = Math.floor(safeMinutes / 60);
  const mins = safeMinutes % 60;
  if (!hours) return `${mins} mins`;
  if (!mins) return `${hours} hr${hours > 1 ? "s" : ""}`;
  return `${hours} hr${hours > 1 ? "s" : ""} ${mins} mins`;
}

function buildLiveSensorTrend(row, now, offset) {
  return Array.from({ length: 12 }, (_, index) => {
    const pointTime = new Date(now.getTime() - (11 - index) * 8000);
    return liveSensorNumber(row, pointTime, offset);
  });
}

function liveSensorNumber(row, now, offset) {
  const base = Number(row?.reading_value || 0);
  if (!Number.isFinite(base)) return 0;

  const unit = String(row?.reading_unit || "").toLowerCase();
  const name = String(row?.sensor_name || "").toLowerCase();
  const amplitude = sensorAmplitude(name, unit, base);
  const seconds = Math.floor(now.getTime() / 1000);
  const wave = Math.sin(seconds * 0.32 + offset * 1.73) + Math.sin(seconds * 0.11 + offset * 0.47) * 0.35;
  let value = base + wave * amplitude;

  if (unit.includes("%")) value = Math.min(100, Math.max(0, value));
  if (name.includes("pressure")) value = Math.max(0, value);
  if (name.includes("tds")) value = Math.max(0, value);
  if (name.includes("power") || name.includes("flow")) value = Math.max(0, value);

  return Number(value.toFixed(sensorDecimals(unit, base)));
}

function sensorAmplitude(name, unit, base) {
  if (name.includes("temperature")) return 0.8;
  if (name.includes("humidity")) return 3.5;
  if (name.includes("rainwater")) return 2.2;
  if (name.includes("battery")) return 1.1;
  if (name.includes("power")) return 16;
  if (name.includes("flow")) return 0.8;
  if (name.includes("pressure")) return 0.12;
  if (name.includes("tds")) return 7;
  if (unit.includes("%")) return 2;
  return Math.max(0.5, Math.abs(base) * 0.04);
}

function sensorDecimals(unit, base) {
  if (unit.includes("bar") || unit.includes("min") || Math.abs(base % 1) > 0.001) return 1;
  return 0;
}

function liveSensorStatus(name, value, fallbackStatus) {
  const sensorName = String(name || "").toLowerCase();
  if (sensorName.includes("battery")) return value < 25 ? "Warning" : value >= 88 ? "Good" : "Normal";
  if (sensorName.includes("rainwater")) return value < 70 ? "Warning" : "Normal";
  if (sensorName.includes("temperature")) return value > 34 || value < 24 ? "Warning" : "Normal";
  if (sensorName.includes("tds")) return value > 210 ? "Warning" : "Good";
  return fallbackStatus || "Normal";
}

async function ensureDeviceControlSchema(pool) {
  await pool.execute(
    `CREATE TABLE IF NOT EXISTS device_commands (
       id CHAR(36) PRIMARY KEY,
       device_id VARCHAR(80) NOT NULL,
       command VARCHAR(20) NOT NULL,
       status VARCHAR(20) NOT NULL DEFAULT 'queued',
       idempotency_key VARCHAR(120) NULL,
       message VARCHAR(240) NOT NULL DEFAULT '',
       created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
       expires_at DATETIME(3) NOT NULL,
       delivered_at DATETIME(3) NULL,
       completed_at DATETIME(3) NULL,
       updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
       UNIQUE KEY uq_device_command_idempotency (device_id, idempotency_key),
       INDEX idx_device_command_queue (device_id, status, created_at),
       INDEX idx_device_command_expiry (status, expires_at)
     ) ENGINE=InnoDB`
  );
  await pool.execute(
    `CREATE TABLE IF NOT EXISTS device_events (
       id CHAR(36) PRIMARY KEY,
       device_id VARCHAR(80) NOT NULL,
       event_key VARCHAR(120) NULL,
       event_type VARCHAR(64) NOT NULL,
       severity VARCHAR(16) NOT NULL DEFAULT 'info',
       component VARCHAR(64) NOT NULL DEFAULT 'system',
       message VARCHAR(500) NOT NULL,
       process_state VARCHAR(24) NULL,
       command_id CHAR(36) NULL,
       metadata JSON NOT NULL,
       created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
       UNIQUE KEY uq_device_event_key (device_id, event_key),
       INDEX idx_device_events_recent (device_id, created_at),
       INDEX idx_device_events_command (command_id)
     ) ENGINE=InnoDB`
  );
  await pool.execute(
    `CREATE TABLE IF NOT EXISTS device_control_presence (
       device_id VARCHAR(80) PRIMARY KEY,
       last_seen_at DATETIME(3) NOT NULL,
       updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
         ON UPDATE CURRENT_TIMESTAMP(3)
     ) ENGINE=InnoDB`
  );
}

async function expireMysqlCommands(executor, now) {
  await executor.execute(
    `UPDATE device_commands
     SET status = 'expired',
         message = IF(message = '', 'Command expired before device acknowledgement.', message),
         completed_at = ?,
         updated_at = ?
     WHERE status = 'queued' AND expires_at <= ?`,
    [now, now, now]
  );
}

async function touchMysqlDevice(executor, deviceId, now) {
  await executor.execute(
    `INSERT INTO device_control_presence (device_id, last_seen_at, updated_at)
     VALUES (?, ?, ?)
     ON DUPLICATE KEY UPDATE last_seen_at = VALUES(last_seen_at), updated_at = VALUES(updated_at)`,
    [deviceId, now, now]
  );
}

module.exports = { createMysqlStore };
