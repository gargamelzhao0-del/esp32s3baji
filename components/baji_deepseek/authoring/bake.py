"""Capture the review's real canvas + DOM at 368 CSS pixels, and pack native RGB565 tiles."""
import argparse
import base64
import functools
import hashlib
import http.server
import io
import json
import os
from pathlib import Path
import shutil
import struct
import threading
import zlib

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright


COMPONENT = Path(__file__).resolve().parents[1]
WEB = COMPONENT.parents[1] / "assets" / "ai-girls"
SIDE, PANEL, STEPS, POSES = 368, 466, 7, 66
COLS = SIDE // 16
PHASES = ("dawn", "day", "dusk", "night")
VIEWS = ("Full", "Close")
# Logical source pixels at physical pixel centers. All wire pixels and CRCs
# are big endian (RRRRRGGG GGGBBBBB); all index/header integers little endian.
SOURCE = ((2 * np.arange(PANEL) + 1) * SIDE) // (2 * PANEL)


class QuietServer(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class Dictionary:
    def __init__(self):
        self.codebook = {}
        self.data = bytearray()

    def index(self, records, width):
        record_size = records.dtype.itemsize * width
        raw = np.ascontiguousarray(records).ravel().view(f"V{record_size}")
        unique, inverse = np.unique(raw, return_inverse=True)
        indexes = []
        for record in unique:
            key = record.tobytes()
            index = self.codebook.get(key)
            if index is None:
                index = len(self.codebook)
                self.codebook[key] = index
                self.data.extend(key)
            indexes.append(index)
        return np.array(indexes, dtype="<u4")[inverse]

    def ids(self, width):
        values = np.frombuffer(self.data, dtype="<u4")
        if values.max() >= (1 << (8 * width)):
            raise OverflowError(f"{width}-byte dictionary index overflow")
        return values.view("u1").reshape(-1, 4)[:, :width].tobytes()


class Pack:
    def __init__(self):
        self.quads = [Dictionary() for _ in range(4)]
        self.rows = Dictionary()
        self.maps = bytearray()
        self.goldens = []

    def append(self, png, phase, view, pose):
        with Image.open(io.BytesIO(png)) as im:
            if im.size != (STEPS * SIDE, SIDE):
                raise ValueError(f"Invalid screenshot size {im.size}")
            rgb = np.asarray(im.convert("RGB"), dtype=np.uint16)
        pixels = ((rgb[:, :, 0] >> 3) << 11 | (rgb[:, :, 1] >> 2) << 5 |
                  (rgb[:, :, 2] >> 3)).astype(">u2")
        cells = pixels.reshape(SIDE, STEPS, SIDE).transpose(1, 0, 2)
        blocks = cells
        for table in self.quads:
            n = blocks.shape[1] // 2
            children = blocks.reshape(STEPS, n, 2, n, 2).transpose(0, 1, 3, 2, 4)
            blocks = table.index(children, 4).reshape(STEPS, n, n)
        rows = self.rows.index(blocks, COLS)
        if rows.max() >= 65536:
            raise OverflowError("16-bit row dictionary exhausted")
        self.maps.extend(rows.astype("<u2").tobytes())

        for bubble, cell in enumerate(cells):
            # Independent reference, taken from the browser pixels before encoding.
            panel = cell[SOURCE[:, None], SOURCE]
            crc = zlib.crc32(panel.tobytes())
            self.goldens.append(f"{phase} {view} {pose} {bubble} {crc:08x}\n")

    def save(self, output, goldens):
        frames = len(PHASES) * len(VIEWS) * POSES
        if len(self.maps) != frames * STEPS * COLS * 2:
            raise ValueError("Incomplete set of browser captures")
        q2, q4, q8, q16 = self.quads
        data = self.maps + self.rows.ids(3) + q16.ids(3) + q8.ids(3) + q4.ids(2) + q2.data
        counts = [len(d.codebook) for d in [*self.quads, self.rows]]
        header = struct.pack("<4s9I", b"DSK2", STEPS, frames, *counts, len(data), zlib.crc32(data))
        output.write_bytes(header + data)
        goldens.write_text("".join(self.goldens), encoding="ascii", newline="\n")
        print(f"{len(data) + len(header):,} bytes, dictionary sizes {counts}, "
              f"SHA256 {hashlib.sha256(header + data).hexdigest()}")


def capture(url, browser_path, manifest, pack):
    with sync_playwright() as api:
        browser = api.chromium.launch(executable_path=browser_path, args=["--force-color-profile=srgb"])
        print(f"Chromium {browser.version}", flush=True)
        page = browser.new_page(viewport={"width": STEPS * SIDE, "height": SIDE + 5}, device_scale_factor=1)
        page.add_init_script("window.requestAnimationFrame = () => 0;")
        page.goto(url)
        page.evaluate("document.fonts.ready")
        # The source stores atlas Image objects privately, so decode the same
        # resources before reselecting a frame; a broken atlas aborts the bake.
        images = ["deepseek-states-atlas.png", "props/icons-16x16.png"]
        for clip in manifest["clips"].values():
            images.append(clip["file"])
        page.evaluate("""srcs => Promise.all(srcs.map(src => {
            const im = new Image(); im.src = src; return im.decode();
        }))""", images)
        setup = Path(__file__).with_name("capture.js").read_text(encoding="utf8")
        page.evaluate(f"window.captureClick = ({setup})({STEPS}); 0")
        cdp = page.context.new_cdp_session(page)

        def click(selector):
            return page.evaluate("selector => window.captureClick(selector)", selector)

        def snap(phase, view, pose):
            shot = cdp.send("Page.captureScreenshot", {
                "format": "png", "optimizeForSpeed": True,
                "clip": {"x": 0, "y": 0, "width": SIDE * STEPS, "height": SIDE, "scale": 1},
            })
            pack.append(base64.b64decode(shot["data"]), phase, view, pose)

        for phase, name in enumerate(PHASES):
            click(f'[data-phase="{name}"]')
            for view, distance in enumerate(VIEWS):
                click("#view" + distance)
                pose = 0
                for clip_name, clip in manifest["clips"].items():
                    for i in range(clip["frames"]):
                        mode, frame = click(f'[data-clip="{clip_name}"]' if i == 0 else "#btnNext")
                        if mode != "clip: " + clip_name or frame != f"{i + 1} / {clip['frames']}":
                            raise ValueError(f"Review changed clip: {mode} {frame}")
                        snap(phase, view, pose)
                        pose += 1
                for i, pose_name in enumerate(manifest["poses_order"]):
                    mode, _ = click(f'[data-idx="{i}"]')
                    if mode != "state: " + pose_name:
                        raise ValueError(f"Review changed pose: {mode}")
                    snap(phase, view, pose)
                    pose += 1
                if pose != POSES:
                    raise ValueError("Update firmware frame ordering for this review")
                print(f"{name}/{distance}: {pose} poses", flush=True)
        browser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web", type=Path, default=WEB, help="directory containing pixel/deepseek/v1")
    parser.add_argument("--browser", default=shutil.which("chromium") or shutil.which("google-chrome")
                        or (os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe")
                            if os.name == "nt" else None))
    parser.add_argument("--output", type=Path, default=COMPONENT / "assets/scene.dsk")
    parser.add_argument("--goldens", type=Path, default=Path(__file__).with_name("web.crc"))
    args = parser.parse_args()
    manifest = json.loads((args.web / "pixel/deepseek/v1/deepseek-pixel-manifest.json").read_text(encoding="utf8"))
    handler = functools.partial(QuietServer, directory=str(args.web))
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            pack = Pack()
            url = f"http://127.0.0.1:{server.server_port}/pixel/deepseek/v1/review.html"
            capture(url, args.browser, manifest, pack)
            pack.save(args.output, args.goldens)
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
