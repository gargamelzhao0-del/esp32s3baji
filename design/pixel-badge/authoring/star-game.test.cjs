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

test("releasing on a palm has no second hop; a short drop stays between the finger and palm", () => {
  for (const [side, p] of [[-1, palms[0]], [1, palms[1]]]) {
    for (const gap of [0, 8]) {
      const { game, catches } = fixture();
      game.down(1, at(game.pos.sx, game.pos.sy), palms, 0);
      game.move(at(p.sx, p.sy - gap), palms, 64);
      game.up(at(p.sx, p.sy - gap), palms, 80);
      let lastY = game.pos.sy;
      for (let ms = 0; ms < 112; ms += 16) {
        game.update(16, palms, false);
        assert.ok(game.pos.sy >= lastY && game.pos.sy <= p.sy);
        lastY = game.pos.sy;
        if (gap === 0) assert.equal(game.phase, "caught");
      }
      assert.deepEqual(catches, [side]);
    }
  }
});

test("the light recedes into the fingers, then both escaping glints leave the moving wrist", () => {
  const { game } = fixture();
  game.land(-1);
  game.update(300, palms, false);
  const litPixels = (frame, currentPalms) => {
    const ink = new Set();
    const ctx = {fillRect(x,y,w,h) {
      for (let r = y; r < y+h; r++)
        for (let c = x; c < x+w; c++) ink.add(c + "," + r);
    }};
    game.place(currentPalms, frame, palms);
    game.draw(ctx);
    return ink;
  };
  const prior = [16,17,18,19].map(frame => litPixels(frame,palms).size);
  assert.ok(prior.every((n, i) => i === 0 || n < prior[i-1]));
  assert.equal(prior.at(-1), 1);
  const lowered = [{sx:palms[0].sx+7, sy:palms[0].sy+11}, palms[1]];
  assert.deepEqual(litPixels(23, palms), litPixels(23, lowered));
  assert.equal(litPixels(26, lowered).size, 0);
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

test("choosing a palm glides from the shown star, including a midflight change of hand", () => {
  const { game } = fixture();
  let previous = { ...game.pos };
  game.aim(1, palms);
  assert.equal(game.pos.sx, previous.sx);
  for (let i = 0; i < 40; i++) {
    if (i === 8) game.aim(-1, palms);
    game.update(16, palms, false);
    assert.ok(Math.hypot(game.pos.sx - previous.sx, game.pos.sy - previous.sy) < 8);
    previous = { ...game.pos };
  }
  assert.equal(game.pos.sx, palms[0].sx - 3);
  assert.equal(game.pos.sy, palms[0].sy - 10);
  game.aim(1, palms);
  game.update(16, palms, true);
  assert.equal(game.pos.sx, palms[1].sx + 3);
  game.down(8, at(palms[0].sx, palms[0].sy), palms, 700);
  assert.equal(game.glow, -1);
  game.move(at(85, 70), palms, 750);
  assert.equal(game.glow, 0);
});

test("a missed star crosses back above the fringe or below the eyes in both badge views", () => {
  for (const [halfSpan, palmY, eyeY] of [[20, 98, 77], [29, 133, 99]]) {
    const hands = [-1, 1].map(side => at(92 + side * halfSpan, palmY));
    for (const side of [-1, 1]) {
      for (const high of [false, true]) {
        const { game, catches } = fixture();
        game.lastHand = side;
        game.start();
        const miss = at(92 + side * (halfSpan + 15), high ? eyeY - 7 : palmY - 5);
        game.down(1, at(game.pos.sx, game.pos.sy), hands, 0);
        game.move(miss, hands, 40);
        game.up(miss, hands, 300);
        assert.equal(game.phase, "return");
        let crossed = false;
        for (let t = 0; t < 650; t += 20) {
          game.update(20, hands, false);
          if (Math.abs(game.pos.sx - 92) > halfSpan * .66) continue;
          crossed = true;
          assert.ok(high ? game.pos.sy < eyeY - 7 : game.pos.sy > eyeY + 7);
        }
        assert.ok(crossed);
        assert.equal(game.phase, "invite");
        assert.deepEqual(catches, []);
      }
    }
  }
});

test("a cancelled palm tap does not send the departing star across the forehead", () => {
  const { game } = fixture();
  game.down(4, at(palms[1].sx, palms[1].sy), palms, 0);
  game.cancelDrag();
  game.update(370, palms, false);
  game.update(14000, palms, false);
  assert.equal(game.phase, "depart");
  for (let t = 0; t < 600; t += 20) {
    game.update(20, palms, false);
    assert.ok(game.pos.sx < 65);
  }
  assert.equal(game.phase, "off");
});
