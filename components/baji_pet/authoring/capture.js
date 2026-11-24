/* Runs inside the existing web renderer. The host calls capturePose once per
 * pose, keeping each transaction and the browser canvas memory bounded. */
async function setupPetCapture(motion = false, actions = ["idle", "pat"]) {
  const lcd = new PixelBadge(document.createElement("canvas"));
  lcd.resize(400);
  const output = document.createElement("canvas");
  output.width = output.height = 466;
  const ctx = output.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  const images = {};
  for (const clip of actions) {
    const image = new Image();
    image.src = "/assets/" + clip + ".png";
    await image.decode();
    images[clip] = image;
  }
  // Earlier versions of the web prototype store the catchlights entirely in
  // the atlas. Apply the close-view gaze accents at the same two landmarks.
  if (!window.FaceFX) {
    window.FaceFX = {draw(paint, display, {action, frame, clip, gazeX, gazeY}) {
      if (action === "pat" && frame >= 4 && frame <= 23) return;
      const [fx, fy, nx, ny] = clip.anchors[frame];
      for (const u of [-0.395, 0.375]) {
        const point = display.facePoint("close", clip, frame,
          nx + u*(ny-fy) - 0.285*(nx-fx), ny - u*(nx-fx) - 0.285*(ny-fy));
        const x = Math.round(point.sx) + Math.round(2*gazeX);
        const y = Math.round(point.sy) + Math.round(gazeY);
        for (const [dx, dy, w, h, color] of [[0,-1,2,2,"#463864"],[-2,-2,2,2,"#fffaff"],[2,2,1,1,"#f2e7ff"]]) {
          paint.fillStyle = color;
          paint.fillRect(x+dx, y+dy, w, h);
        }
      }
    }};
  }
  // On the original web renderer the body is drawn at the same place in the
  // layer stack, but there is no FaceFX call between its blit and props.
  let current;
  if (!lcd.draw.toString().includes("FaceFX.draw")) {
    const blit = lcd.paint.drawImage.bind(lcd.paint);
    lcd.paint.drawImage = function(sheet, ...args) {
      blit(sheet, ...args);
      if (current && sheet === current.sheet) window.FaceFX.draw(this,lcd,current);
    };
  }
  const missing = {percent: null, text: "--"};
  const scale = 466 / 184;
  const strip = document.createElement("canvas");
  strip.width = 368;
  strip.height = 368 + 5 * 48;
  const atlas = strip.getContext("2d");
  const crc = new Uint32Array(256);
  for (let i=0; i<256; ++i) {
    let value = i;
    for (let bit=0; bit<8; ++bit) value = (value>>>1) ^ ((value&1) ? 0xedb88320 : 0);
    crc[i] = value;
  }
  function wireChecksum() {
    const bytes = ctx.getImageData(0,0,466,466).data;
    let value = -1;
    for (let i=0; i<bytes.length; i+=4) {
      const redGreen = (bytes[i]&0xf8) | bytes[i+1]>>>5;
      const greenBlue = (bytes[i+1]&0x1c)<<3 | bytes[i+2]>>>3;
      value = crc[(value^redGreen)&255] ^ (value>>>8);
      value = crc[(value^greenBlue)&255] ^ (value>>>8);
    }
    return (~value)>>>0;
  }
  window.capturePose = ([action, frame, phase = 0, steps = 1, reaction = false]) => {
    const original = PIXEL_CLIPS[action];
    let clip = original;
    if (motion) {
      const next = frame+1 < original.frames ? original.anchors[frame+1] : PIXEL_CLIPS.idle.anchors[0];
      const anchors = original.anchors.slice();
      anchors[frame] = anchors[frame].map((v,i) => v + (next[i]-v) * phase/steps);
      clip = {...original, anchors, offset:frame*steps+phase-frame};
    }
    const [fx, fy, nx, ny] = clip.anchors[frame];
    const size = Math.hypot(nx-fx, ny-fy) / 132;
    if (reaction) {
      lcd.shift = lcd.computeShift("close",action,0,false);
      const landmarks = [];
      for (const [x, y] of [[fx,fy], [nx,ny], [nx-(ny-fy)*.52,ny+(nx-fx)*.52],
                              [nx+(ny-fy)*.52,ny-(nx-fx)*.52]]) {
        const point = lcd.facePoint("close",clip,frame,x,y);
        landmarks.push(Math.round(point.sx*scale), Math.round(point.sy*scale));
      }
      landmarks.push(...[91/0.75,75/0.75,39,32].map(r=>Math.round(r*size*scale/2.75)));
      if (reaction === "landmarks") return landmarks;
      current = {sheet:images[action],hands:null,clip,frame,view:"close",grid:true,
        agentState:"idle",action,code:action==="tickle"?"GIGGLE":"BLUSH",
        telemetry:{mode:"OFF",quota:missing,context:missing},tick:0,motion:true,skyMode:"dusk",
        helpers:0,gazeX:0,gazeY:0,sparks:[],contact:null,starToy:null};
      lcd.draw(current);
      ctx.fillStyle="black";
      ctx.fillRect(0,0,466,466);
      ctx.drawImage(lcd.canvas,0,0,466,466);
      return {landmarks, image:output.toDataURL().split(",")[1], crc:wireChecksum()};
    }
    const variants = [];
    for (let gy=-1; gy<=1; gy++) {
      for (let gx=-2; gx<=2; gx++) {
        current = {sheet:images[action], hands:null, clip, frame, view:"close", grid:true,
          agentState:"idle", action, code:"IDLE", telemetry:{mode:"OFF", quota:missing, context:missing},
          tick:0, motion:true, skyMode:"dusk", helpers:0, gazeX:gx/2, gazeY:gy,
          sparks:[], contact:null, starToy:null};
        lcd.draw(current);
        if (motion) {
          ctx.fillStyle = "black";
          ctx.fillRect(0,0,466,466);
          ctx.drawImage(lcd.canvas,0,0,466,466);
          variants.push(wireChecksum());
          if (gx === 0 && gy === 0) {
            atlas.fillStyle = "black";
            atlas.fillRect(0, 0, 368, 368);
            atlas.drawImage(lcd.canvas,0,0);
          }
          const g = (gy+1)*5+gx+2;
          atlas.fillStyle = "black";
          const x = (g%3)*112, y = 368+Math.floor(g/3)*48;
          atlas.fillRect(x, y, 112, 48);
          atlas.drawImage(lcd.canvas,128,176,112,48,x,y,112,48);
          continue;
        }
        ctx.fillStyle = "black";
        ctx.fillRect(0,0,466,466);
        ctx.drawImage(lcd.canvas,0,0,466,466);
        variants.push(output.toDataURL().split(",")[1]);
      }
    }
    const head = lcd.facePoint("close", clip, frame, fx, fy);
    return {variants, packed:motion && strip.toDataURL().split(",")[1], head:[head.sx*scale, head.sy*scale, 91*size/0.75/2.75*scale,
      75*size/0.75/2.75*scale].map(Math.round)};
  };
  return [PIXEL_CLIPS.idle.frames,PIXEL_CLIPS.pat.frames,PIXEL_CLIPS.idle.fps,PIXEL_CLIPS.pat.fps];
}
