"""Bake individually seekable, bit-exact Chrome frames of the HD breathing animation."""
import argparse
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import struct
import threading
import zlib

import numpy as np

from bake import COMPONENT, WEB, GUIDE_X, GUIDE_Y, SIDE, Pack, QuietServer, inspect_stage


def phase_times():
    # The 1400 ms half-cycle uses CSS ease-in-out (0.42,0,0.58,1).
    # Select actual Chrome animation times for equal transform displacements.
    for level in range(1, 17):
        low = 0.0
        high = 1.0
        for _ in range(48):
            t = (low + high) / 2
            if 3 * (1 - t) * t * t + t ** 3 < level / 16:
                low = t
            else:
                high = t
        t = (low + high) / 2
        yield (3 * .42 * (1-t) ** 2 * t + 3 * .58 * (1-t) * t ** 2 + t ** 3) * 1400


class Motion:
    times = staticmethod(phase_times)

    def __init__(self, still, still_goldens, policy, scratch):
        self.still = still
        self.still_goldens = {}
        for line in still_goldens.splitlines():
            entry, ms, guides, crc = line.split()
            if ms == "0":
                self.still_goldens[int(entry), int(guides)] = crc
        self.policy = policy
        self.scratch = scratch
        self.directory = bytearray()
        self.payload = bytearray()
        self.crcs = []
        self.base = None

    def add(self, entry, level, pixels, guides):
        if level == 0:
            _, _, _, pos, size = struct.unpack_from("<HHIII", self.still, 12 + entry*16)
            raw = zlib.decompress(self.still[pos:pos+size])
            self.base = np.frombuffer(raw, dtype="<u2", count=SIDE*SIDE).reshape(SIDE, SIDE)
            # Phase zero is provided by the still package. Use the original
            # Chrome captures that produced that package, including guides.
            with_guides = self.base.copy()
            strips = np.frombuffer(raw, dtype="<u2", offset=3*SIDE*SIDE)
            with_guides[:, GUIDE_X] = strips[:SIDE]
            with_guides[GUIDE_Y] = strips[SIDE:]
            for guide, image in enumerate((self.base, with_guides)):
                crc = self.still_goldens[entry, guide]
                if f"{zlib.crc32(image.astype('>u2').tobytes()):08x}" != crc:
                    raise ValueError("Still art does not match its browser goldens")
                self.crcs.append(f"{entry} 0 {guide} {crc}\n")
            return

        for guide, image in enumerate((pixels, guides)):
            crc = zlib.crc32(image.astype(">u2").tobytes())
            self.crcs.append(f"{entry} {level} {guide} {crc:08x}\n")
        delta = pixels != guides
        delta[:, GUIDE_X] = False
        delta[GUIDE_Y] = False
        if delta.any():
            raise ValueError(f"Reference lines exceed recorded strips ({entry},{level})")
        # The HDA2 direct frame and both strip arrays retain panel wire order,
        # so visible scanline runs can be copied directly to a DMA buffer.
        dtype = ">u2" if self.policy == "wire" else "<u2"
        strips = guides[:, GUIDE_X].astype(dtype).tobytes() + guides[GUIDE_Y].astype(dtype).tobytes()
        raw = pixels.astype(dtype).tobytes() + strips
        full = zlib.compress(raw, 9)
        block = full
        mode = 0
        if self.policy == "small":
            base_xor = zlib.compress((self.base ^ pixels).astype("<u2").tobytes() + strips, 9)
            if len(base_xor) < len(full):
                block = base_xor
                mode = 1
        self.directory.extend(struct.pack("<III", 12 + 272*12 + len(self.payload), len(block), mode))
        self.payload.extend(block)
        if self.scratch:
            (self.scratch / f"pose{entry:02d}-step{level:02d}.bin.z").write_bytes(full)

    def save(self, output, goldens):
        if len(self.directory) != 272 * 12:
            raise ValueError("Incomplete set of breathing frames")
        payload = self.directory + self.payload
        magic = b"HDA2" if self.policy == "wire" else b"HDA1"
        output.write_bytes(struct.pack("<4sII", magic, 272, zlib.crc32(payload)) + payload)
        goldens.write_text("".join(self.crcs), encoding="ascii", newline="\n")
        print(f"{magic.decode()} {self.policy}: {output.stat().st_size:,} bytes", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web", type=Path, default=WEB)
    parser.add_argument("--browser", default=shutil.which("chromium") or shutil.which("google-chrome")
                        or (os.path.expandvars(r"%ProgramFiles%\Google\Chrome\Application\chrome.exe")
                            if os.name == "nt" else None))
    parser.add_argument("--still", type=Path, default=COMPONENT / "assets/scene.hds")
    parser.add_argument("--still-goldens", type=Path, default=Path(__file__).with_name("web.crc"))
    parser.add_argument("--output", type=Path, default=COMPONENT / "assets/motion.hda")
    parser.add_argument("--goldens", type=Path, default=Path(__file__).with_name("motion-web.crc"))
    parser.add_argument("--policy", choices=("wire", "full", "small"), default="wire",
                        help="wire uses HDA2 display order; full and small produce legacy HDA1")
    parser.add_argument("--scratch", type=Path, help="also write lossless Chrome frames outside the package")
    args = parser.parse_args()
    if args.scratch:
        args.scratch.mkdir(parents=True, exist_ok=True)
    motion = Motion(args.still.read_bytes(), args.still_goldens.read_text(encoding="ascii"),
                    args.policy, args.scratch)
    manifest = json.loads((args.web / "pixel/deepseek/v1/deepseek-pixel-manifest.json").read_text(encoding="utf8"))
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietServer, directory=str(args.web))) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/pixel/deepseek/v1/review.html"
            inspect_stage(url, args.browser, manifest, (), Pack(), None, motion=motion)
            motion.save(args.output, args.goldens)
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
