"""Bake RGB565 output of the checked-out badge webpage into the firmware asset."""
import argparse
import base64
import functools
import http.server
import io
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import threading
import zipfile
import zlib

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

SIDE, TILE, STRIDE, MIDDLE = 466, 8, 59, 7
COMPONENT = Path(__file__).resolve().parents[1]
REFERENCE = Path(__file__).with_name("reference.zip")


def write_reference(web, archive):
    """Retain the source pixels and landmarks actually sampled by these two clips."""
    with zipfile.ZipFile(archive,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=9) as out:
        def put(path, data):
            info=zipfile.ZipInfo(path,(2026,1,1,0,0,0))
            out.writestr(info,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
        for script in ["display.js","face-fx.js"]:
            if (web/script).is_file():
                put(script,(web/script).read_text(encoding="utf8").encode("utf8"))
        for name in ["credits.html","licenses/CrossPet.md","licenses/Fusion-Pixel-OFL.txt"]:
            put(name,(web/name).read_bytes())
        # Both chosen actions bypass nap props in the web renderer.
        put("nap-prop.js","window.NapProp={position(){return null;},sleepMarks(){}};")
        metadata={}
        for clip,script in [("idle","clips.js"),("pat","gesture-clips.js")]:
            text=(web/script).read_text(encoding="utf8")
            record=json.JSONDecoder().raw_decode(text[text.index("{"):])[0][clip]
            metadata[clip]=record
            with Image.open(web/"assets"/(clip+".png")) as src:
                rgba=np.array(src.convert("RGBA"))
            offset=((record["frames"]+7)//8)*132
            rgba[:offset]=0
            # PNG source images retain the original atlas offsets. Unused full
            # views are transparent. Preserve every alpha and ink value.
            colors,indices=np.unique(rgba.reshape(-1,4),axis=0,return_inverse=True)
            if len(colors)<=256:
                img=Image.frombytes("P",(rgba.shape[1],rgba.shape[0]),indices.astype("u1").tobytes())
                img.putpalette(colors[:,:3].tobytes())
                img.info["transparency"]=colors[:,3].tobytes()
            else:
                img=Image.fromarray(rgba)
            buf=io.BytesIO()
            img.save(buf,format="PNG",optimize=True)
            put("assets/"+clip+".png",buf.getvalue())
        put("clips.js","window.PIXEL_CLIPS="+json.dumps(metadata,separators=(",",":"))+";")


def pixels(encoded):
    with Image.open(io.BytesIO(base64.b64decode(encoded))) as image:
        rgb = np.asarray(image.convert("RGB"), dtype=np.uint16)
    return ((rgb[:, :, 0] >> 3) << 11 | (rgb[:, :, 1] >> 2) << 5 |
            (rgb[:, :, 2] >> 3)).astype(">u2")


def enclosing(mask):
    y, x = np.nonzero(mask)
    return (int(x.min()) & ~1, int(y.min()) & ~1,
            (int(x.max()) + 2) & ~1, (int(y.max()) + 2) & ~1)


def union(a, b):
    return min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])


def cost(r):
    return (r[2] - r[0]) * (r[3] - r[1]) + 500


def changes(before, after):
    mask = before != after
    if not mask.any():
        return []
    def axis_plan(transpose):
        active = mask.T if transpose else mask
        bands = []
        for top in range(0, SIDE, TILE):
            band = active[top:top+TILE]
            if band.any():
                x, y, right, bottom = enclosing(band)
                bands.append((top+y,x,top+bottom,right) if transpose else (x,top+y,right,top+bottom))
        costs, cuts = [0], []
        for end, box in enumerate(bands, 1):
            best = float("inf")
            for start in range(end-1, -1, -1):
                box = union(bands[start], box)
                price = costs[start] + cost(box)
                if price < best:
                    best, cut = price, start
            costs.append(best)
            cuts.append(cut)
        boxes, n = [], len(bands)
        while n:
            start = cuts[n-1]
            boxes.append(functools.reduce(union, bands[start:n]))
            n = start
        return list(reversed(boxes))
    plan = min((axis_plan(False), axis_plan(True)), key=lambda p: sum(map(cost, p)))
    mirror = before.copy()
    for x, y, right, bottom in plan:
        mirror[y:bottom, x:right] = after[y:bottom, x:right]
    assert np.array_equal(mirror, after)
    return plan


