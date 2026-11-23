"""Async transports for the badge's authenticated binary snapshot protocol."""
from __future__ import annotations

import asyncio
from contextlib import closing, suppress
import http.client
from urllib.parse import urlsplit


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


async def _gatt(operation, timeout: float):
    try:
        return await asyncio.wait_for(operation, timeout)
    except Exception as err:
        # Bleak backends use different platform exception classes.
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
            raise OSError("Install the optional Bluetooth dependency with python -m pip install bleak") from err
        async with self._lock:
            if self._client is not None:
                raise OSError("Badge Bluetooth session already open")
            try:
                client = BleakClient(self._address, services=[SERVICE_UUID], timeout=10)
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
            for seq, start in enumerate(range(0, len(packet), FRAGMENT_DATA)):
                stop = min(start + FRAGMENT_DATA, len(packet))
                flags = int(start == 0) | (int(stop == len(packet)) << 1)
                fragment = bytes((flags, seq)) + packet[start:stop]
                await _gatt(client.write_gatt_char(STATE_UUID, fragment, response=True), 5)


async def discover_badges(seconds: float) -> list[tuple[str, str]]:
    from bleak import BleakScanner

    if not 1 <= seconds <= 60:
        raise ValueError("Use a scan duration between 1 and 60 seconds")
    nearby = await _gatt(BleakScanner.discover(timeout=seconds, return_adv=True,
                                               service_uuids=[SERVICE_UUID]), seconds + 5)
    return sorted((device.address, advert.local_name or device.name or "BAJI-S3")
                  for device, advert in nearby.values()
                  if SERVICE_UUID in (uuid.lower() for uuid in advert.service_uuids))
