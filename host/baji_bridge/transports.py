"""Async transports for the badge's authenticated binary snapshot protocol."""
from __future__ import annotations

import asyncio
from contextlib import closing, suppress
import http.client
import re
import secrets
import sys
import threading
from urllib.parse import urlsplit

from .pairing import badge_port, exchange


SERVICE_UUID = "95a10000-79bb-4bc9-97b7-b1468fc445a7"
HELLO_UUID = "95a10001-79bb-4bc9-97b7-b1468fc445a7"
STATE_UUID = "95a10002-79bb-4bc9-97b7-b1468fc445a7"
MAX_PACKET = 192
FRAGMENT_DATA = 18  # 23-byte ATT MTU minus request opcode, handle and two framing bytes.


def _check_packet(packet: bytes) -> None:
    if not isinstance(packet, bytes) or not 1 <= len(packet) <= MAX_PACKET:
        raise ValueError("Expected a binary packet of 1 to 192 bytes")


def _nonce(data: bytes) -> bytes:
    if len(data) != 16:
        raise OSError("Badge returned an invalid nonce length")
    return bytes(data)


class WifiResponseError(OSError):
    def __init__(self, method: str, path: str, status: int):
        self.status = status
        self.pairing_rejected = method == "POST" and status == 401
        super().__init__(f"Badge {method} {path} returned HTTP {status}")


class WifiTransport:
    """Connect directly to the selected device. Each request owns its socket."""
    def __init__(self, url: str):
        parsed = urlsplit(url)
        if (parsed.scheme not in ("http", "https") or not parsed.hostname
                or parsed.username is not None or parsed.password is not None
                or parsed.path not in ("", "/") or parsed.query or parsed.fragment):
            raise ValueError("Use a device URL such as http://192.168.4.1")
        self._host = parsed.hostname
        self._port = parsed.port
        self._connection = http.client.HTTPSConnection if parsed.scheme == "https" else http.client.HTTPConnection

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        pass

    def _request(self, method: str, path: str, body: bytes | None, expected: int) -> bytes:
        # http.client uses the supplied host directly and never follows Location.
        # Keep one short-lived connection per request so an old nonce response
        # or cancelled caller cannot leave unread bytes on the next connection.
        headers = {"Connection": "close", "Cache-Control": "no-store"}
        if body is not None:
            headers["Content-Type"] = "application/octet-stream"
        try:
            with closing(self._connection(self._host, self._port, timeout=4)) as conn:
                conn.request(method, path, body, headers)
                response = conn.getresponse()
                if response.status != expected:
                    raise WifiResponseError(method, path, response.status)
                return response.read(17) if method == "GET" else b""
        except http.client.HTTPException as err:
            raise OSError(f"Invalid response from the badge for {path}") from err

    async def hello(self) -> bytes:
        data = await asyncio.to_thread(self._request, "GET", "/v1/hello", None, 200)
        return _nonce(data)

    async def send(self, packet: bytes) -> None:
        _check_packet(packet)
        await asyncio.to_thread(self._request, "POST", "/v1/state", packet, 204)


class PairingError(OSError):
    pairing_rejected = True


class UsbTransport:
    """Signed snapshots over the same physical port used for initial pairing."""
    def __init__(self, port: str):
        self._port = port
        self._ctx = None
        self._wire = None
        self._io = threading.Lock()

    async def __aenter__(self):
        if self._ctx is not None:
            raise OSError("Badge USB session already open")
        ctx = badge_port(self._port)
        self._wire = ctx.__enter__()
        self._ctx = ctx
        return self

    async def __aexit__(self, *exc):
        ctx = self._ctx
        self._wire = None
        if ctx is not None:
            def close():
                with self._io:
                    if self._ctx is ctx:
                        try:
                            ctx.__exit__(*exc)
                        finally:
                            self._ctx = None
            await asyncio.shield(asyncio.to_thread(close))

    async def _request(self, payload: str) -> str:
        cookie = secrets.token_hex(8)
        prefix = f"BAJI_LINK1 {cookie} "
        wire = self._wire
        cancelled = threading.Event()

        def transact():
            with self._io:
                if wire is None or wire is not self._wire or cancelled.is_set():
                    raise OSError("Badge USB session is closed")
                return exchange(wire, f"BAJI_LINK {cookie} {payload}".encode(), prefix.encode(), cancelled)

        try:
            line = await asyncio.to_thread(transact)
        finally:
            cancelled.set()
        return line[len(prefix):]

    async def hello(self) -> bytes:
        response = await self._request("HELLO")
        if not re.fullmatch("[0-9a-fA-F]{32}", response):
            raise OSError("Badge returned an invalid USB handshake")
        return bytes.fromhex(response)

    async def send(self, packet: bytes) -> None:
        _check_packet(packet)
        response = await self._request(packet.hex())
        if response == "AUTH":
            raise PairingError("Badge rejected the signed USB state")
        if response != "OK":
            raise OSError(f"Badge rejected USB state ({response})")


