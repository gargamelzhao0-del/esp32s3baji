"""Exercise the cushion on the actual badge, including native pixel readback."""
import argparse
import functools
from math import dist
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

from contact_check import PAGE, install_pet_clock, sync_playwright


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def audit_drawings(browser, url):
    page = browser.new_page()
    install_pet_clock(page)
    page.goto(url)
    page.wait_for_function("sheet && active==='idle'", polling=20)
    for view in ("full", "close"):
        takes = page.evaluate("""async crop => {
          const result={};
          const probe=document.createElement('canvas');
          probe.width=probe.height=184;
          const mask=probe.getContext('2d',{willReadFrequently:true});
          const actor=probe.cloneNode().getContext('2d',{willReadFrequently:true});
          for (const name of ['doze','peekLeft','peekRight','wakeLeft','wakeRight']) {
            const clip=PIXEL_CLIPS[name], atlas=await loadSheet(name);
            const frames=result[name]=[];
            for(let i=0;i<clip.frames;i++) {
              const p=NapProp.position(lcd,clip,i,crop);
              lcd.draw({sheet:atlas,clip,frame:i,view:crop,code:'NAP',agentState:'idle',
                        action:name,telemetry,tick:0,motion:true});
              const pixels=lcd.paint.getImageData(0,0,184,184).data;
              mask.clearRect(0,0,184,184);
              NapProp.pillow(mask,p,null,true);
              const ink=mask.getImageData(0,0,184,184).data;
              const [fx,fy,nx,ny]=clip.anchors[i];
              const [lx,ly,rx,ry]=clip.cushion[i];
              const point=(x,y)=>lcd.facePoint(crop,clip,i,x,y);
              const unit=11.25/Math.hypot(nx-fx,ny-fy);
              const lip=point(nx+unit*(nx-fx),ny+unit*(ny-fy));
              const grips=[point(lx,ly),point(rx,ry)];
              let visible=0,lowest=0,lipGap=184,clothX=0;
              const top=Array(184).fill(184);
              const touched=[184,184], covered=[0,0];
              for(let k=0;k<ink.length;k+=4) {
                if (!ink[k+3]) continue;
                const x=k/4%184,y=Math.floor(k/4/184);
                const front=ink[k]!==pixels[k] || ink[k+1]!==pixels[k+1] || ink[k+2]!==pixels[k+2];
                if(p.open>=.9 && y<149 && NapProp.contains(p,x,y,null,true)===front)
                  throw Error([crop,name,i,x,y,'paint and touch disagree'].join(' '));
                if(front) {
                  for(let g=0;g<2;g++)
                    if(Math.hypot(x-grips[g].sx,y-grips[g].sy)<8) covered[g]++;
                } else {
                  visible++; lowest=Math.max(lowest,y); clothX+=x+.5;
                  top[x]=Math.min(top[x],y);
                  lipGap=Math.min(lipGap,Math.hypot(x+.5-lip.sx,y+.5-lip.sy));
                  for(let g=0;g<2;g++)
                    touched[g]=Math.min(touched[g],Math.hypot(x+.5-grips[g].sx,y+.5-grips[g].sy));
                }
              }
              let support=null;
              if(name==='doze' && i>=15) {
                const by=crop==='close'?22:18;
                const row=Math.floor(i/8)+(crop==='close'?Math.ceil(clip.frames/8):0);
                actor.clearRect(0,0,184,184);
                actor.drawImage(atlas,(i%8)*138,row*132,138,132,23,by,138,132);
                const body=actor.getImageData(0,0,184,184).data;
                const skin=(x,y)=>{
                  const k=(y*184+x)*4;
                  return (body[k]===255 && body[k+1]===240 && body[k+2]===230) ||
                         (body[k]===238 && body[k+1]===222 && body[k+2]===211);
                };
                const nose=point(nx,ny), brow=point(fx,fy);
                // Transform the original eye centers (208,280) and (332,277)
                // around the nose (270,305) using the local forehead-to-nose
                // axis. Trace the dark eyelid from its centre, so a separate
                // strand of hair near the corner cannot masquerade as an eye.
                const dx=nose.sx-brow.sx,dy=nose.sy-brow.sy;
                const axisX=(dx*7+dy*132)/(7*7+132*132);
                const axisY=(dy*7-dx*132)/(7*7+132*132);
                const eyelidPixels=new Set();
                for(const center of [-62,62]) {
                  const candidates=new Set(), vEye=center<0?-25:-28;
                  const cx=nose.sx+center*axisX-vEye*axisY,cy=nose.sy+center*axisY+vEye*axisX;
                  let seed=null,nearest=184;
                  const norm=axisX*axisX+axisY*axisY,bound=Math.ceil(40*Math.sqrt(norm));
                  for(let x=Math.floor(cx)-bound;x<=Math.ceil(cx)+bound;x++)
                    for(let y=Math.floor(cy)-bound;y<=Math.ceil(cy)+bound;y++) {
                    // Inverse-map pixel centres. Rounding portrait samples
                    // outward here would include the jaw under the outer lash.
                    const px=x+.5-nose.sx,py=y+.5-nose.sy;
                    const u=(px*axisX+py*axisY)/norm,v=(py*axisX-px*axisY)/norm;
                    if(Math.abs(u-center)>32 || v< -39 || v> -17) continue;
                    const k=4*(184*y+x);
                    if(body[k+3]!==255 || body[k]>=124 || body[k+1]>=115) continue;
                    candidates.add(k);
                    const distance=Math.hypot(x+.5-cx,y+.5-cy);
                    if(distance<nearest){seed=k;nearest=distance;}
                  }
                  if(seed===null) throw Error("missing eyelid "+[crop,i,center]);
                  const connected=[seed];candidates.delete(seed);
                  for(let j=0;j<connected.length;j++){
                    const k=connected[j];eyelidPixels.add(k);
                    for(const dy of [-1,0,1]) for(const dx of [-1,0,1]){
                      const next=k+4*(184*dy+dx);
                      if(candidates.delete(next)) connected.push(next);
                    }
                  }
                }
                const hiddenEye=[...eyelidPixels].filter(k=>
                  pixels[k]!==body[k] || pixels[k+1]!==body[k+1] || pixels[k+2]!==body[k+2]).length;
                // Follow the cheek fill connected to the nose. The dark jaw
                // contour separates this component from fingers and costume.
                const nx0=Math.floor(nose.sx),ny0=Math.floor(nose.sy);
                const queue=[],seen=new Set(),jaw=Array(184).fill(-1);
                for(let y=ny0-1;y<=ny0+1;y++) for(let x=nx0-1;x<=nx0+1;x++)
                  if(skin(x,y)){queue.push(y*184+x);seen.add(y*184+x);}
                for(let index=0;index<queue.length;index++){
                  const k=queue[index],x=k%184,y=Math.floor(k/184);
                  jaw[x]=Math.max(jaw[x],y);
                  for(const n of [k-1,k+1,k-184,k+184]){
                    const xx=n%184,yy=Math.floor(n/184);
                    if(seen.has(n) || Math.abs(xx-nx0)>30 ||
                        yy<ny0-4 || yy>lip.sy+13 || !skin(xx,yy)) continue;
                    seen.add(n);queue.push(n);
                  }
                }
                let span=0,run=0,penetration=0;
                for(let x=Math.ceil(nose.sx);x<nose.sx+(crop==='close'?20:14);x++){
                  const gap=top[x]-jaw[x]-1;
                  if(jaw[x]>=0){
                    penetration=Math.max(penetration,-gap);
                    run=gap>=-1 && gap<=2 ? run+1 : 0;
                  } else run=0;
                  span=Math.max(span,run);
                }
                const neutral=PIXEL_CLIPS.doze.cushion[0];
                const chest=point((neutral[0]+neutral[2])/2,0);
                const inset=grips.map(g=>Math.min(g.sx-p.x,p.x+p.w-1-g.sx)/p.w);
                // Count real skin-colored hand pixels with fabric directly
                // behind them. The (larger) sleeve mask alone cannot pass this.
                const handCoverage=grips.map(g=>{
                  const r=crop==='close'?5:4;
                  let hand=0,overFabric=0;
                  for(let y=Math.floor(g.sy-r);y<=Math.ceil(g.sy+r);y++){
                    for(let x=Math.floor(g.sx-r);x<=Math.ceil(g.sx+r);x++){
                      const k=(184*y+x)*4;
                      if(!skin(x,y) || Math.hypot(x+.5-g.sx,y+.5-g.sy)>r) continue;
                      hand++;
                      if(ink[k+3] && pixels[k]===body[k] && pixels[k+1]===body[k+1] &&
                          pixels[k+2]===body[k+2]) overFabric++;
                    }
                  }
                  return hand?overFabric/hand:0;
                });
                // From each wrist, follow the rendered sleeve/portrait ink to
                // the outer edge of the cloud. A detached hand cannot get there.
                const connected=grips.map(g=>{
                  const start=184*Math.floor(g.sy)+Math.floor(g.sx);
                  const queue=[start],seen=new Set(queue);
                  for(let j=0;j<queue.length;j++){
                    const k=queue[j];
                    if(!ink[4*k+3]) return true;
                    for(const dy of [-1,0,1]) for(const dx of [-1,0,1]){
                      const next=k+dy*184+dx,m=next*4;
                      if(seen.has(next) || body[m+3]!==255 ||
                          pixels[m]!==body[m] || pixels[m+1]!==body[m+1] || pixels[m+2]!==body[m+2]) continue;
                      seen.add(next);queue.push(next);
                    }
                  }
                  return false;
                });
                support={right:clothX/visible-chest.sx,width:p.w,inset,span,penetration,
                         eyelids:eyelidPixels.size,hiddenEye,handCoverage,connected};
              }
              frames.push({visible,lowest,lipGap,touched,covered,support,hem:p?[p.x,p.y+p.h-1]:null,
                           open:clip.cushion[i][5],weight:clip.cushion[i][6]});
            }
          }
          return result;
        }""", view)
        for name, frames in takes.items():
            for i, f in enumerate(frames):
                assert f["lowest"] < 149, (view, name, i, "below meter", f)
                assert f["lipGap"] >= 2, (view, name, i, "fabric on mouth", f)
                if f["visible"]:
                    assert min(f["touched"]) < 8, (view, name, i, "unsupported fabric", f)
                if f["open"] == 100:
                    assert min(f["covered"]) > 0, (view, name, i, "palm behind fabric", f)
                    assert max(f["touched"]) < 7, (view, name, i, "lost corner", f)
        for side in ("Left", "Right"):
            sleep, peek, wake = takes["doze"][15], takes["peek" + side], takes["wake" + side]
            assert all(dist(sleep["hem"], f["hem"]) <= 1.5 for f in peek), (view, side, "pillow followed head")
            assert peek[0]["weight"] > peek[-1]["weight"] > wake[6]["weight"]
            assert wake[6]["open"] == 100 and all(not f["visible"] for f in wake[18:])
        assert takes["doze"][10]["open"] > takes["doze"][10]["weight"]
        lip = min(f["lipGap"] for frames in takes.values() for f in frames)
        held = [f["support"] for f in takes["doze"] if f["support"]]
        print(f"{view}: settled cloth {min(f['right'] for f in held):.1f} px right of chest, "
              f"inner wrist margins {min(min(f['inset']) for f in held):.0%} of pillow width, "
              f"continuous cheek contact {min(f['span'] for f in held)} columns, "
              f"maximum cheek overlap {max(f['penetration'] for f in held)} px.")
        for i,f in enumerate(held, start=15):
            assert f["right"]>=.20*f["width"], (view,i,"pillow centered under chin",f)
            assert min(f["inset"])>=.18, (view,i,"wrists still on the side seams",f)
            assert min(f["handCoverage"])>=.6, (view,i,"palms do not lie on the fabric",f)
            assert all(f["connected"]), (view,i,"detached resting hand",f)
            assert f["span"]>=3 and f["penetration"]<=2, (view,i,"missing or excessive cheek contact",f)
            assert f["eyelids"] and not f["hiddenEye"], (view,i,"pillow covered the eyelids",f)
        print(f"{view}: {sum(map(len,takes.values()))} drawings, minimum lip clearance {lip:.1f} px.")
    page.close()


