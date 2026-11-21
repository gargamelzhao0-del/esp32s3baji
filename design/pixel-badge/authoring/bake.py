"""Bake the interactive poses from the normal rig into the two approved pixel crops."""
import argparse
import base64
import io
import json
import math
from pathlib import Path

import numpy as np
from numba import njit
from PIL import Image
from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve().parent
BADGE = HERE.parent
DESIGN = BADGE.parent
COLORS = (
    "423a49 594b60 756980 8a7e99 a095ac b6a8c3 cdc0d5 dfd4e7 eee7f3 f9f5fa ffffff "
    "595470 7a739b 9284b3 ae9aca cbb8e0 e1d1f2 fff0e6 eeded3 dec3c1 b89096 e9cfd6 "
    "352f41 514256 78586b a57579 c5958c e4b9a4 f3d6b9 f0c573 ffe9a6 ed9b97 d8788c "
    "41536b 587b90 77a5ab 9bcbbd c5e7d4"
).split()
PALETTE = np.array([[int(color[i:i+2], 16) for i in (0,2,4)] for color in COLORS], dtype=np.float64)
TAKES = {
    "cheekLeft": (16,22), "cheekRight": (16,22), "tickle": (16,30), "brush": (12,45),
    "doze": (10,35), "peekLeft": (12,9), "peekRight": (12,9),
    "wakeLeft": (12,22), "wakeRight": (12,22), "reading": (10,33),
    "readingMoon": (10,33), "turnStar": (16,25), "turnMoon": (16,25),
    "glanceStar": (12,12), "glanceMoon": (12,12),
}
REACTIONS = ("cheekLeft","cheekRight","tickle","brush")
WORK_SCENES = ("reading","readingMoon","working","running","writing","searching","waiting","compact","error")
JOBS = {
    "working": (12,40), "running": (14,32), "writing": (12,63), "searching": (12,40),
    "waiting": (12,42), "compact": (12,33), "error": (12,32), "limited": (10,24),
    "succeeded": (16,36), "ended": (12,29), "cancelled": (12,27),
}
TOUCH = {"wingLeft": (16,25),"wing": (16,25),"tail": (16,29),"nose": (16,22),
         "sneeze": (18,32),"patLeft": (16,30),"pat": (16,30)}
TAKES.update(JOBS, **TOUCH)
TOOL_ACTIONS = {"keyChime":"running","lensFind":"searching","palmTap":"waiting"}
TAKES.update({name:(16,27) for name in TOOL_ACTIONS})
TAKES.update({"heartHug":(16,39),"starWatch":(12,27),"starCatchLeft":(16,32),"starCatchRight":(16,32)})
TAKES.update({"browInviteLeft": (16,89), "browInviteRight": (16,89)})
SETTLE = {name:TAKES[name][1]-1 for name in ("writing","compact","succeeded","ended","error","limited","cancelled")}
for name in SETTLE:
    fps, length = TAKES[name]
    TAKES[name] = (fps, length + 3*fps)

@njit
def nearest(r,g,b):
    count = len(PALETTE) if max(r,g,b)-min(r,g,b)>35 and (g>r or r-b>26) else 22
    best, distance = 0, 1e12
    for k in range(count):
        x,y,z = PALETTE[k]
        error = .30*(r-x)**2 + .52*(g-y)**2 + .18*(b-z)**2
        if error < distance:
            best, distance = k,error
    return best

