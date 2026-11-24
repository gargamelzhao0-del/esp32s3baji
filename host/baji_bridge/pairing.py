"""Physical USB setup. Pairing secrets never travel in a radio request."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import tempfile
import time


@contextmanager
def badge_port(port):
    import serial
    # Set modem control before opening: opening a console must not reset the badge.
    with serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=2) as wire:
        wire.dtr = wire.rts = False
        wire.port = port
        wire.open()
        wire.reset_input_buffer()
        yield wire


def exchange(wire, command, prefix, cancelled=None):
    # Discard an unfinished line left by a disconnected host before delimiting
    # it. A bare newline could execute a previous command missing its terminator.
    frame = b"\x00\n" + command + b"\n"
    if wire.write(frame) != len(frame):
        raise OSError("incomplete badge setup request on the USB port")
    deadline = time.monotonic() + 5
    pending = bytearray()
    dropping = False
    while time.monotonic() < deadline:
        if cancelled is not None and cancelled.is_set():
            raise OSError("USB exchange cancelled")
        part = wire.read_until(b"\n", 256)
        if not part:
            continue
        complete = part.endswith(b"\n")
        if dropping or len(pending) + len(part) > 256:
            pending.clear()
            dropping = not complete
            continue
        pending.extend(part)
        if not complete:
            continue
        line = bytes(pending).rstrip(b"\r\n")
        pending.clear()
        start = line.find(prefix)
        if start >= 0:
            if time.monotonic() >= deadline:
                break
            try:
                return line[start:].decode("ascii")
            except UnicodeDecodeError:
                continue
    raise TimeoutError("badge setup did not reply on the USB port")


def request(port, command, prefix):
    with badge_port(port) as wire:
        return exchange(wire, command, prefix)


def pair(port, profile):
    line = request(port, b"BAJI_PAIR?", b"BAJI_PAIR1 ")
    match = re.fullmatch(r"BAJI_PAIR1 ([0-9a-f]{64}) (BAJI-[A-F0-9]{6}) ([0-9a-f]{24})", line)
    if not match:
        raise ValueError("invalid USB pairing reply")
    key, ssid, password = match.groups()
    profile = Path(profile).expanduser()
    profile.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=profile.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf8") as file:
            json.dump({"key":key, "ap_ssid":ssid, "ap_password":password}, file)
        os.replace(tmp, profile)
    finally:
        Path(tmp).unlink(missing_ok=True)
    return ssid, password


def configure_station(port, ssid, password):
    name, secret = ssid.encode("utf8"), password.encode("utf8")
    if (b"\0" in name+secret or len(name)>32 or (secret and not 8<=len(secret)<=63)
            or (not name and secret)):
        raise ValueError("invalid Wi-Fi SSID or passphrase byte length")
    cmd = b"BAJI_WIFI " + name.hex().encode() + b":" + secret.hex().encode()
    if request(port,cmd,b"BAJI_WIFI_") != "BAJI_WIFI_SAVED reboot":
        raise ValueError("badge did not save network settings")
