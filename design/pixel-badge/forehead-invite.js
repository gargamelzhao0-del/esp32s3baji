"use strict";

// The two atlases have matching poses at every branch: 23, 24, 37, 55 and 65.
const browFrameMs = 1000 / 16;
const browJoinMs = 22;
const browDurations = {
  offer: 24 * browFrameMs,
  wait: 3700,
  touch: 150,
  nuzzle: 13 * browFrameMs,
  rest: 2400,
  tidy: 18 * browFrameMs,
  away: 10 * browFrameMs,
  look: 7 * browFrameMs,
  lookHold: 2400,
  lookBack: 5 * browFrameMs,
};

class BadgeForeheadInvite {
  constructor() {
    this.offered = false;
    this.side = -1;
    this.cancel();
  }

  canOffer(quiet, napAt = 28000) {
    const unansweredTime =
      browDurations.offer + browDurations.wait + browDurations.tidy;
    return (
      this.phase === "off" && !this.offered &&
      quiet >= 7000 && quiet + unansweredTime <= napAt
    );
  }

  skipOffer() {
    this.offered = true;
  }

  rearm() {
    if (this.phase !== "off") return;
    this.offered = false;
    this.quietSpent = 0;
  }

  start(side, { quiet = 0, reduced = false } = {}) {
    if (this.phase !== "off") return false;
    this.side = side < 0 ? -1 : 1;
    this.reduced = reduced;
    this.offered = true;
    this.quietSpent = quiet;
    this.countQuiet = quiet > 0;
    this.change(reduced ? "wait" : "offer");
    return true;
  }

  change(phase) {
    this.phase = phase;
    this.age = 0;
  }

  get clip() {
    if (this.phase === "off") return null;
    return this.side < 0 ? "browInviteLeft" : "browInviteRight";
  }

  get brushFrame() {
    return this.join?.brushFrame ?? null;
  }

  get frame() {
    const step = Math.floor(this.age / browFrameMs);
    switch (this.phase) {
      case "offer": return Math.min(23, step);
      case "wait": return 23;
      case "touch": return 24;
      case "nuzzle": return 24 + Math.min(12, step);
      case "rest": return 36;
      case "unwind": return 24 + Math.max(0, this.from - step);
      case "retract": return Math.max(0, this.from - step);
      case "tidy": return this.reduced ? 54 : 37 + Math.min(17, step);
      case "away": return this.reduced ? 64 : 55 + Math.min(9, step);
      case "look": return 65 + Math.min(6, step);
      case "lookHold": return 71;
      case "lookBack": return this.reduced ? 76 : 72 + Math.min(4, step);
      case "joining": return this.join.frames[Math.floor(this.age / browJoinMs)];
      case "brushReady": return this.join.frames.at(-1);
      case "unjoin": return Math.max(this.to, this.from - Math.floor(this.age / browJoinMs));
      default: return 0;
    }
  }

  touch(side) {
    if (this.touchSide || (this.phase !== "offer" && this.phase !== "wait"))
      return false;
    this.touchSide = side < 0 ? -1 : 1;
    this.countQuiet = false;
    this.quietSpent = 0;
    if (this.phase === "wait") this.respond();
    return true;
  }

  respond() {
    if (this.touchSide === this.side) this.change("touch");
    else this.change(this.reduced ? "lookHold" : "look");
  }

  move(distance) {
    if (!this.touchSide || distance <= 8) return null;
    this.touchSide = 0;
    const shown = this.frame;
    const frames = [shown];
    function to(end) {
      const direction = Math.sign(end - shown);
      for (let i = 1; i <= Math.abs(end - shown); i++)
        frames.push(shown + i * direction);
    }
    let brushFrame = 4;
    if (shown < 12) {
      to(0); // The paw is still dressing the fringe. Put it down where it came from.
    } else if (shown >= 65) {
      to(76); // Finish looking across before following this new finger.
    } else if (shown <= 30) {
      to(shown < 24 ? 23 : 24);
      // 77 matches both the waiting pose (23) and the first nuzzle pose (24).
      frames.push(78, 79, 80, 81, 82);
      brushFrame = this.side < 0 ? 19 : 25;
    } else {
      to(36);
      // 83 matches the fully settled nuzzle; keep the far eye closed.
      frames.push(84, 85, 86, 87, 88);
      brushFrame = this.side < 0 ? 37 : 43;
    }
    this.join = { frames, brushFrame };
    this.change(this.reduced ? "brushReady" : "joining");
    return "brush";
  }

