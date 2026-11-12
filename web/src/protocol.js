export const FRAME_SIZE=30000;
export const AI_PROMPT_MAX=1024;
export const UUID={service:'f0e10000-9c7d-4f2b-a6c8-61d3e5b74001',
  info:'f0e10001-9c7d-4f2b-a6c8-61d3e5b74001',control:'f0e10002-9c7d-4f2b-a6c8-61d3e5b74001',
  data:'f0e10003-9c7d-4f2b-a6c8-61d3e5b74001',status:'f0e10004-9c7d-4f2b-a6c8-61d3e5b74001',
  ai:'f0e10005-9c7d-4f2b-a6c8-61d3e5b74001',preview:'f0e10006-9c7d-4f2b-a6c8-61d3e5b74001'};
export const STATE={IDLE:0,RECEIVING:1,QUEUED:2,DISPLAYING:3,SUCCEEDED:4,FAILED:5,AI_BUSY:6};
export const AI_STATE={IDLE:0,QUEUED:1,CONNECTING:2,GENERATING:3,DOWNLOADING:4,
  CONVERTING:5,DISPLAYING:6,SUCCEEDED:7,FAILED:8};
export function crc32(bytes) {
  let crc=0xffffffff;
  for(const byte of bytes) {
    crc^=byte;
    for(let bit=0;bit<8;bit++) crc=(crc>>>1)^(0xedb88320&-(crc&1));
  }
  return (crc^0xffffffff)>>>0;
}
function validId(id) { if(!Number.isInteger(id)||id<=0||id>0xffffffff) throw new Error('无效任务编号'); }
function encodeCommand(op,jobId) {
  validId(jobId);
  const p=new Uint8Array(6); p.set([op,1]); new DataView(p.buffer).setUint32(2,jobId,true); return p;
}
export function encodeControl(op,jobId) {
  if(op!==2&&op!==3) throw new Error('无效控制命令');
  return encodeCommand(op,jobId);
}
export function encodeBegin(jobId,frame,mode=0) {
  validId(jobId);
  if(!(frame instanceof Uint8Array)||frame.length!==FRAME_SIZE||![0,1].includes(mode)) throw new Error('图片帧格式不正确');
  const p=new Uint8Array(15),v=new DataView(p.buffer); p.set([1,1]);
  v.setUint32(2,jobId,true); v.setUint32(6,FRAME_SIZE,true); v.setUint32(10,crc32(frame),true); p[14]=mode; return p;
}
export function encodeAiBegin(jobId,promptBytes) {
  validId(jobId);
  if(!(promptBytes instanceof Uint8Array)||promptBytes.length<1||promptBytes.length>AI_PROMPT_MAX)
    throw new Error(`AI 提示词需为 1 到 ${AI_PROMPT_MAX} 个 UTF-8 字节`);
  const p=new Uint8Array(15),v=new DataView(p.buffer); p.set([4,1]);
  v.setUint32(2,jobId,true); v.setUint32(6,promptBytes.length,true);
  v.setUint32(10,crc32(promptBytes),true); p[14]=0; return p;
}
export function encodeAiCommit(jobId) { return encodeCommand(5,jobId); }
export function encodePreview(jobId) { return encodeCommand(6,jobId); }
export function encodeData(offset,bytes,total=FRAME_SIZE) {
  if(!Number.isInteger(offset)||offset<0||!bytes.length||offset+bytes.length>total) throw new Error('数据分块超出范围');
  const p=new Uint8Array(4+bytes.length); new DataView(p.buffer).setUint32(0,offset,true); p.set(bytes,4); return p;
}
function view(value,length) {
  const v=value instanceof DataView?value:new DataView(value.buffer,value.byteOffset,value.byteLength);
  if(v.byteLength!==length||v.getUint8(0)!==1) throw new Error('相框协议版本或数据长度不兼容'); return v;
}
export function parseInfo(value) {
  const v=view(value,16);
  const flags=v.getUint8(13),maxPrompt=v.getUint16(14,true)||AI_PROMPT_MAX;
  const info={width:v.getUint16(2,true),height:v.getUint16(4,true),frameSize:v.getUint32(6,true),
    maxWrite:v.getUint16(10,true),modes:v.getUint8(12),ai:Boolean(flags&1),
    aiConfigured:Boolean(flags&2),preview:Boolean(flags&4),maxPrompt};
  if(v.getUint8(1)!==4||info.width!==400||info.height!==300||info.frameSize!==FRAME_SIZE||
    info.maxWrite<5||info.maxWrite>244||!(info.modes&1)) throw new Error('相框规格不兼容');
  return info;
}
export function parseStatus(value) {
  const v=view(value,20),state=v.getUint8(1);
  if(state>6) throw new Error('相框状态不兼容');
  return {state,error:v.getUint16(2,true),jobId:v.getUint32(4,true),received:v.getUint32(8,true),total:v.getUint32(12,true),displayed:v.getUint32(16,true)};
}
export function parseAiStatus(value) {
  const v=view(value,16),state=v.getUint8(1);
  if(state>8) throw new Error('相框 AI 状态不兼容');
  return {state,error:v.getUint16(2,true),jobId:v.getUint32(4,true),elapsed:v.getUint16(8,true),
    failureStage:v.getUint8(10),hasPreview:Boolean(v.getUint8(11)&1),lastJobId:v.getUint32(12,true)};
}
export function parsePreviewChunk(value) {
  const v=value instanceof DataView?value:new DataView(value.buffer,value.byteOffset,value.byteLength);
  if(v.byteLength<4) throw new Error('相框预览数据不完整');
  return {offset:v.getUint32(0,true),data:new Uint8Array(v.buffer,v.byteOffset+4,v.byteLength-4)};
}
