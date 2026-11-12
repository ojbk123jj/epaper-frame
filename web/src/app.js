import {quantizeRgba,unpackFrame,cropRect} from './image.js';
import {BleClient} from './ble.js';
import {ImageSelection} from './selection.js';
import {AI_STATE,STATE} from './protocol.js';
const $=id=>document.getElementById(id);
const client=new BleClient();
const preview=$('preview'),ctx=preview.getContext('2d'),original=document.createElement('canvas');
original.width=400;original.height=300;
const draw=original.getContext('2d',{willReadFrequently:true});
const aiPreview=$('ai-preview'),aiPreviewCtx=aiPreview.getContext('2d');
const encoder=new TextEncoder();
let source=null,frame=null,viewOriginal=false,working=false,loading=false,scheduled=0;
let aiWorking=false,previewing=false,aiStatus=null,aiBusy=false,frameBusy=false,aiPreviewFrame=null,aiPreviewJob=0;
let promptBytes=0,promptValid=false;
const AI_STAGE_LABELS=['空闲','排队等待','连接网络','云端生成','下载图片','四色转换','刷屏显示','显示成功','生成失败'];
const AI_FAILURE_HINTS={2:'无法连接 Wi-Fi 或校时，请检查设备的 Wi-Fi 配置和 2.4GHz 网络。',
  3:'云端生成失败，请检查 ModelScope Token、额度与模型权限，或查看串口日志。',
  4:'图片下载失败，请检查网络连接或图片大小。',
  5:'图片解码或四色转换失败，可能格式或分辨率不受支持。',
  6:'刷屏或蓝牙恢复失败，请检查面板/蓝牙状态或查看串口日志。'};
