"""Project local Agent events to the badge's bounded, signed wireless snapshot."""
import hashlib
import hmac
import math
import struct
import unicodedata

from .preview import ACTIVITIES, OUTCOMES, PHASES, PreviewState, bounded, metric, short_id

HEADER = struct.Struct("<4s16s8sIIBBBBHHIIB")
UNKNOWN = 65535


def metric_fields(sample, elapsed):
    fresh = metric(sample, elapsed)
    if not fresh:
        return UNKNOWN, 0, False
    ttl = sample["ttl_ms"] - sample["age_ms"] - elapsed
    return math.floor(fresh["value"] * 10 + 0.5), ttl, fresh["quality"] == "estimated"


def display_label(model):
    if not model:
        return b""
    # The font consumes UTF-8. Never split a glyph at the wire's byte limit.
    printable = "".join(c for c in unicodedata.normalize("NFC", model) if c.isprintable())
    return printable.strip().encode("utf8")[:32].decode("utf8", "ignore").encode("utf8")


def generic_metric(value):
    if value is None:
        return None
    if not isinstance(value, dict):
        raise ValueError("expected metric object")
    pct, ttl = value.get("used_pct"), value.get("ttl_ms")
    age, quality = value.get("age_ms", 0), value.get("quality", "reported")
    if (type(pct) not in (float, int) or not 0 <= pct <= 100
            or not bounded(ttl, 1, 3600000) or not bounded(age, 0, 0xffffffff)
            or quality not in ("reported", "estimated")):
        raise ValueError("invalid metric")
    return dict(used_pct=pct, ttl_ms=ttl, age_ms=age, quality=quality)


def generic_snapshot(msg, seq):
    selected, model, turn = msg["session"], msg.get("model"), msg.get("turn")
    if ((selected is not None and not short_id(selected, 48))
            or (model is not None and not short_id(model, 64))
            or (turn is not None and not short_id(turn, 48))):
        raise ValueError("invalid session, model or turn identifier")
    session = None
    if selected is not None:
        session = dict(id=selected, turn_id=turn, model_id=model, phase=msg.get("phase"),
                       activity=msg.get("activity", "none"), outcome=msg.get("outcome", "unknown"),
                       active_tools=0, active_subagents=0)
    quota = generic_metric(msg.get("quota"))
    return dict(v=1, type="snapshot", boot_id="local", bridge_id="generic", seq=seq,
                session=session, selected_by="recent_activity" if session else "none",
                metrics={"quota": [quota] if quota else [], "context": generic_metric(msg.get("context"))})


class DeviceState(PreviewState):
    """One stream per process. Generic events are full replacements; tool
    lifecycle folding for Codex and BAJI binding rules come from PreviewState."""
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.format = None
        self.revision = 0

    def accept(self, msg):
        if not isinstance(msg, dict):
            return False
        kind = msg.get("type")
        fmt = "codex"
        if kind == "agent.state":
            fmt = "generic"
        elif kind == "snapshot":
            fmt = "snapshot"
        with self.lock:
            if self.format and self.format != fmt:
                return False
            if fmt == "generic":
                try:
                    msg = generic_snapshot(msg, self.revision + 1)
                except (KeyError, ValueError):
                    return False
            if not (self._codex(msg) if fmt == "codex" else self._snapshot(msg)):
                return False
            self.format, self.received = fmt, self.clock()
            self.revision += 1
            return True

    def sample(self):
        with self.lock:
            elapsed = max(0, int((self.clock() - self.received) * 1000))
            if not self.format or (self.format == "snapshot" and elapsed >= 8000):
                return None
            buckets = self.metrics.get("quota")
            first = buckets[0] if isinstance(buckets, list) and buckets else None
            return (self.revision, self.scope, self.has_session, self.phase, self.activity, self.outcome,
                    display_label(self.model), metric_fields(first, elapsed),
                    metric_fields(self.metrics.get("context"), elapsed))


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
