/* Offline poses for the project's original GptCompanion. Coordinates are in rig space. */
function badgeBookOrigin(left) {
  return {x:left.x+90, y:left.y+3};
}
function badgePageCurl(turn) {
  return 15 * Math.sin(Math.PI * turn);
}
function badgeLeafEdge(left, turn) {
  const origin = badgeBookOrigin(left);
  // A point one sixth down the free edge of the moving page.
  const x = 77 * (1 - 0.04/6) * Math.cos(Math.PI * turn);
  const y = -31 + 65/6 - badgePageCurl(turn);
  const c = Math.cos(-0.05), s = Math.sin(-0.05);
  return {x:origin.x+x*c-y*s, y:origin.y+x*s+y*c};
}
function badgeStackSlip(progress) {
  const u = Math.max(0,Math.min(1,(progress-0.16)/0.52));
  return 1-u*u*(3-2*u);
}
function badgeTicket(left,j,progress) {
  const open = j * badgeStackSlip(progress);
  return {x:left.x-2+Math.round(open*7), y:left.y+4-j*5-Math.round(open*7)};
}
function badgeKey(left, pressure = 0) {
  // The fingertip travels 20 units through air, then carries the cap down six.
  const stroke = 26 * pressure - 20;
  const travel = Math.max(0, stroke);
  return {x:left.x+101, y:left.y+6+travel, travel, gap:Math.max(0, -stroke)};
}
const badgeLensOffset = {x:9+28*1.15*Math.sin(0.48), y:-19-28*1.15*Math.cos(0.48)};

