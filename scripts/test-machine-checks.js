const assert = require("node:assert/strict");
const { test } = require("node:test");
const { normalizeMachineHealth, buildMachineChecks, MAX_REPORT_AGE_MS } = require("../backend/src/db/machineChecks");

const now = Date.parse("2026-10-24T12:00:00Z");
const healthy = { version: 1, unoLink: true, controlIo: true, stopClosed: true,
  emergencyClosed: true, indicatorIo: true, armed: true, dht: true };
const chamberHealthy = {...healthy, version: 2, doorClosed: true, fanOn: false, unlockOn: false, testActive: false};
const control = { device: { connected: true }, processState: "idle", commands: [] };
const report = (health = healthy, recordedAt = new Date(now).toISOString()) => ({ health, recordedAt });

test("only complete, fresh evidence passes", () => {
  assert.equal(buildMachineChecks(report(), control, now).passed, true);
  for (const key of Object.keys(healthy).filter(k => k !== "version")) {
    assert.equal(buildMachineChecks(report({ ...healthy, [key]: false }), control, now).passed, false, key);
    assert.equal(buildMachineChecks(report({ ...healthy, [key]: undefined }), control, now).passed, false, key);
    assert.equal(buildMachineChecks(report({ ...healthy, [key]: "true" }), control, now).passed, false, key);
  }
  assert.equal(normalizeMachineHealth({ version: 99 }), null);
  assert.equal(buildMachineChecks(report({ ...healthy, version: 2 }), control, now).passed, false);
});

test("missing, invalid, future and expired reports cannot pass", () => {
  for (const entry of [null, report(null), report(healthy, "invalid"), report(healthy, new Date(now + 1).toISOString()),
    report(healthy, new Date(now - MAX_REPORT_AGE_MS - 1).toISOString())]) {
    assert.equal(buildMachineChecks(entry, control, now).passed, false);
  }
  assert.equal(buildMachineChecks(report(), control, now + 1000).validForMs, MAX_REPORT_AGE_MS - 1000);
});

test("component tests require supported, complete, idle chamber evidence", () => {
  assert.equal(buildMachineChecks(report(), control, now).componentTestsSupported, false);
  assert.equal(buildMachineChecks(report(chamberHealthy), control, now).passed, true);
  for (const [key, value] of [['doorClosed',false],['fanOn',true],['unlockOn',true],['testActive',true]]) {
    assert.equal(buildMachineChecks(report({...chamberHealthy,[key]:value}), control, now).passed, false);
    assert.equal(buildMachineChecks(report({...chamberHealthy,[key]:undefined}), control, now).passed, false);
  }
});

test("disconnected boards do not produce misleading component passes", () => {
  const offline = buildMachineChecks(report(), { ...control, device: { connected: false } }, now);
  assert.equal(offline.passed, false);
  assert.equal(offline.items.find(i => i.id === "stopClosed").status, "unknown");
  const noButtons = buildMachineChecks(report({ ...healthy, controlIo: false }), control, now);
  assert.equal(noButtons.items.find(i => i.id === "emergencyClosed").status, "unknown");
});

test("active commands and unsafe process states prevent readiness", () => {
  for (const processState of ["running", "starting", "stopping", "resetting", "fault", "", "unknown"]) {
    assert.equal(buildMachineChecks(report(), { ...control, processState }, now).passed, false);
  }
  for (const status of ["queued", "delivered"]) {
    assert.equal(buildMachineChecks(report(), { ...control, commands: [{ status: "completed" }, { status }] }, now).passed, false);
  }
});

