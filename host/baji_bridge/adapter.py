"""Adapter boundary; platform-specific hooks are NOT installed or implemented."""
from dataclasses import dataclass
from typing import Literal, Protocol

EventKind = Literal["turn_start", "tool_start", "tool_end", "waiting",
                    "resume", "turn_end", "session_end", "model_change",
                    "usage_update", "subagent_start", "subagent_end",
                    "terminal_error", "limited"]

@dataclass(frozen=True)
class PercentSample:
    used_pct: float | None
    quality: Literal["reported", "estimated", "unknown"]
    observed_monotonic_ms: int | None
    ttl_ms: int

@dataclass(frozen=True)
class QuotaSample:
    id: str
    label: str
    value: PercentSample
    window_s: int | None = None
    reset_at_unix_s: int | None = None

@dataclass(frozen=True)
class UsageSample:
    # Opaque local scope key, never an email address, API key or OAuth token.
    account_scope: str
    quota: tuple[QuotaSample, ...] = ()
    context: PercentSample | None = None
    input_tokens: int | None = None
    output_tokens: int | None = None
    cached_tokens: int | None = None
    token_scope: Literal["turn", "session"] = "session"

@dataclass(frozen=True)
class AgentEvent:
    adapter: str
    adapter_instance: str
    event_id: str
    session_id: str
    turn_id: str | None
    kind: EventKind
    observed_monotonic_ms: int
    tool_id: str | None = None
    parent_id: str | None = None
    model_id: str | None = None
    activity: Literal["none", "thinking", "reading", "writing", "running",
                      "searching", "compact"] | None = None
    outcome: Literal["unknown", "succeeded", "failed", "cancelled"] = "unknown"
    waiting_reason: Literal["approval", "input", "unknown"] | None = None
    usage: UsageSample | None = None
    # Deliberately excludes prompt, source code, tool input/output and credentials.

class Adapter(Protocol):
    def capabilities(self) -> frozenset[str]: ...
    def poll(self) -> list[AgentEvent]: ...
    def close(self) -> None: ...
