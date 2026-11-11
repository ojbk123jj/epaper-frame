import { FRAME_SIZE } from './protocol.js';
export const PALETTE=[[0,0,0],[255,255,255],[255,255,0],[255,0,0]];
export function cropRect(width,height,zoom=1,x=.5,y=.5) {
  if(![width,height,zoom,x,y].every(Number.isFinite)||width<=0||height<=0||zoom<1) throw new Error('无效图片尺寸');
  const w=Math.min(width,height*4/3)/zoom,h=w*3/4;
  return {x:(width-w)*Math.max(0,Math.min(1,x)),y:(height-h)*Math.max(0,Math.min(1,y)),width:w,height:h};
}
export function quantizeRgba(rgba,dither=true) {
  if(rgba.length!==400*300*4) throw new Error('请先将图片缩放为 400 × 300');
  const frame=new Uint8Array(FRAME_SIZE);
  let current=new Float64Array(402*3),next=new Float64Array(402*3);
  for(let y=0;y<300;y++) {
    for(let x=0;x<400;x++) {
      const p=(y*400+x)*4,a=rgba[p+3]/255;
      const sample=[0,1,2].map(c=>Math.max(0,Math.min(255,rgba[p+c]*a+255*(1-a)+(dither?current[(x+1)*3+c]:0))));
      let best=0,distance=Infinity;
      for(let i=0;i<4;i++) {
        const d=3*(sample[0]-PALETTE[i][0])**2+6*(sample[1]-PALETTE[i][1])**2+(sample[2]-PALETTE[i][2])**2;
        if(d<distance){distance=d;best=i;}
      }
      frame[y*100+(x>>2)]|=best<<((3-(x&3))*2);
      if(dither) for(let c=0;c<3;c++) {
        const e=sample[c]-PALETTE[best][c];
        current[(x+2)*3+c]+=e*7/16; next[x*3+c]+=e*3/16;
        next[(x+1)*3+c]+=e*5/16; next[(x+2)*3+c]+=e/16;
      }
    }
    [current,next]=[next,current]; next.fill(0);
  }
  return frame;
}
export function unpackFrame(frame) {
  if(frame.length!==FRAME_SIZE) throw new Error('无效图片帧');
  const rgba=new Uint8ClampedArray(400*300*4);
  for(let i=0;i<400*300;i++) {
    const color=PALETTE[(frame[i>>2]>>((3-(i&3))*2))&3];
    rgba.set([...color,255],i*4);
  }
  return rgba;
}
