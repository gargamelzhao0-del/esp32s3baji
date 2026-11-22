"""Bake the startled reaction without the sugar shell. The player bursts it at the touched position."""
import argparse
import base64
import io
import json
import math

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

from bake import BADGE, DESIGN, sample_pixels, save_sheet


def bake_burst(chrome):
    source = (BADGE / "clips.js").read_text(encoding="utf-8")
    clip = json.loads(source[source.index("{"):source.rindex("}")+1])["bubblePop"]
    rows = math.ceil(clip["frames"] / 8)
    pixels = np.zeros((2 * rows * 132, 8 * 138), dtype=np.uint8)
    lookup = np.full(1 << 24, -1, dtype=np.int16)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=chrome)
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame=()=>0")
        page.goto((DESIGN / "badge-prototype.html").as_uri())
        page.wait_for_function("document.querySelector('#gpt-canvas').dataset.ready==='true'", polling=25)
        page.evaluate("""async () => {
            const canvas = document.createElement("canvas");
            canvas.width = 612;
            canvas.height = 540;
            const ctx = canvas.getContext("2d",{willReadFrequently:true});
            const fill = ctx.fillRect;
            ctx.fillRect = function(x,y,w,h) {
                if (this.globalCompositeOperation === "destination-over" && x===-1 && y===-1 && w===2 && h===2) return;
                return fill.call(this,x,y,w,h);
            };
            const props = window.drawDragonProps;
            window.drawDragonProps = (ctx,pose,...rest) =>
                props(ctx,pose.action==="bubblePop" ? {...pose,action:""} : pose,...rest);
            const pet = new GptCompanion(canvas,{manual:true,seed:10773});
            await pet.ready;
            pet.setScene({state:"idle",scope:null,active:true,reduced:false,offline:false});
            if (pet.stage.office) pet.stage.finishSetup();
            pet.care("feed",1);
            for (let i=0;i<81;i++) pet.advance(1/24);
            pet.popBubble(null);
            pet.advance(0);
            window.sampleBurst = dt => {
                if (dt) { pet.advance(dt/2); pet.advance(dt/2); }
                const nose=pet.rig.anchors.nose;
                return [canvas.toDataURL().split(",")[1],Math.round(nose.x*.75),Math.round(nose.y*.75)];
            };
        }""")
        for i, pan in enumerate(clip["closePan"]):
            data, x, y = page.evaluate("dt=>sampleBurst(dt)", 1 / clip["fps"] if i else 0)
            if [x, y] != clip["anchors"][i][2:4]:
                raise ValueError(f"The reaction no longer matches its landmarks at frame {i}")
            rgba = np.asarray(Image.open(io.BytesIO(base64.b64decode(data))).convert("RGBA"))
            for view, (ox, oy, step) in enumerate(((28, 12, 4.), (113 + pan*2.75, 34.5, 2.75))):
                top, left = (i//8 + view*rows)*132, i%8*138
                pixels[top:top+132, left:left+138] = sample_pixels(rgba, ox, oy, step, lookup)
        browser.close()
    save_sheet("bubblePop", pixels)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome", help="Path to a local Chromium or Chrome executable")
    bake_burst(parser.parse_args().chrome)
