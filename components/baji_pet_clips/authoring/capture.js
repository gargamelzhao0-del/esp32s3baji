/* Capture the renderer's own 184-cell physical-panel view, including its 2x LCD mesh.
 * Encode each native cell as a shared pair of RGB565 colours. */
async function prepare() {
  const lcd = new PixelBadge(document.createElement("canvas"), {panel:true});
  lcd.resize(400);
  const images = new Map(), palette = new Map(), usedImages=new Set();
  const expressions = ["idleSulky","idleSulkPeek","idleReunion","idleBond"];
  const agent = new Set(["working","reading","readingMoon","running","writing","searching",
                         "waiting","compact","error","limited","ended","cancelled","succeeded"]);
  const phys = 466/184;
  // This second canvas is the physical reference. Its browser resampler and
  // independent RGB565 wire checksum also cover partial-update correctness.
  const screen = document.createElement("canvas");
  screen.width=screen.height=466;
  const output=screen.getContext("2d",{willReadFrequently:true});
  output.imageSmoothingEnabled=false;
  const crc=new Uint32Array(256);
  for(let i=0;i<256;i++) {
    let k=i;
    for(let j=0;j<8;j++) k=(k>>>1)^((k&1)?0xedb88320:0);
    crc[i]=k;
  }
  function screenChecksum() {
    output.fillStyle="black";
    output.fillRect(0,0,466,466);
    output.drawImage(lcd.canvas,0,0,466,466);
    const p=output.getImageData(0,0,466,466).data;
    let value=-1;
    for(let i=0;i<p.length;i+=4) {
      const hi=(p[i]&0xf8)|(p[i+1]>>>5), lo=((p[i+1]&0x1c)<<3)|(p[i+2]>>>3);
      value=crc[(value^hi)&255]^(value>>>8);
      value=crc[(value^lo)&255]^(value>>>8);
    }
    return (~value)>>>0;
  }
  async function imageFor(name) {
    if (!images.has(name)) {
      const im = new Image();
      im.src = "/assets/"+name+".png";
      await im.decode();
      usedImages.add("assets/"+name+".png");
      if (images.size >= 4) images.delete(images.keys().next().value);
      images.set(name,im);
    }
    return images.get(name);
  }
  function rgb(buffer, offset) {
    return (buffer[offset]>>3)<<11 | (buffer[offset+1]>>2)<<5 | buffer[offset+2]>>3;
  }
  window.captureClip = async ([name,scene]) => {
    const action=expressions.includes(name)?"idle":name;
    const source = PIXEL_CLIPS[action], clip = source.scenes?.[scene] || source;
    const img = await imageFor(clip.atlas || action);
    const gear = PIXEL_CLIPS[clip.gear];
    const hands = gear && {clip:gear,sheet:await imageFor(gear.gearAtlas),frame:0};
    const frames = new Uint16Array(184*184*clip.frames);
    const metadata = [];
    const activity = scene || (agent.has(action) ? action : "idle");
    for (let f=0;f<clip.frames;f++) {
      lcd.draw({sheet:img, hands, clip, frame:f, view:"full", grid:true, skyMode:"dusk",
        agentState:activity,
        action, tick:Math.floor(1000*f/clip.fps), motion:true, sparks:[],
        sulkPhase:name==="idleSulkPeek"?"peek":name==="idleSulky"?"away":null,
        reunion:name==="idleReunion", bondLevel:name==="idleBond"?3:0,
        offeredFringe:action.startsWith("browInvite") && f===23 ? (action.endsWith("Left")?-1:1) : 0,
        offeredBubble:action==="feed" && f/clip.fps>=3.24 && !!clip.bubbles?.[f],
        telemetry:{mode:"OFF",quota:{percent:null,text:"--"},context:{percent:null,text:"--"}}});
      const pix = lcd.ctx.getImageData(0,0,368,368).data;
      const frameOffset=f*184*184;
      for(let y=0;y<184;y++) for(let x=0;x<184;x++) {
        const i = 4*(y*2*368+x*2);
        const grid=rgb(pix,i), base=rgb(pix,i+4*369);
        if (rgb(pix,i+4)!==grid || rgb(pix,i+4*368)!==grid) throw Error("LCD mesh mismatch");
        const key=grid*65536+base;
        let id=palette.get(key);
        if (id===undefined) {id=palette.size;palette.set(key,id);}
        if (id>65535) throw Error("palette exceeds 16 bits");
        frames[frameOffset+y*184+x]=id;
      }
      const map=(x,y)=> {
        const p=lcd.facePoint("full",clip,f,x,y);
        return [Math.round(p.sx*phys),Math.round(p.sy*phys)];
      };
      const a=clip.anchors[f], axes=[];
      for(let i=0;i<10;i+=2) axes.push(...map(a[i],a[i+1]));
      const px=phys/(clip.fullStep||4);
      const palms=[];
      if (clip.palms) for(let i=0;i<4;i+=2) {
        const [x,y]=map(...clip.palms[f].slice(i,i+2));
        palms.push(x,Math.round(y-4*phys));
      }
      const p=NapProp.position(lcd,clip,f,"full");
      let cushion=null;
      if (p && p.open>=.9) {
        const bits=new Uint8Array(Math.ceil(p.w*p.h/8));
        for(let y=0;y<p.h;y++) for(let x=0;x<p.w;x++) if (NapProp.contains(p,p.x+x,p.y+y,null,true)) {
          const k=y*p.w+x; bits[k>>3]|=1<<(k%8);
        }
        cushion=[p.x,p.y,p.w,p.h,btoa(String.fromCharCode(...bits))];
      }
      let prop=null, kind=0;
      if (f/clip.fps>=3.24 && clip.bubbles?.[f]) {
        const [x,y,r]=clip.bubbles[f]; prop=[...map(x,y),Math.round((r+12)*px),Math.round((r+12)*px)]; kind=9;
      } else if (clip.book?.[f]) {
        prop=[...map(...clip.book[f]),Math.round(30*px),Math.round(28*px)];kind=10;
      } else if (clip.target?.[f]) {
        const key=scene||action;
        kind=/search|lens/i.test(key)?12:/wait|palm/i.test(key)?13:11;
        prop=[...map(...clip.target[f]),Math.round((kind===12?27:24)*px),Math.round((kind===12?27:22)*px)];
      }
      metadata.push({axes,palms,prop,kind,cushion,scale:px,webCRC:screenChecksum()});
    }
    const bytes=await new Response(new Blob([frames.buffer]).stream().pipeThrough(new CompressionStream("deflate"))).arrayBuffer();
    const a=new Uint8Array(bytes), chunks=[];
    for(let i=0;i<a.length;i+=0x4000)chunks.push(String.fromCharCode(...a.subarray(i,i+0x4000)));
    return {metadata,data:btoa(chunks.join("")),fps:clip.fps};
  };
  window.exportPalette=()=>Array.from(palette.keys());
  window.exportImages=()=>Array.from(usedImages);
  return [...Object.entries(PIXEL_CLIPS).flatMap(([name,c])=>[[name,""],...Object.keys(c.scenes||{}).map(s=>[name,s])]),
          ...expressions.map(name=>[name,""])];
}
