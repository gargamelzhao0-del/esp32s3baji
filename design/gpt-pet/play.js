/* The hand target used for touch, gaze and drawing is the same deformed landmark. */
(() => {
    "use strict";
    const clamp = (n, low, high) => Math.max(low, Math.min(high, n));
    const mix = (a, b, amount) => a + (b - a) * amount;
    function ease(a, b, value) {
        const t = clamp((value - a) / (b - a), 0, 1);
        return t * t * (3 - 2 * t);
    }
    const handFor = side => side < 0 ? "left" : "right";

    class DragonPlay {
        constructor(pet) {
            this.pet = pet;
            this.round = null;
            this.timer = 0;
        }

        start(kind, side) {
            const pet = this.pet;
            if (!pet.rig || !pet.scene.active || !["five", "fly"].includes(kind)) return;
            if(pet.stage?.sulking){pet.note("她把手藏起来了，还在等那颗和好糖。");return;}
            pet.stage?.interrupt();
            pet.work?.clear();
            this.clear();
            clearTimeout(pet.stillTimer);
            pet.action = pet.nextCare = pet.heldStar = null;
            pet.quiet = 0;
            pet.attention = null;
            side = Math.sign(side) || (pet.random() < 0.5 ? -1 : 1);
            const eye = pet.rig.anchors.eyes;
            const x = eye.x + side * 188, y = eye.y - 96;
            this.round = {kind, side, phase:"offer", age:0, x, y, aimX:x, aimY:y,
                          guided:false, dwell:0, tilt:0, trail:[], trailClock:0};
            pet.note(kind === "five" ? "她摊开手等你。碰一碰亮圈里的手心。" :
                     "一只萤火虫来了。把它引到她的手心，稍停一下。");
            pet.onPlay(this.round);
            if (pet.scene.reduced) pet.advance(0, true);
        }

        clear() {
            clearTimeout(this.timer);
            if (!this.round) return;
            this.round = null;
            this.pet.onPlay(null);
        }

        end() {
            this.clear();
            this.pet.quiet = 0;
            this.pet.note(this.pet.baseNote());
            if (this.pet.scene.reduced && this.pet.scene.active) this.pet.advance(0, true);
        }

        palm() { return this.pet.rig.anchors[handFor(this.round.side)]; }

        aim(x, y) {
            const r = this.round;
            if (!r || r.kind !== "fly" || r.phase !== "offer") return;
            r.guided = true;
            r.aimX = clamp(x, 42, 774);
            r.aimY = clamp(y, 50, 663);
            // Keep the same hand near the centre to avoid switching at every pixel.
            const offset = x - this.pet.rig.anchors.eyes.x;
            if (Math.abs(offset) > 60) r.side = Math.sign(offset);
        }

        press(point) {
            const r = this.round;
            if (!r || r.phase !== "offer") return;
            if (r.kind === "fly") this.aim(point.worldX, point.worldY);
            const palm = this.palm();
            const hit = Math.hypot(point.worldX - palm.x, point.worldY - palm.y) <= (point.hitRadius??51);
            if (r.kind === "five") {
                if (hit) this.finish("met");
                else this.pet.note("她晃了晃举着的手，等你碰一碰。");
            } else if (this.pet.scene.reduced) {
                r.x = r.aimX; r.y = r.aimY;
                if (hit) this.finish("landed");
                else this.pet.advance(0, true);
            }
        }

        key(key) {
            const r = this.round;
            if (!r) return false;
            if (key === "Escape") { this.end(); return true; }
            if (key === "Enter" || key === " ") {
                const palm = this.palm();
                this.press({worldX:palm.x, worldY:palm.y});
                return true;
            }
            if (r.kind !== "fly" || r.phase !== "offer") return false;
            const offset = {ArrowLeft:[-42,0], ArrowRight:[42,0], ArrowUp:[0,-42], ArrowDown:[0,42]}[key];
            if (!offset) return false;
            this.press({worldX:r.aimX + offset[0], worldY:r.aimY + offset[1]});
            return true;
        }

        finish(phase) {
            const r = this.round;
            if (!r || r.phase !== "offer") return;
            r.phase = phase; r.age = 0;
            if (phase === "withdraw") {
                this.pet.note(r.kind === "five" ? "她看了看自己的手，笑着收了回去。" : "她抬起头，目送那一点光飞远了。");
            } else {
                this.pet.reward();
                const previous=this.pet.lastPlay;
                r.duet=phase==="met"&&previous?.kind==="five"&&previous.side!==r.side&&this.pet.time-previous.at<12;
                this.pet.lastPlay={kind:r.kind, side:r.side, at:this.pet.time, recalled:false};
                this.pet.note(phase === "met" ? "啪！她踮起脚，朝你眨了一只眼。" : "萤火虫停稳了。她捧着那点光，悄悄放轻了呼吸。");
            }
            this.pet.onPlay(r);
            if (this.pet.scene.reduced) {
                this.pet.advance(0, true);
                this.timer = setTimeout(() => this.settle(), 1500);
            }
        }

        settle(){
            const duet=this.round?.duet;
            this.end();
            if(duet)this.pet.stage?.perform("cheer");
        }

        tick(dt) {
            const r = this.round;
            if (!r || !dt || this.pet.scene.reduced) return;
            const pastAge=r.age;
            r.age += dt;
            if(r.phase==="landed" && pastAge<1.10 && r.age>=1.10) this.pet.note("萤火虫又飞起来了。她抬起眼睛，送它走了一小段。");
            if (r.phase === "offer" && r.age >= (r.kind === "five" ? 7 : 18)) this.finish("withdraw");
            const palm = this.palm();
            if (r.kind === "fly") {
                let tx, ty;
                if (r.phase === "offer") {
                    const drift = r.guided ? 2.4 : 21;
                    tx = r.aimX + drift * Math.sin(r.age * 2.8);
                    ty = r.aimY + drift * 0.68 * Math.sin(r.age * 4.3 + 0.4);
                } else {
                    const lift = r.phase === "withdraw" ? ease(0, 1.25, r.age) : ease(1.08, 2.1, r.age);
                    tx = (r.phase === "withdraw" ? r.aimX : palm.x) + r.side * lift * 108;
                    ty = (r.phase === "withdraw" ? r.aimY : palm.y - 12) - lift * 149;
                }
                const lastX = r.x, lastY = r.y;
                const amount = 1 - Math.exp(-dt * 9.5);
                r.x = mix(r.x, tx, amount); r.y = mix(r.y, ty, amount);
                const distance = Math.hypot(r.x - palm.x, r.y - (palm.y - 10));
                r.tilt = mix(r.tilt, clamp((r.x-lastX)/(dt*290),-0.76,0.76), 1-Math.exp(-dt*8));
                r.trailClock += dt;
                if (r.trailClock >= 0.048) {
                    r.trailClock %= 0.048;
                    r.trail.push({x:lastX, y:lastY});
                    if (r.trail.length > 6) r.trail.shift();
                }
                if (r.phase === "offer" && r.guided) {
                    const speed = Math.hypot(r.x - lastX, r.y - lastY) / dt;
                    r.dwell = distance < 51 && speed < 126 ? r.dwell + dt : 0;
                    if (r.dwell >= 0.42) this.finish("landed");
                }
            }
            if (r.phase !== "offer" && r.age >= (r.phase === "landed" ? 2.25 : 1.38)) this.settle();
        }

        pose(g) {
            const r = this.round;
            if (!r) return;
            const eye = this.pet.rig.anchors.eyes, palm = this.palm();
            const still = this.pet.scene.reduced;
            const t = still && r.phase !== "offer" ? 0.48 : r.age;
            const hand = handFor(r.side);
            const fade = r.phase === "offer" ? 1 : 1 - ease(r.phase === "landed" ? 1.58 : 0.84,
                                                              r.phase === "landed" ? 2.25 : 1.38, t);
            const aimX = r.kind === "fly" ? r.x : palm.x;
            const aimY = r.kind === "fly" ? r.y : palm.y - 35;
            g.gazeX = mix(g.gazeX, clamp((aimX - eye.x) / 144, -1, 1), fade);
            g.gazeY = mix(g.gazeY, clamp((aimY - eye.y) / 145, -1, 1), fade);
            g.head += g.gazeX * 4.2 * fade;
            g.nod += g.gazeY * 3.5 * fade;
            for (const k of ["book", "pad", "keys", "lens", "leftY", "rightY", "leftX", "rightX"]) g[k] *= 1 - fade;
            if (r.kind === "five") {
                const reach = still ? 1 : r.phase === "offer" ? ease(0, 0.34, t) : 1;
                const tap = r.phase === "met" ? ease(0,0.12,t) * (1-ease(0.15,0.62,t)) : 0;
                g[hand+"Y"] -= (44 + 14*tap) * reach * fade;
                g[hand+"X"] += r.side * (27 + 8*tap) * reach * fade;
                g.lean += r.side * 7 * fade;
                g.jump += 12 * tap;
                g.wing += 12 * tap;
                if (r.phase === "offer") {
                    g[hand+"X"] += r.side * 7 * Math.sin(t*6) * ease(1.5,2.1,t) * reach;
                } else if (r.phase === "met") {
                    const wink = ease(0.06,0.23,t) * (1-ease(0.60,0.93,t));
                    g[r.side < 0 ? "winkL" : "winkR"] = wink;
                    g.smile += wink; g.blush += wink * 0.88;
                    g.tailTip += 17 * Math.sin(t*8) * fade;
                }
            } else {
                const close = r.phase === "landed" ? 1 : 1 - ease(46,220,Math.hypot(r.x-palm.x,r.y-palm.y));
                g[hand+"Y"] -= (12 + 24*close)*fade;
                g[hand+"X"] += r.side * (7 + 9*close)*fade;
                g.light = (0.45 + 0.55*close)*fade;
                g.smile += close * 0.60 * fade;
                if (r.phase === "landed") {
                    const hold = 1-ease(1.0,1.9,t);
                    g.breath *= 1-0.78*hold; g.lid *= 1-0.25*hold;
                    g.blush += hold*0.52; g.head -= r.side * 4*hold;
                }
            }
        }
    }
    window.DragonPlay = DragonPlay;
})();
