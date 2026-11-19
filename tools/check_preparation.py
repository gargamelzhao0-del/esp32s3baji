"""Check source assets, protocol fixtures and JSON contracts without hardware."""
import copy
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "host"))
from baji_bridge.protocol import encode, decode

try:
    from jsonschema import Draft202012Validator
except ImportError:
    sys.exit("jsonschema is required. Use the ESP-IDF Python environment or host/requirements-dev.txt.")

schema = json.loads((ROOT / "protocol/baji-v1.schema.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
count = 0
snapshot = None
for fixture in sorted((ROOT / "protocol/fixtures").glob("*.jsonl")):
    for n, line in enumerate(fixture.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        message = json.loads(line)
        errors = list(validator.iter_errors(message))
        if errors:
            sys.exit(f"{fixture.name}:{n}: {errors[0].message}")
        assert decode(encode(message)) == message
        if message["type"] == "snapshot" and message["session"]:
            snapshot = message
        count += 1
assert snapshot is not None
negative = []
bad = copy.deepcopy(snapshot); bad["metrics"]["context"]["used_pct"] = 101; negative.append(bad)
bad = copy.deepcopy(snapshot); bad["metrics"]["context"]["used_pct"] = None
bad["metrics"]["context"]["quality"] = "reported"; negative.append(bad)
bad = copy.deepcopy(snapshot); bad["seq"] = 0; negative.append(bad)
bad = copy.deepcopy(snapshot); bad["prompt"] = "must never cross wire"; negative.append(bad)
bad = copy.deepcopy(snapshot); bad["session"]["phase"] = "success"; negative.append(bad)
for bad in negative:
    assert not validator.is_valid(bad), "Negative fixture unexpectedly accepted"

manifest_path = ROOT / "assets/character-pack.source.json"
pack = json.loads(manifest_path.read_text(encoding="utf-8"))
source = (manifest_path.parent / pack["source_root"]).resolve()
catalog = json.loads((source / "catalog.json").read_text(encoding="utf-8"))
indexed = {item["path"]: item for item in catalog["files"]}
pose_count = 0
for character in pack["characters"].values():
    for pose, rel in character["poses"].items():
        file = (source / rel).resolve()
        assert file.is_relative_to(source), f"Escaped source root: {rel}"
        item = indexed[rel]
        assert hashlib.sha256(file.read_bytes()).hexdigest() == item["sha256"], rel
        assert item["frames"] == 1 and item["has_transparency"], rel
        pose_count += 1
assert pose_count == 48
assert not pack["firmware_ready"]
print(f"PASS: schema, {count} framed messages, {len(negative)} invalid cases, "
      f"{pose_count} static transparent poses with SHA-256.")
print("No live hooks, serial traffic, board initialization or quota account access was tested.")
