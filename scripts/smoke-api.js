const { loadEnvFile } = require("../backend/src/config/env");

loadEnvFile();

const { getDatabaseClient, parseMysqlDatabaseUrl } = require("../backend/src/config/database");
const { createServer } = require("../backend/src/server/createServer");
const { dataStore } = require("../backend/src/db");

const marker = `smoke-${Date.now()}`;
const server = createServer();

function listen() {
  return new Promise((resolve) => {
    server.listen(0, "127.0.0.1", () => resolve(server.address().port));
  });
}

async function request(baseUrl, method, path, body, expectedStatus) {
  const response = await fetch(`${baseUrl}${path}`, {
    method,
    headers: body ? { "Content-Type": "application/json" } : undefined,
    body: body ? JSON.stringify(body) : undefined
  });
  const text = await response.text();

  if (response.status !== expectedStatus) {
    throw new Error(`${method} ${path} expected ${expectedStatus}, got ${response.status}: ${text}`);
  }

  return text ? JSON.parse(text) : {};
}

async function requestText(baseUrl, path, expectedStatus, expectedText) {
  const response = await fetch(`${baseUrl}${path}`);
  const text = await response.text();

  if (response.status !== expectedStatus) {
    throw new Error(`GET ${path} expected ${expectedStatus}, got ${response.status}: ${text}`);
  }

  if (expectedText && !text.includes(expectedText)) {
    throw new Error(`GET ${path} did not include expected text: ${expectedText}`);
  }

  return text;
}

async function cleanupMysql() {
  if (getDatabaseClient() !== "mysql" || !process.env.DATABASE_URL) return;

  let mysql;
  try {
    mysql = require("mysql2/promise");
  } catch (error) {
    return;
  }

  const pool = mysql.createPool(parseMysqlDatabaseUrl(process.env.DATABASE_URL));
  const ignoreMissing = async (sql, params) => {
    try {
      await pool.execute(sql, params);
    } catch (error) {
      if (!["ER_NO_SUCH_TABLE", "ER_BAD_FIELD_ERROR"].includes(error.code)) throw error;
    }
  };

  await ignoreMissing("DELETE FROM access_requests WHERE email = ?", [`${marker}@example.test`]);
  await ignoreMissing("DELETE FROM qr_scans WHERE qr_code = ?", [marker]);
  await ignoreMissing("DELETE FROM reward_redemptions WHERE contributor_email = ?", [`${marker}@example.test`]);
  await ignoreMissing("DELETE FROM process_actions WHERE payload LIKE ?", [`%${marker}%`]);
  await pool.end();
}

async function main() {
  const port = await listen();
  const baseUrl = `http://127.0.0.1:${port}`;
  const results = [];

  try {
    const health = await request(baseUrl, "GET", "/api/health", null, 200);
    results.push(`health:${health.ok ? "ok" : "bad"}:${health.database}`);

    await requestText(baseUrl, "/", 200, "PaperBriq");
    results.push("web-index:ok");

    await requestText(baseUrl, "/assets/images/paperbriq-illustration.png", 200);
    results.push("web-image:ok");

    await request(baseUrl, "POST", "/api/auth/login", {
      email: "admin@paperbriq.local",
      password: "paperbriq123",
      role: "admin"
    }, 200);
    results.push("login:ok");

    const getEndpoints = [
      "/api/summary",
      "/api/dashboard",
      "/api/process",
      "/api/iot",
      "/api/contributions",
      "/api/rewards",
      "/api/reports",
      "/api/history",
      "/api/settings",
      "/api/qr-scans"
    ];

    for (const endpoint of getEndpoints) {
      await request(baseUrl, "GET", endpoint, null, 200);
      results.push(`${endpoint}:ok`);
    }

    await request(baseUrl, "POST", "/api/process/actions", { action: "start", source: marker }, 201);
    results.push("process-action:ok");

    await request(baseUrl, "POST", "/api/rewards/redeem", {
      rewardId: "voucher-100",
      email: `${marker}@example.test`
    }, 201);
    results.push("reward-redeem:ok");

    const settings = await request(baseUrl, "GET", "/api/settings", null, 200);
    await request(baseUrl, "PUT", "/api/settings", settings, 200);
    results.push("settings-update:ok");

    await request(baseUrl, "POST", "/api/qr-scans", {
      code: marker,
      contributor: "Smoke Test",
      barangay: "San Isidro",
      weightKg: 1.25,
      pointsEarned: 13
    }, 201);
    results.push("qr-create:ok");

    await request(baseUrl, "POST", "/api/access-requests", {
      fullName: "Smoke Test",
      email: `${marker}@example.test`,
      requestedRole: "contributor",
      barangay: "San Isidro",
      message: "Backend smoke test"
    }, 201);
    results.push("access-request:ok");

    await cleanupMysql();
    results.push("cleanup:ok");
    console.log(results.join("\n"));
  } finally {
    await shutdown();
  }
}

async function shutdown() {
  if (server.listening) {
    await new Promise((resolve, reject) => {
      server.close((error) => {
        if (error) reject(error);
        else resolve();
      });
    });
  }

  if (typeof dataStore.close === "function") {
    await dataStore.close();
  }
}

main().catch((error) => {
  console.error(error.message);
  process.exit(1);
});
