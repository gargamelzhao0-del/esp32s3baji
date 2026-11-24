"""Check the panel aperture independently of the per-frame RGB565 checksums."""
import os
from pathlib import Path
import shutil
import struct
import sys

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

COMPONENT = Path(__file__).resolve().parents[1]
PET = COMPONENT.parent/"baji_pet"
sys.path.insert(0, str(PET/"authoring"))
from panel_source import SCRIPTS, metadata, panel_source


def check_silhouettes(web):
    clips = metadata(web)
    sheets = {}
    count = 0
    axis = ((2*np.arange(466)+1)*368-1)//932//2
    edge = np.searchsorted(axis, np.arange(185))

    def check(name, tile, column, clip):
        if name not in sheets:
            with Image.open(web/"assets"/(name+".png")) as im:
                sheets[name] = np.asarray(im.convert("RGBA").getchannel("A"))
        w, h = clip.get("tileW", 138), clip.get("tileH", 132)
        row, col = tile//8*h, (tile%8+column)*w
        pixels = sheets[name][row:row+h, col:col+w]
        assert pixels.shape == (h, w), (name, tile)
        ys, xs = np.nonzero(pixels)
        if not len(xs):
            return
        assert min(xs)>0 and max(xs)<w-1 and min(ys)>0 and max(ys)<h-1, (name, tile, "ink meets tile border")
        xs = xs+clip.get("drawX", 23)
        ys = ys+clip.get("drawY", 24)
        # All four corners of each actually painted cell must clear the glass.
        dx = np.maximum(abs(2*edge[xs]+1-466), abs(2*edge[xs+1]-1-466))
        dy = np.maximum(abs(2*edge[ys]+1-466), abs(2*edge[ys+1]-1-466))
        assert np.all(dx*dx+dy*dy<=466**2), (name, tile, "glass")
        assert min(edge[ys])>=62 and max(edge[ys+1])<=392, (name, tile, "model or gauges")

    for name, base in clips.items():
        for clip in (base, *base.get("scenes", {}).values()):
            for f in range(clip["frames"]):
                source = clip.get("atlas", name)
                tile = f + clip.get("offset", 0)
                check(source, tile, 0, clip)
                if "cushion" in clip:
                    check(source, tile, 8, clip)
                count += 1
            if clip.get("gear"):
                check(clips[clip["gear"]]["gearAtlas"], 0, 0, clip)
    print(f"{count} body poses and their foreground layers clear the atlas, glass and HUD")


def check_aperture(web):
    chrome = shutil.which("chromium") or shutil.which("google-chrome") or os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe")
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=chrome)
        page = browser.new_page(device_scale_factor=1)
        for name in SCRIPTS:
            page.add_script_tag(path=web/name)
        result = page.evaluate("""() => {
          const canvas = document.body.appendChild(document.createElement("canvas"));
          const lcd = new PixelBadge(canvas, {panel:true});
          lcd.resize(400);
          const solid = document.createElement("canvas");
          solid.width=138; solid.height=132;
          const ink=solid.getContext("2d");
          ink.fillStyle="#ff00ff"; ink.fillRect(0,0,138,132);
          const flat=document.createElement("canvas");
          flat.width=flat.height=466;
          const ctx=flat.getContext("2d",{willReadFrequently:true});
          ctx.imageSmoothingEnabled=false;
          const seen=new Uint8Array(466*466);
          const frame={frame:0,view:"full",grid:false,action:"idle",agentState:"idle",telemetry:{mode:"OFF"},
                       skyMode:"dusk",tick:0,motion:false,sparks:[],useFaceFX:false};
          for (const x of [-36,82]) for (const y of [-40,80]) {
            lcd.draw({...frame,sheet:solid,clip:{...PIXEL_CLIPS.idle,drawX:x,drawY:y}});
            ctx.clearRect(0,0,466,466);ctx.drawImage(canvas,0,0,466,466);
            const p=ctx.getImageData(0,0,466,466).data;
            for(let i=0;i<seen.length;i++)
              if(p[4*i]===255 && p[4*i+1]===0 && p[4*i+2]===255) seen[i]=1;
          }
          const tag={draw(c){c.fillStyle="#ff00ff";c.fillRect(0,0,184,184);}};
          lcd.draw({...frame,clip:PIXEL_CLIPS.idle,starToy:tag});
          ctx.clearRect(0,0,466,466);ctx.drawImage(canvas,0,0,466,466);
          const fx=ctx.getImageData(0,0,466,466).data;
          let tested=0;
          for(let y=0;y<466;y++) for(let x=0;x<466;x++) {
            const px=2*x+1-466,py=2*y+1-466,i=466*y+x;
            if(px*px+py*py<=466*466) {
              if(!seen[i] || fx[4*i]!==255 || fx[4*i+2]!==255) throw Error("cut at "+x+","+y);
              tested++;
            } else if(px*px+py*py>474*474 && (seen[i] || fx[4*i+3])) throw Error("square corner");
          }
          const box=canvas.getBoundingClientRect();
          for(const [x,y,inside] of [[1,233,true],[464,233,true],[233,1,true],[233,464,true],
                                    [69,69,true],[396,69,true],[396,396,true],[69,396,true],
                                    [0,0,false],[465,465,false]]) {
            const e={clientX:box.left+(x+.5)*box.width/466,clientY:box.top+(y+.5)*box.height/466};
            if(Boolean(lcd.sourcePoint(e,"full",PIXEL_CLIPS.idle,0))!==inside) throw Error("touch at "+x+","+y);
          }
          return tested;
        }""")
        print(f"Translated character and effects reach all {result} physical disc pixels")
        browser.close()


def check_playback_paths():
    data = (COMPONENT/"assets/clips.bpc").read_bytes()
    draws = np.fromfile(COMPONENT/"tests/chrome.crc", "<u4")
    hfr = np.fromfile(PET/"tests/motion.crc", "<u4").reshape(-1, 15)[:,7]
    reactions = np.fromfile(PET/"tests/reactions.crc", "<u4")
    entries = {}
    for i in range(struct.unpack_from("<I", data, 12)[0]):
        name, scene, start, n, _, _ = struct.unpack_from("<24s16sIHBB", data, 32+48*i)
        if not scene.strip(b"\0"):
            entries[name.rstrip(b"\0").decode()] = (start, n)
    for names, rate, samples in ((("idle","pat"), (4,3), hfr), (("cheekLeft","cheekRight","tickle"), (3,3,3), reactions)):
        pos = 0
        for name, steps in zip(names, rate):
            start, n = entries[name]
            assert np.array_equal(draws[start:start+n], samples[pos:pos+n*steps:steps]), name
            pos += n*steps
        assert pos == len(samples)
    print("All 147 original poses match between high-rate and scene playback")


if __name__ == "__main__":
    with panel_source() as web:
        check_silhouettes(web)
        check_aperture(web)
    check_playback_paths()
