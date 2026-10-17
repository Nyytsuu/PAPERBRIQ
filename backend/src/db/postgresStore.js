const { randomUUID } = require("crypto");
const { ROLE_LABELS } = require("../config/roles");
const { createLiveSummarySnapshot } = require("../data/liveDashboard");
const {
  canAcknowledge,
  inferProcessState,
  mapDeviceCommand,
  mapDeviceEvent,
  publicStoreError
} = require("./deviceControl");

function loadPgPool() {
  try {
    return require("pg").Pool;
  } catch (error) {
    return null;
  }
}

function createPostgresStore(databaseUrl) {
  const Pool = loadPgPool();
  if (!Pool || !databaseUrl) return null;

  const pool = new Pool({ connectionString: databaseUrl });
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
    mode: "PostgreSQL",
    configured: true,
    async close() {
      await pool.end();
    },
    async login(email, password, role) {
      const result = await pool.query(
        `SELECT id, full_name, email, role, barangay
         FROM app_users
         WHERE LOWER(email) = $1
           AND role = $2::user_role
           AND is_active = TRUE
           AND password_hash = crypt($3, password_hash)
         LIMIT 1`,
        [email, role, password]
      );

      if (!result.rowCount) return null;

      const row = result.rows[0];
      return {
        id: row.id,
        fullName: row.full_name,
        email: row.email,
        role: row.role,
        roleLabel: ROLE_LABELS[row.role],
        barangay: row.barangay
      };
    },
    async getStaffAccounts() {
      const result = await pool.query(
        `SELECT id, full_name, email, barangay, is_active, created_at
         FROM app_users
         WHERE role = 'barangay_staff'::user_role
         ORDER BY created_at DESC, full_name ASC`
      );

      return result.rows.map((row) => ({
        id: row.id,
        fullName: row.full_name,
        email: row.email,
        role: "barangay_staff",
        roleLabel: ROLE_LABELS.barangay_staff,
        barangay: row.barangay,
        isActive: row.is_active,
        createdAt: row.created_at
      }));
    },
    async createStaffAccount(staff) {
      try {
        const result = await pool.query(
          `INSERT INTO app_users (full_name, email, role, password_hash, barangay, is_active)
           VALUES ($1, $2, 'barangay_staff'::user_role, crypt($3, gen_salt('bf')), $4, TRUE)
           RETURNING id, full_name, email, role, barangay, is_active, created_at`,
          [staff.fullName, staff.email, staff.password, staff.barangay]
        );
        const row = result.rows[0];
        return {
          id: row.id,
          fullName: row.full_name,
          email: row.email,
          role: row.role,
          roleLabel: ROLE_LABELS[row.role],
          barangay: row.barangay,
          isActive: row.is_active,
          createdAt: row.created_at
        };
      } catch (error) {
        if (error && error.code === "23505") {
          error.statusCode = 409;
          error.publicMessage = "An account with that email already exists.";
        }
        throw error;
      }
    },
    async getSummary() {
      const [totals, collections, batches] = await Promise.all([
        pool.query("SELECT paper_kg, briquettes, contributors, co2_kg_avoided FROM dashboard_totals"),
        pool.query(
          `SELECT barangay,
                  paper_weight_kg,
                  status,
                  TO_CHAR(collected_at AT TIME ZONE 'Asia/Manila', 'Mon DD, HH12:MI AM') AS collected_at
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

      const totalRow = totals.rows[0] || {};
      return createLiveSummarySnapshot({
        totals: {
          paperKg: Number(totalRow.paper_kg || 0),
          briquettes: Number(totalRow.briquettes || 0),
          contributors: Number(totalRow.contributors || 0),
          co2KgAvoided: Number(totalRow.co2_kg_avoided || 0)
        },
        collections: collections.rows.map((row) => ({
          barangay: row.barangay,
          paperWeightKg: Number(row.paper_weight_kg),
          status: row.status,
          collectedAt: row.collected_at
        })),
        batches: batches.rows.map((row) => ({
          batchCode: row.batch_code,
          briquettesProduced: Number(row.briquettes_produced),
          qualityStatus: row.quality_status
        }))
      });
    },
    async createDeviceCommand(input) {
      await ensureControlSchema();
      const createdAt = new Date();
      const expiresAt = new Date(createdAt.getTime() + input.ttlSeconds * 1000);
      const result = await pool.query(
        `INSERT INTO device_commands
           (id, device_id, command, status, idempotency_key, message, created_at, expires_at, updated_at)
         VALUES ($1, $2, $3, 'queued', $4, '', $5, $6, $5)
         ON CONFLICT (device_id, idempotency_key)
         DO UPDATE SET idempotency_key = EXCLUDED.idempotency_key
         RETURNING id, device_id, command, status, message, created_at, expires_at,
                   delivered_at, completed_at`,
        [randomUUID(), input.deviceId, input.command, input.idempotencyKey || null, createdAt, expiresAt]
      );
      return mapDeviceCommand(result.rows[0]);
    },
    async getNextDeviceCommand(deviceId) {
      await ensureControlSchema();
      const now = new Date();
      await expirePostgresCommands(pool, now);
      await touchPostgresDevice(pool, deviceId, now);
      const result = await pool.query(
        `SELECT id, device_id, command, status, message, created_at, expires_at,
                delivered_at, completed_at
         FROM device_commands
         WHERE device_id = $1 AND status = 'queued'
         ORDER BY created_at ASC, id ASC
         LIMIT 1`,
        [deviceId]
      );
      return mapDeviceCommand(result.rows[0]);
    },
    async acknowledgeDeviceCommand(id, acknowledgement) {
      await ensureControlSchema();
      const client = await pool.connect();
      const now = new Date();
      try {
        await client.query("BEGIN");
        await expirePostgresCommands(client, now);
        await touchPostgresDevice(client, acknowledgement.deviceId, now);
        const result = await client.query(
          `SELECT id, device_id, command, status, message, created_at, expires_at,
                  delivered_at, completed_at
           FROM device_commands
           WHERE id = $1 AND device_id = $2
           FOR UPDATE`,
          [id, acknowledgement.deviceId]
        );
        const current = result.rows[0];
        if (!current) throw publicStoreError(404, "Device command not found.");
        if (!canAcknowledge(current.status, acknowledgement.status)) {
          throw publicStoreError(409, `Command cannot move from ${current.status} to ${acknowledgement.status}.`);
        }
        if (current.status !== acknowledgement.status) {
          const deliveredAt = acknowledgement.status === "delivered"
            ? now
            : (["completed", "failed"].includes(acknowledgement.status) ? (current.delivered_at || now) : current.delivered_at);
          const completedAt = ["completed", "failed"].includes(acknowledgement.status) ? now : current.completed_at;
          const updated = await client.query(
            `UPDATE device_commands
             SET status = $1, message = $2, delivered_at = $3, completed_at = $4, updated_at = $5
             WHERE id = $6
             RETURNING id, device_id, command, status, message, created_at, expires_at,
                       delivered_at, completed_at`,
            [
              acknowledgement.status,
              acknowledgement.message || current.message || "",
              deliveredAt,
              completedAt,
              now,
              id
            ]
          );
          await client.query("COMMIT");
          return mapDeviceCommand(updated.rows[0]);
        }
        await client.query("COMMIT");
        return mapDeviceCommand(current);
      } catch (error) {
        await client.query("ROLLBACK");
        throw error;
      } finally {
        client.release();
      }
    },
    async recordDeviceEvent(input) {
      await ensureControlSchema();
      const now = new Date();
      await touchPostgresDevice(pool, input.deviceId, now);
      const result = await pool.query(
        `INSERT INTO device_events
           (id, device_id, event_key, event_type, severity, component, message,
            process_state, command_id, metadata, created_at)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10::jsonb, $11)
         ON CONFLICT (device_id, event_key)
         DO UPDATE SET event_key = EXCLUDED.event_key
         RETURNING id, device_id, event_type, severity, component, message,
                   process_state, command_id, metadata, created_at`,
        [
          randomUUID(),
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
      return mapDeviceEvent(result.rows[0]);
    },
    async getDeviceControlStatus(deviceId, onlineAfter) {
      await ensureControlSchema();
      await expirePostgresCommands(pool, new Date());
      const [commandResult, eventResult, presenceResult] = await Promise.all([
        pool.query(
          `SELECT id, device_id, command, status, message, created_at, expires_at,
                  delivered_at, completed_at
           FROM device_commands
           WHERE device_id = $1
           ORDER BY created_at DESC, id DESC
           LIMIT 12`,
          [deviceId]
        ),
        pool.query(
          `SELECT id, device_id, event_type, severity, component, message,
                  process_state, command_id, metadata, created_at
           FROM device_events
           WHERE device_id = $1
           ORDER BY created_at DESC, id DESC
           LIMIT 20`,
          [deviceId]
        ),
        pool.query(
          `SELECT last_seen_at
           FROM device_control_presence
           WHERE device_id = $1
           LIMIT 1`,
          [deviceId]
        )
      ]);
      const commands = commandResult.rows.map(mapDeviceCommand);
      const events = eventResult.rows.map(mapDeviceEvent);
      const lastSeenAt = presenceResult.rows[0]?.last_seen_at || null;
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
    async createAccessRequest(request) {
      const result = await pool.query(
        `INSERT INTO access_requests (full_name, email, requested_role, barangay, message)
         VALUES ($1, $2, $3, $4, $5)
         RETURNING id, status, created_at`,
        [request.fullName, request.email, request.requestedRole, request.barangay, request.message]
      );
      return result.rows[0];
    }
  };
}

async function ensureDeviceControlSchema(pool) {
  await pool.query(
    `CREATE TABLE IF NOT EXISTS device_commands (
       id UUID PRIMARY KEY,
       device_id VARCHAR(80) NOT NULL,
       command VARCHAR(20) NOT NULL CHECK (command IN ('start', 'stop', 'reset')),
       status VARCHAR(20) NOT NULL DEFAULT 'queued'
         CHECK (status IN ('queued', 'delivered', 'completed', 'failed', 'expired')),
       idempotency_key VARCHAR(120),
       message VARCHAR(240) NOT NULL DEFAULT '',
       created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
       expires_at TIMESTAMPTZ NOT NULL,
       delivered_at TIMESTAMPTZ,
       completed_at TIMESTAMPTZ,
       updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
       UNIQUE (device_id, idempotency_key)
     )`
  );
  await pool.query(
    `CREATE INDEX IF NOT EXISTS idx_device_command_queue
       ON device_commands (device_id, status, created_at)`
  );
  await pool.query(
    `CREATE INDEX IF NOT EXISTS idx_device_command_expiry
       ON device_commands (status, expires_at)`
  );
  await pool.query(
    `CREATE TABLE IF NOT EXISTS device_events (
       id UUID PRIMARY KEY,
       device_id VARCHAR(80) NOT NULL,
       event_key VARCHAR(120),
       event_type VARCHAR(64) NOT NULL,
       severity VARCHAR(16) NOT NULL DEFAULT 'info',
       component VARCHAR(64) NOT NULL DEFAULT 'system',
       message VARCHAR(500) NOT NULL,
       process_state VARCHAR(24),
       command_id UUID,
       metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
       created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
       UNIQUE (device_id, event_key)
     )`
  );
  await pool.query(
    `CREATE INDEX IF NOT EXISTS idx_device_events_recent
       ON device_events (device_id, created_at DESC)`
  );
  await pool.query(
    `CREATE INDEX IF NOT EXISTS idx_device_events_command
       ON device_events (command_id)`
  );
  await pool.query(
    `CREATE TABLE IF NOT EXISTS device_control_presence (
       device_id VARCHAR(80) PRIMARY KEY,
       last_seen_at TIMESTAMPTZ NOT NULL,
       updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
     )`
  );
}

async function expirePostgresCommands(executor, now) {
  await executor.query(
    `UPDATE device_commands
     SET status = 'expired',
         message = CASE
           WHEN message = '' THEN 'Command expired before device acknowledgement.'
           ELSE message
         END,
         completed_at = $1,
         updated_at = $1
     WHERE status = 'queued' AND expires_at <= $1`,
    [now]
  );
}

async function touchPostgresDevice(executor, deviceId, now) {
  await executor.query(
    `INSERT INTO device_control_presence (device_id, last_seen_at, updated_at)
     VALUES ($1, $2, $2)
     ON CONFLICT (device_id)
     DO UPDATE SET last_seen_at = EXCLUDED.last_seen_at, updated_at = EXCLUDED.updated_at`,
    [deviceId, now]
  );
}

module.exports = { createPostgresStore };
