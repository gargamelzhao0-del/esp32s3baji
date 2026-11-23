"""Build two eight-frame RGB565 workloads from the checked-out badge atlas PNGs."""
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

from PIL import Image

W, H, N = 138, 132, 8
# At 2x scale 180 source pixels cost about 72 us of 40 MHz quad wire time.
# This is a planning estimate, the device measures the plans independently.
WINDOW_COST = 180
CLIPS = [("heartHug", 14, 0), ("sneeze", 12, 4)]


def bounds(points):
    xs, ys = zip(*points)
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def union(a, b):
    return min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])


def cost(box):
    return (box[2] - box[0]) * (box[3] - box[1]) + WINDOW_COST


def plan_axis(changed, axis):
    buckets = {}
    for point in changed:
        buckets.setdefault(point[axis] // 4, []).append(point)
    bands = [bounds(buckets[key]) for key in sorted(buckets)]
    costs, previous = [0], []
    for end, box in enumerate(bands, start=1):
        best, cut = float("inf"), 0
        for start in range(end - 1, -1, -1):
            box = union(box, bands[start])
            candidate = costs[start] + cost(box)
            if candidate < best:
                best, cut = candidate, start
        costs.append(best)
        previous.append(cut)
    result, end = [], len(bands)
    while end:
        start = previous[end-1]
        box = bands[start]
        for band in bands[start+1:end]:
            box = union(box, band)
        result.insert(0, box)
        end = start
    return result


def source_frames(path, start, row_offset):
    frames = []
    with Image.open(path) as atlas:
        image = atlas.convert("RGBA")
    if image.width != 8*W or image.height < ((start+N-1)//8 + row_offset + 1)*H:
        raise ValueError(f"Unexpected atlas geometry for {path}")
    for f in range(start, start + N):
        x, y = f % 8 * W, (row_offset + f // 8) * H
        tile = image.crop((x, y, x + W, y + H))
        rgb = Image.alpha_composite(Image.new("RGBA", (W, H), (34, 33, 56, 255)), tile)
        frames.append([(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
                       for r, g, b, _ in struct.iter_unpack("4B", rgb.tobytes())])
    return frames


def list_c(values):
    return "{" + ",".join(str(value) for value in values) + "}"


def display_rect(box):
    x0, y0, x1, y1 = box
    return list_c((94 + 2*x0, 100 + 2*y0, 2*(x1-x0), 2*(y1-y0)))


def generate(assets, output):
    declarations, clips = [], []
    for name, start, row_offset in CLIPS:
        path = assets / (name + ".png")
        print(name, start, "atlas SHA256", hashlib.sha256(path.read_bytes()).hexdigest())
        frames = source_frames(path, start, row_offset)
        raw = b"".join(struct.pack(f"<{W*H}H", *pixels) for pixels in frames)
        colors = sorted({pixel for f in frames for pixel in f})
        if len(colors) > 256:
            raise ValueError("The diagnostic fixture uses one-byte color indices")
        index = {value: i for i, value in enumerate(colors)}
        compressed = zlib.compress(bytes(index[value] for f in frames for value in f), level=9)
        lines = [",".join(map(str, compressed[i:i+32])) for i in range(0, len(compressed), 32)]
        declarations.append(f"static const uint8_t {name}_data[] = {{\n" + ",\n".join(lines) + "\n};")
        declarations.append(f"static const uint16_t {name}_colors[] = {list_c(colors)};")
        rects, steps = [], []
        for i, pixels in enumerate(frames):
            changed = [(j % W, j // W) for j, pair in enumerate(zip(pixels, frames[i-1]))
                       if pair[0] != pair[1]]
            if not changed:
                steps.append("{.count=0}")
                continue
            plans = [plan_axis(changed, axis) for axis in (0, 1)]
            chosen = min(plans, key=lambda boxes: sum(map(cost, boxes)))
            mirror = frames[i-1].copy()
            for x0, y0, x1, y1 in chosen:
                for y in range(y0, y1):
                    sl = slice(y*W+x0, y*W+x1)
                    mirror[sl] = pixels[sl]
            assert mirror == pixels
            steps.append(f"{{.box={display_rect(bounds(changed))}, .first={len(rects)}, .count={len(chosen)}}}")
            rects.extend(map(display_rect, chosen))
        declarations.append(f"static const baji_rect_t {name}_rects[] = {list_c(rects)};")
        declarations.append(f"static const step_t {name}_steps[] = {list_c(steps)};")
        clips.append(f'{{"{name}", {name}_data, sizeof({name}_data), {zlib.crc32(raw)}u, '
                     f'{name}_colors, {len(colors)}, {name}_rects, {name}_steps}}')
    declarations.append("static const clip_t clips[] = " + list_c(clips) + ";")
    output.write_text("\n".join(declarations) + "\n", encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.assets, args.output)
