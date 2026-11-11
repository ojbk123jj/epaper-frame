export class ImageSelection {
  constructor({decode,onBusy=()=>{},onLoaded=()=>{},onError=()=>{}}) {
    Object.assign(this,{decode,onBusy,onLoaded,onError});this.ticket=0;
  }
  async load(file){
    const ticket=++this.ticket;
    let error;
    if(!['image/jpeg','image/png','image/webp'].includes(file.type))error='请选择 JPG、PNG 或 WebP 图片。';
    else if(file.size>20*1024*1024)error='图片超过 20 MB，请先缩小后再选择。';
    if(error){this.onBusy(false);this.onError(error);return;}
    this.onBusy(true);
    try{const result=await this.decode(file);if(ticket===this.ticket)this.onLoaded(result);}
    catch(cause){if(ticket===this.ticket)this.onError(cause.message);}
    finally{if(ticket===this.ticket)this.onBusy(false);}
  }
}
