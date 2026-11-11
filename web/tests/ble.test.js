import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BleClient } from '../src/ble.js';
import { UUID, FRAME_SIZE } from '../src/protocol.js';

function fixture(options={}) {
  let time=0, state=0, job=0, received=0, connections=0;
  const writes=[];
  const info=new DataView(new ArrayBuffer(16));
  info.setUint8(0,1);info.setUint8(1,4);info.setUint16(2,400,true);info.setUint16(4,300,true);
  info.setUint32(6,30000,true);info.setUint16(10,244,true);info.setUint8(12,3);
  const status=()=>{
    const v=new DataView(new ArrayBuffer(20));v.setUint8(0,1);v.setUint8(1,state);
    v.setUint32(4,options.wrongJob&&state===4?job+1:job,true);
    v.setUint32(8,received,true);v.setUint32(12,30000,true);return v;
  };
  const characteristics={
    [UUID.info]:{readValue:async()=>info},
    [UUID.status]:{readValue:async()=>status()},
    [UUID.control]:{writeValueWithResponse:async p=>{
      writes.push(p.slice());
      const v=new DataView(p.buffer,p.byteOffset,p.byteLength);
      if(p[0]===1){job=v.getUint32(2,true);received=0;state=1;}
      if(p[0]===2){
        state=options.fail?5:options.stuck?3:4;
        if(options.commitDisconnect){gatt.connected=false;throw new Error('Disconnected');}
      }
      if(p[0]===3)state=5;
    }},
    [UUID.data]:{writeValueWithResponse:async p=>{
      if(options.dataHang)return new Promise(()=>{});
      const offset=new DataView(p.buffer,p.byteOffset,p.byteLength).getUint32(0,true);
      assert.equal(offset,received);assert.ok(p.length<=244);received+=p.length-4;
    }}
  };
  const server={getPrimaryService:async()=>({getCharacteristic:async uuid=>characteristics[uuid]})};
  const gatt={connected:false,connect:async()=>{connections++;gatt.connected=true;return server;},disconnect:()=>{gatt.connected=false;}};
  const device={name:'EPaperFrame',gatt,addEventListener:()=>{},removeEventListener:()=>{}};
  const client=new BleClient({bluetooth:{requestDevice:async()=>device},secure:true,
    now:()=>time,sleep:async ms=>{time+=ms;},newJobId:()=>42,ioTimeoutMs:15,resultTimeoutMs:1000});
  device.id='device-a';
  return {client,device,writes,get received(){return received;},get connections(){return connections;},setState:s=>{state=s;},setJob:id=>{job=id;}};
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