def check_file_entry(browser, view):
    # Chrome taints a composed canvas after drawing a local PNG. Exercise the
    # published double-click entry with actual inputs, without reading that canvas.
    page = browser.new_page(has_touch=view=="close", viewport={"width":960,"height":840})
    install_pet_clock(page)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto(PAGE.as_uri())
    page.wait_for_function("active==='idle' && sheet", polling=20)
    page.locator(f"[data-view={view}]").evaluate("e=>e.click()")
    page.locator("#scene-button").evaluate("e=>e.click()")
    page.evaluate("advancePet(1600)")
    points = page.evaluate("""() => {
      lcd.canvas.scrollIntoView({block:"center"});
      const b=lcd.canvas.getBoundingClientRect(), p=NapProp.position(lcd,take,frame,view);
      const at=(x,y)=>[b.x+b.width*x/184,b.y+b.height*y/184];
      const x=Math.floor(p.x+p.w*.55);
      let y=p.y;
      while(y<p.y+p.h && !NapProp.contains(p,x,y,null,true)) y++;
      const head=lcd.facePoint(view,take,frame,...take.anchors[frame].slice(0,2));
      return [at(x+.5,y+.5),at(head.sx,head.sy)];
    }""")
    tap = page.touchscreen.tap if view == "close" else page.mouse.click
    tap(*points[0])
    assert page.evaluate("active==='doze' && gestures.contact?.phase==='release'")
    tap(*points[1])
    page.evaluate("advancePet(700)")
    assert page.evaluate("active.startsWith('peek')") and not errors, (view, errors)
    page.close()


