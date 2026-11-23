"""Build Version 1 (Pixel-Art Production Pack v2) and Version 2 (Normalized HD Backup Pack)
for the second badge character: DeepSeek Whale-Girl (深蓝女仆鲸鱼娘).

v2 Studio-Grade Upgrades:
- Embedded surgical alpha-matting & additive-glow un-premultiplication for 6 bespoke signature poses
  (feed_munch, feed_mega_basin, sulk_weigh, ahoge_spout, jellyfish_play, wave_pillow_nap)
- Continuous C1 raised-cosine smooth displacement warp (ZERO horizontal seams / ZERO white cracks)
- Enhanced 40-color Ocean-Maid v2 palette (brighter luminous cyan-turquoise hair tips & vivid blush)
- ZERO fixed-coordinate blush/star painting over eyes; pure outer-ambient pixel FX only
- 6 dual-view (full + close) pixel animation sheets including new DeepSleep wave-pillow nap clip
- Automated QA enforcing mode=P, transparency=0, pinholes=0, AND horizontal-slits=0
"""
from __future__ import annotations

from collections import deque
import hashlib
import json
import math
import os
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance

ROOT = Path(__file__).resolve().parents[3]  # esp32s3baji/assets/ai-girls
SRC_DIR = ROOT / "state-poses" / "deepseek"
PIXEL_DIR = ROOT / "pixel" / "deepseek" / "v1"
PROPS_DIR = PIXEL_DIR / "props"
NORMAL_DIR = ROOT / "normal" / "deepseek" / "v1"
NORMAL_POSES_DIR = NORMAL_DIR / "poses"
NORMAL_SIG_DIR = NORMAL_DIR / "signature-poses"

POSES = [
    "idle", "thinking", "reading", "writing",
    "running", "searching", "asking", "happy",
    "oops", "tired", "compact", "sleeping",
]

# 40-color Ocean-Maid v2 indexed palette (index 0 = transparent)
# Boosted luminous cyan-turquoise hair tips (10..14) + vivid tsundere blush/anger (27..30)
PALETTE_HEX = (
    # 1..7: Crisp dark-indigo outline & deep navy maid dress / upper hair
    "20243a 2c3456 3b4774 4d6096 637bb4 7f99cf a3bbe3 "
    # 8..14: Deep ocean-blue to luminous cyan-turquoise hair tips & water spout
    "2f64a0 3e83c4 4ea6dc 5cc8ea 7ee2f4 b4f3fc ebfbff "
    # 15..19: Crisp maid lace headdress, apron & fluffy white rice
    "c5cfe2 dbe2f0 ecf1f9 f8fafe ffffff "
    # 20..23: Whale pectoral head-fins & split tail fluke
    "475274 606f96 8192b8 b5c2da "
    # 24..30: Warm anime cream skin, peach blush & vivid tsundere crimson / anger mark
    "fff0e6 f6dfd4 ebc9bf e4a6ae f493a6 e65f77 b83d54 "
    # 31..36: Wood chopsticks, gold hem bows, starry eyes & ceramic rice grains
    "7e5d40 b58858 e0b77c f7da92 fff3bd e3d8ce "
    # 37..40: Bioluminescent jellyfish & mint weight scale
    "2f8d9c 50bebf 86e8de cefaf2"
).split()

PAL_RGB = np.array([[int(c[i:i + 2], 16) for i in (0, 2, 4)] for c in PALETTE_HEX], dtype=np.float64)
FLAT_PALETTE = [0, 0, 0] + [int(v) for row in PAL_RGB for v in row] + [0] * (3 * (255 - len(PALETTE_HEX)))

CANVAS_HD = 680
TARGET_FACE_X = 320.0
TARGET_FEET_Y = 656
TILE_W, TILE_H = 138, 132

FULL_CROP = (-30.0, -24.0, 5.15)
CLOSE_CROP = (76.0, -6.0, 3.55)

# The normalized sheets ship with the review. Set this only to re-import the original JPEGs.
ARTIFACT_DIR = Path(os.environ.get("DEEPSEEK_ORIGINAL_SHEETS", NORMAL_DIR))
HD_SHEETS_MAP = {
    "deepseek_signature_interactions_sheet_1795429724780.jpg": "deepseek-signature-interactions-sheet.png",
    "deepseek_normal_rig_turnaround_1795429779605.jpg": "deepseek-normal-rig-turnaround.png",
    "deepseek_normal_rig_turnaround_1795429744385.jpg": "deepseek-normal-fullbody-sheet.png",
}

SIG_PANELS = [
    ("feed_munch",      (28,  17,  455, 379)),
    ("feed_mega_basin", (461, 17,  914, 379)),
    ("sulk_weigh",      (920, 17, 1345, 379)),
    ("ahoge_spout",     (28,  387, 455, 742)),
    ("jellyfish_play",  (461, 387, 914, 742)),
    ("wave_pillow_nap", (920, 387, 1345, 742)),
]

SIG_TUNING = {
    "feed_munch":      (0.96,   6,  -2),
    "feed_mega_basin": (0.91,  36,  -4),
    "sulk_weigh":      (0.93,  -4,  -4),
    "ahoge_spout":     (0.93,   0,  -4),
    "jellyfish_play":  (0.92,  28,  -4),
    "wave_pillow_nap": (0.91,  26,  -8),
}


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def detect_face_metrics(rgba: np.ndarray) -> tuple[float, float, float, int]:
    r, g, b, a = rgba[:, :, 0].astype(int), rgba[:, :, 1].astype(int), rgba[:, :, 2].astype(int), rgba[:, :, 3]
    skin = (a > 200) & (r >= 215) & (g >= 155) & (r - b >= 14) & (r - g >= 6)
    h = rgba.shape[0]
    ys, xs = np.where(skin[:int(h * 0.72), :])
    mx, my = float(np.median(xs)), float(np.median(ys))
    rad = max(60, int(h * 0.17))
    face = (np.abs(xs - mx) < rad) & (np.abs(ys - my) < rad * 0.85)
    fxs, fys = xs[face], ys[face]
    ays, _ = np.where(a > 128)
    fw = float(np.percentile(fxs, 95) - np.percentile(fxs, 5))
    return float(fxs.mean()), float(fys.mean()), fw, int(ays.max())


def align_hd_poses() -> tuple[dict[str, Image.Image], dict[str, dict]]:
    NORMAL_POSES_DIR.mkdir(parents=True, exist_ok=True)
    aligned: dict[str, Image.Image] = {}
    meta: dict[str, dict] = {}
    for name in POSES:
        src = Image.open(SRC_DIR / f"{name}.webp").convert("RGBA")
        arr = np.asarray(src)
        fx, fy, _, feet_y = detect_face_metrics(arr)
        dx = int(round(TARGET_FACE_X - fx))
        dy = (TARGET_FEET_Y - feet_y - 14) if name == "happy" else (TARGET_FEET_Y - feet_y)
        canvas = Image.new("RGBA", (CANVAS_HD, CANVAS_HD), (0, 0, 0, 0))
        canvas.alpha_composite(src, (dx, dy))
        out_path = NORMAL_POSES_DIR / f"{name}.png"
        canvas.save(out_path, optimize=True)
        aligned[name] = canvas
        cx, cy = fx + dx, fy + dy
        meta[name] = {
            "source_size": [src.width, src.height],
            "offset": [dx, dy],
            "face_center": [round(cx, 1), round(cy, 1)],
            "anchors": [
                round(cx, 1), round(cy - 108, 1),
                round(cx, 1), round(cy + 14, 1),
                round(cx - 148, 1), round(cy - 28, 1),
                round(cx + 150, 1), round(cy - 28, 1),
                round(cx + 228, 1), round(cy + 178, 1),
            ],
            "sha256": sha256_file(out_path),
        }
    return aligned, meta


