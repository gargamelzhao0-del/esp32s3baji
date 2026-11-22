"use strict";

const $ = (id) => document.getElementById(id);
const states = {
  idle: ["待机", "IDLE", "她抬眼望向你，右侧的尾尖轻轻一晃。"],
  working: [
    "思考",
    "THINK",
    "她托起小爪，眼睛往斜上方转了转，头顶的呆毛慢半拍翘起来。",
  ],
  reading: ["阅读", "READ", "她把书摊开，视线沿着书页慢慢移动。"],
  running: ["执行", "RUN", "她托着口袋终端，用另一只爪在亮键上敲了两下。"],
  writing: ["写入", "WRITE", "她摊开手帐，在纸上一笔一笔地写。"],
  searching: ["搜索", "SCAN", "她举起放大镜，凑近找线索。"],
  waiting: ["等待确认", "WAIT", "她举起一只小爪，等你回应。"],
  succeeded: ["完成", "DONE", "她张开龙翼，为刚刚完成的事庆祝。"],
  ended: ["本轮结束", "REST", "她放下手里的活，慢慢舒了口气。"],
  error: ["异常", "ERROR", "她缩了缩翅膀，抬头看你。"],
  limited: ["受限", "LIMIT", "她垂下翅膀，安静歇一会儿。"],
  compact: ["整理上下文", "SORT", "她把摊开的纸收拢，对齐折角。"],
  cancelled: ["已取消", "STOP", "她收起手边的东西，等下一次开工。"],
};
const actions = {
  cheekLeft: ["脸红了", "BLUSH", "左脸一碰，她眯起左眼，悄悄看你的手。"],
  cheekRight: ["脸红了", "BLUSH", "右脸让开了一点，花结旁的流苏还在轻晃。"],
  tickle: ["怕痒啦", "GIGGLE", "她缩起肩膀笑出声，笑够了才慢慢松开爪子。"],
  brush: ["顺毛", "PET", "她抬起额头，等你顺着刘海摸一摸。"],
  browInviteLeft: ["想贴贴", "PET", "她理了理左边的刘海，歪着头，抬眼看你。"],
  browInviteRight: ["想贴贴", "PET", "她拨开右边的发梢，把额头送过来，悄悄看你。"],
  doze: ["打盹", "NAP", "她把云朵枕挪到胸前，两只手搭上去，侧着脸趴下睡着了。"],
  peekLeft: ["偷看", "PEEK", "她只睁开左眼。再轻点一下，会被你抓个正着。"],
  peekRight: ["偷看", "PEEK", "右眼悄悄睁开了，她还以为你没发现。"],
  wakeLeft: [
    "醒啦",
    "WAKE",
    "被发现啦。她抬起脸，把云朵枕收回袖边，再舒展开小翅膀。",
  ],
  wakeRight: ["醒啦", "WAKE", "她收起装睡的表情，两翼一前一后地伸了个懒腰。"],
  turnStar: ["翻书", "PAGE", "她按住书脊，目光跟着星图的折角。"],
  turnMoon: ["翻书", "PAGE", "她抬起小爪，等你翻过月牙这一页。"],
  glanceStar: [
    "偷瞄",
    "LOOK",
    "纸页弹回来了。她抬眼瞄了瞄你，又低头去看星图。",
  ],
  glanceMoon: ["偷瞄", "LOOK", "折角弹回了月牙旁边，她抬眼看了你一下。"],
  pat: ["蹭蹭", "PET", "她仰起脸，往你的手心蹭了蹭。"],
  patLeft: ["蹭蹭", "PET", "她顺着左边的手心，悄悄歪了歪头。"],
  feed: ["吃星糖", "CANDY", "她伸爪接住了星糖。"],
  bubblePop: ["戳泡泡", "POP", "啵！碎星落在鼻尖，她眯起眼睛笑了。"],
  nose: ["碰鼻尖", "BOOP", "她盯住鼻尖，轻轻皱了皱鼻子。"],
  sneeze: ["打喷嚏", "ACHOO", "阿嚏！她缩起肩膀，甩了甩头发。"],
  play: ["追星星", "STAR", "她望着抛起的星星，悄悄屈起膝盖。"],
  starWatch: [
    "盯住啦",
    "STAR",
    "一只星萤飞到她身旁。牵着它左右绕一绕，看她追着谁。",
  ],
  starCatchLeft: ["接到啦", "CATCH", "接到啦！看，在左手。"],
  starCatchRight: ["接到啦", "CATCH", "嘿，这次在右手。给你看一眼，就一眼。"],
  heartHug: ["送给你", "LOVE", "刚刚那样摸，我最喜欢了。这颗给你。"],
  wing: ["抖翅膀", "WING", "右边的小龙翼抖了一下。"],
  wingLeft: ["抖翅膀", "WING", "左边的小龙翼抖了一下。"],
  tail: ["摇尾巴", "TAIL", "尾尖躲了一下，又绕回你身边。"],
  keyChime: ["合拍", "BEAT", "你碰了亮键。她敲两下回你，最后偷偷眨了一只眼。"],
  lensFind: [
    "找到啦",
    "FIND",
    "她举起镜片对准眼睛。瞧，镜沿亮起了一颗金色的小星。",
  ],
  palmTap: [
    "击个掌",
    "FIVE",
    "她伸爪和你碰了一下，开心地点点头，继续陪你等确认。",
  ],
};
const workTouch = {
  running: [
    "keyChime",
    "敲亮键",
    "轻点口袋终端右端的亮键，看她用两拍回应。",
    "keys",
  ],
  searching: [
    "lensFind",
    "找星屑",
    "轻点她手中的镜片，一起找藏在镜沿的星屑。",
    "lens",
  ],
  waiting: [
    "palmTap",
    "击个掌",
    "碰一碰举起的小爪。Agent 的确认仍在电脑端处理。",
    "hand",
  ],
};
const settingsKey = "pixel-badge.display.v1";
function readSettings() {
  try {
    return JSON.parse(localStorage.getItem(settingsKey)) || {};
  } catch {
    return {};
  }
}
const saved = readSettings();
let view = saved.view === "close" ? "close" : "full",
  grid = saved.grid !== false;
const motion = matchMedia("(prefers-reduced-motion: reduce)");
const motionParam = new URLSearchParams(location.search).get("motion");
const useDirector = motionParam !== "legacy";
const motionStorage = (() => {
  try {
    return localStorage;
  } catch {
    return null;
  }
})();
const director = useDirector
  ? new BadgeMotion({
      reduced: motion.matches,
      storage: motionStorage,
      getClip: (id) => window.PIXEL_CLIPS?.[id],
    })
  : null;
