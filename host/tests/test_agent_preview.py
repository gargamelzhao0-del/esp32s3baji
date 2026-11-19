import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "host"))
from baji_bridge.preview import PreviewState


def fixture(name):
    return [json.loads(line) for line in (ROOT/"protocol"/"fixtures"/name).read_text(encoding="utf-8").splitlines() if line.strip()]


class PreviewTests(unittest.TestCase):
    def test_fixture_projects_work_and_never_turns_unknown_end_into_success(self):
        state = PreviewState(replay=True)
        stages = []
        for msg in fixture("codex-flow.jsonl"):
            self.assertTrue(state.accept(msg))
            stages.append(state.view()["state"])
        self.assertEqual(stages, ["idle","working","reading","running","waiting","running","writing","ended"])
        state.close_input()
        self.assertEqual(state.view()["link"], "finished")

    def test_metrics_expire_independently_and_duplicate_does_not_refresh_link(self):
        now = [0.0]
        source = PreviewState(clock=lambda: now[0])
        msg = fixture("codex-flow.jsonl")[0]
        msg["metrics"]["context"].update(used_pct=0,age_ms=0,ttl_ms=1000)
        source.accept(msg)
        self.assertEqual(source.view()["context"]["value"], 0)
        now[0] = 2
        self.assertIsNone(source.view()["context"])
        self.assertEqual(source.view()["quota"]["value"], 32)
        self.assertFalse(source.accept(msg))
        now[0] = 8
        self.assertEqual(source.view()["link"], "offline")
        self.assertIsNone(source.view()["quota"])

    def test_empty_selection_clears_previous_role_and_tools(self):
        state = PreviewState()
        state.accept(fixture("codex-flow.jsonl")[2])
        empty = fixture("edge-cases.jsonl")[0]
        empty["seq"] = 32
        self.assertTrue(state.accept(empty))
        view = state.view()
        self.assertFalse(view["has_session"])
        self.assertEqual((view["family"],view["tools"],view["model"]), ("neutral",0,None))
        self.assertIsNone(view["context"])

    def test_explicit_end_outcomes_and_parallel_counts(self):
        msg = fixture("codex-flow.jsonl")[-1]
        state = PreviewState()
        for outcome, expected in [("unknown","ended"),("cancelled","cancelled"),("failed","error"),("succeeded","succeeded")]:
            msg["seq"] += 1
            msg["session"].update(outcome=outcome,active_subagents=2)
            self.assertTrue(state.accept(msg))
            self.assertEqual((state.view()["state"],state.view()["helpers"]), (expected,2))

    def test_cli_events_concurrent_items_and_privacy(self):
        state = PreviewState(model="gpt-test")
        state.accept({"type":"turn.started"})
        def item(phase, id, type):
            state.accept({"type":"item."+phase,"item":{"id":id,"type":type,"command":"sample-private-command","text":"sample-private-content"}})
        item("started","one","command_execution")
        item("started","two","web_search")
        self.assertEqual((state.view()["state"],state.view()["tools"]), ("searching",2))
        item("updated","two","web_search")
        item("completed","two","web_search")
        self.assertEqual((state.view()["state"],state.view()["tools"]), ("running",1))
        item("completed","one","command_execution")
        state.accept({"type":"turn.completed","usage":{"input_tokens":10,"output_tokens":4,"cached_input_tokens":3}})
        view = state.view()
        self.assertEqual(view["tokens"],{"input":10,"output":4,"scope":"turn"})
        self.assertIsNone(view["quota"])
        self.assertIsNone(view["context"])
        self.assertNotIn("sample-private",json.dumps(view))
        self.assertEqual(view["state"],"ended")
        self.assertFalse(state.accept({"type":"item.completed","item":{"id":"z","type":"agent_message"}}))
        state.close_input()
        self.assertEqual(state.view()["link"],"finished")

    def test_unknown_model_and_incomplete_stream(self):
        state = PreviewState()
        state.accept({"type":"turn.started"})
        self.assertEqual(state.view()["family"],"neutral")
        state.close_input()
        self.assertEqual(state.view()["link"],"offline")
