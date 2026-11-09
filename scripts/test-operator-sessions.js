const { test } = require("node:test");
const assert = require("node:assert/strict");

test("machine sign-in requires an approved active operator and stays scoped to one machine", async () => {
  process.env.DATABASE_URL = "";
  process.env.DB_CLIENT = "none";
  process.env.CONTROL_API_KEY = "";
  process.env.REMOTE_CONTROL_ENABLED = "true";
  process.env.MACHINE_OPERATOR_IDS = "operator-1";
  const { dataStore } = require("../backend/src/db");
  const { createServer } = require("../backend/src/server/createServer");
  const approved = { id: "operator-1", role: "admin" };
  const staff = { id: "staff-1", role: "barangay_staff" };
  let currentOperator = approved;
  dataStore.login = async (email) => email === "operator@example.test" ? approved :
    email === "demo@example.test" ? { id: "demo-admin", role: "admin" } : staff;
  dataStore.getMachineOperator = async () => currentOperator;
  const server = createServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const post = (path, data, headers = {}) => fetch(base + path, {
    method: "POST",
    headers: { "Content-Type": "application/json", ...headers },
    body: JSON.stringify(data)
  });
  const login = (email, headers) => post("/api/auth/login", { email, password: "test-fixture" }, headers);
  const stop = (headers, deviceId = "paperbriq-esp32") => post("/api/device/commands", { command: "stop", deviceId }, headers);
  const controlStatus = async (headers) =>
    (await fetch(base + "/api/device/control-status?deviceId=paperbriq-esp32", { headers })).json();

  try {
    const demo = await login("operator@example.test");
    assert.match(demo.headers.get("set-cookie"), /Max-Age=0/);
    dataStore.configured = true;
    const denied = await login("staff@example.test");
    assert.match(denied.headers.get("set-cookie"), /Max-Age=0/);
    assert.equal((await stop({})).status, 401);

    const accepted = await login("operator@example.test", { "X-Forwarded-Proto": "https" });
    const setCookie = accepted.headers.get("set-cookie");
    assert.match(setCookie, /HttpOnly; SameSite=Strict; Secure/);
    assert.doesNotMatch(JSON.stringify(await accepted.json()), /paperbriqOperator/);
    const headers = { Cookie: setCookie.split(";")[0] };
    assert.equal((await controlStatus(headers)).temporaryMachineKeysSupported, true);
    assert.equal((await controlStatus(headers)).operatorAuthorized, true);
    assert.equal((await controlStatus({})).operatorAuthorized, false);
    assert.equal((await controlStatus({})).machineKeyAuthorized, false);
    assert.equal((await stop({ ...headers, "Sec-Fetch-Site": "same-site" })).status, 403);
    assert.equal((await stop(headers, "other-machine")).status, 403);
    assert.equal((await stop(headers)).status, 201);
    const settings = await post("/api/device/production-settings", { deviceId: "paperbriq-esp32", dryingSeconds: 120 }, headers);
    assert.equal(settings.status, 200);

    currentOperator = null;
    assert.equal((await stop(headers)).status, 403);
    currentOperator = approved;
    process.env.REMOTE_CONTROL_ENABLED = "false";
    assert.equal((await stop(headers)).status, 403);
    process.env.REMOTE_CONTROL_ENABLED = "true";
    assert.equal((await post("/api/auth/logout", {}, headers)).status, 200);
    assert.equal((await stop(headers)).status, 401);

    const renewed = await login("operator@example.test");
    const renewedHeaders = { Cookie: renewed.headers.get("set-cookie").split(";")[0] };
    const originalNow = Date.now;
    try {
      Date.now = () => originalNow() + 8 * 60 * 60 * 1000;
      assert.equal((await stop(renewedHeaders)).status, 401);
    } finally {
      Date.now = originalNow;
    }
    dataStore.configured = false;
    process.env.CONTROL_API_KEY = "local-machine-test-key";
    const machineKey = { "X-Control-Key": process.env.CONTROL_API_KEY };
    assert.equal((await controlStatus(machineKey)).machineKeyAuthorized, true);
    assert.equal((await controlStatus({ "X-Control-Key": "wrong" })).machineKeyAuthorized, false);
    const otherMachine = await fetch(base + "/api/device/control-status?deviceId=other-machine", { headers: machineKey });
    assert.equal((await otherMachine.json()).machineKeyAuthorized, false);
    process.env.REMOTE_CONTROL_ENABLED = "false";
    assert.equal((await controlStatus(machineKey)).machineKeyAuthorized, false);
    process.env.REMOTE_CONTROL_ENABLED = "true";
    const https = { "X-Forwarded-Proto": "https" };
    assert.match((await login("demo@example.test", https)).headers.get("set-cookie"), /Max-Age=0/);
    assert.match((await login("demo@example.test", machineKey)).headers.get("set-cookie"), /Max-Age=0/);
    assert.match((await login("staff@example.test", { ...https, ...machineKey })).headers.get("set-cookie"), /Max-Age=0/);
    const demoLogin = () => login("demo@example.test", { ...https, ...machineKey });
    const first = (await demoLogin()).headers.get("set-cookie").split(";")[0];
    const firstHeaders = { ...https, Cookie: first };
    assert.equal((await controlStatus(firstHeaders)).operatorAuthorized, true);
    assert.equal((await stop(firstHeaders, "other-machine")).status, 403);
    assert.equal((await stop(firstHeaders)).status, 201);
    assert.equal((await post("/api/auth/logout", {}, firstHeaders)).status, 200);
    assert.equal((await stop(firstHeaders)).status, 401);
    const second = (await demoLogin()).headers.get("set-cookie").split(";")[0];
    assert.notEqual(first, second);
    assert.equal((await controlStatus({ ...https, Cookie: second })).operatorAuthorized, true);
    process.env.CONTROL_API_KEY = "rotated-machine-test-key";
    assert.equal((await stop({ ...https, Cookie: second })).status, 401);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
});
