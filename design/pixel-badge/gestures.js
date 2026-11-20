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
    this.pointer = null;
    this.brush = null;
    this.page = null;
    this.holdTimer = 0;
    canvas.addEventListener("pointerdown", (event) => this.down(event));
    canvas.addEventListener("pointermove", (event) => this.move(event));
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

  cancelContact() {
    this.release();
    if (this.brush) this.finishBrush();
    if (this.page?.phase === "drag") this.settlePage(false);
  }

  interrupt() {
    this.release();
    this.brush = null;
    this.page = null;
    this.quiet = 0;
    this.cheekTimes = [];
  }

  poke(side) {
    const now = performance.now();
    this.release();
    this.page = this.brush = null;
    this.quiet = 0;
    this.cheekTimes = this.cheekTimes.filter((stamp) => now - stamp < 1100);
    this.cheekTimes.push(now);
    if (this.cheekTimes.length >= 3) {
      this.cheekTimes = [];
      this.io.show("tickle");
    } else {
      this.io.show(side < 0 ? "cheekLeft" : "cheekRight");
    }
    this.nextSide = -side;
  }

  nap() {
    if (this.io.base() !== "idle") return;
    this.interrupt();
    this.sleepOffset = 0;
    this.io.show("doze");
  }

  rouse(side) {
    const id = this.io.intent();
    this.interrupt();
    if (id === "doze") this.io.show(side < 0 ? "peekLeft" : "peekRight");
    else this.io.show(id === "peekLeft" ? "wakeLeft" : "wakeRight");
  }

  startBrush(side) {
    clearTimeout(this.holdTimer);
    this.cheekTimes = [];
    this.quiet = 0;
    this.brush = {
      position: side * 0.35,
      target: side * 0.35,
      depth: 0,
      stroked: false,
      releaseMs: null,
      sparks: [],
    };
    this.io.show("brush");
  }

  finishBrush() {
    const brush = this.brush;
    if (!brush || brush.releaseMs !== null) return;
    brush.releaseMs = 0;
    brush.target = Math.max(-1, Math.min(1, brush.target * 1.24));
    if (brush.stroked) this.io.note("你的手停下了。她又往刚才的方向蹭了一下。");
  }

  startPage() {
    this.brush = null;
    this.cheekTimes = [];
    this.quiet = 0;
    this.page = { phase: "drag", progress: 0, from: 0, age: 0 };
    this.io.show(this.moon ? "turnMoon" : "turnStar");
  }

  settlePage(commit) {
    if (!this.page) return;
    Object.assign(this.page, {
      phase: commit ? "forward" : "back",
      from: this.page.progress,
      age: 0,
    });
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
    if (!hit) return;
    this.quiet = 0;
    const id = this.io.intent();
    if (sleepingClip(id)) hit.part = "wake";
    if (!hit.part) return;
    event.preventDefault();
    this.canvas.focus({ preventScroll: true });
    this.pointer = {
      id: event.pointerId,
      part: hit.part,
      x: hit.x,
      y: hit.y,
      side: hit.side,
      markX: hit.x,
      markY: hit.y,
      moved: false,
    };
    this.canvas.setPointerCapture(event.pointerId);
    if (hit.part === "book") this.startPage();
    if (hit.part === "head") {
      this.holdTimer = setTimeout(() => {
        if (this.pointer?.id === event.pointerId) this.startBrush(hit.side);
      }, 230);
    }
  }

  move(event) {
    const touch = this.pointer;
    if (!touch || event.pointerId !== touch.id) return;
    const hit = this.io.hit(event, true);
    if (!hit) return;
    const dx = hit.x - touch.x,
      dy = hit.y - touch.y;
    if (Math.hypot(dx, dy) > 8) touch.moved = true;
    if (touch.part === "book" && this.page) {
      this.page.progress = clampUnit(-dx / 88);
    } else if (touch.part === "head") {
      if (
        !hit.inside ||
        Math.abs(dx) > 145 ||
        Math.abs(dy) > 88 ||
        (touch.moved && hit.part !== "head")
      ) {
        touch.part = "outside";
        clearTimeout(this.holdTimer);
        this.finishBrush();
      } else if (touch.moved) {
        if (!this.brush) this.startBrush(touch.side);
        this.brush.target = Math.max(
          -1,
          Math.min(1, touch.side * 0.25 + dx / 59),
        );
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
    this.release();
    if (touch.part === "book" && this.page) {
      this.settlePage(
        Boolean(hit && (!touch.moved || this.page.progress >= 0.58)),
      );
      return;
    }
    if (touch.part === "head" && this.brush?.releaseMs === null)
      return this.finishBrush();
    if (!hit || touch.moved) return;
    if (touch.part === "wake") return this.rouse(touch.side);
    if (touch.part === "cheek") return this.poke(touch.side);
    if (touch.part === "head")
      return this.io.care(touch.side < 0 ? "patLeft" : "pat");
    if (touch.part !== "outside") this.io.care(touch.part);
  }

  idleTime(id, dt) {
    if (id === "idle" && this.io.base() === "idle" && !this.pointer) {
      this.quiet += dt;
      if (this.quiet >= 28000) this.nap();
    } else this.quiet = 0;
  }

  sample(id, elapsed, dt, reduced) {
    if (id === "doze") {
      const sample = this.sleepOffset + Math.floor(elapsed / 100);
      return reduced ? 15 : sample < 15 ? sample : 15 + ((sample - 15) % 20);
    }
    if (id.startsWith("peek")) {
      const last = PIXEL_CLIPS[id].frames - 1;
      if (elapsed >= 3100) {
        this.sleepOffset = 15;
        this.io.show("doze");
      }
      if (reduced) return last;
      return Math.round(
        last * clampUnit(Math.min(elapsed / 460, (3100 - elapsed) / 480)),
      );
    }
    if (id === "brush" && this.brush) {
      const b = this.brush;
      for (const spark of b.sparks) spark.age += dt;
      while (b.sparks.length && b.sparks[0].age > 380) b.sparks.shift();
      if (b.auto !== undefined && elapsed >= b.auto) this.finishBrush();
      if (b.releaseMs !== null) b.releaseMs += dt;
      const depth =
        b.releaseMs !== null && b.releaseMs > 160 ? 0 : b.stroked ? 1 : 0.45;
      const blend = reduced ? 1 : 1 - Math.exp(-dt / 105);
      b.depth += (depth - b.depth) * blend;
      b.position += (b.target - b.position) * blend;
      const pose =
        9 * Math.round(4 * b.depth) + Math.round(4 * (b.position + 1));
      if (b.releaseMs !== null && b.releaseMs >= 420) {
        this.brush = null;
        this.io.restore();
      }
      return pose;
    }
    if (bookClip(id) && this.page) {
      const page = this.page;
      if (page.phase !== "drag") {
        page.age += dt;
        const forward = page.phase === "forward";
        const u = clampUnit(page.age / (forward ? 420 : 240));
        const blend = reduced ? 1 : 1 - (1 - u) ** 3;
        page.progress = page.from + ((forward ? 1 : 0) - page.from) * blend;
        if (u === 1) {
          if (forward) this.moon = !this.moon;
          this.page = null;
          if (!forward && page.from > 0.035)
            this.io.show(this.moon ? "glanceMoon" : "glanceStar");
          else this.io.restore();
        }
      }
      return Math.round(page.progress * (PIXEL_CLIPS[id].frames - 1));
    }
    return null;
  }
}
