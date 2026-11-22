const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../gestures.js"), "utf8");
const baked = {};
vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../gesture-clips.js"), "utf8"),
  { window: { PIXEL_CLIPS: baked } });

function fixture(base = "idle") {
  let now = 0,
    current = base;
  const clips = {
    cheekLeft: { fps: 16, frames: 22 },
    cheekRight: { fps: 16, frames: 22 },
    peekLeft: { frames: 9 },
    peekRight: { frames: 9 },
    turnStar: baked.turnStar,
    turnMoon: baked.turnMoon,
  };
  const Controller = vm.runInNewContext(source + "\nBadgeGestures", {
    PIXEL_CLIPS: clips,
    performance: { now: () => now },
    clearTimeout,
    setTimeout,
  });
  const canvas = {
    addEventListener() {},
    focus() {},
    setPointerCapture() {},
    hasPointerCapture() {
      return false;
    },
  };
  const api = {
    base: () => base,
    intent: () => current,
    show: (id) => {
      current = id;
    },
    hit: (e, unbounded) => (e.point.inside || unbounded ? e.point : null),
    care: (id) => {
      current = id;
    },
    note() {},
    restore: () => {
      current = g.moon && base === "reading" ? "readingMoon" : base;
    },
  };
  const g = new Controller(canvas, api);
  return {
    g,
    clip: () => current,
    at: (t) => {
      now = t;
    },
    pointer(kind, part, x = 0, y = 0, side = 1, inside = true) {
      g[kind]({
        pointerId: 7,
        isPrimary: true,
        button: 0,
        preventDefault() {},
        point: { part, x, y, side, inside, sx: x + 80, sy: y + 80 },
      });
    },
  };
}

test("a third cheek contact must fall inside the rolling 1.1 second window", () => {
  const { g, clip, at } = fixture();
  g.poke(-1);
  assert.equal(clip(), "cheekLeft");
  at(550);
  g.poke(1);
  at(1100);
  g.poke(-1);
  assert.equal(clip(), "cheekLeft");
  at(1150);
  g.poke(1);
  assert.equal(clip(), "tickle");
  g.interrupt();
  g.poke(-1);
  assert.equal(clip(), "cheekLeft");
});

test("a cancelled half-page returns quietly, a completed turn changes chart exactly once", () => {
  const { g, clip } = fixture("reading");
  g.startPage();
  g.page.progress = 0.4;
  g.cancelContact();
  assert.equal(g.sample("turnStar", 120, 120, false), 7);
  g.sample("turnStar", 420, 300, false);
  assert.equal(clip(), "reading");
  assert.equal(g.moon, false);
  g.startPage();
  g.settlePage(true);
  const seen = new Set();
  for (let ms = 0; ms <= 1500; ms += 20)
    seen.add(g.sample("turnStar", ms, ms === 0 ? 0 : 20, false));
  assert.deepEqual([...seen], Array.from({ length: 25 }, (_, i) => i));
  assert.equal(clip(), "readingMoon");
  assert.equal(g.moon, true);
  g.startPage();
  assert.equal(clip(), "turnMoon");
});

test("the free edge follows a finger forward and back through both charts", () => {
  const { g, pointer } = fixture("reading");
  for (const [id, moon] of [["turnStar", false], ["turnMoon", true]]) {
    g.moon = moon;
    pointer("down", "book");
    const edge = baked[id].book, right = edge[0][0], left = edge.at(-1)[0];
    for (const direction of [-1, 1]) {
      for (let i = 0; i <= right - left; i++) {
        const x = direction < 0 ? right - i : left + i;
        pointer("move", "book", x - right);
        const frame = g.sample(id, 0, 0, false);
        assert.ok(Math.abs(edge[frame][0] - x) <= 4, id + " edge at " + x);
      }
    }
    g.interrupt();
  }
});

test("the cheek reacts on contact, holds pressure and only rebounds after release", () => {
  const { g, clip, pointer, at } = fixture();
  pointer("down", "cheek", 0, 0, -1);
  assert.equal(clip(), "cheekLeft");
  assert.equal(g.sample(clip(), 125, 125, false), 2);
  assert.equal(g.sample(clip(), 3000, 2875, false), 3);
  at(3000);
  pointer("up", "cheek", 0, 0, -1);
  assert.equal(g.cheekTimes.length, 0);
  assert.equal(g.sample(clip(), 3100, 100, false), 4);
  assert.equal(g.contact.phase, "release");
  g.idleTime(clip(), 180);
  assert.equal(g.contact, null);
  g.sample(clip(), 5100, 2000, false);
  assert.equal(clip(), "idle");
});