test("HTTP health reporting stays read-only, device-specific, and clears old passes", async () => {
  // Explicitly isolate the test from .env and production databases/devices.
  process.env.DATABASE_URL = "";
  process.env.DB_CLIENT = "none";
  process.env.IOT_API_KEY = "local-health-test";
  process.env.CONTROL_API_KEY = "local-control-test";
  process.env.REMOTE_CONTROL_ENABLED = "true";
  const { createServer } = require("../backend/src/server/createServer");
  const { dataStore } = require("../backend/src/db");
  assert.equal(dataStore.configured, false);
  const server = createServer();
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const deviceId = "check-test-device";
  const request = async (path, body, key = "local-health-test", controlKey) => {
    const response = await fetch(base + path, { method: body ? "POST" : "GET",
      headers: { "Content-Type": "application/json", "X-API-Key": key, ...(controlKey ? {"X-Control-Key":controlKey} : {}) }, body: body ? JSON.stringify(body) : undefined });
    return { status: response.status, body: await response.json() };
  };
  const read = async () => (await request(`/api/device/control-status?deviceId=${deviceId}`)).body;
  const telemetry = checks => ({ deviceId, readings: [{ name: "IR Sensor", value: 0, unit: "digital" }], checks });
  try {
    const initial = await read();
    assert.equal(initial.machineChecks.passed, false);
    assert.equal(initial.processRun.source, "controller_events");
    assert.equal(initial.processRun.hasRun, false);
    assert.equal((await request("/api/iot/readings", telemetry(healthy), "wrong-key")).status, 401);
    assert.equal(await dataStore.getMachineHealth(deviceId), null);
    await request(`/api/device/commands/next?deviceId=${deviceId}`); // Presence only; empty queue.
    assert.equal((await request("/api/iot/readings", telemetry(healthy))).status, 202);
    assert.equal((await read()).machineChecks.passed, true);
    const other = (await request("/api/device/control-status?deviceId=another-device")).body;
    assert.equal(other.machineChecks.passed, false);
    for (const flag of ["dht", "unoLink", "emergencyClosed"]) {
      await request("/api/iot/readings", telemetry({ ...healthy, [flag]: false }));
      assert.equal((await read()).machineChecks.passed, false);
    }
    // An old firmware upload must invalidate the previous version's evidence.
    await request("/api/iot/readings", telemetry(undefined));
    const final = await read();
    assert.equal(final.machineChecks.passed, false);
    assert.equal(final.commands.length, 0);
    assert.equal(final.events.length, 0);
    assert.equal(final.processState, "idle");
    const batchCommand = {deviceId, command: 'batch_2', supervised: true};
    const sendBatch = body => request('/api/device/commands', body, 'local-health-test', 'local-control-test');
    assert.equal((await request('/api/device/commands', batchCommand)).status, 401);
    assert.equal((await sendBatch(batchCommand)).status, 409);
    await request('/api/iot/readings', telemetry({...chamberHealthy, version: 3, batchMask: 7, doorClosed: false}));
    assert.equal((await sendBatch(batchCommand)).status, 409);
    await request('/api/iot/readings', telemetry({...chamberHealthy, version: 3, batchMask: 6}));
    assert.deepEqual((await read()).batchControls.available, [2, 3]);
    assert.equal((await sendBatch({...batchCommand, command: 'batch_1'})).status, 409);
    assert.equal((await sendBatch({...batchCommand, supervised: false})).status, 409);
    const batchQueued = await sendBatch(batchCommand);
    assert.equal(batchQueued.status, 201);
    assert.equal((await sendBatch(batchCommand)).status, 409);
    const batchId = batchQueued.body.command.id;
    await request(`/api/device/commands/next?deviceId=${deviceId}`);
    const batchAck = await request(`/api/device/commands/${batchId}/ack`, {deviceId, status: 'completed', message: 'started'});
    assert.equal(batchAck.status, 200);
    await request('/api/iot/readings', telemetry(undefined));
    const command = {deviceId, command:'self_test', unloadedSupervised:true};
    const send = body => request('/api/device/commands', body, 'local-health-test', 'local-control-test');
    assert.equal((await request('/api/device/commands',command)).status,401);
    assert.equal((await send(command)).status,409); // Missing health.
    await request('/api/iot/readings',telemetry(healthy));
    assert.equal((await send(command)).status,409); // Old firmware.
    await request('/api/iot/readings',telemetry({...chamberHealthy,doorClosed:false}));
    assert.equal((await send(command)).status,409);
    await request('/api/iot/readings',telemetry(chamberHealthy));
    assert.equal((await send({...command,unloadedSupervised:false})).status,409);
    const queued = await send(command);
    assert.equal(queued.status,201);
    assert.equal(Date.parse(queued.body.command.expiresAt)-Date.parse(queued.body.command.createdAt),15000);
    assert.equal((await read()).componentTests.state,'queued');
    assert.equal((await send(command)).status,409); // A pending test prevents a second one.
    const id = queued.body.command.id;
    const postEvent = (type,component) => request('/api/device/events',{
      deviceId,type,component,severity:'info',message:type,commandId:id,processState:'running'
    });
    await postEvent('test_started','diagnostics');
    await new Promise(resolve=>setTimeout(resolve,5));
    await postEvent('test_item_started','shredder');
    await new Promise(resolve=>setTimeout(resolve,5));
    await postEvent('test_item_done','shredder');
    assert.equal((await read()).componentTests.items[0].checked,true);
    assert.equal((await read()).componentTests.items[0].physicalVerified,false);
    const reportSheets = async () => (await request("/api/reports")).body.monthly;
    const completedBatch = {deviceId, type: "batch_completed", component: "process", message: "Batch 1",
      processState: "running"};
    assert.equal((await reportSheets()).thisMonth.paperSheets, 0);
    assert.equal((await request("/api/device/events", completedBatch)).status, 400);
    const batchOneEvent = {...completedBatch, eventId: "batch-test-1"};
    assert.equal((await request("/api/device/events", batchOneEvent)).status, 202);
    assert.equal((await reportSheets()).thisMonth.paperSheets, 4);
    assert.equal((await request("/api/device/events", batchOneEvent)).status, 202);
    assert.equal((await reportSheets()).thisMonth.paperSheets, 4);
    const batchThreeEvent = {...completedBatch, message: "Batch 3", eventId: "batch-test-3"};
    assert.equal((await request("/api/device/events", batchThreeEvent)).status, 202);
    const monthly = await reportSheets();
    assert.equal(monthly.thisMonth.paperSheets, 8);
    assert.equal(monthly.thisMonth.weeks.reduce((sum, week) => sum + week.paperSheets, 0), 8);
    assert.equal(monthly.lastMonth.paperSheets, 0);
  } finally {
    await new Promise(resolve => server.close(resolve));
    await dataStore.close();
  }
});
