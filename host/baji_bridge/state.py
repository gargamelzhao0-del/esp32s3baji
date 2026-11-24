"""Normalize local Agent input and track the latest selected session."""
import re
import threading
import time

from .adapter import PercentSample


LINK_TIMEOUT_MS = 8000
SOURCE_FORMATS = {"agent.state": "generic", "snapshot": "snapshot"}
PHASES = ("idle", "working", "tool", "waiting", "ended", "error", "limited")
ACTIVITIES = ("none", "thinking", "reading", "writing", "running", "searching", "compact")
OUTCOMES = ("unknown", "succeeded", "failed", "cancelled")
ITEMS = {"command_execution": "running", "file_change": "writing", "web_search": "searching",
         "mcp_tool_call": "running", "agent_message": "writing", "reasoning": "thinking",
         "todo_list": "writing", "collab_tool_call": "running"}
TOOLS = {"command_execution", "file_change", "web_search", "mcp_tool_call", "collab_tool_call"}


def bounded(value, lo, hi):
    return type(value) is int and lo <= value <= hi


def short_id(value, maximum):
    return isinstance(value, str) and 0 < len(value) <= maximum and value.isprintable()


def model_family(model):
    value = (model or "").lower().split("/")[-1]
    if value.startswith(("gpt-", "chatgpt-", "codex-")) or re.match(r"^o[1-9](?:-|$)", value):
        return "gpt"
    return next((name for name in ("claude", "deepseek", "gemini") if value.startswith(name)), "neutral")


def percent_sample(sample, generic, now_ms):
    if sample is None:
        return None
    if isinstance(sample, dict):
        value, ttl = sample.get("used_pct"), sample.get("ttl_ms")
        age = sample.get("age_ms", 0 if generic else None)
        quality = sample.get("quality", "reported" if generic else None)
        if (type(value) in (float, int) and 0 <= value <= 100 and bounded(ttl, 1, 3600000)
                and bounded(age, 0, 0xffffffff) and quality in ("reported", "estimated")):
            return PercentSample(value, quality, now_ms - age, ttl)
    if generic:
        raise ValueError("invalid metric")
    return None


def counts(source, scope):
    if not isinstance(source, dict):
        return None
    incoming, outgoing = source.get("input"), source.get("output")
    if not bounded(incoming, 0, 2**53-1) or not bounded(outgoing, 0, 2**53-1):
        return None
    return {"input": incoming, "output": outgoing, "scope": scope}


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
                       character_id=model_family(model),
                       active_tools=0, active_subagents=0)
    quota = msg.get("quota")
    return dict(v=1, type="snapshot", boot_id="local", bridge_id="generic", seq=seq,
                session=session, selected_by="recent_activity" if session else "none",
                metrics={"quota": [quota] if quota is not None else [], "context": msg.get("context")})


