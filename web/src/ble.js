import {UUID,FRAME_SIZE,STATE,encodeBegin,encodeControl,encodeData,parseInfo,parseStatus} from './protocol.js';

function bounded(promise,ms,message) {
  let timer;
  return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(message)),ms);})])
    .finally(()=>clearTimeout(timer));
}
export class BleClient {
  constructor({bluetooth=globalThis.navigator?.bluetooth,secure=globalThis.isSecureContext,
    sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>Date.now(),
    newJobId=()=>crypto.getRandomValues(new Uint32Array(1))[0]||1,ioTimeoutMs=10000,resultTimeoutMs=90000}={}) {
    Object.assign(this,{bluetooth,secure,sleep,now,newJobId,ioTimeoutMs,resultTimeoutMs});
    this.device=null;this.info=null;this.sending=false;this.connecting=false;this.pendingJob=null;
    this.pendingDeviceId=null;this.epoch=0;this.onConnection=()=>{};
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
    this.chars=chars;this.info=info;this.onConnection(true);return info;
  }
  async connect() {
    if(this.connecting)throw new Error('正在连接相框，请等待系统选择与连接完成');
    if(this.sending)throw new Error('正在发送图片，请等待当前任务结束');
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
  async send(frame,{mode=0,onProgress=()=>{},onStage=()=>{}}={}) {
    if(this.sending)throw new Error('相框忙，请等待当前任务结束');
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
