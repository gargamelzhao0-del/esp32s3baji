"""Browser projection of the shared Agent lifecycle state."""
import secrets
import time

from .state import AgentState, LINK_TIMEOUT_MS


def metric(sample, now_ms):
    if sample is None or not sample.remaining_ms(now_ms):
        return None
    return {"value": sample.used_pct, "quality": sample.quality}


class PreviewState(AgentState):
    def __init__(self, model=None, replay=False, clock=time.monotonic):
        super().__init__(model=model, clock=clock)
        self.stream_id = secrets.token_hex(6)
        self.replay = replay
        self.closed_revision = None

    def close_input(self):
        with self.lock:
            self.closed_revision = self.revision

    def view(self):
        with self.lock:
            now = self.clock()
            age = max(0, (now - self.received) * 1000)
            eof = self.closed_revision == self.revision
            if self.source is None:
                link = "offline" if eof else "connecting"
            elif eof:
                link = "finished" if self.replay or self.phase in ("ended", "error") else "offline"
            elif self.source == "snapshot" and age >= LINK_TIMEOUT_MS and not self.replay:
                link = "offline"
            else:
                link = "online"
            state = self.phase
            if state in ("working", "tool"):
                state = self.activity if self.activity in ("reading", "writing", "running", "searching", "compact") else "working"
            elif state == "ended":
                state = {"succeeded": "succeeded", "failed": "error", "cancelled": "cancelled"}.get(self.outcome, "ended")
            return {"version": 1, "stream": self.stream_id, "link": link, "replay": self.replay,
                    "source": self.source, "has_session": self.has_session,
                    "client": self.client if self.source else "unknown", "model": self.model, "family": self.family,
                    "variant": self.variant, "state": state, "scope": self.scope, "selection": self.selected_by,
                    "tools": self.tools, "helpers": self.helpers, "tokens": self.tokens if link != "offline" else None,
                    "quota": metric(self.quota, int(now * 1000)) if link in ("online","finished") else None,
                    "context": metric(self.context, int(now * 1000)) if link in ("online","finished") else None}