def matte_signature_panel(rgb: np.ndarray, is_jellyfish: bool = False) -> np.ndarray:
    """Surgical border-seeded flood-fill + corner trim + additive jellyfish cyan glow un-premultiplication."""
    H, W = rgb.shape[:2]
    bg_rgb = np.array([38.0, 45.0, 64.0])
    arr = rgb.astype(np.float64)
    dist = np.linalg.norm(arr - bg_rgb[None, None, :], axis=2)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    sat = np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b)
    is_grid_slate = (dist < 48.0) & (r >= 28) & (r <= 82) & (g >= 34) & (g <= 94) & (b >= 50) & (b <= 118) & (sat < 42) & (b > g) & (g > r)

    if is_jellyfish:
        is_cyan_glow = (g - r > 22) & (b - r > 28) & (g > 68)
        is_bg_cand = ((dist < 22.0) | is_grid_slate) & (~is_cyan_glow)
    else:
        is_bg_cand = (dist < 22.0) | is_grid_slate

    yy, xx = np.mgrid[0:H, 0:W]
    corner_tri = (
        ((xx + yy) < 34) | (((W - 1 - xx) + yy) < 34)
        | ((xx + (H - 1 - yy)) < 34) | (((W - 1 - xx) + (H - 1 - yy)) < 34)
    )
    is_bg_cand = is_bg_cand | (corner_tri & (dist < 75.0))

    ext_bg = np.zeros((H, W), dtype=bool)
    q: deque[tuple[int, int]] = deque()
    for x in range(W):
        for y in (0, H - 1):
            if is_bg_cand[y, x] and not ext_bg[y, x]:
                ext_bg[y, x] = True
                q.append((y, x))
    for y in range(H):
        for x in (0, W - 1):
            if is_bg_cand[y, x] and not ext_bg[y, x]:
                ext_bg[y, x] = True
                q.append((y, x))
    while q:
        cy, cx = q.popleft()
        for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
            if 0 <= ny < H and 0 <= nx < W and not ext_bg[ny, nx] and is_bg_cand[ny, nx]:
                ext_bg[ny, nx] = True
                q.append((ny, nx))

    visited = ext_bg.copy()
    int_bg = np.zeros((H, W), dtype=bool)
    strict_bg = (dist < 14.0) & (~ext_bg)
    ys, xs = np.where(strict_bg)
    for sy, sx in zip(ys, xs):
        if visited[sy, sx]:
            continue
        comp = []
        qq = deque([(sy, sx)])
        visited[sy, sx] = True
        while qq:
            cy, cx = qq.popleft()
            comp.append((cy, cx))
            for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                if 0 <= ny < H and 0 <= nx < W and not visited[ny, nx] and strict_bg[ny, nx]:
                    visited[ny, nx] = True
                    qq.append((ny, nx))
        if len(comp) >= 12 and np.mean([dist[y, x] for y, x in comp]) < 7.5:
            for y, x in comp:
                int_bg[y, x] = True

    bg_mask = ext_bg | int_bg
    pad = np.pad(bg_mask, 1, mode="constant", constant_values=True)
    near_bg = pad[:-2, 1:-1] | pad[2:, 1:-1] | pad[1:-1, :-2] | pad[1:-1, 2:]
    alpha = np.where(bg_mask, 0.0, 255.0)
    fringe = (~bg_mask) & near_bg & (dist < 36.0)
    alpha[fringe] = np.clip((dist[fringe] - 12.0) / 24.0 * 255.0, 0.0, 255.0)
    rgb_out = arr.copy()

    if is_jellyfish:
        jf_zone = np.zeros((H, W), dtype=bool)
        jf_zone[:192, :172] = True
        halo_mask = jf_zone & (~bg_mask) & (r < 76) & (g < 168)
        solid_jf = jf_zone & ((g >= 152) | ((r >= 95) & (g >= 110)))
        for _ in range(3):
            p = np.pad(solid_jf, 1, mode="constant", constant_values=False)
            solid_jf = solid_jf | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
        pure_halo = halo_mask & (~solid_jf)
        glow_a = np.clip(np.maximum(np.maximum(0.0, g - bg_rgb[1]) / 115.0, np.maximum(0.0, b - bg_rgb[2]) / 125.0), 0.0, 0.72)
        alpha[pure_halo] = np.where(glow_a[pure_halo] < 0.08, 0.0, glow_a[pure_halo] * 255.0)
        rgb_out[pure_halo, 0], rgb_out[pure_halo, 1], rgb_out[pure_halo, 2] = 108.0, 236.0, 242.0

    # Strip disconnected <9px specks
    fg = alpha > 24
    vis = np.zeros((H, W), dtype=bool)
    fys, fxs = np.where(fg)
    for sy, sx in zip(fys, fxs):
        if vis[sy, sx]:
            continue
        comp = []
        qq = deque([(sy, sx)])
        vis[sy, sx] = True
        while qq:
            cy, cx = qq.popleft()
            comp.append((cy, cx))
            for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                if 0 <= ny < H and 0 <= nx < W and not vis[ny, nx] and fg[ny, nx]:
                    vis[ny, nx] = True
                    qq.append((ny, nx))
        if len(comp) < 9:
            for y, x in comp:
                alpha[y, x] = 0.0

    return np.dstack([np.clip(rgb_out, 0, 255).astype(np.uint8), np.round(alpha).astype(np.uint8)])


def extract_signature_poses(ref_idle: Image.Image) -> tuple[dict[str, Image.Image], dict[str, str]]:
    """Ensure HD signature sheet exists, then extract & align all 6 bespoke 680x680 signature poses."""
    NORMAL_DIR.mkdir(parents=True, exist_ok=True)
    NORMAL_SIG_DIR.mkdir(parents=True, exist_ok=True)
    sheet_path = NORMAL_DIR / "deepseek-signature-interactions-sheet.png"
    if not sheet_path.exists():
        sp = ARTIFACT_DIR / "deepseek_signature_interactions_sheet_1795429724780.jpg"
        Image.open(sp).convert("RGBA").save(sheet_path, optimize=True)

    _, rfy, rfw, rfeet = detect_face_metrics(np.asarray(ref_idle))
    sheet_rgb = np.asarray(Image.open(sheet_path).convert("RGB"))
    sig_poses: dict[str, Image.Image] = {}
    sig_hashes: dict[str, str] = {}

    for name, (x0, y0, x1, y1) in SIG_PANELS:
        rgba = matte_signature_panel(sheet_rgb[y0:y1, x0:x1], is_jellyfish=(name == "jellyfish_play"))
        _, fy, fw, feet = detect_face_metrics(rgba)
        if name == "wave_pillow_nap":
            scale = (rfw / max(fw, 1.0)) * 0.96
        else:
            scale = 0.55 * ((rfeet - rfy) / max(feet - fy, 80.0)) + 0.45 * (rfw / max(fw, 1.0))
        s_mul, edx, edy = SIG_TUNING[name]
        scale *= s_mul
        im_s = Image.fromarray(rgba, mode="RGBA").resize(
            (int(round(rgba.shape[1] * scale)), int(round(rgba.shape[0] * scale))), Image.LANCZOS
        )
        arr_s = np.asarray(im_s)
        sfx, _, _, sfeet = detect_face_metrics(arr_s)
        dx = int(round(TARGET_FACE_X - sfx)) + edx
        dy = (652 - sfeet) + edy
        ays, _ = np.where(arr_s[:, :, 3] > 60)
        if int(ays.min()) + dy < 12:
            dy = 12 - int(ays.min())
        canvas = Image.new("RGBA", (CANVAS_HD, CANVAS_HD), (0, 0, 0, 0))
        canvas.alpha_composite(im_s, (dx, dy))
        out_p = NORMAL_SIG_DIR / f"{name}.png"
        canvas.save(out_p, optimize=True)
        sig_poses[name] = canvas
        sig_hashes[f"signature-poses/{name}.png"] = sha256_file(out_p)
    return sig_poses, sig_hashes


def nearest_index(rgb: np.ndarray) -> int:
    diff = PAL_RGB - rgb[None, :]
    err = 0.28 * diff[:, 0] ** 2 + 0.54 * diff[:, 1] ** 2 + 0.18 * diff[:, 2] ** 2
    return int(np.argmin(err)) + 1


def fill_pinholes(tile: np.ndarray) -> int:
    mask = tile != 0
    pad = np.pad(mask, 1, mode="constant", constant_values=False)
    n4 = (
        pad[:-2, 1:-1].astype(np.int8)
        + pad[2:, 1:-1]
        + pad[1:-1, :-2]
        + pad[1:-1, 2:]
    )
    ys, xs = np.where((~mask) & (n4 == 4))
    for y, x in zip(ys, xs):
        vals = [int(tile[y - 1, x]), int(tile[y + 1, x]), int(tile[y, x - 1]), int(tile[y, x + 1])]
        tile[y, x] = max(vals, key=lambda v: (vals.count(v), -vals.index(v)))
    return len(ys)


def sample_tile_indexed(rgba: np.ndarray, crop: tuple[float, float, float], crisp_eyes: bool = False) -> np.ndarray:
    """High-craft area-weighted pixel sampler preserving crisp dark anime outlines & luminous cyan gradients."""
    ox, oy, step = crop
    H, W = rgba.shape[:2]
    src = rgba
    if crisp_eyes:
        # Gently boost local contrast inside the face/eye window so oops @_@ spirals stay crisp even at 5.15x
        im_tmp = Image.fromarray(rgba, mode="RGBA")
        eye_box = (210, 260, 430, 390)
        sub = im_tmp.crop(eye_box)
        rgb_sub = ImageEnhance.Contrast(sub.convert("RGB")).enhance(1.38)
        rgb_sub = ImageEnhance.Sharpness(rgb_sub).enhance(1.55)
        sub_out = Image.merge("RGBA", (*rgb_sub.split(), sub.split()[3]))
        im_tmp.paste(sub_out, eye_box)
        src = np.asarray(im_tmp)

    out = np.zeros((TILE_H, TILE_W), dtype=np.uint8)
    for row in range(TILE_H):
        top = oy + row * step
        y0, y1 = max(0, int(math.floor(top))), min(H, int(math.ceil(top + step)))
        if y0 >= y1:
            continue
        for col in range(TILE_W):
            left = ox + col * step
            x0, x1 = max(0, int(math.floor(left))), min(W, int(math.ceil(left + step)))
            if x0 >= x1:
                continue
            patch = src[y0:y1, x0:x1].astype(np.float64)
            alpha = patch[:, :, 3] / 255.0
            total = alpha.sum()
            if total < (step * step) * 0.44:
                continue
            rgb = patch[:, :, :3].reshape(-1, 3)
            wts = alpha.reshape(-1)
            m = wts > 0.01
            rgb, wts = rgb[m], wts[m]
            wsum = wts.sum()
            avg = (rgb * wts[:, None]).sum(axis=0) / wsum
            lumas = 0.28 * rgb[:, 0] + 0.54 * rgb[:, 1] + 0.18 * rgb[:, 2]
            order = np.argsort(lumas)
            lim = wsum * 0.22
            rem = lim
            dark = np.zeros(3)
            for idx in order:
                tk = min(rem, wts[idx])
                dark += tk * rgb[idx]
                rem -= tk
                if rem <= 1e-9:
                    break
            dark /= max(lim, 1e-9)
            avg_luma = 0.28 * avg[0] + 0.54 * avg[1] + 0.18 * avg[2]
            dark_luma = 0.28 * dark[0] + 0.54 * dark[1] + 0.18 * dark[2]
            contrast = avg_luma - dark_luma
            if total < step * step * 0.86 and dark_luma < 145:
                target = dark
            elif contrast > 36:
                k = 0.82 if contrast > 78 else 0.58
                target = avg * (1 - k) + dark * k
            else:
                target = avg
            out[row, col] = nearest_index(target)
    fill_pinholes(out)
    return out


