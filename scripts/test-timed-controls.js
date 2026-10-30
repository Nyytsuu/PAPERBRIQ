const { test } = require('node:test');
const assert = require('node:assert/strict');
const { encodeTimedCommand, buildTimedControls } = require('../backend/src/db/timedControls');
const now = Date.now();
const health = {version:3, unoLink:true, controlIo:true, indicatorIo:true, stopClosed:true,
  emergencyClosed:true, doorClosed:true, armed:false, fanOn:false, unlockOn:false, testActive:false,
  timedSupported:true, heaterOn:false, pumpOn:false, heaterReady:true, pumpReady:true};
const report = (h = health, time = now) => ({health:h, recordedAt:new Date(time).toISOString()});
const control = {device:{connected:true}, processState:'stopped', commands:[]};

test('strict duration limits reject coercion, fractions, missing values and overflow', () => {
  for (const action of ['heater','pump']) {
    const max = action === 'heater' ? 10 : 3;
    for (let n = 1; n <= max; ++n) assert.equal(encodeTimedCommand(action,n), `${action}:${n}`);
    for (const n of [undefined,null,true,'1','10',0,-1,1.1,max+1,Infinity,NaN]) assert.throws(() => encodeTimedCommand(action,n));
  }
  assert.throws(() => encodeTimedCommand('heater:10',10));
});
test('fresh supported stopped controller needed; each cooldown is independent', () => {
  assert(buildTimedControls(report(),control,now).heater.available);
  for (const r of [null,report({...health,version:2}),report(health,now-15000),report(health,now+1)]) {
    assert.equal(buildTimedControls(r,control,now).heater.available,false);
  }
  for (const key of ['unoLink','controlIo','indicatorIo','stopClosed','emergencyClosed','doorClosed','timedSupported']) {
    assert.equal(buildTimedControls(report({...health,[key]:false}),control,now).heater.available,false,key);
    assert.equal(buildTimedControls(report({...health,[key]:undefined}),control,now).heater.available,false,key);
  }
  for (const key of ['armed','fanOn','unlockOn','testActive','heaterOn','pumpOn']) {
    assert.equal(buildTimedControls(report({...health,[key]:true}),control,now).pump.available,false,key);
    assert.equal(buildTimedControls(report({...health,[key]:undefined}),control,now).pump.available,false,key);
  }
  const cooling = buildTimedControls(report({...health,heaterReady:false}),control,now);
  assert(!cooling.heater.available && cooling.pump.available);
  assert(!buildTimedControls(report(),{...control,commands:[{status:'queued'}]},now).pump.available);
  assert(!buildTimedControls(report(),{...control,device:{connected:false}},now).pump.available);
});
test('HTTP validates authorization, version, supervision, duration, queue and short expiry', async () => {
  process.env.DATABASE_URL=''; process.env.DB_CLIENT='none'; process.env.IOT_API_KEY='timed-iot';
  process.env.CONTROL_API_KEY='timed-control'; process.env.REMOTE_CONTROL_ENABLED='true';
  const {createServer}=require('../backend/src/server/createServer');
  const {dataStore}=require('../backend/src/db'); assert.equal(dataStore.configured,false);
  const server=createServer(); await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const base=`http://127.0.0.1:${server.address().port}`, deviceId='timed-test';
  const req=async(path,body,key='timed-control')=>{
    const r=await fetch(base+path,{method:body?'POST':'GET',headers:{'Content-Type':'application/json','X-API-Key':'timed-iot','X-Control-Key':key},body:body?JSON.stringify(body):undefined});
    return {status:r.status,body:await r.json()};
  };
  const command={deviceId,command:'heater',durationSeconds:10,supervised:true};
  const send=b=>req('/api/device/commands',b);
  const postHealth=h=>req('/api/iot/readings',{deviceId,readings:[{name:'IR Sensor',value:0,unit:'digital'}],checks:h});
  try {
    assert.equal((await req('/api/device/commands',command,'wrong')).status,401);
    assert.equal((await send(command)).status,409);
    await req(`/api/device/commands/next?deviceId=${deviceId}`);
    await postHealth({...health,version:2}); assert.equal((await send(command)).status,409);
    await postHealth(health);
    assert.equal((await send({...command,durationSeconds:11})).status,400);
    assert.equal((await send({...command,durationSeconds:'10'})).status,400);
    assert.equal((await send({...command,supervised:false})).status,409);
    const queued=await send(command); assert.equal(queued.status,201);
    assert.equal(queued.body.command.command,'heater:10');
    assert.equal(Date.parse(queued.body.command.expiresAt)-Date.parse(queued.body.command.createdAt),5000);
    assert.equal((await send({...command,command:'pump',durationSeconds:3})).status,409);
    const next=await req(`/api/device/commands/next?deviceId=${deviceId}`);
    assert.equal(next.body.command.command,'heater:10');
    await req(`/api/device/commands/${queued.body.command.id}/ack`,{deviceId,status:'completed',message:'timed_run_started'});
    await postHealth({...health,heaterOn:true,heaterReady:false});
    assert.equal((await send({...command,command:'pump',durationSeconds:3})).status,409);
  } finally { await new Promise(r=>server.close(r)); }
});
