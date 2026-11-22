"use strict";

const clampUnit = (value) => Math.min(1, Math.max(0, value));
const sleepingClip = (id) => id === "doze" || id.startsWith("peek");
const bookClip = (id) => id === "turnStar" || id === "turnMoon";

class BadgeGestures {
  constructor(canvas, handlers) {
    this.canvas = canvas;
    this.io = handlers;
    this.quiet = 0;
    this.cheekTimes = [];
    this.nextSide = -1;
    this.moon = false;
    this.sleepOffset = 0;
    this.sleepFrame = 0;
    this.sleepJoin = null;
    this.pointer = null;
    this.contact = null;
    this.cheek = null;
    this.brush = null;
    this.page = null;
    this.holdTimer = 0;
    canvas.addEventListener("pointerdown", (event) => this.down(event));
    canvas.addEventListener("pointermove", (event) => {
      // On a touch display one paint can contain the turnaround of a short rub.
      for (const sample of event.getCoalescedEvents?.() || [])
        this.move(sample);
      this.move(event);
    });
    canvas.addEventListener("pointerup", (event) => this.up(event));
    for (const name of ["lostpointercapture", "pointercancel"]) {
      canvas.addEventListener(name, (event) => {
        if (event.pointerId === this.pointer?.id) this.cancelContact();
      });
    }
  }

  release() {
    clearTimeout(this.holdTimer);
    const id = this.pointer?.id;
    this.pointer = null;
    if (id != null && this.canvas.hasPointerCapture(id))
      this.canvas.releasePointerCapture(id);
  }

  pillowKey(point) {
    if (point) {
      if (this.pointer) return;
      this.contact = {...point, part:"pillow", keyboard:true, phase:"down", age:0};
    } else if (this.contact?.keyboard && this.contact.phase==="down") {
      this.contact = {...this.contact, heldMs:this.contact.age, phase:"release", age:0};
      this.io.note("你松开手，云朵枕轻轻鼓了起来。");
    } else return;
    this.io.invalidate?.();
  }

  cancelContact() {
    const atForehead = this.pointer?.part === "offeredHead" ||
      (this.brush?.fromForehead && this.io.forehead?.clip);
    this.release();
    this.contact = null;
    if (atForehead) {
      this.brush = null;
      this.io.cancelForehead();
    }
    if (this.cheek) this.cheek.held = false;
    if (this.brush) this.finishBrush(false);
    if (this.page?.phase === "drag") this.settlePage(false, false);
  }

  interrupt() {
    this.release();
    this.brush = null;
    this.page = null;
    this.cheek = null;
    this.contact = null;
    this.sleepJoin = null;
    this.quiet = 0;
    this.cheekTimes = [];
  }

  registerTap() {
    const now = performance.now();
    this.cheekTimes = this.cheekTimes.filter((stamp) => now - stamp < 1100);
    this.cheekTimes.push(now);
    if (this.cheekTimes.length < 3) return false;
    this.cheekTimes = [];
    return true;
  }

  poke(side) {
    this.release();
    this.page = this.brush = this.cheek = this.contact = null;
    this.quiet = 0;
    const clip = this.registerTap()
      ? "tickle"
      : side < 0
        ? "cheekLeft"
        : "cheekRight";
    this.io.show(clip);
    this.io.poke?.(clip, side);
    this.nextSide = -side;
  }

  nap() {
    if (this.io.base() !== "idle") return;
    this.interrupt();
    this.sleepOffset = 0;
    this.sleepFrame = 0;
    this.io.show("doze");
  }

  rouse(side) {
    const id = this.io.intent();
    if (!sleepingClip(id)) return;
    if (this.sleepJoin) {
      // A second call may land while the sleepy head is still settling.
      if (id === "doze" && this.sleepJoin.next.startsWith("peek"))
        this.sleepJoin.stretch = true;
      return;
    }
    this.interrupt();
    if (id.startsWith("peek")) {
      this.joinSleep(
        PIXEL_CLIPS[id].frames - 1,
        id === "peekLeft" ? "wakeLeft" : "wakeRight",
      );
    } else if (this.sleepFrame <= 6) {
      // During the first sleepy nod she can still sit straight back up.
      this.joinSleep(0, "idle");
    } else {
      // Once her head has tipped forward, finish settling. The breathing loop joins
      // along the shortest part of its cycle to the matching closed-eye pose.
      this.joinSleep(
        this.sleepFrame > 25 ? 35 : 15,
        side < 0 ? "peekLeft" : "peekRight",
      );
    }
  }