@njit
def sample_pixels(image, ox, oy, step, lookup):
    """Vote by covered area and retain thin lines using the darkest 22% of the cell."""
    out = np.zeros((132,138), dtype=np.uint8)
    tones = np.empty(36)
    samples = np.empty((36,3))
    weights = np.empty(36)
    votes = np.empty(len(PALETTE))
    for row in range(132):
        top = oy+row*step
        for col in range(138):
            left = ox+col*step
            votes[:] = 0
            total, size = 0.,0
            for y in range(int(top), math.ceil(top+step)):
                for x in range(int(left), math.ceil(left+step)):
                    r,g,b,alpha = image[y,x].astype(np.int32)
                    if not alpha:
                        continue
                    area = (min(y+1.,top+step)-max(y,top))*(min(x+1.,left+step)-max(x,left))
                    weight = area*alpha/255.
                    key = (r<<16)|(g<<8)|b
                    ink = lookup[key]
                    if ink<0:
                        ink = nearest(r,g,b)
                        lookup[key] = ink
                    votes[ink] += weight
                    total += weight
                    tones[size] = .30*r+.52*g+.18*b
                    samples[size,0],samples[size,1],samples[size,2] = r,g,b
                    weights[size] = weight
                    size += 1
            if total < step*step*.5:
                continue
            ink = np.argmax(votes)
            r,g,b = PALETTE[ink]
            limit = total*.22
            remaining = limit
            dark = np.zeros(3)
            for j in np.argsort(tones[:size]):
                take = min(remaining, weights[j])
                dark += take*samples[j]
                remaining -= take
                if remaining < 1e-9:
                    break
            dr,dg,db = dark/limit
            luma = .30*dr+.52*dg+.18*db
            contrast = .30*r+.52*g+.18*b-luma
            if total<step*step*.92 and luma<150:
                ink = nearest(dr,dg,db)
            elif contrast>35:
                k = .8 if contrast>80 else .64
                ink = nearest(r*(1-k)+dr*k,g*(1-k)+dg*k,b*(1-k)+db*k)
            out[row,col] = ink+1
    return out

def book_art():
    # Use the original renderer's precise prop transform, paper geometry and occlusion.
    art = (DESIGN/"gpt-pet/props.js").read_text(encoding="utf-8")
    # The badge draws native contact cues at the actual instrument, after cropping.
    art = art.replace('if(!s.kind)window.DragonArt.work(ctx,p,a);','')
    art = art.replace('a.right.x-1,a.right.y-6,magnifier','a.right.x+9,a.right.y-19,magnifier')
    art = art.replace('ctx.rotate(0.48);','ctx.rotate(0.48); ctx.scale(1.15,1.15);')
    art = art.replace("prop(ctx,p.book*gear,cx,cy+3,book,pageTurn);",
                      'if (p.badgeJob === "reading") prop(ctx,p.book*gear,cx,cy+3,book,pageTurn);\n        window.drawBadgeInstrument(ctx,p,a);')
    start = art.index("        // A gilded star chart")
    stop = art.index("        // A page curls",start)
    art = art[:start] + "        window.drawBadgeChart(ctx, turn > 0 ? !window.moonChart : window.moonChart);\n" + art[stop:]
    # Paint the old chart on the leaf until the viewer sees its reverse.
    insertion = art.index("        const swing=", start)
    return art[:insertion] + """
        if (turn > 0 && turn < .49) {
            ctx.save();
            ctx.transform(Math.cos(Math.PI*turn), 0, 0, 1, 0, -34*Math.sin(Math.PI*turn));
            window.drawBadgeChart(ctx, window.moonChart);
            ctx.restore();
        }
""" + art[insertion:]