def save_indexed(arr: np.ndarray, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    clean = arr.copy()
    fill_pinholes(clean)
    im = Image.fromarray(clean, mode="P")
    im.putpalette(FLAT_PALETTE)
    im.save(path, transparency=0, optimize=True)


def indexed_to_rgba(arr: np.ndarray) -> Image.Image:
    im = Image.fromarray(arr, mode="P")
    im.putpalette(FLAT_PALETTE)
    im.info["transparency"] = 0
    return im.convert("RGBA")


def _smoothstep(v: np.ndarray, edge0: float, edge1: float) -> np.ndarray:
    t = np.clip((v - edge0) / (edge1 - edge0), 0.0, 1.0)
    return 0.5 - 0.5 * np.cos(math.pi * t)


# Precomputed 680x680 continuous coordinate grids & C1 smooth weight fields
_GRID_Y, _GRID_X = np.mgrid[0:CANVAS_HD, 0:CANVAS_HD].astype(np.float32)
_W_BREATH = _smoothstep(652.0 - _GRID_Y, 0.0, 340.0)
_W_SWAY = _smoothstep(652.0 - _GRID_Y, 20.0, 520.0)
_W_AHOGE = np.exp(-(((_GRID_X - 308.0) / 72.0) ** 2 + ((_GRID_Y - 58.0) / 38.0) ** 2)) * _smoothstep(128.0 - _GRID_Y, 0.0, 48.0)
_W_TAIL = _smoothstep(_GRID_X, 440.0, 565.0) * _smoothstep(_GRID_Y, 445.0, 560.0)
_W_LEFT_PROP = _smoothstep(290.0 - _GRID_X, 0.0, 95.0) * _smoothstep(345.0 - _GRID_Y, 0.0, 110.0)
_W_SPOUT = _smoothstep(175.0 - _GRID_Y, 0.0, 75.0) * np.exp(-(((_GRID_X - 320.0) / 145.0) ** 2))


def warp_hd_smooth(
    im: Image.Image,
    dx: float = 0.0,
    dy: float = 0.0,
    breath: float = 0.0,
    sway_x: float = 0.0,
    ahoge_dx: float = 0.0,
    ahoge_dy: float = 0.0,
    tail_dy: float = 0.0,
    left_prop_dy: float = 0.0,
    spout_dy: float = 0.0,
) -> np.ndarray:
    """Continuous C1 smooth displacement field + bilinear RGBA resampling.
    Mathematically guarantees ZERO horizontal tears, ZERO cut seams, and grounded feet!
    """
    arr = np.asarray(im, dtype=np.float32)
    # Inverse displacement field: destination (X, Y) samples from source (sx, sy)
    sx = _GRID_X - dx
    sy = _GRID_Y - dy

    if abs(breath) > 1e-4:
        # Squash/stretch anchored solidly at feet (y=652): breath > 0 stretches gently up, < 0 squashes slightly down
        sy = sy + (breath * 360.0) * _W_BREATH
        sx = sx - ((_GRID_X - TARGET_FACE_X) * breath * 0.42) * _W_BREATH
    if abs(sway_x) > 1e-4:
        sx = sx - sway_x * _W_SWAY
    if abs(ahoge_dx) > 1e-4:
        sx = sx - ahoge_dx * _W_AHOGE
    if abs(ahoge_dy) > 1e-4:
        sy = sy - ahoge_dy * _W_AHOGE
    if abs(tail_dy) > 1e-4:
        sy = sy - tail_dy * _W_TAIL
    if abs(left_prop_dy) > 1e-4:
        sy = sy - left_prop_dy * _W_LEFT_PROP
    if abs(spout_dy) > 1e-4:
        sy = sy - spout_dy * _W_SPOUT

    # Vectorized premultiplied-alpha bilinear interpolation (clean zero-fringe edges!)
    H, W = arr.shape[:2]
    x0 = np.floor(sx).astype(np.int32)
    y0 = np.floor(sy).astype(np.int32)
    fx = (sx - x0)[..., None]
    fy = (sy - y0)[..., None]

    x0c = np.clip(x0, 0, W - 1)
    x1c = np.clip(x0 + 1, 0, W - 1)
    y0c = np.clip(y0, 0, H - 1)
    y1c = np.clip(y0 + 1, 0, H - 1)

    valid = (sx >= 0) & (sx <= W - 1) & (sy >= 0) & (sy <= H - 1)

    a00 = arr[y0c, x0c, 3:4] / 255.0
    a10 = arr[y0c, x1c, 3:4] / 255.0
    a01 = arr[y1c, x0c, 3:4] / 255.0
    a11 = arr[y1c, x1c, 3:4] / 255.0

    w00 = (1.0 - fx) * (1.0 - fy)
    w10 = fx * (1.0 - fy)
    w01 = (1.0 - fx) * fy
    w11 = fx * fy

    p00 = arr[y0c, x0c, :3] * a00
    p10 = arr[y0c, x1c, :3] * a10
    p01 = arr[y1c, x0c, :3] * a01
    p11 = arr[y1c, x1c, :3] * a11

    out_a = w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11
    out_p = w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11

    safe_a = np.where(out_a > 1e-4, out_a, 1.0)
    out_rgb = np.where(out_a > 1e-4, out_p / safe_a, 0.0)
    out_rgba = np.concatenate([np.clip(out_rgb, 0, 255), np.clip(out_a * 255.0, 0, 255)], axis=2)
    out_rgba[~valid] = 0.0
    return np.round(out_rgba).astype(np.uint8)


# Palette index aliases (1-based; 0 = transparent)
C_OUT, C_NAVY, C_NAVY2, C_BLUE1, C_BLUE2, C_PERI, C_PERI_HI = 1, 2, 3, 4, 5, 6, 7
C_OCEAN1, C_OCEAN2, C_CERU, C_CYAN, C_AQUA, C_AQUA_HI, C_FOAM = 8, 9, 10, 11, 12, 13, 14
C_LACE_S, C_LACE_M, C_WHITE_S, C_WHITE, C_PURE = 15, 16, 17, 18, 19
C_FIN1, C_FIN2, C_FIN3, C_FIN_HI = 20, 21, 22, 23
C_SKIN, C_SKIN_S, C_SKIN_D, C_ROSE, C_BLUSH, C_CRIMSON, C_MOUTH = 24, 25, 26, 27, 28, 29, 30
C_WOOD1, C_WOOD2, C_GOLD1, C_GOLD2, C_STAR, C_CERAMIC = 31, 32, 33, 34, 35, 36
C_TEAL1, C_TEAL2, C_MINT, C_GLOW = 37, 38, 39, 40


def pset(arr: np.ndarray, x: int, y: int, c: int) -> None:
    if 0 <= y < arr.shape[0] and 0 <= x < arr.shape[1]:
        arr[y, x] = c


def prect(arr: np.ndarray, x: int, y: int, w: int, h: int, c: int) -> None:
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(arr.shape[1], x + w), min(arr.shape[0], y + h)
    if x0 < x1 and y0 < y1:
        arr[y0:y1, x0:x1] = c


def pellipse(arr: np.ndarray, cx: float, cy: float, rx: float, ry: float, c: int) -> None:
    x0, x1 = max(0, int(math.floor(cx - rx))), min(arr.shape[1] - 1, int(math.ceil(cx + rx)))
    y0, y1 = max(0, int(math.floor(cy - ry))), min(arr.shape[0] - 1, int(math.ceil(cy + ry)))
    for y in range(y0, y1 + 1):
        dy = (y - cy) / max(ry, 0.1)
        for x in range(x0, x1 + 1):
            dx = (x - cx) / max(rx, 0.1)
            if dx * dx + dy * dy <= 1.0:
                arr[y, x] = c


def pline(arr: np.ndarray, x0: int, y0: int, x1: int, y1: int, c: int) -> None:
    steps = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(steps + 1):
        t = i / steps
        pset(arr, int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t)), c)


def add_outline(arr: np.ndarray, color: int = C_OUT) -> np.ndarray:
    out = arr.copy()
    mask = arr != 0
    pad = np.pad(mask, 1, mode="constant", constant_values=False)
    n4 = pad[:-2, 1:-1] | pad[2:, 1:-1] | pad[1:-1, :-2] | pad[1:-1, 2:]
    out[(~mask) & n4] = color
    return out


def draw_sparkle(arr: np.ndarray, cx: int, cy: int, r: int = 2, c_core: int = C_PURE, c_tip: int = C_STAR, only_empty: bool = True) -> None:
    """Draw a tiny outer sparkle; when only_empty=True, NEVER overwrites character pixels (zero face/eye pollution!)."""
    pts = [(cx, cy, c_core)]
    for d in range(1, r + 1):
        col = c_core if d == 1 else c_tip
        pts.extend([(cx - d, cy, col), (cx + d, cy, col), (cx, cy - d, col), (cx, cy + d, col)])
    for px, py, col in pts:
        if 0 <= py < arr.shape[0] and 0 <= px < arr.shape[1]:
            if (not only_empty) or arr[py, px] == 0:
                arr[py, px] = col


def draw_steam_wisp(arr: np.ndarray, x: int, y: int, phase: int = 0, only_empty: bool = False) -> None:
    for i in range(5):
        wx = x + int(round(math.sin((i + phase) * 1.05)))
        wy = y - i
        if 0 <= wy < arr.shape[0] and 0 <= wx < arr.shape[1]:
            if (not only_empty) or arr[wy, wx] == 0:
                arr[wy, wx] = C_FOAM if i < 2 else C_AQUA_HI


def make_rice_bowl(steam_phase: int = 0, with_chopsticks: bool = True) -> np.ndarray:
    a = np.zeros((24, 28), dtype=np.uint8)
    pellipse(a, 14, 15, 9.4, 5.8, C_WHITE_S)
    prect(a, 4, 10, 20, 4, 0)
    pellipse(a, 14, 11, 8.2, 4.4, C_WHITE)
    pellipse(a, 14, 9.5, 6.2, 3.2, C_PURE)
    for gx, gy in ((11, 9), (14, 8), (17, 10), (13, 11)):
        pset(a, gx, gy, C_CERAMIC)
    prect(a, 5, 13, 18, 2, C_OCEAN2)
    prect(a, 6, 13, 16, 1, C_CYAN)
    prect(a, 6, 15, 16, 2, C_WHITE)
    prect(a, 7, 17, 14, 2, C_WHITE_S)
    prect(a, 9, 19, 10, 2, C_LACE_S)
    prect(a, 10, 21, 8, 1, C_LACE_M)
    a = add_outline(a, C_OUT)
    if with_chopsticks:
        pline(a, 2, 10, 12, 12, C_WOOD1)
        pline(a, 2, 11, 12, 13, C_WOOD2)
        pline(a, 3, 7, 12, 10, C_WOOD1)
    if steam_phase >= 0:
        draw_steam_wisp(a, 10, 5, steam_phase)
        draw_steam_wisp(a, 17, 5, steam_phase + 2)
    return a


