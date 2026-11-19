"""Read-only projection of BAJI snapshots or Codex exec events for the local badge."""
import math
import re
import secrets
import threading
import time


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
    return isinstance(value, str) and 0 < len(value) <= maximum and all(ord(c) >= 32 for c in value)


def model_family(model):
    value = (model or "").lower().split("/")[-1]
    if value.startswith(("gpt-", "chatgpt-", "codex-")) or re.match(r"^o[1-9](?:-|$)", value):
        return "gpt"
    return next((name for name in ("claude", "deepseek", "gemini") if value.startswith(name)), "neutral")


def metric(sample, elapsed):
    if not isinstance(sample, dict):
        return None
    value, quality = sample.get("used_pct"), sample.get("quality")
    age, ttl = sample.get("age_ms"), sample.get("ttl_ms")
    if (type(value) not in (float, int) or not 0 <= value <= 100 or not math.isfinite(value)
            or quality not in ("reported", "estimated") or not bounded(age, 0, 0xffffffff)
            or not bounded(ttl, 1, 3600000) or age + elapsed >= ttl):
        return None
    return {"value": value, "quality": quality}


def counts(source, scope):
    if not isinstance(source, dict):
        return None
    incoming, outgoing = source.get("input"), source.get("output")
    if not bounded(incoming, 0, 2**53-1) or not bounded(outgoing, 0, 2**53-1):
        return None
    return {"input": incoming, "output": outgoing, "scope": scope}


class PreviewState:
    def __init__(self, model=None, replay=False, clock=time.monotonic):
        self.clock, self.lock = clock, threading.Lock()
        self.stream_id = secrets.token_hex(6)
        self.model = model if short_id(model, 64) else None
        self.family, self.client, self.variant = model_family(self.model), "codex", None
        self.phase, self.activity, self.outcome = "idle", "none", "unknown"
        self.tools = self.helpers = 0
        self.replay, self.eof, self.source = replay, False, None
        self.has_session = False
        self.scope = self.seq = 0
        self.binding, self.identity, self.received = None, None, 0
        self.active, self.metrics, self.tokens = {}, {}, None
        self.selected_by = "recent_activity"

    def accept(self, msg):
        if not isinstance(msg, dict):
            return False
        with self.lock:
            if msg.get("type") == "snapshot":
                accepted = self.source != "codex" and self._snapshot(msg)
            else:
                accepted = self.source != "snapshot" and self._codex(msg)
            if accepted:
                self.received, self.eof = self.clock(), False
            return accepted

    def _snapshot(self, msg):
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
        if self.binding and self.binding != binding and self.clock() - self.received < 8:
            return False
        identity = (binding, None) if s is None else (binding, s["id"], s.get("turn_id"), s.get("model_id"),
                                                     s.get("variant"), s.get("client"), s.get("character_id"))
        if identity != self.identity:
            self.identity = identity
            self.scope += 1
        self.binding, self.seq, self.source = binding, seq, "snapshot"
        self.has_session = s is not None
        if s is None:
            self.client, self.family, self.model, self.variant = "unknown", "neutral", None, None
            self.phase, self.activity, self.outcome = "idle", "none", "unknown"
            self.tools = self.helpers = 0
            self.metrics, self.tokens, self.selected_by = {}, None, "none"
            return True
        self.client = s.get("client") if s.get("client") in ("codex", "claude_code", "gemini_cli", "opencode", "cursor") else "unknown"
        self.model = s.get("model_id") if short_id(s.get("model_id"), 64) else None
        self.family = s.get("character_id") if s.get("character_id") in ("gpt", "claude", "deepseek", "gemini") else "neutral"
        self.variant = s.get("variant") if short_id(s.get("variant"), 24) else None
        self.phase, self.activity, self.outcome = s["phase"], s["activity"], s["outcome"]
        self.tools, self.helpers = s["active_tools"], s["active_subagents"]
        selection = msg.get("selected_by")
        self.selected_by = selection if selection in ("pinned","foreground_session","recent_activity") else "none"
        self.metrics = msg.get("metrics") if isinstance(msg.get("metrics"), dict) else {}
        token = self.metrics.get("tokens")
        self.tokens = (counts(token, token.get("scope")) if isinstance(token, dict)
                       and token.get("scope") in ("turn", "session") and token.get("quality") == "reported" else None)
        return True

    def _codex(self, msg):
        kind = msg.get("type")
        if kind in ("thread.started", "turn.started"):
            if short_id(msg.get("model"), 64):
                self.model, self.family = msg["model"], model_family(msg["model"])
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
            if kind == "item.completed":
                self.active.pop(key, None)
            else:
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
        self.source = "codex"
        self.has_session = True
        return True

    def close_input(self):
        with self.lock:
            self.eof = True

    def view(self):
        with self.lock:
            age = max(0, (self.clock() - self.received) * 1000)
            if self.source is None:
                link = "offline" if self.eof else "connecting"
            elif self.eof:
                link = "finished" if self.replay or self.phase in ("ended", "error") else "offline"
            elif self.source == "snapshot" and age >= 8000 and not self.replay:
                link = "offline"
            else:
                link = "online"
            state = self.phase
            if state in ("working", "tool"):
                state = self.activity if self.activity in ("reading", "writing", "running", "searching", "compact") else "working"
            elif state == "ended":
                state = {"succeeded": "succeeded", "failed": "error", "cancelled": "cancelled"}.get(self.outcome, "ended")
            buckets = self.metrics.get("quota")
            first = buckets[0] if isinstance(buckets, list) and buckets else None
            return {"version": 1, "stream": self.stream_id, "link": link, "replay": self.replay,
                    "source": self.source, "has_session": self.has_session,
                    "client": self.client if self.source else "unknown", "model": self.model, "family": self.family,
                    "variant": self.variant, "state": state, "scope": self.scope, "selection": self.selected_by,
                    "tools": self.tools, "helpers": self.helpers, "tokens": self.tokens if link != "offline" else None,
                    "quota": metric(first, age) if link in ("online","finished") else None,
                    "context": metric(self.metrics.get("context"), age) if link in ("online","finished") else None}