let activeRange = null;
let microMeta = null;
let statusLock = false;
let lastPointerAttention = -Infinity;
let brushCounted = false;
let heartCounted = false;
let lastMoon = false;
const bondNames = ["初见", "好奇", "亲近", "黏人", "默契"];
function refreshBond() {
  const note = $("bond-note");
  if (!note) return;
  note.textContent = director ? bondNames[director.bondLevel()] : "—";
}
const previewUrl = new URL(location.href);
if (previewUrl.searchParams.get("observe") === "1") {
  document.documentElement.classList.add("observe");
  $("badge").setAttribute("aria-label", "白龙娘互动圆屏");
  $("badge").setAttribute("aria-describedby", "metrics-description");
  $("observation-link").textContent = "展开控制";
  previewUrl.searchParams.delete("observe");
} else previewUrl.searchParams.set("observe", "1");
$("observation-link").href = previewUrl.href;
const linked =
  location.protocol.startsWith("http") &&
  new URLSearchParams(location.search).get("agent") === "1";
const lcd = new PixelBadge($("badge"));
let telemetry = badgeTelemetry(linked ? { link: "connecting" } : null, !linked);
function refreshTelemetry(next) {
  if (
    next.mode !== telemetry.mode ||
    next.quota.text !== telemetry.quota.text ||
    next.context.text !== telemetry.context.text
  )
    dirty = true;
  telemetry = next;
  const describe = (m) =>
    m.percent === null ? "未提供" : m.text.replace("~", "约 ");
  const source = {
    SIM: "模拟",
    LIVE: "实时",
    PLAY: "回放",
    END: "本轮结束",
    OFF: "离线或无会话",
    SYNC: "等待连接",
  };
  $("metrics-description").textContent =
    source[next.mode] +
    " · 剩余额度 " +
    describe(next.quota) +
    " · 上下文已用 " +
    describe(next.context);
}
const sheets = new Map();
function loadSheet(id) {
  if (!sheets.has(id)) {
    const img = new Image();
    img.src = "assets/" + id + ".png";
    const promise = img
      .decode()
      .then(() => img)
      .catch((error) => {
        if (sheets.get(id) === promise) sheets.delete(id);
        throw error;
      });
    sheets.set(id, promise);
  }
  const sheet = sheets.get(id);
  // Keep only recent decoded atlases on mobile. The displayed sheet has its own reference.
  sheets.delete(id);
  sheets.set(id, sheet);
  if (sheets.size > 5) sheets.delete(sheets.keys().next().value);
  return sheet;
}
async function transferSheet(move) {
  if (move.sheet) return move.sheet;
  const base = await loadSheet(move.id);
  if (!move.paperFrame) return base;
  const job = move.id.slice(5);
  const group = move.paperFrame - move.paperFrame % 4;
  const [patches, work] = await Promise.all([
    loadSheet(move.id + "-from-" + group), loadSheet(job),
  ]);
  const sheet = document.createElement("canvas");
  sheet.width = base.width;
  sheet.height = base.height;
  const ctx = sheet.getContext("2d");
  const layer = (y) => ctx.drawImage(patches, 0, y, base.width, base.height,
    0, 0, base.width, base.height);
  ctx.drawImage(base, 0, 0);
  for (let i = group; i <= move.paperFrame; i++) {
    const y = (i - group) * 2 * base.height;
    ctx.globalCompositeOperation = "destination-out";
    layer(y + base.height);
    ctx.globalCompositeOperation = "source-over";
    layer(y);
  }
  const w = base.width / 8, h = base.height / 2;
  ctx.clearRect(0, 0, w, base.height);
  for (let v = 0; v < 2; v++)
    ctx.drawImage(work, move.paperFrame % 8 * w, Math.floor(move.paperFrame / 8) * h + v * work.height / 2,
      w, h, 0, v*h, w, h);
  return sheet;
}
let desired = "idle",
  overrideNote = "",
  active = "",
  intent = "",
  ticket = 0,
  sheet = null,
  take = null,
  frame = 0,
  elapsed = 0;
let raf = 0,
  previous = null,
  dirty = true,
  lastDraw = "",
  bubbleOffered = false,
  burstOrigin = null;
let lastNose = -Infinity,
  clockText = "",
  lastMinute = -1;
const stateControl = $("agent-state");
let heldScene = "",
  transfer = null,
  lowering = null,
  napAfterStretch = false,
  pausedHands = null,
  savedTime = 0;
for (const [id, [name]] of Object.entries(states))
  stateControl.add(new Option(name, id));
stateControl.disabled = linked;
function baseClip() {
  return desired === "reading" && gestures.moon ? "readingMoon" : desired;
}
const forehead = new BadgeForeheadInvite();
const browClip = (id) => id === "browInviteLeft" || id === "browInviteRight";
let nextBrowSide = -1;
let assistedHold = 0;
let heldBrowKey = "";
let browCaption = "";
const gestures = new BadgeGestures($("badge"), {
  forehead,
  acceptForehead,
  cancelForehead,
  hit: hitAt,
  intent: () => intent,
  base: () => desired,
  frame: () => frame,
  show: switchClip,
  care,
  note: speak,
  poke: (id, side) => {
    director?.interact(id, { side, now: performance.now() });
    refreshBond();
    dirty = true;
  },
  restore: () => switchClip(baseClip()),
  invalidate: () => {
    dirty = true;
    render();
  },
});
let wantsRematch = false;
let quietBeforeInvite = 0;
const starGame = new BadgeStarGame({
  catchStar: (side) => {
    wantsRematch = true;
    director?.interact(side < 0 ? "starCatchLeft" : "starCatchRight", {
      side,
      now: performance.now(),
    });
    refreshBond();
    switchClip(side < 0 ? "starCatchLeft" : "starCatchRight");
  },
  leave: () => {
    // Ignoring her invitation still counts as quiet time before a nap.
    if (quietBeforeInvite) gestures.quiet = quietBeforeInvite + starGame.clock;
    switchClip(baseClip());
  },
  note: speak,
});
const starClip = (id) =>
  id === "starWatch" || id === "starCatchLeft" || id === "starCatchRight";