def eye_bounds(mask):
    if not mask.any():
        return [(0, 0, 0, 0)] * 2
    xs = np.flatnonzero(mask.any(axis=0))
    split = xs[np.argmax(np.diff(xs)) + 1]
    result = []
    for lo, hi in [(0, split), (split, SIDE)]:
        x, y, right, bottom = enclosing(mask[:, lo:hi])
        result.append(((int(lo)+x)&~1, y, min(SIDE,(int(lo)+right+1)&~1), bottom))
    return result


def rect(box):
    x,y,right,bottom = box
    return struct.pack("<4H",x,y,right-x,bottom-y)


def tile_rows(frame):
    padded = np.pad(frame, ((0, STRIDE*TILE-SIDE), (0, STRIDE*TILE-SIDE)))
    # np.pad may convert non-native endian arrays on some NumPy releases.
    return padded.reshape(STRIDE,TILE,STRIDE,TILE).transpose(0,2,1,3).astype(">u2")


def encode_rows(row_ids, rows):
    """Share pixel rows across two 32K-tile banks using 16-bit local indices."""
    split = 1 << 15
    left = np.unique(row_ids[:split])
    right = np.unique(row_ids[split:])
    if max(len(left), len(right)) > 1 << 16:
        return b"BPT1", row_ids.astype("<u4").tobytes() + rows

    # Left-only, shared, right-only. Both banks now see one contiguous
    # range, and a row used in both banks still has only one pixel copy.
    left_only = np.setdiff1d(left, right, assume_unique=True)
    shared = np.intersect1d(left, right, assume_unique=True)
    right_only = np.setdiff1d(right, left, assume_unique=True)
    order = np.concatenate((left_only, shared, right_only))
    new_id = np.empty(len(order), dtype=np.uint32)
    new_id[order] = np.arange(len(order), dtype=np.uint32)
    indices = new_id[row_ids]
    indices[split:] -= len(left_only)
    pixels = np.frombuffer(rows, dtype=np.uint8).reshape(-1, 2*TILE)
    tables = struct.pack("<I", len(left_only)) + indices.astype("<u2").tobytes() + pixels[order].tobytes()
    return b"BPT2", tables


class Pack:
    def __init__(self):
        self.tiles = {}
        self.maps, self.gazes, self.patches, self.heads = [], [], [], []
        self.eyes, self.references = [], []
        self.golden = bytearray()

    def tile_id(self, tile):
        data = tile.tobytes()
        return self.tiles.setdefault(data, len(self.tiles))

    def pose(self, shot, action, index):
        variants = [pixels(png) for png in shot["variants"]]
        base = variants[MIDDLE]
        self.heads.append(shot["head"])
        self.references.append(base)
        cells = tile_rows(base)
        self.maps.append(np.array([self.tile_id(cell) for row in cells for cell in row],
                                  dtype="<u2").tobytes())
        eye_mask = np.zeros(base.shape, dtype=bool)
        for current in variants:
            diff = current != base
            eye_mask |= diff
            start = len(self.patches)
            tiles = tile_rows(current)
            dirty = np.pad(diff, ((0, STRIDE*TILE-SIDE),(0, STRIDE*TILE-SIDE)))
            ys, xs = np.nonzero(dirty.reshape(STRIDE,TILE,STRIDE,TILE).any(axis=(1,3)))
            for y,x in zip(ys, xs):
                self.patches.append(struct.pack("<HH",int(y)*STRIDE+int(x),self.tile_id(tiles[y,x])))
            self.gazes.append(struct.pack("<IHH",start,len(self.patches)-start,0))
            self.golden.extend(struct.pack("<I", zlib.crc32(current.tobytes())))
        closed=action=="pat" and 4<=index<=23
        if closed == bool(eye_mask.any()):
            raise ValueError(f"{action} frame {index} did not preserve its open/closed eye behavior")
        self.eyes.append(eye_bounds(eye_mask))

    def finish(self, config):
        if max(self.tiles.values()) >= 65536:
            raise ValueError("Tile dictionary exceeded 16-bit addressing")
        frame_info, rectangles = [], []
        n_idle,n_pat,_,_=config
        for i,image in enumerate(self.references):
            prev = n_idle-1 if i == 0 else n_idle+n_pat-1 if i == n_idle else i-1
            eyes = b"".join(rect(box) for box in self.eyes[i])
            plans = bytearray()
            for src,dst in [(self.references[prev],image),(image,self.references[0]),
                            (image,self.references[n_idle])]:
                pieces = changes(src,dst)
                plans += struct.pack("<IHH",len(rectangles),len(pieces),0)
                rectangles.extend(map(rect,pieces))
            frame_info.append(struct.pack("<4h",*self.heads[i]) + eyes + plans)
        rows = {}
        row_ids = np.empty((len(self.tiles),TILE),dtype="<u4")
        for tile,t in self.tiles.items():
            for y in range(TILE):
                key = tile[y*(TILE*2):(y+1)*(TILE*2)]
                row_ids[t,y] = rows.setdefault(key,len(rows))
        raw = bytearray(struct.pack("<IIII4H",len(self.tiles),len(rows),len(self.patches),len(rectangles),*config))
        raw += b"".join(frame_info)
        raw += b"".join(self.maps)
        raw += bytes((-len(raw))%4)
        for table in (self.gazes,self.patches,rectangles):
            raw += b"".join(table)
        magic, tables = encode_rows(row_ids, b"".join(rows))
        raw += tables
        packed = zlib.compress(raw,9)
        print(f"{len(self.tiles)} tiles, {len(raw)} decoded bytes, {len(packed)} compressed",flush=True)
        return struct.pack("<4sIII",magic,len(raw),len(packed),zlib.crc32(raw)) + packed