def check(browser, url, view, reduced):
    page = browser.new_page(viewport={"width":950, "height":840}, has_touch=view=="close",
                            reduced_motion="reduce" if reduced else "no-preference")
    install_pet_clock(page)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto(url)
    page.wait_for_function("active==='idle' && sheet", polling=20)
    page.locator(f"[data-view={view}]").evaluate("b=>b.click()")
    page.locator("#scene-button").evaluate("b=>b.click()")
    page.evaluate("advancePet(1600)")
    position = page.evaluate("""() => {
      const p=NapProp.position(lcd,take,frame,view,active);
      lcd.canvas.scrollIntoView({block:"center"});
      const box=lcd.canvas.getBoundingClientRect(),scale=box.width/184;
      // Press visible stuffing. The two resting hands now occupy the middle
      // of the pillow, so a hard-coded rectangle percentage can hit a sleeve.
      let sx,sy,best=Infinity;
      for(let y=p.y+2;y<p.y+p.h-2;y++) for(let x=p.x+2;x<p.x+p.w-2;x++){
        if(!NapProp.contains(p,x,y,null,true) || !NapProp.contains(p,x,y+1,null,true)) continue;
        const score=Math.hypot(x-(p.x+.52*p.w),y-(p.y+.68*p.h));
        if(score<best){best=score;sx=x+.5;sy=y+.5;}
      }
      window.sampleCushion=()=>Array.from(lcd.paint.getImageData(p.x,p.y,p.w,p.h).data);
      const client={clientX:box.left+scale*sx,clientY:box.top+scale*sy};
      const head=lcd.facePoint(view,take,frame,...take.anchors[frame].slice(0,2));
      return [client.clientX,client.clientY,hitAt(client).part,
        box.left+scale*head.sx,box.top+scale*head.sy];
    }""")
    x, y, part, head_x, head_y = position
    assert part == "pillow"
    before = page.evaluate("sampleCushion()")
    if view == "close":
        cdp = page.context.new_cdp_session(page)
        def press(): cdp.send("Input.dispatchTouchEvent", {"type":"touchStart","touchPoints":[{"x":x,"y":y}]})
        def release(): cdp.send("Input.dispatchTouchEvent", {"type":"touchEnd","touchPoints":[]})
    else:
        def press():
            page.mouse.move(x,y)
            page.mouse.down()
        def release(): page.mouse.up()
    press()
    assert page.evaluate("gestures.pointer.part==='pillow' && gestures.contact.phase==='down'")
    after = page.evaluate("sampleCushion()")
    changed = sum(before[i:i+3] != after[i:i+3] for i in range(0,len(before),4))
    assert changed >= 8, (view,reduced,changed)
    release()
    page.evaluate("advancePet(300)")
    assert page.evaluate("active==='doze' && !gestures.sleepJoin && !gestures.contact")
    # A press dragged from the fabric onto the face must neither pat nor wake.
    page.mouse.move(x,y)
    page.mouse.down()
    page.mouse.move(head_x,head_y)
    page.mouse.up()
    page.evaluate("advancePet(200)")
    assert page.evaluate("active==='doze' && !gestures.sleepJoin")
    # Count the actual illuminated strokes, both on dark sky. Reduced motion
    # keeps these pixels constant even while the viewport's clock advances.
    def glyphs():
        return page.evaluate("""() => {
          const rgb=lcd.paint.getImageData(view==='close'?23:34,38,39,44).data;
          let amber=0,mint=0;
          for(let i=0;i<rgb.length;i+=4) {
            if (rgb[i]===255 && rgb[i+1]===240 && rgb[i+2]===193) amber++;
            if (rgb[i]===201 && rgb[i+1]===230 && rgb[i+2]===223) mint++;
          }
          return [amber,mint];
        }""")
    marks = glyphs()
    assert min(marks) >= 12, marks
    page.locator("#badge").focus()
    page.keyboard.down("p")
    assert page.evaluate("gestures.contact.keyboard && gestures.contact.phase==='down'")
    page.keyboard.up("p")
    page.evaluate("advancePet(400)")
    assert page.evaluate("active==='doze'")
    page.mouse.click(head_x,head_y)
    page.evaluate("advancePet(650)")
    assert page.evaluate("active.startsWith('peek') && frame>=4")
    assert glyphs() == [0,0], (view,"sleep symbols linger during peek")
    page.locator("#scene-button").evaluate("b=>b.click()")
    wake_ms = page.evaluate("Math.ceil(1000*PIXEL_CLIPS.wakeLeft.frames/PIXEL_CLIPS.wakeLeft.fps)+160")
    page.evaluate("ms=>advancePet(ms)", wake_ms)
    assert page.evaluate("active==='idle'") and not errors, errors
    if not reduced:
        page.locator("#scene-button").evaluate("b=>b.click()")
        page.evaluate("advancePet(450)")
        assert page.evaluate("active==='doze' && frame<=6")
        page.locator("#scene-button").evaluate("b=>b.click()")
        page.evaluate("advancePet(800)")
        assert page.evaluate("active==='idle' && !NapProp.position(lcd,take,frame,view,active)")
    print(f"{view}, reduced={reduced}: {changed} cushion pixels on press, Z pixels {marks}.")
    page.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    args = parser.parse_args()
    with ThreadingHTTPServer(("127.0.0.1",0),functools.partial(QuietHandler,directory=PAGE.parent)) as server:
        threading.Thread(target=server.serve_forever,daemon=True).start()
        with sync_playwright() as pw:
            browser = pw.chromium.launch(executable_path=args.chrome)
            audit_drawings(browser, f"http://127.0.0.1:{server.server_port}/index.html")
            for crop in ("full","close"):
                check_file_entry(browser, crop)
                for reduced in (False,True):
                    check(browser,f"http://127.0.0.1:{server.server_port}/index.html",crop,reduced)
            browser.close()
        server.shutdown()
