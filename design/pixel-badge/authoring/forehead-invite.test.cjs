const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const Invite = vm.runInNewContext(
  fs.readFileSync(path.join(__dirname, "../forehead-invite.js"), "utf8") +
    "\nBadgeForeheadInvite",
);
const frameMs = 1000 / 16;
function waiting(side = -1) {
  const invite = new Invite();
  invite.start(side, { quiet: 7500 });
  invite.tick(24 * frameMs);
  assert.equal(invite.frame, 23);
  return invite;
}

test("an unanswered first invitation tidies once and preserves the nap deadline", () => {
  const invite = new Invite();
  assert.equal(invite.canOffer(6999), false);
  assert.equal(invite.canOffer(7000), true);
  // Enabling motion late in the quiet period must not postpone her nap.
  assert.equal(invite.canOffer(27000), false);
  assert.ok(invite.start(-1, { quiet: 7000 }));
  assert.equal(invite.clip, "browInviteLeft");
  invite.tick(1500);
  assert.equal(invite.phase, "wait");
  invite.tick(3700);
  assert.equal(invite.frame, 37);
  invite.tick(1300);
  assert.equal(invite.clip, null);
  assert.equal(invite.quietSpent, 13500);
  invite.tick(1000);
  assert.equal(invite.quietSpent, 13500);
  assert.equal(invite.canOffer(20000), false);
  invite.rearm();
  assert.equal(invite.quietSpent, 0);
  assert.equal(invite.canOffer(7000), true);
});

test("an early touch lets the drawn hand lower; an early lift reverses from there", () => {
  const invite = new Invite();
  invite.start(1);
  invite.tick(4 * frameMs);
  assert.ok(invite.touch(1));
  invite.tick(frameMs);
  const shown = invite.frame;
  assert.ok(invite.release());
  assert.equal(invite.frame, shown);
  invite.tick(frameMs);
  assert.equal(invite.frame, shown - 1);
  invite.tick(1500);
  assert.equal(invite.phase, "off");
  assert.equal(invite.quietSpent, 0);
  invite.start(1);
  invite.touch(1);
  invite.tick(24 * frameMs);
  assert.equal(invite.phase, "touch");
  invite.tick(150 + 13 * frameMs);
  assert.equal(invite.phase, "rest");
});

test("the forehead follows a stationary touch and releases from its actual depth", () => {
  for (const side of [-1, 1]) {
    for (const depth of [0, 1, 6, 12]) {
      const invite = waiting(side);
      assert.ok(invite.touch(side));
      assert.equal(invite.touch(-side), false);
      invite.tick(150 + depth * frameMs);
      assert.equal(invite.frame, 24 + depth);
      assert.equal(invite.move(8), null);
      invite.release();
      assert.equal(invite.frame, 24 + depth);
      for (let i = depth; i > 0; i--) {
        invite.tick(frameMs);
        assert.equal(invite.frame, 23 + i);
      }
      invite.tick(frameMs);
      assert.equal(invite.phase, "away");
      invite.tick(10 * frameMs);
      assert.equal(invite.clip, null);
    }
  }
});

test("a tap leaves without nuzzling and a touch on the other side gets a glance", () => {
  const tap = waiting();
  tap.touch(-1);
  tap.tick(80);
  tap.release();
  assert.equal(tap.frame, 55);
  tap.tick(630);
  assert.equal(tap.phase, "off");

  for (const hold of [30, 530]) {
    const invite = waiting();
    invite.touch(1);
    invite.tick(hold);
    invite.release();
    if (hold < 7 * frameMs) invite.tick(7 * frameMs - hold);
    assert.equal(invite.frame, 72);
    invite.tick(5 * frameMs);
    assert.equal(invite.phase, "off");
    assert.equal(invite.quietSpent, 0);
  }
});

test("holding still settles the outside eye then lets the pet withdraw on her own", () => {
  const invite = waiting();
  invite.touch(-1);
  invite.tick(1100);
  assert.equal(invite.phase, "rest");
  assert.equal(invite.frame, 36);
  invite.tick(5000);
  assert.equal(invite.phase, "off");
  assert.equal(invite.release(), false);
  assert.equal(invite.canOffer(28000), false);
});