function beginForehead(side, quiet = 0) {
  forehead.start(side, { quiet, reduced: motion.matches });
  nextBrowSide = -side;
  switchClip(forehead.clip);
}
function acceptForehead(side) {
  // Reduced motion uses the same touch target with still poses and a manual start.
  if (motion.matches && active === "idle" && intent === "idle" && desired === "idle")
    beginForehead(side);
  if (intent !== forehead.clip || !browClip(intent)) return false;
  const touched = forehead.touch(side);
  if (touched) {
    director?.interact("forehead", { side, now: performance.now() });
    refreshBond();
    dirty = true;
  }
  return touched;
}
function cancelForehead() {
  if (forehead.phase === "off") return;
  forehead.cancel();
  if (browClip(intent)) switchClip(baseClip());
}
function offerHand() {
  if (browClip(intent)) {
    if (!gestures.pointer && acceptForehead(forehead.side)) assistedHold = 1250;
  } else if (desired === "idle" && active === "idle" && intent === "idle") {
    beginForehead(nextBrowSide);
  } else care("pat");
}
function describeForehead() {
  const phase = forehead.phase;
  let title = "想贴贴", note = actions[forehead.clip][2];
  if (phase === "touch") {
    title = "碰到了";
    note = "她抬眼看着停在额边的手指。";
  } else if (phase === "nuzzle" || phase === "rest") {
    title = "贴贴";
    note = "你的手停在那里。她自己蹭过来，轻轻靠住。";
  } else if (phase.startsWith("look")) {
    title = "在这边呀";
    note = "手在另一边。她先转动眼睛，朝那边瞄了一眼。";
  } else if (phase === "joining" || phase === "brushReady") {
    title = "顺毛";
    note = "她顺着手指移动的方向，把刘海轻轻送过来。";
  } else if (phase === "tidy") {
    title = "理理头发";
    note = "她把翘起来的那绺刘海压平，又坐好了。";
  } else if (["away", "unwind", "unjoin", "retract"].includes(phase)) {
    title = "收好啦";
    note = "她慢慢抬起脸，把身子坐正。";
  }
  if (browCaption !== title) {
    browCaption = title;
    $("state-chip").textContent = title;
    speak(note);
  }
  const button = $("pat-label").parentElement;
  const busy = Boolean(forehead.touchSide) || (phase !== "offer" && phase !== "wait");
  if (button.disabled !== busy) button.disabled = busy;
}
function palmsAt(clip, index) {
  const [lx, ly, rx, ry] = clip.palms[index];
  return [[lx, ly], [rx, ry]].map(([x, y]) => {
    const seat = lcd.facePoint(view, clip, index, x, y);
    // The light is drawn on the display grid in either crop. Rest its
    // lower edge on the baked palm surface, four display pixels below its centre.
    return { sx: seat.sx, sy: seat.sy - 4 };
  });
}
function stopStar() {
  const id = starGame.finger,
    canvas = $("badge");
  starGame.stop();
  if (id != null && canvas.hasPointerCapture(id))
    canvas.releasePointerCapture(id);
}
function inviteStar(quiet = 0) {
  quietBeforeInvite = quiet;
  wantsRematch = false;
  if (quiet) forehead.skipOffer();
  starGame.start();
  if (!quiet) {
    const canvas = $("badge");
    canvas.focus({ preventScroll: true });
    const box = canvas.getBoundingClientRect();
    if (box.top < 0 || box.bottom > window.innerHeight)
      canvas.scrollIntoView({ block: "nearest", behavior: "instant" });
  }
  switchClip("starWatch");
}
function cancelStarDrag() {
  const id = starGame.finger;
  starGame.cancelDrag();
  if (id != null && $("badge").hasPointerCapture(id))
    $("badge").releasePointerCapture(id);
}
function refreshSceneControl() {
  let icon = "pillow",
    label = "打个盹",
    hint = "按住脸颊来回轻揉，或沿着刘海往返摸两趟。";
  if (intent === "starWatch") {
    icon = "recall";
    hint = "把发亮的星萤拖到一只手心再松开，也可以直接点手心。";
    label = "收起星萤";
  } else if (sleepingClip(intent)) {
    icon = "wake";
    label = intent === "doze" ? "轻轻叫她" : "再叫一声";
    hint = "按住胸前露出的蓝色枕面可以拍枕头，键盘也能按 P。轻碰脸颊，她会睁一只眼。";
  } else if (desired === "reading") {
    icon = "book";
    label = "翻一页";
    hint = "捏住书的右角向左划；只翻一点就松手，纸会弹回。";
  } else if (workTouch[desired]) {
    [, label, hint, icon] = workTouch[desired];
  } else if (desired !== "idle") {
    icon = "brush";
    label = "顺顺毛";
  }
  const hand = browClip(intent) ? "把手给她" : intent === "idle" ? "伸手等她" : "摸摸头";
  $("pat-label").textContent = hand;
  $("pat-label").parentElement.disabled = false;
  if (browClip(intent)) {
    const side = forehead.side < 0 ? "左" : "右";
    hint = "把指尖停在她送出的" + side + "侧额头，或沿着刘海轻轻移动。";
  }
  $("scene-label").textContent = label;
  $("gesture-hint").textContent = hint;
  $("star-label").textContent = intent === "starWatch" ? "送到手心" : "逗星萤";
  PixelBadge.setButtonIcon($("scene-button").querySelector("canvas"), icon);
  PixelBadge.setButtonIcon($("star-label").previousElementSibling, intent === "starWatch" ? "catch" : "firefly");
}
function speak(text) {
  if ($("reply").textContent !== text) $("reply").textContent = text;
}
function baseNote() {
  if (overrideNote) return overrideNote;
  if (desired === "reading" && gestures.moon)
    return "翻到月牙图了。她低下头，沿着月牙读下去。";
  return states[desired][2];
}
function refreshFeedControl() {
  const label = $("feed-label");
  const busy = (intent === "feed" && !bubbleOffered) || intent === "bubblePop";
  let text = "喂星糖";
  if (intent === "feed") {
    const seconds = active === "feed" ? frame / take.fps : 0;
    text = bubbleOffered ? "戳泡泡" : seconds > 7.04 ? "回味中" : seconds >= 1.82 ? "吹泡泡" : "吃糖中";
  } else if (intent === "bubblePop") text = "偷笑中";
  if (label.textContent === text && label.parentElement.hasAttribute("aria-disabled")) return;
  label.textContent = text;
  label.parentElement.setAttribute("aria-disabled", String(busy));
  PixelBadge.setButtonIcon(label.previousElementSibling, bubbleOffered ? "bubble" : "candy");
}
function setBubble(flag) {
  const changed = bubbleOffered !== flag;
  bubbleOffered = flag;
  refreshFeedControl();
  if (!changed) return;
  if (flag) {
    $("state-chip").textContent = "吹泡泡";
    speak("她吹出一颗糖泡泡。轻点泡泡，把里面的小星星放出来。");
  } else if (intent === "feed") {
    $("state-chip").textContent = "回味中";
    speak("糖泡泡慢慢散开了。");
  }
}
// The waking hands finish packing the pillow at frame 20, before stretching.
// If the stretch is underway, finish it rather than dropping raised hands.
function pillowExit(id, clip, image, index, wakeImage) {
  function span(id, clip, sheet, index, to, next = null) {
    return { id, clip, sheet, index, to, next, age: 0, folding: true };
  }
  if (id === "doze") {
    // Late breathing frames meet the start of the cycle at frame 15.
    if (index > 25)
      return span(id, clip, image, index, clip.frames - 1, span(id, clip, image, 15, 0));
    return span(id, clip, image, index, 0);
  }
  if (id.startsWith("peek")) {
    const wake = id === "peekLeft" ? "wakeLeft" : "wakeRight";
    return span(id, clip, image, index, clip.frames - 1, span(wake, PIXEL_CLIPS[wake], wakeImage, 0, 20));
  }
  return span(id, clip, image, index, index <= 20 ? 20 : clip.frames - 1);
}
async function switchClip(id, range = null, meta = null) {
  napAfterStretch = false;
  // A finger can arrive with a paw at the hair or mouth. Finish that short
  // reach from the drawing on screen before the next action takes the hand.
  if (!range && activeRange && active === intent && desired === "idle" && id !== "idle" &&
      !motion.matches && /^(idleGroomLeft|idleGroomRight|idleStretch|idleYawn)$/.test(active)) {
    const last = take.frames - 1;
    if (frame > 0 && frame < last)
      lowering = { id: active, clip: take, sheet, index: frame,
        to: frame < last / 2 ? 0 : last, age: 0 };
  }
  if (!range) {
    activeRange = null;
    microMeta = null;
    director?.microEnded();
  } else {
    activeRange = {
      from: range.from ?? 0,
      to: range.to ?? (PIXEL_CLIPS[id]?.frames ?? 1) - 1,
    };
    microMeta = meta;
  }
  if (!starClip(id) && starGame.phase !== "off") stopStar();
  if (sleepingClip(intent) && (id === "idle" || id.startsWith("wake")))
    forehead.rearm();
  if (!browClip(id)) {
    browCaption = "";
    assistedHold = 0;
    heldBrowKey = "";
    if (forehead.phase !== "off") forehead.cancel();
    if (gestures.pointer?.part === "offeredHead") {
      gestures.release();
      gestures.contact = null;
    }
  }
  const request = ++ticket;
  intent = id;
  setBubble(false);
  refreshSceneControl();
  try {
    const source = PIXEL_CLIPS[id];
    const base = baseClip();
    const contextual = source.scenes?.[base];
    const clip = contextual || source;
    const fold = !motion.matches && !lowering && !transfer && take?.cushion && !clip.cushion &&
      !(active === "doze" && frame === 0) &&
      !(active.startsWith("wake") && frame === take.frames - 1);
    const peekWake = fold && active.startsWith("peek") ? active.replace("peek", "wake") : null;
    const paper = clip.gear && PIXEL_CLIPS[clip.gear];
    const nextScene =
      (contextual ||
        id === base ||
        id === workTouch[desired]?.[0] ||
        bookClip(id) ||
        id.startsWith("glance")) &&
      PIXEL_CLIPS["stow-" + base]
        ? base
        : "";
    // A job queued while the pillow is being folded has no prop in hand yet.
    const currentMove = lowering?.folding ? null : transfer?.moves[0];
    const inHand = lowering?.folding ? "" : currentMove ? currentMove.id.slice(5) : heldScene;
    const sameProp =
      inHand === nextScene ||
      (inHand.startsWith("reading") && nextScene.startsWith("reading"));
    const moves = [];
    if (!motion.matches) {
      if (currentMove) {
        // Reversing a transfer starts at the hand position already on screen.
        const last = currentMove.clip.frames - 1;
        const shown = transferFrame(currentMove, transfer.age);
        moves.push({
          ...currentMove,
          reverse: sameProp,
          from: sameProp ? last - shown : shown,
        });
      } else if (!sameProp && inHand) {
        const paperFrame = inHand === "writing" || inHand === "compact"
          ? (active === inHand ? frame : pausedHands?.frame ?? 0) : 0;
        moves.push({ id: "stow-" + inHand, reverse: false, paperFrame });
      }
      if (!sameProp && nextScene)
        moves.push({ id: "stow-" + nextScene, reverse: true });
    }
    const [img, handSheet, wakeImage, ...reels] = await Promise.all([
      loadSheet(clip.atlas || id),
      paper ? loadSheet(paper.gearAtlas) : null,
      peekWake ? loadSheet(peekWake).catch(() => null) : null,
      ...moves.map(transferSheet),
    ]);
    if (request !== ticket) return;
    // A missing optional waking reel must not suppress an available Agent state.
    if (fold && (!peekWake || wakeImage))
      lowering = pillowExit(active, take, sheet, frame, wakeImage);
    if (active === base && id !== base) savedTime = elapsed;
    if (
      id === base &&
      desired === "reading" &&
      (bookClip(active) || active.startsWith("glance"))
    )
      savedTime = 0; // After settling a page, read the new chart from its first star.
    if (id === "idle" && actions[active]) savedTime = 0;
    // The scan is cyclic. After the find, her lens is back at the start
    // of the scan, even if the original tap caught it near her eye.
    if (id === "searching" && active === "lensFind") savedTime = 0;
    let entryFrame = 0;
    if (id === "lensFind" && active === "searching" && !transfer && !lowering && !motion.matches) {
      const [cx, cy] = take.target[frame];
      let closest = Infinity;
      // Use the approach drawings before the eye is centred on F8.
      for (let i = 0; i < 8; i++) {
        const [x, y] = clip.target[i];
        const distance = Math.hypot(x - cx, y - cy);
        if (distance < closest) {
          closest = distance;
          entryFrame = i;
        }
      }
    }
    // If the Agent asks for the same sheet before it was put away,
    // reopen that drawing and resume from its nib or leading ticket.
    if (currentMove?.paperFrame && sameProp && moves[0]?.reverse)
      savedTime = Math.ceil(currentMove.paperFrame * 1000 / PIXEL_CLIPS[inHand].fps);
    pausedHands = paper
      ? {
          sheet: handSheet,
          clip: paper,
          frame: jobFrame(base, paper, savedTime),
        }
      : null;
    active = id;
    sheet = img;
    take = clip;
    heldScene = nextScene;
    transfer = moves.length
      ? {
          moves: moves.map((m, i) => ({
            ...m,
            sheet: reels[i],
            clip: PIXEL_CLIPS[m.id],
          })),
          age: 0,
        }
      : null;
    elapsed = id === base ? savedTime : 1000 * entryFrame / clip.fps;
    frame = activeRange
      ? activeRange.from
      : browClip(id)
        ? forehead.frame
        : gestures.sample(id, elapsed, 0, motion.matches) ??
          (id === base ? jobFrame(id, clip, elapsed) : entryFrame);
    dirty = true;
    previous = null;
    $("badge").dataset.clip = id;
    const label =
      statusLock && id !== base
        ? states[desired][0]
        : microMeta?.title ||
          (actions[id] || states[id === "readingMoon" ? "reading" : id])?.[0] ||
          states[desired][0];
    $("state-chip").textContent = label;
    $("cheek-button").classList.toggle(
      "active",
      id.startsWith("cheek") || id === "tickle",
    );
    for (const button of document.querySelectorAll("[data-action]")) {
      const key =
        id === "sneeze"
          ? "nose"
          : id === "bubblePop"
            ? "feed"
            : id === "patLeft"
              ? "pat"
              : id;
      button.classList.toggle("active", button.dataset.action === key);
      if (button.dataset.action === "play" && starClip(id))
        button.classList.add("active");
    }
    if (browClip(id)) describeForehead();
    else if (id === "starWatch" && quietBeforeInvite)
      speak(
        (starGame.lastHand < 0 ? "上回是左手接到的。" : "上回是右手接到的。") +
          "星萤又从另一边飞回来，她悄悄把手摊开了。",
      );
    else if (!bookClip(id) || gestures.page?.phase === "drag")
      speak(microMeta?.note || actions[id]?.[2] || baseNote());
    render();
  } catch {
    if (request !== ticket) return;
    if (activeRange) {
      activeRange = null;
      microMeta = null;
      director?.microEnded();
    }
    intent = active;
    refreshFeedControl();
    if (starClip(id)) stopStar();
    if (browClip(id)) forehead.cancel();
    gestures.cancelContact();
    if (browClip(active) && forehead.phase === "off" && id !== baseClip())
      switchClip(baseClip());
    refreshSceneControl();
    speak("动作图集读取失败。请确认 assets 文件夹与本页放在一起。");
  }
}
function state(id, note = "", newSession = false) {
  if (!states[id]) return;
  const cue = director?.setState(id, { newSession, now: performance.now() });
  statusLock = ["waiting", "error", "limited"].includes(id);
  if (cue?.emote) dirty = true;
  refreshBond();
  const changed = desired !== id || newSession;
  desired = id;
  overrideNote = note;
  stateControl.value = id;
  if (changed) {
    const wasLowering = Boolean(lowering);
    if (!lowering?.folding) lowering = null;
    if (take?.cushion || lowering?.folding) {
      dirty = true;
      render();
    }
    lastNose = -Infinity;
    if (newSession) wantsRematch = false;
    if (browClip(intent)) forehead.cancel();
    forehead.rearm();
    savedTime = 0;
    gestures.moon = false;
    lastMoon = false;
    const interrupted =
      newSession ||
      wasLowering ||
      Boolean(activeRange) ||
      starClip(intent) ||
      browClip(intent) ||
      gestures.pointer ||
      gestures.brush ||
      gestures.page ||
      take?.paired ||
      transfer ||
      Object.values(workTouch).some(([action]) => intent === action) ||
      sleepingClip(intent) ||
      intent.startsWith("wake") ||
      intent.startsWith("glance") ||
      id === "error" ||
      id === "waiting";
    gestures.interrupt();
    refreshSceneControl();
    if (interrupted) {
      switchClip(baseClip());
      return;
    }
  }
  if (!actions[intent] && (changed || intent !== baseClip()))
    switchClip(baseClip());
  else if (!actions[intent]) speak(baseNote());
}
function care(id) {
  if (id === "feed" && (intent === "feed" || intent === "bubblePop")) {
    if (!bubbleOffered || intent !== "feed") return;
    id = "bubblePop";
  }
  if (id === "bubblePop") {
    if (!bubbleOffered || active !== "feed" || intent !== active) return;
    burstOrigin = take.bubbles[frame];
  }
  // A second tap can light the same contact while her reply finishes its beats.
  if (id === intent && id === workTouch[desired]?.[0]) return;
  if (sleepingClip(intent)) {
    gestures.rouse(id.endsWith("Left") ? -1 : 1);
    return;
  }
  gestures.interrupt();
  if (id === "play") {
    if (!lowering && intent === "starWatch" && starGame.available()) {
      starGame.land(starGame.hand);
      return;
    }
    if (starGame.phase === "caught") stopStar();
    if (starGame.phase !== "off") return;
    director?.interact("play", { now: performance.now() });
    refreshBond();
    inviteStar();
    return;
  }
  if (id === "nose") {
    const now = performance.now();
    if (now - lastNose < 900 && (active === "nose" || intent === "nose"))
      id = "sneeze";
    lastNose = now;
  }
  const side = id.endsWith("Left") ? -1 : 1;
  director?.interact(id, { side, now: performance.now() });
  refreshBond();
  switchClip(id);
}
function synchronizeSettings(write) {
  $("grid").checked = grid;
  $("view-label").textContent = view === "close" ? "近景取景" : "全身取景";
  $("grid-label").textContent = grid ? "网格开启" : "网格关闭";
  for (const button of document.querySelectorAll("[data-view]"))
    button.setAttribute("aria-pressed", String(button.dataset.view === view));
  if (write) {
    try {
      localStorage.setItem(settingsKey, JSON.stringify({ view, grid }));
    } catch {}
  }
  refreshBond();
  dirty = true;
  render();
}
for (const b of document.querySelectorAll("[data-action]"))
  b.addEventListener("click", () => b.dataset.action === "pat" ? offerHand() : care(b.dataset.action));
