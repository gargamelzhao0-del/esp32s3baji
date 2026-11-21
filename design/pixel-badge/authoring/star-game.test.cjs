const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const Game = vm.runInNewContext(
  fs.readFileSync(path.join(__dirname, "../star-game.js"), "utf8") +
    "\nBadgeStarGame",
);
const palms = [
  { sx: 66, sy: 120 },
  { sx: 114, sy: 120 },
];
const at = (sx, sy) => ({ sx, sy, inside: true });
function fixture() {
  const catches = [],
    exits = [];
  const game = new Game({
    catchStar: (v) => catches.push(v),
    leave: () => exits.push(1),
    note() {},
  });
  game.start();
  return { game, catches, exits };
}

test("either palm can receive the light and keeps it until the fingers close", () => {
  for (const i of [0, 1]) {
    const { game, catches } = fixture();
    const p = palms[i];
    assert.ok(game.down(8, at(game.pos.sx, game.pos.sy), palms, 0));
    game.move(at(p.sx, p.sy), palms, 70);
    game.up(at(p.sx, p.sy), palms, 120);
    assert.equal(game.phase, "landing");
    game.update(300, palms, false);
    assert.deepEqual(catches, [i === 0 ? -1 : 1]);
    game.place(palms, 12);
    assert.equal(game.pos.sx, p.sx);
    game.update(30, palms, false);
    assert.equal(catches.length, 1);
  }
});

test("an aimed flick travels to an actual palm; a slow, stopped or sideways toss returns", () => {
  for (const [endX, endY, upTime, landing] of [
    [100, 120, 90, true],
    [100, 120, 500, false],
    [90, 99, 90, false],
  ]) {
    const { game } = fixture();
    game.down(1, at(game.pos.sx, game.pos.sy), palms, 0);
    game.move(at(76, 120), palms, 160);
    game.move(at(endX, endY), palms, 230);
    game.up(at(endX, endY), palms, 160 + upTime);
    assert.equal(game.phase, landing ? "landing" : "return");
    if (landing) {
      assert.equal(game.hand, 1);
      assert.equal(game.thrown, true);
    }
  }
});

test("holding waits for the finger, cancellation and unclaimed invitations yield no catch", () => {
  const { game, catches, exits } = fixture();
  game.down(1, at(game.pos.sx, game.pos.sy), palms, 0);
  game.update(16000, palms, false);
  assert.equal(game.phase, "held");
  game.cancelDrag();
  game.update(370, palms, false);
  game.update(14000, palms, false);
  game.update(600, palms, false);
  assert.deepEqual(catches, []);
  assert.equal(exits.length, 1);
  assert.equal(game.phase, "off");
});

test("the 27 eye directions include both hands and keyboard play works in reduced motion", () => {
  const { game, catches } = fixture();
  const eyes = at(92, 82);
  game.pos = at(46, 49);
  assert.equal(game.watchFrame(eyes, palms), 0);
  game.pos = eyes;
  assert.equal(game.watchFrame(eyes, palms), 13);
  game.pos = at(139, 131);
  assert.equal(game.watchFrame(eyes, palms), 26);
  game.aim(1, palms);
  game.land(1);
  game.update(16, palms, true);
  assert.deepEqual(catches, [1]);
});
