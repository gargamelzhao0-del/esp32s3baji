"use strict";

// Positions live on the badge's native grid. The two palms come from the displayed pose.
class BadgeStarGame {
  constructor(events) {
    this.events = events;
    this.phase = "off";
    this.lastHand = 1;
  }

  start() {
    this.hand = -this.lastHand;
    this.home = { sx: 92 + this.hand * 51, sy: 69 };
    this.pos = { ...this.home };
    this.clock = this.age = 0;
    this.phase = "invite";
    this.finger = null;
    this.glow = 0;
    this.trail = [];
  }

  stop() {
    this.finger = null;
    this.phase = "off";
    this.trail = [];
  }

  change(phase) {
    this.from = { ...this.pos };
    this.phase = phase;
    this.age = 0;
  }

  nearest(point, palms) {
    const distance = (hand) =>
      Math.hypot(point.sx - hand.sx, point.sy - hand.sy);
    return distance(palms[0]) <= distance(palms[1]) ? -1 : 1;
  }

  handAt(point, palms, radius) {
    const side = this.nearest(point, palms);
    const p = palms[side < 0 ? 0 : 1];
    return Math.hypot(point.sx - p.sx, point.sy - p.sy) <= radius ? side : 0;
  }

  available() {
    return ["invite", "aim", "return"].includes(this.phase);
  }

  down(id, point, palms, time) {
    if (!this.available() || !point.inside) return false;
    const onStar =
      Math.hypot(point.sx - this.pos.sx, point.sy - this.pos.sy) <= 12;
    const palm = this.handAt(point, palms, 9);
    if (!onStar && !palm) return false;
    this.finger = id;
    this.change(onStar ? "held" : "hand");
    this.path = [{ ...this.pos, time }];
    this.offset = { sx: this.pos.sx - point.sx, sy: this.pos.sy - point.sy };
    if (!onStar) this.hand = this.glow = palm;
    this.events.note(
      onStar
        ? "她盯住你手里的星萤。把它带到一只掌心，再松手。"
        : "她摊开小爪，等你把星萤交给她。",
    );
    return true;
  }

  move(point, palms, time) {
    if (this.phase === "hand") {
      this.glow = point.inside && this.handAt(point, palms, 12) === this.hand ? this.hand : 0;
      return;
    }
    if (this.phase !== "held") return;
    const sx = point.sx + this.offset.sx;
    const sy = Math.max(31, Math.min(144, point.sy + this.offset.sy));
    const half = Math.sqrt(73 ** 2 - (sy - 92) ** 2);
    this.pos = { sx: Math.max(92 - half, Math.min(92 + half, sx)), sy };
    this.glow = point.inside ? this.handAt(this.pos, palms, 12) : 0;
    this.path = this.path.filter((p) => time - p.time <= 120);
    this.path.push({ ...this.pos, time });
    const prev = this.trail.at(-1);
    if (
      !prev ||
      Math.hypot(this.pos.sx - prev.sx, this.pos.sy - prev.sy) >= 3
    ) {
      this.trail.push({ ...this.pos, born: this.clock });
      if (this.trail.length > 4) this.trail.shift();
    }
  }

  up(point, palms, time) {
    this.finger = null;
    if (this.phase === "held") this.move(point, palms, time);
    const side =
      this.phase === "hand"
        ? this.hand === this.handAt(point, palms, 12)
          ? this.hand
          : 0
        : this.glow;
    const throwSide =
      !side && this.phase === "held" && point.inside
        ? this.throwTarget(palms, time)
        : 0;
    if (side && point.inside) this.land(side);
    else if (throwSide) this.land(throwSide, true);
    else {
      this.cancelDrag();
      this.events.note("星萤绕回来啦。可以再牵一次，也可以直接点她的手心。");
    }
  }

  throwTarget(palms, time) {
    const start = this.path[0],
      dt = time - start.time;
    const vx = this.pos.sx - start.sx,
      vy = this.pos.sy - start.sy;
    const length = Math.hypot(vx, vy);
    if (dt < 12 || length < 8 || length / dt < 0.16) return 0;
    // Only a palm inside the forward cone can catch a thrown star.
    let hit = 0,
      nearest = Infinity;
    palms.forEach((p, i) => {
      const dx = p.sx - this.pos.sx,
        dy = p.sy - this.pos.sy;
      const forward = (dx * vx + dy * vy) / length;
      const lateral = Math.abs(dx * vy - dy * vx) / length;
      if (forward > 0 && forward < 56 && lateral < 11 && forward < nearest) {
        hit = i === 0 ? -1 : 1;
        nearest = forward;
      }
    });
    return hit;
  }

