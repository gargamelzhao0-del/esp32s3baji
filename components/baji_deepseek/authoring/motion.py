"""Build the motion-capable DSK3 from the same browser anchors and indexed scene art."""

import argparse
import base64
import hashlib
import importlib.util
import io
import json
import struct
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

from bake import COMPONENT, WEB, PHASES, QuietServer
from functools import partial
from http.server import ThreadingHTTPServer
from threading import Thread

W, H = 138, 132
COLS, ROWS = 19, 18  # eight source pixels per mesh cell, with a terminating control point
CLIPS = ("idle", "feed-rice", "sulk-weigh", "ahoge-splash", "jellyfish-play", "deepsleep-nap")


def prop_rows(source):
    # Execute only the reference's animation recipes. Capture their rig controls
    # instead of resampling 680px art or writing over any of the review images.
    spec = importlib.util.spec_from_file_location("whale_rig", source)
    rig = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rig)
    captured = []

    def collect(image, **controls):
        captured.append((image, controls))

    rig.warp_hd_smooth = collect
    rig.sample_tile_indexed = lambda *args, **kwargs: np.zeros((H, W), np.uint8)
    aligned = {k: k for k in rig.POSES}
    signature = {
        k: k
        for k in (
            "feed_munch",
            "feed_mega_basin",
            "sulk_weigh",
            "ahoge_spout",
            "jellyfish_play",
            "wave_pillow_nap",
        )
    }
    result = []
    for name in CLIPS:
        captured.clear()
        fn = getattr(rig, "build_clip_" + name.replace("-", "_"))
        if name == "idle":
            fn(aligned)
        elif name == "deepsleep-nap":
            fn(signature)
        else:
            fn(aligned, signature)
        result.append(list(captured))
    return rig, result


def displacement(rig, controls, crop):
    # The authoring rig maps destination coordinates back into one source pose.
    # Sample its field on a coarse mesh; the device interpolates each scanline.
    ox, oy, step = crop
    xx = np.clip(np.rint(ox + (8 * np.arange(COLS) + 0.5) * step).astype(int), 0, 679)
    yy = np.clip(np.rint(oy + (8 * np.arange(ROWS) + 0.5) * step).astype(int), 0, 679)

    def field(name):
        return getattr(rig, "_W_" + name)[np.ix_(yy, xx)]

    def val(name):
        return controls.get(name, 0.0)

    x = -val("dx") - ((xx[None, :] - rig.TARGET_FACE_X) * val("breath") * 0.42) * field("BREATH")
    x -= val("sway_x") * field("SWAY") + val("ahoge_dx") * field("AHOGE")
    y = -val("dy") + val("breath") * 360 * field("BREATH") - val("ahoge_dy") * field("AHOGE")
    y -= (
        val("tail_dy") * field("TAIL")
        + val("left_prop_dy") * field("LEFT_PROP")
        + val("spout_dy") * field("SPOUT")
    )
    return np.stack([x, y], axis=-1) / step


def run_lengths(values):
    a = values.ravel()
    edges = np.flatnonzero(np.r_[True, a[1:] != a[:-1], True])
    return zip(a[edges[:-1]].tolist(), np.diff(edges).tolist())


def rle_sprite(a):
    out = bytearray()
    for value, length in run_lengths(a):
        while length:
            n = min(length, 259)
            out.append(value | ((min(n, 4) - 1) << 6))
            if n >= 4:
                out.append(n - 4)
            length -= n
    return bytes(out)


def blocks(items):
    positions = np.cumsum([0] + [len(i) for i in items], dtype="<u4")
    return positions.tobytes() + b"".join(items)


