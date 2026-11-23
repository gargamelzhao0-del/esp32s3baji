"""Use two independent transport endpoint handles into one firmware state service."""
import ctypes as ct
from concurrent.futures import ThreadPoolExecutor
import hmac
from pathlib import Path
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "host"))
from baji_bridge.device import DeviceState, signed_snapshot

PTR = ct.c_void_p
U64, SIZE = ct.c_uint64, ct.c_size_t
HELLO = ct.CFUNCTYPE(None, PTR, PTR)
RECEIVE = ct.CFUNCTYPE(ct.c_int, PTR, PTR, SIZE)
HMAC = ct.CFUNCTYPE(ct.c_int, PTR, SIZE, PTR, SIZE, PTR)
KEY, STREAM = bytes(range(32)), b"stream-1"
OK, BAD_STATE, BAD_SIZE, BAD_AUTH = 0, 0x103, 0x104, 0x109
CONNECTING, ONLINE, OFFLINE = range(3)


class Endpoint(ct.Structure):
    _fields_ = [("ctx", PTR), ("hello", HELLO), ("receive", RECEIVE)]

    def challenge(self):
        buf = ct.create_string_buffer(16)
        self.hello(self.ctx, buf)
        return buf.raw

    def send(self, data):
        return self.receive(self.ctx, data, len(data))


class View(ct.Structure):
    _fields_ = [("link", ct.c_int), ("phase", ct.c_int), ("pose", ct.c_char_p),
                ("animate", ct.c_bool), ("quota_known", ct.c_bool), ("context_known", ct.c_bool),
                ("quota_estimated", ct.c_bool), ("context_estimated", ct.c_bool),
                ("quota", ct.c_uint16), ("context", ct.c_uint16), ("outcome", ct.c_int)]


class Display(ct.Structure):
    _fields_ = [("view", View), ("model", ct.c_char * 33), ("selected", ct.c_bool)]


READ = ct.CFUNCTYPE(ct.c_bool, PTR, U64, ct.POINTER(Display))


class Feed(ct.Structure):
    _fields_ = [("ctx", PTR), ("read", READ), ("changed", PTR)]


def load_dll(path):
    dll = ct.CDLL(path)
    for name, args, result in (
        ("test_set_hmac", [HMAC], None), ("test_set_ms", [U64], None),
        ("baji_agent_sync_open", [ct.POINTER(PTR), PTR], ct.c_int),
        ("baji_agent_sync_close", [PTR], None), ("baji_agent_endpoint", [PTR], Endpoint),
        ("baji_agent_feed", [PTR], Feed), ("xSemaphoreTake", [PTR, ct.c_uint32], ct.c_int),
    ):
        fn = getattr(dll, name)
        fn.argtypes, fn.restype = args, result
    return dll


