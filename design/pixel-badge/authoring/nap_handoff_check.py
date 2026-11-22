"""Check the real bridge and drawn native frames when work interrupts a nap."""
import argparse
import json
import subprocess
import sys

from contact_check import PAGE, install_pet_clock, sync_playwright


def seek(page, name, target):
    page.evaluate("""async ([name,target]) => {
        for (let n=0; n<240; n++) {
            if (active===name && !lowering && frame===target) return;
            await advancePet(20);
        }
        throw Error('Did not reach '+name+' frame '+target);
    }""", [name, target])


def review(browser, view, source, target, reduced=False, local=False):
    project = PAGE.parents[2]
    fixture = project / "protocol/fixtures/codex-flow.jsonl"
    events = [json.loads(line) for line in fixture.read_text(encoding="utf-8").splitlines()]
    command = [sys.executable, "-B", str(project/"tools/agent_preview.py"), "--port", "0"]
    with subprocess.Popen(command, stdin=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8") as bridge:
        def publish(index):
            bridge.stdin.write(json.dumps(events[index])+"\n")
            bridge.stdin.flush()

        page = browser.new_page(reduced_motion="reduce" if reduced else "no-preference")
        errors = []
        page.on("pageerror", lambda error: errors.append(str(error)))
        try:
            endpoint = bridge.stderr.readline().strip()
            publish(0)
            install_pet_clock(page)
            page.goto(endpoint+"badge/design/pixel-badge/index.html?agent=1")
            page.wait_for_function("active==='idle' && telemetry.mode==='LIVE'", polling=20)
            page.locator("[data-view="+view+"]").evaluate("e=>e.click()")
            scene = page.locator("#scene-button")
            scene.evaluate("e=>e.click()")
            seek(page, "doze", 15 if source!="doze" else target)
            if source!="doze":
                side = source[4:]
                page.locator("#badge").focus()
                page.keyboard.press("Arrow"+side)
                seek(page, "peek"+side, target if source.startswith("peek") else 8)
                if source.startswith("wake"):
                    scene.evaluate("e=>e.click()")
                    seek(page, source, target)
            # Count a cloth color in the already-composited screen so an orphan
            # landmark or a missing foreground cannot masquerade as a held pillow.
            page.evaluate("""() => {
                window.trace=[];
                const headers=[];
                const draw=lcd.draw.bind(lcd);
                lcd.draw=o=>{
                    draw(o);
                    const rgba=lcd.paint.getImageData(15,75,154,74).data;
                    let cloth=0;
                    for(let i=0; i<rgba.length; i+=4)
                        if(rgba[i]===164 && rgba[i+1]===201 && rgba[i+2]===223) cloth++;
                    const header=Array.from(lcd.paint.getImageData(60,16,64,5).data).join(',');
                    if (!headers.includes(header)) headers.push(header);
                    trace.push([o.action,o.frame,o.code,cloth,performance.now(),headers.indexOf(header)]);
                };
                dirty=true;render();
            }""")
            if local:
                page.locator("[data-action=feed]").evaluate("e=>e.click()")
                page.wait_for_function("active==='feed'", polling=20)
            else:
                # This original fixture order is also the wire sequence.
                publish(2)
                page.wait_for_function("active==='reading'", polling=20)
                if source=="doze" and not reduced:
                    page.evaluate("advancePet(120)")
                    publish(4)
                    page.wait_for_function("active==='waiting'", polling=20)
            page.evaluate("advancePet(1600)")
            trace = page.evaluate("trace")
            assert trace[0][:2] == [source,target] and trace[0][3]>0, trace[:1]
            expected_action = "feed" if local else "waiting" if source=="doze" and not reduced else "reading"
            assert trace[-1][0]==expected_action and not errors, (trace[-1][:4],errors)
            if not local:
                final_code = "WAIT" if expected_action=="waiting" else "READ"
                assert trace[-1][2]==final_code and trace[0][5]!=trace[-1][5]
                assert all(row[5]==trace[-1][5] for row in trace if row[2]==final_code)
            if not reduced:
                stage = [row for row in trace if row[0].startswith(("doze","wake","peek"))]
                steps = [row[:2] for i,row in enumerate(stage) if i==0 or row[:2]!=stage[i-1][:2]]
                if source=="doze":
                    expected = [[source,f] for f in [*range(target,35),*range(15,-1,-1)]]
                elif source.startswith("peek"):
                    expected = [[source,f] for f in range(target,9)]
                    expected += [["wake"+source[4:],f] for f in range(21)]
                else:
                    expected = [[source,f] for f in range(target,21)]
                assert steps==expected, (steps,expected)
                assert stage[0][3] == stage[1][3] and stage[-1][3] == 0, [row[:4] for row in stage]
                assert any(0 < row[3] < stage[0][3] for row in stage), stage
                assert stage[-1][4]-stage[0][4] < 800, stage
                if not local:
                    assert stage[1][2] == "READ", stage[:3]
                if source=="doze":
                    assert all(row[0]!="stow-reading" for row in trace), trace
                    assert any(row[2]=="WAIT" and row[3]>0 for row in stage)
                print(view, source, target, "to empty hands in", stage[-1][4]-stage[0][4],
                      "ms;", stage[0][3], "visible midtone pixels before folding")
            else:
                assert all(row[1]==target for row in trace if row[0]==source), trace
                assert not any(row[3] for row in trace if row[0]!=source), trace
            assert page.evaluate("!lowering && !transfer")
        finally:
            page.close()
            bridge.terminate()


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=parser.parse_args().chrome)
        review(browser,"full","doze",31)
        review(browser,"close","peekRight",5)
        review(browser,"full","peekLeft",0)
        review(browser,"close","wakeRight",0)
        review(browser,"close","wakeLeft",8)
        review(browser,"full","wakeRight",8,local=True)
        review(browser,"close","doze",15,reduced=True)
        browser.close()
