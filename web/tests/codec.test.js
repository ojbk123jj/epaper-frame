import { test } from 'node:test';
import assert from 'node:assert/strict';
import { crc32, encodeBegin, encodeControl, encodeData, encodeAiBegin, encodeAiCommit,
  encodePreview, parseInfo, parseStatus, parseAiStatus, parsePreviewChunk, FRAME_SIZE,
  AI_PROMPT_MAX } from '../src/protocol.js';
import { quantizeRgba, unpackFrame, cropRect } from '../src/image.js';

test('IEEE CRC32 and exact wire layout', () => {
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
  const packet = encodeBegin(42, new Uint8Array(FRAME_SIZE), 0);
  assert.equal(packet.length, 15);
  assert.deepEqual([...packet.slice(0, 6)], [1, 1, 42, 0, 0, 0]);
  assert.equal(new DataView(packet.buffer).getUint32(6, true), 30000);
  assert.deepEqual([...encodeControl(2, 42)], [2, 1, 42, 0, 0, 0]);
  assert.deepEqual([...encodeData(256, Uint8Array.of(1, 2))], [0, 1, 0, 0, 1, 2]);
  assert.throws(() => encodeBegin(0, new Uint8Array(FRAME_SIZE)));
  assert.throws(() => encodeBegin(1, new Uint8Array(1)));
});
test('status preserves unsigned job IDs and rejects incompatible records', () => {
  const b = new Uint8Array(20), v = new DataView(b.buffer);
  b[0]=1; b[1]=4; v.setUint32(4, 0xf1234567, true); v.setUint32(8,30000,true);
  assert.equal(parseStatus(v).jobId, 0xf1234567);
  assert.equal(parseStatus(v).state, 4);
  assert.throws(() => parseStatus(new DataView(new ArrayBuffer(19))));
  b[0]=2; assert.throws(() => parseStatus(v));
});
test('info bounds the payload by negotiated ATT MTU', () => {
  const b = new Uint8Array(16), v = new DataView(b.buffer);
  b[0]=1; b[1]=4; v.setUint16(2,400,true); v.setUint16(4,300,true);
  v.setUint32(6,30000,true); v.setUint16(10,20,true); b[12]=3;
  assert.equal(parseInfo(v).maxWrite, 20);
  v.setUint16(10,4,true); assert.throws(() => parseInfo(v));
});
test('palette packing, white alpha compositing and unpack roundtrip', () => {
  const rgba = new Uint8ClampedArray(400*300*4);
  for(let i=0;i<rgba.length;i+=4) rgba.set([255,255,255,255],i);
  rgba.set([0,0,0,255,255,255,255,255,255,255,0,255,255,0,0,255]);
  const frame=quantizeRgba(rgba, false);
  assert.equal(frame.length,30000); assert.equal(frame[0],0x1b);
  assert.deepEqual([...unpackFrame(frame).slice(0,16)],[...rgba.slice(0,16)]);
  rgba.set([0,0,0,0]);
  assert.equal(quantizeRgba(rgba,false)[0]>>6,1);
  assert.equal(quantizeRgba(rgba,true).length,30000);
  assert.throws(()=>quantizeRgba(rgba.slice(1)));
});
test('AI prompt, preview command and status wire layouts', () => {
  const prompt=new TextEncoder().encode('海边灯塔');
  const begin=encodeAiBegin(77,prompt);
  const v=new DataView(begin.buffer);
  assert.equal(begin.length,15);
  assert.deepEqual([...begin.slice(0,2)],[4,1]);
  assert.equal(v.getUint32(2,true),77);
  assert.equal(v.getUint32(6,true),prompt.length);
  assert.equal(v.getUint32(10,true),crc32(prompt));
  assert.deepEqual([...encodeAiCommit(77)],[5,1,77,0,0,0]);
  assert.deepEqual([...encodePreview(77)],[6,1,77,0,0,0]);
  assert.throws(()=>encodeAiBegin(0,prompt));
  assert.throws(()=>encodeAiBegin(1,new Uint8Array(0)));
  assert.throws(()=>encodeAiBegin(1,new Uint8Array(AI_PROMPT_MAX+1)));
  assert.throws(()=>encodeData(0,prompt,4));
  assert.deepEqual([...encodeData(0,prompt,prompt.length).slice(0,4)],[0,0,0,0]);

  const status=new Uint8Array(16),s=new DataView(status.buffer);
  status[0]=1;status[1]=7;s.setUint32(4,0x81234567,true);s.setUint16(8,125,true);
  status[11]=1;s.setUint32(12,0x81234567,true);
  const parsed=parseAiStatus(s);
  assert.equal(parsed.state,7);assert.equal(parsed.jobId,0x81234567);
  assert.equal(parsed.elapsed,125);assert.equal(parsed.hasPreview,true);
  assert.equal(parsed.lastJobId,0x81234567);
  status[1]=9;assert.throws(()=>parseAiStatus(s));

  const chunk=new Uint8Array(8);new DataView(chunk.buffer).setUint32(0,1024,true);
  chunk.set([1,2,3,4],4);
  const parsedChunk=parsePreviewChunk(new DataView(chunk.buffer));
  assert.equal(parsedChunk.offset,1024);assert.deepEqual([...parsedChunk.data],[1,2,3,4]);
});
test('info advertises AI, configuration and preview capabilities', () => {
  const b = new Uint8Array(16), v = new DataView(b.buffer);
  b[0]=1; b[1]=4; v.setUint16(2,400,true); v.setUint16(4,300,true);
  v.setUint32(6,30000,true); v.setUint16(10,244,true); b[12]=3;
  b[13]=7; v.setUint16(14,1024,true);
  const info=parseInfo(v);
  assert.equal(info.ai,true);assert.equal(info.aiConfigured,true);assert.equal(info.preview,true);
  assert.equal(info.maxPrompt,1024);
  b[13]=0;v.setUint16(14,0,true);
  const old=parseInfo(v);
  assert.equal(old.ai,false);assert.equal(old.maxPrompt,1024);
});
test('crop stays inside source for wide, tall and zoomed images', () => {
  for(const [w,h] of [[1600,900],[900,1600],[1,1]]) for(const zoom of [1,3]) {
    const r=cropRect(w,h,zoom,0,1);
    assert.ok(Math.abs(r.width/r.height-4/3)<1e-9);
    assert.ok(r.x>=0 && r.y>=0 && r.x+r.width<=w+1e-8 && r.y+r.height<=h+1e-8);
  }
  assert.throws(()=>cropRect(0,10));
});
