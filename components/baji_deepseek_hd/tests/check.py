"""Check the actual ESP renderer in a host process, with the ROM inflate and CRC calls shimmed."""
import argparse
import ctypes as ct
import importlib.util
from pathlib import Path
import os
import random
import subprocess
import tempfile
import zlib

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent
PET_TEST = COMPONENT.parent / "baji_pet/tests"
spec = importlib.util.spec_from_file_location("pet_host", PET_TEST / "run.py")
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class View(ct.Structure):
    _fields_ = [(k, ct.c_uint8) for k in ("item", "sheet", "zoom", "breath")] + [
        ("gallery", ct.c_bool), ("guides", ct.c_bool), ("pan_x", ct.c_int8), ("pan_y", ct.c_int8)]


def build(folder):
    for name in ["esp_heap_caps.h", "esp_rom_crc.h", "miniz.h"]:
        (folder / name).write_text('#include "host.h"\n')
    (folder / "esp_heap_caps.h").write_text('#include "host.h"\n'
        'void *limited_alloc(size_t, unsigned);\n#define heap_caps_malloc limited_alloc\n')
    (folder / "esp_partition.h").write_text('#include "partition_host.h"\n')
    shim = folder / "shim.c"
    shim.write_text("""
#include "host.h"
#include "baji_deepseek_hd.h"
inflate_fn inflate_host;
checksum_fn checksum_host;
static int deny_stage;
void deny_cache(int deny) {deny_stage = deny;}
void *limited_alloc(size_t size, unsigned flags) {
    (void)flags;
    if (deny_stage && size >= 300000 && size <= 340000) return NULL;
    return malloc(size);
}
void hooks(inflate_fn inf, checksum_fn crc) {inflate_host=inf; checksum_host=crc;}
size_t canvas_size(void) {return sizeof(dsh_canvas_t);}
size_t art_size(void) {return sizeof(dsh_art_t);}
size_t motion_size(void) {return sizeof(dsh_motion_t);}
""")
    target = folder / ("hd.dll" if os.name == "nt" else "hd.so")
    subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fPIC", "-shared", "-DESP_PLATFORM", "-DDSH_HOST_TEST", "-I", folder, "-I", HERE,
                    "-I", PET_TEST, "-I", COMPONENT / "include", "-I", COMPONENT.parent / "baji_graphics/include",
                    COMPONENT / "deepseek_hd.c", COMPONENT / "hd_playback.c", COMPONENT / "hd_compact.c",
                    HERE / "partition_host.c", shim, "-o", target], check=True)
    lib = ct.CDLL(str(target))
    lib.hooks.argtypes = [host.inflate_fn, host.checksum_fn]
    lib.hooks(host.inflate, host.crc)
    lib.canvas_size.restype = ct.c_size_t
    lib.art_size.restype = ct.c_size_t
    lib.motion_size.restype = ct.c_size_t
    for fn, args in [(lib.dsh_art_open, [ct.c_void_p, ct.c_void_p, ct.c_size_t]),
                     (lib.dsh_canvas_init, [ct.c_void_p, ct.c_void_p]),
                     (lib.dsh_canvas_select, [ct.c_void_p, View]),
                     (lib.dsh_canvas_select_blend, [ct.c_void_p, View, ct.c_uint]),
                     (lib.dsh_select_compact, [ct.c_void_p, View, ct.c_uint]),
                     (lib.dsh_motion_open, [ct.c_void_p, ct.c_void_p, ct.c_size_t]),
                     (lib.dsh_motion_open_frame, [ct.c_void_p, ct.c_uint, ct.c_uint, ct.c_uint,
                                                  ct.c_void_p, ct.c_size_t])]:
        fn.argtypes, fn.restype = args, ct.c_bool
    lib.dsh_paint.argtypes = [ct.POINTER(host.Rect), ct.c_void_p, ct.c_void_p]
    lib.dsh_canvas_motion_damage.argtypes = [ct.c_void_p, ct.c_uint, ct.POINTER(host.Rect)]
    lib.dsh_canvas_motion_damage.restype = ct.c_uint
    lib.dsh_canvas_close.argtypes = [ct.c_void_p]
    lib.dsh_canvas_set_motion.argtypes = [ct.c_void_p, ct.c_void_p]
    lib.dsh_partition_motion_open.argtypes = [ct.c_void_p]
    lib.dsh_partition_motion_open.restype = ct.c_bool
    lib.dsh_partition_motion_close.argtypes = [ct.c_void_p]
    lib.test_mount.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_size_t]
    lib.test_fail_read_at.argtypes = [ct.c_size_t]
    return lib


