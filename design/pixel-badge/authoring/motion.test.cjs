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
    quiet: 0,
    pointer: false,
    forehead: false,
    star: false,
    sleeping: false,
    transfer: false,
    ...overrides,
  };
}

test("state transitions cue the right expression", () => {
  const { motion } = make();
  assert.equal(motion.setState("waiting").emote.kind, "question");
  assert.equal(motion.setState("error").emote.kind, "exclaim");
  assert.equal(motion.setState("succeeded").emote.kind, "sparkle");
  assert.equal(motion.setState("limited").emote.kind, "ellipsis");
  assert.equal(motion.setState("ended").emote.kind, "ellipsis");
  assert.equal(motion.setState("cancelled").emote.kind, "ellipsis");
  assert.equal(motion.setState("working").emote, null);
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
  assert.equal(motion.setState("waiting").emote.kind, "question");
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

test("working state emits thought dots on its periodic beat", () => {
  const { motion, advance } = make({ random: () => 0 });
  motion.setState("working", { now: 1000 });
  advance(3000);
  motion.tick(0, ctx({ state: "working" }));
  assert.ok(
    motion.activeEmotes().some((emote) => emote.kind === "dots"),
    "expected thought dots",
  );
});

test("expressions expire independently", () => {
  const { motion, advance } = make();
  motion.setState("waiting", { now: 1000 });
  assert.equal(motion.activeEmotes(1000).length, 1);
  advance(800);
  assert.equal(motion.activeEmotes().length, 1);
  advance(500);
  assert.equal(
    motion.activeEmotes().filter((emote) => emote.kind === "question").length,
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
