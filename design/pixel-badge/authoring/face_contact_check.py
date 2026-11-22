"""Check painted face contacts and the hand clearance during a nap in both pixel crops."""
import argparse
import base64
import io
from math import dist

import numpy as np
from PIL import Image

from bake import DESIGN, HERE, TAKES, sample_pixels, sync_playwright


CAPTURE = """() => {
  const layer = document.createElement('canvas');
  layer.width = 612; layer.height = 540;
  const ink = layer.getContext('2d', {willReadFrequently:true});
  const palm = window.drawBadgePalm, props = window.drawDragonProps;
  window.drawBadgePalm = (ctx, arm, grip, pass, ...args) => {
    if (pass === 'body' && grip.shape === window.shape) {
      ink.save(); ink.setTransform(ctx.getTransform());
      palm(ink, arm, grip, pass, ...args);
      ink.restore();
    }
    return palm(ctx, arm, grip, pass, ...args);
  };
  window.drawDragonProps = (ctx, p, a, arms) => {
    if (window.napping) {
      ink.save(); ink.setTransform(ctx.getTransform());
      arms.draw(ink, p, 'body');
      ink.restore();
      window.contactFrame = {mouth:a.mouth, hands:[a.left,a.right],
        shoulders:[a.leftShoulder,a.rightShoulder]};
      return props(ctx,p,a,arms);
    }
    const {mouth, nose, forehead} = a, side = window.side;
    // The mesh mouth and nose landmarks are 15 portrait pixels apart.
    // At peak opening the lower lip is seven pixels past the mouth landmark.
    const lower = {x:mouth.x+(mouth.x-nose.x)*7/15, y:mouth.y+(mouth.y-nose.y)*7/15};
    const cheek = {x:nose.x+side*(nose.y-forehead.y)*.52, y:nose.y-side*(nose.x-forehead.x)*.52};
    window.contactFrame = {target:window.shape === 'cover' ? lower : cheek,
      mouth, wrist:a[side < 0 ? 'left' : 'right']};
    return props(ctx,p,a,arms);
  };
  window.capture = (args) => {
    ink.clearRect(0,0,layer.width,layer.height);
    sampleBadgeTake(args);
    return {...window.contactFrame, mask:layer.toDataURL().split(',')[1]};
  };
}"""


def check_nap(page, lookup):
    page.evaluate("window.napping=true; window.shape=null")
    close_scale = 0.75 / 2.75  # Convert source rig units to close-crop pixels.
    rest = None
    for name in ("doze", "peekLeft", "peekRight"):
        fps, n = TAKES[name]
        page.evaluate("o=>startBadgeTake(o)", {"name":name, "scene":"idle"})
        previous = None
        for i in range(n):
            frame = page.evaluate("args=>capture(args)", {"fps":fps, "n":n, "i":i})
            hands = [(p["x"], p["y"]) for p in frame["hands"]]
            assert hands[0][0] < hands[1][0], (name, i, "wrists crossed")
            if previous is not None:
                for start, end in zip(previous, hands):
                    # The opening reach crosses to the cuff in eight drawings.
                    limit = 5.5 if name == "doze" and i <= 13 else 2
                    assert dist(start, end)*close_scale < limit, (name, i, "abrupt wrist step")
            previous = hands
            if name == "doze" and i == 15:
                rest = hands
            if rest is not None:
                mouth = frame["mouth"]
                assert hands[0][0] < mouth["x"] < hands[1][0], (name, i, "hands lost pillow ends")
                for wrist, anchor in zip(hands, rest):
                    assert (wrist[1]-mouth["y"])*close_scale >= 8, (name, i, "grip above lower face")
                    assert dist(wrist, anchor)*close_scale <= 2, (name, i, "sleep/peek hand drift")

            mask = np.asarray(Image.open(io.BytesIO(base64.b64decode(frame["mask"]))).convert("RGBA"))
            for ox, oy, step in ((28, 12, 4.), (113, 34.5, 2.75)):
                ys, xs = np.nonzero(sample_pixels(mask, ox, oy, step, lookup))
                assert len(xs), (name, i, step, "missing sleeves and hands")
                mouth = frame["mouth"]
                x = (.75*mouth["x"]-ox)/step-.5
                y = (.75*mouth["y"]-oy)/step-.5
                clearance = np.hypot(xs-x, ys-y).min()
                # A nap leaves the space in front of her face empty. Independent
                # idle yawns still reach the lower lip and are tested separately.
                assert clearance * step >= 6*2.75, (name, i, step, "hand near mouth", clearance)


def check(chrome):
    lookup = np.full(1 << 24, -1, dtype=np.int16)
    windows = {"cheekLeft": range(9, 14), "cheekRight": range(9, 14),
               "idleYawn": range(10, 14)}
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=chrome)
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame=()=>0")
        page.goto((DESIGN / "badge-prototype.html").as_uri())
        page.wait_for_function("document.querySelector('#gpt-canvas').dataset.ready==='true'", polling=25)
        page.add_script_tag(content=(HERE / "poses.js").read_text(encoding="utf-8"))
        page.evaluate(CAPTURE)
        for name, contact in windows.items():
            cheek = name.startswith("cheek")
            page.evaluate("o=>Object.assign(window,o)", {
                "shape":"brush" if cheek else "cover", "side":1 if name.endswith("Right") else -1})
            page.evaluate("o=>startBadgeTake(o)", {"name":name, "scene":"idle"})
            fps, n = TAKES[name]
            previous = None
            for i in range(n):
                frame = page.evaluate("args=>capture(args)", {"fps":fps, "n":n, "i":i})
                wrist = (frame["wrist"]["x"], frame["wrist"]["y"])
                if cheek and previous:
                    assert dist(wrist, previous)*0.75/2.75 < 6, (name, i, "wrist step")
                previous = wrist
                if i not in contact:
                    continue
                mask = np.asarray(Image.open(io.BytesIO(base64.b64decode(frame["mask"]))).convert("RGBA"))
                for ox, oy, step in ((28, 12, 4.), (113, 34.5, 2.75)):
                    ys, xs = np.nonzero(sample_pixels(mask, ox, oy, step, lookup))
                    assert len(xs), (name, i, step, "missing fingers")
                    def gap(point):
                        x = (.75*point["x"]-ox)/step-.5
                        y = (.75*point["y"]-oy)/step-.5
                        return np.hypot(xs-x, ys-y).min()
                    assert gap(frame["target"]) <= (2.6 if cheek else 1.45), (name, i, step, "contact", gap(frame["target"]))
                    if not cheek:
                        assert gap(frame["mouth"]) > .45, (name, i, step, "mouth hidden")
        check_nap(page, lookup)
        browser.close()
    print("14 cheek/yawn contact frames and all 53 nap/peek frames checked in both native crops.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome")
    check(parser.parse_args().chrome)
