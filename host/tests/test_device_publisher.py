"""Drive the public CLI over loopback and exercise EOF, retries and USB setup."""
import asyncio
from contextlib import asynccontextmanager
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
from pathlib import Path
from queue import Queue
import struct
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "host"))
from baji_bridge import pairing
from baji_bridge.device import DeviceState
from baji_bridge.publisher import MAX_LINE, publish, stream_events
from baji_bridge.transports import UsbTransport, WifiResponseError

KEY = bytes(range(32))
NONCE = bytes(range(16))
UNKNOWN = 0xffff


def event(phase="working", **fields):
    return {"type": "agent.state", "session": "private-session", "model": "gpt-test",
            "phase": phase, **fields}


def packet_fields(packet):
    body, tag = packet[:-32], packet[-32:]
    if not hmac.compare_digest(tag, hmac.digest(KEY, body, hashlib.sha256)):
        raise ValueError("bad signature")
    if len(body) < 53 or body[:4] != b"BAS1" or body[4:20] != NONCE or len(body) != 53 + body[52]:
        raise ValueError("bad header")
    seq, scope, phase, activity, outcome, flags, quota, context, quota_ttl, ctx_ttl = (
        struct.unpack_from("<IIBBBBHHII", body, 28))
    return SimpleNamespace(stream=body[20:28], seq=seq, scope=scope, phase=phase,
                           activity=activity, outcome=outcome, flags=flags, quota=quota,
                           context=context, quota_ttl=quota_ttl, ctx_ttl=ctx_ttl, model=body[53:])


class SignedPeer(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, status, payload=b""):
        self.send_response(status)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/v1/hello":
            self.reply(200, NONCE)
        else:
            self.reply(404)

    def do_POST(self):
        if self.path != "/v1/state" or self.headers.get("Content-Type") != "application/octet-stream":
            self.reply(400)
            return
        raw = self.rfile.read(int(self.headers["Content-Length"]))
        try:
            frame = packet_fields(raw)
        except ValueError:
            self.reply(401)
            return
        if self.server.stream not in (None, frame.stream) or frame.seq <= self.server.seq:
            self.reply(409)
            return
        self.server.stream, self.server.seq = frame.stream, frame.seq
        self.reply(204)
        self.server.received.put(frame)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.peer = ThreadingHTTPServer(("127.0.0.1", 0), SignedPeer)
        self.peer.received = Queue()
        self.peer.stream, self.peer.seq = None, 0
        thread = threading.Thread(target=self.peer.serve_forever, args=(0.02,))
        thread.start()
        self.addCleanup(self.peer.server_close)
        self.addCleanup(thread.join)
        self.addCleanup(self.peer.shutdown)
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.profile = Path(directory.name) / "pairing.json"

    def start_cli(self, key):
        self.profile.write_text(json.dumps({"key": key.hex()}), encoding="utf-8")
        args = [sys.executable, str(ROOT / "tools/agent_sync.py"), "send",
                "--wifi", f"http://127.0.0.1:{self.peer.server_port}", "--profile", str(self.profile)]
        proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def cleanup():
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=5)
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()
        self.addCleanup(cleanup)
        return proc

    @staticmethod
    def write_event(proc, msg):
        proc.stdin.write(json.dumps(msg).encode() + b"\n")
        proc.stdin.flush()

    def test_generic_cli_signs_live_updates_expires_metrics_and_flushes_eof(self):
        proc = self.start_cli(KEY)
        self.write_event(proc, event(activity="thinking", quota={"used_pct": 0, "ttl_ms": 350},
                             context={"used_pct": 32.1, "quality": "estimated", "ttl_ms": 20000}))
        first = self.peer.received.get(timeout=6)
        self.assertEqual((first.phase, first.activity, first.model, first.quota, first.context, first.flags),
                         (1, 1, b"gpt-test", 0, 321, 5))
        self.assertTrue(0 < first.quota_ttl <= 350)
        expired = self.peer.received.get(timeout=6)
        self.assertEqual((expired.quota, expired.quota_ttl, expired.context), (UNKNOWN, 0, 321))
        self.assertLess(expired.ctx_ttl, first.ctx_ttl)
        self.write_event(proc, event("ended", outcome="unknown"))
        proc.stdin.close()
        last = self.peer.received.get(timeout=6)
        self.assertEqual((last.phase, last.activity, last.outcome, last.context, last.ctx_ttl),
                         (4, 0, 0, UNKNOWN, 0))
        self.assertEqual([first.seq, expired.seq, last.seq], [1, 2, 3])
        self.assertEqual(first.stream, last.stream)
        self.assertEqual(first.scope, last.scope)
        self.assertEqual(proc.wait(timeout=6), 0)
        self.assertIn(b"revision 2", proc.stderr.read())

    def test_wrong_key_exits_with_input_pipe_still_open(self):
        proc = self.start_cli(bytes(32))
        self.write_event(proc, event())
        self.assertNotEqual(proc.wait(timeout=6), 0)
        error = proc.stderr.read()
        self.assertIn(b"pairing key was rejected", error)
        self.assertNotIn(b"Fatal Python error", error)