def bake(page,name,fps,count,lookup,scene="idle"):
    page.evaluate("args => startBadgeTake(args)", dict(name=name,scene=scene))
    rows = math.ceil(count/8)
    sheet = np.zeros((rows*2*132,8*138),dtype=np.uint8)
    gear_sheet = np.zeros_like(sheet) if name in ("writing","compact") else None
    anchors,book,targets,palms = [],[],[],[]
    close_origin = (113,34.5)
    for i in range(count):
        data = page.evaluate("args => sampleBadgeTake(args)",dict(i=i,n=count,fps=fps,settle=SETTLE.get(name)))
        image = np.array(Image.open(io.BytesIO(base64.b64decode(data["png"]))).convert("RGBA"))
        if gear_sheet is not None:
            gear = np.array(Image.open(io.BytesIO(base64.b64decode(data["gear"]))).convert("RGBA"))
        anchors.append(data["coords"])
        if data["book"]:
            book.append(data["book"])
        if data["target"]:
            targets.append(data["target"])
        if data["palms"]:
            palms.append(data["palms"])
        for view,(ox,oy,step) in enumerate([(28,12,4.),(*close_origin,2.75)]):
            tile = sample_pixels(image,ox,oy,step,lookup)
            x,y = i%8*138,(i//8+view*rows)*132
            sheet[y:y+132,x:x+138] = tile
            if gear_sheet is not None:
                mask = sample_pixels(gear,ox,oy,step,lookup) != 0
                gear_sheet[y:y+132,x:x+138] = np.where(mask,tile,0)
    result = dict(fps=fps,frames=count,anchors=anchors)
    if name in SETTLE:
        result["holdFrom"] = SETTLE[name]
    if book:
        result["book"] = book
    if targets:
        result["target"] = targets
    if palms:
        result["palms"] = palms
    if gear_sheet is not None:
        result["gearAtlas"] = "hands-"+name
        save_sheet(result["gearAtlas"],gear_sheet)
    return result,sheet

def save_sheet(name,pixels):
    indexed = Image.frombytes("P",(pixels.shape[1],pixels.shape[0]),pixels.tobytes())
    indexed.putpalette([0,0,0]+list(PALETTE.astype(int).ravel()))
    indexed.save(BADGE/"assets"/(name+".png"),transparency=0,optimize=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome",help="Path to a local Chromium or Chrome executable")
    args = parser.parse_args()
    lookup = np.full(1<<24,-1,dtype=np.int16)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=args.chrome)
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame=()=>0")
        page.goto((DESIGN/"badge-prototype.html").as_uri())
        page.wait_for_function("document.querySelector('#gpt-canvas').dataset.ready==='true'")
        page.add_script_tag(content=(HERE/"poses.js").read_text(encoding="utf-8"))
        arms = (DESIGN/"gpt-pet/arms.js").read_text(encoding="utf-8")
        arms = arms.replace('const depth=s.desk', 'if(p.palmOpen)open=p.palmOpen[hand];\n                const depth=s.desk')
        page.add_script_tag(content=arms)
        page.add_script_tag(content=book_art())
        takes = {}
        for name,(fps,count) in TAKES.items():
            scene = TOOL_ACTIONS.get(name, name if name in JOBS else "idle")
            takes[name], sheet = bake(page,name,fps,count,lookup,scene)
            save_sheet(name,sheet)
            print(name,count,flush=True)
        contact_scenes = (*WORK_SCENES,"writingDone","compactDone")
        for scene in contact_scenes:
            name = "stow-"+scene
            takes[name],sheet = bake(page,name,24,8,lookup,scene)
            save_sheet(name,sheet)
        props = ("reading","readingMoon","running","searching","writing","compact")
        bundles = [(scene,"touch-",REACTIONS) for scene in WORK_SCENES]
        for scene in props:
            bundles.extend([
                (scene,"touch-face-",("pat","patLeft","nose","sneeze")),
                (scene,"touch-limb-",("wing","wingLeft","tail")),
            ])
        for scene,prefix,reactions in bundles:
            front,close = [],[]
            atlas = prefix+scene
            offset = 0
            for name in reactions:
                metadata,sheet = bake(page,name,*TAKES[name],lookup,scene=scene)
                first,second = np.split(sheet,2)
                front.append(first)
                close.append(second)
                metadata.update(atlas=atlas,offset=offset,paired=True)
                if scene in ("writing","compact"):
                    metadata["gear"] = scene
                takes[name].setdefault("scenes",{})[scene] = metadata
                offset += 8*(first.shape[0]//132)
            save_sheet(atlas,np.concatenate([np.concatenate(front),np.concatenate(close)],axis=1))
            print(atlas,flush=True)
        browser.close()
    data = json.dumps(takes,separators=(",",":"))
    (BADGE/"gesture-clips.js").write_text("Object.assign(window.PIXEL_CLIPS,"+data+");\n",encoding="utf-8")

if __name__=="__main__":
    main()
