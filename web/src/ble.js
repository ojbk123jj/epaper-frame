import {UUID,FRAME_SIZE,STATE,AI_STATE,encodeBegin,encodeControl,encodeData,
  encodeAiBegin,encodeAiCommit,encodePreview,parseInfo,parseStatus,parseAiStatus,
  parsePreviewChunk} from './protocol.js';

function bounded(promise,ms,message) {
  let timer;
  return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(message)),ms);})])
    .finally(()=>clearTimeout(timer));
}
const AI_BUSY_STATES=[AI_STATE.QUEUED,AI_STATE.CONNECTING,AI_STATE.GENERATING,
  AI_STATE.DOWNLOADING,AI_STATE.CONVERTING,AI_STATE.DISPLAYING];
export class BleClient {
  constructor({bluetooth=globalThis.navigator?.bluetooth,secure=globalThis.isSecureContext,
    sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>Date.now(),
    newJobId=()=>crypto.getRandomValues(new Uint32Array(1))[0]||1,ioTimeoutMs=10000,
    resultTimeoutMs=90000,aiResultTimeoutMs=900000,previewTimeoutMs=120000}={}) {
    Object.assign(this,{bluetooth,secure,sleep,now,newJobId,ioTimeoutMs,resultTimeoutMs,
      aiResultTimeoutMs,previewTimeoutMs});
    this.device=null;this.info=null;this.sending=false;this.connecting=false;this.pendingJob=null;
    this.pendingDeviceId=null;this.pendingAiJob=null;this.pendingAiDeviceId=null;
    this.previewing=false;this.watchingAi=false;this.epoch=0;this.onConnection=()=>{};
    this.disconnected=()=>{this.info=null;this.onConnection(false);};
  }
  get connected(){return Boolean(this.device?.gatt.connected&&this.info);}
  async _io(promise){return bounded(promise,this.ioTimeoutMs,'蓝牙操作超时，请靠近相框后重试');}
  async _attach(epoch,device=this.device) {
    const server=await this._io(device.gatt.connect());
    if(epoch!==this.epoch||device!==this.device){device.gatt.disconnect();throw new Error('连接已取消');}
    const service=await this._io(server.getPrimaryService(UUID.service));
    const chars={};
    for(const key of ['info','control','data','status'])chars[key]=await this._io(service.getCharacteristic(UUID[key]));
    const info=parseInfo(await this._io(chars.info.readValue()));
    if(epoch!==this.epoch)throw new Error('连接已取消');
    if(info.ai)chars.ai=await this._io(service.getCharacteristic(UUID.ai));
    if(info.preview)chars.preview=await this._io(service.getCharacteristic(UUID.preview));
    if(epoch!==this.epoch)throw new Error('连接已取消');
    this.chars=chars;this.info=info;this.onConnection(true);return info;
  }
  async connect() {
    if(this.connecting)throw new Error('正在连接相框，请等待系统选择与连接完成');
    if(this.sending)throw new Error('正在发送图片，请等待当前任务结束');
    if(this.watchingAi)throw new Error('正在监控 AI 任务，请等待完成');
    if(this.previewing)throw new Error('正在读取 AI 预览，请等待完成');
    if(!this.secure)throw new Error('请通过 HTTPS 打开网站；电脑本机可使用 localhost');
    if(!this.bluetooth?.requestDevice)throw new Error('请使用 Android 的 Chrome 浏览器打开此页面');
    this.connecting=true;
    this.disconnect();const epoch=this.epoch;let device;
    try {
      /* Keep requestDevice in the original click activation, before any await. */
      device=await this.bluetooth.requestDevice({filters:[{services:[UUID.service]}]});
      if(epoch!==this.epoch)throw new Error('连接已取消');
      this.device=device;device.addEventListener('gattserverdisconnected',this.disconnected);
      return await this._attach(epoch,device);
    } catch(error) {
      if(epoch===this.epoch)this.disconnect();
      else if(device&&device!==this.device)device.gatt.disconnect();
      throw error;
    } finally {this.connecting=false;}
  }
  disconnect() {
    this.epoch++;
    if(this.device){this.device.removeEventListener('gattserverdisconnected',this.disconnected);this.device.gatt.disconnect();}
    this.info=null;this.onConnection(false);
  }
  async readStatus(){
    if(!this.connected)throw new Error('请先连接相框');
    return parseStatus(await this._io(this.chars.status.readValue()));
  }
  async readAiStatus(){
    if(!this.connected)throw new Error('请先连接相框');
    if(!this.chars.ai)throw new Error('当前固件不支持 AI 生图');
    return parseAiStatus(await this._io(this.chars.ai.readValue()));
  }
  async readVerifiedStatus(){
    const status=await this.readStatus();
    if(this.pendingJob!==null) {
      if(this.device.id!==this.pendingDeviceId)throw new Error('这不是上次发送的设备，请重新连接原相框核对显示结果');
      if(status.jobId!==this.pendingJob)throw new Error('相框任务已变化，无法确认上次图片是否显示；请先目视检查相框');
      if((status.state===STATE.SUCCEEDED&&!status.error)||status.state===STATE.FAILED) {
        this.pendingJob=null;this.pendingDeviceId=null;
      }
    }
    return status;
  }
  async readVerifiedAiStatus(){
    const status=await this.readAiStatus();
    if(this.pendingAiJob!==null) {
      if(this.device.id!==this.pendingAiDeviceId)throw new Error('这不是上次提交的设备，请重新连接原相框核对 AI 结果');
      if(status.jobId!==this.pendingAiJob) {
        const frame=await this.readStatus();
        if(![STATE.RECEIVING,STATE.QUEUED,STATE.DISPLAYING,STATE.AI_BUSY].includes(frame.state)) {
          this.pendingAiJob=null;this.pendingAiDeviceId=null;
          throw new Error('未找到上次 AI 任务，可能未提交或被后续任务覆盖；请目视检查相框');
        }
        throw new Error('相框 AI 任务已变化，请稍后重新查询');
      }
      if(status.state===AI_STATE.SUCCEEDED||status.state===AI_STATE.FAILED) {
        this.pendingAiJob=null;this.pendingAiDeviceId=null;
      }
    }
    return status;
  }
  async send(frame,{mode=0,onProgress=()=>{},onStage=()=>{}}={}) {
    if(this.sending)throw new Error('相框忙，请等待当前任务结束');
    if(this.watchingAi)throw new Error('相框正在执行 AI 任务，请等待完成');
    if(this.previewing)throw new Error('正在读取 AI 预览，请等待完成');
    if(this.pendingAiJob!==null)throw new Error('上次 AI 任务结果尚未确认，请先查询相框状态');
    if(!this.connected)throw new Error('请先连接相框');
    if(this.pendingJob!==null)throw new Error('上次任务结果尚未确认，请先查询原相框状态');
    if(!(frame instanceof Uint8Array)||frame.length!==FRAME_SIZE)throw new Error('无效图片帧');
    this.sending=true;
    const epoch=this.epoch,bytes=frame.slice(),jobId=this.newJobId();
    let commitAttempted=false;
    this.pendingJob=jobId;
    this.pendingDeviceId=this.device.id;
    try {
      const status=await this.readStatus();
      if([STATE.RECEIVING,STATE.QUEUED,STATE.DISPLAYING,STATE.AI_BUSY].includes(status.state))throw new Error('相框忙，请等待当前任务结束');
      if(mode!==0&&!(this.info.modes&2))throw new Error('相框不支持快速刷新');
      const chunkSize=this.info.maxWrite-4;
      await this._io(this.chars.control.writeValueWithResponse(encodeBegin(jobId,bytes,mode)));
      const begun=await this.readStatus();
      if(begun.jobId!==jobId||begun.state!==STATE.RECEIVING||begun.error)throw new Error('相框未接受图片传输');
      onStage('sending');
      for(let offset=0;offset<bytes.length;offset+=chunkSize) {
        if(epoch!==this.epoch)throw new Error('传输已取消');
        const block=bytes.subarray(offset,offset+chunkSize);
        await this._io(this.chars.data.writeValueWithResponse(encodeData(offset,block)));
        onProgress(offset+block.length);
      }
      if(epoch!==this.epoch)throw new Error('传输已取消');
      commitAttempted=true;onStage('displaying');
      try{await this._io(this.chars.control.writeValueWithResponse(encodeControl(2,jobId)));}
      catch{/* Display may close BLE before the COMMIT response reaches the browser. Read back; never resubmit. */}
      const result=await this._awaitResult(jobId,epoch,onStage);
      this.pendingJob=null;this.pendingDeviceId=null;return result;
    } catch(error) {
      if(!commitAttempted) {
        /* A timed-out write may still be in flight. Disconnect instead of issuing
         * overlapping GATT operations; the peripheral cancels its reservation. */
        this.disconnect();this.pendingJob=null;this.pendingDeviceId=null;
      }
      throw error;
    } finally {this.sending=false;}
  }
  async submitAi(prompt,{onUpload=()=>{},onStatus=()=>{},onTransport=()=>{}}={}) {
    if(this.sending)throw new Error('相框忙，请等待当前任务结束');
    if(this.watchingAi)throw new Error('正在监控 AI 任务');
    if(this.previewing)throw new Error('正在读取 AI 预览，请等待完成');
    if(!this.connected)throw new Error('请先连接相框');
    if(!this.info.ai)throw new Error('当前固件不支持 AI 生图');
    if(!this.info.aiConfigured)throw new Error('相框未配置 Wi-Fi 或 ModelScope Token');
    if(this.pendingJob!==null)throw new Error('上次图片任务结果尚未确认，请先查询相框状态');
    if(this.pendingAiJob!==null)throw new Error('上次 AI 任务结果尚未确认，请先查询相框状态');
    if(!this.chars.ai)throw new Error('相框 AI 状态特征不可用');
    const bytes=new TextEncoder().encode(prompt);
    if(!bytes.length||bytes.length>this.info.maxPrompt||bytes.includes(0))
      throw new Error(`提示词需为 1 到 ${this.info.maxPrompt} 个 UTF-8 字节`);
    const status=await this.readStatus();
    if([STATE.RECEIVING,STATE.QUEUED,STATE.DISPLAYING,STATE.AI_BUSY].includes(status.state))
      throw new Error('相框忙，请等待当前任务结束');
    const ai=await this.readAiStatus();
    if(AI_BUSY_STATES.includes(ai.state))throw new Error('相框正在执行 AI 任务，请等待完成');
    this.sending=true;
    const epoch=this.epoch,jobId=this.newJobId();
    let commitAttempted=false;
    this.pendingAiJob=jobId;this.pendingAiDeviceId=this.device.id;
    try {
      await this._io(this.chars.control.writeValueWithResponse(encodeAiBegin(jobId,bytes)));
      onUpload(0,bytes.length);
      const chunkSize=this.info.maxWrite-4;
      for(let offset=0;offset<bytes.length;offset+=chunkSize) {
        if(epoch!==this.epoch)throw new Error('提示词上传已取消');
        const block=bytes.subarray(offset,offset+chunkSize);
        await this._io(this.chars.data.writeValueWithResponse(encodeData(offset,block,bytes.length)));
        onUpload(offset+block.length,bytes.length);
      }
      if(epoch!==this.epoch)throw new Error('提示词上传已取消');
      commitAttempted=true;
      try{await this._io(this.chars.control.writeValueWithResponse(encodeAiCommit(jobId)));}
      catch{/* COMMIT may still be accepted. Never resubmit; read the AI status back. */}
      return await this._awaitAiResult(jobId,epoch,{onStatus,onTransport});
    } catch(error) {
      if(!commitAttempted) {
        this.disconnect();this.pendingAiJob=null;this.pendingAiDeviceId=null;
      }
      throw error;
    } finally {this.sending=false;}
  }
  async watchAi({jobId=null,onStatus=()=>{},onTransport=()=>{},timeoutMs=this.aiResultTimeoutMs}={}) {
    if(this.watchingAi)throw new Error('正在监控 AI 任务');
    if(!this.connected)throw new Error('请先连接相框');
    if(!this.info.ai)throw new Error('当前固件不支持 AI 生图');
    if(jobId===null)jobId=(await this.readAiStatus()).jobId;
    if(!Number.isInteger(jobId)||jobId<=0)throw new Error('相框没有可监控的 AI 任务');
    this.watchingAi=true;
    try{return await this._awaitAiResult(jobId,this.epoch,{onStatus,onTransport,timeoutMs});}
    finally{this.watchingAi=false;}
  }
  async _awaitAiResult(jobId,epoch,{onStatus=()=>{},onTransport=()=>{},timeoutMs=this.aiResultTimeoutMs}={}) {
    const deadline=this.now()+timeoutMs;
    while(this.now()<deadline) {
      if(epoch!==this.epoch)throw new Error('已停止等待；相框可能仍在生图，可重新连接查看结果');
      if(!this.connected) {
        onTransport('reconnecting');
        try{await this._attach(epoch);}catch{await this.sleep(1000);continue;}
      }
      let status;
      try{status=await this.readAiStatus();}catch{this.info=null;await this.sleep(1000);continue;}
      if(status.jobId!==jobId)throw new Error('相框 AI 任务已变化，请重新连接确认结果');
      onStatus(status);onTransport('connected');
      if(status.state===AI_STATE.SUCCEEDED&&!status.error) {
        if(this.pendingAiJob===jobId){this.pendingAiJob=null;this.pendingAiDeviceId=null;}
        return status;
      }
      if(status.state===AI_STATE.FAILED||status.error) {
        if(this.pendingAiJob===jobId){this.pendingAiJob=null;this.pendingAiDeviceId=null;}
        return status;
      }
      if(status.state===AI_STATE.IDLE)throw new Error('相框已重启或 AI 任务丢失，无法确认结果');
      await this.sleep(1000);
    }
    throw new Error('等待 AI 生图超时；相框可能仍在生成，请重新连接查询');
  }
  async fetchAiPreview(jobId,{onProgress=()=>{}}={}) {
    if(this.previewing)throw new Error('正在读取 AI 预览，请等待完成');
    if(this.sending||this.watchingAi)throw new Error('相框正在处理任务，请等待完成');
    if(!this.connected)throw new Error('请先连接相框');
    if(!this.info.preview||!this.chars.preview)throw new Error('当前固件不支持 AI 画面回读');
    if(!Number.isInteger(jobId)||jobId<=0)throw new Error('无效的 AI 任务编号');
    this.previewing=true;
    const result=new Uint8Array(FRAME_SIZE);
    const deadline=this.now()+this.previewTimeoutMs;
    let received=0,restarts=0;
    try {
      await this._io(this.chars.control.writeValueWithResponse(encodePreview(jobId)));
      while(received<FRAME_SIZE) {
        if(this.now()>deadline)throw new Error('读取 AI 预览超时，请重试');
        const chunk=parsePreviewChunk(await this._io(this.chars.preview.readValue()));
        if(chunk.offset!==received) {
          if(++restarts>1)throw new Error('预览读取偏移不一致，请重新获取');
          await this._io(this.chars.control.writeValueWithResponse(encodePreview(jobId)));
          received=0;continue;
        }
        if(!chunk.data.length)throw new Error('相框返回了空的预览分块');
        if(received+chunk.data.length>FRAME_SIZE)throw new Error('预览数据超出范围');
        result.set(chunk.data,received);received+=chunk.data.length;onProgress(received);
      }
      return result;
    } finally {this.previewing=false;}
  }
  async _awaitResult(jobId,epoch,onStage) {
    const deadline=this.now()+this.resultTimeoutMs;
    while(this.now()<deadline) {
      if(epoch!==this.epoch)throw new Error('已停止等待；相框可能仍在刷屏，可重新连接查看结果');
      if(!this.connected) {
        onStage('reconnecting');
        try{await this._attach(epoch);}catch{await this.sleep(1000);continue;}
      }
      let status;
      try{status=await this.readStatus();}catch{this.info=null;await this.sleep(1000);continue;}
      if(status.jobId!==jobId)throw new Error('相框任务编号已变化，请检查相框是否重启或被其他设备更新');
      if(status.state===STATE.SUCCEEDED&&!status.error)return status;
      if(status.state===STATE.FAILED||status.error)throw new Error(`相框未完成显示（错误 ${status.error}），请查看设备日志`);
      if(status.state===STATE.RECEIVING)throw new Error('相框未确认提交，请断开连接后重新发送');
      onStage('displaying');await this.sleep(1000);
    }
    throw new Error('等待显示结果超时；请重新连接查看状态，避免重复发送');
  }
}
