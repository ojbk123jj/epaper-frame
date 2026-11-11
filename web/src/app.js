import {quantizeRgba,unpackFrame,cropRect} from './image.js';
import {BleClient} from './ble.js';
import {ImageSelection} from './selection.js';
const $=id=>document.getElementById(id);
const client=new BleClient();
const preview=$('preview'),ctx=preview.getContext('2d'),original=document.createElement('canvas');
original.width=400;original.height=300;
const draw=original.getContext('2d',{willReadFrequently:true});
let source=null,frame=null,viewOriginal=false,working=false,loading=false,scheduled=0;
function message(text,type=''){ $('message').textContent=text;$('message').className=`status-message ${type}`; }
function controls(){
  $('file').disabled=working;$('adjustments').disabled=working||loading;
  $('connect').disabled=working||client.connecting||!client.secure||!client.bluetooth;
  $('send').disabled=working||loading||client.connecting||!frame||!client.connected;
  $('download').disabled=working||loading||!frame;
  $('refresh').hidden=!client.connected;$('refresh').disabled=working||client.connecting;
  $('connect').textContent=client.connected?'断开连接':'连接相框';
}
client.onConnection=connected=>{
  $('device-dot').classList.toggle('connected',connected);
  $('device-name').textContent=connected?client.device.name||'EPaperFrame':working?'正在等待相框':'尚未连接相框';
  $('device-detail').textContent=connected?'已连接，可以发送图片':working?'刷屏期间暂时断开，完成后自动重连':'打开蓝牙，将手机放在相框附近';
  controls();
};
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
  c.fillStyle='#b3293b';for(const [x,y] of [[325,185],[420,245],[480,160]]){for(let i=0;i<5;i++){const a=i*Math.PI*2/5;c.beginPath();c.ellipse(x+Math.cos(a)*16,y+Math.sin(a)*16,23,18,a,0,Math.PI*2);c.fill();}c.fillStyle='#f0c72e';c.beginPath();c.arc(x,y,10,0,Math.PI*2);c.fill();c.fillStyle='#b3293b';}
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
$('download').addEventListener('click',()=>{
  if(!frame)return;const url=URL.createObjectURL(new Blob([frame],{type:'application/octet-stream'}));
  const a=document.createElement('a');a.href=url;a.download='epaper-400x300-2bpp.bin';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
});
async function showStatus(){
  const status=await client.readVerifiedStatus();
  const names=['空闲','正在接收图片','等待显示','正在刷屏','显示成功','显示失败','正在生成 AI 图片'];
  message(`相框状态：${names[status.state]}。已成功显示 ${status.displayed} 张。${status.error?`错误编号：${status.error}。`:''}`,status.state===5?'error':status.state===4?'success':'');
}
$('connect').addEventListener('click',async()=>{
  if(client.connected){client.disconnect();message('已断开连接。');return;}
  $('connect').disabled=true;message('请在系统弹窗中选择 EPaperFrame。');
  try{await client.connect();await showStatus();}
  catch(error){message(error.name==='NotFoundError'?'已取消选择。准备好后可以重新连接。':error.message,'error');}
  finally{controls();}
});
$('refresh').addEventListener('click',async()=>{try{await showStatus();}catch(error){message(error.message,'error');}finally{controls();}});
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
sample();controls();