test("two deliberate cheek reversals cause one tickle, jitter and dragging off the face do not", () => {
  const { g, clip, pointer } = fixture();
  const reactions = [];
  g.io.poke = (id) => reactions.push(id);
  pointer("down", "cheek");
  for (const x of [2, -2, 3, -4, 1]) pointer("move", "cheek", x);
  assert.equal(clip(), "cheekRight");
  for (const x of [20, -5, 17]) pointer("move", "cheek", x);
  assert.equal(clip(), "tickle");
  pointer("up", "cheek", 17);
  assert.deepEqual(reactions, ["cheekRight", "tickle"]);
  assert.equal(g.cheekTimes.length, 0);
  pointer("down", "cheek");
  pointer("move", "nose", -55);
  assert.equal(g.contact, null);
  pointer("up", "cheek");
  assert.equal(g.cheekTimes.length, 0);
});

test("a tickled cheek leaves its last contact on the skin while the finger continues moving", () => {
  const { g, clip, pointer } = fixture();
  pointer("down", "cheek");
  for (const x of [20, -5, 17]) pointer("move", "cheek", x);
  assert.equal(clip(), "tickle");
  assert.equal(g.contact.phase, "release");
  const skin = [g.contact.sx, g.contact.sy];
  pointer("move", "head", -40, -50);
  g.idleTime("tickle", 120);
  assert.deepEqual([g.contact.sx, g.contact.sy], skin);
  pointer("up", "head", -40, -50);
  assert.equal(g.contact.age, 120);
  g.idleTime("tickle", 60);
  assert.equal(g.contact, null);
});

test("three quick physical cheek taps still trigger tickle", () => {
  const { clip, pointer, at } = fixture();
  for (const t of [10, 340, 670]) {
    at(t);
    pointer("down", "cheek");
    at(t + 70);
    pointer("up", "cheek");
  }
  assert.equal(clip(), "tickle");
});

test("two forehead reversals earn a voluntary idle release hug; cancellation or a job suppresses it", () => {
  for (const [base, finish, expected] of [
    ["idle", "up", "heartHug"],
    ["idle", "cancel", "idle"],
    ["working", "up", "working"],
  ]) {
    const { g, clip, pointer } = fixture(base);
    pointer("down", "head");
    for (const x of [30, -12, 22]) pointer("move", "head", x);
    assert.equal(g.brush.turns, 2);
    assert.equal(clip(), "brush");
    if (finish === "up") pointer("up", "head", 22);
    else g.cancelContact();
    g.sample("brush", 700, 420, false);
    assert.equal(clip(), expected);
  }
});

test("recontact interrupts brush release without snapping back to the old scene", () => {
  const { g, clip, pointer } = fixture();
  pointer("down", "head");
  pointer("move", "head", 36);
  g.sample(clip(), 210, 210, false);
  pointer("up", "head", 36);
  g.sample(clip(), 350, 140, false);
  const depth = g.brush.depth;
  pointer("down", "head", 34);
  assert.equal(g.brush.depth, depth);
  assert.equal(g.brush.releaseMs, null);
  g.sample(clip(), 600, 500, false);
  assert.equal(clip(), "brush");
  g.cancelContact();
});

test("sliding an offered forehead keeps the same finger and finishes one nuzzle on a pause", () => {
  const { g, clip, pointer } = fixture();
  g.io.acceptForehead = () => true;
  g.io.forehead = {
    move: (distance) => distance > 8 ? "brush" : null,
    endStroke: () => false,
  };
  pointer("down", "head");
  const contact = g.pointer;
  pointer("move", "head", 5);
  assert.equal(g.pointer.part, "offeredHead");
  pointer("move", "head", 16);
  assert.equal(g.pointer, contact);
  g.joinBrush(25);
  assert.equal(clip(), "brush");
  assert.equal(g.brush.depth, 0.5);
  assert.equal(g.brush.position, 0.75);
  assert.equal(g.brush.fromForehead, true);
  for (let i = 0; i < 8; i++) g.sample("brush", i * 16, 16, false);
  const moving = g.brush.position;
  for (let i = 8; i < 80; i++) g.sample("brush", i * 16, 16, false);
  const resting = g.brush.position;
  assert.ok(resting > moving + 0.2);
  g.sample("brush", 1400, 120, false);
  assert.ok(Math.abs(g.brush.position - resting) < 0.001);
  pointer("move", "head", -18);
  assert.equal(g.brush.still, 0);
  g.sample("brush", 1450, 50, false);
  assert.ok(g.brush.position < resting);
  pointer("up", "head", -18);
  assert.equal(g.brush.target, g.brush.position);
});

test("a vertical book swipe cannot commit a turn after crossing the horizontal threshold", () => {
  const { g, clip, pointer } = fixture("reading");
  pointer("down", "book");
  pointer("move", "", -66, 60);
  pointer("up", "", -66, 60);
  g.sample("turnStar", 500, 500, false);
  assert.equal(g.moon, false);
  assert.equal(clip(), "reading");
  pointer("down", "book");
  pointer("move", "", -41, 5);
  pointer("up", "", -41, 5);
  g.sample("turnStar", 500, 500, false);
  assert.equal(clip(), "glanceStar");
});

