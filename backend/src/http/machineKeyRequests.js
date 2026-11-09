const { createHmac, randomBytes, randomInt, timingSafeEqual } = require("node:crypto");

const REQUEST_MS = 5 * 60 * 1000;
const requests = new Map();

function phoneSignature(key, id) {
  return createHmac("sha256", key).update("paperbriq-phone-approval:" + id).digest("hex");
}

function validPhoneApproval(key, receipt) {
  if (!key || typeof receipt !== "string" || !/^[a-f0-9]{64}\.[a-f0-9]{64}$/.test(receipt)) return false;
  const [id, signature] = receipt.split(".");
  return timingSafeEqual(Buffer.from(signature, "hex"), Buffer.from(phoneSignature(key, id), "hex"));
}

function removeExpiredRequests() {
  const now = Date.now();
  for (const [id, request] of requests) {
    if (request.expiresAt <= now) requests.delete(id);
  }
}

function requestMachineKey(owner = null) {
  removeExpiredRequests();
  if (owner) {
    for (const [id, request] of requests) {
      if (request.owner === owner) requests.delete(id);
    }
  }
  if (requests.size >= 64) return null;
  const requestId = randomBytes(32).toString("hex");
  const existingCodes = new Set(Array.from(requests.values(), request => request.code));
  let code;
  do {
    code = String(randomInt(0, 1000000)).padStart(6, "0");
  } while (existingCodes.has(code));
  requests.set(requestId, { code, owner, expiresAt: Date.now() + REQUEST_MS, attempts: 0 });
  return { requestId, code, expiresInSeconds: REQUEST_MS / 1000 };
}

function approveMachineKey(key, code) {
  removeExpiredRequests();
  for (const request of requests.values()) {
    if (request.owner || request.code !== code) continue;
    const id = randomBytes(32).toString("hex");
    request.receipt = id + "." + phoneSignature(key, id);
    return true;
  }
  return false;
}

function redeemMachineKey(key, requestId, code, owner = null) {
  removeExpiredRequests();
  const request = requests.get(requestId);
  if (!request || request.owner !== owner) {
    return { status: 404, error: "Request expired. Tap Request machine key again." };
  }
  if (request.code !== code) {
    request.attempts++;
    if (request.attempts >= 5) {
      requests.delete(requestId);
      return { status: 429, error: "Too many attempts. Request a new machine key." };
    }
    return { status: 400, error: "Paste the six-digit key shown on this phone." };
  }
  if (!owner && !validPhoneApproval(key, request.receipt)) {
    return { status: 409, error: "Waiting for machine administrator approval. Try again after approval." };
  }
  requests.delete(requestId);
  return owner ? { status: 200, approved: true } : { status: 200, receipt: request.receipt };
}

module.exports = { requestMachineKey, approveMachineKey, redeemMachineKey, validPhoneApproval };
