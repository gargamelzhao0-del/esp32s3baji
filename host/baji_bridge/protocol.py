"""Bounded, CRC-protected USB line framing. Does not open serial ports.
JSON Schema validation belongs at the application boundary, after decoding.
"""
from __future__ import annotations
import json
import zlib

PREFIX = b"@BAJI1:"
MAX_FRAME_BYTES = 4096

def _reject_constant(value):
    raise ValueError(f"Non-JSON numeric constant: {value}")

def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result

def encode(message: dict) -> bytes:
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")
    frame = PREFIX + body + b"*" + f"{zlib.crc32(body):08x}".encode("ascii") + b"\n"
    if len(frame) > MAX_FRAME_BYTES:
        raise ValueError("Frame exceeds 4096 bytes")
    return frame

def decode(line: bytes) -> dict:
    if len(line) > MAX_FRAME_BYTES or not line.endswith(b"\n"):
        raise ValueError("Oversized or incomplete frame")
    if not line.startswith(PREFIX):
        raise ValueError("Not a badge frame")
    payload, crc = line[len(PREFIX):-1].rsplit(b"*", 1)
    if len(crc) != 8 or any(c not in b"0123456789abcdefABCDEF" for c in crc):
        raise ValueError("Invalid CRC encoding")
    if int(crc, 16) != zlib.crc32(payload):
        raise ValueError("CRC mismatch")
    value = json.loads(payload.decode("utf-8"), parse_constant=_reject_constant,
                       object_pairs_hook=_unique_object)
    if not isinstance(value, dict):
        raise ValueError("Expected object")
    return value

class LineDecoder:
    """Drops overlong lines until LF; unrelated ESP log lines are ignored."""
    def __init__(self):
        self.buffer = bytearray()
        self.discarding = False
        self.errors = 0

    def feed(self, chunk: bytes) -> list[dict]:
        messages = []
        for byte in chunk:
            if self.discarding:
                if byte == 10:
                    self.discarding = False
                continue
            self.buffer.append(byte)
            if len(self.buffer) > MAX_FRAME_BYTES:
                self.errors += 1
                self.buffer.clear()
                self.discarding = byte != 10
                continue
            if byte == 10:
                line = bytes(self.buffer)
                self.buffer.clear()
                if not line.startswith(PREFIX):
                    continue
                try:
                    messages.append(decode(line))
                except (ValueError, UnicodeError, RecursionError):
                    self.errors += 1
        return messages
