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

    window.dragonReadingBeat = function(t) {
        const beat=t%4.8;
        const scan=smooth(.16,1.28,beat)-smooth(1.28,1.52,beat)+smooth(1.64,2.65,beat);
        return {scan:2*scan-1,turn:smooth(3.44,4.40,beat),
                peek:smooth(2.67,2.99,beat)*(1-smooth(3.24,3.62,beat))};
    };
    function book(ctx, turn) {
        ctx.rotate(-0.05);
        shape(ctx, "M-88 -23 Q-39 -33 0 -14 Q44 -41 87 -29 L84 39 Q38 36 2 55 Q-44 39 -85 45 Z",
              "#42635c", "#334540", 4);
        shape(ctx, "M-82 -28 Q-35 -33 0 -13 L1 45 Q-38 31 -79 36 Z", "#f3e4c9", "#b7ac95");
        shape(ctx, "M0 -13 Q42 -38 81 -34 L78 33 Q40 27 1 45 Z", "#fff1d6", "#b7ac95");
        ctx.strokeStyle = "#b9a98c"; ctx.lineWidth = 2.2;
        for (let i = 0; i < 4; i++) {
            ctx.beginPath();
            ctx.moveTo(-68, -12 + i * 9); ctx.quadraticCurveTo(-45, -15 + i * 9, -16, -6 + i * 9);
            ctx.stroke();
        }
        // A gilded star chart on the illuminated page, visible even when held still.
        shape(ctx,"M12 -7 Q45 -24 71 -23 L69 22 Q39 19 12 32Z","#595a7f","#ccab76",2.4);
        shape(ctx,"M20 19 L33 -3 L49 7 L62 -14",null,"#e3cca4",1.5);
        for(const [x,y,size] of [[20,19,3],[33,-3,5],[49,7,3],[62,-14,4]])fourPoint(ctx,x,y,size,"#ffe4aa");
        // A page curls up, crosses the spine, then settles on the other side.
        if (turn > 0 && turn < 1) {
            const tip = 77 * Math.cos(Math.PI * turn);
            const curl = 34 * Math.sin(Math.PI * turn);
            ctx.beginPath(); ctx.moveTo(1,-13);
            ctx.quadraticCurveTo(tip * 0.52, -28-curl, tip, -31-curl);
            ctx.lineTo(tip * 0.96, 34-curl);
            ctx.quadraticCurveTo(tip * 0.44, 30-curl, 1, 45); ctx.closePath();
            const paper=ctx.createLinearGradient(0,0,tip||.01,0);
            paper.addColorStop(0,"#c6ab92");paper.addColorStop(.43,"#fff5d9");paper.addColorStop(1,"#dfcbb2");
            ctx.fillStyle = paper; ctx.fill(); ctx.strokeStyle = "#bfa684"; ctx.stroke();
        }
        const swing=10*Math.sin(Math.PI*turn*2)*Math.sin(Math.PI*turn);
        shape(ctx,"M14 39 Q19 49 "+(16+swing)+" 61",null,"#b67a85",3);
        fourPoint(ctx,16+swing,64,7,"#ffda85");
    }

    // Quadratic spans are sampled once by distance. The nib and the visible end of
    // each line use the same point, including at sharp corners and between strokes.
    function inkStroke(start,end,color,width,x,y,curves){
        const points=[{x,y,d:0}];
        let length=0;
        for(const [cx,cy,ex,ey] of curves){
            for(let i=1;i<=10;i++){
                const u=i/10,v=1-u,px=v*v*x+2*v*u*cx+u*u*ex,py=v*v*y+2*v*u*cy+u*u*ey;
                const prev=points[points.length-1];
                length+=Math.hypot(px-prev.x,py-prev.y);
                points.push({x:px,y:py,d:length});
            }
            x=ex;y=ey;
        }
        return {start,end,color,width,points,length};
    }
    const ink=[
        inkStroke(.36,2.12,"#64526e",3.8,-23,3,[
            [-30,-2,-28,-20],[-26,-24,-12,-14],[1,-19,12,-16],[21,-28,24,-25],
            [28,-20,27,-7],[38,5,24,12],[7,21,-11,16],[-29,14,-23,3]]),
        inkStroke(2.34,2.60,"#64526e",3.5,-15,0,[[-11,-7,-7,-1]]),
        inkStroke(2.82,3.08,"#64526e",3.5,7,-2,[[11,-9,16,-3]]),
        inkStroke(3.26,3.56,"#b67480",3.2,-5,4,[[-2,10,1,4],[5,9,9,3]]),
        inkStroke(3.79,4.17,"#b87980",3.1,39,-12,[[25,-22,33,-25],[36,-26,39,-22],[48,-34,49,-23],[49,-20,39,-12]])
    ];
    function inkPoint(stroke,f){
        const d=clamp(f)*stroke.length,ps=stroke.points;
        let i=1;
        while(i<ps.length-1 && ps[i].d<d)i++;
        const a=ps[i-1],b=ps[i],w=(d-a.d)/(b.d-a.d||1);
        return {x:a.x+(b.x-a.x)*w,y:a.y+(b.y-a.y)*w,index:i};
    }
    function writingBeat(time){
        const t=time%5.8;
        let last={x:-16,y:-15},lastEnd=0;
        for(const s of ink){
            if(t<s.start){
                const next=s.points[0],u=smooth(lastEnd,s.start,t);
                return {t,x:last.x+(next.x-last.x)*u,y:last.y+(next.y-last.y)*u-10*Math.sin(Math.PI*u),peek:0};
            }
            if(t<=s.end)return {t,...inkPoint(s,smooth(s.start,s.end,t)),peek:0};
            last=s.points[s.points.length-1];lastEnd=s.end;
        }
        const lift=smooth(4.17,4.48,t),home=smooth(5.10,5.8,t);
        return {t,x:last.x+8*lift+(-16-last.x-8)*home,y:last.y-16*lift+(-15-last.y+16)*home,
                peek:lift*(1-home)};
    }
    function nibAt(beat,park){
        return {x:beat.x+(54-beat.x)*park,y:beat.y+(-38-beat.y)*park};
    }
    function writingLayout(left,beat,park){
        const x=left.x+79,y=left.y+19,nib=nibAt(beat,park);
        return {x,y,tipX:x+nib.x,tipY:y+nib.y};
    }
    window.DragonWriting={beat:writingBeat,layout:writingLayout};
    function notepad(ctx,beat){
        shape(ctx,"M-89 -22 L70 -39 Q76 -40 78 -33 L87 31 L-69 51 Q-75 52 -77 45Z","#668780","#414e55",3.5);
        shape(ctx,"M-75 37 L77 20 L82 28 L-72 47Z","#d5c1a3","#9b8b80",2);
        shape(ctx,"M-84 -23 L70 -38 L81 23 L-72 42Z","#fff1d9","#b8a48d",2.6);
        shape(ctx,"M-58 -20 L-48 38",null,"#cf9ba0",2.5);
        for(let i=0;i<4;i++){
            const y=-16+i*14;
            shape(ctx,"M-82 "+y+" q-12 -3 -13 4 q0 6 12 5",null,"#817781",3.8);
        }
        shape(ctx,"M-38 26 L63 15",null,"#e5cfad",1.8);
        ctx.save();ctx.strokeStyle="#cc9da5";ctx.lineWidth=1.8;ctx.setLineDash([3.3,3.3]);
        ctx.beginPath();ctx.ellipse(52,8,11,9,-.12,0,tau);ctx.stroke();ctx.restore();
        ctx.save();ctx.globalAlpha*=1-smooth(5.12,5.73,beat.t);
        for(const s of ink){
            if(beat.t<=s.start)break;
            const end=inkPoint(s,smooth(s.start,s.end,beat.t));
            ctx.beginPath();ctx.moveTo(s.points[0].x,s.points[0].y);
            for(let i=1;i<end.index;i++)ctx.lineTo(s.points[i].x,s.points[i].y);
            ctx.lineTo(end.x,end.y);ctx.strokeStyle=s.color;ctx.lineWidth=s.width;ctx.stroke();
        }
        ctx.restore();
        const flip=smooth(5.1,5.8,beat.t);
        if(flip>0 && flip<1){
            const w=158*flip,curl=20*Math.sin(Math.PI*flip);
            ctx.save();ctx.globalAlpha*=Math.sin(Math.PI*flip);
            ctx.beginPath();ctx.moveTo(75-w,-37+.1*w);ctx.quadraticCurveTo(59-w-curl,-10-curl,81-w,23+.1*w);
            ctx.strokeStyle="#b3a49a";ctx.lineWidth=3;ctx.stroke();ctx.restore();
        }
    }
    function quill(ctx) {
        shape(ctx,"M0 -2 Q-9 -29 4 -49 Q12 -62 29 -69 Q28 -54 25 -48 L18 -43 L23 -44 Q21 -34 15 -30 L10 -28 L14 -28 Q11 -15 0 -2Z",
              "#9dd5ca","#506f78",3);
        shape(ctx,"M1 -7 Q5 -36 26 -64 M7 -33 L4 -43 M13 -43 L20 -48",null,"#e5f2da",2.4);
        shape(ctx,"M-1 -10 L5 -7 L1 0 L-5 8 L-5 1Z","#e8b771","#836757",2);
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
        const arc=4*f*(1-f);
        const loft=Math.min(90+height*158,Math.max(0,Math.min(y0,y1)-34));
        const curve=clamp(cast?.curve??side*44,24-Math.min(x0,x1),792-Math.max(x0,x1));
        return {f,x:x0+(x1-x0)*f+curve*arc,y:y0+(y1-y0)*f-loft*arc};
    }
    window.dragonStarPath=starPath;
    function sugarBubble(t,mouth) {
        if(t<1.82 || t>=7.78)return null;
        const loft=smooth(2.02,3.24,t);
        const fade=1-smooth(7.04,7.78,t);
        const x=mouth.x+12+(685-mouth.x-12)*loft+6*Math.sin(2.8*t)*loft;
        const y=mouth.y+4+(239-mouth.y-4)*loft-24*Math.sin(loft*Math.PI)+7*Math.sin(2*t)*loft-21*(1-fade);
        return {x,y,r:51*smooth(1.82,2.65,t)*fade,ready:t>=2.45&&t<=7.04};
    }
    window.dragonSugarBubble=sugarBubble;
    function bubbleArt(ctx,p,a) {
        const t=p.actionTime;
        if(p.action==="bubblePop"){
            const f=clamp(t/.9),origin=p.cast;
            ctx.save();
            ctx.globalAlpha=1-smooth(.38,.95,t);
            ctx.strokeStyle="#fff1dc";ctx.lineWidth=3.5*(1-f)+.3;
            ctx.beginPath();ctx.arc(origin.x,origin.y,origin.r+27*f,0,tau);ctx.stroke();
            const colors=["#ffedb4","#ffb7db","#b8f1ea"];
            for(let i=0;i<11;i++){
                const angle=i*2.40,travel=origin.r+(26+11*(i%4))*f;
                fourPoint(ctx,origin.x+Math.cos(angle)*travel,origin.y+Math.sin(angle)*travel+28*f*f,
                          (7+i%5)*(1-.48*f),colors[i%3]);
            }
            ctx.restore();
            const drift=smooth(.10,.73,t);
            ctx.save();ctx.globalAlpha=1-smooth(.85,1.45,t);
            fourPoint(ctx,origin.x+(a.nose.x+4-origin.x)*drift,
                      origin.y+(a.nose.y-origin.y)*drift-60*Math.sin(Math.PI*drift),13,"#fff0a8");
            ctx.restore();
            return;
        }
        const b=sugarBubble(t,a.mouth);
        if(!b || b.r<1)return;
        ctx.save();ctx.translate(b.x,b.y);ctx.scale(b.r,b.r);
        const glass=ctx.createRadialGradient(-.23,-.37,.04,0,0,1);
        glass.addColorStop(0,"#fffae999");glass.addColorStop(.34,"#f3d7ff28");
        glass.addColorStop(.69,"#a7f7ec2e");glass.addColorStop(.91,"#e8b8fd8c");glass.addColorStop(1,"#fff1f3cc");
        ctx.beginPath();ctx.arc(0,0,.98,0,tau);ctx.fillStyle=glass;ctx.fill();
        const rim=ctx.createLinearGradient(-1,-1,1,1);
        rim.addColorStop(0,"#c8fff3");rim.addColorStop(.47,"#ffa9d4");rim.addColorStop(.72,"#faf2be");rim.addColorStop(1,"#a9caff");
        ctx.strokeStyle=rim;ctx.lineWidth=.064;ctx.stroke();
        ctx.beginPath();ctx.arc(0,0,.78,3.68,4.48);ctx.strokeStyle="#fffcf0";ctx.lineWidth=.10;ctx.stroke();
        ctx.beginPath();ctx.arc(0,0,.82,.42,1.08);ctx.strokeStyle="#aaffec";ctx.lineWidth=.052;ctx.stroke();
        star(ctx,0,.06,.025,.22+.16*Math.sin(t*2),false);
        fourPoint(ctx,.72,-.67,.28,"#fff9dd");
        ctx.restore();
    }
    function treats(ctx,p,a) {
        const t=p.actionTime;
        if(p.action==="feed"||p.action==="bubblePop")bubbleArt(ctx,p,a);
        if(p.action==="feed" && t<1.35) {
            const pickup=smooth(0.08,0.68,t),scale=p.stage.scale;
            const x=643+(a.right.x-8*scale-643)*pickup;
            const y=355+(a.right.y-9*scale-355)*pickup-35*Math.sin(pickup*Math.PI);
            const size=smooth(0,0.15,t)*(1-smooth(1.12,1.35,t));
            star(ctx,x,y,size*1.18,0.2-0.58*pickup,true);
        }
        if(p.action==="play" && t<2.34) {
            const palm=p.cast?.side<0 ? a.left : a.right;
            const {x,y,f}=starPath(t,p.cast,palm);
            const size=(p.cast?1:smooth(0,0.16,t))*(1-smooth(1.92,2.34,t));
            if(f>0.08 && f<0.95) {
                for(let i=3;i>=1;i--){
                    const tail=starPath(t-i*.048,p.cast,palm);
                    fourPoint(ctx,tail.x,tail.y,(8-1.5*i)*size,i%2?"#ffe4a2":"#abe6da");
                }
            }
            star(ctx,x,y,1.22*size,(p.cast?.side??1)*f*4,false);
            const catchBeat=clamp((t-1.38)/.45);
            if(catchBeat>0 && catchBeat<1){
                for(let i=0;i<5;i++){
                    const angle=3.6+i*.76,dist=20+20*catchBeat;
                    fourPoint(ctx,x+Math.cos(angle)*dist,y+Math.sin(angle)*dist,7*Math.sin(Math.PI*catchBeat),"#fff1bc");
                }
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
        if(p.contact){
            const c=p.contact;
            ctx.save();
            // The two short arcs leave the face visible, even under a stationary finger.
            ctx.translate(c.x,c.y);ctx.rotate(.22*p.brushX);
            ctx.strokeStyle="#ffefca";ctx.lineWidth=2.8;
            ctx.globalAlpha=.78*p.press;
            for(const start of [-2.8,.34]){
                ctx.beginPath();ctx.ellipse(0,0,19+3*p.press,14,start*.04,start,start+.72);ctx.stroke();
            }
            ctx.restore();
            for(const s of c.sparks){
                const f=clamp((p.time-s.at)/.72);
                if(f>=1)continue;
                ctx.save();ctx.translate(s.x+12*s.dx*f,s.y-10-45*f);
                ctx.globalAlpha=(1-smooth(.25,1,f))*.93;
                const size=.34+.43*Math.sin(Math.PI*f);
                ctx.scale(size,size);
                shape(ctx,"M0 10 C-27 -6 -9 -18 0 -7 C13 -20 25 -4 0 10Z","#ffc4d4","#a56682",2);
                ctx.restore();
            }
        } else if(p.action==="pat" && t>0.3 && t<1.6) {
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

    window.drawDragonProps = function(ctx,p,a,arms) {
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
        window.DragonStageArt.furniture(ctx,s,p.time,true);
        if(s.desk>.005)arms.draw(ctx,p,"table");
        const guidedPage=p.work?.id==="reading";
        const pageTurn=guidedPage ? (p.still && p.work.phase==="done" ? .60 : p.work.progress) : window.dragonReadingBeat(p.time).turn;
        const gear=s.kind?0:s.workMix*(1-s.present);
        prop(ctx,p.book*gear,cx,cy+3,book,pageTurn);
        if(p.pad*gear>.012){
            const page=writingLayout(a.left,p.writing,p.penPark);
            // Apply entrance opacity to the pair as a unit so the nib stays on the ink.
            ctx.save();ctx.globalAlpha=clamp(p.pad*gear);
            ctx.translate(page.x,page.y);notepad(ctx,p.writing);
            ctx.translate(page.tipX-page.x+5,page.tipY-page.y-8);quill(ctx);
            ctx.restore();
        }
        prop(ctx,p.keys*gear*(1-s.dock),cx,cy+16,keys,p.time);
        prop(ctx,p.lens*gear,a.right.x-1,a.right.y-6,magnifier,p.time);
        ctx.restore();
        treats(ctx,p,a);
        arms.draw(ctx,p,"grip");
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