def make_mega_rice_basin(steam_phase: int = 0) -> np.ndarray:
    a = np.zeros((36, 46), dtype=np.uint8)
    pellipse(a, 23, 21, 18.5, 11.5, C_WHITE_S)
    prect(a, 4, 15, 38, 4, 0)
    pellipse(a, 23, 16, 16.5, 7.5, C_WHITE)
    pellipse(a, 23, 13, 13.0, 5.5, C_PURE)
    for gx, gy in ((15, 13), (21, 10), (26, 11), (31, 14), (23, 15), (18, 16)):
        pset(a, gx, gy, C_CERAMIC)
    prect(a, 5, 18, 36, 3, C_OCEAN2)
    prect(a, 6, 18, 34, 1, C_CYAN)
    prect(a, 6, 21, 34, 3, C_WHITE)
    prect(a, 8, 24, 30, 3, C_WHITE_S)
    prect(a, 11, 27, 24, 3, C_LACE_M)
    prect(a, 15, 30, 16, 3, C_LACE_S)
    prect(a, 21, 23, 5, 2, C_OCEAN2)
    pset(a, 26, 22, C_OCEAN2)
    pset(a, 22, 23, C_PURE)
    a = add_outline(a, C_OUT)
    if steam_phase >= 0:
        draw_steam_wisp(a, 13, 6, steam_phase)
        draw_steam_wisp(a, 23, 5, steam_phase + 2)
        draw_steam_wisp(a, 33, 6, steam_phase + 4)
    return a


def make_weight_scale(needle_step: int = 3) -> np.ndarray:
    a = np.zeros((22, 30), dtype=np.uint8)
    prect(a, 4, 4, 22, 14, C_MINT)
    prect(a, 3, 6, 24, 11, C_MINT)
    prect(a, 5, 5, 20, 2, C_GLOW)
    prect(a, 4, 15, 22, 3, C_TEAL2)
    prect(a, 6, 9, 4, 6, C_TEAL1)
    prect(a, 20, 9, 4, 6, C_TEAL1)
    prect(a, 7, 10, 2, 4, C_TEAL2)
    prect(a, 21, 10, 2, 4, C_TEAL2)
    pellipse(a, 15, 9, 4.6, 3.6, C_PURE)
    prect(a, 12, 6, 6, 1, C_LACE_S)
    pset(a, 12, 9, C_OUT); pset(a, 15, 7, C_OUT); pset(a, 18, 9, C_CRIMSON)
    tx, ty = [(12, 8), (15, 7), (17, 7), (19, 8)][max(0, min(3, needle_step))]
    pline(a, 15, 11, tx, ty, C_CRIMSON)
    pset(a, 15, 11, C_OUT)
    return add_outline(a, C_OUT)


def make_jellyfish(frame: int = 0) -> np.ndarray:
    a = np.zeros((26, 24), dtype=np.uint8)
    pellipse(a, 12, 8, 7.6, 5.8, C_MINT)
    pellipse(a, 12, 7, 6.2, 4.4, C_GLOW)
    pellipse(a, 10, 5.5, 2.8, 1.8, C_PURE)
    prect(a, 4, 11, 16, 3, 0)
    for x in range(5, 20):
        y = 11 + ((x + frame) % 2)
        pset(a, x, y, C_TEAL2)
        pset(a, x, y - 1, C_MINT)
    pset(a, 9, 8, C_OUT); pset(a, 15, 8, C_OUT); pset(a, 12, 9, C_TEAL1)
    sway = -1 if frame % 2 == 0 else 1
    for tx, length in ((7, 9), (10, 11), (13, 12), (16, 9)):
        for i in range(length):
            dx = int(round(math.sin(i * 0.75 + frame * 1.6) * 1.2)) + (sway if i > 5 else 0)
            col = C_GLOW if i < 4 else (C_MINT if i < 8 else C_TEAL2)
            pset(a, tx + dx, 12 + i, col)
    return add_outline(a, C_TEAL1)


def make_wave_pillow() -> np.ndarray:
    a = np.zeros((24, 34), dtype=np.uint8)
    pellipse(a, 17, 12, 13.5, 9.2, C_OCEAN2)
    pellipse(a, 17, 11, 11.5, 7.2, C_CERU)
    for cx, cy in ((4, 5), (29, 5), (4, 18), (29, 18)):
        prect(a, cx, cy, 2, 2, C_OCEAN1)
    pellipse(a, 13, 11, 6.2, 4.5, C_PURE)
    pellipse(a, 14, 13, 4.2, 3.2, C_CERU)
    pellipse(a, 22, 14, 5.0, 3.2, C_AQUA_HI)
    pellipse(a, 23, 15, 3.2, 2.2, C_OCEAN2)
    for fx, fy in ((9, 8), (11, 7), (14, 7), (17, 9), (20, 11), (25, 12)):
        pset(a, fx, fy, C_PURE)
    return add_outline(a, C_OUT)


def build_icons_16x16() -> np.ndarray:
    strip = np.zeros((16, 16 * 7), dtype=np.uint8)

    def place(idx: int, tile: np.ndarray) -> None:
        strip[:, idx * 16:(idx + 1) * 16] = tile

    t = np.zeros((16, 16), dtype=np.uint8)
    pellipse(t, 8, 7, 4.8, 3.0, C_WHITE); pellipse(t, 8, 6, 3.6, 2.0, C_PURE)
    prect(t, 3, 8, 10, 2, C_OCEAN2); prect(t, 4, 10, 8, 2, C_WHITE_S); prect(t, 5, 12, 6, 2, C_LACE_S)
    t = add_outline(t, C_OUT)
    pset(t, 6, 2, C_AQUA_HI); pset(t, 7, 1, C_FOAM); pset(t, 10, 2, C_AQUA_HI); pset(t, 9, 1, C_FOAM)
    place(0, t)

    t = np.zeros((16, 16), dtype=np.uint8)
    prect(t, 3, 3, 10, 10, C_MINT); prect(t, 4, 4, 8, 2, C_GLOW); prect(t, 5, 5, 6, 4, C_PURE)
    pline(t, 8, 8, 10, 6, C_CRIMSON); prect(t, 4, 10, 3, 2, C_TEAL1); prect(t, 9, 10, 3, 2, C_TEAL1)
    place(1, add_outline(t, C_OUT))

    t = np.zeros((16, 16), dtype=np.uint8)
    pellipse(t, 8, 10, 4.2, 4.0, C_CYAN); pellipse(t, 7, 9, 2.4, 2.2, C_AQUA_HI)
    pline(t, 8, 3, 6, 8, C_CYAN); pline(t, 8, 3, 10, 8, C_CYAN); pline(t, 8, 4, 8, 7, C_AQUA)
    pset(t, 6, 8, C_PURE); pset(t, 7, 7, C_PURE)
    t = add_outline(t, C_OUT)
    pset(t, 3, 5, C_AQUA_HI); pset(t, 13, 4, C_FOAM)
    place(2, t)

    t = np.zeros((16, 16), dtype=np.uint8)
    pellipse(t, 8, 6, 4.5, 3.5, C_MINT); pellipse(t, 8, 5, 3.2, 2.2, C_GLOW)
    pset(t, 7, 4, C_PURE); pset(t, 6, 6, C_OUT); pset(t, 10, 6, C_OUT)
    for tx in (5, 8, 11):
        pline(t, tx, 9, tx + (1 if tx == 8 else 0), 13, C_GLOW)
    place(3, add_outline(t, C_TEAL1))

    t = np.zeros((16, 16), dtype=np.uint8)
    pellipse(t, 8, 11, 3.2, 2.4, C_FIN2)
    pline(t, 8, 10, 3, 5, C_FIN1); pline(t, 8, 10, 4, 6, C_FIN2); pline(t, 7, 9, 4, 4, C_FIN3)
    pline(t, 8, 10, 13, 5, C_FIN1); pline(t, 8, 10, 12, 6, C_FIN2); pline(t, 9, 9, 12, 4, C_FIN3)
    prect(t, 6, 10, 4, 3, C_FIN2)
    t = add_outline(t, C_OUT); pset(t, 8, 3, C_AQUA_HI)
    place(4, t)

    t = np.zeros((16, 16), dtype=np.uint8)
    pellipse(t, 7, 10, 5.2, 3.8, C_OCEAN2); pellipse(t, 6, 9, 3.2, 2.2, C_PURE); pellipse(t, 7, 11, 2.2, 1.6, C_CERU)
    t = add_outline(t, C_OUT)
    prect(t, 11, 2, 4, 1, C_STAR); pset(t, 13, 3, C_STAR); pset(t, 12, 4, C_STAR); prect(t, 11, 5, 4, 1, C_STAR)
    place(5, t)

    t = np.zeros((16, 16), dtype=np.uint8)
    pellipse(t, 8, 8, 5.2, 5.2, C_OCEAN2); pellipse(t, 8, 8, 4.0, 4.0, C_CERU)
    draw_sparkle(t, 8, 8, 3, C_PURE, C_GOLD2, only_empty=False)
    place(6, add_outline(t, C_OUT))
    return strip


