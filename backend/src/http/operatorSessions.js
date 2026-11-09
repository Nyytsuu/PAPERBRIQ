const { createHash, randomBytes } = require("crypto");

const COOKIE = "paperbriqOperator";
const SESSION_MS = 8 * 60 * 60 * 1000;
const sessions = new Map();

function operatorConfigured(store) {
  return store.configured && approvedIds().length > 0;
}

function approvedIds() {
  return String(process.env.MACHINE_OPERATOR_IDS || "").split(",").map((id) => id.trim()).filter(Boolean);
}

function eligibleOperator(user, store) {
  return operatorConfigured(store) && user &&
    ["admin", "barangay_staff"].includes(user.role) && approvedIds().includes(String(user.id));
}

function demoEnrollmentFingerprint(user, store) {
  const key = String(process.env.CONTROL_API_KEY || "").trim();
  return !store.configured && user?.id === "demo-admin" && user.role === "admin" && key
    ? createHash("sha256").update(key).digest("hex")
    : null;
}

function secureTransport(req) {
  return Boolean(req.socket.encrypted || req.headers["x-forwarded-proto"] === "https");
}

function allowedTransport(req) {
  return secureTransport(req) ||
    (/^(localhost|127\.0\.0\.1|10\.0\.2\.2|\[::1\])(:\d+)?$/.test(req.headers.host || "") &&
     ["127.0.0.1", "::1", "::ffff:127.0.0.1"].includes(req.socket.remoteAddress));
}

function isSessionWrite(req) {
  const site = req.headers["sec-fetch-site"];
  return (!site || site === "same-origin") &&
    /^application\/json(?:\s*;|$)/i.test(req.headers["content-type"] || "");
}

function sessionToken(req) {
  const match = String(req.headers.cookie || "").match(/(?:^|;\s*)paperbriqOperator=([a-f0-9]{64})(?=;|$)/);
  return match?.[1];
}

function getOperator(req, store) {
  if (!allowedTransport(req)) return null;
  const token = sessionToken(req);
  const operator = sessions.get(token);
  if (!operator) return null;
  const approved = operator.demoEnrollment
    ? secureTransport(req) && operator.demoEnrollment === demoEnrollmentFingerprint(operator, store)
    : eligibleOperator(operator, store);
  if (operator.expiresAt <= Date.now() || !approved) {
    sessions.delete(token);
    return null;
  }
  return operator;
}

function replaceOperatorSession(req, res, store, user, demoEnrollmentVerified = false) {
  if (!allowedTransport(req) || !isSessionWrite(req)) return;
  sessions.delete(sessionToken(req));
  const secure = secureTransport(req) ? "; Secure" : "";
  const attributes = `Path=/api; HttpOnly; SameSite=Strict${secure}`;
  const demoEnrollment = demoEnrollmentVerified && secureTransport(req)
    ? demoEnrollmentFingerprint(user, store) : null;
  if (!demoEnrollment && !eligibleOperator(user, store)) {
    res.setHeader("Set-Cookie", `${COOKIE}=; ${attributes}; Max-Age=0`);
    return;
  }
  const now = Date.now();
  for (const [token, session] of sessions) {
    if (session.expiresAt <= now || (demoEnrollment && session.demoEnrollment)) sessions.delete(token);
  }
  if (sessions.size >= 256) sessions.delete(sessions.keys().next().value);
  const token = randomBytes(32).toString("hex");
  sessions.set(token, { id: user.id, role: user.role, demoEnrollment, expiresAt: now + SESSION_MS });
  res.setHeader("Set-Cookie", `${COOKIE}=${token}; ${attributes}; Max-Age=${SESSION_MS / 1000}`);
}

module.exports = { getOperator, operatorConfigured, replaceOperatorSession, isSessionWrite, secureTransport };