function message(text,type=''){ $('message').textContent=text;$('message').className=`status-message ${type}`; }
function maxPrompt(){return client.info?.maxPrompt||1024;}
function hasPending(){return client.pendingJob!==null||client.pendingAiJob!==null;}
function formatElapsed(seconds){const s=Number(seconds)||0;return `${Math.floor(s/60)}:${String(s%60).padStart(2,'0')}`;}
function updatePromptMeta(){
  const text=$('ai-prompt').value,bytes=encoder.encode(text);
  promptBytes=bytes.length;
  promptValid=text.trim().length>0&&bytes.length<=maxPrompt()&&!bytes.includes(0);
  $('ai-prompt-bytes').textContent=`${bytes.length} / ${maxPrompt()} 字节`;
  $('ai-prompt-bytes').classList.toggle('over',bytes.length>maxPrompt());
  controls();
}
function updateAiNotice(){
  let text='';
  if(!client.connected)text=aiWorking?'相框正在生成，正在等待蓝牙恢复；可保持页面打开。':'连接相框后可以生成 AI 画面。';
  else if(!client.info?.ai)text='当前固件不支持 AI 生图，请更新相框固件。';
  else if(!client.info.aiConfigured)text='相框未配置 Wi-Fi 或 ModelScope Token；请先在固件本地配置中填写并重新烧录。';
  else if(hasPending())text='上次任务结果尚未确认，请先点击“查询状态”。';
  else if(aiBusy)text='相框正在执行 AI 任务，请等待完成。';
  else if(frameBusy)text='相框正在处理图片任务，请等待完成。';
  $('ai-notice').hidden=!text;$('ai-notice').textContent=text;
}
function controls(){
  const busy=working||aiWorking||previewing;
  const aiReady=client.connected&&client.info?.ai&&client.info?.aiConfigured;
  const previewJob=aiPreviewJob||aiStatus?.lastJobId||0;
  $('file').disabled=working;$('adjustments').disabled=working||loading;
  $('connect').disabled=working||aiWorking||previewing||client.connecting||!client.secure||!client.bluetooth;
  $('send').disabled=busy||loading||client.connecting||!frame||!client.connected||aiBusy||frameBusy||hasPending();
  $('download').disabled=working||loading||!frame;
  $('refresh').hidden=!client.connected;$('refresh').disabled=busy||client.connecting;
  $('connect').textContent=client.connected?'断开连接':'连接相框';
  $('ai-generate').disabled=busy||loading||client.connecting||!aiReady||aiBusy||frameBusy||hasPending()||!promptValid;
  $('ai-preview-save').disabled=!aiPreviewFrame;
  $('ai-preview-refresh').disabled=busy||!client.connected||!previewJob;
  updateAiNotice();
}
function render(){
  scheduled=0;if(!source)return;
  const r=cropRect(source.width,source.height,Number($('zoom').value),Number($('position-x').value)/100,Number($('position-y').value)/100);
  draw.fillStyle='#fff';draw.fillRect(0,0,400,300);draw.imageSmoothingEnabled=true;draw.imageSmoothingQuality='high';
  draw.drawImage(source,r.x,r.y,r.width,r.height,0,0,400,300);
  frame=quantizeRgba(draw.getImageData(0,0,400,300).data,$('dither').checked);
  paint();$('zoom-value').value=`${Number($('zoom').value).toFixed(1)}×`;controls();
}
function paint(){
  if(!frame)return;
  if(viewOriginal)ctx.drawImage(original,0,0);
  else ctx.putImageData(new ImageData(unpackFrame(frame),400,300),0,0);
}
function paintAiPreview(){
  if(!aiPreviewFrame)return;
  aiPreviewCtx.putImageData(new ImageData(unpackFrame(aiPreviewFrame),400,300),0,0);
}
function schedule(){if(!scheduled)scheduled=requestAnimationFrame(render);}
function reset(){ $('zoom').value='1';$('position-x').value='50';$('position-y').value='50';$('dither').checked=true;render(); }
function sample(){
  const canvas=document.createElement('canvas');canvas.width=800;canvas.height=600;
  const c=canvas.getContext('2d');c.fillStyle='#f7f7f0';c.fillRect(0,0,800,600);
  c.fillStyle='#eed14b';c.beginPath();c.arc(585,150,83,0,Math.PI*2);c.fill();
  c.fillStyle='#dce4df';c.fillRect(85,65,10,410);c.fillRect(85,65,360,10);c.fillRect(435,65,10,410);c.fillRect(85,265,360,8);
  c.fillStyle='#243434';c.fillRect(0,490,800,10);
  c.strokeStyle='#354c39';c.lineWidth=8;c.lineCap='round';
  for(const [x,y] of [[325,185],[420,245],[480,160]]){c.beginPath();c.moveTo(400,465);c.quadraticCurveTo(x+30,y+85,x,y);c.stroke();}
  c.fillStyle='#647653';for(const [x,y,angle] of [[355,340,-.6],[430,335,.8],[400,390,.5]]){c.save();c.translate(x,y);c.rotate(angle);c.beginPath();c.ellipse(0,0,36,13,0,0,Math.PI*2);c.fill();c.restore();}
  c.fillStyle='#b3293b';for(const [x,y] of [[325,185],[420,245],[480,160]]){for(let i=0;i<5;i++){const a=i*Math.PI*2/5;c.beginPath();c.ellipse(x+Math.cos(a)*16,y+Math.sin(a)*16,23,18,a,0,Math.PI*2);c.fill();c.fillStyle='#f0c72e';c.beginPath();c.arc(x,y,10,0,Math.PI*2);c.fill();c.fillStyle='#b3293b';}}
  c.fillStyle='#e9bf36';c.beginPath();c.moveTo(350,390);c.lineTo(450,390);c.lineTo(435,487);c.quadraticCurveTo(400,505,365,487);c.closePath();c.fill();
  source=canvas;render();
}
async function decodeFile(file){
  const url=URL.createObjectURL(file),image=new Image();
  try{
    await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=()=>reject(new Error('图片无法读取，请换成 JPG 或 PNG 后重试。'));image.src=url;});
    if(image.naturalWidth*image.naturalHeight>32000000)throw new Error('图片分辨率过大，请先缩小到 3200 万像素以内。');
    const scale=Math.min(1,1600/Math.max(image.naturalWidth,image.naturalHeight));
    const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(image.naturalWidth*scale));canvas.height=Math.max(1,Math.round(image.naturalHeight*scale));
    const c=canvas.getContext('2d');c.fillStyle='#fff';c.fillRect(0,0,canvas.width,canvas.height);c.drawImage(image,0,0,canvas.width,canvas.height);
    return {canvas,name:file.name,width:image.naturalWidth,height:image.naturalHeight};
  }finally{URL.revokeObjectURL(url);image.src='';}
}
const selection=new ImageSelection({decode:decodeFile,
  onBusy:value=>{loading=value;controls();if(value)message('正在读取图片…');},
  onError:text=>message(text,'error'),
  onLoaded:image=>{source=image.canvas;$('image-name').textContent=image.name;
    $('image-meta').textContent=`${image.width} × ${image.height} · 已在手机本地处理`;
    reset();message('图片已准备好。调整构图后，连接相框发送。');}});