  joinSleep(to, next) {
    if (this.sleepFrame === to) {
      this.sleepFrame = 0;
      if (next === "idle") this.io.restore();
      else this.io.show(next);
    } else {
      this.sleepJoin = {
        from: this.sleepFrame,
        to,
        next,
        age: 0,
        landed: false,
        stretch: false,
      };
    }
  }

  advanceSleep(dt, reduced) {
    const join = this.sleepJoin;
    // Expose the matching endpoint for one rendered frame before changing atlases.
    if (join.landed) {
      this.sleepJoin = null;
      this.sleepFrame = 0;
      if (join.next === "idle") this.io.restore();
      else {
        if (join.stretch)
          this.joinSleep(
            PIXEL_CLIPS[join.next].frames - 1,
            join.next === "peekLeft" ? "wakeLeft" : "wakeRight",
          );
        this.io.show(join.next);
      }
      return join.to === 35 ? 15 : join.to;
    }
    // A slow draw can delay a join, but cannot skip her head or hand poses.
    join.age += Math.min(dt, 22);
    const distance = Math.abs(join.to - join.from);
    const step = reduced
      ? distance
      : Math.min(distance, Math.floor(join.age / 22));
    const index = join.from + step * Math.sign(join.to - join.from);
    join.landed = index === join.to;
    this.sleepFrame = index === 35 ? 15 : index;
    return this.sleepFrame;
  }

  startBrush(side, target = side * 0.35, fromForehead = false) {
    clearTimeout(this.holdTimer);
    const previous = this.brush;
    this.cheek = null;
    this.cheekTimes = [];
    this.quiet = 0;
    this.brush = {
      position: previous?.position ?? target,
      target,
      depth: previous?.depth ?? 0,
      stroked: false,
      releaseMs: null,
      sparks: [],
      direction: 0,
      turns: 0,
      reward: false,
      fromForehead,
    };
    if (!fromForehead) this.io.show("brush");
  }

  joinBrush(frame) {
    if (!this.brush?.fromForehead) return;
    // Each nine-column row is a pressure level. The last invitation drawing
    // is also an exact cell in the original interactive brush atlas.
    this.brush.position = ((frame % 9) - 4) / 4;
    this.brush.depth = Math.floor(frame / 9) / 4;
    this.io.show("brush");
  }

  finishBrush(voluntary = true) {
    const brush = this.brush;
    if (!brush) return;
    if (brush.fromForehead && this.io.forehead.endStroke()) {
      this.brush = null;
      return;
    }
    if (!voluntary) brush.reward = false;
    if (brush.releaseMs !== null) return;
    brush.releaseMs = 0;
    brush.reward = voluntary && brush.turns >= 2 && this.io.base() === "idle";
    if (brush.fromForehead) brush.target = brush.position;
    else brush.target = Math.max(
      -1,
      Math.min(1, brush.target + 0.24 * (brush.direction || Math.sign(brush.target))),
    );
    if (voluntary && brush.stroked && !brush.fromForehead)
      this.io.note("你的手停下了。她又往刚才的方向蹭了一下。");
  }

  startPage() {
    this.brush = this.cheek = null;
    this.cheekTimes = [];
    this.quiet = 0;
    this.page = { phase: "drag", progress: 0, from: 0, age: 0 };
    this.io.show(this.moon ? "turnMoon" : "turnStar");
  }

  settlePage(commit, glance = true) {
    if (this.page?.phase !== "drag") return;
    Object.assign(this.page, {
      phase: commit ? "forward" : "back",
      from: this.page.progress,
      age: 0,
      glance,
    });
    if (glance)
      this.io.note(
        commit
          ? "她用爪子压住书脊，把新一页摊平。"
          : "纸页弹回来了。她从书上抬眼瞄了瞄你。",
      );
  }

  sceneButton() {
    const id = this.io.intent();
    if (sleepingClip(id)) return this.rouse(-1);
    if (this.io.base() === "idle") return this.nap();
    this.interrupt();
    if (this.io.base() === "reading") {
      this.startPage();
      this.settlePage(true);
    } else {
      this.startBrush(this.nextSide);
      this.brush.stroked = true;
      this.brush.auto = 720;
      this.brush.target = this.nextSide;
      this.nextSide *= -1;
    }
  }

