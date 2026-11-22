"""Verify that the icon, visible offer and live hit target agree in either crop."""
import argparse
import json

from playwright.sync_api import sync_playwright
from contact_check import PAGE, install_pet_clock


def review(browser, view, touch):
    page = browser.new_page(viewport={"width": 390 if touch else 1080, "height": 850},
                            has_touch=touch, is_mobile=touch)
    errors = []
    page.on("pageerror", lambda err: errors.append(str(err)))
    install_pet_clock(page)
    page.goto(PAGE.as_uri())
    page.wait_for_function("sheet && active === 'idle'", polling=20)
    click = lambda selector: page.locator(selector).evaluate("el=>el.click()")
    step = lambda ms: page.evaluate("ms=>advancePet(ms)", ms)

    def until(condition):
        page.evaluate(f"""async () => {{
            for (let tries=0; tries<450; tries++) {{
                if ({condition}) return;
                await advancePet(20);
            }}
            throw Error({json.dumps(condition)} + ' during ' + active + ' frame ' + frame);
        }}""")

    def icon(selector, name):
        assert page.locator(selector + " canvas").get_attribute("data-icon") == name, (view, selector, name)

    click(f"[data-view={view}]")
    page.evaluate("""() => {
        const draw=lcd.draw.bind(lcd), mark=lcd.offerMark.bind(lcd);
        const ink=document.createElement("canvas");
        ink.width=ink.height=184;
        const ctx=ink.getContext("2d",{willReadFrequently:true});
        lcd.draw=opts=>{window.markHits=[]; draw(opts)};
        lcd.offerMark=(paint,...args)=>{
            mark(paint,...args);
            ctx.clearRect(0,0,184,184); mark(ctx,...args);
            const rect=lcd.canvas.getBoundingClientRect(), data=ctx.getImageData(0,0,184,184).data;
            for(let i=3;i<data.length;i+=4) if(data[i]){
                const n=(i-3)/4;
                const clientX=rect.x+((n%184)+.5)*rect.width/184;
                const clientY=rect.y+(Math.floor(n/184)+.5)*rect.height/184;
                markHits.push([clientX,clientY,hitAt({clientX,clientY})?.part]);
            }
        };
    }""")

    def tap_mark(part):
        pixels = page.evaluate("markHits")
        assert pixels and all(p[2] == part for p in pixels), (view, part, pixels)
        x, y, _ = pixels[len(pixels)//2]
        (page.touchscreen.tap if touch else page.mouse.click)(x, y)

    page.locator("#badge").evaluate("el=>el.scrollIntoView({block:'center'})")
    icon("#scene-button", "pillow")
    icon("[data-action=feed]", "candy")
    feed = "[data-action=feed]"
    for offered_frame in (33, 55, 69):
        click(feed)
        until("active==='feed' && frame===9")
        checkpoint = page.evaluate("[ticket, director.bond]")
        click(feed)
        step(80)
        assert page.evaluate("frame>=9 && !bubbleOffered")
        assert page.evaluate("[ticket, director.bond]") == checkpoint
        until("active==='feed' && frame===27")
        assert not page.evaluate("markHits")
        until(f"bubbleOffered && frame==={offered_frame} && markHits.length>0")
        icon(feed, "bubble")
        origin = page.evaluate("take.bubbles[frame]")
        tap_mark("bubblePop")
        until("active==='bubblePop'")
        assert page.evaluate("burstOrigin") == origin
        icon(feed, "candy")
        checkpoint = page.evaluate("[ticket, director.bond]")
        click(feed)
        assert page.evaluate("[ticket, director.bond]") == checkpoint
        page.evaluate("""async () => {
            const canvas=document.createElement("canvas"); canvas.width=canvas.height=184;
            const ctx=canvas.getContext("2d",{willReadFrequently:true});
            const seen=new Set();
            while (active==="bubblePop") {
                ctx.clearRect(0,0,184,184);
                lcd.sugarBurst(ctx,take,frame,view,burstOrigin);
                const data=ctx.getImageData(0,0,184,184).data;
                const centers=[];
                for(let i=0;i<data.length;i+=4)
                    if(data[i]===255 && data[i+1]===245 && data[i+2]===203 && data[i+3]===255)
                        centers.push([(i/4)%184,Math.floor(i/(184*4))]);
                if(centers.length!==(frame<20 ? 1 : 0)) throw Error("sugar star count at "+frame);
                if(frame===0 || (frame>=11 && frame<20)) {
                    const [x,y]=frame===0 ? burstOrigin : [take.anchors[frame][2]+3,take.anchors[frame][3]-3];
                    const p=lcd.facePoint(view,take,frame,x,y);
                    if(centers[0][0]!==Math.round(p.sx) || centers[0][1]!==Math.round(p.sy))
                        throw Error("sugar star detached at "+frame);
                }
                seen.add(frame);
                await advancePet(20);
            }
            if(seen.size!==24) throw Error("missing burst drawings: "+seen.size);
        }""")
        until("active==='idle'")
    click("[data-action=pat]")
    until("forehead.phase==='wait' && markHits.length>0")
    tap_mark("head")
    step(100)
    assert not page.evaluate("markHits")
    until("active==='idle'")
    click("[data-action=play]")
    until("active==='starWatch'")
    icon("[data-action=play]", "catch")
    icon("#scene-button", "recall")
    page.locator("#badge").focus()
    old_star = page.evaluate("({...starGame.pos})")
    page.keyboard.press("ArrowRight")
    assert page.evaluate("({...starGame.pos})") == old_star
    travel = page.evaluate("""async () => {
        const steps = [];
        for (let i=0; i<25; i++) {
            const before = {...starGame.pos};
            await advancePet(20);
            steps.push(Math.hypot(starGame.pos.sx-before.sx, starGame.pos.sy-before.sy));
            const [fx,fy,nx,ny]=take.anchors[frame];
            const eyes=lcd.facePoint(view,take,frame,(fx+nx)/2,fy+.72*(ny-fy));
            const width=view==="close" ? 23 : 17;
            if (Math.abs(starGame.pos.sx-eyes.sx)<width && Math.abs(starGame.pos.sy-eyes.sy)<8)
                throw new Error("The automatic star route crosses her eyes: "+JSON.stringify([view,eyes,starGame.pos,i]));
        }
        return steps;
    }""")
    assert max(travel) < 10 and sum(travel) > 55, (view, travel)
    click("#scene-button")
    until("active==='idle'")
    icon("[data-action=play]", "firefly")
    icon("#scene-button", "pillow")

    click("[data-action=nose]")
    until("active==='nose'")
    step(120)
    click("[data-action=nose]")
    until("active==='sneeze'")
    # Isolate the painted effect from hair, skin and the plum background.
    frames = page.evaluate("""async () => {
        const layer=document.createElement("canvas");
        layer.width=layer.height=184;
        const ctx=layer.getContext("2d",{willReadFrequently:true});
        const draws={};
        while(active==="sneeze") {
            ctx.clearRect(0,0,184,184);
            lcd.expression(ctx,take,frame,view,active,true,[]);
            const data=ctx.getImageData(0,0,184,184).data;
            let ink=0;
            for(let i=3;i<data.length;i+=4) if(data[i]) ink++;
            draws[frame]=ink;
            await advancePet(20);
        }
        return draws;
    }""")
    assert len(frames) == 32, (view,frames)
    assert [int(f) for f,count in frames.items() if count] == list(range(14,21)), (view,frames)
    assert frames["15"] > frames["20"]

    for state, symbol in (("reading","book"),("running","keys"),("searching","lens"),
                          ("waiting","hand"),("writing","brush"),("compact","brush")):
        page.select_option("#agent-state", state)
        until(f"active==='{state}' && !transfer")
        icon("#scene-button", symbol)
    page.emulate_media(reduced_motion="reduce")
    page.select_option("#agent-state", "idle")
    until("active==='idle' && !transfer")
    click(feed)
    until("bubbleOffered")
    click(feed)
    until("active==='bubblePop' && frame===14")
    step(400)
    assert page.evaluate("active==='bubblePop' && frame===14")
    until("active==='idle'")
    click("[data-action=nose]")
    until("active==='nose'")
    step(120)
    click("[data-action=nose]")
    until("active==='sneeze' && frame===15")
    step(400)
    assert page.evaluate("active==='sneeze' && frame===15"), view
    assert not errors, errors
    page.close()


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=parser.parse_args().chrome)
        for view,touch in (("full",False),("close",True)):
            review(browser,view,touch)
        browser.close()
    print("Both crops: feed replay guard, 3 burst origins across 24 drawings, static nose landing, offer pixels, star flight and sneeze passed.")
