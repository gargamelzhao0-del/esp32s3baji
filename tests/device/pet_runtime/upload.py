"""Feed the RAM qualifier its production asset without resetting the board."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import time
import zlib

import serial

DEFAULT_ASSET = Path(__file__).resolve().parents[3] / "components/baji_pet/assets/scene.bpz"
STAGES = {"idle": 180, "drag": 180, "pat": 240}


def verify_stage(fields):
    name = fields["name"]
    ticks = int(fields["ticks"])
    if ticks != STAGES.get(name) or int(fields["sent"]) + int(fields["noop"]) != ticks:
        raise ValueError(f"Unexpected stage counters for {name}")
    if not (0 < int(fields["sent"]) < ticks) or int(fields["unchanged_sent"]):
        raise ValueError(f"Unchanged frames were redrawn or expected draws are missing in {name}")
    if int(fields["pose_changes"]) <= 0:
        raise ValueError(f"No pose changes in {name}")
    if (int(fields["pat_start"]), int(fields["pat_end"])) != ((1, 1) if name == "pat" else (0, 0)):
        raise ValueError(f"Unexpected pat events in {name}")
    if ((name == "idle" and int(fields["gaze_changes"])) or
            (name == "drag" and not int(fields["gaze_only_sent"]))):
        raise ValueError(f"Unexpected gaze activity in {name}")
    return name


def upload(connection, asset, log, timeout):
    crc = zlib.crc32(asset)
    header_sent = body_sent = verified = full = first = passed = False
    stages, timing_errors, scene_errors = [], [], []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = connection.readline()
        if not raw:
            continue
        if log:
            log.write(raw)
            log.flush()
        line = raw.decode("utf-8", errors="replace").strip()
        print(line, flush=True)
        tokens = line.split()
        if not tokens or not tokens[0].startswith("PET_"):
            continue
        message = tokens[0]
        fields = dict(token.split("=", 1) for token in tokens[1:] if "=" in token)
        if message == "PET_READY" and not header_sent:
            if not (0 < len(asset) <= int(fields["max"])):
                raise ValueError("Asset does not fit the board's advertised PSRAM buffer")
            header = struct.pack("<4sII", b"PET1", len(asset), crc)
            if connection.write(header) != len(header):
                raise IOError("Short asset header write")
            header_sent = True
        elif message == "PET_RECEIVE" and not body_sent:
            if not header_sent or int(fields["bytes"]) != len(asset):
                raise ValueError("Asset length negotiation failed")
            for pos in range(0, len(asset), 512):
                chunk = asset[pos:pos+512]
                if connection.write(chunk) != len(chunk):
                    raise IOError("Short asset payload write")
                time.sleep(0.001)
            connection.flush()
            body_sent = True
        elif message == "PET_ASSET":
            verified = (body_sent and fields["ok"] == "1" and int(fields["bytes"]) == len(asset)
                        and int(fields["crc32"], 16) == crc)
            if not verified:
                raise ValueError("The board did not decode the exact uploaded pack")
        elif message == "PET_FULL":
            full = int(fields["n"]) == 12 and 0 < int(fields["p50"]) <= int(fields["max"])
        elif message == "PET_FIRST":
            first = int(fields["sent"]) == 1 and int(fields["us"]) > 0
        elif message == "PET_STAGE":
            name = fields["name"]
            stages.append(name)
            try:
                verify_stage(fields)
            except (ValueError, KeyError) as error:
                scene_errors.append(str(error))
            if int(fields["missed"]) or int(fields["late"]):
                timing_errors.append(name)
        elif message == "PET_END":
            passed = verified and full and first and stages == list(STAGES) and not timing_errors and not scene_errors
            if not passed or fields != {"ok": "1", "functional": "1", "deadlines": "1", "err": "0"}:
                raise ValueError(f"Qualification failed {fields}, stages {stages}, "
                                 f"timing failures {timing_errors}, scene errors {scene_errors}")
        elif message == "PET_LIVE":
            if not passed:
                raise ValueError("Live handoff before qualification passed")
            return
    raise TimeoutError("Timed out waiting for the RAM benchmark and live handoff")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--asset", type=Path, default=DEFAULT_ASSET)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--timeout", type=float, default=75)
    args = parser.parse_args()
    asset = args.asset.read_bytes()
    print(f"{args.asset} {len(asset)} bytes SHA256 {hashlib.sha256(asset).hexdigest()}", flush=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.3, write_timeout=5)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    with connection:
        if args.log:
            with args.log.open("wb") as log:
                upload(connection, asset, log, args.timeout)
        else:
            upload(connection, asset, None, args.timeout)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
