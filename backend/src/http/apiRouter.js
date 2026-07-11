const { normalizeRole } = require("../config/roles");
const { dataStore } = require("../db");
const { readJson, sendJson } = require("./json");

async function routeApi(req, res, requestUrl) {
  if (req.method === "GET" && requestUrl.pathname === "/api/health") {
    sendJson(res, 200, {
      ok: true,
      app: "PaperBriq",
      database: dataStore.configured ? dataStore.mode : "not configured",
      targetDevice: "Google Pixel 7 Android viewport"
    });
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/auth/login") {
    const body = await readJson(req);
    const email = String(body.email || "").trim().toLowerCase();
    const password = String(body.password || "");
    const role = normalizeRole(body.role);

    if (!email || !password || !role) {
      sendJson(res, 400, { error: "Email, password, and role are required." });
      return;
    }

    const user = await dataStore.login(email, password, role);

    if (!user) {
      sendJson(res, 401, { error: "Invalid PaperBriq credentials." });
      return;
    }

    sendJson(res, 200, { user, summary: await dataStore.getSummary() });
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/summary") {
    sendJson(res, 200, await dataStore.getSummary());
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/dashboard") {
    sendJson(res, 200, await dataStore.getDashboard());
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/process") {
    sendJson(res, 200, await dataStore.getProcess());
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/process/actions") {
    const body = await readJson(req);
    sendJson(res, 201, { action: await dataStore.createProcessAction(body) });
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/iot") {
    sendJson(res, 200, await dataStore.getIotMonitoring());
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/contributions") {
    sendJson(res, 200, await dataStore.getContributions());
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/rewards") {
    sendJson(res, 200, await dataStore.getRewards());
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/rewards/redeem") {
    const body = await readJson(req);
    if (!body.rewardId) {
      sendJson(res, 400, { error: "rewardId is required." });
      return;
    }
    sendJson(res, 201, { redemption: await dataStore.redeemReward(body) });
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/reports") {
    sendJson(res, 200, await dataStore.getReports());
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/history") {
    sendJson(res, 200, await dataStore.getHistory());
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/settings") {
    sendJson(res, 200, await dataStore.getSettings());
    return;
  }

  if (req.method === "PUT" && requestUrl.pathname === "/api/settings") {
    const body = await readJson(req);
    sendJson(res, 200, await dataStore.updateSettings(body));
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/qr-scans") {
    sendJson(res, 200, await dataStore.getQrScans());
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/qr-scans") {
    const body = await readJson(req);
    sendJson(res, 201, { scan: await dataStore.createQrScan(body) });
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/access-requests") {
    const body = await readJson(req);
    const request = {
      fullName: String(body.fullName || "").trim(),
      email: String(body.email || "").trim().toLowerCase(),
      requestedRole: normalizeRole(body.requestedRole),
      barangay: String(body.barangay || "").trim(),
      message: String(body.message || "").trim()
    };

    if (!request.fullName || !request.email || !request.requestedRole) {
      sendJson(res, 400, { error: "Name, email, and requested role are required." });
      return;
    }

    sendJson(res, 201, { request: await dataStore.createAccessRequest(request) });
    return;
  }

  sendJson(res, 404, { error: "API route not found." });
}

module.exports = {
  routeApi
};
