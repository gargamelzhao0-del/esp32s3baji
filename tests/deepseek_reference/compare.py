"""Compare the production C painter with the actual review page, including its CSS overlay."""
import argparse
import base64
import contextlib
import itertools
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import zlib
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "assets/ai-girls"
COMPONENT = ROOT / "components/baji_deepseek"
POSES = (8, 12, 10, 8, 8, 8) + (1,) * 12
CLIPS = ("idle", "feed-rice", "sulk-weigh", "ahoge-splash", "jellyfish-play", "deepsleep-nap")
PHASES = ("dawn", "day", "dusk", "night")
WINDOW = 368
PANEL = 466


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


@contextlib.contextmanager
def reference_server(url):
    if url:
        yield url
        return
    with ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(ART))) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            yield f"http://127.0.0.1:{server.server_port}/pixel/deepseek/v1/review.html"
        finally:
            server.shutdown()
            worker.join()


def build_renderer(work):
    exe = work / ("render.exe" if os.name == "nt" else "render")
    subprocess.run([
        os.environ.get("CC", "gcc"), "-std=c11", "-O2",
        "-I", ROOT / "components/baji_graphics/include", "-I", COMPONENT / "include",
        COMPONENT / "deepseek_review.c", COMPONENT / "deepseek_canvas.c",
        COMPONENT / "tests/render.c", "-o", exe,
    ], check=True)
    return exe


def keys(full, levels):
    for item, count in enumerate(POSES):
        for frame in range(count):
            if full:
                yield from ((item, frame, phase, view, bubble)
                            for phase, view, bubble in itertools.product(range(4), range(2), range(levels)))
            else:
                # Each distinct character drawing appears at both distances. Interleave
                # lighting and cloud position so the sample exercises every control.
                for view in range(2):
                    yield (item, frame, (item + frame + view) % 4, view, (item + frame * 2 + view) % levels)


def prepare(page, url, levels=7):
    page.add_init_script("window.requestAnimationFrame = () => 0")
    page.goto(url, wait_until="load")
    page.evaluate("""async levels => {
        await document.fonts.ready;
        const card = document.querySelector('.bezel');
        const strip = document.createElement('div');
        strip.style.cssText = 'position:fixed;left:0;top:0;display:flex;background:#000;z-index:1000';
        document.body.append(strip);
        for (let i = 0; i < levels; i++) {
            const box = document.createElement('div');
            box.dataset.sample = i;
            box.style.cssText = 'width:368px;height:368px;overflow:hidden;background:black';
            strip.append(box);
            if (i === 0) box.append(card);
        }
        for (const url of performance.getEntriesByType('resource')
                     .map(r => r.name).filter(s => /deepseek-.*png|icons-16x16.png/.test(s))) {
            const image = new Image();
            image.src = url;
            await image.decode();
        }
    }""", levels)


def screenshot(page, key, levels, cdp=None, offset=None):
    item, frame, phase, close, bubble = key
    page.evaluate("""([selector, frame, phase, view, amount, slot]) => {
        const box = document.querySelector('[data-sample="' + slot + '"]');
        const card = document.querySelector('.bezel');
        if (card.parentNode !== box) box.append(card);
        document.querySelector(selector).click();
        for (let step = 0; step < frame; step++) document.querySelector('#btnNext').click();
        document.querySelector('[data-phase="' + phase + '"]').click();
        document.querySelector(view).click();
        const cloud = document.querySelector('#cloudBubble');
        const anim = cloud.getAnimations()[0];
        anim.pause();
        let lo = 0, hi = 1200;
        for (let i = 0; i < 23; i++) {
            anim.currentTime = (lo + hi) / 2;
            const y = new DOMMatrix(getComputedStyle(cloud).transform).m42;
            if (-y < amount) lo = anim.currentTime; else hi = anim.currentTime;
        }
        anim.currentTime = amount === 0 ? 0 : amount === 3 ? 1200 : (hi + lo) / 2;
    }""", [
        f'[data-clip="{CLIPS[item]}"]' if item < 6 else f'[data-idx="{item - 6}"]',
        frame, PHASES[phase], "#viewClose" if close else "#viewFull",
        bubble * 3 / (levels - 1) if offset is None else offset, bubble,
    ])
    clip = {"x": bubble * WINDOW, "y": 0, "width": WINDOW, "height": WINDOW}
    if cdp:
        png = base64.b64decode(cdp.send("Page.captureScreenshot", {
            "format": "png", "optimizeForSpeed": True, "clip": dict(clip, scale=1),
        })["data"])
    else:
        png = page.screenshot(clip=clip)
    return Image.open(BytesIO(png)).convert("RGB")


def physical_rgb565(img):
    source = np.asarray(img, dtype=np.uint16)
    packed = (source[:, :, 0] >> 3) << 11 | (source[:, :, 1] >> 2) << 5 | (source[:, :, 2] >> 3)
    index = ((2 * np.arange(PANEL) + 1) * WINDOW) // (2 * PANEL)
    return packed[np.ix_(index, index)].astype(">u2").tobytes()