def save_props_pack() -> dict[str, str]:
    PROPS_DIR.mkdir(parents=True, exist_ok=True)
    rice, mega, scale = make_rice_bowl(0, True), make_mega_rice_basin(0), make_weight_scale(3)
    jf_strip = np.hstack([make_jellyfish(0), make_jellyfish(1)])
    pillow, icons = make_wave_pillow(), build_icons_16x16()
    items = {
        "rice-bowl.png": rice, "mega-rice-basin.png": mega, "weight-scale.png": scale,
        "jellyfish-2f.png": jf_strip, "wave-pillow.png": pillow, "icons-16x16.png": icons,
    }
    hashes: dict[str, str] = {}
    for fname, arr in items.items():
        p = PROPS_DIR / fname
        save_indexed(arr, p)
        hashes[f"props/{fname}"] = sha256_file(p)

    sheet = Image.new("RGBA", (620, 336), (22, 30, 50, 255))
    dr = ImageDraw.Draw(sheet)
    dr.rectangle([10, 10, 610, 326], outline=(82, 126, 178, 255), width=2)
    dr.text((22, 18), "DeepSeek Pixel Props & 16x16 UI Icons (3x Nearest - Ocean-Maid v2 Palette)", fill=(203, 247, 240, 255))
    for label, arr, x, y in [
        ("Rice Bowl 28x24", rice, 26, 56), ("Mega Rice Basin 46x36", mega, 160, 44),
        ("Weight Scale 30x22", scale, 368, 60), ("Wave Pillow 34x24", pillow, 26, 204),
        ("Jellyfish 2f (48x26)", jf_strip, 184, 198),
    ]:
        im = indexed_to_rgba(arr)
        z = im.resize((im.width * 3, im.height * 3), Image.NEAREST)
        dr.rounded_rectangle([x - 6, y - 6, x + z.width + 6, y + z.height + 22], radius=6, fill=(32, 46, 74, 255), outline=(68, 104, 148, 255))
        sheet.alpha_composite(z, (x, y))
        dr.text((x, y + z.height + 5), label, fill=(219, 226, 239, 255))

    ic_z = indexed_to_rgba(icons).resize((icons.shape[1] * 2, icons.shape[0] * 2), Image.NEAREST)
    ix, iy = 368, 210
    dr.rounded_rectangle([ix - 6, iy - 6, ix + ic_z.width + 6, iy + ic_z.height + 52], radius=6, fill=(32, 46, 74, 255), outline=(68, 104, 148, 255))
    sheet.alpha_composite(ic_z, (ix, iy))
    dr.text((ix, iy + ic_z.height + 8), "7x 16x16 UI Icons (2x)", fill=(219, 226, 239, 255))
    dr.text((ix, iy + ic_z.height + 24), "Rice|Scale|Drop|Jelly|Tail|Nap|Think", fill=(152, 220, 246, 255))
    sheet.save(PIXEL_DIR / "deepseek-props-sheet.png", optimize=True)
    return hashes


def pack_dual_atlas(full_frames: list[np.ndarray], close_frames: list[np.ndarray], cols: int = 8) -> np.ndarray:
    n = len(full_frames)
    rows_per = math.ceil(n / cols)
    sheet = np.zeros((rows_per * 2 * TILE_H, cols * TILE_W), dtype=np.uint8)
    for i, tile in enumerate(full_frames):
        r, c = divmod(i, cols)
        sheet[r * TILE_H:(r + 1) * TILE_H, c * TILE_W:(c + 1) * TILE_W] = tile
    for i, tile in enumerate(close_frames):
        r, c = divmod(i, cols)
        rr = rows_per + r
        sheet[rr * TILE_H:(rr + 1) * TILE_H, c * TILE_W:(c + 1) * TILE_W] = tile
    return sheet


def build_state_atlases(aligned: dict[str, Image.Image]) -> tuple[dict[str, np.ndarray], dict[str, np.ndarray], dict[str, str]]:
    full_tiles: dict[str, np.ndarray] = {}
    close_tiles: dict[str, np.ndarray] = {}
    for name in POSES:
        arr = np.asarray(aligned[name])
        full_tiles[name] = sample_tile_indexed(arr, FULL_CROP, crisp_eyes=False)
        close_tiles[name] = sample_tile_indexed(arr, CLOSE_CROP, crisp_eyes=False)

    def make_grid(tiles_map: dict[str, np.ndarray], cols: int = 4) -> np.ndarray:
        rows = math.ceil(len(POSES) / cols)
        out = np.zeros((rows * TILE_H, cols * TILE_W), dtype=np.uint8)
        for i, name in enumerate(POSES):
            r, c = divmod(i, cols)
            out[r * TILE_H:(r + 1) * TILE_H, c * TILE_W:(c + 1) * TILE_W] = tiles_map[name]
        return out

    hashes: dict[str, str] = {}
    for fname, arr in [
        ("deepseek-states-full.png", make_grid(full_tiles, 4)),
        ("deepseek-states-close.png", make_grid(close_tiles, 4)),
        ("deepseek-states-atlas.png", pack_dual_atlas([full_tiles[n] for n in POSES], [close_tiles[n] for n in POSES], 8)),
    ]:
        p = PIXEL_DIR / fname
        save_indexed(arr, p)
        hashes[fname] = sha256_file(p)
    return full_tiles, close_tiles, hashes


def build_clip_idle(aligned: dict[str, Image.Image]) -> tuple[list[np.ndarray], list[np.ndarray]]:
    """8-frame silky continuous breathing + curling ahoge wave + whale-tail wag (ZERO seams)."""
    base = aligned["idle"]
    full_f, close_f = [], []
    for f in range(8):
        ang = 2.0 * math.pi * f / 8.0
        breath = 0.015 * math.sin(ang)
        sway_x = 3.5 * math.sin(ang)
        ahoge_dx = 9.0 * math.sin(ang + 0.5)
        ahoge_dy = -5.0 * math.cos(ang)
        tail_dy = 16.0 * math.sin(ang + 0.35)
        hd = warp_hd_smooth(base, breath=breath, sway_x=sway_x, ahoge_dx=ahoge_dx, ahoge_dy=ahoge_dy, tail_dy=tail_dy)
        ft = sample_tile_indexed(hd, FULL_CROP)
        ct = sample_tile_indexed(hd, CLOSE_CROP)
        if f in (2, 3):
            draw_sparkle(ft, 114, 28, 1, C_PURE, C_AQUA_HI, only_empty=True)
            draw_sparkle(ct, 124, 24, 1, C_PURE, C_AQUA_HI, only_empty=True)
        elif f in (6, 7):
            draw_sparkle(ft, 20, 34, 1, C_PURE, C_AQUA_HI, only_empty=True)
            draw_sparkle(ct, 14, 30, 1, C_PURE, C_AQUA_HI, only_empty=True)
        full_f.append(ft)
        close_f.append(ct)
    return full_f, close_f


def build_clip_feed_rice(aligned: dict[str, Image.Image], sig: dict[str, Image.Image]) -> tuple[list[np.ndarray], list[np.ndarray]]:
    """12-frame Signature Interaction #1: Genuine chopsticks rice-munch -> GIANT blue-rimmed mega rice basin with starry eyes!"""
    full_f, close_f = [], []
    for f in range(12):
        if f == 0:
            hd = warp_hd_smooth(aligned["idle"], breath=-0.014, ahoge_dy=4.0, tail_dy=-8.0)
        elif f == 1:
            hd = warp_hd_smooth(aligned["idle"], dy=-6.0, breath=0.018, ahoge_dy=-6.0, tail_dy=12.0)
        elif f in (2, 3, 4, 5):
            # Genuine bespoke chopsticks + steaming rice bowl + puffed chewing cheeks!
            k = f - 2
            chew = -1.0 if k % 2 == 0 else 1.0
            hd = warp_hd_smooth(
                sig["feed_munch"],
                dy=-3.0 if k % 2 else 1.0,
                breath=0.016 * chew,
                sway_x=4.5 * chew,
                ahoge_dx=7.0 * chew,
                tail_dy=15.0 * chew,
            )
        elif f in (6, 7, 8, 9):
            # Genuine bespoke GIANT overflowing blue-rimmed mega rice basin + starry golden eyes!
            k = f - 6
            hop = [-10.0, -4.0, -8.0, -2.0][k]
            sw = [-5.0, 5.0, -4.0, 3.0][k]
            hd = warp_hd_smooth(
                sig["feed_mega_basin"],
                dy=hop,
                breath=0.018 if k in (0, 2) else -0.008,
                sway_x=sw,
                ahoge_dy=-7.0 if k in (0, 2) else 2.0,
                tail_dy=16.0 if k % 2 == 0 else -12.0,
            )
        elif f == 10:
            hd = warp_hd_smooth(aligned["happy"], dy=-6.0, breath=0.015, tail_dy=14.0)
        else:
            hd = warp_hd_smooth(aligned["idle"], breath=0.006, tail_dy=4.0)

        ft = sample_tile_indexed(hd, FULL_CROP)
        ct = sample_tile_indexed(hd, CLOSE_CROP)

        # Pure outer-ambient FX ONLY (only_empty=True -> NEVER touches eyes, mouth, or hair!)
        for is_close, tile in ((False, ft), (True, ct)):
            if f in (2, 3, 4, 5):
                draw_steam_wisp(tile, 20 if not is_close else 12, 42 if not is_close else 44, f, only_empty=True)
                draw_steam_wisp(tile, 118 if not is_close else 126, 48 if not is_close else 52, f + 2, only_empty=True)
            elif f in (6, 7, 8, 9):
                r1 = 2 if f % 2 == 0 else 1
                r2 = 1 if f % 2 == 0 else 2
                draw_sparkle(tile, 16 if not is_close else 12, 24 if not is_close else 22, r1, C_PURE, C_GOLD2, only_empty=True)
                draw_sparkle(tile, 120 if not is_close else 126, 20 if not is_close else 18, r2, C_PURE, C_STAR, only_empty=True)
                draw_sparkle(tile, 112 if not is_close else 120, 40 if not is_close else 36, r1, C_PURE, C_GOLD2, only_empty=True)
            elif f == 10:
                draw_sparkle(tile, 22, 26, 2, C_PURE, C_STAR, only_empty=True)
                draw_sparkle(tile, 116, 24, 2, C_PURE, C_GOLD2, only_empty=True)

        full_f.append(ft)
        close_f.append(ct)
    return full_f, close_f


