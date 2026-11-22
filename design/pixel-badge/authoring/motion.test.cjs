"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { BadgeMotion } = require("../motion.js");

function make(options = {}) {
  let now = 1000;
  const clock = () => now;
  const storage = options.storage || null;
  const motion = new BadgeMotion({
    now: clock,
    random: options.random || (() => 0.5),
    storage,
    hasClip: options.hasClip || (() => true),
    getClip: options.getClip,
    reduced: options.reduced || false,
  });
  return { motion, clock, advance: (ms) => (now += ms), at: () => now };
}

function ctx(overrides = {}) {
  return {
    state: "idle",
    base: "idle",
    active: "idle",
    intent: "idle",
    quiet: 3200,
    pointer: false,
    forehead: false,
    star: false,
    sleeping: false,
    transfer: false,
    ...overrides,
  };
}

test("seven Agent outcomes light their own emblem; held tools read from the scene", () => {
  const { motion } = make();
  for (const state of ["reading", "running", "searching", "writing", "compact"])
    assert.equal(motion.setState(state).emote, null);
  for (const state of ["waiting", "error", "succeeded", "limited", "ended", "cancelled", "working"]) {
    const { emote } = motion.setState(state);
    assert.equal(emote.zone, "status");
    assert.equal(emote.kind, state);
  }
  assert.equal(motion.snapshot().state, "working");
  assert.equal(motion.setState("working"), null);
});

test("interactions raise bond, mood and emit one expression", () => {
  const { motion } = make();
  const pat = motion.interact("pat");
  assert.equal(pat.bond, 2);
  assert.equal(pat.level, 0);
  assert.equal(pat.emote.kind, "heart");
  assert.ok(pat.mood.affection > 0.5);

  const cheek = motion.interact("cheekLeft");
  assert.equal(cheek.emote.kind, "blush");
  assert.equal(cheek.emote.zone, "cheek");
  assert.equal(cheek.emote.side, -1);

  for (let i = 0; i < 9; i++) motion.interact("brush");
  assert.ok(motion.bond >= 25);
  assert.equal(motion.bondLevel(), 2);
  assert.ok(motion.interact("brush").bond <= 100);
});

test("bond survives a reload and visits increment", () => {
  const store = new Map();
  const storage = {
    getItem: (key) => (store.has(key) ? store.get(key) : null),
    setItem: (key, value) => store.set(key, value),
  };
  const first = make({ storage });
  first.motion.interact("heartHug");
  first.motion.interact("brush");
  const saved = first.motion.bond;
  const second = make({ storage });
  assert.equal(second.motion.bond, saved);
  assert.equal(second.motion.visits, 2);
});

test("idle micro-actions wait for their slot and are not repeated while playing", () => {
  const { motion, advance } = make();
  advance(3000);
  assert.equal(motion.tick(0, ctx()), null);
  advance(3000);
  const first = motion.tick(0, ctx());
  assert.ok(first);
  assert.ok(first.clip);
  assert.ok(first.duration >= 800);
  assert.equal(motion.tick(0, ctx()), null);
  motion.microEnded();
  assert.equal(motion.tick(0, ctx()), null);
  advance(8000);
  assert.ok(motion.tick(0, ctx()));
});

test("micro-actions pause for pointer, invitation, star, sleep and signal priority", () => {
  const { motion, advance } = make();
  advance(8000);
  const cases = [
    { quiet: 0 },
    { quiet: 7000 },
    { pointer: true },
    { forehead: true },
    { star: true },
    { sleeping: true },
    { transfer: true },
    { active: "other" },
    { intent: "other" },
    { state: "working" },
    { base: "reading" },
  ];
  for (const overrides of cases)
    assert.equal(
      motion.tick(0, ctx(overrides)),
      null,
      JSON.stringify(overrides),
    );
  assert.ok(motion.tick(0, ctx()));
});

test("reduced motion suppresses micro-actions but keeps state cues", () => {
  const { motion, advance } = make({ reduced: true });
  advance(20000);
  assert.equal(motion.tick(0, ctx()), null);
  assert.equal(motion.setState("waiting").emote.kind, "waiting");
});

test("recent pointer attention biases the next look", () => {
  const { motion, advance } = make({ random: () => 0.99 });
  motion.setState("idle", { now: 1000 });
  advance(7000);
  motion.pointerAttention(-1, 8000);
  const pick = motion.tick(0, ctx());
  assert.ok(pick);
  assert.equal(pick.clip, "idleLookLeft");
});

test("thinking rekindles the lamp on its periodic beat", () => {
  const { motion, advance } = make({ random: () => 0 });
  motion.setState("working", { now: 1000 });
  advance(3000);
  motion.tick(0, ctx({ state: "working" }));
  assert.ok(
    motion.activeEmotes().some((emote) => emote.kind === "working" && emote.zone === "status"),
    "expected the lamp to light",
  );
});

test("a rebaked gesture gets its entire reel and yields enough time before the next invitation", () => {
  const { motion, advance } = make({
    getClip: id => id === "idleGroomLeft" && { frames: 30, fps: 18 },
  });
  advance(8000);
  assert.equal(motion.tick(0, ctx({ quiet: 5900, nextQuietAt: 7000 })), null);
  const groom = motion.tick(0, ctx({ quiet: 15000, nextQuietAt: 28000 }));
  assert.equal(groom.to, 29);
  assert.equal(groom.duration, 1000 * 30 / 18);
  motion.microEnded();
  advance(11000);
  assert.equal(motion.tick(0, ctx({ quiet: 26500, nextQuietAt: 28000 })), null);
});

test("authored hand impacts earn bond without an early floating hit; a state change retires old cues", () => {
  const { motion, advance } = make({ random: () => 0 });
  motion.setState("waiting");
  assert.equal(motion.activeEmotes()[0].clip, "waiting");
  for (const id of ["palmTap", "keyChime", "lensFind", "heartHug", "feed", "forehead", "sneeze"]) {
    const bond = motion.bond;
    assert.equal(motion.interact(id).emote, null);
    assert.ok(motion.bond > bond);
  }
  motion.setState("reading");
  advance(3000);
  motion.tick(0, ctx({ state: "reading", base: "readingMoon", active: "turnMoon", intent: "turnMoon" }));
  assert.equal(motion.activeEmotes().length, 0);
  motion.tick(0, ctx({ state: "reading", base: "readingMoon", active: "readingMoon", intent: "readingMoon" }));
  assert.equal(motion.activeEmotes()[0].clip, "readingMoon");
  motion.setState("error");
  assert.deepEqual(motion.activeEmotes().map(e => e.kind), ["error"]);
});

test("expressions expire independently", () => {
  const { motion, advance } = make();
  motion.setState("waiting", { now: 1000 });
  assert.equal(motion.activeEmotes(1000).length, 1);
  advance(400);
  assert.equal(motion.activeEmotes().length, 1);
  advance(500);
  assert.equal(
    motion.activeEmotes().filter((emote) => emote.kind === "waiting").length,
    0,
  );
});

test("micro-actions fall back when the new atlases are not baked yet", () => {
  const { motion, advance } = make({
    hasClip: (id) => id === "tail" || id === "wing" || id === "wingLeft",
  });
  advance(8000);
  const pick = motion.tick(0, ctx());
  assert.ok(pick);
  assert.ok(["tail", "wing", "wingLeft"].includes(pick.clip));
});
