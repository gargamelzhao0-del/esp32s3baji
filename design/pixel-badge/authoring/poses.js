/* Offline poses for the project's original GptCompanion. Coordinates are in rig space. */
window.startBadgeTake = async function ({ name, scene }) {
  const canvas = document.createElement("canvas");
  canvas.width = 612;
  canvas.height = 540;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  const fill = ctx.fillRect;
  ctx.fillRect = function (x, y, w, h) {
    if (
      this.globalCompositeOperation === "destination-over" &&
      x === -1 &&
      y === -1 &&
      w === 2 &&
      h === 2
    )
      return;
    return fill.call(this, x, y, w, h);
  };
  const pet = new GptCompanion(canvas, { manual: true, seed: 10773 });
  await pet.ready;
  const book = [
    "reading",
    "readingMoon",
    "turnStar",
    "turnMoon",
    "glanceStar",
    "glanceMoon",
  ].includes(name);
  const turn = name.startsWith("turn");
  const glance = name.startsWith("glance");
  const completed = scene.endsWith("Done");
  const job =
    book || scene === "readingMoon" ? "reading" : scene.replace(/Done$/, "");
  window.moonChart = name.endsWith("Moon") || scene === "readingMoon";
  const persistentPaper = job === "writing" || job === "compact";
  const gearCanvas =
    persistentPaper && name === job ? canvas.cloneNode() : null;
  pet.setScene({
    state: "idle",
    scope: null,
    active: true,
    reduced: false,
    offline: false,
  });
  const baseline = { ...pet.pose };
  const side = name.endsWith("Left") ? -1 : 1;
  const nearEye = side < 0 ? "winkL" : "winkR";
  const farEye = side < 0 ? "winkR" : "winkL";
  function ease(t) {
    t = Math.max(0, Math.min(1, t));
    return t * t * (3 - 2 * t);
  }
  function pulse(t, a, b, c) {
    return ease((t - a) / (b - a)) * (1 - ease((t - b) / (c - b)));
  }
  // A held drawing has an attack, a readable contact pose, then a release.
  function held(t, start, peak, last, end) {
    return (
      ease((t - start) / (peak - start)) * (1 - ease((t - last) / (end - last)))
    );
  }
  function watchStar(p, dx, dy, weight) {
    p.gazeX += 1.32 * dx * weight;
    p.gazeY += 1.15 * dy * weight;
    p.head += 4.5 * dx * weight;
    p.headX += 6 * dx * weight;
    p.nod += 6 * dy * weight;
    p.lean += 3 * dx * weight;
    p.leftX -= 8 * weight;
    p.rightX += 8 * weight;
    p.leftY -= (18 + 10 * Math.max(0, -dx)) * weight;
    p.rightY -= (18 + 10 * Math.max(0, dx)) * weight;
    p.palmOpen = { left: 0.95 * weight, right: 0.95 * weight };
    p.lid += 0.12 * weight;
    p.tailTip -= 12 * dx * weight;
  }
  function brushPose(p, contact, lean) {
    p.gazeX += lean * 0.8 * (1 - contact);
    p.lid *= 1 - 0.97 * contact;
    p.smile += 0.96 * contact;
    p.blush += 0.65 * contact;
    p.headX += lean * 18 * contact;
    p.head += lean * 10 * contact;
    p.nod -= 6 * contact;
    p.hair += lean * 5 * contact;
    p.tassel += lean * 9 * contact;
    p.tuft += lean * 10 * contact;
    p.tailBend -= lean * 17 * contact;
    p.leftWing += 6 * contact;
    p.rightWing += 5 * contact;
    if (job === "idle") {
      p.leftY -= (14 + 10 * Math.max(0, -lean)) * contact;
      p.rightY -= (14 + 10 * Math.max(0, lean)) * contact;
      p.leftX += 12 * contact;
      p.rightX -= 12 * contact;
    }
  }
  function enterBrush(p, frame) {
    const rested = frame >= 83;
    const step = frame - (rested ? 83 : 77);
    if (step === 5) {
      brushPose(p, rested ? 1 : 0.5, side * 0.75);
      return;
    }
    const target = { ...p };
    browInvitation(p, rested ? 36 : 23);
    if (!step) return;
    brushPose(target, rested ? 1 : 0.5, side * 0.75);
    const mix = ease(step / 5);
    // The face multiplies the global lid by each eye's wink. Preserve
    // its actual aperture through the transfer from wink to closed lids.
    const aperture = (eye) =>
      p.lid * (1 - p[eye]) * (1 - mix) + target.lid * (1 - target[eye]) * mix;
    const left = aperture("winkL"), right = aperture("winkR");
    for (const key of Object.keys(target)) {
      if (typeof target[key] === "number") p[key] += (target[key] - p[key]) * mix;
    }
    p.winkL = 1 - left / p.lid;
    p.winkR = 1 - right / p.lid;
  }
  // All four endings start at the very same offered pose. A resting finger
  // can step backward through the nuzzle from the exact drawing it reached.
  function browInvitation(p, frame) {
    const rise = (f, start, end) => ease((f - start) / (end - start));
    const hand = side < 0 ? "left" : "right";
    const otherHand = side < 0 ? "right" : "left";
    let offer = 1, comb = 0, sweep = 0, peek = 0, nest = 0, settle = 0;
    if (frame < 24) {
      // Her paw goes up to the side lock first. As it falls, her uncovered
      // forehead stays tipped out. The eyes return to the viewer last.
      comb = held(frame, 0, 6, 9, 17);
      sweep = pulse(frame, 5, 9, 14);
      peek = held(frame, 0, 4, 12, 22);
      offer = rise(frame, 9, 19);
    } else if (frame < 37) {
      const f = frame - 24;
      nest = rise(f, 0, 8);
      settle = rise(f, 5, 12);
    } else if (frame < 55) {
      const f = frame - 37;
      comb = held(f, 0, 5, 8, 15);
      sweep = pulse(f, 4, 8, 12);
      peek = held(f, 0, 3, 9, 15);
      offer = 1 - rise(f, 6, 17);
    } else if (frame < 65) {
      const f = frame - 55;
      offer = 1 - rise(f, 2, 9);
      p.blink = 0.94 * pulse(f, 0, 2, 5);
      p.smile += 0.48 * pulse(f, 0, 4, 9);
    } else {
      const f = frame - 65;
      offer = 1 - rise(f, 4, 11);
      const look = held(f, 0, 3, 6, 11);
      p.gazeX -= side * 1.2 * look;
      p.head -= side * 3.3 * pulse(f, 2, 6, 11);
      p.tuft -= side * 5 * pulse(f, 3, 7, 11);
    }
    p[hand + "Y"] -= 180 * comb;
    p[hand + "X"] += side * (18 * comb - 18 * sweep);
    p.palmOpen = { [hand]: 0.96 * comb, [otherHand]: 0 };
    p.nod += 5 * comb - 5 * offer - 3 * nest;
    p.head += side * (3.7 * comb * (1 - offer) + 5.7 * offer + 1.8 * nest);
    p.headX += side * (3 * comb + 5 * offer + 7 * nest);
    p.lean += side * (2.5 * offer + 1.8 * nest);
    p.gazeX += side * 0.85 * peek;
    p.gazeY -= 0.64 * peek + 0.12 * offer;
    p.hair += side * (3 * sweep - 2.5 * settle);
    p.tassel -= side * (3.8 * offer + 2 * nest);
    p.tuft += side * (6 * sweep - 2.5 * settle);
    p[nearEye] += 0.99 * nest;
    p[farEye] += 0.98 * settle;
    p.smile += 0.21 * offer + 0.77 * nest;
    p.blush += 0.28 * offer + 0.54 * settle;
    p[otherHand + "Y"] -= 7 * offer + 8 * nest;
    p[otherHand + "X"] -= side * 7 * nest;
    p[otherHand + "Wing"] += 4.5 * offer - 7 * settle;
    p.tailLift -= 8 * offer - 17 * settle;
    p.tailTip -= 5 * offer - 13 * settle;
  }
  function sleeping(p, depth, phase) {
    p.lid *= 1 - depth;
    p.nod += (19 + 2 * Math.cos(phase)) * depth;
    p.head += 7 * depth;
    p.breath += 1.8 * Math.sin(phase) * depth;
    p.tailLift -= 32 * depth;
    p.tailBend -= 25 * depth;
    p.tailTip -= 16 * depth;
    p.leftWing -= 14 * depth;
    p.rightWing -= 13 * depth;
    p.leftX += 12 * depth;
    p.rightX -= 12 * depth;
    // Bring the paws under the chin; wings shut before the last sleepy breath.
    p.leftX += 32 * depth;
    p.rightX -= 39 * depth;
    p.leftY -= 34 * depth;
    p.rightY -= 27 * depth;
    p.blush += 0.32 * depth;
  }
  function workPose(p, u, holding) {
    const cycle = Math.PI * 2 * u,
      sway = Math.sin(cycle);
    const beat = (a, b, c) => pulse(u, a, b, c);
    const lift = beat(0.08, 0.24, 0.52);
    p.stage = { ...baseline.stage, workMix: 1 };
    p.time = baseline.time + u * 4;
    p.breath += 2 * sway;
    p.tailTip += 6 * Math.sin(cycle - 0.4);
    p.blink = 0.96 * beat(0.72, 0.76, 0.83);
    p.badgeJob = job;
    p.jobPhase = u;
    if (job === "reading") {
      p.book = 1;
      p.work = { id: "reading", phase: "offer", progress: 0 };
      p.leftX += 5;
      p.rightX -= 6;
      p.leftY -= 17;
      p.rightY -= 18;
      p.nod += 5 + 2 * sway;
      p.gazeX += 0.72 * (2 * beat(0.03, 0.61, 0.86) - 1);
      p.gazeY += 0.75;
    } else if (job === "working") {
      p.leftX += 20 + 6 * lift;
      p.leftY -= 21 + 17 * lift;
      p.head -= 4 + 4 * lift;
      p.gazeX -= 0.28 + 0.48 * lift;
      p.gazeY -= 0.31 + 0.3 * lift;
      p.tuft += 9 * beat(0.26, 0.42, 0.67);
    } else if (job === "running" || job === "compact") {
      p.book = 1; // Render the thumbs in front of the pocket device or tickets.
      p.leftY -= 14;
      p.leftX += 4;
      p.rightX -= job === "running" ? 88 : 66;
      const tap = beat(0.04, 0.17, 0.29) + beat(0.36, 0.46, 0.59);
      p.rightY -= job === "running" ? 8 + 22 * (1 - tap) : 13 + 21 * lift;
      p.rightX -= job === "compact" ? 15 * lift : 3 * tap;
      p.nod += 4 + 2 * tap;
      p.gazeY += 0.65;
      p.gazeX += 0.34 * sway;
      p.badgeContact = tap;
    } else if (job === "writing") {
      p.pad = 1;
      p.leftY -= 12;
      p.writing = DragonWriting.beat(5.12 * u);
      p.penPark = 0;
      p.nod += 5;
      p.head += 3;
      p.gazeY += 0.68 - 0.57 * p.writing.peek;
      p.gazeX += p.writing.x / 80;
      p.smile += 0.46 * p.writing.peek;
    } else if (job === "searching") {
      p.lens = 1;
      p.rightY -= 31 + 17 * lift;
      p.rightX -= 7 + 11 * lift;
      p.head += 4 + 4 * sway;
      p.gazeX += 0.48 + 0.32 * sway;
      p.gazeY += 0.48 - 0.72 * lift;
      p.winkL += 0.68 * lift;
    } else if (job === "waiting") {
      p.work = { id: "waiting" };
      p.leftY -= 32 + 10 * lift;
      p.leftX -= 10 + 5 * sway;
      p.head -= 5;
      p.gazeY -= 0.18;
      p.tuft += 6 * lift;
      p.smile += 0.2;
    } else if (job === "succeeded") {
      const spring = beat(0.17, 0.38, 0.66);
      p.nod += 7 * beat(0, 0.14, 0.26) - 5 * spring;
      p.jump += 17 * spring;
      p.leftY -= 35 * spring;
      p.rightY -= 32 * beat(0.2, 0.43, 0.7);
      p.wing += 17 * spring;
      p.tailLift += 22 * beat(0.25, 0.55, 0.81);
      p.smile += 0.48 + 0.6 * spring;
      p.blush += 0.38;
    } else if (job === "error") {
      p.head -= 4 + 4 * lift;
      p.leftWing -= 12 * lift;
      p.rightWing -= 10 * lift;
      p.leftY -= 16 * lift;
      p.rightY -= 24 * lift;
      p.gazeY -= 0.28;
      p.pout += 0.58;
    } else if (job === "limited" || job === "ended" || job === "cancelled") {
      const settle = ease(u / 0.72);
      p.nod += (job === "limited" ? 9 : 5) * settle;
      p.lid -= (job === "limited" ? 0.43 : 0.16) * settle;
      p.wing -= 5 * settle;
      p.tailLift -= 10 * settle;
      p.jaw += (job === "ended" ? 0.56 : 0.15) * lift;
      if (job === "cancelled") {
        p.leftY -= 22 * lift;
        p.leftX -= 12 * lift;
      }
    }
    if (holding < 1) {
      for (const k of Object.keys(baseline)) {
        if (
          typeof baseline[k] === "number" &&
          typeof p[k] === "number" &&
          !["book", "pad", "lens"].includes(k)
        )
          p[k] = baseline[k] + (p[k] - baseline[k]) * holding;
      }
      // Lower both wrists together. The object passes behind the cuffs only at the end.
      const tuck = Math.sin(Math.PI * holding);
      p.leftY += 24 * tuck;
      p.rightY += 18 * tuck;
      p.nod += 3 * tuck;
      p.stage.workMix = ease(holding / 0.24);
    }
  }
  window.sampleBadgeTake = ({ i, n, fps, settle }) => {
    const t = i / fps,
      u = i / (n - 1);
    let p = { ...baseline, blink: 0 };
    const stow = name.startsWith("stow-");
    const workTime = completed
      ? 1
      : settle != null
        ? Math.min(1, i / settle)
        : name === job || name === "readingMoon"
          ? u
          : 0;
    if (job !== "idle") workPose(p, workTime, stow ? 1 - ease(u) : 1);
    if (settle != null && i > settle) {
      const rest = (i - settle) / (n - 1 - settle),
        wave = Math.sin(rest * Math.PI * 2);
      p.breath += 1.8 * wave;
      p.tailTip += 4 * wave;
      p.blink = 0.97 * pulse(rest, 0.59, 0.63, 0.7);
      p.gazeX += 0.26 * pulse(rest, 0.17, 0.31, 0.57);
    }
    if (book) {
      p.work.progress = turn ? u : 0;
      if (turn) {
        const curl = Math.sin(Math.PI * u);
        // Meet the corner, track it across the fold, and settle at the first line.
        p.gazeX += 1.55 * held(u, 0, 0.16, 0.25, 0.87);
        p.gazeY -= 0.35 * curl;
        p.rightX -= 15 * curl;
        p.rightY -= 13 * curl;
        p.head -= 3 * curl;
      }
      if (glance) {
        const look = pulse(u, 0, 0.32, 1);
        p.gazeY -= 1.5 * look;
        p.lid += 0.17 * look;
        p.head -= 5 * look;
        p.nod -= 10 * look;
        p.smile += 0.6 * look;
        p.blush += 0.33 * look;
      }
    } else if (name === "keyChime") {
      const contact = pulse(u, 0.06, 0.21, 0.34) + pulse(u, 0.41, 0.54, 0.69);
      const happy = pulse(u, 0.36, 0.64, 1);
      p.rightY += 22 * contact;
      p.rightX -= 3 * contact;
      p.badgeContact = contact;
      p.nod += 4 * contact;
      p.gazeY -= 0.7 * happy;
      p.smile += 0.72 * happy;
      p.winkL += 0.9 * happy;
      p.tailLift += 14 * happy;
    } else if (name === "lensFind") {
      const focus = pulse(u, 0, 0.22, 0.53),
        found = pulse(u, 0.31, 0.59, 1);
      p.gazeX += 0.75 * focus + 0.43 * found;
      p.rightX += 14 * focus + 5 * found;
      p.rightY -= 21 * focus + 14 * found;
      p.winkL += 0.94 * focus;
      p.head += 5 * focus;
      p.lid += 0.16 * found;
      p.jaw += 0.55 * found;
      p.smile += 0.73 * found;
      p.tuft += 9 * found;
      p.nod -= 6 * found;
      p.leftWing += 11 * pulse(u, 0.39, 0.62, 0.94);
      p.tailBend -= 14 * pulse(u, 0.48, 0.71, 1);
    } else if (name === "palmTap") {
      const reach = pulse(u, 0, 0.26, 0.52),
        nod = pulse(u, 0.27, 0.57, 1);
      const highFive = held(u, 0, 0.14, 0.27, 0.56);
      p.leftY -= 42 * highFive;
      p.leftX -= 16 * highFive;
      p.headX -= 5 * reach;
      p.gazeX -= 0.69 * highFive;
      p.tassel += 8 * pulse(u, 0.29, 0.43, 0.65);
      p.lid *= 1 - 0.91 * nod;
      p.nod += 8 * nod;
      p.smile += 0.93 * nod;
      p.leftWing += 15 * nod;
      p.rightWing += 8 * pulse(u, 0.39, 0.7, 1);
      p.blush += 0.59 * nod;
    } else if (name.startsWith("cheek")) {
      const press = held(u, 0, 0.115, 0.19, 0.37),
        rebound = pulse(u, 0.22, 0.43, 0.66),
        shy = held(u, 0.17, 0.38, 0.57, 0.93);
      p[nearEye] += 0.98 * press + 0.43 * shy;
      p[farEye] += 0.12 * press;
      p.headX -= side * (17 * press - 6 * rebound);
      p.head -= side * (7 * press - 4 * rebound);
      p.gazeX += side * (0.76 * shy + 0.4 * press);
      p.pout += 0.87 * press;
      p.smile += 0.98 * shy;
      p.blush += 0.9 * shy;
      p.tassel -= side * 13 * pulse(t, 0.12, 0.3, 0.69);
      p.hair -= side * 5 * rebound;
      p.tuft += side * 8 * rebound;
      p[side < 0 ? "leftWing" : "rightWing"] += 12 * pulse(t, 0.05, 0.22, 0.72);
      if (job === "idle") {
        // Her paw comes up to check the cheek after the finger has gone.
        const paw = held(u, 0.2, 0.4, 0.58, 0.91);
        p[side < 0 ? "leftY" : "rightY"] -= 66 * paw;
        p[side < 0 ? "leftX" : "rightX"] -= side * 14 * paw;
      }
    } else if (name === "tickle") {
      const flinch = pulse(u, 0, 0.1, 0.23);
      const first = pulse(u, 0.1, 0.22, 0.39),
        second = pulse(u, 0.32, 0.48, 0.66);
      const giggle = held(u, 0.16, 0.29, 0.53, 0.79),
        sneak = pulse(u, 0.58, 0.74, 1);
      p.head -= 5 * flinch + 4 * first - 5 * second;
      p.nod += 5 * flinch - 6 * first - 5 * second;
      p.lid *= 1 - 0.98 * giggle;
      p.winkR += 0.88 * sneak;
      p.jaw += 0.5 * flinch + first + 0.83 * second;
      p.smile += 1.1 * giggle + 0.67 * sneak;
      p.blush += 0.87 * giggle + 0.62 * sneak;
      if (job === "idle") {
        p.leftY -= 46 * giggle + 12 * first;
        p.rightY -= 55 * giggle + 10 * second;
        p.leftX += 21 * giggle;
        p.rightX -= 28 * giggle;
      }
      p.leftWing += 17 * first + 9 * second;
      p.rightWing += 11 * first + 16 * second;
      p.tassel += 9 * first - 11 * second;
      p.tailLift -= 14 * pulse(u, 0.3, 0.56, 0.92);
    } else if (name === "brush") {
      // Rows represent contact pressure; columns follow the finger from left to right.
      const contact = Math.floor(i / 9) / 4,
        lean = ((i % 9) - 4) / 4;
      brushPose(p, contact, lean);
    } else if (name.startsWith("browInvite")) {
      if (i < 77) browInvitation(p, i);
      else enterBrush(p, i);
    } else if (name === "starWatch") {
      watchStar(p, ((i % 9) - 4) / 4, Math.floor(i / 9) - 1, 1);
    } else if (name.startsWith("starCatch")) {
      const hold = 1 - ease((u - 0.67) / 0.33);
      const cradle = held(u, 0.05, 0.34, 0.53, 0.86),
        proud = held(u, 0.26, 0.44, 0.6, 0.92);
      watchStar(p, 0.75 * side, 1, hold);
      const hand = side < 0 ? "left" : "right";
      p[hand + "X"] -= side * 34 * cradle;
      p[hand + "Y"] -= 54 * cradle;
      p.palmOpen[hand] = Math.max(p.palmOpen[hand], cradle);
      p.nod += 5 * pulse(u, 0, 0.11, 0.27) - 6 * proud;
      p.head += side * 3 * cradle;
      p.gazeX -= side * 0.68 * proud;
      p.gazeY -= 0.94 * proud;
      p[farEye] += 0.94 * proud;
      p.smile += 0.93 * proud;
      p.blush += 0.64 * proud;
      p[side < 0 ? "leftWing" : "rightWing"] += 16 * pulse(u, 0.36, 0.54, 0.83);
      p.tailTip += 12 * pulse(u, 0.51, 0.71, 1);
    } else if (name === "heartHug") {
      const gather = held(u, 0, 0.24, 0.57, 0.79),
        offer = held(u, 0.28, 0.46, 0.56, 0.72);
      const follow = held(u, 0.53, 0.69, 0.79, 1);
      p.leftX += 76 * gather;
      p.rightX -= 78 * gather;
      p.leftY -= 35 * gather + 12 * offer;
      p.rightY -= 33 * gather + 14 * offer;
      p.palmOpen = { left: offer, right: offer };
      p.nod += 6 * gather - 13 * offer - 7 * follow;
      p.gazeY += 0.93 * gather - 0.8 * offer - 0.65 * follow;
      p.gazeX += 0.89 * follow;
      p.head += 5 * follow;
      p.lid *= 1 - 0.94 * held(u, 0.03, 0.15, 0.2, 0.36);
      p.winkL += 0.8 * pulse(u, 0.42, 0.54, 0.68);
      p.smile += 0.74 * gather + 0.58 * follow;
      p.blush += 0.7 * gather + 0.31 * follow;
      p.leftWing += 16 * pulse(u, 0.41, 0.57, 0.79);
      p.rightWing += 15 * pulse(u, 0.44, 0.61, 0.84);
      p.tailLift -= 16 * pulse(u, 0.49, 0.69, 0.99);
      p.heartTime = u;
    } else if (
      ["wingLeft", "wing", "tail", "patLeft", "pat", "nose", "sneeze"].includes(
        name,
      )
    ) {
      const touch = pulse(u, 0, 0.17, 0.45);
      const follow = pulse(u, 0.13, 0.43, 0.78);
      const settle = pulse(u, 0.41, 0.7, 1);
      if (name.startsWith("wing")) {
        const near = side < 0 ? "leftWing" : "rightWing";
        const far = side < 0 ? "rightWing" : "leftWing";
        p[near] += 22 * touch - 12 * follow + 5 * settle;
        p[far] += 5 * follow;
        p.gazeX += side * 0.82 * follow;
        p.head += side * 4 * follow;
        p[nearEye] += 0.55 * touch;
        p.smile += 0.51 * settle;
      } else if (name === "tail") {
        p.tailBend -= 30 * touch - 19 * follow;
        p.tailLift += 23 * follow;
        p.tailTip -= 22 * follow - 13 * settle;
        p.gazeX += 0.83 * follow;
        p.head += 5 * follow;
        p.winkR += 0.5 * touch;
        p.smile += 0.54 * settle;
      } else if (name.startsWith("pat")) {
        p.gazeX += side * 0.74 * touch;
        p.lid *= 1 - 0.95 * follow;
        p.headX += side * 11 * follow;
        p.head += side * 7 * follow;
        p.nod -= 9 * follow;
        p.tassel += side * 8 * settle;
        p.smile += 0.94 * follow + 0.4 * settle;
        p.blush += 0.63 * follow;
        const nestle = held(u, 0.25, 0.39, 0.53, 0.77);
        p.lean += side * 5 * nestle;
        p.nod -= 5 * nestle;
        p[side < 0 ? "leftWing" : "rightWing"] +=
          13 * pulse(u, 0.34, 0.53, 0.82);
        if (job === "idle") {
          p[side < 0 ? "leftY" : "rightY"] -= 37 * nestle;
          p[side < 0 ? "leftX" : "rightX"] -= side * 13 * nestle;
        }
      } else if (name === "nose") {
        p.gazeY += 0.82 * touch;
        p.nod -= 5 * touch;
        p.squint += 0.73 * follow;
        p.pout += 0.9 * follow;
        p.head += 2 * settle;
        p.blush += 0.43 * settle;
      } else {
        const snap = pulse(u, 0.37, 0.49, 0.66);
        p.nod -= 8 * follow - 16 * snap;
        p.lid *= 1 - 0.99 * snap;
        p.jaw += 0.52 * follow + 0.75 * snap;
        p.wing += 13 * snap;
        p.tuft += 12 * settle;
        p.tassel -= 13 * settle;
        p.blush += 0.48 * settle;
      }
    } else if (name === "idleLookLeft" || name === "idleLookRight") {
      // Eyes lead, head and fringe follow; the whole pose returns to baseline
      // so the clip can be interrupted at any frame.
      const look = Math.sin(Math.PI * u);
      p.gazeX -= side * 0.95 * look;
      p.gazeY += 0.12 * look;
      p.head -= side * 3.2 * look;
      p.headX -= side * 4.5 * look;
      p.tuft -= side * 5 * look;
      p.tassel -= side * 3 * look;
      p.lean -= side * 1.8 * look;
      p.nod += 1.2 * look;
      p.tailTip += side * 6 * look;
      p.tailLift += 2 * look;
      p.smile += 0.18 * look;
      p.blush += 0.08 * look;
      p.blink = 0.9 * pulse(u, 0.54, 0.62, 0.72);
    } else if (name.startsWith("idleGroom")) {
      // A quick, self-conscious hair tidy: paw up, comb the side lock, settle.
      const comb = held(u, 0, 0.22, 0.58, 0.86);
      const settle = held(u, 0.5, 0.7, 0.85, 1);
      const hand = side < 0 ? "left" : "right";
      const other = side < 0 ? "right" : "left";
      p[hand + "Y"] -= 170 * comb;
      p[hand + "X"] += side * (20 * comb - 14 * settle);
      p.palmOpen = { [hand]: 0.96 * comb, [other]: 0 };
      p.head += side * 4.2 * comb;
      p.headX += side * 5 * comb;
      p.gazeX += side * 0.55 * comb;
      p.gazeY -= 0.18 * comb;
      p.hair += side * 3.5 * comb - side * 2 * settle;
      p.tuft += side * (7 * comb - 4 * settle);
      p.tassel -= side * 3 * comb;
      p.nod += 2 * comb;
      p.smile += 0.24 * comb;
      p.blush += 0.2 * comb;
      p[other + "Y"] -= 4 * comb;
      p.tailTip -= 4 * comb;
      p.blink = 0.9 * pulse(u, 0.62, 0.7, 0.8);
    } else if (name === "idleStretch") {
      // Both paws up, wings open, a small yawn, then everything settles.
      const up = held(u, 0, 0.25, 0.62, 0.92);
      p.leftY -= 92 * up;
      p.rightY -= 88 * up;
      p.leftX += 17 * up;
      p.rightX -= 17 * up;
      p.leftWing += 20 * up;
      p.rightWing += 18 * up;
      p.nod -= 7 * up;
      p.head += 1.5 * up;
      p.lid *= 1 - 0.55 * up;
      p.jaw += 0.35 * pulse(u, 0.05, 0.25, 0.45);
      p.tailLift += 10 * up;
      p.tailTip += 6 * up;
      p.smile += 0.45 * up;
      p.blush += 0.15 * up;
      p.breath += 1.5 * Math.sin(Math.PI * u);
      p.blink = 0.92 * pulse(u, 0.58, 0.66, 0.76);
    } else if (name === "idleHum") {
      // Eyes closed, a tiny sway, the tail keeps the beat.
      const hum = Math.sin(Math.PI * u);
      const sway = Math.sin(u * Math.PI * 2);
      p.lid *= 1 - 0.78 * hum;
      p.smile += 0.85 * hum;
      p.blush += 0.3 * hum;
      p.head += 2.6 * sway * hum;
      p.headX += 1.6 * sway * hum;
      p.nod += 1.5 * sway;
      p.breath += 2.2 * sway;
      p.tailTip += 7 * Math.sin(u * Math.PI * 2 - 0.4);
      p.tailLift += 3 * hum;
      p.leftWing += 3 * hum;
      p.rightWing += 2.5 * hum;
    } else if (name === "idleYawn") {
      // A full yawn that returns to the open-eyed idle pose.
      const open = held(u, 0, 0.18, 0.55, 0.9);
      const wake = pulse(u, 0.62, 0.78, 1);
      p.jaw += 1.05 * open;
      p.lid *= 1 - 0.9 * open;
      p.nod += 8 * open;
      p.head += 3 * open;
      p.leftWing -= 8 * open;
      p.rightWing -= 7 * open;
      p.tailLift -= 10 * open;
      p.blush += 0.2 * open;
      p.smile += 0.2 * wake;
      p.nod -= 4 * wake;
      p.tuft += 4 * wake;
      p.blink = 0.9 * pulse(u, 0.58, 0.65, 0.74);
    } else if (name === "doze") {
      const depth = ease(i / 15),
        phase = (Math.max(0, i - 15) * Math.PI * 2) / 20;
      sleeping(p, depth, phase);
      p.jaw += 1.04 * pulse(i, 0, 6, 14);
      const cover = pulse(i, 0, 6, 14);
      p.leftY -= 62 * cover;
      p.leftX += 33 * cover;
      p.squint += 0.74 * cover;
    } else if (name.startsWith("peek")) {
      const peek = ease(i / 8);
      sleeping(p, 1, 0);
      p.lid = 0.82 * peek;
      p[farEye] = 1;
      p.gazeX += side * 0.65 * peek;
      p.nod -= 7 * peek;
      p.smile += 0.47 * peek;
      p.tuft += 4 * peek;
    } else if (name.startsWith("wake")) {
      const up = ease(t / 0.62),
        first = pulse(t, 0.05, 0.51, 1.31),
        second = pulse(t, 0.31, 0.86, 1.65);
      sleeping(p, 1 - up, 0);
      p.lid = 0.82 + 0.18 * up;
      p[farEye] = 1 - up;
      p.gazeX += side * 0.65 * (1 - up);
      p.nod -= 7 * (1 - up) + 5 * first;
      const near = side < 0 ? "left" : "right",
        far = side < 0 ? "right" : "left";
      p[near + "Wing"] += 23 * first;
      p[far + "Wing"] += 22 * second;
      p[near + "Y"] -= 66 * first;
      p[far + "Y"] -= 60 * second;
      p[near + "X"] += side * 23 * first;
      p[far + "X"] -= side * 21 * second;
      p.lid *= 1 - 0.56 * pulse(u, 0.28, 0.45, 0.64);
      p.smile += 0.47 * (1 - up) + 0.82 * second;
      p.tuft += 4 * (1 - up) + 7 * second;
    }
    pet.rig.draw(p);
    const anchors = pet.rig.anchors;
    if (gearCanvas) {
      const layer = gearCanvas.getContext("2d");
      layer.resetTransform();
      layer.clearRect(0, 0, canvas.width, canvas.height);
      layer.setTransform(0.75, 0, 0, 0.75, 0, 0);
      pet.rig.arms.draw(layer, p, "body");
      window.drawDragonProps(layer, p, anchors, pet.rig.arms);
      // Drop the floor shadow. This layer travels with the wrists.
      layer.clearRect(0, 610, 816, 110);
    } else if (persistentPaper && !stow) {
      // The player will place the interrupted frame's sleeves, nib and paper here.
      ctx.putImageData(pet.rig.frame, 0, 0);
    }
    const xy = (pt) => [Math.round(0.75 * pt.x), Math.round(0.75 * pt.y)];
    const coords = ["forehead", "nose", "wingL", "wingR", "tail"].flatMap((k) =>
      xy(anchors[k]),
    );
    const targets = {
      running: { x: anchors.left.x + 101, y: anchors.left.y + 6 },
      searching: { x: anchors.right.x + 22, y: anchors.right.y - 47 },
      waiting: anchors.left,
    };
    return {
      png: canvas.toDataURL().split(",")[1],
      gear: gearCanvas?.toDataURL().split(",")[1],
      coords,
      palms: name.startsWith("star")
        ? [...xy(anchors.left), ...xy(anchors.right)]
        : null,
      target: targets[job] ? xy(targets[job]) : null,
      book:
        job === "reading"
          ? xy({
              x: (anchors.left.x + anchors.right.x) / 2 + 64,
              y: (anchors.left.y + anchors.right.y) / 2 - 12,
            })
          : null,
    };
  };
};