def run(lib, update, ram):
    raw = (COMPONENT / "assets" / ("scene-ram.hds" if ram else "scene.hds")).read_bytes()
    art = ct.create_string_buffer(lib.art_size())
    assert not lib.dsh_art_open(ct.byref(art), raw, len(raw) - 1)
    assert lib.dsh_art_open(ct.byref(art), raw, len(raw))
    canvas = ct.create_string_buffer(lib.canvas_size())
    assert lib.dsh_canvas_init(canvas, ct.byref(art))
    rng = random.Random(14)
    full = host.Rect(0, 0, 466, 466)

    def paint(rect):
        data = ct.create_string_buffer(rect.width * rect.height * 2)
        lib.dsh_paint(ct.byref(rect), data, canvas)
        return data.raw

    def render(view):
        assert lib.dsh_canvas_select(canvas, view)
        image = paint(full)
        r = host.Rect(rng.randrange(425), rng.randrange(421), rng.randrange(1, 39), rng.randrange(1, 43))
        crop = b"".join(image[2 * (y * 466 + r.x):2 * (y * 466 + r.x + r.width)]
                        for y in range(r.y, r.y + r.height))
        assert paint(r) == crop
        return image

    def persist(name, records):
        path = HERE / name
        value = "".join(records)
        if update:
            path.write_text(value, encoding="ascii", newline="\n")
        else:
            assert path.read_text(encoding="ascii") == value, f"golden mismatch: {path}"

    try:
        poses, sheets = [], []
        web = {}
        for line in (COMPONENT / "authoring/web.crc").read_text().splitlines():
            entry, time, guides, crc = line.split()
            web[int(entry), int(time), int(guides)] = int(crc, 16)
        for item in range(18):
            entry = item + 1 if item < 6 else 1 if item == 6 else item
            for guides in (0, 1):
                for breath in range(17):
                    view = View(item=item, breath=breath, guides=guides)
                    img = render(view)
                    if breath == 0:
                        stage = paint(host.Rect(63, 63, 340, 340))
                        assert zlib.crc32(stage) == web[entry, 0, guides], (item, guides)
                    poses.append(f"{item} {guides} {breath} {zlib.crc32(img):08x}\n")
        for sheet in range(4):
            for zoom, px, py in ((0, 0, 0), (1, 0, 0), (1, -100, -100), (1, 100, 100)):
                image = render(View(sheet=sheet, gallery=True, zoom=zoom, pan_x=px, pan_y=py))
                sheets.append(f"{sheet} {zoom} {px} {py} {zlib.crc32(image):08x}\n")
        assert not lib.dsh_canvas_select(canvas, View(item=18))
        persist("poses.crc", poses)
        persist("sheets.crc" if ram else "sheets-native.crc", sheets)
        if ram:
            baseline = ct.create_string_buffer(lib.canvas_size())
            assert lib.dsh_canvas_init(baseline, art)
            stage = host.Rect(63, 63, 340, 340)
            try:
                for item in range(18):
                    for guide in (False, True):
                        for phase in (*range(0, 129, 8), 3, 39, 75, 121):
                            v = View(item=item, breath=phase//8, guides=guide)
                            assert lib.dsh_select_compact(baseline, v, phase%8)
                            assert lib.dsh_canvas_select_blend(canvas, v, phase%8)
                            expected = ct.create_string_buffer(340*340*2)
                            lib.dsh_paint(ct.byref(stage), expected, baseline)
                            assert paint(stage) == expected.raw, (item, guide, phase)
            finally:
                lib.dsh_canvas_close(baseline)
            print("Compact row routing matches the full per-pixel CSS transform for 756 stage selections")
            view = View(item=1)
            assert lib.dsh_canvas_select(canvas, view)
            resting = paint(stage)
            lib.deny_cache(1)
            try:
                assert lib.dsh_canvas_select_blend(canvas, view, 3)
                assert paint(stage) != resting
                assert lib.dsh_canvas_select_blend(canvas, view, 0)
                assert paint(stage) == resting
                view.breath = 7
                assert lib.dsh_canvas_select_blend(canvas, view, 0)
                compact = paint(stage)
                assert lib.dsh_canvas_select(canvas, view)
                assert paint(stage) == compact
            finally:
                lib.deny_cache(0)
        print("36 resting stages match browser RGB565, 612 breathing/guide frames and 16 gallery views checked")
        if not ram:
            check_exact(lib, canvas, render, paint)
    finally:
        lib.dsh_canvas_close(canvas)


def check_partition(lib, packed, golden, canvas, paint):
    import struct
    motion = ct.create_string_buffer(lib.motion_size())
    n = len(packed)
    padded_size = n + 0x10000
    lib.test_mount(None, 0, padded_size)
    assert not lib.dsh_partition_motion_open(motion)
    lib.test_mount(packed, n, n - 1)
    assert not lib.dsh_partition_motion_open(motion)
    corrupted = packed[:-1] + bytes([packed[-1] ^ 1])
    lib.test_mount(corrupted, n, padded_size)
    assert not lib.dsh_partition_motion_open(motion)
    lib.test_mount(packed, n, padded_size)
    for address in (0, 16384, n - 1):
        lib.test_fail_read_at(address)
        assert not lib.dsh_partition_motion_open(motion)
    lib.test_fail_read_at(ct.c_size_t(-1).value)
    assert lib.dsh_partition_motion_open(motion)
    assert not lib.dsh_partition_motion_open(motion)
    lib.dsh_canvas_set_motion(canvas, motion)
    stage = host.Rect(63, 63, 340, 340)
    for entry, level in ((1, 1), (10, 6), (17, 16)):
        offset = struct.unpack_from("<I", packed, 12 + ((entry - 1)*16 + level - 1)*12)[0]
        item = entry - 1 if entry < 7 else entry
        view = View(item=item, breath=level, guides=True)
        lib.test_fail_read_at(offset)
        assert not lib.dsh_canvas_select(canvas, view)
        lib.test_fail_read_at(ct.c_size_t(-1).value)
        assert lib.dsh_canvas_select(canvas, view)
        assert zlib.crc32(paint(stage)) == golden[entry, level, 1]
    lib.dsh_canvas_set_motion(canvas, None)
    lib.dsh_partition_motion_close(motion)
    lib.dsh_partition_motion_close(motion)
    assert lib.dsh_partition_motion_open(motion)
    lib.dsh_partition_motion_close(motion)
    print("Partition extent, chunked CRC, three random flash reads, failed-read retry and release passed")


def check_exact(lib, canvas, render, paint):
    import struct
    packed = (COMPONENT / "assets/motion.hda").read_bytes()
    golden = {}
    for line in (COMPONENT / "authoring/motion-web.crc").read_text().splitlines():
        entry, level, guides, crc = line.split()
        golden[int(entry), int(level), int(guides)] = int(crc, 16)
    assert len(golden) == 17 * 17 * 2
    check_partition(lib, packed, golden, canvas, paint)
    motion = ct.create_string_buffer(lib.motion_size())
    assert not lib.dsh_motion_open(motion, packed, len(packed) - 1)
    assert lib.dsh_motion_open(motion, packed, len(packed))
    lib.dsh_canvas_set_motion(canvas, motion)
    stage = host.Rect(63, 63, 340, 340)
    for item in range(18):
        entry = item + 1 if item < 6 else 1 if item == 6 else item
        for level in range(17):
            for guides in (0, 1):
                key = entry, level, guides
                image = render(View(item=item, breath=level, guides=guides))
                assert zlib.crc32(paint(stage)) == golden[key], key
                assert image[:466 * 2 * 63] == bytes(466 * 2 * 63)
    # Motion anchors are browser captures, including the separately overlaid guide pixels.
    # Test the interpolated painter through an odd subrectangle crossing both guides.
    patch = host.Rect(63 + 151, 63 + 315, 27, 18)
    for item in range(18):
        entry = item + 1 if item < 6 else 1 if item == 6 else item
        for g in (False, True):
            for level in range(17):
                view = View(item=item, breath=level, guides=g)
                assert lib.dsh_canvas_select_blend(canvas, view, 0)
                assert zlib.crc32(paint(stage)) == golden[entry, level, int(g)]
            for level in (0, 3, 12, 15):
                v = View(item=item, breath=level, guides=g)
                assert lib.dsh_canvas_select_blend(canvas, v, 0)
                left = [p[0] for p in struct.iter_unpack(">H", paint(patch))]
                assert lib.dsh_canvas_select_blend(canvas, View(item=item, breath=level+1, guides=g), 0)
                right = [p[0] for p in struct.iter_unpack(">H", paint(patch))]
                mix = 1 + (level + item) % 7
                assert lib.dsh_canvas_select_blend(canvas, v, mix)
                def channel(a, b, shift, mask):
                    return ((((a >> shift & mask)*(8-mix) + (b >> shift & mask)*mix + 4)//8) << shift)
                expected = b"".join(struct.pack(">H", channel(a,b,11,31) | channel(a,b,5,63) | channel(a,b,0,31))
                                    for a,b in zip(left,right))
                assert paint(patch) == expected
            # Apply just the marked bands to a retained display. A jump may
            # cross several anchor pairs or reverse at the end of a breath.
            phases = (0, 5, 8, 11, 78, 126, 128, 113, 76, 0)
            retained = None
            for idx, phase in enumerate(phases):
                view = View(item=item, breath=phase//8, guides=g)
                assert lib.dsh_canvas_select_blend(canvas, view, phase%8)
                latest = paint(stage)
                if retained is None:
                    retained = bytearray(latest)
                    continue
                boxes = (host.Rect * 10)()
                count = lib.dsh_canvas_motion_damage(canvas, phases[idx-1], boxes)
                assert count <= len(boxes)
                for box in boxes[:count]:
                    for y in range(box.y-63, box.y-63+box.height):
                        start = 2*(y*340+box.x-63)
                        end = start + 2*box.width
                        retained[start:end] = latest[start:end]
                assert retained == latest, (item, g, phase)
    # A qualifier may supply a single random-access page. Exercise XOR mode
    # with a real browser frame even when the shipping pack fits direct frames.
    entry, item, level = 2, 1, 9
    offset, size, mode = struct.unpack_from("<3I", packed, 12 + ((entry - 1)*16 + level - 1)*12)
    page = packed[offset:offset + size]
    base_pack = (COMPONENT / "assets/scene.hds").read_bytes()
    _, _, _, base_offset, base_size = struct.unpack_from("<HHIII", base_pack, 12 + entry*16)
    base = zlib.decompress(base_pack[base_offset:base_offset+base_size])[:340*340*2]
    frame = zlib.decompress(page)
    assert packed[:4] == b"HDA2" and mode == 0
    # Convert the screen-order fixture to little-endian HDA1 words.
    native = bytearray(len(frame))
    native[0::2], native[1::2] = frame[1::2], frame[0::2]
    direct = zlib.compress(native)
    delta = zlib.compress(bytes(a ^ b for a, b in zip(base, native)) + native[len(base):])
    assert not lib.dsh_motion_open_frame(motion, entry, level, 3, page, len(page))
    for encoded, encoding in ((page, 2), (direct, 0), (delta, 1)):
        assert lib.dsh_motion_open_frame(motion, entry, level, encoding, encoded, len(encoded))
        lib.dsh_canvas_set_motion(canvas, motion)
        for guides in (0, 1):
            render(View(item=item, breath=level, guides=guides))
            assert zlib.crc32(paint(stage)) == golden[entry, level, guides]
    # Old complete packs can also mix direct and XOR pages. Validate the
    # bytes, offsets, selected pixel output, and reject that old mode in HDA2.
    target = (entry - 1) * 16 + level - 1
    directory, payload = bytearray(), bytearray()
    empty = zlib.compress(bytes(len(native)))
    for i in range(272):
        record, encoding = (delta, 1) if i == target else (empty, 0)
        directory.extend(struct.pack("<III", 12 + 272*12 + len(payload), len(record), encoding))
        payload.extend(record)
    body = directory + payload
    metadata = struct.pack("<II", 272, zlib.crc32(body))
    legacy = b"HDA1" + metadata + body
    invalid = b"HDA2" + metadata + body
    assert not lib.dsh_motion_open(motion, invalid, len(invalid))
    assert lib.dsh_motion_open(motion, legacy, len(legacy))
    lib.dsh_canvas_set_motion(canvas, motion)
    render(View(item=item, breath=level, guides=True))
    assert zlib.crc32(paint(stage)) == golden[entry, level, 1]
    lib.dsh_canvas_set_motion(canvas, None)
    print("All 578 independent Chromium HD pose/breath/guide references matched; HDA2 wire, HDA1 direct/XOR and 18-button linkage passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", action="store_true", help="write panel CRCs for the on-device qualifier")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        library = build(Path(tmp))
        try:
            run(library, args.record, ram=True)
            run(library, args.record, ram=False)
        finally:
            if os.name == "nt":
                import _ctypes
                _ctypes.FreeLibrary(library._handle)
