"""Check solved hands against the paper, key, pen and lens in the actual authoring rig."""
import argparse
from math import dist

from bake import DESIGN, HERE, JOBS, SETTLE, TAKES, TOOL_ACTIONS, book_art, sync_playwright


def check(chrome):
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=chrome)
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame=()=>0")
        page.goto((DESIGN / "badge-prototype.html").as_uri())
        page.wait_for_function("document.querySelector('#gpt-canvas').dataset.ready==='true'", polling=25)
        page.add_script_tag(content=(HERE / "poses.js").read_text(encoding="utf-8"))
        page.add_script_tag(content=book_art())
        page.evaluate("""() => {
          // The close camera puts display row 149, the opaque meters, at
          // source y=383.75. Paint the actual objects to an isolated strip
          // under that boundary, including the bookmark and pen barrel.
          const strip = document.createElement("canvas");
          strip.width = 612; strip.height = 156;
          const mask = strip.getContext("2d",{willReadFrequently:true});
          for (const key of ["book","pad","quill","tickets","terminal"]) {
            const draw = BadgeWorkArt[key];
            BadgeWorkArt[key] = (c,...args) => {
              if (window.checkMeters) {
                const m = c.getTransform();
                mask.save(); mask.setTransform(m.a,m.b,m.c,m.d,m.e,m.f-384);
                draw(mask,...args); mask.restore();
              }
              return draw(c,...args);
            };
          }
          const paint = window.drawDragonProps;
          window.drawDragonProps = (ctx, p, a, arms) => {
            const xy = (v, x=0, y=0) => [v.x+x, v.y+y];
            const page = p.writing && DragonWriting.layout(a.left, p.writing, p.penPark);
            window.handData = {
              left:xy(a.left), right:xy(a.right), mouth:xy(a.mouth), weight:p.stage.workMix,
              pinch:xy(a.right,-11,-7), leaf:xy(badgeLeafEdge(a.left,p.work?.progress || 0)),
              fingertip:xy(a.right,-13,14), key:xy(badgeKey(a.left,p.badgeContact)),
              keyRest:xy(badgeKey(a.left)),
              pressure:p.badgeContact, ticket:xy(badgeTicket(a.left,2,p.jobPhase),130,18),
              pen:page && [page.tipX+10,page.tipY-17],
              ring:xy(a.right,badgeLensOffset.x,badgeLensOffset.y), eye:xy(a.eyes,49,3),
            };
            window.checkMeters = p.stage.workMix >= 1;
            mask.clearRect(0,0,strip.width,strip.height);
            paint(ctx,p,a,arms);
            if (window.checkMeters) {
              const ink = mask.getImageData(0,0,strip.width,strip.height).data;
              window.handData.hitsMeters = ink.some((v,i) => i%4===3 && v>=128);
            }
          }
        }""")
        names = ("reading", "readingMoon", "turnStar", "turnMoon", "glanceStar", "glanceMoon",
                 "working", "running", "keyChime", "compact", "writing", "searching",
                 "lensFind", "waiting", "palmTap", "stow-reading", "stow-writing",
                 "stow-writingFace", "stow-writingDone", "stow-compact", "stow-compactDone", "stow-running")
        scanned = 0
        for name in names:
            stowing = name.startswith("stow-")
            scene = name[5:] if stowing else TOOL_ACTIONS.get(name, name if name in JOBS else "idle")
            fps, n = TAKES.get(name, (24, 8))
            page.evaluate("args=>startBadgeTake(args)", {"name": name, "scene": scene})
            rows = page.evaluate("""({fps,n,settle}) => Array.from({length:n},(_,i) => {
              sampleBadgeTake({i,n,fps,settle});
              return window.handData;
            })""", {"fps":fps, "n":n, "settle":SETTLE.get(name)})
            for i, d in enumerate(rows):
                assert not d.get("hitsMeters"), (name,i,"held prop covered by close-crop meters")
                if d["weight"] <= 0.01:
                    continue
                checks = []
                if name.startswith(("turn", "reading", "glance", "stow-reading")) and i/(n-1) <= (0.63 if name.startswith("turn") else 1):
                    checks.append(("pinch", "leaf", 0.05))
                if name.startswith("turn") and abs(d["right"][0]-d["mouth"][0]) < 32:
                    # The pinch extends 13 units above its wrist. Keep the
                    # mouth clear on the turn and the free hand's return.
                    assert d["right"][1]-13 > d["mouth"][1]+3, (name,i,"hand covers mouth")
                if name.startswith("writing") or scene.startswith("writing"):
                    checks.append(("right", "pen", 0.05))
                if name in ("compact", "stow-compactDone") and (stowing or i >= 6):
                    checks.append(("right", "ticket", 0.05))
                if name in ("running", "keyChime"):
                    gap = d["key"][1] - d["fingertip"][1]
                    depression = d["key"][1] - d["keyRest"][1]
                    assert gap >= -0.05 and depression >= 0, (name,i,gap,depression)
                    if depression > 0:
                        assert abs(gap) < 0.05, (name,i,"cap moved before contact",gap)
                    assert abs(gap - depression - (20-26*d["pressure"])) < 0.05, (name,i,gap)
                    assert abs(d["key"][0]-d["fingertip"][0]) < 0.05, (name,i)
                if name == "lensFind" and 8 <= i <= 12:
                    checks.append(("ring", "eye", 0.05))
                if name == "palmTap":
                    assert dist(rows[0]["left"], d["left"]) <= 5.2, (name,i,d["left"])
                for a,b,limit in checks:
                    assert dist(d[a],d[b]) <= limit, (name,i,a,b,d[a],d[b])
            scanned += n
            if name in ("running","keyChime"):
                pressed = [row["key"][1]-row["keyRest"][1] >= 1 for row in rows]
                assert sum(down and (i==0 or not pressed[i-1]) for i,down in enumerate(pressed)) == 2, name
        browser.close()
        print(f"{scanned} original rig frames across {len(names)} work takes checked.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    check(parser.parse_args().chrome)
