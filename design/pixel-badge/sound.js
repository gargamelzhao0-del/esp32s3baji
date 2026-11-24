"use strict";

/*
 * BadgeSound - cozy retro pixel chime synthesizer (Web Audio API, zero assets).
 * Soft music-box triangle/sine voices, safely no-ops if muted or unsupported.
 */
window.BadgeSound = (() => {
  const KEY = "pixel-badge.sound.v1";

  class Sound {
    constructor() {
      this.ctx = null;
      this.enabled = true;
      try {
        const saved = localStorage.getItem(KEY);
        if (saved === "0") this.enabled = false;
      } catch {}
    }

    setEnabled(flag) {
      this.enabled = Boolean(flag);
      try {
        localStorage.setItem(KEY, this.enabled ? "1" : "0");
      } catch {}
    }

    ensure() {
      if (!this.enabled) return null;
      if (!this.ctx) {
        const AudioCtx = window.AudioContext || window.webkitAudioContext;
        if (!AudioCtx) return null;
        try {
          this.ctx = new AudioCtx();
        } catch {
          return null;
        }
      }
      if (this.ctx.state === "suspended") {
        this.ctx.resume().catch(() => {});
      }
      return this.ctx;
    }

    tone(freq, { delay = 0, dur = 0.09, type = "triangle", gain = 0.06, slideTo = null } = {}) {
      const ctx = this.ensure();
      if (!ctx) return;
      const t0 = ctx.currentTime + delay;
      const osc = ctx.createOscillator();
      const env = ctx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(freq, t0);
      if (slideTo) {
        osc.frequency.exponentialRampToValueAtTime(Math.max(40, slideTo), t0 + dur);
      }
      env.gain.setValueAtTime(0.0001, t0);
      env.gain.linearRampToValueAtTime(gain, t0 + 0.012);
      env.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
      osc.connect(env);
      env.connect(ctx.destination);
      osc.start(t0);
      osc.stop(t0 + dur + 0.02);
    }

    play(kind) {
      if (!this.enabled) return;
      switch (kind) {
        case "pat":
        case "patLeft":
        case "forehead":
          this.tone(587.3, { dur: 0.08, gain: 0.05 });
          this.tone(784.0, { delay: 0.06, dur: 0.11, gain: 0.055 });
          break;
        case "brush":
          this.tone(659.3, { dur: 0.07, type: "sine", gain: 0.045, slideTo: 880 });
          break;
        case "cheekLeft":
        case "cheekRight":
          this.tone(523.3, { dur: 0.06, gain: 0.05, slideTo: 659.3 });
          break;
        case "tickle":
          this.tone(659.3, { dur: 0.055, gain: 0.05 });
          this.tone(784.0, { delay: 0.05, dur: 0.055, gain: 0.055 });
          this.tone(1046.5, { delay: 0.1, dur: 0.09, gain: 0.055 });
          break;
        case "nose":
          this.tone(740, { dur: 0.05, type: "sine", gain: 0.045, slideTo: 587 });
          break;
        case "sneeze":
          this.tone(440, { dur: 0.05, gain: 0.04, slideTo: 680 });
          this.tone(920, { delay: 0.06, dur: 0.08, type: "sine", gain: 0.055, slideTo: 320 });
          break;
        case "feed":
          this.tone(523.3, { dur: 0.06, gain: 0.05 });
          this.tone(659.3, { delay: 0.07, dur: 0.06, gain: 0.05 });
          this.tone(784.0, { delay: 0.14, dur: 0.09, gain: 0.055 });
          break;
        case "bubblePop":
          this.tone(480, { dur: 0.045, type: "sine", gain: 0.06, slideTo: 1240 });
          this.tone(1046.5, { delay: 0.055, dur: 0.12, type: "triangle", gain: 0.05 });
          break;
        case "heartHug":
        case "starCatchLeft":
        case "starCatchRight":
        case "reconcile":
          this.tone(523.3, { dur: 0.08, gain: 0.05 });
          this.tone(659.3, { delay: 0.065, dur: 0.08, gain: 0.05 });
          this.tone(784.0, { delay: 0.13, dur: 0.09, gain: 0.055 });
          this.tone(1046.5, { delay: 0.195, dur: 0.16, gain: 0.06 });
          break;
        case "wing":
        case "wingLeft":
        case "tail":
          this.tone(493.9, { dur: 0.06, type: "sine", gain: 0.045, slideTo: 622.3 });
          break;
        case "sulk":
          this.tone(466.2, { dur: 0.08, gain: 0.055, slideTo: 392.0 });
          this.tone(349.2, { delay: 0.09, dur: 0.12, gain: 0.05, slideTo: 311.1 });
          break;
        case "dsFeedRice":
          this.tone(523.3, { dur: 0.055, gain: 0.05 });
          this.tone(587.3, { delay: 0.06, dur: 0.055, gain: 0.05 });
          this.tone(784.0, { delay: 0.12, dur: 0.08, gain: 0.055 });
          this.tone(1046.5, { delay: 0.2, dur: 0.14, gain: 0.06 });
          break;
        case "dsSulkWeigh":
          this.tone(360, { dur: 0.07, type: "sine", gain: 0.055, slideTo: 740 });
          this.tone(440, { delay: 0.09, dur: 0.08, gain: 0.055, slideTo: 330 });
          this.tone(311.1, { delay: 0.18, dur: 0.12, gain: 0.05 });
          break;
        case "dsAhogeSplash":
          this.tone(392.0, { dur: 0.055, type: "sine", gain: 0.055, slideTo: 987.8 });
          this.tone(1174.7, { delay: 0.065, dur: 0.06, type: "sine", gain: 0.045, slideTo: 1568 });
          this.tone(987.8, { delay: 0.12, dur: 0.07, type: "triangle", gain: 0.04 });
          break;
        case "dsJellyfish":
          this.tone(659.3, { dur: 0.09, type: "sine", gain: 0.045 });
          this.tone(880.0, { delay: 0.07, dur: 0.1, type: "sine", gain: 0.048 });
          this.tone(1174.7, { delay: 0.14, dur: 0.14, type: "sine", gain: 0.045 });
          break;
        case "dsTailPoke":
          this.tone(587.3, { dur: 0.055, type: "sine", gain: 0.05, slideTo: 880.0 });
          break;
        case "dsNapPillow":
          this.tone(523.3, { dur: 0.11, type: "sine", gain: 0.04 });
          this.tone(440.0, { delay: 0.11, dur: 0.14, type: "sine", gain: 0.038 });
          break;
        case "clTeaBreak":
          // Porcelain teacup clink + cozy warm major chord
          this.tone(1318.5, { dur: 0.045, type: "sine", gain: 0.042 });
          this.tone(659.3, { delay: 0.06, dur: 0.09, type: "triangle", gain: 0.048 });
          this.tone(784.0, { delay: 0.13, dur: 0.11, type: "triangle", gain: 0.05 });
          this.tone(987.8, { delay: 0.21, dur: 0.15, type: "sine", gain: 0.052 });
          break;
        case "clClawdCrab":
          // Bouncy retro pixel crab pincer hop
          this.tone(523.3, { dur: 0.045, type: "square", gain: 0.025 });
          this.tone(659.3, { delay: 0.055, dur: 0.045, type: "square", gain: 0.026 });
          this.tone(784.0, { delay: 0.11, dur: 0.055, type: "triangle", gain: 0.05 });
          this.tone(1046.5, { delay: 0.17, dur: 0.1, type: "triangle", gain: 0.052 });
          break;
        case "clAbsRight":
          // Crisp golden starburst affirmation fanfare
          this.tone(587.3, { dur: 0.06, type: "triangle", gain: 0.05 });
          this.tone(880.0, { delay: 0.065, dur: 0.08, type: "triangle", gain: 0.055 });
          this.tone(1174.7, { delay: 0.135, dur: 0.15, type: "sine", gain: 0.058 });
          break;
        case "clRulebook":
          // Earnest strict gavel / flustered harpsichord double-step
          this.tone(493.9, { dur: 0.065, type: "triangle", gain: 0.055, slideTo: 440.0 });
          this.tone(440.0, { delay: 0.085, dur: 0.07, type: "triangle", gain: 0.055, slideTo: 392.0 });
          this.tone(349.2, { delay: 0.17, dur: 0.11, type: "sine", gain: 0.048 });
          break;
        case "clFlowerTouch":
          // Delicate fluttering daisy petal chimes
          this.tone(880.0, { dur: 0.055, type: "sine", gain: 0.042, slideTo: 1046.5 });
          this.tone(1174.7, { delay: 0.065, dur: 0.085, type: "sine", gain: 0.045, slideTo: 1318.5 });
          break;
        case "clBookNap":
          // Warm library lullaby cadence
          this.tone(493.9, { dur: 0.12, type: "sine", gain: 0.038 });
          this.tone(392.0, { delay: 0.12, dur: 0.16, type: "sine", gain: 0.036 });
          break;
        case "keyChime":
          this.tone(880, { dur: 0.035, type: "square", gain: 0.028 });
          this.tone(1174.7, { delay: 0.06, dur: 0.045, type: "square", gain: 0.028 });
          break;
        case "lensFind":
          this.tone(784.0, { dur: 0.07, gain: 0.045 });
          this.tone(1174.7, { delay: 0.07, dur: 0.11, gain: 0.055 });
          break;
        case "palmTap":
          this.tone(659.3, { dur: 0.06, gain: 0.05 });
          this.tone(987.8, { delay: 0.055, dur: 0.1, gain: 0.055 });
          break;
        case "page":
          this.tone(440, { dur: 0.05, type: "sine", gain: 0.035, slideTo: 587 });
          break;
        case "succeeded":
        case "reunion":
          this.tone(587.3, { dur: 0.08, gain: 0.05 });
          this.tone(740.0, { delay: 0.07, dur: 0.08, gain: 0.05 });
          this.tone(880.0, { delay: 0.14, dur: 0.09, gain: 0.055 });
          this.tone(1174.7, { delay: 0.21, dur: 0.16, gain: 0.06 });
          break;
        case "waiting":
          this.tone(659.3, { dur: 0.11, type: "sine", gain: 0.05 });
          this.tone(784.0, { delay: 0.11, dur: 0.15, type: "sine", gain: 0.05 });
          break;
        case "play":
          this.tone(784.0, { dur: 0.06, type: "sine", gain: 0.045 });
          this.tone(987.8, { delay: 0.055, dur: 0.065, type: "sine", gain: 0.048 });
          this.tone(1318.5, { delay: 0.11, dur: 0.11, type: "triangle", gain: 0.045 });
          break;
        case "browInviteLeft":
        case "browInviteRight":
          this.tone(523.3, { dur: 0.07, type: "sine", gain: 0.04, slideTo: 659.3 });
          break;
        case "doze":
          this.tone(523.3, { dur: 0.11, type: "sine", gain: 0.042 });
          this.tone(440.0, { delay: 0.1, dur: 0.14, type: "sine", gain: 0.038 });
          this.tone(392.0, { delay: 0.21, dur: 0.18, type: "sine", gain: 0.035 });
          break;
        case "peekLeft":
        case "peekRight":
          this.tone(587.3, { dur: 0.055, type: "sine", gain: 0.042, slideTo: 740.0 });
          break;
        case "wakeLeft":
        case "wakeRight":
          this.tone(523.3, { dur: 0.07, gain: 0.045 });
          this.tone(659.3, { delay: 0.07, dur: 0.08, gain: 0.045 });
          this.tone(784.0, { delay: 0.14, dur: 0.12, gain: 0.05 });
          break;
        case "pillow":
          this.tone(349.2, { dur: 0.06, type: "sine", gain: 0.04, slideTo: 440.0 });
          break;
        case "ended":
          this.tone(659.3, { dur: 0.09, type: "sine", gain: 0.045 });
          this.tone(523.3, { delay: 0.09, dur: 0.16, type: "sine", gain: 0.042 });
          break;
        case "limited":
          this.tone(392.0, { dur: 0.12, type: "sine", gain: 0.042, slideTo: 329.6 });
          break;
        case "error":
          this.tone(330.0, { dur: 0.1, gain: 0.055 });
          this.tone(277.2, { delay: 0.11, dur: 0.14, gain: 0.055 });
          break;
        default:
          break;
      }
    }
  }

  return Sound;
})();
