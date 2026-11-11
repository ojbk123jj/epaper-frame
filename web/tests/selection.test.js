import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ImageSelection} from '../src/selection.js';
test('invalid new selection clears busy and suppresses an earlier decode',async()=>{
  let finish,busy=false,loaded=null,error=null;
  const selection=new ImageSelection({decode:()=>new Promise(resolve=>{finish=resolve;}),
    onBusy:value=>{busy=value;},onLoaded:value=>{loaded=value;},onError:value=>{error=value;}});
  const first=selection.load({type:'image/png',size:1});assert.equal(busy,true);
  await selection.load({type:'text/plain',size:1});assert.equal(busy,false);assert.ok(error);
  finish('stale-image');await first;
  assert.equal(loaded,null);assert.equal(busy,false);
});
test('only the latest successful image becomes the preview',async()=>{
  const pending=[],images=[];
  const selection=new ImageSelection({decode:()=>new Promise(resolve=>pending.push(resolve)),onLoaded:value=>images.push(value)});
  const a=selection.load({type:'image/png',size:1}),b=selection.load({type:'image/png',size:1});
  pending[1]('new');await b;pending[0]('old');await a;
  assert.deepEqual(images,['new']);
});
