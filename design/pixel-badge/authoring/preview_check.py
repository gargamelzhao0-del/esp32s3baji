"""Exercise the pixel badge against the project's actual loopback preview bridge."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

PROJECT = Path(__file__).resolve().parents[3]


def check(chrome):
    bridge = subprocess.Popen(
        [sys.executable, "-B", str(PROJECT / "tools/agent_preview.py"), "--port", "0"],
        stdin=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
    )
    try:
        url = bridge.stderr.readline().strip() + "badge/design/pixel-badge/index.html?agent=1"
        with sync_playwright() as pw:
            browser = pw.chromium.launch(executable_path=chrome)
            page = browser.new_page(viewport={"width": 960, "height": 840})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(url)
            page.wait_for_function("active==='idle' && telemetry.mode==='SYNC'")
            fixture = (PROJECT / "protocol/fixtures/codex-flow.jsonl").read_text(encoding="utf-8")
            sample = json.loads(fixture.splitlines()[3])

            def publish():
                sample["seq"] += 1
                bridge.stdin.write(json.dumps(sample) + "\n")
                bridge.stdin.flush()

            def screen():
                return page.evaluate("Array.from(lcd.paint.getImageData(45,158,94,12).data).join(',')")

            publish()
            page.wait_for_function("active==='running' && !transfer && telemetry.quota.text==='68%' && telemetry.context.text==='42%'")
            before = screen()
            sample["metrics"]["quota"][0]["used_pct"] = 100
            sample["metrics"]["context"].update(used_pct=0,quality="estimated")
            publish()
            page.wait_for_function("telemetry.quota.text==='0%' && telemetry.context.text==='~0%'")
            page.wait_for_function("!dirty")
            assert screen() != before
            for activity, action in [("running","keyChime"), ("searching","lensFind"), ("waiting","palmTap")]:
                sample["session"].update(phase="waiting" if activity=="waiting" else "tool",
                                         activity="none" if activity=="waiting" else activity)
                publish()
                page.wait_for_function("s=>active===s && !transfer", arg=activity)
                for view in ("full","close"):
                    page.locator(f"[data-view={view}]").click()
                    x, y = page.evaluate("""() => {
                      const [x,y]=take.target[frame], b=lcd.canvas.getBoundingClientRect();
                      const close=view==="close", k=close?2.75:4;
                      return [b.left+(23+(x-(close?113:28))/k)*b.width/184,
                              b.top+((close?22:18)+(y-(close?34.5:12))/k)*b.height/184];
                    }""")
                    page.mouse.click(x,y)
                    page.wait_for_function("a=>active===a && !transfer",arg=action)
                    page.wait_for_function("s=>active===s && !transfer",arg=activity)
            for activity in ["writing","compact"]:
                sample["session"].update(phase="tool",activity=activity)
                publish()
                page.wait_for_function("s=>active===s && !transfer && frame>=9",arg=activity)
                page.locator("#cheek-button").click()
                page.wait_for_function("active.startsWith('cheek') && pausedHands?.frame>=9")
                frozen = page.evaluate("pausedHands.frame")
                page.wait_for_timeout(220)
                assert page.evaluate("pausedHands.frame") == frozen
                page.wait_for_function("s=>active===s && !transfer",arg=activity)
                assert page.evaluate("frame") >= frozen
            sample["metrics"]["context"]["ttl_ms"] = 1
            sample["metrics"]["quota"] = []
            publish()
            page.wait_for_function("telemetry.mode==='LIVE' && telemetry.quota.text==='--' && telemetry.context.text==='--'")
            sample["session"]["character_id"] = "claude"
            publish()
            page.wait_for_function("active==='idle' && telemetry.mode==='OFF'")
            bridge.stdin.close()
            page.wait_for_function("document.querySelector('#connection-label').textContent==='Agent 离线'")
            assert not errors, errors
            browser.close()
    finally:
        bridge.terminate()
        bridge.wait(timeout=5)
    print("Bridge snapshots, metric-only repaint, expiry, session change, disconnect, 6 on-canvas tool contacts and both paper resumes passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    check(parser.parse_args().chrome)
