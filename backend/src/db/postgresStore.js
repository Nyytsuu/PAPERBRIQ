const { ROLE_LABELS } = require("../config/roles");
const { createLiveSummarySnapshot } = require("../data/liveDashboard");

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

module.exports = { createPostgresStore };
