"""Check native palm pixels around material transitions with a stationary wrist."""
import argparse
import base64
import io

import numpy as np
from PIL import Image

from bake import DESIGN, HERE, TAKES, sample_pixels, sync_playwright


def check(chrome):
    lookup = np.full(1 << 24, -1, dtype=np.int16)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=chrome)
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame=()=>0")
        page.goto((DESIGN / "badge-prototype.html").as_uri())
        page.wait_for_function("document.querySelector('#gpt-canvas').dataset.ready==='true'", polling=25)
        page.add_script_tag(content=(HERE / "poses.js").read_text(encoding="utf-8"))
        # Reuse the actual arm rig, including the sleeve, atlas and pass order.
        page.evaluate("""() => {
          const paint = DragonArms.prototype.draw;
          DragonArms.prototype.draw = function(ctx, pose, pass) {
            if (pass === "body") { window.testArms = this; window.testPose = pose; }
            return paint.call(this,ctx,pose,pass);
          };
        }""")
        for take, shape in (("idleGroomLeft", "brush"), ("idleYawn", "cup"),
                            ("idleStretch", "spread"), ("idleYawn", "cover")):
            fps, count = TAKES[take]
            page.evaluate("args=>startBadgeTake(args)", {"name":take, "scene":"idle"})
            page.evaluate("args=>sampleBadgeTake(args)", {"i":4, "n":count, "fps":fps})
            pngs = page.evaluate("""shape => {
              const arms = window.testArms;
              const canvas = document.createElement("canvas");
              canvas.width=612; canvas.height=540;
              const ctx=canvas.getContext("2d");
              ctx.scale(.75,.75);
              return [0, .079, .081, .379, .381, 1].map(amount => {
                ctx.clearRect(0,0,816,720);
                arms.draw(ctx,{stage:{desk:0,workMix:0},actionTime:0,
                  badgePalm:{left:{shape,amount}}},"body");
                return canvas.toDataURL().split(",")[1];
              });
            }""", shape)
            frames = [np.array(Image.open(io.BytesIO(base64.b64decode(p))).convert("RGBA")) for p in pngs]
            for crop, ox, oy, step in (("full",28,12,4), ("close",113,34.5,2.75)):
                tiles = [sample_pixels(frame,ox,oy,step,lookup) for frame in frames]
                for i in (1,3):
                    changed = np.count_nonzero(tiles[i] != tiles[i+1])
                    assert changed <= 8, (take,shape,crop,"palm material changed abruptly",changed)
                assert np.count_nonzero(tiles[0] != tiles[-1]) > 20, (take,shape,crop,"palm never opened")
        # At the middle of withdrawing the photographed high-five the hand used
        # to be two transparent cutouts. Isolate its actual ink from the sleeve.
        page.evaluate("args=>startBadgeTake(args)", {"name":"stow-waiting","scene":"waiting"})
        solid = page.evaluate("""() => [2,3,4].map(i => {
          sampleBadgeTake({i,n:8,fps:24});
          const canvas=document.createElement("canvas");
          canvas.width=612; canvas.height=540;
          const ctx=canvas.getContext("2d"), ink=canvas.cloneNode().getContext("2d");
          const draw=ctx.drawImage;
          ctx.drawImage=function(image,...args) {
            const photo=args.length===8 && (args[0]===0 || args[0]===55) && args[1]===103;
            const transition=args.length===2 && args[0]===-32 && args[1]===-40;
            const matrix=this.getTransform();
            if (matrix.a>0 && (photo || transition)) {
              ink.setTransform(matrix); ink.globalAlpha=this.globalAlpha;
              ink.drawImage(image,...args);
            }
            draw.call(this,image,...args);
          };
          ctx.scale(.75,.75);
          window.testArms.draw(ctx,window.testPose,"body");
          const pixels=ink.getImageData(0,0,612,540).data;
          let count=0;
          for(let a=3;a<pixels.length;a+=4) if(pixels[a]>=215) count++;
          return count;
        })""")
        assert min(solid[0], solid[2])>0 and solid[1]>=.75*min(solid[0],solid[2]), ("offer ink",solid)
        browser.close()
        print("Four stationary-wrist gestures passed at both material boundaries in both native crops. High-five withdrawal retained its opaque palm.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    check(parser.parse_args().chrome)
