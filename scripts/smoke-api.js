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
  await ignoreMissing("DELETE FROM collection_events WHERE paper_grade = ?", [`QR scan donation ${marker}`]);
  await ignoreMissing("DELETE FROM point_transactions WHERE title LIKE ?", [`%${marker}%`]);
  await ignoreMissing("DELETE FROM reward_redemptions WHERE contributor_email = ?", [`${marker}@example.test`]);
  await ignoreMissing("DELETE FROM rewards WHERE reward_name = ?", [`Smoke Reward ${marker}`]);
  await ignoreMissing("DELETE FROM process_actions WHERE payload LIKE ?", [`%${marker}%`]);
  await ignoreMissing("DELETE FROM contributors WHERE display_name = ? AND barangay = ?", ["Smoke Test Signup", marker]);
  await ignoreMissing("DELETE FROM app_users WHERE email = ?", [`staff-${marker}@example.test`]);
  await ignoreMissing("DELETE FROM app_users WHERE email = ?", [`${marker}@example.test`]);
  await pool.end();
}

async function main() {
  const port = await listen();
  const baseUrl = `http://127.0.0.1:${port}`;
  const results = [];

  try {
    const health = await request(baseUrl, "GET", "/api/health", null, 200);
    results.push(`health:${health.ok ? "ok" : "bad"}:${health.database}`);

    const indexHtml = await requestText(baseUrl, "/", 200, "PaperBriq");
    if (indexHtml.includes("/src/main.jsx")) {
      throw new Error("GET / served the Vite source index instead of the compiled build.");
    }
    results.push("web-index:ok");

    await requestText(baseUrl, "/assets/images/paperbriq-illustration.png", 200);
    results.push("web-image:ok");

    await requestText(baseUrl, "/assets/images/paperbriq-logo-mark.svg", 200, "PaperBriq logo mark");
    results.push("web-logo:ok");

    await request(baseUrl, "POST", "/api/auth/signup", {
      fullName: "Smoke Test Signup",
      email: `${marker}@example.test`,
      barangay: marker,
      password: "paperbriq123",
      role: "contributor"
    }, 201);
    results.push("signup:ok");

    await request(baseUrl, "POST", "/api/auth/login", {
      email: "contributor@paperbriq.local",
      password: "paperbriq123",
      role: "contributor"
    }, 200);
    results.push("login:ok");

    await request(baseUrl, "POST", "/api/auth/login", {
      email: "admin@paperbriq.local",
      password: "paperbriq123",
      role: "admin"
    }, 200);
    results.push("admin-login:ok");

    await request(baseUrl, "POST", "/api/auth/forgot-password", {
      email: "admin@paperbriq.local",
      role: "admin"
    }, 202);
    results.push("forgot-password:ok");

    await request(baseUrl, "GET", "/api/staff-accounts?creatorRole=admin", null, 200);
    results.push("staff-list:ok");

    await request(baseUrl, "POST", "/api/staff-accounts", {
      fullName: "Smoke Test Staff",
      email: `staff-${marker}@example.test`,
      barangay: "San Isidro",
      password: "paperbriq123",
      creatorRole: "admin"
    }, 201);
    results.push("staff-create:ok");

    await request(baseUrl, "POST", "/api/users/profile-image", {
      email: `staff-${marker}@example.test`,
      role: "barangay_staff",
      imageData: "data:image/png;base64,iVBORw0KGgo="
    }, 200);
    results.push("profile-image:ok");

    const getEndpoints = [
      "/api/summary",
      "/api/process",
      "/api/contributions",
      "/api/rewards",
      "/api/history",
      "/api/settings",
      "/api/qr-scans"
    ];

    for (const endpoint of getEndpoints) {
      await request(baseUrl, "GET", endpoint, null, 200);
      results.push(`${endpoint}:ok`);
    }

    const createdReward = await request(baseUrl, "POST", "/api/rewards", {
      name: `Smoke Reward ${marker}`,
      category: "Barangay Goods",
      pointsCost: 75,
      stockQuantity: 5,
      description: "Backend smoke test reward item",
      imageUrl: "",
      creatorRole: "barangay_staff",
      creatorName: "Smoke Test Staff"
    }, 201);
    results.push("reward-create:ok");

    const reports = await request(baseUrl, "GET", "/api/reports", null, 200);
    if (!reports.generatedAt || !reports.cards?.length || !Array.isArray(reports.charts?.paperCollected)) {
      throw new Error("/api/reports did not include database report metadata.");
    }
    if (!reports.cards.every((card) => typeof card.percent === "number") || !reports.chartMeta?.source) {
      throw new Error("/api/reports did not include graph percent/source metadata.");
    }
    results.push("/api/reports:ok");

    const dashboard = await request(baseUrl, "GET", "/api/dashboard", null, 200);
    if (!dashboard.generatedAt || !dashboard.summary?.generatedAt || typeof dashboard.overallProgress !== "number") {
      throw new Error("/api/dashboard did not include live dashboard metadata.");
    }
    results.push("/api/dashboard:ok");

    const iot = await request(baseUrl, "GET", "/api/iot", null, 200);
    if (!iot.generatedAt || !iot.sensors?.some((sensor) => Array.isArray(sensor.trend))) {
      throw new Error("/api/iot did not include live telemetry metadata.");
    }
    results.push("/api/iot:ok");

    const liveSnapshot = await requestText(baseUrl, "/api/live?channels=dashboard,reports,iot&once=1", 200, "event: snapshot");
    if (!liveSnapshot.includes('"dashboard"') || !liveSnapshot.includes('"reports"') || !liveSnapshot.includes('"iot"')) {
      throw new Error("/api/live did not include all requested live channels.");
    }
    results.push("/api/live:ok");

    await request(baseUrl, "POST", "/api/process/actions", { action: "start", source: marker }, 201);
    results.push("process-action:ok");

    await request(baseUrl, "POST", "/api/rewards/redeem", {
      rewardId: createdReward.reward.id,
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