function loadFile(file){if(file&&!working)return selection.load(file);}
for(const id of ['zoom','position-x','position-y','dither'])$(id).addEventListener('input',schedule);
$('reset').addEventListener('click',reset);
$('file').addEventListener('change',event=>{loadFile(event.target.files[0]);event.target.value='';});
for(const name of ['dragenter','dragover'])$('drop-zone').addEventListener(name,event=>{event.preventDefault();if(!working)$('drop-zone').classList.add('drag-over');});
for(const name of ['dragleave','drop'])$('drop-zone').addEventListener(name,event=>{event.preventDefault();$('drop-zone').classList.remove('drag-over');if(name==='drop')loadFile(event.dataTransfer.files[0]);});
for(const [id,mode] of [['view-frame',false],['view-original',true]])$(id).addEventListener('click',()=>{viewOriginal=mode;$('view-frame').setAttribute('aria-pressed',String(!mode));$('view-original').setAttribute('aria-pressed',String(mode));paint();});
function saveFrame(bytes,name){
  if(!bytes)return;const url=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'}));
  const a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
$('download').addEventListener('click',()=>saveFrame(frame,'epaper-400x300-2bpp.bin'));
$('ai-preview-save').addEventListener('click',()=>saveFrame(aiPreviewFrame,'epaper-ai-400x300-2bpp.bin'));
function setMode(mode){
  const ai=mode==='ai';
  $('image-mode').hidden=ai;$('ai-mode').hidden=!ai;
  $('tab-image').setAttribute('aria-selected',String(!ai));
  $('tab-ai').setAttribute('aria-selected',String(ai));
  $('preview-note').textContent=ai
    ?'AI 画面由相框直接生成并显示；此处预览的是当前本地图片，生成结果会在下方单独显示。'
    :'预览用于检查构图与四色分布，实际屏幕颜色会有所不同。';
  if(ai)updatePromptMeta();
}
$('tab-image').addEventListener('click',()=>setMode('image'));
$('tab-ai').addEventListener('click',()=>setMode('ai'));
$('ai-prompt').addEventListener('input',updatePromptMeta);
for(const button of document.querySelectorAll('.prompt-presets button'))
  button.addEventListener('click',()=>{$('ai-prompt').value=button.dataset.prompt||'';updatePromptMeta();$('ai-prompt').focus();});
client.onConnection=connected=>{
  $('device-dot').classList.toggle('connected',connected);
  $('device-name').textContent=connected?client.device.name||'EPaperFrame':(working||aiWorking||previewing?'正在等待相框':'尚未连接相框');
  $('device-detail').textContent=connected
    ?(working?'刷屏期间暂时断开，完成后自动重连':aiWorking?'AI 生图中，可保持页面查看进度':previewing?'正在取回 AI 预览':'已连接，可以发送图片或生成 AI 画面')
    :(working||aiWorking||previewing?'相框正在处理任务，完成后会自动重连':'打开蓝牙，将手机放在相框附近');
  if(connected)updatePromptMeta();
  updateAiNotice();controls();
};
function applyAiStatus(status){
  aiStatus=status;
  aiBusy=status.state>=AI_STATE.QUEUED&&status.state<=AI_STATE.DISPLAYING;
  if(aiBusy)frameBusy=true;
  else if(status.state===AI_STATE.SUCCEEDED||status.state===AI_STATE.FAILED)frameBusy=false;
  $('ai-progress').hidden=status.state===AI_STATE.IDLE&&!aiWorking;
  $('ai-stage-label').textContent=AI_STAGE_LABELS[status.state]||'未知状态';
  $('ai-elapsed').textContent=formatElapsed(status.elapsed);
  const stageIndex={2:0,3:1,4:2,5:3,6:4};
  const current=stageIndex[status.state],failed=stageIndex[status.failureStage];
  const steps=[...document.querySelectorAll('#ai-mode .ai-steps li')];
  steps.forEach((li,index)=>{
    li.classList.remove('done','active','error');
    if(status.state===AI_STATE.SUCCEEDED)li.classList.add('done');
    else if(status.state===AI_STATE.FAILED){if(index<failed)li.classList.add('done');else if(index===failed)li.classList.add('error');}
    else if(current!==undefined){if(index<current)li.classList.add('done');else if(index===current)li.classList.add('active');}
  });
  if(status.state===AI_STATE.FAILED){
    const hint=AI_FAILURE_HINTS[status.failureStage]||'生成失败，请查看相框串口日志。';
    $('ai-error').textContent=`${hint}${status.error?`（错误编号 ${status.error}）`:''}`;
    $('ai-error').hidden=false;
  }else $('ai-error').hidden=true;
  if(status.hasPreview&&status.lastJobId)aiPreviewJob=status.lastJobId;
  controls();
}
async function loadAiPreview(jobId,{auto=false}={}){
  if(!client.connected||!client.info?.preview||!jobId){if(!auto)message('当前相框没有可读取的 AI 画面。','error');return false;}
  previewing=true;aiPreviewJob=jobId;
  $('ai-preview-area').hidden=false;$('ai-preview-status').textContent='正在取回预览…';
  $('ai-preview-progress').hidden=false;$('ai-preview-progress').value=0;
  $('ai-preview-save').disabled=true;controls();
  try{
    const fetched=await client.fetchAiPreview(jobId,{onProgress:n=>{$('ai-preview-progress').value=n;}});
    aiPreviewFrame=fetched;paintAiPreview();
    $('ai-preview-status').textContent='与实际屏幕一致的 4 色画面';
    $('ai-preview-save').disabled=false;
    if(!auto)message('已取回 AI 画面预览。','success');
    return true;
  }catch(error){
    $('ai-preview-status').textContent='预览获取失败';
    message(auto?'AI 画面已显示成功，但网页预览获取失败；可稍后重试。':error.message,'error');
    return false;
  }finally{previewing=false;$('ai-preview-progress').hidden=true;controls();}
}
$('ai-preview-refresh').addEventListener('click',()=>{
  const job=aiStatus?.lastJobId||aiPreviewJob;
  if(job)loadAiPreview(job,{auto:false});else message('相框还没有可读取的 AI 画面。','error');
});
$('ai-generate').addEventListener('click',async()=>{
  const prompt=$('ai-prompt').value;
  updatePromptMeta();
  if(!promptValid){message('提示词为空或超过长度限制，请调整后再试。','error');return;}
  aiWorking=true;
  $('ai-progress').hidden=false;$('ai-error').hidden=true;
  $('ai-stage-label').textContent='准备发送提示词';$('ai-elapsed').textContent='0:00';
  $('ai-upload-wrap').hidden=false;$('ai-upload').max=Math.max(1,promptBytes);
  $('ai-upload').value=0;$('ai-upload-value').textContent='0%';
  if(aiPreviewFrame)$('ai-preview-status').textContent='上一次成功的 AI 画面';
  controls();
  try{
    const result=await client.submitAi(prompt,{
      onUpload:(sent,total)=>{const pct=total?Math.floor(sent*100/total):0;$('ai-upload').value=sent;$('ai-upload-value').textContent=`${pct}%`;$('ai-stage-label').textContent='正在发送提示词';},
      onStatus:status=>{$('ai-upload-wrap').hidden=true;applyAiStatus(status);},
      onTransport:state=>{if(state==='reconnecting')$('ai-stage-label').textContent='正在等待相框恢复蓝牙';}
    });
    $('ai-upload-wrap').hidden=true;
    applyAiStatus(result);
    if(result.state===AI_STATE.SUCCEEDED&&!result.error){
      message('AI 画面已显示成功。','success');
      if(result.hasPreview&&result.lastJobId)await loadAiPreview(result.lastJobId,{auto:true});
    }else{
      message(`AI 生图失败：${AI_FAILURE_HINTS[result.failureStage]||'请查看相框串口日志。'}`,'error');
    }
  }catch(error){
    message(error.message,'error');$('ai-stage-label').textContent='未完成';
  }finally{
    aiWorking=false;$('ai-upload-wrap').hidden=true;controls();
  }
});
async function showStatus(){
  const status=await client.readVerifiedStatus();
  const names=['空闲','正在接收图片','等待显示','正在刷屏','显示成功','显示失败','正在生成 AI 图片'];
  frameBusy=[STATE.RECEIVING,STATE.QUEUED,STATE.DISPLAYING,STATE.AI_BUSY].includes(status.state);
  if(status.state===STATE.AI_BUSY)aiBusy=true;
  else if(frameBusy)aiBusy=false;
  message(`相框状态：${names[status.state]}。已成功显示 ${status.displayed} 张。${status.error?`错误编号：${status.error}。`:''}`,status.state===5?'error':status.state===4?'success':'');
  controls();
}
async function syncAi(){
  if(!client.connected||!client.info?.ai){updateAiNotice();controls();return;}
  let status;
  try{status=await client.readVerifiedAiStatus();}
  catch(error){message(error.message,'error');updateAiNotice();controls();return;}
  applyAiStatus(status);
  if(aiBusy&&!aiWorking){
    aiWorking=true;controls();
    try{
      const final=await client.watchAi({jobId:status.jobId,onStatus:applyAiStatus,
        onTransport:state=>{if(state==='reconnecting')$('ai-stage-label').textContent='正在等待相框恢复蓝牙';}});
      applyAiStatus(final);
      if(final.state===AI_STATE.SUCCEEDED&&!final.error){
        message('AI 画面已显示成功。','success');
        if(final.hasPreview&&final.lastJobId)await loadAiPreview(final.lastJobId,{auto:true});
      }else message(`AI 生图失败：${AI_FAILURE_HINTS[final.failureStage]||'请查看相框串口日志。'}`,'error');
    }catch(error){message(error.message,'error');}
    finally{aiWorking=false;controls();}
  }else if(status.hasPreview&&status.lastJobId&&!aiPreviewFrame&&client.info.preview){
    aiPreviewJob=status.lastJobId;
    $('ai-preview-area').hidden=false;
    $('ai-preview-status').textContent='最近一次 AI 画面可读取';
    $('ai-preview-refresh').disabled=false;
    controls();
  }
}
$('connect').addEventListener('click',async()=>{
  if(client.connected){client.disconnect();message('已断开连接。');return;}
  $('connect').disabled=true;message('请在系统弹窗中选择 EPaperFrame。');
  try{await client.connect();await showStatus();await syncAi();}
  catch(error){message(error.name==='NotFoundError'?'已取消选择。准备好后可以重新连接。':error.message,'error');}
  finally{controls();}
});
$('refresh').addEventListener('click',async()=>{try{await showStatus();await syncAi();}catch(error){message(error.message,'error');}finally{controls();}});
$('send').addEventListener('click',async()=>{
  if(scheduled){cancelAnimationFrame(scheduled);render();}
  working=true;controls();$('transfer').hidden=false;$('progress').value=0;$('progress-value').textContent='0%';
  try{
    await client.send(frame,{onProgress:n=>{$('progress').value=n;$('progress-value').textContent=`${Math.floor(n/300)}%`;},onStage:stage=>{
      const labels={sending:'正在传输图片',displaying:'图片已接收，正在刷屏',reconnecting:'正在等待相框恢复蓝牙'};
      $('progress-label').textContent=labels[stage];$('send-label').textContent=stage==='sending'?'正在发送…':'等待显示…';
      message(stage==='sending'?'请保持手机在相框附近。':'刷屏期间蓝牙会暂时断开，请保持页面打开。');
    }});
    message('显示成功，相框已完成本次刷新。','success');$('progress-label').textContent='传输与显示已完成';
  }catch(error){message(error.message,'error');$('progress-label').textContent='请检查上方状态提示';}
  finally{working=false;$('send-label').textContent='发送到相框';controls();}
});
if(!client.secure||!client.bluetooth){$('compatibility').hidden=false;$('compatibility').textContent=!client.secure?'连接蓝牙需要 HTTPS。部署后请通过 HTTPS 地址打开；电脑本机可先预览、调整和导出图片。':'此浏览器不支持网页蓝牙。请用 Android 的 Chrome 打开；当前仍可预览和导出图片。';}
sample();updatePromptMeta();controls();