  endStroke() {
    if (this.phase !== "joining" && this.phase !== "brushReady") return false;
    const shown = this.frame;
    this.join = null;
    if (this.reduced) this.change("away");
    else if (shown >= 77) {
      this.from = shown;
      this.to = shown >= 83 ? 83 : 77;
      this.change("unjoin");
    } else if (shown >= 65) {
      const returning = shown >= 72;
      this.change(returning ? "lookBack" : "look");
      this.age = (shown - (returning ? 72 : 65)) * browFrameMs;
    } else if (shown >= 24) {
      this.from = shown - 24;
      this.change("unwind");
    } else if (shown === 23) this.change("away");
    else {
      this.from = shown;
      this.change("retract");
    }
    return true;
  }

  release() {
    if (!this.touchSide) return false;
    this.touchSide = 0;
    if (this.phase === "offer") {
      this.from = this.frame;
      this.change("retract");
    } else if (this.phase === "touch") this.change("away");
    else if (this.phase === "nuzzle" || this.phase === "rest") this.withdraw();
    else if (this.phase === "lookHold") this.change("lookBack");
    return true;
  }

  withdraw() {
    // Reverse from the drawing currently under the finger, including mid-nuzzle.
    this.from = this.frame - 24;
    this.change(this.reduced ? "away" : "unwind");
  }

  cancel() {
    this.quietSpent = 0;
    this.countQuiet = false;
    this.touchSide = 0;
    this.join = null;
    this.change("off");
  }

  next() {
    switch (this.phase) {
      case "offer":
        if (this.touchSide) this.respond();
        else this.change("wait");
        break;
      case "wait": this.change("tidy"); break;
      case "touch": this.change(this.reduced ? "rest" : "nuzzle"); break;
      case "nuzzle": this.change("rest"); break;
      case "rest":
        // Enough for one invitation. A finger left on the screen cannot re-arm it.
        this.touchSide = 0;
        this.withdraw();
        break;
      case "unwind": this.change("away"); break;
      case "look": this.change(this.touchSide ? "lookHold" : "lookBack"); break;
      case "lookHold":
        this.touchSide = 0;
        this.change("lookBack");
        break;
      case "joining": this.change("brushReady"); break;
      case "unjoin":
        if (this.to === 77) this.change("away");
        else {
          this.from = 12;
          this.change("unwind");
        }
        break;
      default:
        this.touchSide = 0;
        this.countQuiet = false;
        this.change("off");
    }
  }

  tick(dt) {
    // Carry fractional time through a branch. A slow draw cannot extend a quiet
    // invitation past the pet's original sleep deadline.
    const wasCounting = this.countQuiet;
    while (dt > 0 && this.phase !== "off" && this.phase !== "brushReady") {
      let duration = browDurations[this.phase];
      if (this.phase === "retract" || this.phase === "unwind")
        duration = (this.from + 1) * browFrameMs;
      else if (this.phase === "joining")
        duration = this.join.frames.length * browJoinMs;
      else if (this.phase === "unjoin")
        duration = (this.from - this.to + 1) * browJoinMs;
      const advance = Math.min(dt, duration - this.age);
      this.age += advance;
      if (this.countQuiet) this.quietSpent += advance;
      dt -= advance;
      if (this.age >= duration) this.next();
    }
    if (wasCounting && this.phase === "off") this.quietSpent += dt;
  }
}
