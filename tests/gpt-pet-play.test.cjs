const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const browser = {window:{}, setTimeout, clearTimeout};
const script = path.join(__dirname, "../design/gpt-pet/play.js");
vm.runInNewContext(fs.readFileSync(script, "utf8"), browser, {filename:script});

function companion(reduced=false) {
    const pet = {
        scene:{active:true,reduced}, time:0, bond:0,
        rig:{anchors:{eyes:{x:400,y:340},left:{x:300,y:480},right:{x:510,y:480}}},
        reward(){this.bond++;}, random(){return 0.2;}, baseNote(){return "";},
        note(){}, onPlay(){}, advance(){}
    };
    const game = new browser.window.DragonPlay(pet);
    function tick(seconds) {
        const count = Math.ceil(seconds*30);
        for (let i=0;i<count;i++) {pet.time+=seconds/count;game.tick(seconds/count);}
    }
    return {pet,game,tick};
}

test("a high five must meet the raised hand at its current position and counts once", () => {
    const {pet,game,tick}=companion();
    game.start("five",-1);
    pet.rig.anchors.left={x:241,y:411};
    game.press({worldX:300,worldY:480});
    assert.equal(game.round.phase,"offer");
    game.press({worldX:244,worldY:413});
    game.press({worldX:244,worldY:413});
    assert.equal(game.round.phase,"met");
    assert.equal(pet.bond,1);
    assert.equal(pet.lastPlay.side,-1);
    tick(1.5);
    assert.equal(game.round,null);
});

test("an unanswered invitation expires without recording a contact", () => {
    const {pet,game,tick}=companion();
    game.start("five",1);
    tick(7.1);
    assert.equal(game.round.phase,"withdraw");
    game.press({worldX:510,worldY:480});
    tick(1.5);
    assert.equal(pet.bond,0);
    assert.equal(pet.lastPlay,undefined);
    assert.equal(game.round,null);
});

test("the light must slow at a hand before landing and remembers the hand it reached", () => {
    const {pet,game,tick}=companion();
    game.start("fly",-1);
    game.aim(300,470);tick(0.15);
    game.aim(510,215);tick(0.9);
    assert.equal(game.round.phase,"offer");
    assert.equal(game.round.dwell,0);
    assert.equal(pet.bond,0);
    game.aim(509,469);tick(1.45);
    assert.equal(game.round.phase,"landed");
    assert.equal(pet.lastPlay.kind,"fly");
    assert.equal(pet.lastPlay.side,1);
    tick(2.4);
    assert.equal(game.round,null);
    assert.equal(pet.bond,1);
});

test("reduced motion waits for input, supports keyboards and clears the result timer", () => {
    const {pet,game,tick}=companion(true);
    game.start("fly",1);
    tick(30);
    assert.equal(game.round.phase,"offer");
    const x=game.round.aimX;
    assert.ok(game.key("ArrowLeft"));
    assert.ok(game.round.aimX<x);
    assert.ok(game.key("Enter"));
    assert.equal(game.round.phase,"landed");
    assert.ok(game.key("Escape"));
    assert.equal(game.round,null);
    assert.equal(pet.bond,1);
});