  cancelDrag() {
    this.finger = null;
    this.glow = 0;
    if (this.phase === "held" || this.phase === "hand") {
      this.change("return");
      this.returnLane = undefined;
    }
  }

  aim(side, palms) {
    if (!this.available()) return;
    if (this.phase === "aim" && this.hand === side) return;
    this.hand = this.glow = side;
    this.change("aim");
    const p = palms[side < 0 ? 0 : 1];
    const distance = Math.hypot(p.sx + side * 3 - this.pos.sx, p.sy - 10 - this.pos.sy);
    this.aimDuration = Math.max(160, Math.min(470, distance * 5));
    this.events.note(
      side < 0
        ? "她摊开左边的小爪，按回车送给她。"
        : "她摊开右边的小爪，按回车送给她。",
    );
  }

  land(side, thrown = false) {
    if (this.phase === "landing" || this.phase === "caught") return;
    this.hand = this.lastHand = side;
    this.glow = side;
    this.thrown = thrown;
    this.change("landing");
  }

  update(dt, palms, reduced) {
    this.still = reduced;
    this.clock += dt;
    this.age += dt;
    this.trail = this.trail.filter((p) => this.clock - p.born < 160);
    const home = {
      sx: this.home.sx + (reduced ? 0 : 3 * Math.sin(this.clock / 520)),
      sy: this.home.sy + (reduced ? 0 : 3 * Math.sin(this.clock / 340)),
    };
    const travel = (goal, duration, arc, carry = false) => {
      const u = Math.min(1, this.age / duration);
      const ease = (t) => t * t * (3 - 2 * t);
      const middle = (palms[0].sx + palms[1].sx) / 2;
      // When a chosen palm is across the portrait, descend along the near
      // sleeve before crossing at hand height. Keep her eye line clear.
      const skirtFace = carry && this.from.sy < goal.sy - 18 &&
        (this.from.sx - middle) * (goal.sx - middle) < 0;
      const along = ease(skirtFace ? u * u : u);
      const down = ease(skirtFace ? 1 - (1-u) ** 2 : u);
      const lift = skirtFace ? -Math.min(2, arc) : arc;
      this.pos = {
        sx: this.from.sx + (goal.sx - this.from.sx) * along,
        sy:
          this.from.sy +
          (goal.sy - this.from.sy) * down -
          (reduced ? 0 : lift * 4 * u * (1 - u)),
      };
      return u === 1;
    };
    if (this.phase === "invite") {
      this.pos = home;
      if (this.age >= 14000) this.change("depart");
    } else if (this.phase === "aim") {
      const palm = palms[this.hand < 0 ? 0 : 1];
      travel({ sx: palm.sx + this.hand * 3, sy: palm.sy - 10 }, reduced ? 1 : this.aimDuration, 3, true);
    } else if (this.phase === "return") {
      const distance = Math.hypot(this.from.sx - this.home.sx, this.from.sy - this.home.sy);
      if (this.returnLane === undefined) {
        const middle = (palms[0].sx + palms[1].sx) / 2;
        this.returnLane = null;
        // A miss at the far cuff comes home below her face. A miss high in
        // the hair comes back over the fringe. Both paths meet the same hover.
        if ((this.from.sx - middle) * (this.home.sx - middle) < 0) {
          const palmY = (palms[0].sy + palms[1].sy) / 2;
          this.returnLane = this.from.sy > (palmY + this.home.sy) / 2
            ? palmY + 4 : Math.max(36, Math.min(this.home.sy, this.from.sy) - 10);
        }
      }
      if (!reduced && this.returnLane !== null) {
        const u = Math.min(1, this.age / Math.min(620, 220 + distance * 3));
        const v = 1 - u;
        this.pos = {
          sx: this.from.sx + (home.sx - this.from.sx) * u * u * (3 - 2 * u),
          sy: v ** 3 * this.from.sy + 3 * u * v * this.returnLane + u ** 3 * home.sy,
        };
        if (u === 1) this.change("invite");
      } else if (travel(home, reduced ? 1 : Math.min(360, 70 + distance * 5), Math.min(6, distance / 5))) {
        this.change("invite");
      }
    } else if (this.phase === "landing") {
      const palm = palms[this.hand < 0 ? 0 : 1];
      const gap = Math.hypot(palm.sx - this.from.sx, palm.sy - this.from.sy);
      // A hand-off at the palm is already a catch. Only flight through air
      // needs an arc; a nearby drop settles the remaining distance directly.
      const arc = this.thrown
        ? Math.min(13, gap / 3)
        : Math.min(5, Math.max(0, gap - 12) / 6);
      const duration = reduced || gap <= 1
        ? 1
        : Math.min(this.thrown ? 410 : 290, 50 + 5 * gap);
      if (travel(palm, duration, arc, !this.thrown)) {
        this.change("caught");
        this.events.catchStar(this.hand);
      }
    } else if (this.phase === "depart") {
      if (travel({ sx: 92 + Math.sign(this.home.sx - 92) * 43, sy: 36 }, reduced ? 1 : 580, 4)) {
        this.stop();
        this.events.leave();
      }
    }
  }