def changed_tiles(sprite, mesh, view, gentle):
    """Exact occupied tile runs for each fractional pose, in CSS source space."""
    yy, xx = np.mgrid[0 : H * 2, 0 : W * 2]
    gy, gx = yy // 16, xx // 16
    fy, fx = (yy % 16)[:, :, None], (xx % 16)[:, :, None]
    m = mesh.astype(np.int32)
    field = (m[gy, gx] * (16 - fx) + m[gy, gx + 1] * fx) * (16 - fy) + (
        m[gy + 1, gx] * (16 - fx) + m[gy + 1, gx + 1] * fx
    ) * fy
    previous = sprite[yy // 2, xx // 2]
    bias = np.array([-3328, 768, -1280, 2816, -2304, 1792, -256, 3840])
    bx = bias[(yy // 2) % 8] if gentle else 0
    by = bias[(xx // 2 + 3) % 8] if gentle else 0
    rows = bytearray()
    top = 44 if view else 36
    dirty = np.zeros((368, 368), dtype=bool)
    for sub in range(1, 8):
        u = (xx * 8192 + 4096 + bx + field[:, :, 0] * sub) >> 14
        v = (yy * 8192 + 4096 + by + field[:, :, 1] * sub) >> 14
        warped = sprite[np.clip(v, 0, H - 1), np.clip(u, 0, W - 1)]
        warped[(u < 0) | (u >= W) | (v < 0) | (v >= H)] = 0
        dirty[top : top + 2 * H, 46 : 46 + 2 * W] = warped != previous
        previous = warped
        by_tile = dirty.reshape(23, 16, 23, 16).any(axis=(1, 3))
        rows.extend(np.packbits(by_tile, axis=1, bitorder="little").tobytes())
    return rows


def controls_and_art(web):
    local = web / "pixel/deepseek/v1"
    manifest = json.loads((local / "deepseek-pixel-manifest.json").read_text(encoding="utf8"))
    rig, clips = prop_rows(local / "build_deepseek_assets.py")
    sprites = []
    meshes = []
    same = []
    damage = []
    for view, view_name in enumerate(("full", "close")):
        for name, motion in zip(CLIPS, clips):
            clip = manifest["clips"][name]
            path = local / clip["file"]
            if hashlib.sha256(path.read_bytes()).hexdigest() != clip["sha256"]:
                raise ValueError(f"Sprite SHA differs from review manifest: {path}")
            im = Image.open(path)
            atlas = np.asarray(im)
            palette = np.asarray(im.getpalette()[: 41 * 3], np.uint16).reshape(41, 3)
            n = clip["frames"]
            if len(motion) != n:
                raise ValueError("Reference clip recipe changed")
            geom = [
                displacement(rig, p, manifest["crops_on_680"][view_name]) for source, p in motion
            ]
            for i in range(n):
                x = i % 8 * W
                y = (view * ((n + 7) // 8) + i // 8) * H
                sprite = atlas[y : y + H, x : x + W]
                sprites.append(rle_sprite(sprite))
                after = (i + 1) % n
                field = np.rint((geom[after] - geom[i]) * 8)
                if np.max(np.abs(field)) > 127:
                    raise ValueError("Pose motion exceeds 16 source pixels")
                meshes.append(field.astype(np.int8).tobytes())
                damage.append(changed_tiles(sprite, field, view, name in ("idle", "deepsleep-nap")))
                same.append(int(motion[i][0] == motion[after][0]))
    rgb = ((palette[:, 0] >> 3) << 11) | ((palette[:, 1] >> 2) << 5) | (palette[:, 2] >> 3)
    return (
        rgb.astype(">u2").tobytes(),
        blocks(sprites),
        bytes(same) + b"".join(meshes) + b"".join(damage),
    )


def chrome_layers(web, browser):
    # Black and white calibration replaces only the canvas sprite. Their
    # difference measures transmission through the real bubble shadow, HUD and
    # circular clip. This lets warped indexed pixels move behind those layers.
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietServer, directory=str(web)))
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as api:
            b = api.chromium.launch(executable_path=browser, args=["--force-color-profile=srgb"])
            p = b.new_page(viewport={"width": 368 * 7, "height": 375}, device_scale_factor=1)
            p.add_init_script("""
window.requestAnimationFrame=()=>0;
const original=CanvasRenderingContext2D.prototype.drawImage;
CanvasRenderingContext2D.prototype.drawImage=function(im,...rest){
    if(this.canvas.id==='badgeCanvas' && rest.length===2 && im instanceof HTMLCanvasElement)
        window.plate=im;
    if(this.canvas.id==='badgeCanvas' && rest.length===8 && rest[2]===138 && window.calibration){
        this.fillStyle=window.calibration;this.fillRect(23,18,138,136);return;
    }
    return original.call(this,im,...rest);
};""")
            p.goto(f"http://127.0.0.1:{server.server_port}/pixel/deepseek/v1/review.html")
            p.evaluate("document.fonts.ready")
            manifest_path = web / "pixel/deepseek/v1/deepseek-pixel-manifest.json"
            clips = json.loads(manifest_path.read_text(encoding="utf8"))["clips"]
            p.evaluate(
                """paths => Promise.all(paths.map(path => {
                    const image = new Image(); image.src = path; return image.decode();
                }))""",
                [*(clips[name]["file"] for name in CLIPS), "props/icons-16x16.png"],
            )
            setup = Path(__file__).with_name("capture.js").read_text(encoding="utf8")
            p.evaluate(f"window.captureClick=({setup})(7);0")
            cdp = p.context.new_cdp_session(p)

            def capture():
                result = cdp.send(
                    "Page.captureScreenshot",
                    {
                        "format": "png",
                        "optimizeForSpeed": True,
                        "clip": {"x": 0, "y": 0, "width": 368 * 7, "height": 368, "scale": 1},
                    },
                )
                return np.array(
                    Image.open(io.BytesIO(base64.b64decode(result["data"]))).convert("RGB"),
                    np.int16,
                )

            bg = []
            for name in PHASES:
                p.evaluate("name=>window.captureClick('[data-phase=\"'+name+'\"]')", name)
                png = p.evaluate('window.plate.toDataURL().split(",")[1]')
                a = np.asarray(
                    Image.open(io.BytesIO(base64.b64decode(png))).convert("RGB"), dtype=np.uint16
                )
                rgb = ((a[:, :, 0] >> 3) << 11) | ((a[:, :, 1] >> 2) << 5) | (a[:, :, 2] >> 3)
                out = bytearray()
                for color, length in run_lengths(rgb):
                    while length:
                        n = min(255, length)
                        out += bytes((n, color >> 8, color & 255))
                        length -= n
                bg.append(bytes(out))
            masks = []
            for clip in CLIPS:
                images = []
                for color in ("#000", "#fff"):
                    p.evaluate(
                        "([key,color])=>{ window.calibration=color; window.captureClick('[data-clip=\"'+key+'\"]') }",
                        [clip, color],
                    )
                    images.append(capture())
                transmission = images[1] - images[0]
                alpha = np.clip(np.rint(np.median(transmission, axis=2) / 255 * 32), 0, 31).astype(
                    "u1"
                )
                # The direct texture shader may bypass the anchor only when all
                # three channels pass through unchanged, even at shadow fringes.
                alpha[np.all(transmission == 255, axis=2)] = 32
                for i in range(7):
                    mask = alpha[:, i * 368 : (i + 1) * 368].copy()
                    mask[:36] = 0
                    mask[308:] = 0
                    mask[:, :46] = 0
                    mask[:, 322:] = 0
                    masks.append(
                        mask.reshape(23, 16, 23, 16).transpose(0, 2, 1, 3).reshape(529, 256)
                    )
            b.close()
        unique, ids = np.unique(np.concatenate(masks), axis=0, return_inverse=True)
        return blocks(bg), ids.astype("<u2").tobytes(), unique.tobytes()
    finally:
        server.shutdown()
        thread.join()
        server.server_close()


def verify_sources(maps, row_tiles, layers, palette, sprites, cloud_maps, masks, steps):
    """Refuse to pair a different atlas revision with existing Chromium anchors."""

    def ids24(data):
        a = np.frombuffer(data, np.uint8).reshape(-1, 3).astype(np.uint32)
        return a[:, 0] | a[:, 1] << 8 | a[:, 2] << 16

    key_maps = np.frombuffer(maps, "<u2").reshape(4, 2, 66, steps, 23)
    rows = ids24(row_tiles).reshape(-1, 23)
    q16, q8, q4, q2 = layers
    tree = [
        ids24(q16).reshape(-1, 4),
        ids24(q8).reshape(-1, 4),
        np.frombuffer(q4, "<u2").reshape(-1, 4),
    ]
    colors = np.frombuffer(q2, ">u2").reshape(-1, 4)
    pal = np.frombuffer(palette, ">u2")
    offsets = np.frombuffer(sprites, "<u4", 109)
    alpha_maps = np.frombuffer(cloud_maps, "<u2").reshape(6, steps, 23, 23)
    alpha_tiles = np.frombuffer(masks, np.uint8).reshape(-1, 16, 16)
    sy, sx = np.mgrid[1:H:2, 1:W:2]
    for view in range(2):
        x, y = sx * 2 + 46, sy * 2 + (44 if view else 36)
        for pose in range(54):
            idx = view * 54 + pose
            key = rows[key_maps[0, view, pose, 0, y // 16], x // 16]
            for size, children in zip((8, 4, 2), tree):
                key = children[key, (y // size % 2) * 2 + x // size % 2]
            rgb = colors[key, (y % 2) * 2 + x % 2]
            words = np.frombuffer(
                sprites, np.uint8, int(offsets[idx + 1] - offsets[idx]), 4 * 109 + int(offsets[idx])
            )
            lengths, indices = [], []
            it = iter(words.tolist())
            for word in it:
                n = (word >> 6) + 1
                lengths.append(n + next(it) if n == 4 else n)
                indices.append(word & 63)
            sprite = np.repeat(indices, lengths).reshape(H, W)[sy, sx]
            clip = np.searchsorted((8, 20, 30, 38, 46), pose, side="right")
            tile = alpha_maps[clip, 0, y // 16, x // 16]
            exposed = (sprite != 0) & (alpha_tiles[tile, y % 16, x % 16] == 32)
            if not np.any(exposed) or np.any(pal[sprite[exposed]] != rgb[exposed]):
                raise ValueError(
                    f"Browser anchors and sprite atlas differ at view {view}, pose {pose}"
                )


def pack(scene, palette, sprites, meshes, bg, maps, masks):
    magic, steps, frames, n2, n4, n8, n16, rows, sz, crc = struct.unpack_from("<4s9I", scene)
    if magic != b"DSK2" or len(scene) != 40 + sz or zlib.crc32(scene[40:]) != crc:
        raise ValueError("Expected original validated DSK2 keyframes")
    off = 40
    source = memoryview(scene)

    def take(n):
        nonlocal off
        data = source[off : off + n]
        off += n
        return data

    frames_raw = take(steps * frames * 23 * 2)
    row_ids = np.frombuffer(take(rows * 23 * 3), "u1").reshape(rows, 23, 3)
    padded = np.pad(row_ids, ((0, 0), (0, 1), (0, 0))).reshape(-1, 9)
    triples, triple_ids = np.unique(padded, axis=0, return_inverse=True)
    quad16 = take(n16 * 12)
    quad8 = take(n8 * 12)
    raw_quads = take(n4 * 8)
    pairs, pair_ids = np.unique(
        np.frombuffer(raw_quads, "u1").reshape(-1, 4), axis=0, return_inverse=True
    )
    quad2 = take(n2 * 8)
    verify_sources(
        frames_raw, row_ids, (quad16, quad8, raw_quads, quad2), palette, sprites, maps, masks, steps
    )
    if len(triples) > 65536 or len(pairs) > 65536:
        raise ValueError("dictionary too large")
    data = b"".join(
        (
            frames_raw,
            triple_ids.astype("<u2").tobytes(),
            triples.tobytes(),
            quad16,
            quad8,
            pair_ids.astype("<u2").tobytes(),
            pairs.tobytes(),
            quad2,
            palette,
            sprites,
            meshes,
            bg,
            maps,
            masks,
        )
    )
    return (
        struct.pack(
            "<4s13I",
            b"DSK3",
            steps,
            frames,
            n2,
            n4,
            n8,
            n16,
            rows,
            len(data),
            zlib.crc32(data),
            len(triples),
            len(pairs),
            len(sprites),
            len(bg),
        )
        + data
    )


def main():
    import shutil, os

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--web", type=Path, default=WEB)
    ap.add_argument("--anchors", type=Path, required=True)
    ap.add_argument("--output", type=Path, default=COMPONENT / "assets/scene.dsk")
    ap.add_argument(
        "--browser",
        default=shutil.which("chromium")
        or shutil.which("google-chrome")
        or os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe"),
    )
    a = ap.parse_args()
    materials = controls_and_art(a.web)
    layers = chrome_layers(a.web, a.browser)
    data = pack(a.anchors.read_bytes(), *materials, *layers)
    a.output.write_bytes(data)
    print(
        f"{len(data):,} bytes; sprite/mesh/background/mask bytes:",
        *[len(x) for x in (*materials, *layers)],
    )


if __name__ == "__main__":
    main()
