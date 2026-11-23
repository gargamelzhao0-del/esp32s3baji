/* Runs inside the existing web renderer. The host calls capturePose once per
 * pose, keeping each transaction and the browser canvas memory bounded. */
async function setupPetCapture() {
  const lcd = new PixelBadge(document.createElement("canvas"));
  lcd.resize(400);
  const output = document.createElement("canvas");
  output.width = output.height = 466;
  const ctx = output.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  const images = {};
  for (const clip of ["idle", "pat"]) {
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
  window.capturePose = ([action, frame]) => {
    const clip = PIXEL_CLIPS[action];
    const [fx, fy, nx, ny] = clip.anchors[frame];
    const size = Math.hypot(nx-fx, ny-fy) / 132;
    const variants = [];
    for (let gy=-1; gy<=1; gy++) {
      for (let gx=-2; gx<=2; gx++) {
        current = {sheet:images[action], hands:null, clip, frame, view:"close", grid:true,
          agentState:"idle", action, code:"IDLE", telemetry:{mode:"OFF", quota:missing, context:missing},
          tick:0, motion:true, skyMode:"dusk", helpers:0, gazeX:gx/2, gazeY:gy,
          sparks:[], contact:null, starToy:null};
        lcd.draw(current);
        ctx.fillStyle = "black";
        ctx.fillRect(0,0,466,466);
        ctx.drawImage(lcd.canvas,0,0,466,466);
        variants.push(output.toDataURL().split(",")[1]);
      }
    }
    const head = lcd.facePoint("close", clip, frame, fx, fy);
    return {variants, head:[head.sx*scale, head.sy*scale, 91*size/0.75/2.75*scale,
      75*size/0.75/2.75*scale].map(Math.round)};
  };
  return [PIXEL_CLIPS.idle.frames,PIXEL_CLIPS.pat.frames,PIXEL_CLIPS.idle.fps,PIXEL_CLIPS.pat.fps];
}