  down(event) {
    if (!event.isPrimary || event.button !== 0 || this.pointer) return;
    const hit = this.io.hit(event);
    if (!hit?.part) return;
    this.quiet = 0;
    const id = this.io.intent();
    const part = sleepingClip(id) && hit.part !== "pillow" ? "wake" : hit.part;
    event.preventDefault();
    this.canvas.focus({ preventScroll: true });
    this.pointer = {
      id: event.pointerId,
      part,
      wakeClip: part === "wake" ? id : null,
      x: hit.x,
      y: hit.y,
      side: hit.side,
      markX: hit.x,
      markY: hit.y,
      moved: false,
      hairOffset: hit.hairOffset ?? hit.side * 0.35,
      extremeX: hit.x,
      direction: 0,
      turns: 0,
      began: performance.now(),
    };
    this.contact = { sx: hit.sx, sy: hit.sy, part, phase: "down", age: 0 };
    this.io.invalidate?.();
    this.canvas.setPointerCapture(event.pointerId);
    if (part === "book") this.startPage();
    if (part === "cheek") {
      const clip = hit.side < 0 ? "cheekLeft" : "cheekRight";
      this.page = this.brush = null;
      this.cheek = { clip, held: true, age: 0 };
      this.io.show(clip);
      this.io.poke?.(clip, hit.side);
    }
    if (part === "head") {
      if (this.io.acceptForehead?.(hit.side)) {
        this.pointer.part = "offeredHead";
        return;
      }
      if (this.brush) {
        this.startBrush(hit.side, this.pointer.hairOffset);
        return;
      }
      this.holdTimer = setTimeout(() => {
        if (this.pointer?.id === event.pointerId)
          this.startBrush(hit.side, this.pointer.hairOffset);
      }, 230);
    }
  }

  // Count full turns at a moving extreme. Jitter near a reversal is not a stroke.
  trackStroke(touch, x, span) {
    const distance = x - touch.extremeX;
    const sign = Math.sign(distance);
    if (!sign) return;
    if (sign !== touch.direction && Math.abs(distance) < span) return;
    if (touch.direction && sign !== touch.direction) touch.turns++;
    touch.direction = sign;
    touch.extremeX = x;
  }

