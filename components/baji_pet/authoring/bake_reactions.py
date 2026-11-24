"""Rebuild the read-only touch clips from the tracked, approved gesture atlases."""
import functools
import http.server
import json
import os
import shutil
import struct
import threading
import zlib
from pathlib import Path

import numpy as np
from playwright.sync_api import sync_playwright

from bake import COMPONENT, pixels
from panel_source import panel_source
from bake_motion import Dictionary, write_atlases, physical_index
from motion_pack import split_plan

REACTIONS = ("cheekLeft", "cheekRight", "tickle")


def bake(web):
    clips = write_atlases(web, [(name,3) for name in REACTIONS])
    landmarks, crcs = [], []
    quads = Dictionary()
    plans, deltas, keys, rectangles = [], [], [], []
    change_count = 0
    axis = physical_index()
    sample_axis = np.searchsorted(axis,np.arange(368))
    previous = None
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    with http.server.ThreadingHTTPServer(("127.0.0.1",0),functools.partial(Quiet,directory=str(web))) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            with sync_playwright() as pw:
                browser = pw.chromium.launch(executable_path=shutil.which("chromium") or shutil.which("google-chrome") or os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe"))
                page = browser.new_page(device_scale_factor=1)
                origin = f"http://127.0.0.1:{server.server_port}"
                page.route(origin+"/capture", lambda r:r.fulfill(content_type="text/html",body="<meta charset=utf-8>"))
                page.goto(origin+"/capture")
                for script in ["clips.js","gesture-clips.js","nap-prop.js","face-fx.js","display.js"]:
                    page.add_script_tag(url=origin+"/"+script)
                page.add_script_tag(path=Path(__file__).with_name("capture.js"))
                page.evaluate("setupPetCapture(true,"+json.dumps(REACTIONS)+")")
                for name in ("idle","pat",*REACTIONS):
                    steps = 4 if name == "idle" else 3
                    for k in range(clips[name]["frames"]*steps):
                        args = [name,k//steps,k%steps,steps]
                        if name not in REACTIONS:
                            landmarks.append(page.evaluate("capturePose",args+["landmarks"]))
                            continue
                        shot = page.evaluate("capturePose",args+[True])
                        landmarks.append(shot["landmarks"])
                        crcs.append(shot["crc"])
                        wire = pixels(shot["image"])
                        native = wire[sample_axis[:,None],sample_axis]
                        assert np.array_equal(native[axis[:,None],axis],wire)
                        cells = native.reshape(184,2,184,2).transpose(0,2,1,3).reshape(184,184,4)
                        frame = quads.encode(cells).reshape(184,184)
                        if k%12 == 0:
                            keys.append(frame)
                        if k == 0:
                            ids = np.empty(0,dtype="<u2")
                            regions = []
                        else:
                            ids = np.flatnonzero(frame.ravel() != previous.ravel())
                            diff = wire != last_wire
                            regions = split_plan(diff)
                        deltas.append(np.column_stack((ids,frame.ravel()[ids])).astype("<u2").tobytes())
                        plans.append(struct.pack("<IHHI",change_count,len(ids),len(regions),len(rectangles)))
                        change_count += len(ids)
                        rectangles.extend((x,y,r-x,b-y) for x,y,r,b in regions)
                        previous = frame
                        last_wire = wire
                    print(name,len(quads.index),change_count,"changed cells",flush=True)
                browser.close()
        finally:
            server.shutdown()
    assert len(quads.index) <= 65536
    parts = [struct.pack("<4sIII",b"BPR2",len(quads.index),change_count,len(rectangles)),
             np.array(landmarks,dtype="<i2").tobytes(),b"".join(plans),b"".join(quads.index),
             np.array(keys,dtype="<u2").tobytes(),b"".join(deltas),
             np.asarray(rectangles,dtype="<u2").tobytes()]
    data = b"".join(parts)
    (COMPONENT/"assets/reactions.bpr").write_bytes(data+struct.pack("<I",zlib.crc32(data)))
    (COMPONENT/"tests/reactions.crc").write_bytes(np.asarray(crcs,dtype="<u4").tobytes())
    print("Flash bytes", len(data)+4,flush=True)


if __name__ == "__main__":
    with panel_source() as web:
        bake(web)
