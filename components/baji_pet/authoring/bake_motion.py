"""Bake 48 Hz pixel-art poses from the full-body panel atlas and browser renderer."""
import argparse
import functools
import http.server
import os
import shutil
from pathlib import Path
import threading
import zlib

import cv2
import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

from bake import COMPONENT, pixels
from panel_source import metadata, panel_source


class Dictionary:
    def __init__(self):
        self.index = {}

    def encode(self, cells):
        cells = np.ascontiguousarray(cells)
        raw = cells.view(f"V{cells.dtype.itemsize * 4}").reshape(-1)
        unique, inverse = np.unique(raw, return_inverse=True)
        ids = []
        for item in unique:
            key = item.tobytes()
            ids.append(self.index.setdefault(key, len(self.index)))
        return np.asarray(ids, dtype="<u4")[inverse]


def write_atlases(web, clips_and_steps=(("idle", 4), ("pat", 3))):
    # Bidirectional flow registers the two silhouettes before reconstructing
    # contour coverage. The face, hair, hands, and horns can move separately.
    clips = metadata(web)
    x, y = np.meshgrid(np.arange(138, dtype="f"), np.arange(132, dtype="f"))
    with Image.open(web/"assets/idle.png") as idle:
        idle_first = np.array(idle.convert("RGBA").crop((0,0,138,132)))
    for name, steps in clips_and_steps:
        meta = clips[name]
        count = meta["frames"]
        path = web/"assets"/(name+".png")
        atlas = np.asarray(Image.open(path).convert("RGBA"))
        poses = [atlas[(i//8)*132:(i//8+1)*132, i%8*138:(i%8+1)*138] for i in range(count)]
        next_poses = poses[1:] + [idle_first]
        sheet = np.zeros((((count*steps+7)//8)*132, 8*138, 4), dtype=np.uint8)
        for i, (a,b) in enumerate(zip(poses,next_poses)):
            # Composite transparent cells over neutral grey when estimating
            # flow, so the silhouette participates along with the facial ink.
            def features(image):
                tone = cv2.cvtColor(image[:,:,:3],cv2.COLOR_RGB2GRAY)
                return np.where(image[:,:,3], tone, 108).astype("u1")
            ga, gb = features(a), features(b)
            ab = cv2.calcOpticalFlowFarneback(ga, gb, None, 0.5, 3, 11, 5, 5, 1.1, 0)
            ba = cv2.calcOpticalFlowFarneback(gb, ga, None, 0.5, 3, 11, 5, 5, 1.1, 0)
            for phase in range(steps):
                t = phase/steps
                img = a
                if phase:
                    left = advect(a, ab, t, x, y)
                    right = advect(b, ba, 1-t, x, y)
                    # Aligned contour coverage resolves motion smaller than a
                    # native pixel. Newly exposed edges take the nearer pose.
                    residual = np.max(np.abs(left-right),axis=2)
                    movement = np.hypot(ab[:,:,0],ab[:,:,1])
                    w = np.where((residual>80) & (movement>1.25), float(t>0.5), t)[:,:,None]
                    result = left*(1-w)+right*w
                    alpha = result[:,:,3:4]
                    result[:,:,:3] = result[:,:,:3]*255/np.maximum(alpha,1)
                    interp = np.clip(np.rint(result),0,255).astype("u1")
                    # A cross-frame invariant pixel is a locked piece of ink.
                    # This also avoids shimmering on hair texture while nearby
                    # contours travel by a small fraction of a source pixel.
                    img = np.where(np.any(a!=b,axis=2)[:,:,None],interp,a)
                k = i*steps+phase
                sheet[(k//8)*132:(k//8+1)*132,k%8*138:(k%8+1)*138] = img
        Image.fromarray(sheet).save(path)
    return clips


def advect(source, flow, fraction, x, y):
    # Coverage is premultiplied before tracing a contour across transparency.
    rgba = source.astype("f")
    rgba[:,:,:3] *= rgba[:,:,3:4]/255
    return cv2.remap(rgba, x - fraction*flow[:,:,0], y - fraction*flow[:,:,1],
                     cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)


def native_ids(image, palette):
    h,w = image.shape
    cells = image.reshape(h//2,2,w//2,2).transpose(0,2,1,3).reshape(-1,4)
    return palette.encode(cells).reshape(h//2,w//2)


def physical_index():
    # Canvas nearest-neighbour selects the lower texel on an exact boundary.
    return ((2*np.arange(466)+1)*368-1)//932


def generate(web, browser, output, golden_path):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    clips = write_atlases(web)
    palette = Dictionary()
    neutral_frames, eye_variants, heads, sums = [], [], [], []
    axis = physical_index()
    with http.server.ThreadingHTTPServer(("127.0.0.1",0), functools.partial(Quiet,directory=str(web))) as server:
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            with sync_playwright() as pw:
                browser = pw.chromium.launch(executable_path=browser)
                page = browser.new_page(device_scale_factor=1)
                origin = f"http://127.0.0.1:{server.server_port}"
                page.route(origin+"/capture",lambda r:r.fulfill(content_type="text/html; charset=utf-8",body="<meta charset=utf-8>"))
                page.goto(origin+"/capture")
                for name in ["clips.js","gesture-clips.js","nap-prop.js","face-fx.js","display.js"]:
                    if (web/name).exists():
                        page.add_script_tag(url=origin+"/"+name)
                page.add_script_tag(path=Path(__file__).with_name("capture.js"))
                page.evaluate("setupPetCapture(true)")
                for clip,steps in (("idle",4),("pat",3)):
                    for frame in range(clips[clip]["frames"]):
                        for phase in range(steps):
                            shot = page.evaluate("capturePose", [clip,frame,phase,steps])
                            stack = pixels(shot["packed"])
                            base = stack[:368,:368]
                            neutral_frames.append(native_ids(base,palette))
                            eyes = []
                            eye_variants.append(eyes)
                            heads.append(shot["head"])
                            x,y,w,h = shot["eyeRegion"]
                            for g in range(15):
                                gx,gy = g%3*w, 368+g//3*h
                                crop = stack[gy:gy+h,gx:gx+w]
                                eyes.append(native_ids(crop,palette))
                                image = base.copy()
                                image[y:y+h,x:x+w] = crop
                                crc = zlib.crc32(image[axis[:,None],axis].tobytes())
                                assert crc == shot["variants"][g], (clip,frame,phase,g,"canvas CRC")
                                sums.append(shot["variants"][g])
                        if frame%10==0: print(clip,frame,len(palette.index),"LCD colours",flush=True)
                browser.close()
        finally:
            server.shutdown()
    def grid(pose,gaze):
        neutral = neutral_frames[pose]
        if gaze == 7:
            return neutral
        result = neutral.copy()
        result[y//2:(y+h)//2,x//2:(x+w)//2] = eye_variants[pose][gaze]
        return result
    from motion_pack import encode
    output.write_bytes(encode(grid,b"".join(palette.index),heads))
    golden_path.write_bytes(np.asarray(sums,dtype="<u4").tobytes())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", default=shutil.which("chromium") or shutil.which("google-chrome") or os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe"))
    options = parser.parse_args()
    with panel_source() as web:
        generate(web,options.browser,COMPONENT/"assets/motion.bpz",COMPONENT/"tests/motion.crc")
