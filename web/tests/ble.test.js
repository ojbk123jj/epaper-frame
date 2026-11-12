import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BleClient } from '../src/ble.js';
import { UUID, FRAME_SIZE, AI_STATE, encodePreview } from '../src/protocol.js';

function fixture(options={}) {
  let time=0, state=0, job=0, received=0, connections=0;
  let aiMode=false, aiTotal=0, aiReceived=0, aiJob=0, aiState=0, aiElapsed=0,
    aiError=0, aiFailure=0, aiHasPreview=false;
  let previewOffset=0, previewJob=0;
  const writes=[];
  const previewFrame=options.previewFrame||new Uint8Array(FRAME_SIZE);
  const info=new DataView(new ArrayBuffer(16));
  info.setUint8(0,1);info.setUint8(1,4);info.setUint16(2,400,true);info.setUint16(4,300,true);
  info.setUint32(6,30000,true);info.setUint16(10,244,true);info.setUint8(12,3);
  if(options.ai) {
    info.setUint8(13,(options.aiConfigured===false?0:2)|1|(options.noPreview?0:4));
    info.setUint16(14,1024,true);
  }
  const status=()=>{
    const v=new DataView(new ArrayBuffer(20));v.setUint8(0,1);v.setUint8(1,state);
    v.setUint32(4,options.wrongJob&&state===4?job+1:job,true);
    v.setUint32(8,received,true);v.setUint32(12,30000,true);return v;
  };
  const aiStatus=()=>{
    const current=aiState;
    if(current===7)aiHasPreview=true;
    const v=new DataView(new ArrayBuffer(16));v.setUint8(0,1);v.setUint8(1,current);
    v.setUint16(2,aiError,true);v.setUint32(4,aiJob,true);v.setUint16(8,aiElapsed,true);
    v.setUint8(10,aiFailure);v.setUint8(11,aiHasPreview?1:0);
    v.setUint32(12,aiHasPreview?aiJob:0,true);
    aiElapsed++;
    if(current===6&&options.aiDisconnectAtDisplay&&gatt.connected)gatt.connected=false;
    if(current>=3&&current<7&&!options.aiStuck)aiState=current+1;
    return v;
  };
  const characteristics={
    [UUID.info]:{readValue:async()=>info},
    [UUID.status]:{readValue:async()=>status()},
    [UUID.ai]:{readValue:async()=>aiStatus()},
    [UUID.preview]:{readValue:async()=>{
      if(options.previewFail)throw new Error('no preview');
      const n=Math.min(240,FRAME_SIZE-previewOffset);
      const out=new Uint8Array(4+n);new DataView(out.buffer).setUint32(0,previewOffset,true);
      out.set(previewFrame.subarray(previewOffset,previewOffset+n),4);
      previewOffset+=n;return new DataView(out.buffer);
    }},
    [UUID.control]:{writeValueWithResponse:async p=>{
      writes.push(p.slice());
      const v=new DataView(p.buffer,p.byteOffset,p.byteLength);
      if(p[0]===1){job=v.getUint32(2,true);received=0;state=1;}
      if(p[0]===2){
        state=options.fail?5:options.stuck?3:4;
        if(options.commitDisconnect){gatt.connected=false;throw new Error('Disconnected');}
      }
      if(p[0]===3)state=5;
      if(p[0]===4){aiJob=v.getUint32(2,true);aiTotal=v.getUint32(6,true);aiReceived=0;aiMode=true;aiState=0;}
      if(p[0]===5){
        assert.equal(aiReceived,aiTotal);
        aiMode=false;aiError=options.aiFail?5:0;aiFailure=options.aiFail?3:0;
        aiState=options.aiFail?8:(options.aiStartState||3);
        aiElapsed=0;aiHasPreview=false;
      }
      if(p[0]===6){
        if(options.previewFail)throw new Error('no preview');
        previewJob=v.getUint32(2,true);previewOffset=0;
      }
    }},
    [UUID.data]:{writeValueWithResponse:async p=>{
      if(options.dataHang)return new Promise(()=>{});
      const offset=new DataView(p.buffer,p.byteOffset,p.byteLength).getUint32(0,true);
      if(aiMode){assert.equal(offset,aiReceived);assert.ok(aiReceived+p.length-4<=aiTotal);aiReceived+=p.length-4;}
      else{assert.equal(offset,received);assert.ok(p.length<=244);received+=p.length-4;}
    }}
  };
  const server={getPrimaryService:async()=>({getCharacteristic:async uuid=>characteristics[uuid]})};
  const gatt={connected:false,connect:async()=>{connections++;gatt.connected=true;return server;},disconnect:()=>{gatt.connected=false;}};
  const device={name:'EPaperFrame',gatt,addEventListener:()=>{},removeEventListener:()=>{}};
  const client=new BleClient({bluetooth:{requestDevice:async()=>device},secure:true,
    now:()=>time,sleep:async ms=>{time+=ms;},newJobId:()=>42,ioTimeoutMs:15,
    resultTimeoutMs:1000,aiResultTimeoutMs:10000,previewTimeoutMs:5000});
  device.id='device-a';
  return {client,device,writes,get received(){return received;},get aiReceived(){return aiReceived;},
    get connections(){return connections;},get previewJob(){return previewJob;},
    setState:s=>{state=s;},setJob:id=>{job=id;},setAiState:s=>{aiState=s;},
    setAiJob:id=>{aiJob=id;},setAiError:e=>{aiError=e;},setAiPreview:v=>{aiHasPreview=v;}};
}
test('ordered bounded writes and confirmed display result',async()=>{
  const f=fixture();await f.client.connect();let last=0;
  const result=await f.client.send(new Uint8Array(FRAME_SIZE),{onProgress:n=>{last=n;}});
  assert.equal(f.received,30000);assert.equal(last,30000);assert.equal(result.state,4);
  assert.deepEqual(f.writes.map(p=>p[0]),[1,2]);
});
test('disconnect at COMMIT reconnects to read result without retrying COMMIT',async()=>{
  const f=fixture({commitDisconnect:true});await f.client.connect();
  assert.equal((await f.client.send(new Uint8Array(FRAME_SIZE))).jobId,42);
  assert.equal(f.connections,2);assert.equal(f.writes.filter(p=>p[0]===2).length,1);
});
test('busy, failed and different-job results cannot report success',async()=>{
  const busy=fixture();await busy.client.connect();busy.setState(6);
  await assert.rejects(busy.client.send(new Uint8Array(FRAME_SIZE)),/忙/);
  for(const opts of [{fail:true},{wrongJob:true}]) {
    const f=fixture(opts);await f.client.connect();
    await assert.rejects(f.client.send(new Uint8Array(FRAME_SIZE)));
  }
});
test('result timeout is bounded and never resubmits a job',async()=>{
  const f=fixture({stuck:true});await f.client.connect();
  await assert.rejects(f.client.send(new Uint8Array(FRAME_SIZE)),/超时/);
  assert.equal(f.writes.filter(p=>p[0]===2).length,1);
});
test('hung data write disconnects and releases client sending state',async()=>{
  const f=fixture({dataHang:true});await f.client.connect();
  await assert.rejects(f.client.send(new Uint8Array(FRAME_SIZE)),/超时/);
  assert.equal(f.client.sending,false);assert.equal(f.client.connected,false);
});
test('unsafe context and unsupported browser give actionable errors',async()=>{
  await assert.rejects(new BleClient({secure:false}).connect(),/HTTPS/);
  await assert.rejects(new BleClient({secure:true,bluetooth:null}).connect(),/Chrome/);
});
test('manual recovery checks the original device and pending job',async()=>{
  const f=fixture({stuck:true});await f.client.connect();
  await assert.rejects(f.client.send(new Uint8Array(FRAME_SIZE)),/超时/);
  f.setState(4);f.setJob(99);
  await assert.rejects(f.client.readVerifiedStatus(),/任务/);
  f.setJob(42);assert.equal((await f.client.readVerifiedStatus()).state,4);
  assert.equal(f.client.pendingJob,null);
});
test('manual recovery rejects a different selected device',async()=>{
  const f=fixture({stuck:true});await f.client.connect();
  await assert.rejects(f.client.send(new Uint8Array(FRAME_SIZE)),/超时/);
  f.device.id='device-b';f.setState(4);
  await assert.rejects(f.client.readVerifiedStatus(),/设备/);
});
test('connect attempts are serialized throughout discovery',async()=>{
  let resolveDevice;
  const f=fixture();
  f.client.bluetooth.requestDevice=()=>new Promise(resolve=>{resolveDevice=resolve;});
  const first=f.client.connect();
  assert.equal(f.client.connecting,true);
  await assert.rejects(f.client.connect(),/连接/);
  resolveDevice(f.device);await first;
  assert.equal(f.client.connecting,false);
});