def bake(web, browser_path, output, golden):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1",0), functools.partial(Quiet,directory=str(web)))
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        with sync_playwright() as api:
            browser = api.chromium.launch(executable_path=browser_path)
            page = browser.new_page(device_scale_factor=1)
            page.on("pageerror",lambda exc: print("Browser:",exc,flush=True))
            origin = f"http://127.0.0.1:{server.server_port}"
            page.route(origin+"/capture",lambda route:route.fulfill(content_type="text/html; charset=utf-8",body="<meta charset=utf-8><canvas></canvas>"))
            page.goto(origin+"/capture")
            for name in ["clips.js","gesture-clips.js","nap-prop.js","face-fx.js","display.js"]:
                if (web/name).is_file():
                    page.add_script_tag(url=origin+"/"+name)
            page.add_script_tag(path=Path(__file__).with_name("capture.js"))
            config = page.evaluate("setupPetCapture()")
            if config != [43,30,12,16]:
                raise ValueError(f"Update baji_pet clip metadata for {config}")
            pack = Pack()
            for action,count in zip(["idle","pat"],config):
                for frame in range(count):
                    pack.pose(page.evaluate("capturePose",[action,frame]),action,frame)
                print(f"Captured {count} {action} poses",flush=True)
            browser.close()
            output.write_bytes(pack.finish(config))
            golden.write_bytes(pack.golden)
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web",type=Path,help="render a live prototype checkout instead of the frozen two-clip reference")
    parser.add_argument("--freeze-reference",action="store_true",help="replace the frozen authoring source with --web")
    parser.add_argument("--browser",default=shutil.which("chromium") or shutil.which("google-chrome") or
                        (os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe") if os.name=="nt" else None))
    parser.add_argument("--output",type=Path,default=COMPONENT/"assets/scene.bpz")
    parser.add_argument("--golden",type=Path,default=COMPONENT/"tests/web.crc")
    options=parser.parse_args()
    if options.freeze_reference:
        if options.web is None:
            parser.error("--freeze-reference needs --web")
        write_reference(options.web,REFERENCE)
    if options.web and not options.freeze_reference:
        bake(options.web,options.browser,options.output,options.golden)
    else:
        with tempfile.TemporaryDirectory(prefix="baji-authoring-") as directory:
            with zipfile.ZipFile(REFERENCE) as src:
                src.extractall(directory)
            bake(Path(directory),options.browser,options.output,options.golden)
