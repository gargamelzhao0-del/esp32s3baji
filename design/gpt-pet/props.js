/* Small hand-held props share the rig's animated palm/mouth positions. */
(() => {
    "use strict";
    const tau = Math.PI * 2;
    const clamp = (value, low = 0, high = 1) => Math.max(low, Math.min(high, value));
    function smooth(a, b, value) {
        const t = clamp((value - a) / (b - a));
        return t * t * (3 - 2 * t);
    }

    function shape(ctx, commands, fill, stroke, lineWidth = 2) {
        const path = new Path2D(commands);
        if (fill) { ctx.fillStyle = fill; ctx.fill(path); }
        if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = lineWidth; ctx.stroke(path); }
    }

    function book(ctx, t) {
        ctx.rotate(-0.05);
        shape(ctx, "M-88 -23 Q-39 -33 0 -14 Q44 -41 87 -29 L84 39 Q38 36 2 55 Q-44 39 -85 45 Z",
              "#42635c", "#334540", 4);
        shape(ctx, "M-82 -28 Q-35 -33 0 -13 L1 45 Q-38 31 -79 36 Z", "#f3e4c9", "#b7ac95");
        shape(ctx, "M0 -13 Q42 -38 81 -34 L78 33 Q40 27 1 45 Z", "#fff1d6", "#b7ac95");
        ctx.strokeStyle = "#b9a98c"; ctx.lineWidth = 2.2;
        for (let i = 0; i < 4; i++) {
            ctx.beginPath();
            ctx.moveTo(-68, -12 + i * 9); ctx.quadraticCurveTo(-45, -15 + i * 9, -16, -6 + i * 9);
            ctx.moveTo(17, -5 + i * 9); ctx.quadraticCurveTo(41, -18 + i * 9, 65, -19 + i * 9);
            ctx.stroke();
        }
        // A page curls up, crosses the spine, then settles on the other side.
        const phase = t % 3.4;
        if (phase < 0.85) {
            const progress = smooth(0, 0.85, phase);
            const tip = 77 * Math.cos(Math.PI * progress);
            const curl = 29 * Math.sin(Math.PI * progress);
            ctx.beginPath(); ctx.moveTo(1,-13);
            ctx.quadraticCurveTo(tip * 0.52, -28-curl, tip, -31-curl);
            ctx.lineTo(tip * 0.96, 34-curl);
            ctx.quadraticCurveTo(tip * 0.44, 30-curl, 1, 45); ctx.closePath();
            ctx.fillStyle = "#fff3d9"; ctx.fill(); ctx.strokeStyle = "#bfb295"; ctx.stroke();
        }
        shape(ctx, "M14 39 L21 36 L20 55 L16 51 L13 56 Z", "#cb9b97");
    }

    function notepad(ctx, t) {
        ctx.rotate(0.07);
        shape(ctx, "M-80 -9 L52 -25 L75 34 L-58 51 Z", "#9294aa", "#626477", 3);
        shape(ctx, "M-83 -15 L51 -30 L71 28 L-62 45 Z", "#fff0d6", "#c6b69b", 2);
        ctx.strokeStyle = "#aa9c98"; ctx.lineWidth = 2.5;
        for (let i = 0; i < 3; i++) {
            ctx.beginPath(); ctx.moveTo(-57, i*10);
            ctx.lineTo(-57 + (i===2 ? 25 + 20*(1 + Math.sin(t*2.6)) : 72), i*10-8); ctx.stroke();
        }
    }
    function quill(ctx) {
        shape(ctx, "M-1 1 Q-2 -44 26 -66 Q27 -51 22 -40 L16 -36 L19 -35 Q12 -17 1 -2 Z",
              "#e2d9f3", "#79718e", 2.5);
        shape(ctx, "M0 0 Q8 -28 24 -62 M1 -4 L-5 8", null, "#746981", 2.7);
    }
    function keys(ctx, t) {
        shape(ctx, "M-83 -16 Q-86 -16 -88 -11 L-99 22 Q-101 29 -92 31 L77 31 Q88 31 88 23 L79 -11 Q78 -17 71 -17 Z",
              "#596768", "#38474b", 3.5);
        for(let row = 0; row < 2; row++) {
            for(let key = 0; key < 9; key++) {
                const active = (Math.floor(t * 5.4) + row * 4) % 11 === key;
                ctx.fillStyle = active ? "#f7d69c" : (row ? "#bed6ce" : "#a5c5bc");
                ctx.fillRect(-76+key*17-row*4,-10+row*15+(active?2:0),13,8);
            }
        }
    }
    function magnifier(ctx) {
        ctx.rotate(0.48);
        shape(ctx, "M-3 -1 L-7 27 Q-7 33 0 33 Q6 33 6 28 L4 -1 Z", "#d0ac86", "#715c56", 3);
        ctx.beginPath(); ctx.arc(0,-28,27,0,tau);
        ctx.fillStyle="rgba(166,223,219,.38)"; ctx.fill();
        ctx.lineWidth=6; ctx.strokeStyle="#777389"; ctx.stroke();
        ctx.strokeStyle="#e0c8a6"; ctx.lineWidth=2.6; ctx.stroke();
        shape(ctx, "M-17 -32 Q-13 -43 -3 -45", null, "#f1f5e8", 3);
    }

    function fourPoint(ctx, x, y, size, fill) {
        ctx.save(); ctx.translate(x,y); ctx.scale(size,size);
        shape(ctx, "M0 -1 Q.10 -.15 .73 0 Q.1 .12 0 1 Q-.13 .13 -.76 0 Q-.12 -.13 0 -1 Z", fill);
        ctx.restore();
    }
    function star(ctx, x, y, size, angle, candy) {
        ctx.save(); ctx.translate(x,y); ctx.rotate(angle); ctx.scale(size,size);
        if (candy) {
            shape(ctx, "M-13 -4 L-25 -11 L-22 0 L-25 9 L-13 4 Z M13 -4 L25 -11 L22 0 L25 9 L13 4 Z",
                  "#dfadb9", "#a17c91", 1.5);
        }
        shape(ctx, "M0 -18 Q2 -19 5 -12 L7 -7 L16 -5 Q20 -4 16 1 L10 7 L11 15 Q11 19 7 17 L0 13 L-8 17 Q-12 19 -11 14 L-10 7 L-16 1 Q-20 -3 -15 -5 L-7 -7 L-3 -15 Q-2 -18 0 -18 Z",
              candy ? "#ffd5a2" : "#ffe6a0", "#b99a71", 2);
        fourPoint(ctx,-4,-5,4.7,"#fff9dd");
        ctx.restore();
    }

    // Also sampled by the eye controller, so a throw from either hand is tracked in space.
    function starPath(time,cast,palm) {
        const f=clamp((time-0.18)/1.2);
        const x0=cast?.x??165, y0=cast?.y??387;
        const side=cast?.side??1, height=cast?.height??1;
        const x1=palm.x+side*3, y1=palm.y-14;
        const midX=(x0+x1)/2+side*83;
        const midY=Math.max(-152,Math.min(y0,y1)-270-height*270);
        return {f,x:x0*(1-f)*(1-f)+2*midX*(1-f)*f+x1*f*f,
                  y:y0*(1-f)*(1-f)+2*midY*(1-f)*f+y1*f*f};
    }
    window.dragonStarPath=starPath;
    function treats(ctx,p,a) {
        const t=p.actionTime;
        if(p.action==="feed" && t<1.35) {
            const pickup=smooth(0.08,0.68,t), bite=smooth(0.68,1.26,t);
            let x=643+(a.right.x-643)*pickup;
            let y=355+(a.right.y-355)*pickup-35*Math.sin(pickup*Math.PI);
            x+=(a.mouth.x-x)*bite;
            y+=(a.mouth.y-y)*bite-18*Math.sin(bite*Math.PI);
            const size=smooth(0,0.15,t)*(1-smooth(1.12,1.35,t));
            star(ctx,x,y,size*1.18,0.2-0.58*pickup,true);
        }
        if(p.action==="play" && t<1.82) {
            const palm=p.cast?.side<0 ? a.left : a.right;
            const {x,y,f}=starPath(t,p.cast,palm);
            const size=(p.cast?1:smooth(0,0.16,t))*(1-smooth(1.48,1.80,t));
            star(ctx,x,y,1.22*size,f*4,false);
            if(f>0.08 && f<0.95) {
                fourPoint(ctx,x-18,y+6,5*size,"#cadbb9");
                fourPoint(ctx,x-29,y+14,3*size,"#ebe2ae");
            }
        }
        if(p.heldStar) star(ctx,p.heldStar.x,p.heldStar.y,1.22,0,false);
        if(p.action==="feed" && t>1.14 && t<1.80) {
            const f=(t-1.14)/0.66;
            ctx.globalAlpha=1-f;
            fourPoint(ctx,a.mouth.x+17+f*21,a.mouth.y-8+f*f*27,5,"#f8d998");
            fourPoint(ctx,a.mouth.x-12-f*14,a.mouth.y+f*f*24,3.6,"#ecc3b2");
            ctx.globalAlpha=1;
        }
        if(p.action==="pat" && t>0.3 && t<1.6) {
            const f=(t-0.3)/1.3;
            ctx.save(); ctx.translate(a.forehead.x+122+12*f,a.forehead.y-48-49*f);
            ctx.scale(0.7+0.3*f,0.7+0.3*f); ctx.globalAlpha=Math.sin(Math.PI*f)*0.94;
            shape(ctx, "M0 9 C-18 -1 -12 -13 -4 -8 L0 -4 Q9 -15 14 -7 Q21 0 0 9 Z", "#edb3be");
            ctx.restore();
        }
    }

    function prop(ctx, opacity, x, y, paint, t) {
        if(opacity<0.012) return;
        ctx.save(); ctx.globalAlpha=clamp(opacity);
        ctx.translate(x,y+(1-opacity)*9); ctx.scale(.9+.1*opacity,.9+.1*opacity);
        paint(ctx,t); ctx.restore();
    }

    function glow(ctx,x,y,rx,ry,inner,outer) {
        ctx.save(); ctx.translate(x,y); ctx.scale(rx,ry);
        const grad=ctx.createRadialGradient(0,0,0,0,0,1);
        grad.addColorStop(0,inner); grad.addColorStop(1,outer);
        ctx.fillStyle=grad; ctx.fillRect(-1,-1,2,2); ctx.restore();
    }

    function palmCue(ctx,palm,r,weight) {
        ctx.save(); ctx.translate(palm.x,palm.y); ctx.globalAlpha=weight;
        const radius=32+2*Math.sin(r.age*3.3);
        for(const [line,width] of [["rgba(36,56,51,.83)",6],["#c3edd9",3.3]]) {
            ctx.lineWidth=width; ctx.strokeStyle=line;
            for(const start of [0.3,Math.PI+0.3]) {
                ctx.beginPath(); ctx.arc(0,0,radius,start,start+1.74); ctx.stroke();
            }
        }
        fourPoint(ctx,r.side*(radius-1),-14,6.8,"#f8dda0");
        ctx.restore();
    }

    function playEffects(ctx,p,a) {
        const r=p.round;
        if(!r) return;
        const palm=r.side<0?a.left:a.right;
        const t=p.still && r.phase!=="offer" ? 0.40 : r.age;
        if(r.kind==="five") {
            if(r.phase==="offer") palmCue(ctx,palm,r,p.still?1:smooth(0.1,0.35,t));
            if(r.phase==="met" && t<0.77) {
                const burst=1-Math.pow(1-clamp(t/0.76),2);
                ctx.save(); ctx.globalAlpha=1-smooth(0.32,0.77,t);
                for(const angle of [-2.65,-1.85,-1.05,-0.25,0.65]) {
                    const inner=24+26*burst, outer=inner+13*(1-burst*.6);
                    ctx.strokeStyle="#ffe4a0";ctx.lineWidth=4.2;
                    ctx.beginPath();ctx.moveTo(palm.x+inner*Math.cos(angle),palm.y+inner*Math.sin(angle));
                    ctx.lineTo(palm.x+outer*Math.cos(angle),palm.y+outer*Math.sin(angle));ctx.stroke();
                }
                fourPoint(ctx,palm.x+r.side*(35+16*burst),palm.y-32-9*burst,8*(1-0.5*burst),"#fff3c7");
                ctx.restore();
            }
            return;
        }
        if(r.phase==="offer") palmCue(ctx,palm,r,r.guided?0.53:0.68);
        const landed=r.phase==="landed", rest=landed&&t<1.14;
        const x=p.still&&landed?palm.x:r.x, y=p.still&&landed?palm.y-12:r.y;
        ctx.save();
        const fade=landed?1-smooth(1.56,2.23,t):r.phase==="withdraw"?1-smooth(0.15,1.38,t):1;
        ctx.globalAlpha=fade;
        const proximity=1-smooth(23,111,Math.hypot(x-palm.x,y-palm.y));
        if(proximity>0.02) {
            glow(ctx,palm.x,palm.y-2,42,31,"rgba(255,211,117,"+(proximity*.42)+")","rgba(255,211,117,0)");
        }
        for(let i=1;i<r.trail.length;i++) {
            const prev=r.trail[i-1], next=r.trail[i];
            if(Math.hypot(next.x-prev.x,next.y-prev.y)<3) continue;
            ctx.lineWidth=1+i*0.42;
            ctx.strokeStyle="rgba(245,213,135,"+(i/r.trail.length*.45)+")";
            ctx.beginPath(); ctx.moveTo(prev.x,prev.y); ctx.lineTo(next.x,next.y); ctx.stroke();
        }
        const halo=30+(rest?1:3)*Math.sin(t*3.1);
        glow(ctx,x,y,halo,halo,"rgba(255,222,132,.58)","rgba(255,204,104,0)");
        ctx.translate(x,y); ctx.rotate(-0.27+r.tilt*0.8+0.14*Math.sin(t*3)); ctx.scale(1.24,1.24);
        const span=rest?0.54:0.64+0.33*Math.sin(t*22);
        ctx.save(); ctx.scale(1,span);
        shape(ctx,"M-2 -2 Q-22 -19 -17 -24 Q-8 -24 0 -5 M2 -3 Q11 -22 17 -19 Q21 -12 3 0",
              "#d2eee0","#719d94",1.8); ctx.restore();
        shape(ctx,"M-4 0 Q-7 8 -2 13 Q4 15 5 7 Q6 2 2 -2 Z","#ffe29a","#715e46",2.1);
        ctx.beginPath();ctx.arc(0,-3,3.8,0,tau);ctx.fillStyle="#544959";ctx.fill();
        ctx.beginPath();ctx.arc(0,8,2.9,0,tau);ctx.fillStyle="#fff8d4";ctx.fill();
        ctx.restore();
    }

    window.drawDragonProps = function(ctx,p,a,fingers) {
        const s=p.stage, airborne=clamp((p.jump+s.air)/88);
        ctx.save(); ctx.globalCompositeOperation="destination-over";
        window.DragonStageArt.furniture(ctx,s,p.time,false);
        ctx.globalAlpha=1-.62*airborne;
        glow(ctx,397+s.x+p.side*.4,695,116-30*airborne,10-3*airborne,"rgba(120,177,156,.36)","rgba(92,145,134,0)");
        for(const [foot,lift] of [[a.footL,s.footLY],[a.footR,s.footRY]]){
            ctx.globalAlpha=(1-airborne)*clamp(1+lift/23);
            glow(ctx,foot.x,695,28,5.5,"rgba(177,208,183,.43)","rgba(125,170,148,0)");
        }
        ctx.restore();
        ctx.lineCap="round"; ctx.lineJoin="round";
        const cx=(a.left.x+a.right.x)/2, cy=(a.left.y+a.right.y)/2;
        ctx.save();
        if(fingers) {
            // Leave the original inked palms visible over the instruments.
            ctx.beginPath(); ctx.rect(0,0,816,720);
            for(const hand of fingers) {
                ctx.moveTo(hand[0].x,hand[0].y);
                for(let i=1;i<hand.length;i++) ctx.lineTo(hand[i].x,hand[i].y);
                ctx.closePath();
            }
            ctx.clip("evenodd");
        }
        window.DragonStageArt.furniture(ctx,s,p.time,true);
        const guidedPage=p.work?.id==="reading";
        const bookTime=guidedPage ? .85*(p.still && p.work.phase==="done" ? .60 : p.work.progress) : p.time;
        const gear=s.kind?0:s.workMix;
        prop(ctx,p.book*gear,cx,cy+3,book,bookTime);
        prop(ctx,p.pad*gear,cx,cy+17,notepad,p.time);
        prop(ctx,p.pad*gear,a.right.x-15,a.right.y+7,quill,p.time);
        prop(ctx,p.keys*gear*(1-s.dock),cx,cy+16,keys,p.time);
        prop(ctx,p.lens*gear,a.right.x-1,a.right.y-6,magnifier,p.time);
        ctx.restore();
        treats(ctx,p,a);
        if(p.accessory && p.accessory!=="plain")window.DragonArt.icon(ctx,p.accessory,a.fringe.x,a.fringe.y,1.18,p.head*Math.PI/180-.12);
        window.DragonArt.flock(ctx,p,a);
        if(!s.kind)window.DragonArt.work(ctx,p,a);
        playEffects(ctx,p,a);
        window.DragonStageArt.effects(ctx,s,a);
        if(p.nap>0.48) {
            const f=(p.time*0.42)%1;
            ctx.save(); ctx.globalAlpha=clamp((p.nap-0.48)*1.65)*Math.sin(f*Math.PI);
            ctx.fillStyle="#bfcad7"; ctx.font="italic 24px Georgia, serif";
            ctx.fillText("z",a.forehead.x+128+14*f,a.forehead.y-7-43*f);
            ctx.restore();
        }
    };
})();