class AgentState:
    def __init__(self, model=None, clock=time.monotonic):
        self.clock, self.lock = clock, threading.Lock()
        self.configured_model = model if short_id(model, 64) else None
        self.model = self.configured_model
        self.family, self.client, self.variant = model_family(self.model), "codex", None
        self.phase, self.activity, self.outcome = "idle", "none", "unknown"
        self.tools = self.helpers = 0
        self.source, self.revision = None, 0
        self.has_session = False
        self.scope = self.seq = 0
        self.binding, self.identity, self.received = None, None, 0
        self.active = {}
        self.quota = self.context = self.tokens = None
        self.selected_by = "recent_activity"

    def accept(self, msg):
        if not isinstance(msg, dict):
            return False
        kind = msg.get("type")
        if not isinstance(kind, str):
            return False
        source = SOURCE_FORMATS.get(kind, "codex")
        with self.lock:
            if self.source and self.source != source:
                return False
            received = self.clock()
            try:
                if source == "generic":
                    msg = generic_snapshot(msg, self.revision + 1)
                accepted = self._codex(msg) if source == "codex" else self._snapshot(msg, source == "generic", received)
            except (KeyError, ValueError):
                return False
            if accepted:
                self.source = source
                self.revision += 1
                self.received = received
            return accepted

    def _snapshot(self, msg, generic, received):
        s, seq = msg.get("session"), msg.get("seq")
        binding = (msg.get("boot_id"), msg.get("bridge_id"))
        if (type(msg.get("v")) is not int or msg["v"] != 1 or "session" not in msg
                or not bounded(seq, 1, 0xffffffff) or not all(short_id(key, 48) for key in binding)):
            return False
        if s is None:
            if msg.get("selected_by") != "none":
                return False
        elif (not isinstance(s, dict) or not short_id(s.get("id"), 48) or s.get("phase") not in PHASES
              or s.get("activity") not in ACTIVITIES or s.get("outcome") not in OUTCOMES
              or not bounded(s.get("active_tools"), 0, 64) or not bounded(s.get("active_subagents"), 0, 64)):
            return False
        if self.binding == binding and seq <= self.seq:
            return False
        if self.binding and self.binding != binding and (received - self.received) * 1000 < LINK_TIMEOUT_MS:
            return False
        metrics = msg.get("metrics") if isinstance(msg.get("metrics"), dict) else {}
        buckets = metrics.get("quota")
        now_ms = int(received * 1000)
        quota = percent_sample(buckets[0] if isinstance(buckets, list) and buckets else None, generic, now_ms)
        context = percent_sample(metrics.get("context"), generic, now_ms)
        if s is None:
            identity = (binding, None)
        else:
            model = s.get("model_id") if short_id(s.get("model_id"), 64) else None
            turn = s.get("turn_id") if short_id(s.get("turn_id"), 48) else None
            client = s.get("client") if s.get("client") in ("codex", "claude_code", "gemini_cli", "opencode", "cursor") else "unknown"
            family = s.get("character_id") if s.get("character_id") in ("gpt", "claude", "deepseek", "gemini") else "neutral"
            variant = s.get("variant") if short_id(s.get("variant"), 24) else None
            identity = (binding, s["id"], turn, model, variant, client, family)
        if identity != self.identity:
            self.identity = identity
            self.scope += 1
        self.binding, self.seq = binding, seq
        self.has_session = s is not None
        if s is None:
            self.client, self.family, self.model, self.variant = "unknown", "neutral", None, None
            self.phase, self.activity, self.outcome = "idle", "none", "unknown"
            self.tools = self.helpers = 0
            self.quota = self.context = self.tokens = None
            self.selected_by = "none"
            return True
        self.client, self.model, self.family, self.variant = client, model, family, variant
        self.phase, self.activity, self.outcome = s["phase"], s["activity"], s["outcome"]
        self.tools, self.helpers = s["active_tools"], s["active_subagents"]
        selection = msg.get("selected_by")
        self.selected_by = selection if selection in ("pinned","foreground_session","recent_activity") else "none"
        self.quota, self.context = quota, context
        token = metrics.get("tokens")
        self.tokens = (counts(token, token.get("scope")) if isinstance(token, dict)
                       and token.get("scope") in ("turn", "session") and token.get("quality") == "reported" else None)
        return True

    def _codex(self, msg):
        kind = msg.get("type")
        if kind in ("thread.started", "turn.started"):
            if short_id(msg.get("model"), 64):
                self.model = msg["model"]
            elif kind == "thread.started":
                self.model = self.configured_model
            self.family = model_family(self.model)
            self.scope += 1
            self.active.clear()
            self.phase = "working" if kind == "turn.started" else "idle"
            self.activity = "thinking" if kind == "turn.started" else "none"
            self.tools = self.helpers = 0
            self.tokens, self.outcome = None, "unknown"
        elif kind in ("turn.completed", "turn.failed"):
            self.active.clear()
            self.tools = self.helpers = 0
            self.phase = "ended" if kind == "turn.completed" else "error"
            self.outcome = "unknown" if kind == "turn.completed" else "failed"
            self.activity = "none"
            usage = msg.get("usage")
            if isinstance(usage, dict):
                self.tokens = counts({"input": usage.get("input_tokens"), "output": usage.get("output_tokens")}, "turn")
        elif kind in ("item.started", "item.updated", "item.completed"):
            item = msg.get("item")
            if self.phase in ("ended", "error") or not isinstance(item, dict) or not short_id(item.get("id"), 128):
                return False
            item_type, key = item.get("type"), item["id"]
            if not isinstance(item_type, str) or item_type not in ITEMS:
                return False
            # Iteration order is the most recent activity per item, including updates.
            self.active.pop(key, None)
            if kind != "item.completed":
                self.active[key] = item_type
            self.tools = sum(t in TOOLS for t in self.active.values())
            self.phase = "tool" if self.tools else "working"
            # A completed message can be the only writing event exposed in a turn.
            if self.tools:
                latest = next(t for t in reversed(self.active.values()) if t in TOOLS)
            else:
                latest = next(reversed(self.active.values()), "agent_message" if item_type == "agent_message" else "reasoning")
            self.activity = ITEMS[latest]
        else:
            return False
        self.has_session = True
        return True

    def upstream_expired(self):
        return self.source == "snapshot" and (self.clock() - self.received) * 1000 >= LINK_TIMEOUT_MS
