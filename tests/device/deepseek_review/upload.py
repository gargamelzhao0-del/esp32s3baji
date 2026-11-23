"""Send production artwork and browser reference CRCs to the DeepSeek RAM build."""
import argparse
import hashlib
from pathlib import Path
import struct
import time
import zlib

import serial

ROOT = Path(__file__).resolve().parents[3]
ART = ROOT / "components/baji_deepseek"
HD = ROOT / "components/baji_deepseek_hd"
RECORD = struct.Struct("<BBBBI")
HD_RECORD = struct.Struct("<BBBBhhI")
SHAPES = (8, 12, 10, 8, 8, 8) + (1,) * 12


def references(source, every_bubble):
    by_pose = [(item, frame) for item, count in enumerate(SHAPES) for frame in range(count)]
    samples = {}
    for line in source.read_text(encoding="utf-8").splitlines():
        line = line.partition("#")[0].strip()
        if not line:
            continue
        phase, view, pose, bubble, crc = line.split()
        key = tuple(map(int, (phase, view, pose, bubble)))
        if key in samples:
            raise ValueError(f"Duplicate reference {key}")
        samples[key] = int(crc, 16)
    counts = {key[3] for key in samples}
    if counts != set(range(7)):
        raise ValueError("Expected seven browser bubble positions")
    records = bytearray()
    for phase in range(4):
        for view in range(2):
            for pose, (item, frame) in enumerate(by_pose):
                steps = range(7) if every_bubble or (phase == view == frame == 0) else [(pose+phase+view) % 7]
                for bubble in steps:
                    crc = samples[phase, view, pose, bubble]
                    records.extend(RECORD.pack(item, frame, phase, view * 8 + bubble, crc))
    return records


def hd_references(poses, sheets):
    packed = bytearray()
    seen = set()
    def rows(path):
        for line in path.read_text(encoding="ascii").splitlines():
            text = line.partition("#")[0].strip()
            if text:
                yield text.split()

    for item, guides, breath, crc in rows(poses):
        key = tuple(map(int, (item, guides, breath)))
        if key in seen:
            raise ValueError(f"Duplicate HD pose {key}")
        seen.add(key)
        packed.extend(HD_RECORD.pack(0, *key, 0, 0, int(crc, 16)))
    if seen != {(i,g,b) for i in range(18) for g in range(2) for b in range(17)}:
        raise ValueError("Incomplete HD pose references")
    sheet_ids = set()
    for sheet, zoom, x, y, crc in rows(sheets):
        sheet_ids.add(int(sheet))
        packed.extend(HD_RECORD.pack(1, int(sheet), int(zoom), 0, int(x), int(y), int(crc, 16)))
    if sheet_ids != set(range(4)):
        raise ValueError("Incomplete HD sheet references")
    return packed


def write_chunks(port, data):
    for pos in range(0, len(data), 512):
        chunk = data[pos:pos+512]
        if port.write(chunk) != len(chunk):
            raise IOError("Short USB write")
        time.sleep(0.001)
    port.flush()

def motion_pages(path, goldens):
    """Read the production directory; stream one checked zlib member at a time."""
    data = path.read_bytes()
    magic, count, crc = struct.unpack_from("<4sII", data)
    if magic not in (b"HDA1", b"HDA2") or count != 272 or crc != zlib.crc32(memoryview(data)[12:]):
        raise ValueError("Invalid HD motion directory or payload CRC")
    samples = {}
    for row in goldens.read_text(encoding="ascii").splitlines():
        if row and not row.startswith("#"):
            entry, level, guide, crc = row.split()
            key = tuple(map(int, (entry, level, guide)))
            if key in samples:
                raise ValueError(f"Duplicate exact motion reference {key}")
            samples[key] = int(crc, 16)
    pages = {}
    cursor = 12 + 12 * count
    for i in range(count):
        entry, level = i // 16 + 1, i % 16 + 1
        offset, size, mode = struct.unpack_from("<III", data, 12 + i * 12)
        if offset != cursor or not size or size > len(data) - offset or mode > 1:
            raise ValueError("Invalid HD motion block")
        block = memoryview(data)[offset:offset+size]
        if magic == b"HDA2" and mode:
            raise ValueError("Invalid HDA2 wire-format record")
        pages[entry, level] = (2 if magic == b"HDA2" else mode, block, samples[entry,level,0], samples[entry,level,1])
        cursor += size
    if cursor != len(data):
        raise ValueError("Trailing data after the HD motion frames")
    return pages


