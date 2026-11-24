"""Project local Agent events to the badge's bounded, signed wireless snapshot."""
import hashlib
import hmac
import math
import struct
import unicodedata

from .state import ACTIVITIES, LINK_TIMEOUT_MS, OUTCOMES, PHASES, AgentState

HEADER = struct.Struct("<4s16s8sIIBBBBHHIIB")
UNKNOWN = 65535


def metric_fields(sample, now_ms):
    remaining = sample.remaining_ms(now_ms) if sample else 0
    if not remaining:
        return UNKNOWN, 0, False
    return math.floor(sample.used_pct * 10 + 0.5), remaining, sample.quality == "estimated"


def display_label(model):
    if not model:
        return b""
    # The font consumes UTF-8. Never split a glyph at the wire's byte limit.
    printable = "".join(c for c in unicodedata.normalize("NFC", model) if c.isprintable())
    return printable.strip().encode("utf8")[:32].decode("utf8", "ignore").encode("utf8")


class DeviceState(AgentState):
    def sample(self):
        with self.lock:
            now = self.clock()
            if not self.source or (self.source == "snapshot" and (now - self.received) * 1000 >= LINK_TIMEOUT_MS):
                return None
            now_ms = int(now * 1000)
            return (self.revision, self.scope, self.has_session, self.phase, self.activity, self.outcome,
                    display_label(self.model), metric_fields(self.quota, now_ms), metric_fields(self.context, now_ms))


def signed_snapshot(key, nonce, stream, seq, sample):
    if len(key) != 32 or len(nonce) != 16 or len(stream) != 8 or not 1 <= seq <= 0xffffffff:
        raise ValueError("invalid pairing key, challenge, stream or sequence")
    _, identity, selected, phase, activity, outcome, model, quota, context = sample
    flags = int(selected) | (int(quota[2]) << 1) | (int(context[2]) << 2)
    head = HEADER.pack(b"BAS1", nonce, stream, seq, identity, PHASES.index(phase),
                       ACTIVITIES.index(activity), OUTCOMES.index(outcome), flags,
                       quota[0], context[0], quota[1], context[1], len(model))
    body = head + model
    return body + hmac.digest(key, body, hashlib.sha256)
