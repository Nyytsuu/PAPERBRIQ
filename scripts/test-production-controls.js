const {test}=require('node:test');
const assert=require('node:assert/strict');
const {validateSettings,normalizeProduction,buildProductionControls,encodeProductionCommand}=require('../backend/src/db/productionControls');
const now=Date.now();
const p={version:1,pumpMaxSeconds:120,dryingMaxSeconds:3600,pumpVerified:1,dryingVerified:1,kind:0,phase:0,runId:0,totalSeconds:0,elapsedSeconds:0,remainingSeconds:0};
const health={version:3,unoLink:true,controlIo:true,indicatorIo:true,doorClosed:true,stopClosed:true,emergencyClosed:true,armed:false,testActive:false,unlockOn:false,fanOn:false,heaterOn:false,pumpOn:false,production:p};
const control={device:{connected:true},processState:'stopped',commands:[]};
const report=h=>({health:h,recordedAt:new Date(now).toISOString()});
const thermalP={...p,version:2,preheatMaxSeconds:600,targetC:45,temperatureFresh:1,thermalStage:0,heaterFanConfirmed:1};

test('preheat requires new reports, fresh temperature, fan confirmation and stopped forming',()=>{
  const ready=buildProductionControls(report({...health,production:thermalP}),control,null,now);
  assert(ready.preheat.canStart && ready.drying.canStart);
  assert.match(encodeProductionCommand('preheat_start',{durationSeconds:600},ready),/^HH:[0-9A-F]{8}:600$/);
  assert.throws(()=>encodeProductionCommand('preheat_start',{durationSeconds:601},ready));
  for (const patch of [{temperatureFresh:0},{heaterFanConfirmed:0},{dryingVerified:0}]) {
    const blocked=buildProductionControls(report({...health,production:{...thermalP,...patch}}),control,null,now);
    assert(!blocked.preheat.canStart && !blocked.drying.canStart);
  }
  assert(!buildProductionControls(report(health),control,null,now).preheat.canStart);
  assert(!buildProductionControls(report({...health,production:thermalP}),{...control,processState:'running'},null,now).preheat.canStart);
  assert.equal(normalizeProduction({...thermalP,targetC:99}),null);
  assert.equal(normalizeProduction({...thermalP,thermalStage:5}),null);
});

test('ready to load is distinct from drying and cannot start a simultaneous pump',()=>{
  const ready=buildProductionControls(report({...health,production:{...thermalP,kind:3,phase:2,
    runId:0x1234ABCD,totalSeconds:600,elapsedSeconds:60,remainingSeconds:540,thermalStage:2}}),control,null,now);
  assert.equal(ready.run.state,'ready_to_load');
  assert.equal(ready.run.output,'preheat');
  assert(!ready.pump.canStart && !ready.drying.canStart && !ready.preheat.canAdjust);
  const warming=buildProductionControls(report({...health,production:{...thermalP,kind:2,phase:1,
    runId:0x1234ABCD,totalSeconds:60,elapsedSeconds:0,remainingSeconds:60,thermalStage:1}}),control,null,now);
  assert.equal(warming.run.state,'warming');
  assert(warming.drying.canAdjust);
  assert.equal(encodeProductionCommand('drying_time',{durationSeconds:120,runId:'1234ABCD'},warming),'DT:1234ABCD:120');
  assert.throws(()=>encodeProductionCommand('drying_time',{durationSeconds:120,runId:'1234ABCE'},warming));
  const {inferProcessState}=require('../backend/src/db/deviceControl');
  assert.equal(inferProcessState([],[{command:'HH:1234ABCD:600',status:'completed'},{command:'stop',status:'completed'}]),'stopped');
});
test('production durations are independent of bench limits and commissioning gates remain separate',()=>{
  assert.deepEqual(validateSettings({pumpSeconds:60,dryingSeconds:1800}),{dryingSeconds:1800});
  for(const n of [0,-1,1.5,'30',Infinity,2147484]) assert.throws(()=>validateSettings({dryingSeconds:n}));
  assert.equal(normalizeProduction({...p,phase:2}),null);
  const ready=buildProductionControls(report(health),control,null,now);
  assert.match(encodeProductionCommand('pump_arm',{},ready),/^PP:[0-9A-F]{8}:20$/);
  assert.match(encodeProductionCommand('pump_arm',{durationSeconds:121},ready),/^PP:[0-9A-F]{8}:20$/);
  for(const patch of [{pumpVerified:0},{pumpMaxSeconds:0},{pumpMaxSeconds:19}]) {
    const blocked=buildProductionControls(report({...health,production:{...p,...patch}}),control,null,now);
    assert(!blocked.pump.canStart); assert.throws(()=>encodeProductionCommand('pump_arm',{durationSeconds:30},blocked));
  }
  assert(!buildProductionControls(report(health),control,null,now+15000).pump.canStart);
  assert(!buildProductionControls(report({...health,production:{...p,sampleAgeMs:15000}}),control,null,now).pump.canStart);
});
test('an active pump run cannot be adjusted',()=>{
  const active=buildProductionControls(report({...health,armed:true,production:{...p,kind:1,phase:2,runId:0x1234ABCD,totalSeconds:60,elapsedSeconds:20,remainingSeconds:40}}),{...control,processState:'running'},null,now);
  assert(!active.pump.canAdjust && !active.drying.canStart);
  assert.throws(()=>encodeProductionCommand('pump_time',{durationSeconds:90,runId:'1234ABCD'},active));
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
    assert.deepEqual(status.body.productionControls.settings,{dryingSeconds:1800});
    assert.equal(status.body.productionControls.pump.canStart,false);
    const post=h=>req('/api/iot/readings',{deviceId,readings:[
      {name:'IR Sensor',value:0,unit:'digital'},
      {name:'Shredder Inlet',value:1,unit:'digital'},
      {name:'Shredder Outlet',value:0,unit:'digital'}
    ],checks:h});
    await req(`/api/device/commands/next?deviceId=${deviceId}`);
    await post(health);
    const sensors = (await req('/api/iot')).body.sensors;
    assert.equal(sensors.find(s=>s.name==='Shredder Inlet').value,1);
    assert.equal(sensors.find(s=>s.name==='Shredder Outlet').value,0);
    let q=await req('/api/device/commands',{deviceId,command:'pump_arm',durationSeconds:60});assert.equal(q.status,201);
    assert.match(q.body.command.command,/^PP:[0-9A-F]{8}:20$/);
    assert.equal(Date.parse(q.body.command.expiresAt)-Date.parse(q.body.command.createdAt),5000);
    await req(`/api/device/commands/${q.body.command.id}/ack`,{deviceId,status:'completed',message:'production_applied'});
    await post({...health,production:{...p,kind:1,phase:2,runId:0x1234ABCD,totalSeconds:60,elapsedSeconds:20,remainingSeconds:40}});
    assert.equal((await req('/api/device/commands',{deviceId,command:'pump_time',durationSeconds:90,runId:'1234ABCD'})).status,400);
    await post({...health,production:thermalP});
    q=await req('/api/device/commands',{deviceId,command:'preheat_start',durationSeconds:600});
    assert.equal(q.status,201); assert.match(q.body.command.command,/^HH:[0-9A-F]{8}:600$/);
  } finally {await new Promise(r=>server.close(r));}
});
