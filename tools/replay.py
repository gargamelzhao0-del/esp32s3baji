"""Emit a reviewed, synthetic JSONL fixture as framed USB bytes; never opens a COM port."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from baji_bridge.protocol import encode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("fixture", type=Path)
parser.add_argument("--output", type=Path, help="Optional binary output path; default stdout")
args = parser.parse_args()
messages = [json.loads(line) for line in args.fixture.read_text(encoding="utf-8").splitlines()
            if line.strip()]
frames = b"".join(encode(message) for message in messages)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(frames)
else:
    sys.stdout.buffer.write(frames)