test("sliding transfers the pointer once; cancellation grants no residual time or response", () => {
  const invite = waiting();
  invite.touch(-1);
  assert.equal(invite.move(4), null);
  assert.equal(invite.move(10), "brush");
  assert.equal(invite.move(12), null);
  assert.equal(invite.release(), false);
  assert.equal(invite.phase, "joining");
  invite.tick(500);
  assert.equal(invite.phase, "brushReady");
  assert.equal(invite.brushFrame, 19);
  invite.cancel();
  assert.equal(invite.phase, "off");
  assert.equal(invite.quietSpent, 0);

  const ignored = waiting();
  ignored.cancel();
  ignored.tick(10000);
  assert.equal(ignored.quietSpent, 0);
  assert.equal(ignored.touch(-1), false);
});

test("each forehead pose hands the visible brow and paw to a matching brush frame", () => {
  for (const side of [-1, 1]) {
    for (const [source, last, brush] of [
      [5, 0, 4], [17, 82, side < 0 ? 19 : 25], [24, 82, side < 0 ? 19 : 25],
      [28, 82, side < 0 ? 19 : 25], [32, 88, side < 0 ? 37 : 43],
      [36, 88, side < 0 ? 37 : 43], [69, 76, 4],
    ]) {
      const invite = new Invite();
      invite.start(side);
      if (source < 24) {
        invite.tick(source * frameMs);
        invite.touch(side);
      } else {
        invite.tick(24 * frameMs);
        invite.touch(source >= 65 ? -side : side);
        invite.tick(source >= 65 ? (source - 65) * frameMs : 150 + (source - 24) * frameMs);
      }
      assert.equal(invite.frame, source);
      assert.equal(invite.move(12), "brush");
      assert.equal(invite.frame, source);
      assert.equal(invite.brushFrame, brush);
      let previous = source;
      while (invite.phase === "joining") {
        invite.tick(22);
        const now = invite.frame;
        assert.ok(Math.abs(now - previous) <= 1 ||
          ([23, 24].includes(previous) && now === 78) || (previous === 36 && now === 84));
        previous = now;
      }
      assert.equal(invite.phase, "brushReady");
      assert.equal(invite.frame, last);
      invite.tick(1000);
      assert.equal(invite.frame, last);
    }
  }
});

test("lifting mid-handoff unwinds the currently visible join and never transfers the stroke", () => {
  for (const depth of [0, 12]) {
    for (const advance of [0, 22, 66, 111, 400]) {
      const invite = waiting();
      invite.touch(-1);
      invite.tick(150 + depth * frameMs);
      invite.move(13);
      invite.tick(advance);
      const visible = invite.frame;
      assert.ok(invite.endStroke());
      assert.equal(invite.frame, visible);
      assert.equal(invite.brushFrame, null);
      assert.equal(invite.endStroke(), false);
      if (visible >= 78) {
        invite.tick(22);
        assert.equal(invite.frame, visible - 1);
      }
      invite.tick(3000);
      assert.equal(invite.phase, "off");
      assert.equal(invite.canOffer(7000), false);
    }
  }
  const still = new Invite();
  still.start(-1, { reduced: true });
  still.touch(-1);
  still.tick(450);
  still.move(15);
  assert.equal(still.phase, "brushReady");
  assert.equal(still.frame, 88);
  still.endStroke();
  assert.equal(still.frame, 64);
});

test("a reserved star rematch blocks the automatic invite, manual reduced poses still work", () => {
  const invite = new Invite();
  invite.skipOffer();
  assert.equal(invite.canOffer(19000), false);
  for (const side of [1, -1]) {
    invite.start(side, { reduced: true });
    assert.equal(invite.frame, 23);
    invite.touch(side);
    invite.tick(300);
    assert.equal(invite.frame, 36);
    invite.release();
    assert.equal(invite.frame, 64);
    invite.tick(1000);
    assert.equal(invite.phase, "off");
  }
});
