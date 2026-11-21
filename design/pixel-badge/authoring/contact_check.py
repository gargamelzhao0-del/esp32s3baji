"""Replay real mouse and touch input against the standalone pixel badge."""
import argparse
from pathlib import Path

from playwright.sync_api import sync_playwright


PAGE = Path(__file__).resolve().parents[1] / "index.html"


def point(page, part, scroll=True):
    if scroll:
        page.locator("#badge").scroll_into_view_if_needed()
    # Invert the renderer's current camera, including the zoom selected by DPR.
    return page.evaluate("""part => {
        const b=lcd.canvas.getBoundingClientRect(), a=take.anchors[frame];
        const source=(x, y)=>lcd.sourcePoint({clientX:x,clientY:y},view,take,frame,true);
        const zero=source(b.left,b.top), horizontal=source(b.right,b.top);
        const k=b.width/(horizontal.x-zero.x);
        const xy=part==='book' ? take.book[frame] : part==='head' ? a.slice(0,2) :
            [a[2]+(a[3]-a[1])*.52,a[3]-(a[2]-a[0])*.52];
        return [b.left+(xy[0]-zero.x)*k,b.top+(xy[1]-zero.y)*k,k];
    }""", part)


def wait_clip(page, name):
    page.wait_for_function("id=>active===id && !transfer", arg=name)


def check_waking(page, finger):
    page.evaluate("""() => {
        window.wakingFrames=[];window.wakingUps=[];
        const paint=lcd.draw.bind(lcd);
        lcd.draw=options=>{
            wakingFrames.push([options.action,options.frame,performance.now()]);
            paint(options);
        };
        const release=()=>wakingUps.push([active,frame,performance.now()]);
        lcd.canvas.addEventListener('pointerup',release,true);
        window.finishWakeCheck=()=>{
            lcd.draw=paint;
            lcd.canvas.removeEventListener('pointerup',release,true);
        };
    }""")
    delays = []
    for early in (True,False):
        page.locator("#scene-button").evaluate("e=>e.click()")
        wait_clip(page,"doze")
        # Take the cheek coordinate in advance so scrolling cannot consume a pose.
        x,y,_ = point(page,"cheek")
        target = 3 if early else 8
        page.wait_for_function("i=>active==='doze' && frame===i",arg=target)
        page.evaluate("wakingFrames.length=0; wakingUps.length=0")
        finger("Start",x,y)
        finger("End")
        if not early:
            wait_clip(page,"peekRight")
            x,y,_ = point(page,"cheek")
            finger("Start",x,y)
            finger("End")
            wait_clip(page,"wakeRight")
        wait_clip(page,"idle")
        frames, releases = page.evaluate("[wakingFrames,wakingUps]")
        for clip,pressed,stamp in releases:
            first = next(t for name,index,t in frames if t>=stamp and (clip!=name or index!=pressed))
            delays.append(round(first-stamp))
        if early:
            assert all(name in ("doze","idle") for name,_,_ in frames)
            assert [f for name,f,_ in frames if name=="doze"][-1]==0
        else:
            boundary = lambda clip: [f for name,f,_ in frames if name==clip]
            assert boundary("doze")[-1]==15 and boundary("peekRight")[0]==0
            assert boundary("peekRight")[-1]==8 and boundary("wakeRight")[0]==0
    page.evaluate("finishWakeCheck()")
    assert max(delays)<230,delays
    print("First changed drawing after each wake contact (ms):",delays)


def check_invitations(browser):
    page = browser.new_page()
    page.add_init_script("""{
        let pending=new Map(), uid=0, t=0;
        window.requestAnimationFrame=f=>{pending.set(++uid,f);return uid};
        window.cancelAnimationFrame=id=>pending.delete(id);
        window.advancePet=ms=>{for(let n=0;n<ms;n+=20){
            const callbacks=pending;pending=new Map();t+=20;
            for(const fn of callbacks.values())fn(t);
        }};
    }""")
    def wait(name):
        page.wait_for_function("id=>document.querySelector('#badge')?.dataset.clip===id",
                               arg=name,polling=20,timeout=4500)
    def advance(ms):
        page.evaluate("ms=>advancePet(ms)",ms)
    page.goto(PAGE.as_uri()+"?observe=1")
    wait("idle")
    assert not page.locator(".side").is_visible()
    advance(7250)
    wait("browInviteLeft")
    advance(1600)
    x, y, scale = point(page, "head", scroll=False)
    page.mouse.move(x, y)
    page.mouse.down()
    advance(1180)
    assert page.evaluate("forehead.phase==='rest' && frame===36")
    page.mouse.move(x + 16*scale, y)
    advance(80)
    assert page.evaluate("forehead.phase==='joining' && frame>83 && frame<88")
    page.mouse.up()
    assert page.evaluate("forehead.phase==='unjoin' && !gestures.brush")
    advance(2500)
    wait("idle")
    page.locator("#observation-link").evaluate("e=>e.click()")
    wait("idle")
    assert page.locator(".side").is_visible()
    hand = page.locator("[data-action=pat]")
    hand.evaluate("e=>e.click()")
    wait("browInviteLeft")
    hand.evaluate("e=>e.click()")
    advance(2580)
    assert page.evaluate("forehead.phase==='rest'")
    advance(2200)
    wait("idle")
    print("First invitation, contact at the middle fringe, mid-stroke lift, and early button acceptance passed.")
    page.locator("[data-action=play]").evaluate("e=>e.click()")
    wait("starWatch")
    page.keyboard.press("ArrowRight")
    page.keyboard.press("Enter")
    advance(360)
    wait("starCatchRight")
    advance(2200)
    wait("idle")
    advance(11200)
    assert page.evaluate("active==='idle'")
    advance(500)
    wait("starWatch")
    assert page.evaluate("starGame.hand===-1 && !wantsRematch")
    advance(14700)
    wait("idle")
    assert page.evaluate("gestures.quiet>=25000 && !wantsRematch")
    advance(2600)
    wait("doze")
    page.close()
    print("An earned invitation visits the opposite side once; ignoring it preserves nap time.")