test('AI prompt is chunked, committed once and monitored to success',async()=>{
  const f=fixture({ai:true});await f.client.connect();
  let uploaded=0,lastState=0;
  const prompt='黑白红黄四色插画，海边灯塔';
  const result=await f.client.submitAi(prompt,{onUpload:n=>{uploaded=n;},onStatus:s=>{lastState=s.state;}});
  assert.equal(result.state,AI_STATE.SUCCEEDED);
  assert.equal(uploaded,new TextEncoder().encode(prompt).length);
  assert.equal(f.aiReceived,uploaded);
  assert.equal(f.writes.filter(p=>p[0]===5).length,1);
  assert.ok(lastState>=AI_STATE.QUEUED&&lastState<=AI_STATE.SUCCEEDED);
  assert.equal(f.client.pendingAiJob,null);
  assert.equal(result.hasPreview,true);
  assert.equal(result.lastJobId,42);
});
test('AI submit rejects unconfigured devices and busy tasks',async()=>{
  const off=fixture({ai:true,aiConfigured:false});await off.client.connect();
  await assert.rejects(off.client.submitAi('灯塔'),/未配置/);
  const busy=fixture({ai:true});await busy.client.connect();busy.setState(6);
  await assert.rejects(busy.client.submitAi('灯塔'),/忙/);
  const aiBusy=fixture({ai:true});await aiBusy.client.connect();
  aiBusy.setAiState(AI_STATE.GENERATING);
  await assert.rejects(aiBusy.client.submitAi('灯塔'),/AI 任务/);
});
test('AI failure returns the failure stage without reporting success',async()=>{
  const f=fixture({ai:true,aiFail:true});await f.client.connect();
  const result=await f.client.submitAi('灯塔');
  assert.equal(result.state,AI_STATE.FAILED);
  assert.equal(result.failureStage,3);assert.equal(result.error,5);
});
test('AI display reconnect is followed by a final success',async()=>{
  const f=fixture({ai:true,aiDisconnectAtDisplay:true});await f.client.connect();
  const result=await f.client.submitAi('灯塔');
  assert.equal(result.state,AI_STATE.SUCCEEDED);
  assert.equal(f.connections,2);
});
test('AI preview reads the retained frame in ordered chunks',async()=>{
  const frame=new Uint8Array(FRAME_SIZE);for(let i=0;i<frame.length;i++)frame[i]=i&0xff;
  const f=fixture({ai:true,previewFrame:frame});await f.client.connect();
  const result=await f.client.submitAi('灯塔');
  assert.equal(result.hasPreview,true);
  let progress=0;
  const preview=await f.client.fetchAiPreview(result.lastJobId,{onProgress:n=>{progress=n;}});
  assert.equal(progress,FRAME_SIZE);
  assert.deepEqual(preview,frame);
  assert.equal(f.previewJob,result.lastJobId);
  assert.equal(f.client.previewing,false);
});
test('AI preview rejects old firmware or missing frames',async()=>{
  const old=fixture({ai:true,noPreview:true});await old.client.connect();
  await assert.rejects(old.client.fetchAiPreview(42),/不支持/);
  const missing=fixture({ai:true,previewFail:true});await missing.client.connect();
  await assert.rejects(missing.client.fetchAiPreview(42),/没有可预览|no preview/);
});
test('AI status timeout leaves a pending job for manual verification',async()=>{
  const f=fixture({ai:true,aiStuck:true});await f.client.connect();
  await assert.rejects(f.client.submitAi('灯塔'),/超时/);
  assert.equal(f.client.pendingAiJob,42);
  f.setAiState(AI_STATE.SUCCEEDED);f.setAiJob(42);f.setAiPreview(true);f.setAiError(0);
  const status=await f.client.readVerifiedAiStatus();
  assert.equal(status.state,AI_STATE.SUCCEEDED);
  assert.equal(f.client.pendingAiJob,null);
});
test('AI pending job is cleared when the frame is idle and the job is gone',async()=>{
  const f=fixture({ai:true,aiStuck:true});await f.client.connect();
  await assert.rejects(f.client.submitAi('灯塔'),/超时/);
  assert.equal(f.client.pendingAiJob,42);
  f.setAiState(AI_STATE.IDLE);f.setAiJob(0);f.setState(0);
  await assert.rejects(f.client.readVerifiedAiStatus(),/未找到/);
  assert.equal(f.client.pendingAiJob,null);
  assert.equal(f.client.pendingAiDeviceId,null);
});
test('AI preview command uses the documented opcode',()=>{
  assert.deepEqual([...encodePreview(7)],[6,1,7,0,0,0]);
});