  move(event) {
    const touch = this.pointer;
    if (!touch || event.pointerId !== touch.id || touch.part === "rubbed") return;
    const hit = this.io.hit(event, true);
    if (!hit) return;
    const dx = hit.x - touch.x,
      dy = hit.y - touch.y;
    if (Math.hypot(dx, dy) > 8) touch.moved = true;
    if (touch.part === "offeredHead") {
      if (!hit.inside || (touch.moved && hit.part !== "head")) {
        touch.part = "outside";
        this.contact = null;
        this.io.cancelForehead();
        return;
      }
      if (this.contact) Object.assign(this.contact, { sx: hit.sx, sy: hit.sy });
      if (this.io.forehead.move(Math.hypot(dx, dy)) !== "brush") return;
      // Keep the original pointer, touch origin and stroke history during the handoff.
      touch.part = "head";
      this.startBrush(touch.side, touch.hairOffset, true);
      this.brush.still = 0;
      this.brush.restX = touch.x;
      this.brush.restY = touch.y;
    }
    // Simple taps have no drag action. Cancel their pressed state as soon as
    // the finger leaves, even if it later returns to the same animated target.
    if (!["head", "book", "cheek", "rubbed", "outside"].includes(touch.part)) {
      const sameTarget =
        touch.part === "wake" ? Boolean(hit.part && hit.part !== "pillow") : touch.part === hit.part;
      if (touch.moved || !hit.inside || !sameTarget) touch.part = "outside";
    }
    if (!hit.inside || touch.part === "outside") this.contact = null;
    else if (this.contact) {
      this.contact.sx = hit.sx;
      this.contact.sy = hit.sy;
    }
    if (touch.part === "book" && this.page) {
      if (!hit.inside || dx > 26 || Math.abs(dy) > 35) {
        touch.part = "outside";
        this.contact = null;
        this.settlePage(false, false);
      } else {
        // The free edge travels on a curved path; match the photographed
        // edge to the finger instead of scrubbing time by a fixed distance.
        const edge = PIXEL_CLIPS[this.moon ? "turnMoon" : "turnStar"].book;
        const x = edge[0][0] + dx, last = edge.length - 1;
        let nearest = x <= edge[last][0] ? last : 0;
        for (let i = 1; i <= last; i++)
          if (Math.abs(edge[i][0] - x) < Math.abs(edge[nearest][0] - x)) nearest = i;
        this.page.progress = nearest / last;
      }
    } else if (touch.part === "cheek" && this.cheek) {
      if (!hit.inside || Math.abs(dx) > 44 || Math.abs(dy) > 28) {
        touch.part = "outside";
        this.contact = null;
        this.cheek.held = false;
      } else {
        this.trackStroke(touch, hit.x, 17);
        if (touch.turns >= 2) {
          touch.part = "rubbed";
          this.cheek = null;
          this.cheekTimes = [];
          // She pulls her cheek away to giggle. Let the last contact fade
          // on that pixel, even if the finger keeps moving across the glass.
          if (this.contact) Object.assign(this.contact, { phase: "release", age: 0 });
          this.io.show("tickle");
          this.io.poke?.("tickle", touch.side);
        }
      }
    } else if (touch.part === "head") {
      if (
        !hit.inside ||
        Math.abs(dx) > 145 ||
        Math.abs(dy) > 88 ||
        (touch.moved && hit.part !== "head")
      ) {
        touch.part = "outside";
        this.contact = null;
        clearTimeout(this.holdTimer);
        this.finishBrush(false);
      } else if (touch.moved) {
        if (!this.brush) this.startBrush(touch.side, touch.hairOffset);
        this.trackStroke(touch, hit.x, 25);
        const b = this.brush;
        if (!b.fromForehead) b.direction = touch.direction;
        b.turns = touch.turns;
        b.target = Math.max(
          -1,
          Math.min(1, touch.hairOffset + dx / 59),
        );
        if (b.fromForehead && Math.hypot(hit.x - b.restX, hit.y - b.restY) > 3) {
          b.direction = Math.sign(hit.x - b.restX);
          b.still = 0;
          b.restX = hit.x;
          b.restY = hit.y;
        }
        if (Math.hypot(hit.x - touch.markX, hit.y - touch.markY) >= 22) {
          this.brush.sparks.push({ x: hit.x, y: hit.y, age: 0 });
          if (this.brush.sparks.length > 3) this.brush.sparks.shift();
          touch.markX = hit.x;
          touch.markY = hit.y;
        }
        if (!this.brush.stroked)
          this.io.note("她闭起眼，顺着你的手指把额头靠过来。");
        this.brush.stroked = true;
      }
    }
  }

  up(event) {
    const touch = this.pointer;
    if (!touch || event.pointerId !== touch.id) return;
    this.move(event);
    const hit = this.io.hit(event);
    const contact = this.contact;
    this.release();
    if (touch.part === "rubbed") return;
    if (touch.part === "offeredHead") {
      this.io.forehead.release();
    } else if (touch.part === "cheek" && this.cheek) {
      this.cheek.held = false;
      if (
        !touch.moved &&
        hit &&
        performance.now() - touch.began < 420 &&
        this.registerTap()
      ) {
        this.cheek = null;
        this.io.show("tickle");
        this.io.poke?.("tickle", touch.side);
      }
      this.nextSide = -touch.side;
    } else if (touch.part === "book" && this.page) {
      this.settlePage(
        Boolean(hit && (!touch.moved || this.page.progress >= 0.58)),
      );
    } else if (touch.part === "head" && this.brush?.releaseMs === null) {
      this.finishBrush(Boolean(hit));
    } else if (touch.part === "pillow" && hit?.part === "pillow") {
      this.io.note("你松开手，云朵枕轻轻鼓了起来。");
    } else if (hit && !touch.moved) {
      if (
        touch.part === "wake" &&
        (touch.wakeClip === this.io.intent() ||
          (touch.wakeClip === "doze" && this.io.intent().startsWith("peek")))
      )
        this.rouse(touch.side);
      else if (touch.part === hit.part) {
        this.io.care(
          touch.part === "head"
            ? touch.side < 0
              ? "patLeft"
              : "pat"
            : touch.part,
        );
      }
    }
    this.contact =
      hit && contact ? { ...contact, phase: "release", heldMs:contact.age, age: 0 } : null;
    this.io.invalidate?.();
  }