def qualify(port, pack, records, hd_pack, hd_records, motion, native, log, timeout):
    count = len(records) // (HD_RECORD.size if native else RECORD.size)
    body_crc = zlib.crc32(pack)
    hd_crc = zlib.crc32(hd_pack)
    hd_count = len(hd_records) // HD_RECORD.size
    deadline = time.monotonic() + timeout
    state = "READY"
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", "replace").strip()
        if not line:
            continue
        print(line, flush=True)
        if log:
            print(line, file=log, flush=True)
        words = line.split()
        event = words[0]
        if event == "DSK_FAIL" or event == "DSK_LIVE_END":
            raise RuntimeError(line)
        fields = dict(word.split("=", 1) for word in words[1:] if "=" in word)
        if event == "DSK_READY" and state == "READY":
            if len(pack) + len(records) + len(hd_pack) + len(hd_records) > int(fields["max"]):
                raise ValueError("The board has insufficient PSRAM for the requested packs and fixtures")
            if native:
                magic = b"DSQM" if motion else b"DSQN"
            else:
                magic = b"DSQ3" if motion else b"DSQ2" if hd_pack else b"DSQ1"
            header = struct.pack("<4sIIII", magic, len(pack),
                                 body_crc, count, zlib.crc32(records))
            if hd_pack:
                header += struct.pack("<IIII", len(hd_pack), hd_crc, hd_count, zlib.crc32(hd_records))
            write_chunks(port, header)
            state = "RECEIVE"
        elif event == "DSK_RECEIVE" and state == "RECEIVE":
            if int(fields["bytes"]) != len(pack) or int(fields["cases"]) != count:
                raise ValueError("Device and host disagree on payload length")
            write_chunks(port, pack)
            write_chunks(port, records)
            state = "ASSET"
        elif event == "DSK_ASSET" and state == "ASSET":
            if int(fields["crc"], 16) != body_crc or int(fields["bytes"]) != len(pack):
                raise ValueError("RAM copy differs from the source artwork")
            state = "HD_RECEIVE" if hd_pack else "BOARD"
        elif event == "DSK_HD_RECEIVE" and state == "HD_RECEIVE":
            if int(fields["bytes"]) != len(hd_pack) or int(fields["cases"]) != hd_count:
                raise ValueError("Device and host disagree on HD payload length")
            write_chunks(port, hd_pack)
            write_chunks(port, hd_records)
            state = "HD_ASSET"
        elif event == "DSK_HD_ASSET" and state == "HD_ASSET":
            if int(fields["crc"], 16) != hd_crc or int(fields["bytes"]) != len(hd_pack):
                raise ValueError("RAM copy differs from the source HD pack")
            state = "BOARD"
        elif event == "DSK_BOARD" and state == "BOARD":
            if (fields["w"], fields["h"], fields["touch"]) != ("466", "466", "0"):
                raise ValueError(f"Unexpected board or touch probe {fields}")
            state = "HD_PIXELS" if native else "PIXELS"
        elif event == "DSK_PIXELS" and state == "PIXELS":
            if int(fields["cases"]) != count or int(fields["mismatch"]) != 0:
                raise ValueError(f"Board and reference pixels differ {fields}")
            state = "PANEL"
        elif event == "DSK_PANEL" and state == "PANEL":
            if int(fields["frames"]) != len(SHAPES) or not 0 < int(fields["min_us"]) <= int(fields["max_us"]):
                raise ValueError(f"Incomplete display test {fields}")
            state = "HD_PIXELS" if hd_pack else "LIVE"
        elif event == "DSK_HD_PIXELS" and state == "HD_PIXELS":
            if (int(fields["cases"]) != (count if native else hd_count) or int(fields["mismatch"])
                    or int(fields["err"]) or (native and int(fields["gallery_dma"]) != count)):
                raise ValueError(f"The HD render differs from the host {fields}")
            state = "MOTION" if motion else "NATIVE" if native else "SWITCH"
        elif event == "DSK_NATIVE_OK" and state == "NATIVE":
            return
        elif event == "DSK_PAGE" and state == "MOTION":
            entry, level = int(fields["entry"]), int(fields["level"])
            mode, data, c0, c1 = motion[entry,level]
            write_chunks(port, struct.pack("<IIII", mode, len(data), c0, c1) + data)
        elif event == "DSK_MOTION" and state == "MOTION":
            if (int(fields["pages"]) != 272 or int(fields["stages"]) != 544 or
                    int(fields["mismatch"]) or int(fields["err"])):
                raise ValueError(f"Exact motion validation failed {fields}")
            state = "BREATH" if native else "SWITCH"
        elif event == "DSK_BREATH" and state == "BREATH":
            if int(fields["cycles"]) != 3 or int(fields["steps"]) == 0 or int(fields["err"]):
                raise ValueError(f"TE live breathing trace did not complete {fields}")
            state = "NATIVE"
        elif event == "DSK_SWITCH" and state == "SWITCH":
            if fields != {"hd": "1", "pixel": "1"}:
                raise ValueError(f"Production mode transitions failed {fields}")
            state = "LIVE"
        elif event == "DSK_LIVE" and state == "LIVE":
            state = "RUNNING"
        elif event == "DSK_LIVE_OK" and state == "RUNNING":
            if not (int(fields["frames"]) > int(fields["partial"]) > 0
                    and int(fields["max_partial_us"]) > 0 and int(fields["span_us"]) >= 4_000_000):
                raise ValueError(f"Incomplete live partial updates {fields}")
            if "skipped_poses" in fields and (int(fields["skipped_poses"]) or not int(fields["pose_steps"])):
                raise ValueError(f"Missing or skipped poses during live playback {fields}")
            return
    raise TimeoutError(f"Timed out waiting for {state}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--asset", type=Path, default=ART / "assets/scene.dsk")
    parser.add_argument("--reference", type=Path, default=ART / "authoring/web.crc")
    parser.add_argument("--hd", type=Path, help="Also validate the HD stage and enter the dual-mode app")
    parser.add_argument("--native-gallery", type=Path,
                        help="Validate just the native-resolution production HDS and its 16 gallery views")
    parser.add_argument("--hd-poses", type=Path, default=HD / "tests/poses.crc")
    parser.add_argument("--hd-sheets", type=Path, default=HD / "tests/sheets.crc")
    parser.add_argument("--motion", type=Path, help="Stream the 272 exact HDA2 or legacy HDA1 frames")
    parser.add_argument("--motion-reference", type=Path, default=HD / "authoring/motion-web.crc")
    parser.add_argument("--every-bubble", action="store_true")
    parser.add_argument("--smoke", action="store_true",
                        help="Check one sample per pixel/HD item and sheet before interactive handoff")
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    if args.motion and not (args.hd or args.native_gallery):
        parser.error("--motion requires --hd or --native-gallery")
    if args.native_gallery and args.hd:
        parser.error("--native-gallery and --hd select different RAM qualifications")
    if args.native_gallery:
        pack = args.native_gallery.read_bytes()
        reference = args.hd_sheets if args.motion else HD / "tests/sheets-native.crc"
        all_hd = hd_references(args.hd_poses, reference)
        records = b"".join(HD_RECORD.pack(*r) for r in HD_RECORD.iter_unpack(all_hd) if r[0] == 1)
    else:
        pack = args.asset.read_bytes()
        reference = args.reference
        records = references(reference, args.every_bubble)
    hd_pack = args.hd.read_bytes() if args.hd else b""
    hd_records = hd_references(args.hd_poses, args.hd_sheets) if args.hd else b""
    if args.smoke:
        if args.every_bubble or args.native_gallery or args.motion:
            parser.error("--smoke is for a compact interactive handoff")
        pixel_cases = {}
        for row in RECORD.iter_unpack(records):
            pixel_cases.setdefault(row[0], RECORD.pack(*row))
        records = b"".join(pixel_cases.values())
        hd_cases = {}
        for row in HD_RECORD.iter_unpack(hd_records):
            hd_cases.setdefault(row[:2], HD_RECORD.pack(*row))
        hd_records = b"".join(hd_cases.values())
    motion = motion_pages(args.motion, args.motion_reference) if args.motion else None
    identity = (f"pack_sha256={hashlib.sha256(pack).hexdigest()} "
                f"reference_sha256={hashlib.sha256(reference.read_bytes()).hexdigest()}")
    if hd_pack:
        identity += (f" hd_sha256={hashlib.sha256(hd_pack).hexdigest()}"
                     f" hd_records_sha256={hashlib.sha256(hd_records).hexdigest()}")
    if motion:
        identity += (f" motion_sha256={hashlib.sha256(args.motion.read_bytes()).hexdigest()}"
                     f" motion_reference_sha256={hashlib.sha256(args.motion_reference.read_bytes()).hexdigest()}")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=6)
    port.dtr = port.rts = False
    port.port = args.port
    with port:
        if args.log:
            with args.log.open("w", encoding="utf-8") as log:
                print(identity, file=log, flush=True)
                qualify(port, pack, records, hd_pack, hd_records, motion, bool(args.native_gallery), log, args.timeout)
        else:
            print(identity)
            qualify(port, pack, records, hd_pack, hd_records, motion, bool(args.native_gallery), None, args.timeout)


if __name__ == "__main__":
    main()
