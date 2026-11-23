"""Bake the review's linked HD stage using Chrome's own image sampling, shadow and guides."""
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
import threading
import zlib

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright


COMPONENT = Path(__file__).resolve().parents[1]
WEB = COMPONENT.parents[1] / "assets/ai-girls"
SIDE = 340
GUIDE_X, GUIDE_Y = 160, 326
CLIPS = ["idle", "feed-rice", "sulk-weigh", "ahoge-splash", "jellyfish-play", "deepsleep-nap"]


def rgb565(rgba):
    rgb = rgba[:, :, :3].astype(np.uint16)
    return ((rgb[:, :, 0] >> 3) << 11 | (rgb[:, :, 1] >> 2) << 5 |
            rgb[:, :, 2] >> 3).astype("<u2")


class QuietServer(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class Pack:
    def __init__(self):
        self.items = []
        self.crcs = []
        self.sources = []

    def with_sheets(self, web, sheets, limit):
        package = Pack()
        package.items = self.items.copy()
        package.crcs = self.crcs.copy()
        package.sources = self.sources.copy()
        for entry, path in enumerate(sheets, 18):
            with Image.open(web / path.lstrip("/")) as im:
                im = im.convert("RGBA")
                if limit:
                    im.thumbnail((limit, limit), Image.Resampling.LANCZOS)
                backing = Image.new("RGBA", im.size, "#0d1527")
                backing.alpha_composite(im)
                pixels = rgb565(np.asarray(backing))
                package.add(im.width, im.height, pixels.tobytes())
                package.golden(entry, 0, False, pixels)
            package.sources.append((entry, path))
        if len(package.items) != 22:
            raise ValueError(f"Incomplete HD pack: {len(package.items)} entries")
        return package

    def add(self, width, height, raw):
        packed = zlib.compress(raw, 9)
        self.items.append((width, height, len(raw), packed))

    def golden(self, entry, ms, guide, pixels):
        crc = zlib.crc32(pixels.astype(">u2").tobytes())
        self.crcs.append(f"{entry} {ms} {int(guide)} {crc:08x}\n")

    def save(self, path):
        directory = bytearray()
        body = bytearray()
        pos = 12 + 16 * len(self.items)
        for w, h, n, packed in self.items:
            directory.extend(struct.pack("<HHIII", w, h, n, pos + len(body), len(packed)))
            body.extend(packed)
        payload = directory + body
        path.write_bytes(struct.pack("<4sII", b"HDS1", len(self.items), zlib.crc32(payload)) + payload)
        print(f"{len(payload) + 12:,} bytes, compressed entries {[len(e[-1]) for e in self.items]}", flush=True)


def inspect_stage(url, chrome, manifest, shots, pack, scratch, motion=None):
    with sync_playwright() as api:
        browser = api.chromium.launch(executable_path=chrome, args=["--force-color-profile=srgb"])
        page = browser.new_page(viewport={"width": 1100, "height": 700}, device_scale_factor=1)
        page.add_init_script("window.requestAnimationFrame = () => 0")
        page.goto(url)
        page.evaluate("document.fonts.ready")
        page.add_style_tag(content="""
            html, body {background: transparent !important}
            body > :not(#captureStage) {visibility:hidden}
            #captureStage {position:fixed;left:0;top:0;width:340px;height:340px;background:#0b1120}
            #captureStage.alpha, #captureStage.alpha .hd-stage {background:transparent}
            #captureStage.alpha .hd-stage {border-color:transparent}
        """)
        page.evaluate("""() => {
          const host = document.createElement('div');
          host.id = 'captureStage';
          document.body.append(host);
          host.append(document.getElementById('hdStage'));
        }""")
        cdp = page.context.new_cdp_session(page)
        cdp.send("Emulation.setDefaultBackgroundColorOverride", {"color": {"r": 0, "g": 0, "b": 0, "a": 0}})

        def capture(ms=0, guides=False, alpha=False, hide=False):
            page.evaluate("""([ms, guides, alpha, hide]) => {
              const image = document.getElementById('hdPoseImg');
              document.getElementById('captureStage').classList.toggle('alpha', alpha);
              document.getElementById('hdStage').classList.toggle('guides', guides);
              image.style.visibility = hide ? 'hidden' : '';
              const motion = image.getAnimations()[0];
              motion.pause();
              motion.currentTime = ms;
            }""", [ms, guides, alpha, hide])
            shot = cdp.send("Page.captureScreenshot", {"format": "png", "clip": {
                "x": 0, "y": 0, "width": SIDE, "height": SIDE, "scale": 1,
            }})
            png = base64.b64decode(shot["data"])
            return np.array(Image.open(io.BytesIO(png)).convert("RGBA"))

        def record(entry, ms, guides, rgba):
            pixels = rgb565(rgba)
            pack.golden(entry, ms, guides, pixels)
            if scratch:
                Image.fromarray(rgba).save(scratch / f"entry{entry:02d}-{ms:04d}-{int(guides)}.png")
            return pixels

        backdrop = record(0, 0, False, capture(hide=True))
        pack.add(SIDE, SIDE, backdrop.tobytes())
        pack.sources.append((0, "review.html#hdStage"))
        selectors = [f'[data-clip="{name}"]' for name in CLIPS]
        selectors += [f'[data-idx="{i}"]' for i in range(1, len(manifest["poses_order"]))]
        for entry, selector in enumerate(selectors, 1):
            path = page.evaluate("""async s => {
                document.querySelector(s).click();
                const im = document.getElementById('hdPoseImg');
                await im.decode();
                if (im.naturalWidth !== 680 || im.naturalHeight !== 680)
                    throw Error('Unexpected HD image dimensions');
                return new URL(im.src).pathname;
            }""", selector)
            pack.sources.append((entry, path))
            base = record(entry, 0, False, capture())
            guides = record(entry, 0, True, capture(guides=True))
            delta = base != guides
            delta[:, GUIDE_X] = False
            delta[GUIDE_Y] = False
            if delta.any():
                ys, xs = np.nonzero(delta)
                raise ValueError(f"Guides exceed expected lines in {entry}, {(xs.min(),ys.min(),xs.max(),ys.max())}")
            if motion is not None:
                motion.add(entry, 0, base, guides)
                for level, ms in enumerate(motion.times(), 1):
                    motion.add(entry, level, rgb565(capture(ms)), rgb565(capture(ms, True)))
                print(f"HD motion {entry}/17", flush=True)
                continue
            cutout = capture(alpha=True)
            if scratch:
                Image.fromarray(cutout).save(scratch / f"alpha{entry:02d}.png")
            pack.add(SIDE, SIDE, base.tobytes() + cutout[:, :, 3].tobytes() +
                     guides[:, GUIDE_X].tobytes() + guides[GUIDE_Y].tobytes())
            for ms in shots:
                for guide in (False, True):
                    record(entry, ms, guide, capture(ms, guide))
        # Obtain sheet order and paths through the review's own controls.
        sheets = []
        for i in range(page.locator("#sheetBtns button").count()):
            path = page.evaluate("""async i => {
              document.querySelectorAll('#sheetBtns button')[i].click();
              const im = document.getElementById('sheetPreview');
              await im.decode();
              return new URL(im.src).pathname;
            }""", i)
            sheets.append(path)
        browser.close()
        return sheets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web", type=Path, default=WEB)
    parser.add_argument("--browser", default=shutil.which("chromium") or shutil.which("google-chrome")
                        or (os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe")
                            if os.name == "nt" else None))
    parser.add_argument("--output", type=Path, default=COMPONENT / "assets/scene.hds")
    parser.add_argument("--ram-output", type=Path, default=COMPONENT / "assets/scene-ram.hds")
    parser.add_argument("--goldens", type=Path, default=Path(__file__).with_name("web.crc"))
    parser.add_argument("--ram-goldens", type=Path, default=Path(__file__).with_name("ram-web.crc"))
    parser.add_argument("--sources", type=Path, default=Path(__file__).with_name("sources.tsv"))
    parser.add_argument("--scratch", type=Path, help="save unquantized Chromium frames for device comparison")
    args = parser.parse_args()
    if args.scratch:
        args.scratch.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((args.web / "pixel/deepseek/v1/deepseek-pixel-manifest.json").read_text(encoding="utf8"))
    handler = functools.partial(QuietServer, directory=str(args.web))
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            pack = Pack()
            url = f"http://127.0.0.1:{server.server_port}/pixel/deepseek/v1/review.html"
            sheets = inspect_stage(url, args.browser, manifest, (350, 700, 1050, 1400), pack, args.scratch)
            native = pack.with_sheets(args.web, sheets, None)
            ram = pack.with_sheets(args.web, sheets, 768)
            for version, output, goldens in ((native, args.output, args.goldens),
                                            (ram, args.ram_output, args.ram_goldens)):
                version.save(output)
                goldens.write_text("".join(version.crcs), encoding="ascii", newline="\n")
            args.sources.write_text("".join(f"{i}\t{src}\n" for i, src in native.sources),
                                    encoding="utf8", newline="\n")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