async def _gatt(operation, timeout: float, *, final_write: bool = False):
    try:
        return await asyncio.wait_for(operation, timeout)
    except Exception as err:
        # Only the final BAJI Write Response carries the firmware's HMAC result.
        # Bleak 3 exposes an ATT enum. Older WinRT and BlueZ backends carry the
        # code in a labeled error string. An unrelated OS error can also be 128.
        if final_write:
            typed_att = type(err).__name__ == "BleakGATTProtocolError" and getattr(err, "code", None) == 0x80
            legacy_att = re.search(r"\b(?:Protocol Error\s+|ATT error:\s*)0x80\b", str(err), re.IGNORECASE)
            if typed_att or legacy_att:
                raise PairingError("Badge rejected the signed BLE state") from err
        raise OSError(f"Badge Bluetooth operation failed: {err}") from err


class BleTransport:
    """One GATT connection. Each fragment is a write with an ATT response."""
    def __init__(self, address: str):
        if not address:
            raise ValueError("A badge Bluetooth address or device UUID is required")
        self._address = address
        self._client = None
        self._lock = asyncio.Lock()

    async def __aenter__(self):
        try:
            from bleak import BleakClient
        except ImportError as err:
            raise ImportError("Install the optional Bluetooth dependency with python -m pip install bleak") from err
        async with self._lock:
            if self._client is not None:
                raise OSError("Badge Bluetooth session already open")
            try:
                # Request the live service table on Windows, where a badge may
                # have been reflashed at the same Bluetooth address. A filtered
                # WinRT lookup can return Unreachable during service discovery.
                options = ({"winrt": {"use_cached_services": False}} if sys.platform == "win32"
                           else {"services": [SERVICE_UUID]})
                client = BleakClient(self._address, timeout=10, **options)
            except Exception as err:
                raise OSError(f"Unable to initialize Bluetooth for the badge: {err}") from err
            try:
                await _gatt(client.connect(), 15)
            except BaseException:
                with suppress(Exception):
                    await _gatt(client.disconnect(), 5)
                raise
            self._client = client
        return self

    async def __aexit__(self, exc_type, exc, tb):
        async with self._lock:
            client, self._client = self._client, None
            if client is not None:
                if exc_type is None:
                    await _gatt(client.disconnect(), 5)
                else:
                    with suppress(OSError):
                        await _gatt(client.disconnect(), 5)

    def _connected(self):
        if self._client is None or not self._client.is_connected:
            raise OSError("Badge Bluetooth connection is closed")
        return self._client

    async def hello(self) -> bytes:
        async with self._lock:
            return _nonce(await _gatt(self._connected().read_gatt_char(HELLO_UUID), 5))

    async def send(self, packet: bytes) -> None:
        _check_packet(packet)
        async with self._lock:
            client = self._connected()
            # Use only the negotiated ATT MTU. A backend can withhold it until
            # after the first transaction or expose only the baseline of 23.
            try:
                mtu = client.mtu_size
            except Exception:
                mtu = None
            fragment_data = min(MAX_PACKET, mtu - 5) if isinstance(mtu, int) and 23 <= mtu <= 517 else FRAGMENT_DATA
            for seq, start in enumerate(range(0, len(packet), fragment_data)):
                stop = min(start + fragment_data, len(packet))
                flags = int(start == 0) | (int(stop == len(packet)) << 1)
                fragment = bytes((flags, seq)) + packet[start:stop]
                await _gatt(client.write_gatt_char(STATE_UUID, fragment, response=True), 5,
                            final_write=stop == len(packet))


async def discover_badges(seconds: float) -> list[tuple[str, str]]:
    from bleak import BleakScanner

    if not 1 <= seconds <= 60:
        raise ValueError("Use a scan duration between 1 and 60 seconds")
    nearby = await _gatt(BleakScanner.discover(timeout=seconds, return_adv=True,
                                               service_uuids=[SERVICE_UUID]), seconds + 5)
    return sorted((device.address, advert.local_name or device.name or "BAJI-S3")
                  for device, advert in nearby.values()
                  if SERVICE_UUID in (uuid.lower() for uuid in advert.service_uuids))