test("sleep ignores scenery and a tap landing on a different prop cannot activate the original", () => {
  const { g, clip, pointer } = fixture();
  g.nap();
  pointer("down", "");
  pointer("up", "");
  assert.equal(g.pointer, null);
  assert.equal(clip(), "doze");
  g.sample("doze", 1500, 0, false);
  pointer("down", "cheek");
  pointer("up", "cheek");
  assert.equal(clip(), "peekRight");
  g.rouse(1);
  g.rouse(1);
  for (let i = 0; i < 9; i++) g.sample(clip(), i * 22, 22, false);
  assert.equal(clip(), "wakeRight");
  pointer("down", "nose");
  pointer("up", "cheek", 3);
  assert.equal(clip(), "wakeRight");
});

test("only idle time starts a nap and a missed peek returns to breathing", () => {
  const { g, clip } = fixture();
  g.idleTime("idle", 27999);
  assert.equal(clip(), "idle");
  g.idleTime("idle", 1);
  assert.equal(clip(), "doze");
  g.sample("doze", 1500, 0, false);
  g.rouse(-1);
  assert.equal(clip(), "peekLeft");
  g.sample(clip(), 3100, 100, false);
  assert.equal(clip(), "doze");
  assert.equal(g.sample(clip(), 0, 0, false), 15);
  g.rouse(1);
  g.rouse(-1);
  for (let i = 0; i < 9; i++) g.sample(clip(), i * 22, 22, false);
  assert.equal(clip(), "wakeRight");
  const work = fixture("reading");
  work.g.idleTime("reading", 45000);
  assert.equal(work.clip(), "reading");
});

test("interrupt each yawn drawing without skipping a jaw pose or opening it twice", () => {
  for (let from = 0; from < 15; from++) {
    const { g, clip } = fixture();
    g.nap();
    assert.equal(g.sample("doze", from * 100, 0, false), from);
    g.rouse(-1);
    const to = from <= 6 ? 0 : 15;
    if (from !== to) {
      for (let i = 0; i <= Math.abs(to - from); i++)
        assert.equal(
          g.sample("doze", 0, i === 0 ? 0 : 22, false),
          from + i * Math.sign(to - from),
        );
      g.sample("doze", 0, 16, false);
    }
    assert.equal(clip(), from <= 6 ? "idle" : "peekLeft");
  }
});

test("a rapid second touch finishes the visible peek before stretching from the same side", () => {
  for (const side of [-1, 1])
    for (let from = 0; from < 9; from++) {
      const { g, clip } = fixture();
      g.nap();
      g.sample("doze", 1500, 0, false);
      g.rouse(side);
      const peek = clip();
      assert.equal(g.sample(peek, (from * 460) / 8, 0, false), from);
      g.rouse(-side);
      if (from < 8) {
        assert.equal(clip(), peek);
        for (let i = from; i <= 8; i++)
          assert.equal(g.sample(peek, 0, i === from ? 0 : 22, false), i);
        g.sample(peek, 0, 16, false);
      }
      assert.equal(clip(), side < 0 ? "wakeLeft" : "wakeRight");
    }
});

test("a double call during a sleeping breath survives the switch to the peek atlas", () => {
  for (let from = 15; from < 35; from++) {
    const { g, clip } = fixture();
    g.nap();
    g.sample("doze", 100 * from, 0, false);
    g.rouse(-1);
    g.rouse(1);
    if (from !== 15) {
      let previous = from,
        finished = false;
      for (let i = 0; i < 12; i++) {
        const f = g.sample("doze", 0, 22, false);
        const distance = Math.abs(previous - f);
        assert.ok(distance <= 1 || distance === 19);
        previous = f;
        if (clip() === "peekLeft") {
          finished = true;
          break;
        }
      }
      assert.ok(finished);
      assert.equal(previous, 15);
    }
    assert.equal(g.sample("peekLeft", 0, 0, false), 0);
    for (let i = 1; i <= 8; i++)
      assert.equal(g.sample("peekLeft", 0, 22, false), i);
    g.sample("peekLeft", 0, 16, false);
    assert.equal(clip(), "wakeLeft");
  }
});

test("a release or blur lets a held stroke settle and return to the base state", () => {
  const { g, clip } = fixture("working");
  g.startBrush(1);
  g.brush.stroked = true;
  g.brush.target = 1;
  assert.equal(g.sample("brush", 250, 250, true), 44);
  g.cancelContact();
  g.sample("brush", 530, 280, true);
  g.sample("brush", 730, 200, true);
  assert.equal(clip(), "working");
  assert.equal(g.brush, null);
});