def exercise(browser, touch):
    page = browser.new_page(
        viewport={"width": 390 if touch else 1240, "height": 850},
        is_mobile=touch, has_touch=touch, device_scale_factor=2 if touch else 1,
    )
    page.set_default_timeout(6500)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto(PAGE.as_uri())
    wait_clip(page, "idle")
    page.locator("[data-view=close]" if touch else "[data-view=full]").click()
    cdp = page.context.new_cdp_session(page) if touch else None

    def finger(kind, x=0, y=0):
        if touch:
            points = [{"x": x, "y": y, "id": 1}] if kind in ("Start", "Move") else []
            cdp.send("Input.dispatchTouchEvent", {"type": "touch" + kind, "touchPoints": points})
        elif kind == "Start":
            page.mouse.move(x, y)
            page.mouse.down()
        elif kind == "Move":
            page.mouse.move(x, y, steps=3)
        else:
            page.mouse.up()

    check_waking(page,finger)
    x, y, k = point(page, "cheek")
    finger("Start", x, y)
    wait_clip(page, "cheekRight")
    page.wait_for_function("frame===3")
    page.wait_for_timeout(600)
    assert page.evaluate("frame===3 && gestures.cheek.held && gestures.contact.phase==='down'")
    finger("End")
    page.wait_for_function("active==='cheekRight' && frame>=5")
    wait_clip(page, "idle")

    x, y, k = point(page, "cheek")
    finger("Start", x, y)
    wait_clip(page, "cheekRight")
    for offset in [23, -9, 20]:
        finger("Move", x + offset*k,y)
    wait_clip(page, "tickle")
    finger("End")
    wait_clip(page, "idle")

    for state, cancelled, result in [("idle", False, "heartHug"), ("working", False, "working"), ("idle", True, "idle")]:
        page.select_option("#agent-state", state)
        wait_clip(page, state)
        x, y, k = point(page, "head")
        finger("Start", x, y)
        for offset in [34, -12, 23]:
            finger("Move", x + offset*k,y)
        wait_clip(page, "brush")
        page.wait_for_function("gestures.brush.turns===2")
        if cancelled and touch:
            finger("Cancel")
        else:
            if cancelled:
                page.locator("#badge").blur()
            finger("End")
        wait_clip(page, result)
        assert page.evaluate("!gestures.pointer && !gestures.brush")
        if result == "heartHug":
            wait_clip(page, "idle")

    page.select_option("#agent-state", "reading")
    wait_clip(page, "reading")
    x, y, k = point(page, "book")
    finger("Start", x, y)
    wait_clip(page, "turnStar")
    finger("Move", x - 84*k, y + 53*k)
    finger("End")
    wait_clip(page, "reading")
    assert page.evaluate("!gestures.moon && !gestures.page")

    page.select_option("#agent-state","idle")
    wait_clip(page,"idle")
    page.locator("[data-action=play]").click()
    wait_clip(page,"starWatch")
    page.locator("#badge").scroll_into_view_if_needed()
    def toy_point(target):
        return page.evaluate("""target => {
            const b=lcd.canvas.getBoundingClientRect(), k=b.width/184;
            const p=target==='star' ? starGame.pos : typeof target==='number' ?
                palmsAt(take,frame)[target] : target;
            return [b.left+k*p.sx,b.top+k*p.sy];
        }""",target)

    finger("Start",*toy_point("star"))
    finger("Move",*toy_point({"sx":137,"sy":60}))
    page.wait_for_function("frame%9===8 && starGame.phase==='held'")
    finger("Move",*toy_point({"sx":47,"sy":60}))
    page.wait_for_function("frame%9===0 && !gestures.pointer")
    page.wait_for_timeout(160)
    finger("End")
    page.wait_for_function("starGame.phase==='invite'")
    hand = 1 if touch else 0
    finger("Start",*toy_point("star"))
    finger("Move",*toy_point(hand))
    finger("End")
    wait_clip(page,"starCatchRight" if touch else "starCatchLeft")
    assert page.evaluate("starGame.finger===null && wantsRematch")
    wait_clip(page,"idle")
    page.emulate_media(reduced_motion="reduce")
    page.locator("[data-action=play]").click()
    wait_clip(page,"starWatch")
    page.locator("#badge").focus()
    page.keyboard.press("ArrowLeft")
    page.keyboard.press("Enter")
    wait_clip(page,"starCatchLeft")
    assert not errors, errors
    page.close()
    print(("Mobile touch / close" if touch else "Desktop mouse / full") +
          ": cheek hold/rebound, rubbing, voluntary hug, working suppression, cancellation, book slip, star tracking, missed drop, palm catch and reduced-motion keyboard catch passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome", help="Path to a local Chromium or Chrome executable")
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=parser.parse_args().chrome)
        try:
            exercise(browser, False)
            exercise(browser, True)
            check_invitations(browser)
        finally:
            browser.close()
