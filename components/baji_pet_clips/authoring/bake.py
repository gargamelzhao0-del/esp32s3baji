"""Bake all default-character web clips at their authored frame rate into BPC1."""
import argparse
import base64
import functools
import hashlib
import http.server
import os
from pathlib import Path
import shutil
import struct
import sys
import threading
import zlib

import numpy as np
from playwright.sync_api import sync_playwright

COMPONENT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(COMPONENT.parent/"baji_pet/authoring"))
from panel_source import SCRIPTS, panel_source
HEADER = struct.Struct("<4s7I")
CLIP = struct.Struct("<24s16sIHBB")
FRAME = struct.Struct("<IIII4B10h4h4h4BBBH")
GRID = 184
FULL_BODY_PANEL = 1


def add_clip(rec, action, scene, body, clips, descriptors):
    frames = np.frombuffer(zlib.decompress(base64.b64decode(rec["data"])), dtype="<u2").reshape(-1, GRID, GRID)
    start = len(descriptors) // FRAME.size
    clips.extend(CLIP.pack(action.encode(),scene.encode(),start,len(frames),rec["fps"],0))
    zero = np.zeros_like(frames[0])
    for i, (pixels, meta) in enumerate(zip(frames,rec["metadata"])):
        key = i % 12 == 0
        prior = zero if key else frames[i-1]
        rows, cols = np.nonzero(pixels != prior)
        rect = [0,0,0,0]
        encoded = b""
        if len(rows):
            x, y, end, bottom = int(cols.min()), int(rows.min()), int(cols.max())+1, int(rows.max())+1
            rect = [x,y,end-x,bottom-y]
            a, b = pixels[y:bottom,x:end], prior[y:bottom,x:end]
            xor = np.bitwise_xor(a,b)
            # Sparse changes preserve static cloth and environment. Zeroes in
            # either byte-plane form good DEFLATE runs even for wide updates.
            encoded = zlib.compress(xor.view("u1").reshape(-1,2).T.tobytes(), 7)
        data_off = len(body)
        body.extend(encoded)
        mask_off = len(body)
        cushion = meta["cushion"]
        if cushion:
            body.extend(base64.b64decode(cushion[4]))
        flags = int(key) | (2 if meta["palms"] else 0)
        descriptors.extend(FRAME.pack(*map(int,(data_off,len(encoded),mask_off,zlib.crc32(pixels),
            *rect,*meta["axes"],*(meta["palms"] or [0]*4),*(meta["prop"] or [0]*4),
            *(cushion[:4] if cushion else [0]*4),meta["kind"],flags,round(256*meta["scale"])))))


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web",type=Path,help="Override the pinned full-body source")
    parser.add_argument("--output",type=Path,default=COMPONENT/"assets/clips.bpc")
    parser.add_argument("--golden",type=Path,default=COMPONENT/"tests/chrome.crc")
    parser.add_argument("--sources",type=Path,default=COMPONENT/"authoring/web-inputs.sha256")
    args = parser.parse_args()
    if args.web:
        bake(args, args.web.resolve())
    else:
        with panel_source() as web:
            bake(args, web)


def bake(args, web):
    body, clips, frames, golden = bytearray(), bytearray(), bytearray(), bytearray()
    with http.server.ThreadingHTTPServer(("127.0.0.1",0),functools.partial(Quiet,directory=web)) as server:
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            with sync_playwright() as pw:
                chrome = shutil.which("chromium") or shutil.which("google-chrome") or os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe")
                browser = pw.chromium.launch(executable_path=chrome)
                p = browser.new_page(device_scale_factor=1)
                origin = f"http://127.0.0.1:{server.server_port}"
                p.route(origin+"/capture",lambda r:r.fulfill(content_type="text/html",body="<meta charset=utf-8>"))
                p.goto(origin+"/capture")
                scripts=SCRIPTS
                for script in scripts:
                    p.add_script_tag(url=origin+"/"+script)
                p.add_script_tag(path=Path(__file__).with_name("capture.js"))
                entries = p.evaluate("prepare()")
                for name, scene in entries:
                    rec = p.evaluate("p => captureClip(p)",[name,scene])
                    add_clip(rec,name,scene,body,clips,frames)
                    for meta in rec["metadata"]:
                        golden.extend(struct.pack("<I",meta["webCRC"]))
                    print(name,scene,len(body),flush=True)
                palette = b"".join(struct.pack("<HH",int(key)//65536,int(key)%65536) for key in p.evaluate("exportPalette()"))
                sources=sorted([*scripts,*p.evaluate("exportImages()")])
                browser.close()
        finally:
            server.shutdown()
    data = clips + frames + palette + body
    header = HEADER.pack(b"BPC1",HEADER.size+len(data),zlib.crc32(data),len(clips)//CLIP.size,
                         len(frames)//FRAME.size,len(palette)//4,HEADER.size+len(clips)+len(frames)+len(palette),FULL_BODY_PANEL)
    args.output.parent.mkdir(exist_ok=True, parents=True)
    encoded=header+data
    if not args.output.exists() or args.output.read_bytes()!=encoded:
        temp = args.output.with_suffix(".tmp")
        temp.write_bytes(encoded)
        temp.replace(args.output)
    args.golden.parent.mkdir(exist_ok=True,parents=True)
    args.golden.write_bytes(golden)
    args.sources.write_text("".join(f"{hashlib.sha256((web/name).read_bytes()).hexdigest()}  {name}\n"
                                    for name in sources),encoding="ascii",newline="\n")
    print(f"{len(entries)} clips, {len(frames)//FRAME.size} frames, {len(data)+HEADER.size} flash bytes")


if __name__=="__main__":
    main()