for (const b of document.querySelectorAll("[data-view]"))
  b.addEventListener("click", () => {
    if (view !== b.dataset.view) {
      gestures.cancelContact();
      cancelStarDrag();
      if (heldBrowKey || assistedHold) cancelForehead();
    }
    view = b.dataset.view;
    synchronizeSettings(true);
  });
$("grid").addEventListener("change", () => {
  grid = $("grid").checked;
  synchronizeSettings(true);
});
stateControl.addEventListener("change", () => state(stateControl.value));
$("cheek-button").addEventListener("click", () =>
  sleepingClip(intent)
    ? gestures.rouse(gestures.nextSide)
    : gestures.poke(gestures.nextSide),
);
$("scene-button").addEventListener("click", () => {
  if (intent.startsWith("wake") && desired === "idle" && !motion.matches) {
    napAfterStretch = true;
    speak("等这个懒腰伸完，她就再趴到云朵枕上。");
  } else if (intent === "starWatch") switchClip(baseClip());
  else if (workTouch[desired]) care(workTouch[desired][0]);
  else gestures.sceneButton();
});

function transferFrame(move, age) {
  const last = move.clip.frames - 1;
  const step = Math.min(
    last,
    (move.from || 0) + Math.floor((age * move.clip.fps) / 1000),
  );
  return move.reverse ? last - step : step;
}
function render() {
  const now = new Date(),
    minute = Math.floor(now.getTime() / 60000);
  if (lastMinute !== minute) {
    lastMinute = minute;
    clockText = [now.getHours(), now.getMinutes()]
      .map((v) => String(v).padStart(2, "0"))
      .join(":");
    $("clock").textContent = clockText;
  }
  const move = transfer?.moves[0];
  const clip = lowering?.clip || move?.clip || take,
    twinkle = motion.matches ? 0 : Math.floor(elapsed / 420) % 8;
  const sparks = motion.matches ? [] : gestures.brush?.sparks || [];
  const sparkleBeat = sparks.length ? Math.floor(elapsed / 80) : -1;
  const displayedFrame = lowering?.index ?? (move ? transferFrame(move, transfer.age) : frame);
  const contact = gestures.contact;
  const offeredFringe = !lowering && !move && active === intent && forehead.phase === "wait" &&
    browClip(active) ? forehead.side : 0;
  const offeredBubble = !lowering && !move && active === intent && active === "feed" && bubbleOffered;
  const emotes = director?.activeEmotes(performance.now()) || [];
  const toy =
    !lowering && !move && starGame.phase !== "off" && starClip(active) && clip?.palms
      ? starGame
      : null;
  if (toy) {
    const caught = active !== "starWatch";
    toy.place(palmsAt(clip, frame), caught ? frame : 0,
      caught && frame >= 20 ? palmsAt(clip, 20) : undefined);
  }
  const key = [
    lowering?.id || move?.id,
    active,
    desired,
    displayedFrame,
    view,
    grid,
    minute,
    twinkle,
    sparkleBeat,
    contact?.sx,
    contact?.sy,
    contact?.part,
    contact?.phase,
    offeredFringe,
    offeredBubble,
    contact ? Math.min(6, Math.floor(contact.age / 40)) : -1,
    emotes.map((e) => e.kind + e.zone + e.side + Math.floor(e.age / 80)).join(","),
    toy
      ? [
          toy.phase,
          Math.round(toy.pos.sx),
          Math.round(toy.pos.sy),
          toy.glow,
          motion.matches ? 0 : Math.floor(toy.clock / 80),
        ]
      : "",
  ].join("/");
  if (!dirty && key === lastDraw) return;
  lastDraw = key;
  dirty = false;
  lcd.draw({
    sheet: lowering?.sheet || move?.sheet || sheet,
    hands: lowering || move ? null : pausedHands,
    clip,
    frame: displayedFrame,
    view,
    grid,
    code: desired === "idle" ? actions[active]?.[1] || "IDLE" : states[desired][1],
    agentState: desired,
    action: lowering?.id || move?.id || active,
    sparks,
    contact,
    offeredFringe,
    offeredBubble,
    burstOrigin: active === "bubblePop" && !move && !lowering ? burstOrigin : null,
    emotes,
    starToy: toy,
    telemetry,
    tick: elapsed,
    motion: !motion.matches,
  });
}
function jobFrame(id, clip, time) {
  const last = clip.frames - 1,
    sample = Math.floor((time * clip.fps) / 1000);
  if (clip.holdFrom != null) {
    if (motion.matches) return clip.holdFrom;
    return sample < clip.holdFrom
      ? sample
      : clip.holdFrom + ((sample - clip.holdFrom) % (last - clip.holdFrom));
  }
  if (motion.matches) return 0;
  if (id === "idle") {
    const pos = sample % (last * 2);
    return pos <= last ? pos : last * 2 - pos;
  }
  return sample % last;
}
function updateForehead(dt) {
  // A failed replacement sheet leaves the last valid drawing on screen.
  if (forehead.phase === "off") return;
  const joining = forehead.phase === "joining" || forehead.phase === "unjoin";
  forehead.tick(joining ? Math.min(browJoinMs, dt) : dt);
  if (assistedHold > 0 && forehead.phase !== "offer") {
    assistedHold = Math.max(0, assistedHold - dt);
    if (!assistedHold) forehead.release();
  }
  if (forehead.phase === "brushReady") {
    frame = forehead.frame;
    gestures.joinBrush(forehead.brushFrame);
  } else if (forehead.phase === "off") {
    gestures.quiet = forehead.quietSpent;
    switchClip(baseClip());
  } else {
    frame = forehead.frame;
    describeForehead();
  }
}
function update(now) {
  if (document.hidden) return;
  const dt = previous === null ? 0 : Math.min(now - previous, 160);
  previous = now;
  const clip = take;
  const microBase = baseClip();
  const micro = director?.tick(dt, {
    state: desired,
    base: microBase,
    active,
    intent,
    quiet: gestures.quiet,
    nextQuietAt: wantsRematch ? 11500 : !forehead.offered && !motion.matches ? 7000 : 28000,
    pointer: Boolean(gestures.pointer),
    forehead: forehead.phase !== "off",
    star: starGame.phase !== "off",
    sleeping: sleepingClip(intent),
    transfer: Boolean(transfer || lowering),
    now: performance.now(),
  });
  if (
    micro &&
    active === intent &&
    active === microBase &&
    desired === "idle" &&
    !gestures.pointer &&
    forehead.phase === "off" &&
    starGame.phase === "off" &&
    !transfer &&
    !sleepingClip(intent)
  ) {
    switchClip(
      micro.clip,
      {
        from: micro.from ?? 0,
        to: micro.to ?? (PIXEL_CLIPS[micro.clip]?.frames ?? 1) - 1,
      },
      { title: micro.title, note: micro.note },
    );
  } else if (micro) {
    director?.microEnded();
  }
  if (lowering) {
    lowering.age += Math.min(dt, 22);
    if (lowering.index === lowering.to) {
      if (active === intent) lowering = lowering.next || null;
    } else if (lowering.age >= 22) {
      lowering.index += Math.sign(lowering.to - lowering.index);
      lowering.age -= 22;
    }
  } else if (transfer && active === intent) {
    transfer.age += dt;
    const m = transfer.moves[0];
    if (
      motion.matches ||
      transfer.age >= (1000 * (m.clip.frames - (m.from || 0))) / m.clip.fps
    ) {
      transfer.moves.shift();
      transfer.age = 0;
      if (!transfer.moves.length || motion.matches) transfer = null;
      dirty = true;
    }
  } else if (clip && active === intent) {
    elapsed += dt;
    const autonomous = Boolean(activeRange);
    gestures.idleTime(active, dt, autonomous);
    if (
      active === "idle" &&
      active === intent &&
      desired === "idle" &&
      !motion.matches &&
      !gestures.pointer
    ) {
      if (wantsRematch) {
        if (gestures.quiet >= 11500) inviteStar(gestures.quiet);
      } else if (forehead.canOffer(gestures.quiet)) {
        beginForehead(nextBrowSide, gestures.quiet);
      }
    }
    const sample = Math.floor((elapsed * clip.fps) / 1000);
    const driven = gestures.sample(active, elapsed, dt, motion.matches);
    if (activeRange && active === intent) {
      const span = activeRange.to - activeRange.from + 1;
      frame = Math.min(activeRange.to, activeRange.from + sample);
      if (sample >= span) {
        activeRange = null;
        microMeta = null;
        director?.microEnded();
        if (intent === active) switchClip(baseClip());
      }
    } else if (browClip(active) && active === intent) {
      updateForehead(dt);
    } else if (active === "starWatch" && starGame.phase !== "off") {
      const palms = palmsAt(clip, frame);
      starGame.update(dt, palms, motion.matches);
      // A fixed neutral landmark avoids opposite gaze samples tugging the target back and forth.
      const [fx, fy, nx, ny] = clip.anchors[13];
      const eyes = lcd.facePoint(
        view,
        clip,
        13,
        (fx + nx) / 2,
        fy + (ny - fy) * 0.72,
      );
      frame = starGame.watchFrame(eyes, palmsAt(clip, 13));
    } else if (driven !== null) {
      frame = driven;
    } else if (actions[active]) {
      const reducedFrame =
        active === "feed"
          ? Math.floor(3.5 * clip.fps)
          : active === "sneeze" ? 15 : active === "bubblePop" ? 14 :
            active.startsWith("wake") ? 27 : Math.floor(clip.frames * 0.38);
      frame = motion.matches ? reducedFrame : Math.min(sample, clip.frames - 1);
      if (active === "feed" && active === intent) {
        // Offer the shell once the follow camera has settled.
        setBubble(frame / clip.fps >= 3.24 && Boolean(clip.bubbles[frame]));
      }
      if (sample >= clip.frames && intent === active) {
        if (napAfterStretch) gestures.nap();
        else switchClip(baseClip());
      }
    } else {
      frame = jobFrame(active, clip, elapsed);
    }
    if (active === "brush" && gestures.brush?.stroked) {
      if (!brushCounted) {
        director?.interact("brush");
        refreshBond();
        brushCounted = true;
      }
    } else if (active !== "brush") brushCounted = false;
    if (active === "heartHug" && !autonomous) {
      if (!heartCounted) {
        director?.interact("heartHug");
        refreshBond();
        heartCounted = true;
      }
    } else heartCounted = false;
    if (gestures.moon !== lastMoon) {
      if (active.startsWith("turn") || active.startsWith("reading")) {
        director?.interact("page");
        refreshBond();
      }
      lastMoon = gestures.moon;
    }
  }
  render();
  raf = requestAnimationFrame(update);
}
function resume() {
  cancelAnimationFrame(raf);
  if (document.hidden) {
    gestures.cancelContact();
    cancelStarDrag();
    cancelForehead();
  }
  previous = null;
  dirty = true;
  if (!document.hidden) raf = requestAnimationFrame(update);
}
function hitAt(e, unbounded = false) {
  if (!sheet || transfer) return null;
  const clip = lowering?.clip || take;
  const drawn = lowering?.index ?? frame;
  const point = lcd.sourcePoint(e, view, clip, drawn, unbounded);
  if (!point) return null;
  const { x, y } = point,
    at = clip.anchors[drawn];
  const hit = { ...point, part: "", side: x < at[2] ? -1 : 1 };
  if (!lowering && active === intent && sleepingClip(active) && !gestures.sleepJoin) {
    const cushion = NapProp.position(lcd, clip, drawn, view);
    // At the piped edge, the cloth can sink away from a stationary finger.
    const held = gestures.pointer;
    if (cushion && ((held?.part === "pillow" && Math.hypot(x-held.x,y-held.y)<2) ||
        NapProp.contains(cushion, point.sx, point.sy, gestures.contact, !motion.matches)))
      return { ...hit, part: "pillow" };
  }
  if (!lowering && active === "feed" && bubbleOffered) {
    const [bx, by, radius] = clip.bubbles[frame];
    if (Math.hypot(x - bx, y - by) <= radius + 12)
      return { ...hit, part: "bubblePop" };
  }
  const oval = (cx, cy, rx, ry) =>
    ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1;
  const response = workTouch[desired]?.[0];
  if (!lowering && response && (active === desired || active === response) && clip.target) {
    const [px, py] = clip.target[drawn];
    // The brass rim reaches (31 + half its 9-unit stroke) × 0.75 source pixels.
    const onTool = desired === "searching" ? Math.hypot(x - px, y - py) <= 27 : oval(px, py, 24, 22);
    if (onTool) return { ...hit, part: response };
  }
  if (
    (active === "reading" || active === "readingMoon") &&
    desired === "reading"
  ) {
    const [bx, by] = clip.book[frame];
    if (oval(bx, by, 30, 28)) return { ...hit, part: "book" };
  }
  const fx = at[2] - at[0],
    fy = at[3] - at[1];
  const scale = Math.hypot(fx, fy) / 132;
  hit.hairOffset = Math.max(-1, Math.min(1, (x - at[0]) / (95 * scale)));
  const cheekX = at[2] + hit.side * fy * 0.52,
    cheekY = at[3] - hit.side * fx * 0.52;
  if (oval(at[2], at[3], 19, 17)) hit.part = "nose";
  else if (oval(cheekX, cheekY, 39 * scale, 32 * scale)) hit.part = "cheek";
  else if (oval(at[4], at[5], 40, 30)) hit.part = "wingLeft";
  else if (oval(at[6], at[7], 40, 30)) hit.part = "wing";
  else if (oval(at[8], at[9], 38, 49)) hit.part = "tail";
  else if (
    oval(at[0], at[1], (91 * scale) / 0.75, (75 * scale) / 0.75) ||
    oval(at[2], at[3], 63, 40)
  ) {
    hit.part = "head";
    // Keep the central fringe reachable when a fingertip covers the parting.
    hit.side = browClip(active) && Math.abs(x - at[0]) <= 22 * scale
      ? forehead.side : x < at[0] ? -1 : 1;
  }
  // The dress is a valid wake-up touch; the sky between the wings stays quiet.
  if (
    !hit.part &&
    sleepingClip(intent) &&
    oval((at[4] + at[6]) / 2, (at[5] + at[7]) / 2 + 11, 54, 61)
  )
    hit.part = "body";
  return hit;
}
function activateBadge() {
  if (browClip(intent)) offerHand();
  else if (active === "starWatch" && !lowering && !transfer && starGame.available())
    starGame.land(starGame.hand);
  else if (sleepingClip(intent)) gestures.rouse(-1);
  else if (bubbleOffered) care("bubblePop");
  else if (desired === "reading") gestures.sceneButton();
  else if (workTouch[desired]) care(workTouch[desired][0]);
  else care("pat");
}
$("badge").addEventListener("click", (e) => {
  if (e.detail === 0) activateBadge();
});
$("badge").addEventListener("keydown", (e) => {
  if (e.key.toLowerCase()==="p" && active===intent && sleepingClip(active) && !gestures.sleepJoin) {
    const p=NapProp.position(lcd,take,frame,view);
    if (p?.open>=0.9) {
      e.preventDefault();
      if (!e.repeat) {
        // Pat exposed fabric beside the moon, clear of the resting fingers.
        for (const u of [.83,.34,.56]) {
          const sx=Math.floor(p.x+p.w*u), sy=Math.floor(p.y+p.h*.62);
          if (NapProp.contains(p,sx,sy,null,false)) {
            gestures.pillowKey({sx,sy});
            break;
          }
        }
      }
      return;
    }
  }
  if (browClip(intent)) {
    if (e.key === "Escape") {
      e.preventDefault();
      cancelForehead();
      return;
    }
    if (e.key === " " || e.key === "Enter") {
      e.preventDefault();
      if (!e.repeat && !heldBrowKey && !gestures.pointer && acceptForehead(forehead.side))
        heldBrowKey = e.key;
      return;
    }
  }
  if (active === "starWatch" && !lowering && !transfer && starGame.phase !== "off") {
    if (e.key === "Escape") {
      e.preventDefault();
      switchClip(baseClip());
      return;
    }
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      e.preventDefault();
      if (!e.repeat)
        starGame.aim(e.key === "ArrowLeft" ? -1 : 1, palmsAt(take, frame));
      return;
    }
  }
  if (e.key === " " || e.key === "Enter") {
    e.preventDefault();
    if (!e.repeat) activateBadge();
  } else if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
    e.preventDefault();
    if (e.repeat || gestures.pointer) return;
    const side = e.key === "ArrowLeft" ? -1 : 1;
    if (sleepingClip(intent)) return gestures.rouse(side);
    gestures.interrupt();
    gestures.startBrush(side);
    gestures.brush.stroked = true;
    gestures.brush.target = side;
  }
});
$("badge").addEventListener("keyup", (e) => {
  if (e.key.toLowerCase()==="p") gestures.pillowKey(null);
  if (e.key === heldBrowKey) {
    heldBrowKey = "";
    forehead.release();
  }
  if (e.key === "ArrowLeft" || e.key === "ArrowRight") gestures.finishBrush();
});
$("badge").addEventListener("blur", () => {
  gestures.cancelContact();
  cancelStarDrag();
  if (heldBrowKey) cancelForehead();
});
for (const type of [
  "pointerdown",
  "pointermove",
  "pointerup",
  "pointercancel",
  "lostpointercapture",
]) {
  $("badge").addEventListener(
    type,
    (event) => {
      if (
        !event.isPrimary ||
        active !== "starWatch" ||
        active !== intent ||
        lowering ||
        transfer ||
        !take?.palms
      )
        return;
      const canvas = $("badge");
      const point = lcd.sourcePoint(event, view, take, frame, true);
      const palms = palmsAt(take, frame);
      if (type === "pointerdown") {
        if (
          event.button !== 0 ||
          !starGame.down(event.pointerId, point, palms, event.timeStamp)
        )
          return;
        quietBeforeInvite = 0;
        canvas.focus({ preventScroll: true });
        canvas.setPointerCapture(event.pointerId);
      } else {
        if (starGame.finger !== event.pointerId) return;
        if (type === "pointermove")
          starGame.move(point, palms, event.timeStamp);
        else if (type === "pointerup") {
          starGame.up(point, palms, event.timeStamp);
          if (canvas.hasPointerCapture(event.pointerId))
            canvas.releasePointerCapture(event.pointerId);
        } else cancelStarDrag();
      }
      event.stopImmediatePropagation();
      if (event.cancelable) event.preventDefault();
      dirty = true;
      render();
    },
    { capture: true },
  );
}
new ResizeObserver(() => {
  lcd.resize($("workmat").clientWidth);
  dirty = true;
  render();
}).observe($("workmat"));
$("badge").addEventListener(
  "pointermove",
  (event) => {
    if (!director || gestures.pointer || transfer) return;
    const now = performance.now();
    if (now - lastPointerAttention < 1200) return;
    lastPointerAttention = now;
    const box = $("badge").getBoundingClientRect();
    const x = (event.clientX - box.left) / box.width - 0.5;
    director.pointerAttention(x < 0 ? -1 : 1, now);
  },
  { passive: true },
);
document.addEventListener("visibilitychange", resume);
motion.addEventListener("change", () => {
  cancelForehead();
  director?.setReduced(motion.matches);
  const wasMicro = Boolean(microMeta);
  if (motion.matches) {
    lowering = null;
    activeRange = null;
    microMeta = null;
    director?.microEnded();
  }
  if (wasMicro) switchClip(baseClip());
  dirty = true;
  render();
});
lcd.resize($("workmat").clientWidth);
refreshTelemetry(telemetry);
synchronizeSettings(false);
switchClip("idle");
resume();