  watchFrame(eyes, palms) {
    const palm = palms[this.pos.sx < eyes.sx ? 0 : 1];
    // At a palm the gaze must join the first catch frame, in either lens.
    const step = Math.max(1, Math.abs(palm.sx - eyes.sx) / 3);
    const col = Math.max(
      0,
      Math.min(8, 4 + Math.round((this.pos.sx - eyes.sx) / step)),
    );
    const row =
      this.pos.sy < eyes.sy - 8 ? 0 : this.pos.sy > eyes.sy + 8 ? 2 : 1;
    return row * 9 + col;
  }

  place(palms, catchFrame, releasePalms = palms) {
    this.palms = palms;
    this.catchFrame = catchFrame;
    if (this.phase === "caught") {
      const hand = this.hand < 0 ? 0 : 1;
      this.pos = { ...palms[hand] };
      this.afterglow = releasePalms[hand];
    }
  }

  draw(ctx) {
    const { phase, still } = this;
    if (phase === "off" || !this.palms) return;
    const dot = (x, y, w, h, c) => {
      ctx.fillStyle = c;
      ctx.fillRect(Math.round(x), Math.round(y), w, h);
    };
    const gold = "#ffe296",
      mint = "#c7efcf",
      outline = "#825b6d";
    const { sx: x, sy: y } = this.pos;
    const wing = still ? 0 : Math.floor(this.clock / 95) % 2;
    if (!still && phase === "held")
      this.trail.forEach((p) => dot(p.sx, p.sy, 1, 1, mint));
    if (phase === "held" || phase === "aim" || phase === "hand") {
      this.palms.forEach((p, i) => {
        const near = this.glow === (i === 0 ? -1 : 1);
        dot(p.sx - 4, p.sy + 2, 2, 1, near ? gold : "#ceedcf");
        dot(p.sx + 3, p.sy + 2, 2, 1, near ? gold : "#ceedcf");
        if (near) dot(p.sx - 2, p.sy + 4, 5, 1, gold);
      });
    }
    // She closes her fingers on the light; two glints escape before the hands relax.
    if (phase === "caught" && this.catchFrame >= 20) {
      const lift = this.catchFrame - 20;
      if (lift < 6 && !still) {
        // Detached glints drift from where the fingers met even as the arm lowers.
        const at = this.afterglow;
        dot(at.sx - 1 - lift, at.sy + 2 - lift, 1, 2, gold);
        dot(at.sx + 1 + lift, at.sy + 1 - lift, 1, 1, mint);
      }
      return;
    }
    if (phase === "depart" && this.age > 380) {
      dot(x, y, 2, 2, gold);
      return;
    }
    if (phase !== "caught") {
      dot(x - 5, y - 2 - wing, 3, 2, outline);
      dot(x + 3, y - 2 - wing, 3, 2, outline);
      dot(x - 5, y - 3 - wing, 2, 2, mint);
      dot(x + 4, y - 3 - wing, 2, 2, mint);
    }
    if (phase === "caught" && this.catchFrame >= 17) {
      const inset = this.catchFrame - 16;
      const glintY = y + inset;
      if (this.catchFrame === 17) {
        dot(x - 2, glintY - 1, 5, 2, outline);
        dot(x - 1, glintY - 2, 2, 4, gold);
        dot(x - 1, glintY, 3, 1, "#fff8df");
      } else if (this.catchFrame === 18) {
        dot(x - 1, glintY - 1, 3, 2, gold);
        dot(x, glintY - 1, 1, 1, "#fff8df");
      } else dot(x, glintY - 1, 1, 1, gold);
      return;
    }
    dot(x - 1, y - 4, 2, 8, outline);
    dot(x - 4, y - 1, 8, 3, outline);
    dot(x - 2, y - 2, 5, 5, "#e1a468");
    dot(x - 1, y - 3, 2, 6, gold);
    dot(x - 3, y, 6, 1, gold);
    dot(x - 1, y - 1, 2, 2, "#fff8df");
  }
}
