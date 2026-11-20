const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../gestures.js"), "utf8");

function fixture(base = "idle") {
  let now = 0,
    current = base;
  const clips = {
    peekLeft: { frames: 9 },
    peekRight: { frames: 9 },
    turnStar: { frames: 25 },
    turnMoon: { frames: 25 },
  };
  const Controller = vm.runInNewContext(source + "\nBadgeGestures", {
    PIXEL_CLIPS: clips,
    performance: { now: () => now },
    clearTimeout,
    setTimeout,
  });
  const canvas = {
    addEventListener() {},
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

test("a cancelled half-page keeps its chart, a completed turn changes it exactly once", () => {
  const { g, clip } = fixture("reading");
  g.startPage();
  g.page.progress = 0.4;
  g.cancelContact();
  assert.equal(g.sample("turnStar", 120, 120, false), 1);
  g.sample("turnStar", 240, 120, false);
  assert.equal(clip(), "glanceStar");
  assert.equal(g.moon, false);
  g.startPage();
  g.settlePage(true);
  g.sample("turnStar", 420, 420, false);
  assert.equal(clip(), "readingMoon");
  assert.equal(g.moon, true);
  g.startPage();
  assert.equal(clip(), "turnMoon");
});

test("only idle time starts a nap and a missed peek returns to breathing", () => {
  const { g, clip } = fixture();
  g.idleTime("idle", 27999);
  assert.equal(clip(), "idle");
  g.idleTime("idle", 1);
  assert.equal(clip(), "doze");
  g.rouse(-1);
  assert.equal(clip(), "peekLeft");
  g.sample(clip(), 3100, 100, false);
  assert.equal(clip(), "doze");
  assert.equal(g.sample(clip(), 0, 0, false), 15);
  g.rouse(1);
  g.rouse(-1);
  assert.equal(clip(), "wakeRight");
  const work = fixture("reading");
  work.g.idleTime("reading", 45000);
  assert.equal(work.clip(), "reading");
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