def build_clip_sulk_weigh(aligned: dict[str, Image.Image], sig: dict[str, Image.Image]) -> tuple[list[np.ndarray], list[np.ndarray]]:
    """10-frame Signature Interaction #2: Genuine hands-covering-redline-scale crimson-blush '才不胖!!' sulk arc."""
    full_f, close_f = [], []
    for f in range(10):
        if f == 0:
            hd = warp_hd_smooth(aligned["idle"], breath=-0.008)
        elif f == 1:
            hd = warp_hd_smooth(aligned["thinking"], breath=-0.012, sway_x=-4.0, ahoge_dx=-6.0)
        elif f in (2, 3, 4, 5, 6, 7):
            # Genuine bespoke crimson-blushed hands-covering-scale pout + flying sweatdrops & anger mark!
            k = f - 2
            side = -1.0 if k % 2 == 0 else 1.0
            amp = 1.0 if k < 4 else 0.6
            hd = warp_hd_smooth(
                sig["sulk_weigh"],
                dx=5.0 * side * amp,
                dy=(-6.0 if k in (0, 2, 4) else 2.0) * amp,
                breath=(0.018 if k in (0, 2, 4) else -0.016) * amp,
                sway_x=8.5 * side * amp,
                ahoge_dx=11.0 * side,
                ahoge_dy=-7.0 if k % 2 == 0 else 3.0,
                tail_dy=18.0 * side,
            )
        elif f == 8:
            hd = warp_hd_smooth(aligned["thinking"], breath=-0.014, sway_x=4.0, ahoge_dy=4.0, tail_dy=-8.0)
        else:
            hd = warp_hd_smooth(aligned["thinking"], breath=-0.004, tail_dy=-3.0)

        ft = sample_tile_indexed(hd, FULL_CROP)
        ct = sample_tile_indexed(hd, CLOSE_CROP)

        # Tiny outer panic sparkles strictly in empty space
        for is_close, tile in ((False, ft), (True, ct)):
            if f in (2, 4, 6):
                draw_sparkle(tile, 18 if not is_close else 10, 26 if not is_close else 22, 1, C_PURE, C_AQUA_HI, only_empty=True)
                draw_sparkle(tile, 122 if not is_close else 128, 22 if not is_close else 18, 1, C_PURE, C_CRIMSON, only_empty=True)

        full_f.append(ft)
        close_f.append(ct)
    return full_f, close_f


def build_clip_ahoge_splash(aligned: dict[str, Image.Image], sig: dict[str, Image.Image]) -> tuple[list[np.ndarray], list[np.ndarray]]:
    """8-frame Signature Interaction #3A: Ahoge press -> genuine luminous cyan water-fountain spout!"""
    full_f, close_f = [], []
    for f in range(8):
        if f == 0:
            hd = warp_hd_smooth(aligned["idle"], dy=4.0, breath=-0.024, ahoge_dy=12.0, tail_dy=-8.0)
        elif f in (1, 2, 3, 4):
            # Genuine bespoke startled wide-eyed 'o' face + cyan water fountain bursting out of ahoge!
            k = f - 1
            spout_pulse = [-14.0, -20.0, -10.0, -4.0][k]
            body_hop = [-8.0, -5.0, -2.0, 0.0][k]
            hd = warp_hd_smooth(
                sig["ahoge_spout"],
                dy=body_hop,
                breath=[0.022, 0.014, 0.004, -0.006][k],
                sway_x=[-3.0, 3.0, -2.0, 1.0][k],
                spout_dy=spout_pulse,
                tail_dy=[14.0, -10.0, 10.0, -4.0][k],
            )
        elif f == 5:
            hd = warp_hd_smooth(aligned["happy"], dy=-6.0, breath=0.016, tail_dy=14.0)
        elif f == 6:
            hd = warp_hd_smooth(aligned["happy"], dy=-2.0, breath=0.006, tail_dy=-6.0)
        else:
            hd = warp_hd_smooth(aligned["idle"], breath=-0.005)

        ft = sample_tile_indexed(hd, FULL_CROP)
        ct = sample_tile_indexed(hd, CLOSE_CROP)

        for is_close, tile in ((False, ft), (True, ct)):
            if f in (1, 2, 3):
                draw_sparkle(tile, 36 if not is_close else 24, 10 + (f - 1) * 3, 1, C_PURE, C_AQUA_HI, only_empty=True)
                draw_sparkle(tile, 102 if not is_close else 114, 9 + (f - 1) * 3, 1, C_PURE, C_AQUA_HI, only_empty=True)
            elif f in (4, 5):
                draw_sparkle(tile, 26, 22, 1, C_PURE, C_FOAM, only_empty=True)
                draw_sparkle(tile, 112, 20, 1, C_PURE, C_FOAM, only_empty=True)

        full_f.append(ft)
        close_f.append(ct)
    return full_f, close_f


def build_clip_jellyfish_play(aligned: dict[str, Image.Image], sig: dict[str, Image.Image]) -> tuple[list[np.ndarray], list[np.ndarray]]:
    """8-frame Signature Interaction #3B: Genuine open-palm smile playing with glowing translucent cyan jellyfish!"""
    full_f, close_f = [], []
    for f in range(8):
        if f == 0:
            hd = warp_hd_smooth(aligned["idle"], breath=0.010, sway_x=-4.0, ahoge_dx=-6.0)
        elif f in (1, 2, 3, 4, 5):
            # Genuine bespoke glowing jellyfish companion + joyful raised hands!
            ang = 2.0 * math.pi * (f - 1) / 5.0
            hd = warp_hd_smooth(
                sig["jellyfish_play"],
                dy=-4.0 * abs(math.sin(ang)),
                breath=0.014 * math.sin(ang),
                sway_x=4.5 * math.sin(ang),
                ahoge_dx=7.0 * math.cos(ang),
                tail_dy=15.0 * math.sin(ang),
                left_prop_dy=-18.0 * math.sin(ang),
            )
        elif f == 6:
            hd = warp_hd_smooth(aligned["happy"], dy=-6.0, breath=0.016, tail_dy=12.0)
        else:
            hd = warp_hd_smooth(aligned["idle"], breath=0.004)

        ft = sample_tile_indexed(hd, FULL_CROP)
        ct = sample_tile_indexed(hd, CLOSE_CROP)

        for is_close, tile in ((False, ft), (True, ct)):
            if f in (1, 2, 3, 4, 5):
                draw_sparkle(tile, 12 if not is_close else 8, 22 + (f % 3) * 4, 1, C_PURE, C_GLOW, only_empty=True)
                draw_sparkle(tile, 118 if not is_close else 126, 26 + ((f + 1) % 3) * 3, 1, C_PURE, C_MINT, only_empty=True)

        full_f.append(ft)
        close_f.append(ct)
    return full_f, close_f


def build_clip_deepsleep_nap(sig: dict[str, Image.Image]) -> tuple[list[np.ndarray], list[np.ndarray]]:
    """8-frame Signature Interaction #3C (NEW): DeepSleep hugging the Japanese Great-Wave pillow + floating Zz bubble!"""
    base = sig["wave_pillow_nap"]
    full_f, close_f = [], []
    for f in range(8):
        ang = 2.0 * math.pi * f / 8.0
        # Dreamy slow cosine breathing + gentle floating Zz bubble in upper-left + soft tail curl
        hd = warp_hd_smooth(
            base,
            breath=0.018 * math.sin(ang),
            sway_x=2.0 * math.sin(ang),
            ahoge_dx=5.0 * math.sin(ang + 0.6),
            tail_dy=10.0 * math.sin(ang),
            left_prop_dy=-14.0 * math.sin(ang),
        )
        ft = sample_tile_indexed(hd, FULL_CROP)
        ct = sample_tile_indexed(hd, CLOSE_CROP)

        for is_close, tile in ((False, ft), (True, ct)):
            if f in (1, 2, 5, 6):
                draw_sparkle(tile, 16 if not is_close else 12, 18 if f < 4 else 14, 1, C_PURE, C_STAR, only_empty=True)
                draw_sparkle(tile, 118 if not is_close else 126, 22, 1, C_PURE, C_AQUA_HI, only_empty=True)

        full_f.append(ft)
        close_f.append(ct)
    return full_f, close_f


OCEAN_PHASES = {
    "dawn": {
        "sky": [(0, (54, 72, 118)), (48, (96, 126, 168)), (84, (176, 198, 220)), (108, (238, 214, 206))],
        "mist": (176, 232, 238), "sea_top": (122, 196, 216), "sea_bot": (78, 138, 174),
        "foam": (235, 250, 255), "sun": (144, 62, (255, 240, 204)), "stars": 14, "lighthouse": True, "bio": False,
        "label": "DAWN SEA-MIST",
    },
    "day": {
        "sky": [(0, (52, 112, 178)), (46, (88, 160, 216)), (82, (144, 212, 238)), (108, (206, 242, 248))],
        "mist": (196, 246, 250), "sea_top": (108, 198, 228), "sea_bot": (62, 138, 188),
        "foam": (244, 252, 255), "sun": (146, 48, (255, 246, 212)), "stars": 0, "lighthouse": True, "bio": False,
        "label": "DAY AZURE HARBOR",
    },
    "dusk": {
        "sky": [(0, (38, 44, 86)), (44, (74, 76, 128)), (78, (158, 118, 152)), (106, (232, 168, 162))],
        "mist": (152, 214, 226), "sea_top": (102, 164, 196), "sea_bot": (54, 94, 136),
        "foam": (246, 218, 216), "sun": (142, 82, (255, 208, 164)), "stars": 42, "lighthouse": True, "bio": False,
        "label": "DUSK CORAL LIGHTHOUSE",
    },
    "night": {
        "sky": [(0, (16, 24, 52)), (44, (24, 38, 78)), (80, (36, 68, 112)), (108, (58, 116, 148))],
        "mist": (118, 224, 224), "sea_top": (54, 136, 162), "sea_bot": (26, 62, 98),
        "foam": (176, 248, 242), "sun": None, "stars": 88, "lighthouse": True, "bio": True,
        "label": "NIGHT BIOLUMINESCENT",
    },
}


