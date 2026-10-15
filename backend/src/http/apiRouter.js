const { normalizeRole } = require("../config/roles");
const { dataStore } = require("../db");
const { readJson, sendJson } = require("./json");
const { streamLive } = require("./liveStream");

async function routeApi(req, res, requestUrl) {
  if (req.method === "GET" && requestUrl.pathname === "/api/live") {
    await streamLive(req, res, requestUrl);
    return;
  }

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

  if (req.method === "POST" && requestUrl.pathname === "/api/auth/forgot-password") {
    const body = await readJson(req);
    const email = String(body.email || "").trim().toLowerCase();
    const role = normalizeRole(body.role);

    if (!email || !role) {
      sendJson(res, 400, { error: "Email and role are required." });
      return;
    }

    if (!email.includes("@")) {
      sendJson(res, 400, { error: "Enter a valid email address." });
      return;
    }

    sendJson(res, 202, {
      message: "If that account exists, password reset instructions will be sent."
    });
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/auth/signup") {
    const body = await readJson(req);
    const signup = {
      fullName: String(body.fullName || "").trim(),
      email: String(body.email || "").trim().toLowerCase(),
      password: String(body.password || ""),
      role: normalizeRole(body.role || "contributor"),
      barangay: String(body.barangay || "").trim(),
      phone: String(body.phone || "").trim()
    };

    if (!signup.fullName || !signup.email || !signup.password || !signup.role || !signup.barangay) {
      sendJson(res, 400, { error: "Name, email, password, role, and barangay are required." });
      return;
    }

    if (!signup.email.includes("@")) {
      sendJson(res, 400, { error: "Enter a valid email address." });
      return;
    }

    if (signup.password.length < 8) {
      sendJson(res, 400, { error: "Password must be at least 8 characters." });
      return;
    }

    if (signup.role !== "contributor") {
      sendJson(res, 403, { error: "Self signup is open for users only." });
      return;
    }

    try {
      const user = await dataStore.signup(signup);
      sendJson(res, 201, { user, summary: await dataStore.getSummary() });
    } catch (error) {
      sendJson(res, error.statusCode || 500, { error: error.publicMessage || "Unable to create account." });
    }
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/staff-accounts") {
    const creatorRole = normalizeRole(requestUrl.searchParams.get("creatorRole"));
    if (creatorRole !== "admin") {
      sendJson(res, 403, { error: "Only admins can view barangay staff accounts." });
      return;
    }

    sendJson(res, 200, { staffAccounts: await dataStore.getStaffAccounts() });
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/staff-accounts") {
    const body = await readJson(req);
    const creatorRole = normalizeRole(body.creatorRole);
    const staff = {
      fullName: String(body.fullName || "").trim(),
      email: String(body.email || "").trim().toLowerCase(),
      password: String(body.password || ""),
      barangay: String(body.barangay || "").trim()
    };

    if (creatorRole !== "admin") {
      sendJson(res, 403, { error: "Only admins can create barangay staff accounts." });
      return;
    }

    if (!staff.fullName || !staff.email || !staff.password || !staff.barangay) {
      sendJson(res, 400, { error: "Name, email, password, and barangay are required." });
      return;
    }

    if (!staff.email.includes("@")) {
      sendJson(res, 400, { error: "Enter a valid email address." });
      return;
    }

    if (staff.password.length < 8) {
      sendJson(res, 400, { error: "Password must be at least 8 characters." });
      return;
    }

    try {
      const account = await dataStore.createStaffAccount(staff);
      sendJson(res, 201, { staffAccount: account });
    } catch (error) {
      sendJson(res, error.statusCode || 500, { error: error.publicMessage || "Unable to create staff account." });
    }
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/users/profile-image") {
    const body = await readJson(req);
    const profile = {
      email: String(body.email || "").trim().toLowerCase(),
      role: normalizeRole(body.role),
      imageData: String(body.imageData || "").trim()
    };

    if (!profile.email || !profile.role || !profile.imageData) {
      sendJson(res, 400, { error: "Email, role, and profile image are required." });
      return;
    }

    if (profile.role !== "barangay_staff") {
      sendJson(res, 403, { error: "Profile image upload is available for barangay staff accounts." });
      return;
    }

    if (!/^data:image\/(png|jpe?g|webp);base64,/i.test(profile.imageData)) {
      sendJson(res, 400, { error: "Upload a PNG, JPG, or WebP image." });
      return;
    }

    if (profile.imageData.length > 900000) {
      sendJson(res, 413, { error: "Profile image must be under 650 KB." });
      return;
    }

    if (typeof dataStore.updateProfileImage !== "function") {
      sendJson(res, 501, { error: "Profile image upload is not available in this data mode." });
      return;
    }

    try {
      const user = await dataStore.updateProfileImage(profile);
      sendJson(res, 200, { user });
    } catch (error) {
      sendJson(res, error.statusCode || 500, { error: error.publicMessage || "Unable to update profile image." });
    }
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

  if (req.method === "POST" && requestUrl.pathname === "/api/iot/readings") {
    const configuredKey = String(process.env.IOT_API_KEY || "").trim();
    const suppliedKey = String(req.headers["x-api-key"] || "").trim();
    if (configuredKey && suppliedKey !== configuredKey) {
      sendJson(res, 401, { error: "Invalid IoT API key." });
      return;
    }

    const body = await readJson(req);
    const readings = Array.isArray(body.readings) ? body.readings : [];
    if (!String(body.deviceId || "").trim() || !readings.length) {
      sendJson(res, 400, { error: "deviceId and at least one sensor reading are required." });
      return;
    }

    const telemetry = {
      deviceId: String(body.deviceId).trim().slice(0, 80),
      readings: readings.slice(0, 20).map((reading) => ({
        name: String(reading.name || "").trim().slice(0, 120),
        value: Number(reading.value),
        unit: String(reading.unit || "").trim().slice(0, 24),
        status: String(reading.status || "Normal").trim().slice(0, 40)
      })).filter((reading) => reading.name && Number.isFinite(reading.value)),
      hardware: body.hardware && typeof body.hardware === "object" ? body.hardware : {}
    };

    if (!telemetry.readings.length) {
      sendJson(res, 400, { error: "No valid numeric sensor readings were supplied." });
      return;
    }

    sendJson(res, 202, { accepted: true, telemetry: await dataStore.recordIotTelemetry(telemetry) });
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

  if (req.method === "POST" && requestUrl.pathname === "/api/rewards") {
    const body = await readJson(req);
    const creatorRole = normalizeRole(body.creatorRole);
    const reward = {
      name: String(body.name || "").trim(),
      category: String(body.category || "Barangay Reward").trim(),
      description: String(body.description || "").trim(),
      pointsCost: Number(body.pointsCost || 0),
      stockQuantity: Number(body.stockQuantity || 0),
      imageUrl: String(body.imageUrl || "").trim(),
      creatorRole,
      creatorName: String(body.creatorName || "").trim()
    };

    if (!["admin", "barangay_staff"].includes(creatorRole)) {
      sendJson(res, 403, { error: "Only admin and barangay staff web accounts can add reward items." });
      return;
    }

    if (!reward.name || !reward.category || !reward.pointsCost) {
      sendJson(res, 400, { error: "Reward name, category, and points cost are required." });
      return;
    }

    if (reward.pointsCost < 1 || reward.stockQuantity < 0) {
      sendJson(res, 400, { error: "Points must be at least 1 and stock cannot be negative." });
      return;
    }

    sendJson(res, 201, { reward: await dataStore.createRewardItem(reward) });
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
    const currentSettings = await dataStore.getSettings();
    sendJson(res, 200, await dataStore.updateSettings(deepMerge(currentSettings, body)));
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/qr-scans") {
    sendJson(res, 200, await dataStore.getQrScans());
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/qr-scans") {
    const body = await readJson(req);
    if (!String(body.code || "").trim()) {
      sendJson(res, 400, { error: "Contributor QR code is required." });
      return;
    }
    const weightKg = Number(body.weightKg || 0);
    if (!Number.isFinite(weightKg) || weightKg <= 0) {
      sendJson(res, 400, { error: "Paper weight is required before points can be recorded." });
      return;
    }
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

function deepMerge(target, source) {
  if (!isPlainObject(target)) return isPlainObject(source) ? deepMerge({}, source) : source;
  if (!isPlainObject(source)) return source === undefined ? target : source;

  return Object.entries(source).reduce((merged, [key, value]) => {
    merged[key] = deepMerge(merged[key], value);
    return merged;
  }, { ...target });
}

function isPlainObject(value) {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

module.exports = {
  routeApi
};