if (linked) {
  $("connection-label").textContent = "等待桥接";
  let session = "";
  let lastLink = "";
  async function poll() {
    if (!document.hidden) {
      try {
        const response = await fetch("/agent/state", {
          cache: "no-store",
          signal: AbortSignal.timeout(2500),
        });
        if (!response.ok) throw Error();
        const data = await response.json();
        if (data.version !== 1 || !Object.hasOwn(states, data.state))
          throw Error();
        refreshTelemetry(badgeTelemetry(data));
        const gpt = data.family === "gpt" && data.has_session === true,
          offline = data.link === "offline",
          connecting = data.link === "connecting";
        let label = "Agent 已连接",
          note = "";
        if (offline) {
          label = "Agent 离线";
          note = "电脑已离线，她在小屋里等你。";
        } else if (connecting) {
          label = "等待 Agent";
          note = "小屋就绪，她在等电脑发来第一个状态。";
        } else if (!data.has_session) {
          label = "等待会话";
          note = "桥接已就绪，她在等下一次开工。";
        } else if (!gpt) {
          label = "等待 GPT";
          note = "当前会话使用其他模型，白龙娘在小屋里等候。";
        } else if (data.link === "finished")
          label = data.replay ? "回放结束" : "本轮已结束";
        else if (data.replay) label = "Agent 回放";
        $("connection-label").textContent = label;
        $("connection-light").classList.toggle(
          "off",
          offline || connecting || !gpt,
        );
        const link = offline
          ? "offline"
          : connecting
            ? "connecting"
            : data.has_session
              ? "online"
              : "session";
        const currentSession =
          gpt && !offline && !connecting ? data.stream + "/" + data.scope : "";
        state(
          currentSession ? data.state : "idle",
          note,
          session !== currentSession,
        );
        if (link !== lastLink && link !== "online") {
          director?.emote("question", { zone: "head", life: 1400 });
          dirty = true;
        }
        lastLink = link;
        session = currentSession;
      } catch {
        refreshTelemetry(badgeTelemetry(null));
        $("connection-label").textContent = "桥接未连接";
        $("connection-light").classList.add("off");
        state(
          "idle",
          "还没收到电脑的状态，你仍然可以陪她玩。",
          Boolean(session),
        );
        if (lastLink !== "offline") {
          director?.emote("question", { zone: "head", life: 1400 });
          dirty = true;
          lastLink = "offline";
        }
        session = "";
      }
    }
    setTimeout(poll, document.hidden ? 2200 : 700);
  }
  poll();
}