def render_ocean_plate(phase_name: str) -> Image.Image:
    th = OCEAN_PHASES[phase_name]
    im = Image.new("RGBA", (184, 184), (10, 14, 24, 255))
    dr = ImageDraw.Draw(im)
    stops = th["sky"]
    for i in range(1, len(stops)):
        y0, c0 = stops[i - 1]
        y1, c1 = stops[i]
        for y in range(y0, y1):
            t = ((y - y0) // 3 * 3) / max(1, (y1 - y0))
            col = tuple(int(round(c0[k] + (c1[k] - c0[k]) * t)) for k in range(3)) + (255,)
            dr.line([(0, y), (183, y)], fill=col)

    seed = 7331
    for _ in range(th["stars"]):
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        sx = 14 + (seed % 156)
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        sy = 14 + (seed % 74)
        im.putpixel((sx, sy), (235, 250, 255, 255) if (seed & 3) == 0 else (188, 218, 245, 220))

    if th["sun"]:
        sx, sy, scol = th["sun"]
        dr.ellipse([sx - 10, sy - 10, sx + 10, sy + 10], fill=scol + (90,))
        dr.ellipse([sx - 6, sy - 6, sx + 6, sy + 6], fill=scol + (255,))
    else:
        dr.ellipse([138, 36, 152, 50], fill=(245, 246, 220, 255))
        dr.ellipse([142, 34, 155, 48], fill=th["sky"][1][1] + (255,))

    cloud_col = (242, 250, 255, 210) if phase_name in ("day", "dawn") else (126, 166, 202, 140)
    for cx, cy, rw in ((34, 44, 18), (118, 52, 16), (62, 64, 14)):
        dr.rounded_rectangle([cx - rw, cy, cx + rw, cy + 5], radius=2, fill=cloud_col)
        dr.rounded_rectangle([cx - rw + 4, cy - 3, cx + rw - 5, cy + 2], radius=2, fill=cloud_col)

    if th["lighthouse"]:
        dr.rectangle([132, 96, 174, 108], fill=(64, 86, 122, 255))
        dr.rectangle([146, 78, 152, 96], fill=(238, 244, 250, 255))
        dr.rectangle([146, 82, 152, 85], fill=(82, 134, 188, 255))
        dr.rectangle([146, 89, 152, 92], fill=(82, 134, 188, 255))
        dr.rectangle([145, 74, 153, 78], fill=(255, 242, 182, 255))
        if phase_name in ("dusk", "night"):
            dr.polygon([(145, 75), (108, 66), (108, 84)], fill=(203, 247, 240, 52))

    mist = th["mist"]
    for y in range(88, 114):
        alpha = int(175 * (1.0 - abs(y - 103) / 16.0))
        if alpha > 0:
            dr.line([(0, y), (183, y)], fill=mist + (alpha,))

    st, sb = th["sea_top"], th["sea_bot"]
    for y in range(106, 184):
        t = ((y - 106) // 4 * 4) / 78.0
        col = tuple(int(round(st[k] + (sb[k] - st[k]) * t)) for k in range(3)) + (255,)
        dr.line([(0, y), (183, y)], fill=col)

    foam = th["foam"]
    for row_y, span in ((110, 42), (118, 56), (126, 48), (135, 62), (145, 52)):
        dr.line([(92 - span, row_y), (92 + span, row_y)], fill=foam + (150,))
        dr.line([(92 - span // 2, row_y + 1), (92 + span // 2, row_y + 1)], fill=mist + (110,))

    if th["bio"]:
        for bx, by in ((30, 128), (44, 142), (146, 132), (156, 118), (64, 148), (126, 146), (36, 98)):
            dr.ellipse([bx - 2, by - 2, bx + 2, by + 2], fill=(142, 224, 220, 110))
            im.putpixel((bx, by), (235, 250, 255, 255))
    elif phase_name == "day":
        for gx, gy in ((42, 34), (56, 42)):
            dr.line([(gx - 3, gy), (gx, gy + 2), (gx + 3, gy)], fill=(245, 252, 255, 235))
    return im


def compose_badge_preview(
    tile: np.ndarray, phase: str = "day", view: str = "full",
    icon_idx: int = 4, bubble_text: str = "", status_label: str = "IDLE",
) -> Image.Image:
    bg = render_ocean_plate(phase)
    sprite = indexed_to_rgba(tile)
    bg.alpha_composite(sprite, (23, 22 if view == "close" else 18))

    out = Image.new("RGBA", (184, 184), (14, 18, 30, 255))
    dr_out = ImageDraw.Draw(out)
    dr_out.ellipse([4, 4, 180, 180], fill=(24, 34, 54, 255), outline=(74, 118, 166, 255), width=2)
    mask = Image.new("L", (184, 184), 0)
    ImageDraw.Draw(mask).ellipse([10, 10, 174, 174], fill=255)

    dr_bg = ImageDraw.Draw(bg)
    dr_bg.rounded_rectangle([54, 13, 130, 24], radius=5, fill=(18, 28, 52, 185))
    dr_bg.text((61, 14), ">_ DEEPSEEK", fill=(203, 247, 240, 255))

    icons = build_icons_16x16()
    ic_im = indexed_to_rgba(icons[:, icon_idx * 16:(icon_idx + 1) * 16])
    mx, my = 15, 78
    dr_bg.rounded_rectangle([mx, my, mx + 19, my + 19], radius=5, fill=(24, 40, 68, 230), outline=(142, 224, 220, 255), width=1)
    bg.alpha_composite(ic_im, (mx + 2, my + 2))

    if bubble_text:
        bx, by, bw, bh = 95, 32, 46, 16
        dr_bg.ellipse([bx - 3, by + 2, bx + 13, by + bh], fill=(238, 251, 255, 246))
        dr_bg.ellipse([bx + 7, by - 4, bx + 27, by + bh - 1], fill=(238, 251, 255, 246))
        dr_bg.ellipse([bx + 22, by - 2, bx + 40, by + bh], fill=(238, 251, 255, 246))
        dr_bg.ellipse([bx + 32, by + 2, bx + bw + 2, by + bh], fill=(238, 251, 255, 246))
        dr_bg.rounded_rectangle([bx, by + 2, bx + bw, by + bh], radius=7, fill=(242, 252, 255, 248), outline=(114, 192, 234, 255))
        dr_bg.ellipse([bx + 5, by + bh + 1, bx + 9, by + bh + 5], fill=(238, 251, 255, 245), outline=(114, 192, 234, 255))
        dr_bg.ellipse([bx + 1, by + bh + 6, bx + 4, by + bh + 9], fill=(238, 251, 255, 245))
        dr_bg.text((bx + 5, by + 4), bubble_text[:7], fill=(34, 48, 86, 255))

    dr_bg.rounded_rectangle([52, 154, 132, 167], radius=6, fill=(18, 30, 54, 215), outline=(114, 192, 234, 220))
    lbl = status_label[:12]
    dr_bg.text((92 - (len(lbl) * 6) // 2, 156), lbl, fill=(235, 250, 255, 255))

    out.paste(bg, (0, 0), mask)
    dr_out.ellipse([10, 10, 174, 174], outline=(118, 186, 224, 190), width=1)
    return out


def save_preview_sheets(
    full_tiles: dict[str, np.ndarray],
    close_tiles: dict[str, np.ndarray],
    clips: dict[str, tuple[list[np.ndarray], list[np.ndarray]]],
) -> None:
    cards = [
        ("IDLE FULL (DAY)", full_tiles["idle"], "day", "full", 4, "READY!", "IDLE · DAY"),
        ("IDLE CLOSE (DAWN)", close_tiles["idle"], "dawn", "close", 4, "HI~", "CLOSE VIEW"),
        ("DEEPTHINK (NIGHT)", full_tiles["thinking"], "night", "full", 6, "THINK..", "DEEPTHINK"),
        ("FEED RICE · MUNCH", clips["feed-rice"][0][3], "day", "full", 0, "YUMMY!", "FEED MUNCH"),
        ("MEGA RICE BASIN!", clips["feed-rice"][0][7], "dawn", "full", 0, "SO GOOD", "MEGA BASIN"),
        ("WEIGH SULK · REDLINE", clips["sulk-weigh"][0][4], "dusk", "full", 1, "NO FAT!", "SULK WEIGH"),
        ("AHOGE WATER SPOUT", clips["ahoge-splash"][0][2], "day", "full", 2, "SPLASH!", "AHOGE POP"),
        ("JELLYFISH PLAY", clips["jellyfish-play"][0][3], "night", "full", 3, "GLOW~", "JELLYFISH"),
        ("DEEPSLEEP · PILLOW", clips["deepsleep-nap"][0][2], "night", "full", 5, "ZZZ~", "DEEPSLEEP"),
    ]
    cell, pad, cols, rows = 220, 16, 3, 3
    W_out = cols * cell + (cols + 1) * pad
    H_out = rows * (cell + 22) + (rows + 1) * pad + 32
    sheet = Image.new("RGBA", (W_out, H_out), (14, 19, 34, 255))
    dr = ImageDraw.Draw(sheet)
    dr.text((pad, 10), "DeepSeek Whale-Girl V1 Pixel Pack v2 - Bespoke 6 Signature Interactions Showcase", fill=(203, 247, 240, 255))
    for i, (title, tile, phase, view, ic, bub, st) in enumerate(cards):
        r, c = divmod(i, cols)
        x = pad + c * (cell + pad)
        y = 34 + pad + r * (cell + 22 + pad)
        badge = compose_badge_preview(tile, phase=phase, view=view, icon_idx=ic, bubble_text=bub, status_label=st)
        sheet.alpha_composite(badge.resize((cell, cell), Image.NEAREST), (x, y))
        dr.text((x + 6, y + cell + 4), title, fill=(195, 239, 252, 255))
    sheet.save(PIXEL_DIR / "preview-badge-showcase.png", optimize=True)

    cell4 = 220
    s4 = Image.new("RGBA", (4 * cell4 + 5 * pad, cell4 + 72), (14, 19, 34, 255))
    dr4 = ImageDraw.Draw(s4)
    dr4.text((pad, 10), "Tide Harbor (4-Phase Ocean Stage) - Ocean-Maid v2 Palette Contrast Verification", fill=(203, 247, 240, 255))
    for i, ph in enumerate(("dawn", "day", "dusk", "night")):
        x = pad + i * (cell4 + pad)
        b = compose_badge_preview(
            full_tiles["idle"], phase=ph, view="full", icon_idx=4,
            bubble_text="HI~" if ph in ("dawn", "day") else "GLOW~",
            status_label=f"TIDE · {ph.upper()}",
        ).resize((cell4, cell4), Image.NEAREST)
        s4.alpha_composite(b, (x, 34))
        dr4.text((x + 8, 34 + cell4 + 6), OCEAN_PHASES[ph]["label"], fill=(219, 226, 239, 255))
    s4.save(PIXEL_DIR / "preview-4-skies.png", optimize=True)


def save_normal_pack(
    aligned: dict[str, Image.Image],
    pose_meta: dict[str, dict],
    sig_hashes: dict[str, str],
) -> dict[str, str]:
    NORMAL_DIR.mkdir(parents=True, exist_ok=True)
    thumb, cols, rows = 240, 4, 3
    sheet = Image.new("RGBA", (cols * thumb + 50, rows * (thumb + 24) + 56), (22, 28, 46, 255))
    dr = ImageDraw.Draw(sheet)
    dr.text((16, 12), "DeepSeek Whale-Girl V2 Normal HD Pack - 12 Head-Center Aligned 680x680 Poses", fill=(203, 247, 240, 255))
    cx_g = int(round(TARGET_FACE_X / CANVAS_HD * thumb))
    fy_g = int(round(TARGET_FEET_Y / CANVAS_HD * thumb))
    for i, name in enumerate(POSES):
        r, c = divmod(i, cols)
        x, y = 10 + c * (thumb + 10), 40 + r * (thumb + 24)
        dr.rounded_rectangle([x, y, x + thumb, y + thumb], radius=8, fill=(32, 42, 68, 255), outline=(66, 98, 144, 255))
        dr.line([(x + cx_g, y + 6), (x + cx_g, y + thumb - 6)], fill=(114, 192, 234, 70))
        dr.line([(x + 8, y + fy_g), (x + thumb - 8, y + fy_g)], fill=(142, 224, 220, 90))
        sheet.alpha_composite(aligned[name].resize((thumb, thumb), Image.LANCZOS), (x, y))
        dr.text((x + 8, y + thumb + 4), f"{i + 1:02d}. {name}", fill=(219, 226, 239, 255))
    contact_path = NORMAL_DIR / "normal-12-poses-aligned.png"
    sheet.save(contact_path, optimize=True)

    sheet_hashes: dict[str, str] = {"normal-12-poses-aligned.png": sha256_file(contact_path)}
    for src_name, dst_name in HD_SHEETS_MAP.items():
        sp = ARTIFACT_DIR / src_name
        dp = NORMAL_DIR / dst_name
        if sp.exists() and not dp.exists():
            Image.open(sp).convert("RGBA").save(dp, optimize=True)
        if dp.exists():
            sheet_hashes[dst_name] = sha256_file(dp)

    rig_spec = {
        "character_id": "deepseek-whale-girl",
        "version": "normal-hd-v2",
        "license": "CC BY-NC-SA 4.0",
        "attribution": "原型「溟月」上善无形 · 女仆设定 ZipZipPipe · 12态基础立绘 CrossPet (commit 8954eb8f7f69fcfb8d35e9deceeb78e10c804402)",
        "canvas": {"width": CANVAS_HD, "height": CANVAS_HD, "target_face_x": TARGET_FACE_X, "target_feet_y": TARGET_FEET_Y},
        "z_order_layers": [
            "whale_tail_back", "back_hair_gradient", "back_waist_bow",
            "body_maid_dress", "apron_whale_emblem", "arms_hands",
            "head_face_base", "cheek_blush_layer", "eyes_expression_swap", "mouth_swap",
            "front_bangs_wave", "whale_head_fins_L_R", "side_blue_ribbon", "maid_lace_headdress", "curling_ahoge",
            "foreground_props_rice_scale_pillow", "fx_water_jellyfish_sparkles",
        ],
        "pivots_680": {
            "root_feet": [320, 656], "torso_chest": [320, 448], "head_neck": [320, 352],
            "face_center": [320, 284], "curling_ahoge_root": [316, 118],
            "left_head_fin_root": [202, 248], "right_head_fin_root": [438, 248], "whale_tail_root": [392, 515],
        },
        "signature_poses_680": sig_hashes,
        "concept_sheets": sheet_hashes,
        "poses": pose_meta,
    }
    (NORMAL_DIR / "rig-spec.json").write_text(json.dumps(rig_spec, ensure_ascii=False, indent=2), encoding="utf-8")
    return sheet_hashes


def count_horizontal_slits(arr: np.ndarray) -> int:
    """Detect any 1px horizontal transparent tear >= 7px wide sandwiched strictly between opaque rows above & below."""
    if arr.shape[0] < TILE_H or arr.shape[1] < TILE_W:
        return 0
    slits = 0
    rows = arr.shape[0] // TILE_H
    cols = arr.shape[1] // TILE_W
    for r in range(rows):
        for c in range(cols):
            tile = arr[r * TILE_H:(r + 1) * TILE_H, c * TILE_W:(c + 1) * TILE_W]
            if not np.any(tile):
                continue
            opaque = tile != 0
            # A pixel is a 1px horizontal slit if it is transparent while directly above AND below are opaque
            sandwiched = (~opaque[1:-1, :]) & opaque[:-2, :] & opaque[2:, :]
            for y in range(sandwiched.shape[0]):
                run = 0
                for v in sandwiched[y]:
                    if v:
                        run += 1
                        if run == 7:
                            slits += 1
                    else:
                        run = 0
    return slits


def verify_indexed_png(path: Path, check_slits: bool = False) -> tuple[bool, str]:
    im = Image.open(path)
    arr = np.asarray(im)
    mask = arr != 0
    pad = np.pad(mask, 1, mode="constant", constant_values=False)
    n4 = pad[:-2, 1:-1].astype(np.int8) + pad[2:, 1:-1] + pad[1:-1, :-2] + pad[1:-1, 2:]
    pinholes = int(np.sum((~mask) & (n4 == 4)))
    slits = count_horizontal_slits(arr) if check_slits else 0
    ok = (im.mode == "P") and (im.info.get("transparency") == 0) and (pinholes == 0) and (slits == 0)
    return ok, f"{path.name}: {im.width}x{im.height} mode={im.mode} transp={im.info.get('transparency')} pinholes={pinholes} slits={slits}"


def main() -> None:
    PIXEL_DIR.mkdir(parents=True, exist_ok=True)
    aligned, pose_meta = align_hd_poses()
    sig_poses, sig_hashes = extract_signature_poses(aligned["idle"])
    prop_hashes = save_props_pack()
    full_tiles, close_tiles, state_hashes = build_state_atlases(aligned)

    clip_builders = {
        "idle": (lambda: build_clip_idle(aligned), "deepseek-idle-anim.png", 6, True),
        "feed-rice": (lambda: build_clip_feed_rice(aligned, sig_poses), "deepseek-feed-rice.png", 7, False),
        "sulk-weigh": (lambda: build_clip_sulk_weigh(aligned, sig_poses), "deepseek-sulk-weigh.png", 7, False),
        "ahoge-splash": (lambda: build_clip_ahoge_splash(aligned, sig_poses), "deepseek-ahoge-splash.png", 8, False),
        "jellyfish-play": (lambda: build_clip_jellyfish_play(aligned, sig_poses), "deepseek-jellyfish-play.png", 7, False),
        "deepsleep-nap": (lambda: build_clip_deepsleep_nap(sig_poses), "deepseek-deepsleep-nap.png", 4, True),
    }

    clips: dict[str, tuple[list[np.ndarray], list[np.ndarray]]] = {}
    clip_manifest: dict[str, dict] = {}
    qa_atlases: list[Path] = [
        PIXEL_DIR / "deepseek-states-full.png",
        PIXEL_DIR / "deepseek-states-close.png",
        PIXEL_DIR / "deepseek-states-atlas.png",
    ]
    for name, (fn, fname, fps, loop) in clip_builders.items():
        full_f, close_f = fn()
        clips[name] = (full_f, close_f)
        atlas = pack_dual_atlas(full_f, close_f, cols=8)
        out_p = PIXEL_DIR / fname
        save_indexed(atlas, out_p)
        qa_atlases.append(out_p)
        clip_manifest[name] = {
            "file": fname, "frames": len(full_f), "cols": 8, "fps": fps, "loop": loop,
            "tile_size": [TILE_W, TILE_H], "views": ["full", "close"], "sha256": sha256_file(out_p),
        }

    save_preview_sheets(full_tiles, close_tiles, clips)
    normal_hashes = save_normal_pack(aligned, pose_meta, sig_hashes)

    manifest = {
        "character_id": "deepseek-whale-girl",
        "display_name": "DeepSeek 鲸鱼娘（深蓝女仆）",
        "version": "pixel-v2",
        "primary_for_dev": True,
        "license": "CC BY-NC-SA 4.0",
        "attribution": "原型「溟月」上善无形 · 女仆设定 ZipZipPipe · 基础12态立绘 CrossPet",
        "tile_size": [TILE_W, TILE_H],
        "badge_geometry": {"face_side": 184, "face_radius": 82, "origin_x": 23, "origin_y_full": 18, "origin_y_close": 22},
        "crops_on_680": {"full": list(FULL_CROP), "close": list(CLOSE_CROP)},
        "palette_40_v2": [f"#{c}" for c in PALETTE_HEX],
        "poses_order": POSES,
        "state_atlases": state_hashes,
        "clips": clip_manifest,
        "props_and_icons": prop_hashes,
        "icon_order_16x16": ["rice-bowl", "scale-sulk", "splash-drop", "jellyfish", "whale-tail", "wave-sleep", "star-think"],
        "stage_theme": "tide-harbor-4-phases",
        "normal_hd_backup_dir": "../../../normal/deepseek/v1",
        "normal_hd_signature_poses": sig_hashes,
        "normal_hd_sheets": normal_hashes,
    }
    (PIXEL_DIR / "deepseek-pixel-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    all_ok = True
    print("=== DeepSeek Dual-Version Asset Build v2 & Automated QA (Pinholes + Horizontal Slits) ===")
    for p in qa_atlases:
        ok, msg = verify_indexed_png(p, check_slits=True)
        print(f"  [{'OK' if ok else 'FAIL'}] {msg}")
        all_ok = all_ok and ok
    for f in ("rice-bowl.png", "mega-rice-basin.png", "weight-scale.png", "jellyfish-2f.png", "wave-pillow.png", "icons-16x16.png"):
        ok, msg = verify_indexed_png(PROPS_DIR / f, check_slits=False)
        print(f"  [{'OK' if ok else 'FAIL'}] {msg}")
        all_ok = all_ok and ok
    print(f"Version 2 Normal HD poses: {len(list(NORMAL_POSES_DIR.glob('*.png')))} base + {len(list(NORMAL_SIG_DIR.glob('*.png')))} bespoke signature poses")
    if not all_ok:
        raise SystemExit(1)
    print("ALL V2 QA CHECKS PASSED (ZERO PINHOLES, ZERO HORIZONTAL SLITS)!")


if __name__ == "__main__":
    main()
