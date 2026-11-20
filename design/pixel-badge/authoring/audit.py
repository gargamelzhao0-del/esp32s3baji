"""Inspect every stored pixel in both crops. Run from any directory."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
W, H = 138, 132


def manifest(filename):
    text = (ROOT / filename).read_text(encoding="utf-8")
    return json.loads(text[text.index("{"):text.rindex("}") + 1])


def inspect(csv_path, contact_path):
    clips = {**manifest("clips.js"), **manifest("gesture-clips.js")}
    board = Image.new("RGB", (8 * W, ((len(clips) + 7) // 8) * (H + 18)), "#242737")
    draw = ImageDraw.Draw(board)
    rows = []
    gear_tiles = 0
    for k, (name, base) in enumerate(clips.items()):
        if "gearAtlas" in base:
            with Image.open(ROOT / "assets" / (base["gearAtlas"]+".png")) as layer, Image.open(ROOT / "assets" / (name+".png")) as source:
                layer, source = np.asarray(layer), np.asarray(source)
                assert layer.shape == source.shape
                for view in range(2):
                    for i in range(base["frames"]):
                        x = i%8*W
                        y = (i//8 + view*((base["frames"]+7)//8))*H
                        cut = layer[y:y+H,x:x+W]
                        original = source[y:y+H,x:x+W]
                        assert cut.any() and np.all((cut == 0) | (cut == original)), (name,view,i)
                        gear_tiles += 1
        for scene, clip in [("base", base), *base.get("scenes", {}).items()]:
            with Image.open(ROOT / "assets" / (clip.get("atlas", name) + ".png")) as sheet:
                assert sheet.mode == "P" and sheet.info["transparency"] == 0
                indices = np.asarray(sheet)
                n = clip["frames"]
                assert len(clip["anchors"]) == n
                for view in ["full", "close"]:
                    previous = first = repeat_from = None
                    repeat_index = clip.get("holdFrom", 0)
                    for i in range(n):
                        t = i + clip.get("offset", 0)
                        x = (t % 8 + (8 if clip.get("paired") and view == "close" else 0)) * W
                        y = (t // 8 + ((n + 7) // 8 if view == "close" and not clip.get("paired") else 0)) * H
                        tile = indices[y:y + H, x:x + W]
                        assert tile.shape == (H, W) and tile.any(), (name, scene, view, i)
                        if first is None:
                            first = tile
                        if i == repeat_index:
                            repeat_from = tile
                        delta = np.count_nonzero(tile != previous) if previous is not None else 0
                        ys, xs = np.nonzero(tile)
                        def visible(point):
                            if view == "full":
                                ox, oy, step, pan = 28, 12, 4, 0
                            else:
                                ox, oy = clip.get("closeOrigin", [113, 34.5])
                                step = 2.75
                                pan = clip["closePan"][i] if "closePan" in clip else 0
                            sx = 23 + (point[0]-ox)/step-pan
                            sy = (18 if view == "full" else 22) + (point[1]-oy)/step
                            return 23 <= sy < 149 and 23 <= sx < 161 and (sx-92)**2 + (sy-92)**2 < 82**2
                        a = clip["anchors"][i]
                        target = clip.get("target") or clip.get("book") or clip.get("bubbles")
                        can_touch = visible(target[i]) if target and target[i] else ""
                        nose, head = visible(a[2:4]), visible(a[:2])
                        assert nose and head and can_touch is not False, (name, scene, view, i, "hit outside the screen")
                        cycle = scene == "base" and (name in {"working","running","reading","readingMoon","searching","waiting"} or "holdFrom" in clip)
                        seam = np.count_nonzero(tile != repeat_from) if cycle and i == n-1 else ""
                        rows.append((name, scene, view, i, len(xs), xs.min(), ys.min(), xs.max(), ys.max(),
                                     delta, np.count_nonzero(tile != first) if i == n - 1 else "",
                                     seam, nose, head, can_touch))
                        previous = tile
                        if scene == "base" and view == "full" and i == n // 3:
                            sprite = sheet.crop((x, y, x + W, y + H)).convert("RGBA")
                            ox, oy = k % 8 * W, k // 8 * (H + 18)
                            board.paste(sprite, (ox, oy), sprite)
                            draw.text((ox + 6, oy + H), name, fill="white")
    if csv_path:
        with open(csv_path, "w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow("clip scene view frame opaque x_min y_min x_max y_max changed first_to_last repeat_seam nose_visible forehead_visible target_visible".split())
            writer.writerows(rows)
    if contact_path:
        board.save(contact_path)
    print(f"{len(clips)} clips, {len(set(r[:2] for r in rows))} takes, {len(rows)} tiles, {len(rows)*W*H:,} indexed pixels")
    if gear_tiles:
        print(f"{gear_tiles} additional paper/hand tiles checked against their exact source pixels.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv")
    parser.add_argument("--sheet")
    args = parser.parse_args()
    inspect(args.csv, args.sheet)