class Wireless(unittest.TestCase):
    def setUp(self):
        self.now = 1000
        self.barrier = None

        @HMAC
        def verify_hmac(key, key_size, body, size, out):
            try:
                digest = hmac.digest(ct.string_at(key, key_size), ct.string_at(body, size), "sha256")
                ct.memmove(out, digest, len(digest))
                if self.barrier:
                    # Both transports authenticate before either can acquire the C state lock.
                    self.barrier.wait(timeout=5)
                return 0
            except threading.BrokenBarrierError:
                return -1

        self.hash_callback = verify_hmac  # The DLL borrows this callback until close.
        lib.test_set_hmac(verify_hmac)
        lib.test_set_ms(self.now)
        self.sync = PTR()
        self.assertEqual(lib.baji_agent_sync_open(ct.byref(self.sync), KEY), OK)
        self.addCleanup(lib.baji_agent_sync_close, self.sync)
        self.wifi = lib.baji_agent_endpoint(self.sync)
        self.ble = lib.baji_agent_endpoint(self.sync)
        self.feed = lib.baji_agent_feed(self.sync)
        self.host = DeviceState(clock=lambda: self.now / 1000)
        self.nonce = self.wifi.challenge()
        self.assertEqual(self.nonce, self.ble.challenge())

    def at(self, ms):
        self.assertGreaterEqual(ms, self.now)
        self.now = ms
        lib.test_set_ms(ms)

    def event(self, session="one", model="gpt-test", phase="working", **values):
        self.assertTrue(self.host.accept(dict(type="agent.state", session=session, model=model,
                                              phase=phase, **values)))

    def packet(self, seq, nonce=None, stream=STREAM, key=KEY):
        return signed_snapshot(key, self.nonce if nonce is None else nonce, stream, seq, self.host.sample())

    def read(self, available=True):
        display = Display()
        self.assertEqual(self.feed.read(self.feed.ctx, self.now, ct.byref(display)), available)
        return display

    def changed(self):
        return bool(lib.xSemaphoreTake(self.feed.changed, 0))

    def race(self, wifi, ble):
        self.barrier = threading.Barrier(2)
        try:
            with ThreadPoolExecutor(max_workers=2) as workers:
                a = workers.submit(self.wifi.send, wifi)
                b = workers.submit(self.ble.send, ble)
                return a.result(timeout=10), b.result(timeout=10)
        finally:
            self.barrier = None

    def test_authentication_and_rejected_input_preserve_last_view(self):
        self.assertEqual(self.read(False).view.link, CONNECTING)
        self.event(activity="writing")
        packet = self.packet(1)
        self.assertEqual(self.ble.send(packet[:-1] + bytes([packet[-1] ^ 1])), BAD_AUTH)
        self.assertEqual(self.ble.send(self.packet(1, key=bytes(32))), BAD_AUTH)
        self.assertEqual(self.wifi.send(b"x"), BAD_SIZE)
        self.assertEqual(self.wifi.send(self.packet(1, nonce=bytes(16))), BAD_STATE)
        self.assertFalse(self.changed())
        self.assertEqual(self.read(False).view.link, CONNECTING)
        self.assertEqual(self.ble.send(packet), OK)
        self.assertTrue(self.changed())
        before = self.read()
        self.assertEqual((before.model, before.view.pose), (b"gpt-test", b"writing"))
        self.event(model="wrong", phase="error")
        body = bytearray(self.packet(2)[:-32])
        body[36] = 255  # Authenticated, out-of-schema phase.
        self.assertEqual(self.ble.send(bytes(body) + hmac.digest(KEY, body, "sha256")), 0x102)
        self.assertEqual(self.wifi.send(self.packet(2, stream=b"stream-2")), BAD_STATE)
        self.assertFalse(self.changed())
        after = self.read()
        self.assertEqual((after.model, after.view.pose), (before.model, before.view.pose))

    def test_utf8_label_survives_and_invalid_encoding_cannot_replace_it(self):
        self.event(model="本地鲸娘" * 3)
        self.assertEqual(self.wifi.send(self.packet(1)), OK)
        expected = ("本地鲸娘" * 3)[:10].encode("utf8")
        self.assertEqual(self.read().model, expected)
        for invalid in (b"\x00", b"\xc0\x80", b"\x80", b"\xed\xa0\x80", b"\xf0\x9f", b"\xf4\x90\x80\x80"):
            body = self.packet(2)[:52] + bytes([len(invalid)]) + invalid
            self.assertEqual(self.ble.send(body + hmac.digest(KEY, body, "sha256")), 0x102)
        self.assertEqual(self.read().model, expected)

    def test_unknown_zero_and_remaining_metric_ttl(self):
        self.event(activity="searching", quota=dict(used_pct=0, ttl_ms=120, age_ms=20))
        self.assertEqual(self.wifi.send(self.packet(1)), OK)
        v = self.read().view
        self.assertEqual((v.quota_known, v.quota, v.context_known, v.pose),
                         (True, 0, False, b"searching"))
        self.assertFalse(v.quota_estimated)
        self.at(1099)
        self.assertTrue(self.read().view.quota_known)
        self.at(1100)
        self.assertFalse(self.read().view.quota_known)
        self.event(context=dict(used_pct=73.5, ttl_ms=2200, age_ms=200, quality="estimated"))
        # Wait 500 ms on the sender before it signs and delivers the new state.
        self.at(1600)
        self.assertEqual(self.ble.send(self.packet(2)), OK)
        v = self.read().view
        self.assertEqual((v.quota_known, v.context_known, v.context), (False, True, 735))
        self.assertTrue(v.context_estimated)
        self.at(3099)
        self.assertTrue(self.read().view.context_known)
        self.at(3100)
        self.assertFalse(self.read().view.context_estimated)
        self.assertFalse(self.read().view.context_known)
        self.assertEqual(self.read().view.link, ONLINE)

    def test_competing_transports_replay_expiry_and_rebind(self):
        self.event(activity="running")
        self.assertCountEqual(self.race(self.packet(10), self.packet(10)), [OK, BAD_STATE])
        self.assertTrue(self.changed())
        self.assertFalse(self.changed())
        self.at(8999)
        self.assertEqual(self.ble.send(self.packet(9)), BAD_STATE)
        self.assertEqual(self.wifi.send(self.packet(10)), BAD_STATE)
        self.assertEqual(self.read().view.link, ONLINE)
        self.assertFalse(self.changed())
        # A fresh sequence signed under the expired challenge cannot renew the lease.
        self.at(9000)
        self.assertEqual(self.ble.send(self.packet(11)), BAD_STATE)
        stale = self.read()
        self.assertEqual((stale.view.link, stale.view.animate), (OFFLINE, False))
        fresh = self.ble.challenge()
        self.assertNotEqual(self.nonce, fresh)
        self.assertEqual(self.wifi.challenge(), fresh)

        self.event(session="next", model="gpt-wifi")
        wifi = self.packet(1, nonce=fresh, stream=b"stream-W")
        self.event(session="another", model="gpt-ble")
        ble = self.packet(1, nonce=fresh, stream=b"stream-B")
        outcomes = self.race(wifi, ble)
        self.assertCountEqual(outcomes, [OK, BAD_STATE])
        winner = 0 if outcomes[0] == OK else 1
        self.assertEqual(self.read().model, (b"gpt-wifi", b"gpt-ble")[winner])
        self.assertEqual(self.read().view.link, ONLINE)
        loser_stream = (b"stream-B", b"stream-W")[winner]
        self.assertEqual(self.wifi.send(self.packet(2, nonce=fresh, stream=loser_stream)), BAD_STATE)
        self.assertEqual(self.wifi.send(self.packet(100)), BAD_STATE)

    def test_new_session_and_no_selection(self):
        self.event(phase="ended", outcome="succeeded")
        self.assertEqual(self.wifi.send(self.packet(1)), OK)
        self.assertEqual(self.read().view.pose, b"happy")
        self.at(2600)
        self.event(phase="ended", outcome="succeeded")
        self.assertEqual(self.ble.send(self.packet(2)), OK)
        self.assertEqual(self.read().view.pose, b"idle")
        self.event(session="two", phase="ended", outcome="succeeded")
        self.assertEqual(self.wifi.send(self.packet(3)), OK)
        self.assertEqual(self.read().view.pose, b"happy")
        self.event(session=None)
        self.assertEqual(self.ble.send(self.packet(4)), OK)
        state = self.read()
        self.assertEqual((state.selected, state.model, state.view.pose, state.view.link),
                         (False, b"", b"idle", ONLINE))


if __name__ == "__main__":
    lib = load_dll(sys.argv.pop())
    unittest.main(verbosity=2)