def check_cloud(page, cdp, exe, pack, tmp):
    # Choose intermediate times across every button and both viewing distances.
    # RGB565 channels are expanded back to the same 0..255 scale for comparison.
    cases = [(i, 3 * i % POSES[i], (i + v) % 4, v, (i + v) % 6, (2*i + 3*v) % 7 + 1)
             for i in range(18) for v in range(2)]
    near = [(*key[:4], key[4] + int(key[5] >= 4)) for key in cases]
    refs = []
    for key in cases:
        offset = (key[4] + key[5] / 8) / 2
        refs.append(physical_rgb565(screenshot(page, key[:5], 7, cdp, offset)))

    def channels(data):
        p = np.frombuffer(data, dtype=">u2").reshape(-1, PANEL, PANEL)[:, 56:159, 222:401]
        return np.stack(((p >> 11) * (255 / 31), ((p >> 5) & 63) * (255 / 63),
                         (p & 31) * (255 / 31)), axis=-1)

    reference = channels(b"".join(refs))
    errors = []
    for mode, items in [([], near), (["--blend"], cases)]:
        dump = Path(tmp) / "cloud.rgb"
        query = "".join(" ".join(map(str, key)) + "\n" for key in items)
        subprocess.run([exe, *mode, pack, dump], input=query, text=True, check=True,
                       stdout=subprocess.DEVNULL)
        errors.append(np.abs(reference - channels(dump.read_bytes())).mean())
    print(f"{len(cases)} intermediate cloud positions; mean absolute 8-bit channel error "
          f"in cloud region: nearest {errors[0]:.3f}, blended {errors[1]:.3f}")
    if errors[1] >= errors[0]:
        raise RuntimeError("Interpolating cloud samples did not improve the reference match")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--url", help="optional URL of the running review.html")
    ap.add_argument("--chrome", default=os.environ.get("CHROME_BIN") or shutil.which("google-chrome"))
    ap.add_argument("--full", action="store_true", help="check all backgrounds, distances and cloud samples")
    ap.add_argument("--pack", type=Path, default=COMPONENT / "assets/scene.dsk")
    ap.add_argument("--renderer", type=Path, help="use an existing host renderer executable")
    ap.add_argument("--levels", type=int, default=7)
    ap.add_argument("--strict", action="store_true", help="also require identical browser gradient dithering")
    ap.add_argument("--cloud", action="store_true", help="also compare interpolated bubble positions")
    args = ap.parse_args()
    cases = list(keys(args.full, args.levels))
    with tempfile.TemporaryDirectory() as tmp, reference_server(args.url) as url, sync_playwright() as pw:
        exe = args.renderer or build_renderer(Path(tmp))
        query = "".join(" ".join(map(str, k)) + "\n" for k in cases)
        run = subprocess.run([exe, args.pack], input=query, capture_output=True, text=True, check=True)
        crc = run.stdout.splitlines()
        if len(crc) != len(cases):
            raise RuntimeError(f"renderer supplied {len(crc)} CRCs for {len(cases)} cases: {run.stderr}")
        chrome = args.chrome
        if not chrome and os.name == "nt":
            chrome = str(Path(os.environ["ProgramFiles"]) / "Google/Chrome/Application/chrome.exe")
        browser = pw.chromium.launch(executable_path=chrome, args=["--force-color-profile=srgb"])
        page = browser.new_page(viewport={"width": args.levels * WINDOW, "height": WINDOW + 5}, device_scale_factor=1)
        prepare(page, url, args.levels)
        cdp = page.context.new_cdp_session(page)
        exact = affected = 0
        for key, actual in zip(cases, crc):
            pixels = physical_rgb565(screenshot(page, key, args.levels, cdp))
            expected = zlib.crc32(pixels)
            if expected == int(actual, 16):
                exact += 1
                continue
            dump = Path(tmp) / "frame.rgb565"
            subprocess.run([exe, args.pack, dump], input=" ".join(map(str, key)), text=True,
                           check=True, stdout=subprocess.DEVNULL)
            ref = np.frombuffer(pixels, dtype=">u2").astype(np.int32)
            dev = np.frombuffer(dump.read_bytes(), dtype=">u2").astype(np.int32)
            changed = int(np.count_nonzero(ref != dev))
            peak = max(np.abs(((ref >> shift) & mask) - ((dev >> shift) & mask)).max()
                       for shift, mask in [(0, 31), (5, 63), (11, 31)])
            if args.strict or peak > 1:
                raise RuntimeError(f"{key}: browser {expected:08x}, painter {actual}, "
                                   f"{changed} different pixels, peak channel difference {peak}")
            affected += changed
        print(f"{len(cases)} independent browser snapshots, {exact} exact frames, "
              f"{affected} pixels within one RGB565 channel step at {PANEL}x{PANEL}")
        if args.cloud:
            check_cloud(page, cdp, exe, args.pack, tmp)
        browser.close()


if __name__ == "__main__":
    main()
