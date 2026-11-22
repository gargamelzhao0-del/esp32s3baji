"use strict";

/*
 * BadgeMotion - pure motion director for the pixel badge.
 * No DOM, no timers: callers pass a monotonic clock and tick it from the
 * existing requestAnimationFrame loop. This keeps the behaviour testable and
 * makes the whole layer removable with ?motion=legacy.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) root.BadgeMotion = api.BadgeMotion;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const KEY = "pixel-badge.motion.v1";
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const defaultNow = () =>
    typeof performance !== "undefined" ? performance.now() : Date.now();

  const STATES = new Set([
    "idle",
    "working",
    "reading",
    "running",
    "writing",
    "searching",
    "waiting",
    "succeeded",
    "ended",
    "error",
    "limited",
    "compact",
    "cancelled",
  ]);

  // Bond earned by one interaction. Values stay intentionally small: the
  // relationship should warm up over a session, not after two taps.
  const INTERACTIONS = {
    pat: { bond: 2, emote: "heart", zone: "head", affection: 0.08, energy: 0.02 },
    patLeft: { bond: 2, emote: "heart", zone: "head", side: -1, affection: 0.08, energy: 0.02 },
    brush: { bond: 3, affection: 0.1 },
    cheekLeft: { bond: 1, emote: "blush", zone: "cheek", side: -1, curiosity: 0.04 },
    cheekRight: { bond: 1, emote: "blush", zone: "cheek", side: 1, curiosity: 0.04 },
    tickle: { bond: 3, emote: "note", zone: "head", energy: 0.08, affection: 0.06 },
    nose: { bond: 1, emote: "question", zone: "nose", curiosity: 0.04 },
    sneeze: { bond: 1, energy: 0.03 },
    wing: { bond: 1, emote: "sparkle", zone: "wing", side: 1 },
    wingLeft: { bond: 1, emote: "sparkle", zone: "wing", side: -1 },
    tail: { bond: 1, emote: "sparkle", zone: "tail" },
    feed: { bond: 2, energy: 0.06, affection: 0.05 },
    bubblePop: { bond: 1, energy: 0.03 },
    play: { bond: 1, emote: "sparkle", zone: "head", curiosity: 0.06 },
    starCatchLeft: { bond: 3, emote: "heart", zone: "hand", side: -1, affection: 0.1, energy: 0.06 },
    starCatchRight: { bond: 3, emote: "heart", zone: "hand", side: 1, affection: 0.1, energy: 0.06 },
    // Their atlas or frame-timed contact effect already shows the exact result.
    heartHug: { bond: 4, affection: 0.14 },
    palmTap: { bond: 2, affection: 0.06 },
    keyChime: { bond: 1, energy: 0.03 },
    lensFind: { bond: 1, curiosity: 0.05 },
    page: { bond: 1, curiosity: 0.03 },
    forehead: { bond: 3, affection: 0.12 },
    default: { bond: 1, emote: null },
  };

  const STATE_MOOD = {
    idle: { energy: 0.02, curiosity: 0.02 },
    working: { energy: -0.04, curiosity: 0.05 },
    reading: { energy: -0.03, curiosity: 0.08 },
    running: { energy: -0.07 },
    writing: { energy: -0.05, curiosity: 0.03 },
    searching: { energy: -0.02, curiosity: 0.08 },
    waiting: { energy: -0.02, curiosity: 0.05 },
    succeeded: { energy: 0.16, affection: 0.08 },
    ended: { energy: -0.05 },
    error: { energy: -0.16, affection: -0.02 },
    limited: { energy: -0.2 },
    compact: { energy: -0.03 },
    cancelled: { energy: -0.04 },
  };

  const MARKED_STATES = new Set([
    "working", "waiting", "succeeded", "ended", "limited", "cancelled", "error",
  ]);

  const MICROS = [
    {
      id: "lookLeft",
      clip: "idleLookLeft",
      from: 0,
      to: 9,
      duration: 900,
      weight: 3,
      title: "看看你",
      note: "她的视线先溜过去，头才慢慢跟上。",
    },
    {
      id: "lookRight",
      clip: "idleLookRight",
      from: 0,
      to: 9,
      duration: 900,
      weight: 3,
      title: "看看你",
      note: "她先抬眼看你，刘海慢了半拍。",
    },
    {
      id: "groomLeft",
      clip: "idleGroomLeft",
      from: 0,
      to: 29,
      duration: 1667,
      weight: 1.6,
      title: "理理头发",
      note: "她抬爪拨了拨刘海，假装只是路过。",
    },
    {
      id: "groomRight",
      clip: "idleGroomRight",
      from: 0,
      to: 29,
      duration: 1667,
      weight: 1.6,
      title: "理理头发",
      note: "她把翘起来的那绺发梢压平，又坐好了。",
    },
    {
      id: "stretch",
      clip: "idleStretch",
      from: 0,
      to: 27,
      duration: 1750,
      weight: 1.2,
      title: "伸个懒腰",
      note: "她举起双爪，翅膀也跟着舒展开。",
      requires: (now, m) => now - m.stateSince > 9000,
    },
    {
      id: "hum",
      clip: "idleHum",
      from: 0,
      to: 13,
      duration: 1200,
      weight: 1,
      title: "哼歌",
      note: "她闭着眼轻轻晃，尾巴在打拍子。",
      emote: "note",
      life: 900,
      requires: (now, m) => m.bond >= 10,
    },
    {
      id: "yawn",
      clip: "idleYawn",
      from: 0,
      to: 23,
      duration: 1714,
      weight: 0.8,
      title: "打哈欠",
      note: "她打了个小哈欠，尾巴尖都跟着发懒。",
      emote: "ellipsis",
      requires: (now, m) => now - m.stateSince > 14000,
    },
    {
      id: "tail",
      clip: "tail",
      from: 0,
      to: 28,
      duration: 1800,
      weight: 2,
      title: "摇尾巴",
      note: "尾尖先躲了一下，又慢慢绕回身边。",
      emote: "sparkle",
      zone: "tail",
    },
    {
      id: "wing",
      clip: "wing",
      from: 0,
      to: 24,
      duration: 1600,
      weight: 2,
      title: "抖翅膀",
      note: "右侧的小龙翼抖了一下，视线随后追过去。",
      emote: "sparkle",
      zone: "wing",
      side: 1,
    },
    {
      id: "wingLeft",
      clip: "wingLeft",
      from: 0,
      to: 24,
      duration: 1600,
      weight: 2,
      title: "抖翅膀",
      note: "左侧的小龙翼先动了，另一边慢半拍。",
      emote: "sparkle",
      zone: "wing",
      side: -1,
    },
    {
      id: "nose",
      clip: "nose",
      from: 0,
      to: 21,
      duration: 1400,
      weight: 1,
      title: "皱鼻子",
      note: "她盯住鼻尖，轻轻皱了皱。",
      emote: "question",
      zone: "nose",
    },
    {
      id: "sneeze",
      clip: "sneeze",
      from: 0,
      to: 31,
      duration: 1800,
      weight: 0.3,
      title: "打喷嚏",
      note: "阿嚏！她缩起肩膀，甩了甩头发。",
    },
    {
      id: "heart",
      clip: "heartHug",
      from: 0,
      to: 38,
      duration: 2400,
      weight: 0.2,
      title: "送给你",
      note: "她捧起一颗小星心，悄悄递给你。",
      requires: (now, m) => m.bond >= 25,
    },
  ];

  function moodAfter(mood, delta) {
    return {
      energy: clamp(mood.energy + (delta.energy || 0), 0, 1),
      affection: clamp(mood.affection + (delta.affection || 0), 0, 1),
      curiosity: clamp(mood.curiosity + (delta.curiosity || 0), 0, 1),
    };
  }

  class BadgeMotion {
    constructor({
      reduced = false,
      random = Math.random,
      now = defaultNow,
      storage = null,
      hasClip = () => true,
      getClip = hasClip,
    } = {}) {
      this.reduced = Boolean(reduced);
      this.random = random;
      this.now = now;
      this.storage = storage;
      this.getClip = getClip;
      this.bond = 0;
      this.visits = 0;
      this.state = "idle";
      this.stateSince = now();
      this.lastInteraction = -Infinity;
      this.attentionAt = -Infinity;
      this.attentionSide = 1;
      this.micro = null;
      this.lastMicroId = "";
      this.recent = Object.create(null);
      this.microAt = this.stateSince + 4200 + random() * 2600;
      this.periodicAt = this.stateSince + 2600;
      this.emotes = [];
      this.session = 0;
      this.load();
    }

    load() {
      let saved = null;
      try {
        saved = this.storage?.getItem(KEY);
      } catch {}
      if (saved) {
        try {
          const value = JSON.parse(saved);
          this.bond = clamp(Number(value.bond) || 0, 0, 100);
          this.visits = Math.max(0, Number(value.visits) || 0);
        } catch {}
      }
      this.visits += 1;
      this.save();
    }

    save() {
      if (!this.storage) return;
      try {
        this.storage.setItem(
          KEY,
          JSON.stringify({
            bond: Math.round(this.bond),
            visits: this.visits,
            lastSeen: Date.now(),
          }),
        );
      } catch {}
    }

    setReduced(value) {
      this.reduced = Boolean(value);
      if (this.reduced) this.micro = null;
    }

    bondLevel() {
      if (this.bond >= 75) return 4;
      if (this.bond >= 50) return 3;
      if (this.bond >= 25) return 2;
      if (this.bond >= 10) return 1;
      return 0;
    }

    setState(id, { newSession = false, now = this.now() } = {}) {
      if (!STATES.has(id)) return null;
      const previous = this.state;
      const changed = id !== previous || newSession;
      if (!changed) return null;
      if (newSession) {
        this.session += 1;
        this.micro = null;
        this.recent = Object.create(null);
        this.lastMicroId = "";
      }
      this.state = id;
      this.stateSince = now;
      this.emotes.length = 0;
      this.periodicAt = now + 2600;
      this.micro = null;
      this.mood = this.mood || { energy: 0.6, affection: 0.5, curiosity: 0.6 };
      this.mood = moodAfter(this.mood, STATE_MOOD[id] || {});
      const wait =
        id === "idle"
          ? previous === "idle"
            ? 3800 + this.random() * 3400
            : 1600 + this.random() * 1400
          : 2600 + this.random() * 1800;
      this.microAt = now + wait;
      // Briefly light the new state emblem. Its silhouette is drawn from
      // the current Agent state for as long as that state remains active.
      const emote = MARKED_STATES.has(id)
        ? this.emote(id, { zone: "status", life: 800, now })
        : null;
      return { changed: true, emote };
    }

    interact(action, { side = 0, zone = "", now = this.now() } = {}) {
      const info = INTERACTIONS[action] || INTERACTIONS.default;
      const resolvedSide = side || info.side || 1;
      this.bond = clamp(this.bond + (info.bond || 0), 0, 100);
      this.mood = this.mood || { energy: 0.6, affection: 0.5, curiosity: 0.6 };
      this.mood = moodAfter(this.mood, info);
      this.lastInteraction = now;
      this.micro = null;
      this.microAt = now + 2600 + this.random() * 1800;
      this.emotes.length = 0;
      const emote = info.emote
        ? this.emote(info.emote, {
            zone: info.zone || zone || "head",
            clip: action === "play" ? "starWatch" : action,
            side: resolvedSide,
            life: info.life || 900,
            now,
          })
        : null;
      this.save();
      return {
        bond: this.bond,
        level: this.bondLevel(),
        mood: { ...this.mood },
        emote,
      };
    }

    pointerAttention(side = 1, now = this.now()) {
      this.attentionAt = now;
      this.attentionSide = side < 0 ? -1 : 1;
    }

    tick(dt, ctx = {}) {
      const now = ctx.now ?? this.now();
      this.emotes = this.emotes.filter((e) => now - e.born < e.life);
      const available = ctx.active === ctx.intent && ctx.intent === ctx.base &&
        !ctx.pointer && !ctx.forehead && !ctx.star && !ctx.sleeping && !ctx.transfer;
      if (now >= this.periodicAt && available && !this.reduced) {
        this.periodic(now, ctx);
        this.periodicAt = now + 2600;
      }
      if (this.reduced) return null;
      if (this.micro) return null;
      if (!available) return null;
      if (ctx.state !== "idle" || ctx.base !== "idle") return null;
      // A long cuddle may outlast its scheduling cooldown. Let her sit down
      // again before starting an overdue gesture on the very next frame.
      if (ctx.quiet < 2600) return null;
      if (now < this.microAt) return null;
      const remaining = (ctx.nextQuietAt ?? 7000) - (ctx.quiet || 0);
      const pick = this.pick(now, remaining);
      if (!pick) return null;
      this.micro = pick;
      this.lastMicroId = pick.id;
      this.recent[pick.id] = now;
      this.microAt = now + pick.duration + 2400 + this.random() * 1800;
      if (pick.emote)
        this.emote(pick.emote, {
          zone: pick.zone || "head",
          clip: pick.clip,
          side: pick.side || 1,
          life: pick.life || 900,
          now,
        });
      return pick;
    }

    microEnded() {
      this.micro = null;
    }

    pick(now, remaining) {
      const items = [];
      const consider = (candidate) => {
        const reel = this.getClip(candidate.clip);
        if (!reel) return;
        const to = reel.frames ? reel.frames - 1 : candidate.to;
        const duration = reel.fps ? 1000 * (to + 1) / reel.fps : candidate.duration;
        if (duration + 180 > remaining) return;
        if (candidate.requires && !candidate.requires(now, this)) return;
        let weight =
          typeof candidate.weight === "function"
            ? candidate.weight(now, this)
            : candidate.weight;
        if (candidate.id === this.lastMicroId) weight *= 0.3;
        if (now - (this.recent[candidate.id] ?? -Infinity) < 8000)
          weight *= 0.5;
        if (weight > 0) items.push({ candidate: { ...candidate, to, duration }, weight });
      };
      for (const candidate of MICROS) consider(candidate);
      if (now - this.attentionAt < 1600) {
        consider({
          id: "attention",
          clip:
            this.attentionSide < 0 ? "idleLookLeft" : "idleLookRight",
          from: 0,
          to: 9,
          duration: 900,
          weight: 4,
          title: "看你",
          note: "她注意到你在这边，先抬眼看过来。",
        });
      }
      if (!items.length) return null;
      const total = items.reduce((sum, item) => sum + item.weight, 0);
      let roll = this.random() * total;
      for (const item of items) {
        roll -= item.weight;
        if (roll <= 0) return { ...item.candidate };
      }
      return { ...items[items.length - 1].candidate };
    }

    periodic(now, ctx) {
      const state = ctx.state || this.state;
      const fresh = (kind, within = 2000) =>
        !this.emotes.some((e) => e.kind === kind && now - e.born < within);
      if (state === "working" && this.random() < 0.45 && fresh(state))
        this.emote(state, { zone: "status", life: 800, now });
      else if (state === "reading" && this.random() < 0.22 && fresh("sparkle"))
        this.emote("sparkle", { zone: "book", clip: ctx.active, life: 800, now });
      else if (state === "searching" && this.random() < 0.3 && fresh("sparkle"))
        this.emote("sparkle", { zone: "lens", life: 800, now });
      else if (state === "error" && this.random() < 0.18 && fresh("sweat"))
        this.emote("sweat", { zone: "head", life: 900, now });
    }

    emote(kind, { zone = "head", side = 1, life = 900, clip = this.state, now = this.now() } = {}) {
      if (!kind) return null;
      const event = { kind, zone, side, born: now, life, clip };
      this.emotes.push(event);
      if (this.emotes.length > 8) this.emotes.shift();
      return event;
    }

    activeEmotes(now = this.now()) {
      return this.emotes
        .filter((e) => now - e.born < e.life)
        .map((e) => ({ ...e, age: now - e.born }));
    }

    snapshot() {
      return {
        state: this.state,
        bond: this.bond,
        level: this.bondLevel(),
        mood: { ...(this.mood || {}) },
        micro: this.micro ? { ...this.micro } : null,
        emotes: this.activeEmotes().map((e) => ({ ...e })),
      };
    }
  }

  return { BadgeMotion };
});
