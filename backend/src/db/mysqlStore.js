const { randomUUID } = require("crypto");
const { ROLE_LABELS } = require("../config/roles");
const { parseMysqlDatabaseUrl } = require("../config/database");
const { pageData } = require("../data/pageData");

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

  return {
    mode: "MySQL",
    configured: true,
    async close() {
      await pool.end();
    },
    async login(email, password, role) {
      const [rows] = await pool.execute(
        `SELECT id, full_name, email, role, barangay
         FROM app_users
         WHERE LOWER(email) = ?
           AND role = ?
           AND is_active = TRUE
           AND password_hash = SHA2(?, 256)
         LIMIT 1`,
        [email, role, password]
      );

      if (!rows.length) return null;

      const row = rows[0];
      return {
        id: row.id,
        fullName: row.full_name,
        email: row.email,
        role: row.role,
        roleLabel: ROLE_LABELS[row.role],
        barangay: row.barangay
      };
    },
    async getSummary() {
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
    },
    async getDashboard() {
      return withFallback("dashboard", async () => ({
        ...pageData.dashboard,
        summary: await this.getSummary(),
        processProgress: (await getProcessSteps(pool)).slice(0, 5),
        processLog: (await getProcessLogs(pool)).slice(0, 5)
      }));
    },
    async getProcess() {
      return withFallback("process", async () => {
        const [runs] = await pool.query(
          `SELECT batch_code, status, overall_progress, estimated_time_remaining,
                  target_briquettes, completed_briquettes
           FROM process_runs
           ORDER BY started_at DESC
           LIMIT 1`
        );

        const active = runs[0];
        return {
          activeBatch: active
            ? {
                batchCode: active.batch_code,
                status: active.status,
                overallProgress: Number(active.overall_progress),
                estimatedTimeRemaining: active.estimated_time_remaining,
                targetBriquettes: Number(active.target_briquettes),
                completedBriquettes: Number(active.completed_briquettes)
              }
            : pageData.process.activeBatch,
          steps: await getProcessSteps(pool),
          qualityChecks: await getQualityChecks(pool),
          logs: await getProcessLogs(pool)
        };
      });
    },
    async createProcessAction(action) {
      return withFallbackValue(async () => {
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
      }, {
        id: `demo-${randomUUID()}`,
        action: action.action || "start",
        status: "accepted",
        created_at: new Date().toISOString()
      });
    },
    async getIotMonitoring() {
      return withFallback("iot", async () => {
        const [sensors] = await pool.query(
          `SELECT sensor_name, reading_value, reading_unit, status
           FROM iot_sensor_readings
           WHERE recorded_at >= NOW() - INTERVAL 1 DAY
           ORDER BY recorded_at DESC`
        );
        const latestSensors = [];
        const seen = new Set();
        for (const row of sensors) {
          if (seen.has(row.sensor_name)) continue;
          seen.add(row.sensor_name);
          latestSensors.push({
            name: row.sensor_name,
            value: Number(row.reading_value),
            unit: row.reading_unit,
            status: row.status
          });
        }

        const [devices] = await pool.query(
          `SELECT device_name, device_value, status
           FROM device_statuses
           ORDER BY display_order ASC`
        );
        const [alerts] = await pool.query(
          `SELECT title, severity, message
           FROM system_alerts
           ORDER BY created_at DESC
           LIMIT 8`
        );
        const [diagnostics] = await pool.query(
          `SELECT item, status
           FROM diagnostics
           ORDER BY display_order ASC`
        );

        return {
          sensors: latestSensors.length ? latestSensors : pageData.iot.sensors,
          devices: devices.map((row) => ({ name: row.device_name, value: row.device_value, status: row.status })),
          alerts,
          diagnostics,
          network: pageData.iot.network
        };
      });
    },
    async getContributions() {
      return withFallback("contributions", async () => {
        const [contributors] = await pool.query(
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
        );

        const list = contributors.map((row) => ({
          name: row.display_name,
          barangay: row.barangay,
          paperDonatedKg: Number(row.paper_kg),
          points: Number(row.points),
          status: row.scan_status || "Scanned"
        }));

        return {
          metrics: {
            totalContributors: list.length,
            paperDonatedThisWeekKg: Number(list.reduce((sum, item) => sum + item.paperDonatedKg, 0).toFixed(2)),
            briquettesProducedThisWeek: 8,
            weeklyTarget: 20
          },
          contributors: list.length ? list : pageData.contributions.contributors
        };
      });
    },
    async getRewards() {
      return withFallback("rewards", async () => {
        const [rewards] = await pool.query(
          `SELECT reward_code, reward_name, points_cost, category, is_active
           FROM rewards
           WHERE is_active = TRUE
           ORDER BY points_cost ASC`
        );
        const [history] = await pool.query(
          `SELECT points, transaction_type, title, DATE_FORMAT(happened_at, '%b %d, %Y %h:%i %p') AS happened_at
           FROM point_transactions
           ORDER BY happened_at DESC
           LIMIT 8`
        );

        return {
          ...pageData.rewards,
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
            category: row.category
          }))
        };
      });
    },
    async redeemReward(request) {
      return withFallbackValue(async () => {
        const id = randomUUID();
        await pool.execute(
          `INSERT INTO reward_redemptions (id, reward_code, contributor_email, points_spent, status)
           SELECT ?, reward_code, ?, points_cost, 'redeemed'
           FROM rewards
           WHERE reward_code = ?
           LIMIT 1`,
          [id, request.email || null, request.rewardId]
        );
        return {
          id,
          rewardId: request.rewardId,
          status: "redeemed",
          created_at: new Date().toISOString()
        };
      }, {
        id: `demo-${randomUUID()}`,
        rewardId: request.rewardId,
        status: "redeemed",
        created_at: new Date().toISOString()
      });
    },
    async getReports() {
      return withFallback("reports", async () => {
        const summary = await this.getSummary();
        const [barangays] = await pool.query(
          `SELECT barangay,
                  SUM(paper_weight_kg) AS paper_kg,
                  COUNT(DISTINCT contributor_id) AS contributors
           FROM collection_events
           GROUP BY barangay
           ORDER BY paper_kg DESC
           LIMIT 5`
        );
        return {
          ...pageData.reports,
          totals: summary.totals,
          topBarangays: barangays.map((row) => ({
            barangay: row.barangay,
            paperKg: Number(row.paper_kg),
            contributors: Number(row.contributors)
          }))
        };
      });
    },
    async getHistory() {
      return withFallback("history", async () => {
        const [events] = await pool.query(
          `SELECT DATE_FORMAT(event_time, '%h:%i %p') AS time,
                  title,
                  description,
                  category
           FROM system_events
           ORDER BY event_time DESC
           LIMIT 30`
        );
        return {
          ...pageData.history,
          events: events.length ? events : pageData.history.events
        };
      });
    },
    async getSettings() {
      return withFallback("settings", async () => {
        const [rows] = await pool.query("SELECT settings_json FROM app_settings WHERE settings_key = 'default' LIMIT 1");
        if (!rows.length) return pageData.settings;
        return typeof rows[0].settings_json === "string" ? JSON.parse(rows[0].settings_json) : rows[0].settings_json;
      });
    },
    async updateSettings(settings) {
      return withFallbackValue(async () => {
        const merged = { ...pageData.settings, ...settings };
        await pool.execute(
          `INSERT INTO app_settings (settings_key, settings_json)
           VALUES ('default', ?)
           ON DUPLICATE KEY UPDATE settings_json = VALUES(settings_json), updated_at = CURRENT_TIMESTAMP`,
          [JSON.stringify(merged)]
        );
        return {
          settings: merged,
          updated_at: new Date().toISOString()
        };
      }, {
        settings: { ...pageData.settings, ...settings },
        updated_at: new Date().toISOString()
      });
    },
    async getQrScans() {
      return withFallback("qrScans", async () => {
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
        return {
          ...pageData.qrScans,
          recent: rows.map((row) => ({
            time: formatDate(row.scan_time),
            contributor: row.contributor_name,
            barangay: row.barangay,
            weightKg: Number(row.weight_kg),
            points: Number(row.points_earned),
            status: row.status
          }))
        };
      });
    },
    async createQrScan(scan) {
      return withFallbackValue(async () => {
        const id = randomUUID();
        const points = Number(scan.pointsEarned || 15);
        await pool.execute(
          `INSERT INTO qr_scans (id, qr_code, contributor_name, barangay, weight_kg, points_earned, status)
           VALUES (?, ?, ?, ?, ?, ?, 'Success')`,
          [
            id,
            scan.code || "manual",
            scan.contributor || "Manual Contributor",
            scan.barangay || "Unassigned",
            Number(scan.weightKg || 0),
            points
          ]
        );
        return {
          id,
          code: scan.code || "manual",
          status: "success",
          pointsEarned: points,
          created_at: new Date().toISOString()
        };
      }, {
        id: `demo-${randomUUID()}`,
        code: scan.code || "manual",
        status: "success",
        pointsEarned: 15,
        created_at: new Date().toISOString()
      });
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
    : pageData.process.steps;
}

async function getProcessLogs(pool) {
  const [rows] = await pool.query(
    `SELECT DATE_FORMAT(log_time, '%h:%i %p') AS time, event, status
     FROM process_logs
     ORDER BY log_time DESC
     LIMIT 12`
  );
  return rows.length ? rows : pageData.process.logs;
}

async function getQualityChecks(pool) {
  const [rows] = await pool.query(
    `SELECT check_label, check_value
     FROM quality_checks
     ORDER BY display_order ASC`
  );
  return rows.length
    ? rows.map((row) => ({ label: row.check_label, value: row.check_value }))
    : pageData.process.qualityChecks;
}

function withFallback(section, producer) {
  return producer().catch((error) => {
    if (isMissingSchemaError(error)) {
      return pageData[section];
    }
    throw error;
  });
}

function withFallbackValue(producer, fallback) {
  return producer().catch((error) => {
    if (isMissingSchemaError(error)) return fallback;
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

module.exports = { createMysqlStore };