function badgeHeartSeat(anchors, scale) {
  return { x: (anchors.left.x + anchors.right.x) / 2,
    y: (anchors.left.y + anchors.right.y) / 2 - 11 * scale };
}

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
  const catFace = scene === "writingFace";
  const job =
    book || scene === "readingMoon" ? "reading" : scene.replace(/(Done|Face)$/, "");
  window.moonChart = name.endsWith("Moon") || scene === "readingMoon";
  const persistentPaper = job === "writing" || job === "compact";
  const sleepingTake = name === "doze" || /^(peek|wake)(Left|Right)$/.test(name);
  // Trace the actual portrait jaw, including its dark keyline. Binding these
  // points to the same mesh keeps the cheek in front of fabric as it lowers,
  // without bringing the neck, collar or a rectangular patch of hair along.
  const lowerFace = sleepingTake ? [
    [174,262],[368,247],[369,274],[362,292],[353,306],[353,313],
    [349,319],[343,324],[335,328],[325,332],[313,335],[300,338],
    [286,340],[270,341],[253,340],[237,339],[223,335],[211,332],
    [201,328],[194,323],[189,318],[185,310],[181,297],[175,276],
  ].map(([x,y],i)=>{
    const key="pillowJaw"+i;
    pet.rig.addLandmark(key,x,y);
    return key;
  }) : [];
  const gearCanvas =
    (persistentPaper && name === job) || sleepingTake ? canvas.cloneNode() : null;
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
  function coverYawn(p, lift) {
    // The mouth moves with the sleepy head. Keep the palm tucked just
    // under its lower lip so the viewer can still read the open mouth.
    p.leftGrip = ({ mouth }) => ({
      x: mouth.x - 13 * p.stage.scale,
      y: mouth.y + 28 * p.stage.scale,
      weight: lift,
    });
    p.badgePalm = { left: { shape: "cover", amount: lift,
      elbow: { x: -33, y: 18, mix: lift } } };
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
    p.leftY -= (11 + 16 * Math.max(0, -dx)) * weight;
    p.rightY -= (11 + 16 * Math.max(0, dx)) * weight;
    p.badgePalm = {
      left: { shape: "cup", amount: (0.47 + 0.53 * Math.max(0, -dx)) * weight },
      right: { shape: "cup", amount: (0.47 + 0.53 * Math.max(0, dx)) * weight },
    };
    p.lid += 0.12 * weight;
    p.tailTip -= 12 * dx * weight;
  }
  function offerHeart(p, u) {
    const gather = held(u, 0, 0.24, 0.56, 0.79),
      join = held(u, 0.055, 0.26, 0.59, 0.82),
      offer = held(u, 0.28, 0.46, 0.56, 0.72);
    const follow = held(u, 0.53, 0.69, 0.79, 1);
    p.leftX += 80 * gather;
    p.rightX -= 80 * join;
    p.leftY -= 33 * gather + 14 * offer;
    p.rightY -= 31 * join + 14 * offer;
    p.badgePalm = {
      left: { shape: "cup", amount: gather },
      right: { shape: "cup", amount: join },
    };
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
  }
  // Sample the release pose once. After it leaves the fingertips the heart
  // flies through this fixed arc, independently of both arms returning to rest.
  let heartFlight;
  if (name === "heartHug") {
    const launch = { ...baseline, blink: 0 };
    offerHeart(launch, 0.5);
    pet.rig.draw(launch);
    const a = pet.rig.anchors;
    heartFlight = { from: badgeHeartSeat(a, launch.stage.scale),
      to: { x: a.nose.x + 161, y: a.nose.y - 35 } };
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
    // The wrist only reaches the temple. Let the bent fingertips meet the
    // lock, and sweep in from outside instead of sliding straight up the eye.
    const approach = frame < 24 ? pulse(frame, 0, 4, 10)
      : frame < 55 && frame >= 37 ? pulse(frame - 37, 0, 3, 9) : 0;
    p[hand + "Y"] -= 153 * comb;
    p[hand + "X"] += side * (13 * comb + 26 * approach - 12 * sweep);
    p.badgePalm = { [hand]: {
      shape: "brush", amount: comb,
      elbow: { x: side * 49, y: -26, mix: comb },
    } };
    p.nod += 11 * comb - 5 * offer - 3 * nest;
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
  function cradle(p, beat, depth, phase) {
    // Both arms travel towards her right breast. The left forearm crosses
    // her chest while the right elbow stays outside the cheek's resting place.
    const gather = ease(beat / 8);
    const open = ease((beat-7)/6);
    const lift = ease((beat-6)/7);
    p.leftX += 88*gather + 23*open;
    // The far hand starts its outward arc a beat earlier, spreading the
    // travel over nine frames instead of flicking across the cushion.
    p.rightX += -48*gather + 67*ease((beat-5)/9);
    p.leftY -= 14*gather + 22*lift;
    p.rightY -= 20*gather + 35*ease((beat-5)/8);
    // After landing, the palms stay on the cushion while the shoulders
    // continue down with the cheek. The planted foot gives both palms a
    // body-space reference independent of the breathing head and neck.
    const planted = ease((beat-11)/3);
    if (planted > 0) {
      p.leftGrip = ({footL}) => ({x:footL.x+67*p.stage.scale,
        y:footL.y-219*p.stage.scale, weight:planted});
      p.rightGrip = ({footL}) => ({x:footL.x+176*p.stage.scale,
        y:footL.y-227*p.stage.scale, weight:planted});
    }
    p.badgePalm = {
      left: {shape:"drape", amount:gather, elbow:{x:5,y:36,mix:gather}},
      right:{shape:"drape", amount:gather, elbow:{x:100,y:-32,mix:open}},
    };
    p.cushion = [ease((beat-3)/5), open, depth];
    p.lid *= 1 - depth;
    p.gazeX += .45 * gather * (1-depth);
    p.gazeY += .8 * gather * (1-depth);
    p.lean += 10 * open;
    p.nod += (44 + 1.6 * Math.sin(phase)) * depth;
    p.head += 26 * depth;
    p.headX += 43 * depth;
    p.hair += 5 * depth;
    p.tuft -= 6 * depth;
    p.breath += 1.4 * Math.sin(phase) * depth;
    p.tailLift -= 32 * depth;
    p.tailBend -= 25 * depth;
    p.tailTip -= 16 * depth;
    p.leftWing -= 14 * depth;
    p.rightWing -= 13 * depth;
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
    // Seat the instrument in the hands before it appears. On the way back,
    // the fingers keep their grip until the instrument has gone.
    const gripWeight = ease(holding / 0.35);
    if (job === "reading") {
      p.book = 1;
      p.work = { id: "reading", phase: "offer", progress: 0 };
      p.leftX += 5;
      p.rightX -= 6;
      // Keep the bookmark and lower cover above the close crop's meter line.
      p.leftY -= 55;
      p.rightY -= 18;
      p.nod += 5 + 2 * sway;
      p.gazeX += 0.72 * (2 * beat(0.03, 0.61, 0.86) - 1);
      p.gazeY += 0.75;
      p.rightGrip = a => {
        const t = p.work.progress;
        // Keep the binding still. Once the page passes the spine it falls on its own,
        // and the free hand arcs back over the upper edge to support the cover.
        const edge = badgeLeafEdge(a.left, Math.min(t, 0.63));
        const home = badgeLeafEdge(a.left, 0);
        const back = ease((t-0.63)/0.37);
        return {x:edge.x+(home.x-edge.x)*back+11,
                y:edge.y+(home.y-edge.y)*back+7-8*Math.sin(Math.PI*back),weight:gripWeight};
      };
      p.badgePalm = {left:"support",right:"pagePinch"};
    } else if (job === "working") {
      p.leftX += 59 + 7 * lift;
      p.leftY -= 65 + 8 * lift;
      p.head -= 4 + 4 * lift;
      p.gazeX -= 0.28 + 0.48 * lift;
      p.gazeY -= 0.31 + 0.3 * lift;
      p.tuft += 9 * beat(0.26, 0.42, 0.67);
    } else if (job === "running" || job === "compact") {
      p.book = 1; // Bring the working fingers forward over the case or tickets.
      p.leftY -= job === "running" ? 25 : 36;
      p.leftX += 4;
      const tap = beat(0.04, 0.17, 0.29) + beat(0.36, 0.46, 0.59);
      p.nod += 4 + 2 * tap;
      p.gazeY += 0.65;
      p.gazeX += 0.34 * sway;
      p.badgeContact = tap;
      p.badgePalm = {left:"support",right:job==="running"?"pressKey":"tidyEdge"};
      p.rightGrip = a => {
        if(job==="running") {
          const key=badgeKey(a.left,p.badgeContact);
          return {x:key.x+13,y:key.y-14-key.gap,weight:gripWeight};
        }
        const ticket=badgeTicket(a.left,2,u);
        const approach=ease(u/.16);
        return {x:ticket.x+119+11+8*(1-approach),
                y:ticket.y+12+6-13*(1-approach), weight:gripWeight};
      };
    } else if (job === "writing") {
      p.pad = 1;
      p.leftY -= 63;
      p.writing = DragonWriting.beat(5.12 * u);
      p.penPark = 0;
      p.nod += 5;
      p.head += 3;
      p.gazeY += 0.68 - 0.57 * p.writing.peek;
      p.gazeX += p.writing.x / 80;
      p.smile += 0.46 * p.writing.peek;
      p.badgePalm = {left:"support",right:"quill"};
      p.rightGrip = a => {
        const page=DragonWriting.layout(a.left,p.writing,p.penPark);
        return {x:page.tipX+10,y:page.tipY-17,weight:gripWeight};
      };
    } else if (job === "searching") {
      p.lens = 1;
      p.rightY -= 31 + 17 * lift;
      p.rightX -= 7 + 11 * lift;
      p.head += 4 + 4 * sway;
      p.gazeX += 0.48 + 0.32 * sway;
      p.gazeY += 0.48 - 0.72 * lift;
      p.winkL += 0.68 * lift;
      p.badgePalm = {right:"lensGrip"};
    } else if (job === "waiting") {
      p.work = { id: "waiting" };
      p.palmOpen = {left:0.88*ease(holding),right:0};
      p.leftY -= 32 + 3 * lift;
      p.leftX -= 10 + 2 * sway;
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
      p.stage.workMix = ease((holding-0.35) / 0.65);
      if (gripWeight < 0.15) p.badgePalm = null;
    }
  }
  window.sampleBadgeTake = ({ i, n, fps, settle, paperSource }) => {
    const t = i / fps,
      u = i / (n - 1);
    let p = { ...baseline, blink: 0 };
    const stow = name.startsWith("stow-");
    // A paper transfer keeps the exact stroke, nib and ticket positions from
    // the drawing on screen. Ease only the body's settling motion as it closes.
    const workTime = paperSource ? Math.min(1, paperSource.frame / paperSource.settle)
      : completed || catFace
      ? (catFace ? 44/62 : 1)
      : settle != null
        ? Math.min(1, i / settle)
        : name === job || name === "readingMoon"
          ? u
          : 0;
    if (job !== "idle") workPose(p, workTime, stow ? 1 - ease(u) : 1);
    const sample = paperSource || { frame: i, last: n - 1, settle };
    if (sample.settle != null && sample.frame > sample.settle) {
      const rest = (sample.frame - sample.settle) / (sample.last - sample.settle),
        wave = Math.sin(rest * Math.PI * 2);
      const held = paperSource ? 1 - ease(u) : 1;
      p.breath += 1.8 * wave * held;
      p.tailTip += 4 * wave * held;
      p.blink = 0.97 * pulse(rest, 0.59, 0.63, 0.7) * held;
      p.gazeX += 0.26 * pulse(rest, 0.17, 0.31, 0.57) * held;
    }
    if (book) {
      p.work.progress = turn ? u : 0;
      if (turn) {
        const curl = Math.sin(Math.PI * u);
        // Meet the corner, track it across the fold, and settle at the first line.
        p.gazeX += 1.55 * held(u, 0, 0.16, 0.25, 0.87);
        p.gazeY -= 0.35 * curl;
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
      p.rightGrip = a => ({
        x:a.eyes.x+49-badgeLensOffset.x, y:a.eyes.y+3-badgeLensOffset.y,
        weight: held(u,0.02,0.30,0.49,0.86),
      });
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
      p.leftY += 4 * highFive - 4 * nod;
      p.leftX += 3 * highFive;
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
        const paw = held(u, 0.11, 0.48, 0.63, 0.99);
        const hand = side < 0 ? "left" : "right";
        const stroke = held(u, 0.45, 0.58, 0.63, 0.84);
        p[hand + "X"] += side * 11 * pulse(u, 0.11, 0.29, 0.47);
        p[hand + "Grip"] = ({ nose, forehead }) => ({
          // Same cheek axis as touch detection. The rounded finger pad
          // settles on the blush and slips down a little before releasing.
          x: nose.x + side * (nose.y - forehead.y) * 0.52
            + side * (2 + 3 * stroke) * p.stage.scale,
          y: nose.y - side * (nose.x - forehead.x) * 0.52
            + (24 + 7 * stroke) * p.stage.scale,
          weight: paw,
        });
        p.badgePalm = { [hand]: { shape: "brush", amount: 0.76 * paw,
          elbow: { x: side * 37, y: 3, mix: paw } } };
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
        // One palm catches the laugh under her chin. The other curls against
        // her dress a beat later instead of punching the air in unison.
        const cover = held(u, 0.08, 0.29, 0.51, 0.82);
        const tuck = held(u, 0.19, 0.39, 0.62, 0.86);
        p.leftY -= 77 * cover + 7 * first;
        p.leftX += 55 * cover;
        p.rightY -= 22 * tuck + 5 * second;
        p.rightX -= 20 * tuck;
        p.badgePalm = { left: { shape: "cup", amount: cover } };
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
      // The wrist yields to the landing before lifting the light for a look.
      const cushion = pulse(i, 0, 2, 5);
      p[hand + "X"] -= side * (34 * cradle - 3 * cushion);
      p[hand + "Y"] += 8 * cushion - 54 * cradle;
      const free = side < 0 ? "right" : "left";
      const curl = ease((i - 16) / 4);
      p.badgePalm[hand].amount = Math.max(p.badgePalm[hand].amount, cradle) * (1 - curl);
      p.badgePalm[free].amount *= 1 - cradle;
      p[free + "Y"] += 9 * cradle;
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
      offerHeart(p, u);
      p.heartTime = u;
      p.heartFlight = heartFlight;
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
      const eyes = held(u, 0, 0.18, 0.66, 1);
      const look = held(u, 0.11, 0.43, 0.65, 1);
      p.gazeX += side * 0.95 * eyes;
      p.gazeY += 0.12 * look;
      p.head += side * 3.2 * look;
      p.headX += side * 4.5 * look;
      p.tuft += side * 5 * look;
      p.tassel += side * 3 * look;
      p.lean += side * 1.8 * look;
      p.nod += 1.2 * look;
      p.tailTip += side * 6 * look;
      p.tailLift += 2 * look;
      p.smile += 0.18 * look;
      p.blush += 0.08 * look;
      p.blink = 0.9 * pulse(u, 0.54, 0.62, 0.72);
    } else if (name.startsWith("idleGroom")) {
      // A quick, self-conscious hair tidy: paw up, comb the side lock, settle.
      const lift = held(u, 0.05, 0.46, 0.68, 0.98);
      const arc = held(u, 0.015, 0.31, 0.72, 0.95);
      const comb = held(u, 0.40, 0.64, 0.71, 0.99);
      const glance = held(u, 0, 0.20, 0.52, 0.82);
      const hand = side < 0 ? "left" : "right";
      const other = side < 0 ? "right" : "left";
      // Approach the outside of the lock, brush down its edge, release, then lower.
      p[hand + "Y"] -= 151 * lift - 22 * comb;
      p[hand + "X"] += side * (44 * arc - 15 * lift + 9 * comb);
      p.badgePalm = { [hand]: { shape: "brush", amount: lift } };
      p.head += side * 3.7 * lift;
      p.headX += side * 5 * lift;
      p.gazeX += side * 0.63 * glance;
      p.gazeY -= 0.18 * glance;
      p.hair += side * 4 * comb;
      p.tuft += side * 6 * held(u, 0.44, 0.65, 0.7, 1);
      p.tassel -= side * 3 * comb;
      p.nod += 2 * lift;
      p.smile += 0.24 * comb;
      p.blush += 0.2 * lift;
      p[other + "Y"] -= 4 * comb;
      p.tailTip -= 4 * comb;
      p.blink = 0.9 * pulse(u, 0.62, 0.7, 0.8);
    } else if (name === "idleStretch") {
      // The wrists travel out from the shoulders, with one side leading the other.
      const up = held(u, 0.04, 0.46, 0.60, 0.92);
      const follow = held(u, 0.14, 0.56, 0.67, 0.99);
      p.leftY -= 85 * up;
      p.rightY -= 75 * follow;
      p.leftX -= 63 * held(u, 0, 0.35, 0.66, 0.95);
      p.rightX += 57 * held(u, 0.08, 0.46, 0.74, 1);
      p.badgePalm = { left: { shape: "spread", amount: up }, right: { shape: "spread", amount: follow } };
      p.leftWing += 15 * held(u, 0.13, 0.50, 0.66, 0.98);
      p.rightWing += 13 * held(u, 0.22, 0.60, 0.68, 1);
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
      p.tailTip += 7 * hum * Math.sin(u * Math.PI * 2 - 0.4);
      p.tailLift += 3 * hum;
      p.leftWing += 3 * hum;
      p.rightWing += 2.5 * hum;
    } else if (name === "idleYawn") {
      // A full yawn that returns to the open-eyed idle pose.
      const cover = held(u, 0.005, 0.37, 0.72, 1);
      const open = held(u, 0.18, 0.41, 0.57, 0.78);
      const wake = pulse(u, 0.62, 0.78, 1);
      p.leftX -= 9 * pulse(u, 0, 0.15, 0.31);
      coverYawn(p, cover);
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
      const depth = ease((i-7) / 8),
        phase = (Math.max(0, i - 15) * Math.PI * 2) / 20;
      cradle(p, i, depth, phase);
      p.blink += .38 * pulse(i, 0, 3, 7);
      p.nod += 5 * pulse(i, 1, 6, 10);
    } else if (name.startsWith("peek")) {
      const peek = ease(i / 8);
      cradle(p, 15, 1, 0);
      p.cushion[2] -= .68 * peek;
      p.lid = 0.82 * peek;
      p[farEye] = 1;
      p.gazeX += side * 0.65 * peek;
      p.nod -= 6 * peek;
      p.smile += 0.47 * peek;
      p.tuft += 4 * peek;
    } else if (name.startsWith("wake")) {
      const up = ease(i/6),
        first = pulse(i, 20, 26, 33),
        second = pulse(i, 21, 29, 35);
      cradle(p, Math.max(0, 15-(i-5)), 1-up, 0);
      p.cushion[2] *= .32;
      p.lid = 0.82 + 0.18 * up;
      p[farEye] = 1 - up;
      p.gazeX += side * 0.65 * (1 - up);
      p.nod -= 6 * (1 - up) + 5 * first;
      const near = side < 0 ? "left" : "right",
        far = side < 0 ? "right" : "left";
      p[near + "Wing"] += 23 * first;
      p[far + "Wing"] += 22 * second;
      p[near + "Y"] -= 66 * first;
      p[far + "Y"] -= 60 * second;
      p[near + "X"] += side * 53 * first;
      p[far + "X"] -= side * 49 * second;
      if (first > 0) p.badgePalm[near] = {shape:"spread", amount:first};
      if (second > 0) p.badgePalm[far] = {shape:"spread", amount:second};
      p.lid *= 1 - 0.56 * pulse(i, 21, 26, 32);
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
      if (sleepingTake) {
        // This is a coverage plate; the bake takes its colours from the
        // complete portrait. The near forearm lies across the top while the
        // far sleeve comes around the far edge and shows only its wrist cuff.
        pet.rig.arms.draw(layer, p, "body", "left");
        const arm=pet.rig.arms.hands.right, elbow=p.badgePalm.right.elbow;
        const ex=elbow ? arm.ex+(arm.sx+elbow.x*arm.scale-arm.ex)*elbow.mix : arm.ex;
        const ey=elbow ? arm.ey+(arm.sy+elbow.y*arm.scale-arm.ey)*elbow.mix : arm.ey;
        const dx=ex-arm.x, dy=ey-arm.y;
        // Follow the real far forearm to the pillow's outer crest. Drawing
        // one sleeve at a time prevents an isolated slice of the other elbow.
        layer.save();
        layer.translate(arm.x,arm.y);
        layer.rotate(Math.atan2(dy,dx));
        layer.beginPath();
        layer.roundRect(-27*arm.scale,-18*arm.scale,Math.hypot(dx,dy)+42*arm.scale,36*arm.scale,15*arm.scale);
        layer.clip();
        layer.setTransform(.75,0,0,.75,0,0);
        pet.rig.arms.draw(layer, p, "body", "right");
        layer.restore();
        layer.beginPath();
        lowerFace.forEach((key,i)=>{
          const at=anchors[key];
          if (i===0) layer.moveTo(at.x,at.y); else layer.lineTo(at.x,at.y);
        });
        layer.closePath(); layer.fill();
      } else {
        pet.rig.arms.draw(layer, p, "body");
        window.drawDragonProps(layer, p, anchors, pet.rig.arms);
      }
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
    const key = job === "running" ? badgeKey(anchors.left, p.badgeContact) : null;
    const targets = {
      running: key,
      searching: { x: anchors.right.x + badgeLensOffset.x, y: anchors.right.y + badgeLensOffset.y },
      waiting: anchors.left,
    };
    return {
      png: canvas.toDataURL().split(",")[1],
      gear: gearCanvas?.toDataURL().split(",")[1],
      coords,
      cushion: sleepingTake ? [...xy(anchors.left), ...xy(anchors.right),
        ...p.cushion.map(v=>Math.round(v*100))] : null,
      keyDown: key ? key.travel >= 1 : false,
      palms: name.startsWith("star")
        ? ["left", "right"].flatMap((hand) => {
            const a = anchors[hand], cup = p.badgePalm[hand].amount;
            const scale = p.stage.scale;
            // The floor of the cupped outline is at -3, the closed knuckles
            // near -16. The player adds the native light's own radius.
            return xy({ x: a.x + (hand === "left" ? 1 : -1) * 2 * scale * cup,
              y: a.y - (16 - 13 * cup) * scale });
          })
        : null,
      target: targets[job] ? xy(targets[job]) : null,
      book:
        job === "reading"
          ? xy(badgeLeafEdge(anchors.left, p.work?.progress || 0))
          : null,
    };
  };
};

// The idle cutout is a curled fist. For intentional contact, draw one
// continuous palm silhouette: its fingertips unfold without a second ghost
// palm under them. Coordinates are mirrored around the solved wrist.
const badgeHandContours = {
  fist:   [[-11,9],[-17,3],[-17,-7],[-14,-16],[-6,-19],[-1,-18],[4,-18],[10,-15],[13,-10],[17,-6],[16,2],[10,8],[3,12],[-6,13]],
  brush:  [[-10,9],[-14,0],[-13,-11],[-8,-24],[-4,-31],[1,-32],[2,-27],[-1,-16],[6,-24],[11,-24],[11,-16],[5,-5],[8,9],[-5,13]],
  cup:    [[-12,9],[-18,3],[-20,-4],[-20,-12],[-16,-13],[-12,-7],[-7,-4],[2,-2],[10,-6],[18,-12],[23,-10],[20,-2],[9,10],[-4,13]],
  // Back of a relaxed hand. The wrist leads into a broad knuckle line;
  // the joined fingers hang over the cloth, with a lower outer fingertip.
  drape:  [[-12,6],[-15,1],[-14,-5],[-9,-10],[-2,-11],[8,-9],[17,-5],[25,1],[27,9],[24,14],[19,8],[14,10],[6,6],[-4,8]],
  cover:  [[-11,10],[-14,3],[-12,-5],[-9,-11],[-4,-16],[2,-20],[7,-23],[12,-23],[14,-19],[11,-14],[17,-17],[18,-10],[7,5],[-3,13]],
  // Back of a loose hand. The thumb lies against the fingers, which
  // point down and inward along the skirt instead of curling into a fist.
  rest:   [[-9,6],[-13,-1],[-12,-9],[-8,-13],[-2,-14],[5,-12],[9,-7],[11,-1],[15,5],[16,10],[13,13],[9,12],[6,17],[0,14]],
  spread: [[-11,9],[-17,0],[-20,-10],[-17,-15],[-11,-12],[-7,-25],[-3,-28],[1,-21],[9,-27],[13,-22],[10,-12],[20,-15],[12,7],[-4,14]],
};
function paintBadgePalm(c, shape, t) {
  const target = badgeHandContours[shape];
  const points = target.map(([x,y], i) => {
    const [ox,oy] = badgeHandContours.fist[i];
    return [ox+(x-ox)*t, oy+(y-oy)*t];
  });
  c.beginPath();
  const last = points.at(-1);
  c.moveTo((last[0]+points[0][0])/2, (last[1]+points[0][1])/2);
  points.forEach(([x,y],i) => {
    const next = points[(i+1) % points.length];
    c.quadraticCurveTo(x,y,(x+next[0])/2,(y+next[1])/2);
  });
  c.closePath();
  c.fillStyle = "#ffeadf";
  c.fill(); c.strokeStyle = "#745d69"; c.lineWidth = 2.8; c.stroke();
  c.beginPath();
  if (shape === "rest") {
    c.moveTo(6, -2*t); c.quadraticCurveTo(6, 2*t, 10, 6*t);
  } else if (shape === "drape") {
    c.moveTo(7,-2*t); c.quadraticCurveTo(11,1*t,14,6*t);
  } else if (shape === "cup") {
    c.moveTo(-8, 1); c.quadraticCurveTo(-4, 6, 6, 3);
  } else {
    c.moveTo(8, -6*t); c.quadraticCurveTo(3,-4*t,1,2);
  }
  c.strokeStyle = "#cba6a6"; c.lineCap = "round"; c.lineWidth = 2.5; c.stroke();
}

// The curled atlas hand includes its silk rim. Unfold that rim and its ink
// onto the same row spans as the first drawn pose, so the wrist never changes
// silhouette or loses a whole patch of detail at the start of a gesture.
const badgeCurlEnd = 0.38;
const badgeCurlArt = new WeakMap();
function curlTemplate(paint) {
  const canvas = document.createElement("canvas");
  canvas.width = 64; canvas.height = 80;
  const c = canvas.getContext("2d", {willReadFrequently:true});
  c.translate(32,40);
  paint(c);
  const alpha = c.getImageData(0,0,64,80).data;
  const rows = [];
  for (let y=0; y<80; y++) {
    let left=64, right=-1;
    for (let x=0; x<64; x++) {
      if (alpha[4*(64*y+x)+3] < 12) continue;
      left=Math.min(left,x); right=x;
    }
    if (right>=left) rows.push({y, left, width:right-left+1});
  }
  return {canvas,rows};
}
function unfurlBadgePalm(atlas, shape, progress) {
  let art = badgeCurlArt.get(atlas);
  if (!art) {
    art = {closed:curlTemplate(c=>c.drawImage(atlas,0,103,48,49,-24,-22,48,49)), shapes:new Map()};
    badgeCurlArt.set(atlas,art);
  }
  if (!art.shapes.has(shape)) {
    const open=curlTemplate(c=>{
      if (shape === "offer") {
        c.rotate(.62*.66);
        c.drawImage(atlas,55,103,55,51,-27,-23,55,51);
      } else paintBadgePalm(c,shape,badgeCurlEnd);
    });
    art.shapes.set(shape,{open,poses:new Map()});
  }
  const {open,poses} = art.shapes.get(shape);
  const step = Math.round(progress*64);
  if (poses.has(step)) return poses.get(step);
  const t=step/64, closed=art.closed;
  const lerp=(a,b)=>a+(b-a)*t;
  const result=document.createElement("canvas");
  result.width=64; result.height=80;
  // Keep the intermediate on the same software raster path as the bake canvas.
  // An accelerated stamp can otherwise change the ink on subsequent neutral frames.
  const c=result.getContext("2d",{willReadFrequently:true});
  const top=lerp(closed.rows[0].y,open.rows[0].y);
  const height=lerp(closed.rows.length,open.rows.length);
  for (let y=Math.ceil(top-.5); y<top+height-.5; y++) {
    const v=(y+.5-top)/height;
    const from=closed.rows[Math.min(closed.rows.length-1,Math.floor(v*closed.rows.length))];
    const to=open.rows[Math.min(open.rows.length-1,Math.floor(v*open.rows.length))];
    const x=lerp(from.left,to.left), w=lerp(from.width,to.width);
    // Both textures share a single moving row outline; only its ink mixes.
    c.globalCompositeOperation="source-over"; c.globalAlpha=1-t;
    c.drawImage(closed.canvas,from.left,from.y,from.width,1,x,y,w,1);
    c.globalCompositeOperation="lighter"; c.globalAlpha=t;
    c.drawImage(open.canvas,to.left,to.y,to.width,1,x,y,w,1);
  }
  poses.set(step,result);
  return result;
}
window.drawBadgePalm = (c, arm, grip, pass, atlas, restTilt=0) => {
  if (window.drawBadgeWorkGrip?.(c,arm,grip,pass)) return true;
  const t=grip?.amount, shape=grip?.shape;
  const offer=shape==="offer";
  if (offer) {
    // The photographed high-five changes cutouts only within this aperture.
    if (!atlas || t<=.36 || t>=.66 || pass==="grip") return false;
  } else if (!badgeHandContours[shape] || !(t>0) || pass==="grip") return false;
  c.save();
  c.translate(arm.x,arm.y);
  c.scale(-arm.side*arm.scale,arm.scale);
  if (offer) {
    c.rotate(restTilt);
    c.drawImage(unfurlBadgePalm(atlas,shape,(t-.36)/.30),-32,-40);
  } else if (atlas && t<badgeCurlEnd) {
    const u=t/badgeCurlEnd;
    const progress=u*u*(3-2*u);
    c.rotate(restTilt*(1-progress));
    if (progress < 0.5/64) c.drawImage(atlas,0,103,48,49,-24,-22,48,49);
    else if (progress > 1-0.5/64) paintBadgePalm(c,shape,t);
    else c.drawImage(unfurlBadgePalm(atlas,shape,progress),-32,-40);
  } else {
    paintBadgePalm(c,shape,t);
  }
  c.restore();
  return true;
};

/* Called by the offline exporter at the original book's ink layer.
   The second chart is exposed as the right leaf lifts off the book. */
// Foreground fingers cover only the edge they grip. The supporting palm is
// behind the paper or case; a tapping finger and a pen grip stay above it.
window.drawBadgeWorkGrip = (c, arm, grip, pass) => {
  if (!["support","pagePinch","pressKey","tidyEdge","quill","lensGrip"].includes(grip)) return false;
  if (grip === "support" && pass !== "grip") return false;
  c.save();
  c.translate(arm.x,arm.y);
  c.scale(arm.scale,arm.scale);
  c.lineJoin="round"; c.lineCap="round";
  c.strokeStyle="#836b75"; c.lineWidth=2.5; c.fillStyle="#ffeadb";
  const draw = path => {
    const outline = new Path2D(path);
    c.fill(outline); c.stroke(outline);
  };
  if(grip==="support") {
    draw("M-5 -1 Q-2 -8 5 -8 L12 -8 Q17 -7 15 -2 Q13 2 5 4 Q-3 8 -5 4 Z");
  } else if(grip==="pressKey") {
    // Index lands at (-13,14), with the folded fingers clear of the lit key.
    draw("M9 -12 Q17 -12 17 -3 Q18 6 11 9 Q7 11 3 6 L0 2 L-8 11 Q-11 16 -15 14 Q-18 12 -15 7 L-7 -5 Q-5 -13 3 -14 Z");
    c.strokeStyle="#c69795"; c.lineWidth=2;
    c.stroke(new Path2D("M7 4 Q10 5 12 1"));
  } else if(grip==="pagePinch" || grip==="tidyEdge") {
    // The index reaches over the free edge; the thumb curls under it.
    draw("M13 -10 Q19 -8 17 1 Q16 11 8 13 Q0 14 -5 8 L-11 2 Q-13 -2 -8 -3 L1 0 L-11 -5 Q-15 -7 -11 -10 Q-8 -13 -2 -11 L5 -9 Q8 -13 13 -10Z");
    c.strokeStyle="#c69795"; c.lineWidth=1.8;
    c.stroke(new Path2D("M2 0 L5 2"));
  } else {
    // Keep the feather/handle visible on either side of a narrow curled grip.
    draw("M12 -12 Q20 -7 15 4 Q12 13 3 12 L-6 10 Q-12 8 -10 3 L-13 0 Q-15 -5 -9 -7 L-2 -7 Q-2 -13 4 -13 Z");
    c.strokeStyle="#ba8990"; c.lineWidth=2;
    c.stroke(new Path2D("M-9 -2 L-1 0 M-7 4 L0 6"));
  }
  c.restore();
  return true;
};
window.drawBadgeInstrument = (c, p, a) => {
  if (p.heartTime != null) {
    const t = p.heartTime;
    if (t < 0.22 || t > 0.93) return;
    const f = Math.max(0, (t - 0.5) / 0.43);
    const heart = t < 0.25 || f > 0.94 ? [".w.", "wgw", ".g."]
      : t < 0.29 || f > 0.85 ? [".o.o.", "owogo", "ogggg", ".ogo.", "..o.."]
      : t < 0.34 || f > 0.76 ? [".oo.oo.", "owgoffo", "ogggggo", ".ofggo.", "..ogo..", "...o..."]
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
    let x, y;
    if (f === 0) {
      const seat = badgeHeartSeat(a, p.stage.scale);
      // As the lobes grow, keep the bottom point resting on the two inner fingers.
      x = seat.x;
      y = seat.y - heart.length * size / 2;
    } else {
      const {from, to} = p.heartFlight;
      // Clear the cheek sideways before climbing beside the open eye.
      const progressX = f * (1.25 - 0.25 * f);
      const progressY = f * (0.75 + 0.25 * f);
      x = from.x + (to.x - from.x) * progressX;
      y = from.y - 16 + (to.y - (from.y - 16)) * progressY;
    }
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
  if (p.badgeJob === "running") BadgeWorkArt.terminal(c, a, p.badgeContact);
  else BadgeWorkArt.tickets(c, a.left, p.jobPhase);
  c.restore();
};