/* Called by the offline exporter at the original book's ink layer.
   The second chart is exposed as the right leaf lifts off the book. */
window.drawBadgeInstrument = (c, p, a) => {
  if (p.heartTime != null) {
    // Build the gift between the hands; a tiny heart first, then a full one.
    // It leaves from that same point and follows an arc beside the face.
    const t = p.heartTime;
    if (t < 0.22 || t > 0.9) return;
    const f = Math.min(1, Math.max(0, (t - 0.52) / 0.38));
    const cx = (a.left.x + a.right.x) / 2,
      cy = (a.left.y + a.right.y) / 2 - 17;
    const x = cx + (a.nose.x + 161 - cx) * f,
      y = cy + (a.nose.y - 35 - cy) * f - 54 * f * (1 - f);
    const small = t < 0.32 || f > 0.81;
    const heart = small
      ? [".o.o.", "owogo", "ogggg", ".ogo.", "..o.."]
      : [
          ".ooo.ooo.",
          "owggofffo",
          "ofggggggo",
          "ofggggggo",
          ".ofggggo.",
          "..offgo..",
          "...ofo...",
          "....o....",
        ];
    const colors = { o: "#78586b", w: "#fff0e6", g: "#ed9b97", f: "#d8788c" };
    const size = 4;
    for (let row = 0; row < heart.length; row++)
      for (let col = 0; col < heart[row].length; col++) {
        const color = colors[heart[row][col]];
        if (!color) continue;
        c.fillStyle = color;
        c.fillRect(
          x + (col - heart[row].length / 2) * size,
          y + (row - heart.length / 2) * size,
          size,
          size,
        );
      }
    return;
  }
  if (p.badgeJob !== "running" && p.badgeJob !== "compact") return;
  const alpha = p.stage.workMix;
  if (!alpha) return;
  c.save();
  c.globalAlpha = alpha;
  c.translate(a.left.x - 8, a.left.y - 8 + (1 - alpha) * 12);
  const ink = (x, y, w, h, color) => {
    c.fillStyle = color;
    c.fillRect(x, y, w, h);
  };
  const width = 142;
  if (p.badgeJob === "running") {
    // The left hand supports the casing. The right index strikes the lit pad.
    ink(0, -9, width, 51, "#423a49");
    ink(4, -13, width - 8, 55, "#423a49");
    ink(5, -8, width - 10, 45, "#77a5ab");
    ink(9, -5, width - 18, 37, "#41536b");
    ink(18, 2, 6, 6, "#c5e7d4");
    ink(24, 8, 6, 6, "#c5e7d4");
    ink(18, 14, 6, 6, "#c5e7d4");
    ink(36, 17, 16, 4, "#9bcbbd");
    for (let j = 0; j < 3; j++)
      ink(
        67 + j * 18,
        10,
        12,
        9,
        j === 2 && p.badgeContact > 0.5 ? "#ffe9a6" : "#77a5ab",
      );
  } else {
    // Bottom sheet stays against the palm while the loose top corner is tucked in.
    for (let j = 0; j < 3; j++) {
      const open = j * (1 - p.jobPhase);
      const x = 6 + Math.round(open * 7),
        y = 12 - j * 5 - Math.round(open * 7);
      ink(x, y, 120, 43, "#594b60");
      ink(x + 4, y + 4, 112, 34, j === 2 ? "#f3d6b9" : "#b6a8c3");
      if (j === 2) {
        ink(x + 12, y + 11, 53, 4, "#78586b");
        ink(x + 12, y + 21, 36, 4, "#a57579");
        ink(x + 88, y + 3, 16, 22, "#77a5ab");
        ink(x + 88, y + 19, 7, 8, "#77a5ab");
      }
    }
  }
  c.restore();
};
window.drawBadgeChart = (ctx, moon) => {
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(12, -7);
  ctx.quadraticCurveTo(45, -24, 71, -23);
  ctx.lineTo(69, 22);
  ctx.quadraticCurveTo(39, 19, 12, 32);
  ctx.closePath();
  ctx.fillStyle = moon ? "#41536b" : "#595a7f";
  ctx.fill();
  ctx.strokeStyle = "#ccab76";
  ctx.lineWidth = 2.4;
  ctx.stroke();
  if (moon) {
    ctx.fillStyle = "#ffe9a6";
    ctx.beginPath();
    ctx.arc(43, 2, 15, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#41536b";
    ctx.beginPath();
    ctx.arc(49, -3, 13, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#f3d6b9";
    ctx.fillRect(20, 14, 4, 4);
    ctx.fillRect(60, -12, 4, 4);
  } else {
    ctx.strokeStyle = "#e3cca4";
    ctx.lineWidth = 2;
    ctx.beginPath();
    const stars = [
      [20, 19],
      [33, -3],
      [49, 7],
      [62, -14],
    ];
    for (const [x, y] of stars) ctx.lineTo(x, y);
    ctx.stroke();
    ctx.fillStyle = "#ffe4aa";
    for (const [x, y] of stars) {
      ctx.fillRect(x - 4, y - 1, 9, 3);
      ctx.fillRect(x - 1, y - 4, 3, 9);
    }
  }
  ctx.restore();
};
