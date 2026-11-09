const { timingSafeEqual } = require("crypto");
const { normalizeRole } = require("../config/roles");
const { dataStore } = require("../db");
const { normalizeMachineHealth, buildMachineChecks } = require("../db/machineChecks");
const { buildMachineProcess } = require("../db/machineProcess");
const { buildComponentTests } = require("../db/componentTests");
const { encodeTimedCommand, buildTimedControls } = require("../db/timedControls");
const { validateSettings, buildProductionControls, encodeProductionCommand } = require('../db/productionControls');
const { readJson, sendJson } = require("./json");
const { streamLive } = require("./liveStream");
const { getReportsWithMonths } = require("../data/monthlyReports");
const { getOperator, operatorConfigured, replaceOperatorSession, isSessionWrite, secureTransport } = require("./operatorSessions");
const { requestMachineKey, approveMachineKey, redeemMachineKey, validPhoneApproval } = require("./machineKeyRequests");

const MACHINE_DEVICE_ID = "paperbriq-esp32";

async function routeApi(req, res, requestUrl) {
  if (req.method === 'POST' && requestUrl.pathname === '/api/device/production-settings') {
    const body = await readJson(req), deviceId = normalizeDeviceId(body.deviceId);
    if (!deviceId) { sendJson(res,400,{error:'A valid deviceId is required.'}); return; }
    try {
      const denied = await requireControlAccess(req, deviceId);
      if (denied) { sendJson(res, denied.status, {error:denied.message}); return; }
      const settings = validateSettings(body);
      const latest = await dataStore.getMachineHealth(deviceId);
      const maximum = latest?.health?.production?.dryingMaxSeconds;
      if (Number.isInteger(maximum) && maximum > 0 && settings.dryingSeconds > maximum) {
        sendJson(res,400,{error:`Maximum drying duration is ${maximum} seconds.`}); return;
      }
      sendJson(res,200,{ settings:await dataStore.saveProductionSettings(deviceId,settings) });
    }
    catch(error) { sendStoreError(res,error,'Unable to save production settings.'); }
    return;
  }
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

  if (req.method === "POST" && requestUrl.pathname === "/api/device/commands") {
    const body = await readJson(req);
    const deviceId = normalizeDeviceId(body.deviceId);
    let command = String(body.command || "").trim().toLowerCase();
    const timed = command === "heater" || command === "pump";
    const productionCommand = ['pump_arm','drying_start','drying_time','preheat_start'].includes(command);
    const idempotencyKey = normalizeOptionalToken(req.headers["idempotency-key"], 120);
    if (!deviceId) {
      sendJson(res, 400, { error: "A valid deviceId is required." });
      return;
    }
    if (!new Set(["start", "stop", "reset", "self_test", "heater", "pump"]).has(command) && !productionCommand) {
      sendJson(res, 400, { error: "Unsupported machine command." });
      return;
    }
    if (timed) {
      try { encodeTimedCommand(command, body.durationSeconds); }
      catch (error) { sendJson(res, 400, { error: error.message }); return; }
    }
    if (req.headers["idempotency-key"] && !idempotencyKey) {
      sendJson(res, 400, { error: "Idempotency-Key contains unsupported characters or is too long." });
      return;
    }

    try {
      const accessError = await requireControlAccess(req, deviceId);
      if (accessError) {
        sendJson(res, accessError.status, { error: accessError.message });
        return;
      }
      if (productionCommand) {
        const status = await dataStore.getDeviceControlStatus(deviceId, new Date(Date.now() - deviceOnlineSeconds() * 1000));
        const controls = buildProductionControls(await dataStore.getMachineHealth(deviceId), status, await dataStore.getProductionSettings(deviceId));
        command = encodeProductionCommand(command, body, controls);
      }
      if (timed) {
        const status = await dataStore.getDeviceControlStatus(deviceId, new Date(Date.now() - deviceOnlineSeconds() * 1000));
        const controls = buildTimedControls(await dataStore.getMachineHealth(deviceId), status);
        if (body.supervised !== true || !controls[command].available) {
          sendJson(res, 409, { error: controls[command].reason || 'Remain beside the machine during this short run.' }); return;
        }
        command = encodeTimedCommand(command, body.durationSeconds);
      }
      if (command === "self_test") {
        const status = await dataStore.getDeviceControlStatus(deviceId, new Date(Date.now() - deviceOnlineSeconds() * 1000));
        const checks = buildMachineChecks(await dataStore.getMachineHealth(deviceId), status);
        if (body.unloadedSupervised !== true || !checks.passed || !checks.componentTestsSupported) {
          sendJson(res, 409, { error: "Confirm unloaded supervision, close the door, Reset, and refresh machine checks before testing." });
          return;
        }
      }
      const commandRecord = await dataStore.createDeviceCommand({
        deviceId,
        command,
        idempotencyKey,
        ttlSeconds: timed || productionCommand ? 5 : command === "self_test" ? 15 : deviceCommandTtlSeconds()
      });
      sendJson(res, 201, { command: commandRecord });
    } catch (error) {
      sendStoreError(res, error, "Unable to queue device command.");
    }
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/device/commands/next") {
    const accessError = requireIotAccess(req);
    if (accessError) {
      sendJson(res, accessError.status, { error: accessError.message });
      return;
    }
    const deviceId = normalizeDeviceId(requestUrl.searchParams.get("deviceId"));
    if (!deviceId) {
      sendJson(res, 400, { error: "A valid deviceId query parameter is required." });
      return;
    }
    try {
      sendJson(res, 200, { command: await dataStore.getNextDeviceCommand(deviceId) });
    } catch (error) {
      sendStoreError(res, error, "Unable to read the device command queue.");
    }
    return;
  }

  const commandAckMatch = requestUrl.pathname.match(
    /^\/api\/device\/commands\/([0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})\/ack$/i
  );
  if (req.method === "POST" && commandAckMatch) {
    const accessError = requireIotAccess(req);
    if (accessError) {
      sendJson(res, accessError.status, { error: accessError.message });
      return;
    }
    const body = await readJson(req);
    const deviceId = normalizeDeviceId(body.deviceId);
    const status = String(body.status || "").trim().toLowerCase();
    const message = String(body.message || "").trim().slice(0, 240);
    if (!deviceId) {
      sendJson(res, 400, { error: "A valid deviceId is required." });
      return;
    }
    if (!new Set(["delivered", "completed", "failed"]).has(status)) {
      sendJson(res, 400, { error: "status must be delivered, completed, or failed." });
      return;
    }
    try {
      const command = await dataStore.acknowledgeDeviceCommand(commandAckMatch[1], {
        deviceId,
        status,
        message
      });
      sendJson(res, 200, { command });
    } catch (error) {
      sendStoreError(res, error, "Unable to acknowledge device command.");
    }
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/device/events") {
    const accessError = requireIotAccess(req);
    if (accessError) {
      sendJson(res, accessError.status, { error: accessError.message });
      return;
    }
    const body = await readJson(req);
    const event = normalizeDeviceEvent(body);
    if (event.error) {
      sendJson(res, 400, { error: event.error });
      return;
    }
    try {
      sendJson(res, 202, { accepted: true, event: await dataStore.recordDeviceEvent(event.value) });
    } catch (error) {
      sendStoreError(res, error, "Unable to record device event.");
    }
    return;
  }

  if (req.method === "GET" && requestUrl.pathname === "/api/device/control-status") {
    const deviceId = normalizeDeviceId(requestUrl.searchParams.get("deviceId"));
    if (!deviceId) {
      sendJson(res, 400, { error: "A valid deviceId query parameter is required." });
      return;
    }
    const controlKey = String(process.env.CONTROL_API_KEY || "").trim();
    const configured = Boolean(controlKey) || operatorConfigured(dataStore);
    const enabled = remoteControlFlagEnabled() && configured;
    const onlineAfter = new Date(Date.now() - deviceOnlineSeconds() * 1000);
    try {
      const status = await dataStore.getDeviceControlStatus(deviceId, onlineAfter);
      const healthReport = await dataStore.getMachineHealth(deviceId);
      res.setHeader("Cache-Control", "no-store");
      sendJson(res, 200, {
        enabled,
        configured,
        temporaryMachineKeysSupported: true,
        machineKeyRequestsSupported: enabled && Boolean(controlKey) && !dataStore.configured,
        operatorAuthorized: enabled && deviceId === MACHINE_DEVICE_ID && Boolean(getOperator(req, dataStore)),
        machineKeyAuthorized: enabled && deviceId === MACHINE_DEVICE_ID && secretsMatch(controlKey, req.headers["x-control-key"]),
        ...status,
        processRun: buildMachineProcess(status),
        componentTests: buildComponentTests(status),
        machineChecks: buildMachineChecks(healthReport, status),
        timedControls: buildTimedControls(healthReport, status),
        productionControls: buildProductionControls(healthReport, status, await dataStore.getProductionSettings(deviceId)),
        generatedAt: new Date().toISOString()
      });
    } catch (error) {
      sendStoreError(res, error, "Unable to read device control status.");
    }
    return;
  }

  const machineKeyAction = /^\/api\/auth\/machine-key\/(request|approve|redeem)$/.exec(requestUrl.pathname)?.[1];
  if (req.method === "POST" && machineKeyAction) {
    res.setHeader("Cache-Control", "no-store");
    const key = String(process.env.CONTROL_API_KEY || "").trim();
    if (!secureTransport(req) || !isSessionWrite(req)) {
      sendJson(res, 403, { error: "Machine key requests require a secure connection." });
      return;
    }
    if (!key || dataStore.configured || !remoteControlFlagEnabled()) {
      sendJson(res, 403, { error: "Machine key requests are unavailable on this server." });
      return;
    }
    if (machineKeyAction === "approve" && !secretsMatch(key, req.headers["x-control-key"])) {
      sendJson(res, 401, { error: "Machine administrator approval is required." });
      return;
    }
    const body = await readJson(req);
    if (machineKeyAction === "request") {
      const request = requestMachineKey();
      sendJson(res, request ? 201 : 429, request || { error: "Too many requests. Try again in five minutes." });
      return;
    }
    const { code, requestId } = body;
    if (typeof code !== "string" || !/^\d{6}$/.test(code)) {
      sendJson(res, 400, { error: "Enter the six-digit machine key." });
      return;
    }
    if (machineKeyAction === "approve") {
      const approved = approveMachineKey(key, code);
      sendJson(res, approved ? 200 : 404, approved ? { ok: true } : { error: "No active request for that key." });
      return;
    }
    const { status, ...result } = redeemMachineKey(key, requestId, code);
    sendJson(res, status, result);
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/auth/login") {
    const body = await readJson(req);
    const email = String(body.email || "").trim().toLowerCase();
    const password = String(body.password || "");
    const requestedRole = String(body.role || "").trim();
    const role = requestedRole ? normalizeRole(requestedRole) : null;

    if (!email || !password) {
      sendJson(res, 400, { error: "Email and password are required." });
      return;
    }
    if (requestedRole && !role) {
      sendJson(res, 400, { error: "A valid role is required when a role is supplied." });
      return;
    }

    const user = await dataStore.login(email, password, role);

    if (!user) {
      sendJson(res, 401, { error: "Invalid PaperBriq credentials." });
      return;
    }

    const summary = await dataStore.getSummary();
    res.setHeader("Cache-Control", "no-store");
    const controlKey = String(process.env.CONTROL_API_KEY || "").trim();
    const demoEnrollmentVerified = remoteControlFlagEnabled() &&
      (validPhoneApproval(controlKey, req.headers["x-phone-approval"]) ||
        secretsMatch(controlKey, req.headers["x-control-key"]));
    replaceOperatorSession(req, res, dataStore, user, demoEnrollmentVerified);
    sendJson(res, 200, { user, summary });
    return;
  }

  if (req.method === "POST" && requestUrl.pathname === "/api/auth/logout") {
    if (!isSessionWrite(req)) {
      sendJson(res, 403, { error: "Sign-out must come from the app." });
      return;
    }
    res.setHeader("Cache-Control", "no-store");
    replaceOperatorSession(req, res, dataStore, null);
    sendJson(res, 200, { ok: true });
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

    const stored = await dataStore.recordIotTelemetry(telemetry);
    await dataStore.recordMachineHealth(telemetry.deviceId, normalizeMachineHealth(body.checks));
    sendJson(res, 202, { accepted: true, telemetry: stored });
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
    sendJson(res, 200, await getReportsWithMonths(dataStore));
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

async function requireControlAccess(req, deviceId) {
  if (!remoteControlFlagEnabled()) {
    return { status: 403, message: "Remote device control is disabled." };
  }
  const configuredKey = String(process.env.CONTROL_API_KEY || "").trim();
  if (secretsMatch(configuredKey, req.headers["x-control-key"])) return null;
  const operator = getOperator(req, dataStore);
  if (!operator) {
    return { status: 401, message: "Sign in again with an approved machine operator account." };
  }
  if (deviceId !== MACHINE_DEVICE_ID || !isSessionWrite(req)) {
    return { status: 403, message: "This session cannot control the requested machine." };
  }
  if (operator.demoEnrollment) return null;
  const current = await dataStore.getMachineOperator(operator.id);
  if (current?.role !== operator.role) {
    return { status: 403, message: "Machine access for this account has changed." };
  }
  return null;
}

function requireIotAccess(req) {
  const configuredKey = String(process.env.IOT_API_KEY || "").trim();
  if (!configuredKey) {
    return { status: 503, message: "IoT API key is not configured." };
  }
  if (!secretsMatch(configuredKey, req.headers["x-api-key"])) {
    return { status: 401, message: "Invalid IoT API key." };
  }
  return null;
}

function secretsMatch(expected, supplied) {
  const expectedBuffer = Buffer.from(String(expected || ""));
  const suppliedBuffer = Buffer.from(String(supplied || ""));
  return expectedBuffer.length > 0 &&
    expectedBuffer.length === suppliedBuffer.length &&
    timingSafeEqual(expectedBuffer, suppliedBuffer);
}

function remoteControlFlagEnabled() {
  return String(process.env.REMOTE_CONTROL_ENABLED || "").trim().toLowerCase() === "true";
}

function deviceCommandTtlSeconds() {
  const configured = Number(process.env.DEVICE_COMMAND_TTL_SECONDS || 120);
  return Number.isFinite(configured) ? Math.min(3600, Math.max(10, Math.round(configured))) : 120;
}

function deviceOnlineSeconds() {
  const configured = Number(process.env.DEVICE_ONLINE_SECONDS || 60);
  return Number.isFinite(configured) ? Math.min(600, Math.max(10, Math.round(configured))) : 60;
}

function normalizeDeviceId(value) {
  const deviceId = String(value || "").trim();
  return /^[A-Za-z0-9][A-Za-z0-9._:-]{0,79}$/.test(deviceId) ? deviceId : "";
}

function normalizeOptionalToken(value, maxLength) {
  if (value === undefined || value === null || value === "") return null;
  const token = String(value).trim();
  const pattern = new RegExp(`^[A-Za-z0-9][A-Za-z0-9._:-]{0,${maxLength - 1}}$`);
  return pattern.test(token) ? token : null;
}

function normalizeDeviceEvent(body) {
  const deviceId = normalizeDeviceId(body.deviceId);
  const eventKey = normalizeOptionalToken(body.eventId, 120);
  const type = String(body.type || "").trim().toLowerCase();
  const severity = String(body.severity || "info").trim().toLowerCase();
  const component = String(body.component || "system").trim().toLowerCase();
  const message = String(body.message || "").trim();
  const processState = body.processState === undefined || body.processState === null
    ? null
    : String(body.processState).trim().toLowerCase();
  const commandId = body.commandId === undefined || body.commandId === null || body.commandId === ""
    ? null
    : String(body.commandId).trim().toLowerCase();
  const metadata = body.metadata === undefined ? {} : body.metadata;

  if (!deviceId) return { error: "A valid deviceId is required." };
  if (body.eventId && !eventKey) return { error: "eventId contains unsupported characters or is too long." };
  if (!/^[a-z][a-z0-9_]{1,63}$/.test(type)) {
    return { error: "type must be 2-64 lowercase letters, numbers, or underscores." };
  }
  if (!new Set(["info", "warning", "error", "critical"]).has(severity)) {
    return { error: "severity must be info, warning, error, or critical." };
  }
  if (!/^[a-z][a-z0-9_-]{0,63}$/.test(component)) {
    return { error: "component contains unsupported characters or is too long." };
  }
  if (!message || message.length > 500) return { error: "message must be 1-500 characters." };
  if (processState && !new Set(["idle", "starting", "running", "stopping", "stopped", "fault", "resetting"]).has(processState)) {
    return { error: "processState is invalid." };
  }
  if (commandId && !/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(commandId)) {
    return { error: "commandId must be a UUID." };
  }
  if (!isPlainObject(metadata) || Buffer.byteLength(JSON.stringify(metadata), "utf8") > 4096) {
    return { error: "metadata must be a JSON object no larger than 4 KB." };
  }

  return {
    value: {
      deviceId,
      eventKey,
      type,
      severity,
      component,
      message,
      processState,
      commandId,
      metadata
    }
  };
}

function sendStoreError(res, error, fallbackMessage) {
  const status = Number(error?.statusCode || 500);
  if (status >= 500) console.error(fallbackMessage, error);
  sendJson(res, status, { error: error?.publicMessage || fallbackMessage });
}

module.exports = {
  routeApi
};