  idleTime(id, dt, autonomous = false) {
    if (this.contact) {
      this.contact.age = Math.min(200, this.contact.age + dt);
      if (this.contact.phase === "release" && this.contact.age >= 180)
        this.contact = null;
    }
    if ((id === "idle" || autonomous) && this.io.base() === "idle" && !this.pointer) {
      this.quiet += dt;
      if (this.quiet >= 28000) this.nap();
    } else this.quiet = 0;
  }

  sample(id, elapsed, dt, reduced) {
    if (id === this.cheek?.clip) {
      const cheek = this.cheek,
        clip = PIXEL_CLIPS[id];
      // The first fully compressed drawing precedes the rebound and paw lift.
      const peak = Math.ceil(0.115 * (clip.frames - 1));
      cheek.age += dt;
      if (cheek.held) {
        cheek.age = Math.min(cheek.age, (1000 * peak) / clip.fps);
        return reduced ? peak : Math.floor((cheek.age * clip.fps) / 1000);
      }
      const frame = Math.floor((cheek.age * clip.fps) / 1000);
      if (frame >= clip.frames) {
        this.cheek = null;
        this.io.restore();
      }
      return reduced
        ? Math.round(0.54 * clip.fps)
        : Math.min(frame, clip.frames - 1);
    }
    if (sleepingClip(id) && this.sleepJoin)
      return this.advanceSleep(dt, reduced);
    if (id === "doze") {
      const sample = this.sleepOffset + Math.floor(elapsed / 100);
      this.sleepFrame = reduced
        ? 15
        : sample < 15
          ? sample
          : 15 + ((sample - 15) % 20);
      return this.sleepFrame;
    }
    if (id.startsWith("peek")) {
      const last = PIXEL_CLIPS[id].frames - 1;
      if (elapsed >= 3100) {
        this.sleepOffset = 15;
        this.sleepFrame = 15;
        this.io.show("doze");
        return reduced ? last : 0;
      }
      this.sleepFrame = reduced
        ? last
        : Math.round(
            last * clampUnit(Math.min(elapsed / 460, (3100 - elapsed) / 480)),
          );
      return this.sleepFrame;
    }
    if (id === "brush" && this.brush) {
      const b = this.brush;
      for (const spark of b.sparks) spark.age += dt;
      while (b.sparks.length && b.sparks[0].age > 380) b.sparks.shift();
      if (b.auto !== undefined && elapsed >= b.auto) this.finishBrush();
      if (b.releaseMs !== null) b.releaseMs += dt;
      if (b.releaseMs > 160) b.target = 0;
      let depth =
        b.releaseMs !== null && b.releaseMs > 160 ? 0 : b.stroked ? 1 : 0.45;
      const blend = reduced ? 1 : 1 - Math.exp(-dt / 105);
      let target = b.target;
      if (b.fromForehead && b.releaseMs === null) {
        b.still += dt;
        // Once the finger rests she finishes a small nuzzle, then holds that drawing.
        const settle = reduced ? 1 : clampUnit((b.still - 190) / 220);
        depth = 0.75 + 0.25 * settle;
        target = Math.max(-1, Math.min(1, target + 0.30 * settle * b.direction));
      }
      b.depth += (depth - b.depth) * blend;
      b.position += (target - b.position) * blend;
      const pose =
        9 * Math.round(4 * b.depth) + Math.round(4 * (b.position + 1));
      if (b.releaseMs !== null && b.releaseMs >= 420) {
        this.brush = null;
        if (b.reward && this.io.base() === "idle") this.io.show("heartHug");
        else this.io.restore();
      }
      return pose;
    }
    if (bookClip(id) && this.page) {
      const page = this.page;
      const clip = PIXEL_CLIPS[id], last = clip.frames - 1;
      if (page.phase !== "drag") {
        page.age += dt;
        const forward = page.phase === "forward";
        // Frame spacing already contains the paper's acceleration over the
        // spine. Play the remaining drawings once, a little faster on recoil.
        const step = page.age * clip.fps * (forward ? 1 : 1.5) / (1000 * last);
        page.progress = reduced ? Number(forward) : clampUnit(page.from + (forward ? step : -step));
        if (page.progress === Number(forward)) {
          if (forward) this.moon = !this.moon;
          this.page = null;
          if (!forward && page.glance && page.from > 0.035)
            this.io.show(this.moon ? "glanceMoon" : "glanceStar");
          else this.io.restore();
        }
      }
      return Math.round(page.progress * last);
    }
    return null;
  }
}
