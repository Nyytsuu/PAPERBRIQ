const {test}=require('node:test');
const assert=require('node:assert/strict');
const {validateSettings,normalizeProduction,buildProductionControls,encodeProductionCommand}=require('../backend/src/db/productionControls');
const now=Date.now();
const p={version:1,pumpMaxSeconds:120,dryingMaxSeconds:3600,pumpVerified:1,dryingVerified:1,kind:0,phase:0,runId:0,totalSeconds:0,elapsedSeconds:0,remainingSeconds:0};
const health={version:3,unoLink:true,controlIo:true,indicatorIo:true,doorClosed:true,stopClosed:true,emergencyClosed:true,armed:false,testActive:false,unlockOn:false,fanOn:false,heaterOn:false,pumpOn:false,production:p};
const control={device:{connected:true},processState:'stopped',commands:[]};
const report=h=>({health:h,recordedAt:new Date(now).toISOString()});
test('production durations are independent of bench limits and commissioning gates remain separate',()=>{
  assert.deepEqual(validateSettings({pumpSeconds:60,dryingSeconds:1800}),{pumpSeconds:60,dryingSeconds:1800});
  for(const n of [0,-1,1.5,'30',Infinity,2147484]) assert.throws(()=>validateSettings({pumpSeconds:n,dryingSeconds:60}));
  assert.equal(normalizeProduction({...p,phase:2}),null);
  const ready=buildProductionControls(report(health),control,null,now);
  assert.match(encodeProductionCommand('pump_arm',{durationSeconds:120},ready),/^PP:[0-9A-F]{8}:120$/);
  assert.throws(()=>encodeProductionCommand('pump_arm',{durationSeconds:121},ready));
  for(const patch of [{pumpVerified:0},{pumpMaxSeconds:0}]) {
    const blocked=buildProductionControls(report({...health,production:{...p,...patch}}),control,null,now);
    assert(!blocked.pump.canStart); assert.throws(()=>encodeProductionCommand('pump_arm',{durationSeconds:30},blocked));
  }
  assert(!buildProductionControls(report(health),control,null,now+15000).pump.canStart);
  assert(!buildProductionControls(report({...health,production:{...p,sampleAgeMs:15000}}),control,null,now).pump.canStart);
});
test('live adjustment is scoped to a fresh active run and does not erase forming state',()=>{
  const active=buildProductionControls(report({...health,armed:true,production:{...p,kind:1,phase:2,runId:0x1234ABCD,totalSeconds:60,elapsedSeconds:20,remainingSeconds:40}}),{...control,processState:'running'},null,now);
  assert(active.pump.canAdjust && !active.drying.canStart);
  assert.equal(encodeProductionCommand('pump_time',{durationSeconds:15,runId:'1234ABCD'},active),'PT:1234ABCD:15');
  assert.throws(()=>encodeProductionCommand('pump_time',{durationSeconds:90,runId:'1234ABCE'},active));
  const {inferProcessState}=require('../backend/src/db/deviceControl');
  assert.equal(inferProcessState([],[{command:'PT:1234ABCD:15',status:'completed'},{command:'start',status:'completed'}]),'running');
});
test('HTTP persists production settings and validates live run identity, limits and expired reports',async()=>{
  process.env.DATABASE_URL='';process.env.DB_CLIENT='none';process.env.IOT_API_KEY='prod-iot';process.env.CONTROL_API_KEY='prod-control';process.env.REMOTE_CONTROL_ENABLED='true';
  const {createServer}=require('../backend/src/server/createServer');
  const {dataStore}=require('../backend/src/db');assert.equal(dataStore.configured,false);
  const server=createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const base=`http://127.0.0.1:${server.address().port}`,deviceId='production-test';
  const req=async(path,body,key='prod-control')=>{
    const r=await fetch(base+path,{method:body?'POST':'GET',headers:{'Content-Type':'application/json','X-API-Key':'prod-iot','X-Control-Key':key},body:body?JSON.stringify(body):undefined});return {status:r.status,body:await r.json()};
  };
  try {
    const settings={deviceId,pumpSeconds:60,dryingSeconds:1800};
    assert.equal((await req('/api/device/production-settings',settings,'bad')).status,401);
    assert.equal((await req('/api/device/production-settings',settings)).status,200);
    let status=await req(`/api/device/control-status?deviceId=${deviceId}`);
    assert.equal(status.body.productionControls.settings.pumpSeconds,60);
    assert.equal(status.body.productionControls.pump.canStart,false);
    const post=h=>req('/api/iot/readings',{deviceId,readings:[{name:'IR Sensor',value:0,unit:'digital'}],checks:h});
    await req(`/api/device/commands/next?deviceId=${deviceId}`);
    await post(health);
    let q=await req('/api/device/commands',{deviceId,command:'pump_arm',durationSeconds:60});assert.equal(q.status,201);
    assert.match(q.body.command.command,/^PP:[0-9A-F]{8}:60$/);
    assert.equal(Date.parse(q.body.command.expiresAt)-Date.parse(q.body.command.createdAt),5000);
    await req(`/api/device/commands/${q.body.command.id}/ack`,{deviceId,status:'completed',message:'production_applied'});
    await post({...health,production:{...p,kind:1,phase:2,runId:0x1234ABCD,totalSeconds:60,elapsedSeconds:20,remainingSeconds:40}});
    assert.equal((await req('/api/device/commands',{deviceId,command:'pump_time',durationSeconds:90,runId:'1234ABCE'})).status,409);
    q=await req('/api/device/commands',{deviceId,command:'pump_time',durationSeconds:90,runId:'1234ABCD'});assert.equal(q.status,201);
    assert.equal(q.body.command.command,'PT:1234ABCD:90');
  } finally {await new Promise(r=>server.close(r));}
});
