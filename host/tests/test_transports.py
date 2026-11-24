import asyncio
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from baji_bridge.transports import (BleTransport, HELLO_UUID, SERVICE_UUID,
                                    STATE_UUID, WifiResponseError, WifiTransport)


class DeviceHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.server.paths.append(self.path)
        if self.path != "/v1/hello":
            self.send_error(404)
            return
        nonce = self.server.nonce
        self.send_response(self.server.get_status)
        self.send_header("Content-Length", str(len(nonce)))
        self.send_header("Location", "/redirect-target")
        self.end_headers()
        self.wfile.write(nonce)

    def do_POST(self):
        self.server.paths.append(self.path)
        if self.path != "/v1/state":
            self.send_error(404)
            return
        self.server.payloads.append((self.headers.get("Content-Type"), self.rfile.read(int(self.headers["Content-Length"]))))
        self.send_response(self.server.post_status)
        self.send_header("Content-Length", "0")
        self.end_headers()


class WifiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.peer = ThreadingHTTPServer(("127.0.0.1", 0), DeviceHandler)
        self.peer.paths, self.peer.payloads = [], []
        self.peer.nonce = bytes(range(16))
        self.peer.get_status, self.peer.post_status = 200, 204
        self.worker = threading.Thread(target=self.peer.serve_forever, args=(0.02,))
        self.worker.start()
        self.url = f"http://127.0.0.1:{self.peer.server_port}/"

    async def asyncTearDown(self):
        await asyncio.to_thread(self.peer.shutdown)
        self.worker.join()
        self.peer.server_close()

    async def test_direct_loopback(self):
        payload = bytes(range(192))
        with patch.dict("os.environ", HTTP_PROXY="http://127.0.0.1:1", NO_PROXY=""):
            async with WifiTransport(self.url) as transport:
                self.assertEqual(await transport.hello(), self.peer.nonce)
                await transport.send(payload)
                for status in (401, 409, 503):
                    self.peer.post_status = status
                    with self.assertRaises(WifiResponseError) as raised:
                        await transport.send(payload)
                    self.assertEqual(raised.exception.status, status)
                for bad in (b"", bytes(193)):
                    with self.assertRaises(ValueError):
                        await transport.send(bad)
        self.assertEqual(self.peer.payloads, [("application/octet-stream", payload)] * 4)
        self.assertEqual(self.peer.paths, ["/v1/hello"] + ["/v1/state"] * 4)

    async def test_invalid_nonce_and_redirect(self):
        async with WifiTransport(self.url) as transport:
            for n in (0, 15, 17, 64):
                self.peer.nonce = bytes(n)
                with self.assertRaisesRegex(OSError, "nonce"):
                    await transport.hello()
            self.peer.get_status = 307
            with self.assertRaises(WifiResponseError) as raised:
                await transport.hello()
            self.assertEqual(raised.exception.status, 307)
        self.assertNotIn("/redirect-target", self.peer.paths)

    def test_only_explicit_origins(self):
        for url in ("http://a@host", "http://host/path", "http://host/?key=value", "tcp://host", "bad"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                WifiTransport(url)


class BleTests(unittest.IsolatedAsyncioTestCase):
    async def test_discovery_only_reports_badges(self):
        from types import SimpleNamespace
        from unittest.mock import AsyncMock
        from baji_bridge.transports import SERVICE_UUID, discover_badges

        device = SimpleNamespace(address="badge", name=None)
        nearby = {"1": (device, SimpleNamespace(local_name="BAJI-S3", service_uuids=[SERVICE_UUID.upper()])),
                  "2": (device, SimpleNamespace(local_name="other", service_uuids=[]))}
        scanner = SimpleNamespace(discover=AsyncMock(return_value=nearby))
        with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakScanner=scanner)}):
            self.assertEqual(await discover_badges(1), [("badge", "BAJI-S3")])
            scanner.discover.assert_awaited_once_with(timeout=1, return_adv=True, service_uuids=[SERVICE_UUID])

    def setUp(self):
        self.writes = []
        self.fail_seq = None
        self.write_error = RuntimeError("ATT rejected packet")
        self.disconnected = 0
        case = self

        class Peer:
            is_connected = False

            def __init__(self, address, services, timeout):
                case.assertEqual(address, "demo")
                case.assertEqual(services, [SERVICE_UUID])
                case.assertGreater(timeout, 0)

            async def connect(self):
                self.is_connected = True

            async def disconnect(self):
                self.is_connected = False
                case.disconnected += 1

            async def read_gatt_char(self, uuid):
                case.assertEqual(uuid, HELLO_UUID)
                return bytearray(range(16))

            async def write_gatt_char(self, uuid, fragment, *, response):
                case.assertEqual(uuid, STATE_UUID)
                case.assertIs(response, True)
                case.assertLessEqual(len(fragment), 20)
                await asyncio.sleep(0)
                case.writes.append(fragment)
                if fragment[1] == case.fail_seq:
                    raise case.write_error

        fake = SimpleNamespace(BleakClient=Peer)
        bleak_module = patch.dict(sys.modules, {"bleak": fake})
        bleak_module.start()
        self.addCleanup(bleak_module.stop)

    async def test_default_mtu_and_serialization(self):
        payload = bytes(range(192))
        async with BleTransport("demo") as transport:
            self.assertEqual(await transport.hello(), bytes(range(16)))
            await asyncio.gather(transport.send(payload), transport.send(b"x"))
        expected = [bytes((1 if i == 0 else 2 if i == 10 else 0, i)) + payload[i*18:(i+1)*18]
                    for i in range(11)]
        self.assertEqual(self.writes, expected + [b"\x03\x00x"])
        self.assertEqual(self.disconnected, 1)
        with self.assertRaises(OSError):
            await transport.hello()

    async def test_failed_write_is_not_replayed(self):
        async with BleTransport("demo") as transport:
            self.fail_seq = 1
            with self.assertRaisesRegex(OSError, "ATT rejected"):
                await transport.send(bytes(64))
            self.assertEqual([p[1] for p in self.writes], [0, 1])
            self.fail_seq = None
            await transport.send(bytes(18))
            self.assertEqual(self.writes[-1], b"\x03\x00" + bytes(18))
            with self.assertRaises(ValueError):
                await transport.send(bytes(193))
        self.assertEqual(self.disconnected, 1)

    async def test_only_final_firmware_auth_error_rejects_pairing(self):
        class BleakGATTProtocolError(Exception):
            code = 0x80

        async with BleTransport("demo") as transport:
            for error, seq, rejected in (
                (RuntimeError("Write failed: Protocol Error 0x80: Unknown"), 1, True),
                (RuntimeError("ATT error: 0x80 (unknown)"), 1, True),
                (BleakGATTProtocolError(), 1, True),
                (RuntimeError("Protocol Error 0x80: Unknown"), 0, False),
                (RuntimeError("Protocol Error 0x81: Unknown"), 1, False),
                (OSError("Windows system error 0x80"), 1, False),
            ):
                self.fail_seq, self.write_error = seq, error
                with self.assertRaises(OSError) as raised:
                    await transport.send(bytes(22))
                self.assertEqual(getattr(raised.exception, "pairing_rejected", False), rejected)


if __name__ == "__main__":
    unittest.main()