def connection(link):
    @asynccontextmanager
    async def connect():
        yield link
    return connect


class PublisherTests(unittest.IsolatedAsyncioTestCase):
    async def test_upstream_heartbeat_is_forwarded_once_and_stale_link_released(self):
        now = [0.0]
        state = DeviceState(clock=lambda: now[0])
        eof, closed = threading.Event(), asyncio.Event()
        sent = asyncio.Queue()
        msg = dict(type="snapshot", v=1, seq=1, boot_id="a", bridge_id="b",
                   session=None, selected_by="none")
        state.accept(msg)
        link = SimpleNamespace(hello=AsyncMock(return_value=NONCE), send=sent.put)

        @asynccontextmanager
        async def connect():
            try:
                yield link
            finally:
                closed.set()

        job = asyncio.create_task(publish(state, connect, KEY, eof, heartbeat=0.01))
        try:
            await asyncio.wait_for(sent.get(), 1)
            await asyncio.sleep(0.25)
            self.assertTrue(sent.empty())
            now[0] = 8.0
            await asyncio.wait_for(closed.wait(), 1)
            self.assertEqual(link.hello.await_count, 1)
            self.assertTrue(state.accept(dict(msg, seq=2)))
            eof.set()
            self.assertEqual(await asyncio.wait_for(job, 2), 2)
            self.assertEqual(link.hello.await_count, 2)
            self.assertEqual(packet_fields(sent.get_nowait()).seq, 2)
        finally:
            if not job.done():
                job.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await job

    async def test_last_revision_arrives_during_ack(self):
        state, eof, packets = DeviceState(), threading.Event(), []
        state.accept(event())

        async def send(data):
            packets.append(packet_fields(data))
            if len(packets) == 1:
                await asyncio.sleep(0)
                state.accept(event("waiting"))
                eof.set()
        link = SimpleNamespace(hello=AsyncMock(return_value=NONCE), send=send)
        self.assertEqual(await publish(state, connection(link), KEY, eof), 2)
        self.assertEqual([(p.seq, p.phase) for p in packets], [(1, 1), (2, 3)])
        self.assertEqual(packets[0].stream, packets[1].stream)

    async def test_handshake_backoff_and_single_unauthorized_attempt(self):
        times = []
        async def hello():
            times.append(time.monotonic())
            if len(times) < 3:
                raise OSError("temporary disconnect")
            return NONCE
        state, eof = DeviceState(), threading.Event()
        state.accept(event())
        eof.set()
        link = SimpleNamespace(hello=hello, send=AsyncMock())
        self.assertEqual(await publish(state, connection(link), KEY, eof), 1)
        self.assertEqual(len(times), 3)
        self.assertGreaterEqual(times[1]-times[0], 0.20)
        self.assertGreaterEqual(times[2]-times[1], 0.45)
        link.hello = AsyncMock(return_value=NONCE)
        link.send = AsyncMock(side_effect=WifiResponseError("POST", "/v1/state", 401))
        with self.assertRaisesRegex(ValueError, "pairing key"):
            await publish(state, connection(link), KEY, eof)
        self.assertEqual((link.hello.await_count, link.send.await_count), (1, 1))

    async def test_disconnect_after_final_ack_keeps_receipt(self):
        state, eof = DeviceState(), threading.Event()
        state.accept(event())
        eof.set()
        link = SimpleNamespace(hello=AsyncMock(return_value=NONCE), send=AsyncMock())
        @asynccontextmanager
        async def connect():
            yield link
            raise OSError("disconnected during close")
        self.assertEqual(await publish(state, connect, KEY, eof, retry_limit=0), 1)
        link.send.assert_awaited_once()

    async def test_disconnected_at_eof_expires_and_invalid_or_broken_input_fail(self):
        state, eof = DeviceState(), threading.Event()
        state.accept(event())
        eof.set()
        link = SimpleNamespace(hello=AsyncMock(side_effect=OSError("offline")), send=AsyncMock())
        with self.assertRaisesRegex(OSError, "offline"):
            await publish(state, connection(link), KEY, eof, retry_limit=0)
        link.send.assert_not_awaited()
        source = io.BytesIO(b"x" * (MAX_LINE + 1) + b"\n" + b"{}\n")
        with self.assertRaisesRegex(ValueError, "no supported"):
            await stream_events(source, DeviceState(), connection(link), KEY)
        class BrokenInput:
            def readline(self, maximum):
                raise OSError("read failed")
        with self.assertRaisesRegex(OSError, "read failed"):
            await stream_events(BrokenInput(), DeviceState(), connection(link), KEY)
        clock = [0.0]
        stale = DeviceState(clock=lambda: clock[0])
        stale.accept(dict(type="snapshot", v=1, seq=1, boot_id="a", bridge_id="b", session=None,
                          selected_by="none"))
        clock[0] = 8.0
        with self.assertRaisesRegex(ValueError, "expired"):
            await publish(stale, connection(link), KEY, eof)
        self.assertEqual(link.hello.await_count, 1)


class PairTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("serial"), "pyserial not installed")
    def test_enter_with_no_port_does_not_open_pyserial(self):
        import serial
        with serial.Serial(port=None) as wire:
            self.assertFalse(wire.is_open)
            self.assertIsNone(wire.port)

    def fake_usb(self, chunks):
        wire = MagicMock()
        wire.__enter__.return_value = wire
        wire.read_until.side_effect = chunks
        wire.write.side_effect = len
        wire.open.side_effect = lambda: self.assertEqual((wire.port, wire.dtr, wire.rts),
                                                         ("mock-port", False, False))
        fake = SimpleNamespace(Serial=MagicMock(return_value=wire))
        module = patch.dict(sys.modules, {"serial": fake})
        module.start()
        self.addCleanup(module.stop)
        return wire

    def test_usb_split_response_overlong_log_and_saved_profile(self):
        password = "ab" * 12
        reply = f"BAJI_PAIR1 {KEY.hex()} BAJI-AABBCC {password}\r\n".encode()
        wire = self.fake_usb([b"X"*256, b"BAJI_PAIR1 partial\n", b"normal log\n",
                              b"log " + reply[:12], b"", reply[12:50], reply[50:]])
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "config" / "key.json"
            self.assertEqual(pairing.pair("mock-port", profile), ("BAJI-AABBCC", password))
            self.assertEqual(json.loads(profile.read_text()),
                             {"key": KEY.hex(), "ap_ssid": "BAJI-AABBCC", "ap_password": password})
        wire.open.assert_called_once()
        wire.write.assert_called_once_with(b"\x00\nBAJI_PAIR?\n")
        wire.__exit__.assert_called_once()

    def test_usb_network_bytes_ack_and_rejected_or_incomplete_write(self):
        wire = self.fake_usb([b"BAJI_WIFI_SAVED reboot\n", b"BAJI_WIFI_INVALID\n"])
        pairing.configure_station("mock-port", "测试", "12345678")
        wire.write.assert_called_once_with(b"\x00\nBAJI_WIFI " + "测试".encode().hex().encode() + b":3132333435363738\n")
        with self.assertRaisesRegex(ValueError, "did not save"):
            pairing.configure_station("mock-port", "", "")
        with self.assertRaisesRegex(ValueError, "byte length"):
            pairing.configure_station("mock-port", "测" * 11, "")
        wire.write.side_effect = lambda data: len(data) - 1
        with self.assertRaisesRegex(OSError, "incomplete"):
            pairing.pair("mock-port", "unused")
        self.assertEqual(wire.__exit__.call_count, 3)

    def test_usb_signed_link_correlates_replies_and_keeps_one_handle(self):
        cookie = "0123456789abcdef"
        header = f"BAJI_LINK1 {cookie} ".encode()
        wire = self.fake_usb([b"BAJI_LINK1 deadbeefabcd1234 OK\n", header + NONCE.hex().encode() + b"\n",
                              header+b"OK\n", header+b"AUTH\n", header+b"BUSY\n"])

        async def session():
            async with UsbTransport("mock-port") as link:
                self.assertEqual(await link.hello(), NONCE)
                await link.send(b"\x00\xff")
                with self.assertRaises(OSError) as rejected:
                    await link.send(b"bad-key")
                self.assertTrue(rejected.exception.pairing_rejected)
                with self.assertRaisesRegex(OSError, "BUSY"):
                    await link.send(b"competing")

        with patch("baji_bridge.transports.secrets.token_hex", return_value=cookie):
            asyncio.run(session())
        self.assertEqual(wire.write.call_args_list[1].args[0], b"\x00\nBAJI_LINK 0123456789abcdef 00ff\n")
        wire.open.assert_called_once()
        wire.__exit__.assert_called_once()

    def test_timed_out_partial_reply_preserves_prior_profile(self):
        wire = self.fake_usb([b"BAJI_PAIR1 half a reply"])
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "key.json"
            profile.write_bytes(b"old profile")
            with patch.object(pairing.time, "monotonic", side_effect=[10, 11, 16]):
                with self.assertRaisesRegex(TimeoutError, "did not reply"):
                    pairing.pair("mock-port", profile)
            self.assertEqual(profile.read_bytes(), b"old profile")
        wire.__exit__.assert_called_once()

    def test_matching_usb_line_completed_after_deadline_is_expired(self):
        self.fake_usb([b"BAJI_LINK1 abc OK\n"])
        with patch.object(pairing.time, "monotonic", side_effect=[10,14.99,15.01]):
            with self.assertRaisesRegex(TimeoutError, "did not reply"):
                pairing.request("mock-port", b"example", b"BAJI_LINK1 abc ")

    def test_usb_cancel_and_close_wait_for_reader_then_port_reopens(self):
        entered, finish_read = threading.Event(), threading.Event()
        wire = self.fake_usb([])
        def read(*args):
            entered.set()
            self.assertTrue(finish_read.wait(2))
            return b""
        wire.read_until.side_effect = read

        async def session():
            link = UsbTransport("mock-port")
            with self.assertRaisesRegex(OSError, "closed"):
                await link.hello()
            async with link:
                with self.assertRaisesRegex(OSError, "already open"):
                    await link.__aenter__()
                request = asyncio.create_task(link.hello())
                self.assertTrue(await asyncio.to_thread(entered.wait, 2))
                request.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await request
                close = asyncio.create_task(link.__aexit__(None, None, None))
                await asyncio.sleep(.02)
                self.assertFalse(close.done())
                self.assertFalse(wire.__exit__.called)
                finish_read.set()
                await close
            async with link:
                pass

        asyncio.run(session())
        self.assertEqual((wire.open.call_count, wire.__exit__.call_count), (2, 2))

    def test_concurrent_usb_requests_each_receive_their_own_split_reply(self):
        wire = self.fake_usb([])
        barrier, first_read = threading.Event(), threading.Event()
        fragments = []
        def write(frame):
            header = b"BAJI_LINK1 " + frame[2:].split()[1] + b" "
            fragments.extend((header + b"corrupt\xff\n", header[:17], header[17:] + b"OK\n"))
            return len(frame)
        def read(*args):
            first_read.set()
            self.assertTrue(barrier.wait(2))
            return fragments.pop(0)
        wire.write.side_effect, wire.read_until.side_effect = write, read

        async def session():
            async with UsbTransport("mock-port") as link:
                first = asyncio.create_task(link.send(b"A"))
                self.assertTrue(await asyncio.to_thread(first_read.wait, 2))
                second = asyncio.create_task(link.send(b"B"))
                await asyncio.sleep(.02)
                self.assertEqual(wire.write.call_count, 1)
                barrier.set()
                await asyncio.gather(first, second)

        asyncio.run(session())


if __name__ == "__main__":
    unittest.main()
