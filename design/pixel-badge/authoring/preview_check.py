"""Exercise the pixel badge against the project's actual loopback preview bridge."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from playwright.sync_api import sync_playwright
from contact_check import install_pet_clock, point

PROJECT = Path(__file__).resolve().parents[3]


def mark_outline(page):
    # Read the dark keyline from the actual 184px compositor, without
    # counting motion highlights or relying on an HTML state attribute.
    return page.evaluate("""() => {
      const [x,y] = view==="close" ? [15,75] : [32,47];
      const pixels = lcd.paint.getImageData(x,y,13,13).data;
      const ink = [];
      for (let i=0; i<pixels.length; i+=4)
        if (pixels[i]===69 && pixels[i+1]===56 && pixels[i+2]===72) ink.push(i/4);
      return ink.join(",");
    }""")


def check_static_marks(browser, url):
    states = ("working","waiting","succeeded","ended","limited","cancelled","error")
    reference = {}
    for variant in ("reduced","legacy"):
        page = browser.new_page(reduced_motion="reduce" if variant=="reduced" else "no-preference")
        page.goto(url.split("?")[0] + ("?motion=legacy" if variant=="legacy" else ""))
        for view in ("full","close"):
            page.locator(f"[data-view={view}]").click()
            for state in states:
                page.select_option("#agent-state", state)
                page.wait_for_function("s=>active===s && !transfer && !dirty",arg=state)
                mask = mark_outline(page)
                assert mask and reference.setdefault(state, mask)==mask, (state,view,variant)
        page.close()
    assert len(set(reference.values()))==len(states), "Agent outcomes must be distinct in monochrome"


def check_tool_replies(browser, url):
    for view in ("full", "close"):
        page = browser.new_page(viewport={"width": 960, "height": 840}, has_touch=view=="close")
        install_pet_clock(page)
        page.goto(url.split("?")[0])
        page.wait_for_function("sheet && active==='idle'", polling=20)
        page.locator(f"[data-view={view}]").evaluate("b=>b.click()")
        tap = page.touchscreen.tap if view=="close" else page.mouse.click

        def wait_frame(clip, frame):
            page.evaluate("""async ([clip,wanted]) => {
                for (let i=0;i<300;i++) {
                    if (active===clip && !transfer && frame===wanted) return;
                    await advancePet(20);
                }
                throw Error(clip+" F"+wanted);
            }""", [clip, frame])

        def tap_tool(dy=0):
            x, y = page.evaluate("""dy=>{
                const [x,y]=take.target[frame], t=lcd.facePoint(view,take,frame,x,y+dy);
                const b=lcd.canvas.getBoundingClientRect();
                return [b.left+t.sx*b.width/184,b.top+t.sy*b.height/184];
            }""", dy)
            tap(x,y)

        for job, reply in (("searching","lensFind"), ("running","keyChime"), ("waiting","palmTap")):
            page.select_option("#agent-state", job)
            wait_frame(job, 10)
            before = page.evaluate("take.target[frame]")
            tap_tool(-25 if job=="searching" else 0)
            page.wait_for_function("id=>active===id", arg=reply, polling=20)
            if job=="searching":
                delta = page.evaluate("([x,y])=>Math.hypot(take.target[frame][0]-x,take.target[frame][1]-y)", before)
                assert delta <= 5.5, (view,"scan-to-find handoff",delta)
                wait_frame(reply, 9)
                # The first gold pixel should sit on the upper-right brass rim in
                # the actual composited frame, in either crop.
                assert page.evaluate("""() => {
                    const [x,y]=take.target[frame], t=lcd.facePoint(view,take,frame,x+16.5,y-16.5);
                    const c=lcd.paint.getImageData(Math.round(t.sx),Math.round(t.sy),1,1).data;
                    return c[0]===255 && c[1]===231 && c[2]===170;
                }"""), view
            wait_frame(reply, 10)
            tap_tool()
            assert page.evaluate("id=>intent===id && gestures.contact?.part===id && gestures.contact.phase==='release'", reply), (view, reply)
            elapsed = page.evaluate("elapsed")
            page.locator("#scene-button").evaluate("button=>button.click()")
            page.locator("#badge").focus()
            page.keyboard.press("Enter")
            page.evaluate("advancePet(100)")
            assert page.evaluate("([id,t])=>active===id && elapsed>t", [reply,elapsed]), (view,reply,"restarted")
            if job=="searching":
                wait_frame(job, 0)
                assert page.evaluate("take.target[frame].join()===PIXEL_CLIPS.lensFind.target.at(-1).join()")
        page.close()


def check_page_edge(browser, url):
    for view in ("full", "close"):
        page = browser.new_page(viewport={"width": 960, "height": 840}, has_touch=view=="close")
        install_pet_clock(page)
        page.goto(url.split("?")[0])
        page.wait_for_function("sheet && active==='idle'", polling=20)
        page.locator(f"[data-view={view}]").evaluate("b=>b.click()")
        page.select_option("#agent-state", "reading")
        page.wait_for_function("active==='reading'", polling=20)
        page.evaluate("advancePet(700)")
        x, y, k = point(page, "book")
        dxs = [8, 19, 36, 51, 68, 81, 97, 106, 94, 77, 63, 41, 20, 5]
        if view=="close":
            cdp = page.context.new_cdp_session(page)
            def pointer(phase, xx):
                cdp.send("Input.dispatchTouchEvent", {"type":"touch"+phase,
                    "touchPoints":[] if phase=="End" else [{"x":xx,"y":y}]})
        else:
            def pointer(phase, xx):
                page.mouse.move(xx, y)
                if phase=="Start": page.mouse.down()
                if phase=="End": page.mouse.up()
        pointer("Start", x)
        page.evaluate("advancePet(20)")
        assert page.evaluate("active==='turnStar' && gestures.page.phase==='drag'")
        for dx in dxs:
            pointer("Move", x-dx*k)
            page.evaluate("advancePet(20)")
            error = page.evaluate("""() => {
                const contact=gestures.contact, [edge]=take.book[frame];
                const {sx:now} = lcd.facePoint(view,take,frame,edge,0);
                const displacement=(gestures.pointer.x - take.book[0][0])/(view==='full'?4:2.75);
                return Math.abs(now + displacement - contact.sx);
            }""")
            assert error <= 1.55, (view, dx, error)
        pointer("End", x-dxs[-1]*k)
        page.evaluate("advancePet(1600)")
        assert page.evaluate("active==='reading' && !gestures.moon")
        page.evaluate("""() => {
            window.turnDrawings=[];
            const draw=lcd.draw.bind(lcd);
            lcd.draw=p=>{draw(p);if(p.action==='turnStar')turnDrawings.push(p.frame)};
        }""")
        page.locator("#scene-button").evaluate("b=>b.click()")
        page.evaluate("advancePet(1700)")
        assert page.evaluate("[...new Set(turnDrawings)]") == list(range(25)), view
        assert page.evaluate("active==='readingMoon' && gestures.moon"), view
        page.close()


def check_paper_stows(browser, url):
    page = browser.new_page()
    install_pet_clock(page)
    page.goto(url.split("?")[0])
    page.wait_for_function("sheet && active==='idle'", polling=20)
    page.evaluate("""() => {
        window.drawing = (sheet, n, i) => {
            const c=document.createElement('canvas'); c.width=138; c.height=264;
            const ctx=c.getContext('2d');
            for (let v=0; v<2; v++)
                ctx.drawImage(sheet, i%8*138, (Math.floor(i/8)+v*Math.ceil(n/8))*132,
                    138,132,0,v*132,138,132);
            return ctx.getImageData(0,0,138,264).data;
        };
    }""")
    for job, index, interrupt in [
        ("writing", 2, False), ("writing", 17, True), ("writing", 37, False),
        ("writing", 44, False), ("writing", 47, True), ("writing", 51, False),
        ("writing", 62, False), ("writing", 84, False), ("compact", 2, False),
        ("compact", 11, True), ("compact", 19, False), ("compact", 22, True),
        ("compact", 32, False), ("compact", 55, False)
    ]:
        page.select_option("#agent-state", job)
        page.wait_for_function("job=>active===job", arg=job, polling=20)
        page.evaluate("""async ([job,index]) => {
            for (let i=0;i<400;i++) {
                await advancePet(20);
                if (active===job && !transfer && frame===index) {
                    window.workDrawing=drawing(sheet,take.frames,frame); return;
                }
            }
            throw Error(job+' did not reach frame '+index);
        }""", [job, index])
        if interrupt:
            page.locator("#cheek-button").evaluate("b=>b.click()")
            page.wait_for_function("active.startsWith('cheek') && pausedHands", polling=20)
            page.evaluate("advancePet(160)")
            assert page.evaluate("pausedHands.frame") == index
        page.select_option("#agent-state", "idle")
        page.wait_for_function("active==='idle' && transfer", polling=20)
        assert page.evaluate("""() => {
            const m=transfer.moves[0], actual=drawing(m.sheet,m.clip.frames,0);
            return actual.every((v,i)=>v===workDrawing[i]);
        }"""), (job,index,interrupt)
        reference = {("writing", 44): "stow-writingFace", ("writing", 62): "stow-writingDone",
                     ("compact", 32): "stow-compactDone"}.get((job, index))
        if reference:
            assert page.evaluate("""async name => {
                const m=transfer.moves[0], reference=await loadSheet(name);
                for (let i=0; i<m.clip.frames; i++) {
                    const expected=drawing(reference,m.clip.frames,i);
                    if (!drawing(m.sheet,m.clip.frames,i).every((v,p)=>v===expected[p])) return false;
                }
                return true;
            }""", reference), reference
        # Reopen a partly lowered sheet before taking a new one. The hands must
        # retrace the drawing just displayed, then resume its original job time.
        if index in (17, 11):
            page.evaluate("""async () => {
                await advancePet(135);
                const m=transfer.moves[0];
                window.midFrame=transferFrame(m,transfer.age);
                window.midDrawing=drawing(m.sheet,m.clip.frames,midFrame);
            }""")
            page.select_option("#agent-state", job)
            page.wait_for_function("job=>active===job && transfer?.moves[0]?.reverse", arg=job, polling=20)
            assert page.evaluate("""() => {
                const m=transfer.moves[0], i=transferFrame(m,transfer.age);
                return i===midFrame && drawing(m.sheet,m.clip.frames,i).every((v,j)=>v===midDrawing[j]);
            }""")
            page.evaluate("""async () => { while(transfer) await advancePet(10); }""")
            assert page.evaluate("frame") == index
            page.select_option("#agent-state", "idle")
            page.wait_for_function("active==='idle' && transfer", polling=20)
        page.evaluate("advancePet(600)")
    page.close()


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
                    page.wait_for_function("!dirty")
                    header = page.evaluate("Array.from(lcd.paint.getImageData(60,16,64,5).data).join(',')")
                    outline = mark_outline(page) if activity=="waiting" else None
                    if activity=="waiting":
                        assert outline, view
                    x, y = page.evaluate("""() => {
                      const [x,y]=take.target[frame], b=lcd.canvas.getBoundingClientRect();
                      const close=view==="close", k=close?2.75:4;
                      return [b.left+(23+(x-(close?113:28))/k)*b.width/184,
                              b.top+((close?22:18)+(y-(close?34.5:12))/k)*b.height/184];
                    }""")
                    page.mouse.click(x,y)
                    page.wait_for_function("a=>active===a && !transfer",arg=action)
                    assert page.evaluate("Array.from(lcd.paint.getImageData(60,16,64,5).data).join(',')")==header
                    if activity=="waiting":
                        assert mark_outline(page)==outline, (view, "high-five hid the Agent waiting mark")
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
            sample["session"].update(phase="waiting",activity="none")
            publish()
            page.wait_for_function("active==='waiting' && !transfer && !dirty")
            assert mark_outline(page)
            sample["session"]["character_id"] = "claude"
            publish()
            page.wait_for_function("active==='idle' && telemetry.mode==='OFF'")
            page.wait_for_function("!transfer && !dirty")
            assert not mark_outline(page), "stale Agent mark after disconnect or session change"
            bridge.stdin.close()
            page.wait_for_function("document.querySelector('#connection-label').textContent==='Agent 离线'")
            assert not errors, errors
            check_static_marks(browser, url)
            check_tool_replies(browser, url)
            check_page_edge(browser, url)
            check_paper_stows(browser, url)
            browser.close()
    finally:
        bridge.terminate()
        bridge.wait(timeout=5)
    print("Bridge snapshots, expiry, disconnect, 6 tool contacts, both paper resumes, "
          "14 pixel-exact paper stows, two reversals and seven static state marks passed. Both crops: dragged edge "
          "followed the finger; the automatic turn displayed all 25 frames.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    check(parser.parse_args().chrome)
