/* The pet's attention, Agent activity and touch reactions run on independent clocks. */
(() => {
    "use strict";
    const tau = 2 * Math.PI;
    const clamp = (v, lo=0, hi=1) => Math.max(lo, Math.min(hi, v));
    function ease(a,b,v) {
        const t=clamp((v-a)/(b-a));
        return t*t*(3-2*t);
    }
    function pulse(a,b,c,v) { return ease(a,b,v)*(1-ease(b,c,v)); }

    const rest = {
        head:0, headX:0, nod:0, lean:0, gazeX:0, gazeY:0,
        leftX:0, leftY:0, rightX:0, rightY:0, lid:1, smile:0.12, jaw:0, blush:0,
        wing:1, tailBend:0, tailTip:0, tailLift:0, hair:0, tassel:0, tuft:0,
        breath:0, side:0, squash:0, jump:0, book:0, pad:0, keys:0, lens:0, nap:0,
        leftWing:0, rightWing:0, winkL:0, winkR:0, light:0, squint:0, pout:0, keyContact:0
    };
    const activities = {
        idle:{},
        working:{head:-5.5,nod:1,leftX:24,leftY:-46,gazeX:-0.28,gazeY:-0.36,lid:0.94},
        reading:{head:-2.4,nod:7,gazeY:0.88,leftX:17,leftY:-23,rightX:-19,rightY:-22,book:1,lid:0.91},
        writing:{head:4.2,nod:7,gazeY:0.78,leftX:9,leftY:-4,rightX:-32,rightY:-15,pad:1},
        running:{head:1.3,nod:3,gazeY:0.62,leftX:16,leftY:-12,rightX:-17,rightY:-13,keys:1},
        searching:{head:5.8,nod:-4,rightX:-12,rightY:-53,lens:1,lid:1.08},
        waiting:{head:-6.5,nod:-3,leftX:-7,leftY:-28,gazeY:-0.15,lid:1.06},
        ended:{head:1.8,lid:0.95},
        error:{head:-7,nod:3,leftX:9,rightX:-9,leftY:-14,rightY:-18,smile:-0.35,lid:1.09},
        limited:{head:4.3,nod:11,lid:0.64,smile:-0.17,wing:-5},
        compact:{head:2.4,nod:6,gazeY:.58,leftX:16,rightX:-14,leftY:-12,rightY:-18},
        succeeded:{head:-2,nod:-3,wing:6,smile:.56,lid:1.03},
        cancelled:{head:3,nod:5,lid:.83,wing:-2}
    };
    const activityNotes = {
        idle:"她把尾巴拢在脚边。", working:"她在小桌前敲两下键，抬眼想一想。", reading:"她低下头，一行一行地读。",
        writing:"她握着羽毛笔，在纸上落下几笔。", running:"她正用两只小爪轮流敲键。",
        searching:"她举起放大镜，左右找了找。", waiting:"她举起一只爪，等你回应。",
        ended:"她放下手里的活，慢慢舒了口气。", error:"她缩了缩翅膀，抬头看看你。", limited:"她垂下翅膀，歇一会儿。",
        compact:"她把摊开的纸拢起来，对齐折角。",succeeded:"她悄悄捏住礼花筒的拉绳。",cancelled:"她合上笔帽，等下一次开工。"
    };
    const durations={pat:1.95,feed:2.75,play:2.65,wing:1.72,tail:2.12,nose:1.2,sneeze:1.73};
    const careAt={pat:0.52,feed:1.3,play:1.46};
    const careNotes={pat:"她眯起眼，往你的手心蹭了蹭。",feed:"她接过糖，认真地嚼了几口。",play:"接住了！她把星星抱进怀里。"};
    const startNotes={pat:"她轻轻仰起了头。",feed:"她看见了你手里的星糖。",play:"她盯住星星，悄悄屈起膝盖。",
        wing:"被碰到的翅膀抖了一下。她回头找你的手。",tail:"尾巴绕开了你的手。她回过头来，又把尾巴轻轻摆回来。",
        nose:"她盯住鼻尖，轻轻皱了皱鼻子。",sneeze:"阿嚏！她缩起肩膀，甩了甩头发。"};
    const springs=Object.keys(rest);
    const slow = new Set(["hair","tuft","tailTip","tassel","nap"]);
    const fast = new Set(["gazeX","gazeY","jaw","jump","rightY","leftY","winkL","winkR","squint"]);
    const touchAreas=[["nose",35,24],["left",45,44],["right",45,44],
                      ["wingL",61,48],["wingR",61,48],["tail",58,63]];

    function busyDetails(goal,state,t,elapsed) {
        if(state==="reading") {
            const line=(t*0.77)%1;
            goal.gazeX+=0.60*(2*(ease(0.10,0.82,line)-ease(0.88,1,line))-1);
            goal.rightY-=6*pulse(0,0.43,0.85,t%3.4);
        } else if(state==="writing") {
            goal.rightX+=10*Math.sin(t*7.2);
            goal.rightY+=4*Math.sin(t*12.4);
            goal.gazeX+=0.23*Math.sin(t*2.3);
        } else if(state==="running") {
            const burst=1-pulse(1.45,1.83,2.22,t%2.55);
            goal.leftY-=13*burst*Math.max(0,Math.sin(t*12));
            goal.rightY-=13*burst*Math.max(0,-Math.sin(t*12));
            goal.nod+=2*burst*Math.sin(t*6)-3*(1-burst);
            goal.gazeY-=0.60*(1-burst);
        } else if(state==="searching") {
            goal.gazeX+=0.72*Math.sin(t*1.6);
            goal.rightX+=11*Math.sin(t*1.6);
            goal.head+=3.3*Math.sin(t*1.6-0.3);
        } else if(state==="waiting") {
            const wave=pulse(0,0.42,1.45,elapsed%4.6);
            goal.leftY-=19*wave;
            goal.leftX+=13*wave*Math.sin(elapsed*8);
        } else if(state==="error") {
            goal.nod-=6*pulse(0,0.16,0.68,elapsed);
        } else if(state==="compact") {
            const crease=pulse(0,.52,1.65,elapsed%2.7);
            goal.leftX+=9*crease;goal.rightY+=9*crease;goal.nod+=3*crease;
        }
    }

    function touchPose(g,kind,t,side) {
        if(kind==="wing") {
            const lift=pulse(0,0.20,0.68,t)+0.52*pulse(0.50,0.76,1.17,t);
            g[side<0?"leftWing":"rightWing"]+=24*lift;
            const turn=pulse(0.13,0.52,1.62,t);
            g.gazeX=side*turn; g.head+=side*7*turn; g.nod+=3*turn;
            g[side<0?"winkL":"winkR"]+=lift*0.66;
        } else if(kind==="tail") {
            const coil=pulse(0,0.28,0.93,t), peek=pulse(0.16,0.70,1.85,t);
            g.tailTip-=37*coil-20*pulse(0.70,1.18,1.95,t);
            g.tailLift-=23*coil; g.rightWing+=11*coil;
            g.gazeX=peek; g.gazeY=0.82*peek; g.head+=9*peek;
            g.rightY-=15*peek; g.smile+=0.54*peek;
        } else if(kind==="nose") {
            const tickle=pulse(0,0.20,1.08,t);
            g.squint+=tickle; g.nod-=6*tickle; g.lid*=1-0.36*tickle;
            g.gazeX*=1-tickle; g.gazeY+=0.48*tickle; g.blush+=0.39*tickle;
        } else if(kind==="sneeze") {
            const inhale=pulse(0,0.27,0.47,t), sneeze=pulse(0.36,0.49,0.89,t);
            const settle=pulse(0.75,1.01,1.52,t);
            g.nod-=9*inhale-20*sneeze+4*settle;
            g.jaw+=0.88*inhale+0.65*sneeze; g.lid*=1-sneeze;
            g.squash+=1.2*sneeze; g.wing+=15*sneeze;
            g.leftY-=19*sneeze; g.rightY-=29*sneeze;
            g.head+=side*4*settle; g.blush+=0.64*settle;
        }
    }

    function quietPose(g,quiet,wakeAge,sign,memory) {
        // Look first, turn a little later, then flick the far wing and look back.
        const glance=pulse(4.6,5.03,7.24,quiet), turn=pulse(4.82,5.48,7.36,quiet);
        if(memory) {
            const side=memory.side, hand=side<0?"left":"right";
            g.gazeX+=side*0.84*glance; g.gazeY+=0.75*glance;
            g.head+=side*4.8*turn; g.smile+=0.46*turn;
            if(memory.kind==="five") {
                g[hand+"Y"]-=32*turn;
                g[hand+"X"]+=side*(13+8*Math.sin((quiet-5)*8))*turn;
                g.gazeY-=0.72*pulse(5.6,5.95,6.9,quiet);
            } else {
                g[hand+"Y"]-=24*turn; g[hand+"X"]-=side*12*turn;
                g.nod+=6*turn; g.lid*=1-0.17*turn; g.breath*=1-0.38*turn;
            }
        } else {
            g.gazeX+=sign*0.82*glance; g.gazeY+=0.62*glance;
            g.head+=sign*6.3*turn; g.nod+=3.6*turn;
            g[sign<0?"leftWing":"rightWing"]+=17*pulse(5.36,5.73,6.74,quiet);
            g.tailTip+=sign*12*turn;
        }
        const inhale=pulse(16.1,16.82,18.18,quiet), yawn=pulse(16.66,17.53,19.37,quiet);
        g.nod-=4*inhale+6*yawn; g.jaw+=1.30*yawn;
        g.lid*=1-0.92*yawn; g.leftY-=25*yawn; g.wing+=6*inhale;
        g.breath-=4*inhale; g.blush+=0.23*yawn;
        const first=pulse(0.08,0.51,1.43,wakeAge), second=pulse(0.32,0.83,1.88,wakeAge);
        g.leftWing+=21*first; g.rightWing+=19*second;
        g.leftY-=26*first; g.rightY-=17*second; g.head-=6*first;
        g.winkL+=0.69*first; g.winkR+=0.41*second; g.smile+=0.34*second;
    }

    function carePose(goal,kind,t,side,bond,cast,anchors) {
        if(!(kind in careAt)) {touchPose(goal,kind,t,side);return;}
        const weight=ease(0,0.23,t)*(1-ease(durations[kind]-0.43,durations[kind],t));
        for(const name of ["book","pad","keys","lens","leftX","leftY","rightX","rightY"]) goal[name]*=1-weight;
        if(kind==="pat") {
            const nuzzle=pulse(0.05,0.67,1.73,t);
            goal.head+=side*(6+bond*0.18)*nuzzle;
            goal.headX+=side*8*nuzzle;
            goal.nod-=6*nuzzle;
            goal.lid*=1-nuzzle;
            goal.smile+=nuzzle; goal.blush+=nuzzle*0.84;
            goal.wing+=6*nuzzle;
            goal.rightY-=9*nuzzle;
            goal.tailTip+=13*nuzzle*Math.sin(t*7);
        } else if(kind==="feed") {
            const reach=pulse(0.04,0.58,1.48,t);
            const bite=pulse(0.78,1.19,1.45,t);
            const chew=ease(1.27,1.42,t)*(1-ease(2.18,2.52,t));
            const jaw=0.5+0.5*Math.sin((t-1.32)*14);
            goal.gazeX=goal.gazeX*(1-weight)+weight*(0.97-0.86*ease(0.55,1.23,t));
            goal.gazeY+=weight*0.36;
            goal.rightX+=20*reach-41*bite; goal.rightY-=45*reach+22*bite;
            goal.head+=4*reach-4*bite; goal.nod+=4*bite+3*jaw*chew;
            goal.lid*=1-0.75*chew; goal.jaw+=0.95*bite+0.56*jaw*chew;
            goal.smile+=0.7*chew; goal.blush+=0.48*chew;
        } else if(kind==="play") {
            const crouch=pulse(0.34,0.64,0.86,t);
            const landing=pulse(1.34,1.56,1.98,t);
            const jump=clamp((t-0.68)/0.76);
            const up=Math.sin(jump*Math.PI);
            const hand=cast?.side<0?"left":"right";
            const star=window.dragonStarPath(t,cast,anchors[hand]);
            const lookX=clamp((star.x-anchors.eyes.x)/170,-1,1);
            const lookY=clamp((star.y-anchors.eyes.y)/145,-1,1);
            goal.gazeX=goal.gazeX*(1-weight)+weight*lookX;
            goal.gazeY=goal.gazeY*(1-weight)+weight*lookY;
            goal.head+=lookX*3.5*weight;
            goal.squash+=1.45*crouch+1.12*landing-0.37*up;
            const toward=hand==="left"?-1:1;
            goal.jump+=(32+7*(cast?.height??1))*up; goal.side+=toward*8*up;
            goal[hand+"X"]+=toward*20*up; goal[hand+"Y"]-=49*up+24*landing;
            goal[hand==="left"?"rightY":"leftY"]-=18*up; goal.wing+=18*up-5*crouch;
            goal.tailLift-=14*up; goal.lid*=1-0.55*landing;
            goal.smile+=0.72*landing; goal.blush+=0.42*landing;
        }
    }

    class GptCompanion {
        constructor(canvas,{seed = (Math.random()*0xffffffff)>>>0,manual=false,bond=0,
                            onNote=()=>{},onBond=()=>{},onPlay=()=>{},onWork=()=>{},onStory=()=>{},onTone=()=>{},accessory="plain"}={}) {
            this.canvas=canvas; this.seed=seed; this.manual=manual;
            this.onNote=onNote; this.onBond=onBond; this.onPlay=onPlay; this.bond=clamp(bond,0,16);
            this.pose={...rest}; this.velocity=Object.fromEntries(springs.map(k=>[k,0]));
            this.scene={state:"idle",offline:false,reduced:false,active:false};
            this.time=0; this.stateTime=0; this.quiet=0; this.attention=null;
            this.autoX=0; this.autoY=0; this.nextLook=1.5;
            this.blinkAt=1.25; this.doubleBlink=false; this.blink=0;
            this.action=null; this.nextCare=null; this.heldStar=null; this.slept=false;
            this.game=new window.DragonPlay(this); this.work=new window.DragonWork(this);
            this.onWork=onWork;this.onStory=onStory;this.onTone=onTone;this.accessory=accessory;this.wakeAt=-Infinity;
            this.stage=new window.DragonStage(this);
            this.helpers=0;this.craneIndex=-1;this.craneAt=-Infinity;
            this.quietSide=1; this.quietBeat=""; this.lastPlay=null;
            this.frameId=0; this.lastFrame=null; this.stillTimer=0;
            this.frame=this.frame.bind(this);
            const image=new Image();
            image.src=window.GPT_DRAGON_PORTRAIT;
            this.ready=image.decode().then(()=>{
                this.rig=new window.DragonRig(canvas,image);
                this.refresh(true);
                canvas.dataset.ready="true";
            });
        }

        random() {
            this.seed=(Math.imul(1664525,this.seed)+1013904223)>>>0;
            return this.seed/4294967296;
        }
        note(text) { if(this.scene.active) this.onNote(text); }
        reward() {
            this.bond=Math.min(16,this.bond+1);
            this.onBond(this.bond);
        }
        craneAtPoint(point){
            if(!point||!this.helpers||this.action||this.game.round||this.work.round)return -1;
            return window.DragonArt.cranes(this.helpers,this.time,this.rig.anchors).findIndex(b=>
                Math.hypot(point.worldX-b.x,point.worldY-b.y)<(point.hitRadius||43));
        }
        greetCrane(i){
            if(i<0||i>=this.helpers)return;
            this.craneIndex=i;this.craneAt=this.time;
            this.note("纸鹤朝你扑了扑翅膀，她也跟着望了过去。");
            if(this.scene.reduced)this.advance(0,true);
        }
        baseNote() {
            return this.stage.officeNote() || (this.scene.offline ? "电脑已离线，她在身边歇着。" : activityNotes[this.scene.state]);
        }

        setScene(next) {
            const prior=this.scene;
            if(next.active===prior.active && next.reduced===prior.reduced &&
               next.offline===prior.offline && next.state===prior.state && next.scope===prior.scope) return;
            this.scene={...next};
            const stateChanged=prior.offline!==next.offline || prior.state!==next.state || prior.scope!==next.scope;
            if(stateChanged || !next.active || prior.reduced!==next.reduced)this.work.clear();
            if(stateChanged) { this.stateTime=0; this.quiet=0; this.slept=false; this.wakeAt=-Infinity;this.craneIndex=-1; }
            if(!next.active || prior.reduced!==next.reduced || prior.scope!==next.scope ||
               (stateChanged && (["waiting","error","limited"].includes(next.state)||next.offline))) {
                this.action=null; this.nextCare=null; this.attention=null; this.heldStar=null;
                this.game.clear();
                clearTimeout(this.stillTimer);
            }
            if(stateChanged && !next.offline && next.state==="succeeded"){
                clearTimeout(this.stillTimer);this.action=this.nextCare=this.heldStar=null;this.game.clear();
            }
            this.stage.sync(prior,next);
            if(next.active && (!prior.active || stateChanged || next.reduced!==prior.reduced) && !this.action && !this.game.round && !this.stage.mood) this.note(this.baseNote());
            if(this.rig) this.refresh(next.reduced || !prior.active);
        }

        refresh(snap=false) {
            cancelAnimationFrame(this.frameId);
            this.frameId=0; this.lastFrame=null;
            if(!this.scene.active || !this.rig) return;
            this.advance(0,snap);
            if(!this.manual && !this.scene.reduced) this.frameId=requestAnimationFrame(this.frame);
        }
        frame(now) {
            if(!this.scene.active || this.scene.reduced) return;
            if(this.lastFrame===null || now-this.lastFrame>=1000/30-0.9) {
                const dt=this.lastFrame===null ? 0 : Math.min(0.10,(now-this.lastFrame)/1000);
                this.lastFrame=now;
                this.advance(dt);
            }
            this.frameId=requestAnimationFrame(this.frame);
        }

        pointAt(clientX,clientY,pointerType) {
            const box=this.canvas.getBoundingClientRect();
            if(!box.width || !box.height || !this.rig) return null;
            const x=(clientX-box.left)*window.DragonRig.world.width/box.width;
            const y=(clientY-box.top)*window.DragonRig.world.height/box.height;
            const eye=this.rig.anchors.eyes;
            const inHead=((x-eye.x)/180)**2+((y-eye.y+75)/157)**2<1;
            const inBody=((x-eye.x)/213)**2+((y-523)/179)**2<1;
            const touch=pointerType==="touch"||pointerType==="pen";
            const slop=touch?clamp(14*window.DragonRig.world.width/box.width-35,0,20):0;
            const hitRadius=touch?clamp(22*window.DragonRig.world.width/box.width,51,82):51;
            let part=null;
            for(const [key,rx,ry] of touchAreas) {
                const a=this.rig.anchors[key];
                if(((x-a.x)/(rx+slop))**2+((y-a.y)/(ry+slop))**2<=1) {part=key;break;}
            }
            return {region:inHead?"head":inBody?"body":"surround",part,hitRadius,worldX:x,worldY:y,
                    x:clamp((x-eye.x)/204,-1,1),y:clamp((y-eye.y)/150,-1,1)};
        }
        attend(point) {
            if(!this.scene.active || this.scene.reduced || !point || this.game.round || this.work.round || this.stage.mood) return;
            if(this.slept) {
                this.slept=false; this.wakeAt=this.time;
                this.note("她睁开一只眼，先伸左翅，再慢慢伸开右翅。");
            }
            if(this.quiet>=4.6) this.quietSide=this.random()<0.5?-1:1;
            this.attention={x:point.x,y:point.y,until:this.time+2.8};
            this.quiet=0;
        }
        releaseAttention() {
            if(this.attention) this.attention.until=Math.min(this.attention.until,this.time+0.32);
        }

        stroke(point) {
            if(this.action?.kind!=="pat" || this.scene.reduced) return;
            this.action.lastStroke=this.time;
            if(Math.abs(point.x)>0.08) this.action.side=clamp(point.x*2,-1,1);
        }
        holdStar(point) {
            if(!this.action && !this.scene.reduced && !this.stage.sulking) {
                this.heldStar={x:clamp(point.worldX,38,778),y:clamp(point.worldY,36,669)};
            }
        }
        releaseStar(dx,dy) {
            const release=this.heldStar;
            this.heldStar=null;
            const side=dx<-7?-1:dx>7?1:(release && release.x<400?-1:1);
            const cast=release?{...release,side,height:clamp(-dy/87)}:null;
            this.care("play",side,cast);
        }
        care(kind,side=0,cast=null) {
            if(!durations[kind] || !this.scene.active || !this.rig) return;
            if(this.stage.sulking){
                if(kind==="feed")this.stage.perform("mend");
                else this.stage.refuse();
                return;
            }
            if(kind==="tail" && this.stage.tailTap())return;
            this.stage.interrupt();
            this.work.clear();
            this.game.clear();
            if(this.action) {
                if(kind==="nose" && this.action.kind==="nose" &&
                   (this.scene.reduced || this.time-this.action.at<0.82)) {
                    kind="sneeze"; this.nextCare=null;
                } else {
                    if(!this.scene.reduced && this.action.kind!==kind && kind in careAt) this.nextCare={kind,side,cast};
                    return;
                }
            }
            this.quiet=0; this.slept=false;
            const direction=Math.abs(side)>0.12?Math.sign(side):(this.random()<0.5?-1:1);
            this.action={kind,side:direction,cast,at:this.time,awarded:false,lastStroke:-Infinity};
            this.note(startNotes[kind]);
            if(this.scene.reduced) {
                clearTimeout(this.stillTimer);
                this.action.at=this.time-(careAt[kind]??0.44)-0.05;
                this.advance(0,true);
                this.stillTimer=setTimeout(()=>{
                    this.action=null;
                    if(this.scene.active) { this.advance(0,true); this.note(this.baseNote()); }
                },1350);
            }
        }

        updateClocks(dt) {
            this.time+=dt; this.stateTime+=dt; this.quiet+=dt;
            this.game.tick(dt);
            this.work.tick(dt);
            this.stage.tick(dt);
            if(this.attention && this.time>=this.attention.until) this.attention=null;
            if(this.time>=this.nextLook) {
                this.autoX=(this.random()-0.5)*1.14;
                this.autoY=(this.random()-0.5)*0.59;
                this.nextLook=this.time+1.7+this.random()*3.5;
            }
            const b=this.time-this.blinkAt;
            this.blink=pulse(0,0.08,0.23,b);
            if(b>0.24) {
                const short=!this.doubleBlink && this.random()<0.17;
                this.blinkAt=this.time+(short?0.09:2.1+this.random()*3.8);
                this.doubleBlink=short;
            }
            if(this.action) {
                if(this.action.kind==="pat" && this.time-this.action.lastStroke<0.20 &&
                   this.time-this.action.at>0.84 && this.time-this.action.at<1.12) this.action.at+=dt;
                const age=this.time-this.action.at;
                if(!this.action.awarded && this.action.kind in careAt && age>=careAt[this.action.kind]) {
                    this.action.awarded=true; this.reward(); this.note(careNotes[this.action.kind]);
                }
                if(age>=durations[this.action.kind]) {
                    this.action=null; this.quiet=0; this.note(this.baseNote());
                    const queued=this.nextCare; this.nextCare=null;
                    if(queued) this.care(queued.kind,queued.side,queued.cast);
                }
            }
        }

        target(stage) {
            const state=this.scene.offline?"idle":this.scene.state;
            const g={...rest,...activities[state]};
            const t=this.time;
            busyDetails(g,state,t,this.stateTime);
            const atRest=["idle","ended","succeeded","cancelled"].includes(state);
            const free=atRest && !this.action && !this.game.round && !this.work.round && !this.stage.mood && !this.stage.office;
            g.nap=free && !this.attention ? ease(22,27,this.quiet) : 0;
            if(g.nap>0.65 && !this.slept) {this.slept=true;this.note("她把翅膀收好，打起了小盹。");}
            if(this.slept && g.nap<0.1) {this.slept=false;if(!this.action && !this.game.round)this.note(this.baseNote());}
            const aw=1-g.nap;
            const rhythm=state==="running"?2.9:1.85;
            g.breath=(Math.sin(t*rhythm)+0.16*Math.sin(t*rhythm*2-0.6))*(2.45+g.nap);
            const attention=this.attention;
            const follow=attention ? (atRest?0.96:0.76) : 0;
            g.gazeX=(g.gazeX+this.autoX*(atRest?0.85:0.23))*(1-follow)+(attention?.x||0)*follow;
            g.gazeY=(g.gazeY+this.autoY*(atRest?0.8:0.18))*(1-follow)+(attention?.y||0)*follow;
            g.head+=g.gazeX*4.8*aw+1.15*Math.sin(t*0.74);
            g.headX=g.gazeX*6*aw; g.nod+=g.gazeY*3.8+13*g.nap;
            g.lean=g.gazeX*3.6*aw; g.head+=6*g.nap; g.lid*=1-g.nap;
            const wingBeat=Math.sin(t*1.72-0.3)*2+Math.sin(t*0.74+0.7);
            g.wing+=wingBeat*aw-10*g.nap;
            g.tailBend=(6*Math.sin(t*1.65)+3*Math.sin(t*0.69))*aw;
            g.tailTip=(18*Math.sin(t*1.65-0.72)+3.2*Math.sin(t*0.92))*(1+this.bond*0.018)*(0.2+0.8*aw);
            g.tailLift=6.7*Math.sin(t*1.65-1.1)*aw;
            const quiet=free && !attention && !this.scene.reduced ? this.quiet : 0;
            const curious=quiet>=4.6 && quiet<7.4;
            if(this.lastPlay && (t-this.lastPlay.at>45 || (this.lastPlay.recalled&&!curious))) this.lastPlay=null;
            const memory=curious?this.lastPlay:null;
            if(memory)memory.recalled=true;
            if(free && !this.scene.reduced) quietPose(g,quiet,t-this.wakeAt,this.quietSide,memory);
            if(Number.isFinite(this.wakeAt) && t-this.wakeAt>1.95) {
                this.wakeAt=-Infinity;
                if(free)this.note(this.baseNote());
            }
            const beat=curious ? (memory?.kind??"peek") : quiet>=16.1 && quiet<19.4 ? "yawn" : "";
            if(beat!==this.quietBeat) {
                if(beat==="peek") this.note(this.quietSide>0?"她听见一点动静，瞄了瞄自己的尾巴。":"她偏过头，听了听翅膀边的动静。");
                else if(beat==="five") this.note("她又举起刚才和你击掌的那只手，悄悄挥了两下。");
                else if(beat==="fly") this.note("她低头瞧了瞧刚才停过萤火虫的手心。");
                else if(beat==="yawn") this.note("她吸了口气，抬起手打了个长长的哈欠。");
                else if(free && t-this.wakeAt>2) this.note(this.baseNote());
                this.quietBeat=beat;
            }
            if(state==="idle" && t<1.55 && !this.action) {
                const stretch=pulse(0,0.44,1.55,t);
                g.wing+=10*stretch; g.head-=4*stretch; g.leftY-=15*stretch;
            }
            if(this.action) carePose(g,this.action.kind,t-this.action.at,this.action.side,
                                     this.bond,this.action.cast,this.rig.anchors);
            this.game.pose(g);
            this.work.pose(g);
            this.stage.pose(g,stage);
            if(this.craneIndex>=0&&this.craneIndex<this.helpers&&!this.work.round&&!this.game.round&&!this.action){
                const age=t-this.craneAt;
                if(age<1.45){
                    const toward=this.craneIndex?1:-1, watch=this.scene.reduced ? .8 : pulse(0,.19,1.45,age);
                    g.gazeX=g.gazeX*(1-watch)+toward*watch;g.gazeY=g.gazeY*(1-watch)-.75*watch;
                    g.head+=toward*6*watch;g.smile+=.48*watch;
                }else{this.craneIndex=-1;this.note(this.baseNote());}
            }
            g.hair=clamp(-this.velocity.head*0.23-this.velocity.side*0.11,-12,12)+2.5*Math.sin(t*1.49)+stage.ruffle*8;
            g.tassel=g.hair*1.8+3*Math.sin(t*1.82-0.65);
            g.tuft=g.hair*0.95+2.4*Math.sin(t*1.65-0.5);
            return g;
        }

        advance(dt,snap=false) {
            if(!this.rig || !this.scene.active) return;
            this.updateClocks(dt);
            const stage=this.stage.sample();
            const goal=this.target(stage);
            const steps=Math.max(1,Math.ceil(dt*96)), step=dt/steps;
            for(const name of springs) {
                if(snap) {this.pose[name]=goal[name];this.velocity[name]=0;continue;}
                const frequency=fast.has(name)?23:slow.has(name)?8.1:13.6;
                const damping=slow.has(name)?0.73:0.91;
                let x=this.pose[name], speed=this.velocity[name];
                for(let i=0;i<steps;i++) {
                    speed+=(frequency*frequency*(goal[name]-x)-2*damping*frequency*speed)*step;
                    x+=speed*step;
                }
                this.pose[name]=x; this.velocity[name]=speed;
            }
            this.pose.blink=this.scene.reduced?0:this.blink;
            this.pose.time=this.time;
            this.pose.stage=stage;
            const theatre=stage.desk>.02||!!stage.kind;
            if(theatre!==this.inTheatre){
                this.inTheatre=theatre;this.canvas.classList.toggle("in-theatre",theatre);
            }
            this.pose.action=this.action?.kind||"";
            this.pose.actionTime=this.action ? this.time-this.action.at : 0;
            this.pose.cast=this.action?.cast;
            this.pose.heldStar=this.heldStar;
            this.pose.round=this.game.round;
            this.pose.still=this.scene.reduced;
            this.pose.accessory=this.accessory;
            this.pose.helpers=this.helpers;this.pose.craneIndex=this.craneIndex;this.pose.craneAge=this.time-this.craneAt;
            const available=this.work.id;
            const passive=!this.game.round&&!this.action&&["waiting","limited","error","succeeded","compact","cancelled"].includes(available);
            this.pose.work=this.work.round || (passive?{id:available,age:this.stateTime,phase:"offer",passive:true}:null);
            this.rig.draw(this.pose);
        }
    }
    window.GptCompanion=GptCompanion;
})();
