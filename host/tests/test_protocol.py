import json
from pathlib import Path
import sys
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from baji_bridge.protocol import LineDecoder, encode, decode, PREFIX, MAX_FRAME_BYTES

class ProtocolTest(unittest.TestCase):
    def test_unicode_fragmented_and_coalesced(self):
        message = {"v": 1, "label": "额度未知"}
        frame = encode(message)
        d = LineDecoder()
        self.assertEqual(d.feed(frame[:12]), [])
        self.assertEqual(d.feed(frame[12:] + frame), [message, message])

    def test_logs_corruption_and_recovery(self):
        frame = encode({"v": 1})
        d = LineDecoder()
        self.assertEqual(d.feed(b"I (100) baji: ready\n" + frame.replace(b'"v"', b'"x"')), [])
        self.assertEqual(d.errors, 1)
        self.assertEqual(d.feed(frame), [{"v": 1}])

    def test_oversized_input_resynchronizes(self):
        d = LineDecoder()
        self.assertEqual(d.feed(b"x" * (MAX_FRAME_BYTES + 300)), [])
        self.assertLessEqual(len(d.buffer), MAX_FRAME_BYTES)
        self.assertEqual(d.feed(b"ignored\n" + encode({"v": 1})), [{"v": 1}])
        self.assertEqual(d.errors, 1)

    def test_duplicate_key_rejected(self):
        body = b'{"v":1,"v":2}'
        frame = PREFIX + body + b"*" + f"{zlib.crc32(body):08x}".encode() + b"\n"
        with self.assertRaises(ValueError):
            decode(frame)

    def test_nonfinite_and_overlong_output_rejected(self):
        with self.assertRaises(ValueError):
            encode({"n": float("nan")})
        with self.assertRaises(ValueError):
            encode({"n": "x" * MAX_FRAME_BYTES})

if __name__ == "__main__":
    unittest.main()
