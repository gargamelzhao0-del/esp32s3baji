/* Timelines use seconds, distances use the rig's logical pixels, and the floor is y=695. */
(() => {
    "use strict";
    const busy=new Set(["working","reading","writing","running","searching","compact"]);
    const urgent=new Set(["waiting","error","limited"]);
    const setUp=6.15, park=654, dock=324;
    const desk={y:529,buttonX:151,buttonY:-12,keyLeft:35,keyRight:124,screenX:235,screenY:-19};
    const unit=x=>Math.max(0,Math.min(1,x));
    const mix=(a,b,t)=>a+(b-a)*t;
    function ease(a,b,x){const t=unit((x-a)/(b-a));return t*t*(3-2*t);}
    const beat=(a,b,c,d,t)=>ease(a,b,t)*(1-ease(c,d,t));
    const arc=(a,b,x)=>{const t=unit((x-a)/(b-a));return 4*t*(1-t);};
    const neutral=()=>({x:0,scale:1,bodyY:0,tilt:0,seat:0,turn:0,air:0,footLX:0,footLY:0,footRX:0,footRY:0,
        desk:0,handle:park,grip:0,open:0,boot:0,dock:0,stool:0,roll:0,workMix:1,switchReach:0,press:0,ruffle:0,
        present:0,meet:0,setup:false});

    // During the support half-cycle x offsets the distance travelled by the body.
    // The swing half catches up along an arc. Distance controls the feet even as a heavy cart slows.
    function walk(s,delta,stride,weight,height){
        const distance=Math.abs(delta);
        for(const [foot,offset] of [["L",0],["R",.5]]){
            const phase=(distance/stride+offset)%1;
            const swing=ease(.5,1,phase);
            s["foot"+foot+"X"]+=Math.sign(delta)*stride*(swing-phase+.25)*weight;
            s["foot"+foot+"Y"]-=height*Math.sin(Math.PI*unit((phase-.5)*2))*weight;
        }
        s.bodyY-=4*weight*Math.abs(Math.sin(2*Math.PI*distance/stride));
    }
    const setupNotes=[
        [0,"她听见开工的动静，转头找自己的小桌子。"],[.58,"她踮着脚走过去，伸手够桌沿的拉手。"],
        [1.87,"抓稳了。她往后一仰，一步一步把桌子拖过来。"],[3.85,"她停住小轮子，绕到桌后把凳子勾出来。"],
        [4.69,"她探过身一按，桃色开关咔哒一声，折叠屏弹了起来。"],[5.51,"屏幕亮了。她把爪子搭上键盘，先试敲两下。"]
    ];
    function officePose(t){
        const s=neutral(), approach=ease(.54,1.60,t);
        // Two pulls and a short catch of breath before the little casters roll freely.
        const pull=.37*ease(1.99,2.68,t)+.63*ease(2.76,3.83,t);
        const behind=ease(3.96,4.96,t);
        s.desk=ease(.02,.55,t);
        s.scale=1-.19*ease(.02,.55,t);
        s.x=140*approach-284*pull+112*behind;
        s.handle=mix(park,dock,pull);
        s.grip=beat(1.43,1.78,3.84,4.02,t);
        s.effort=beat(1.66,2.10,3.64,3.96,t);
        s.reach=beat(1.18,1.68,3.82,4.06,t);
        s.look=beat(.04,.38,3.65,4.37,t);
        s.tilt=.043*beat(.52,.91,1.53,1.91,t)-.076*s.effort+.039*beat(4.08,4.36,4.77,5.02,t);
        s.ruffle=beat(1.91,2.16,2.40,2.74,t)+beat(2.75,3.06,3.45,3.91,t);
        s.roll=pull; s.dock=behind;
        s.stool=ease(4.05,4.76,t);
        s.seat=ease(4.56,5.08,t);
        s.switchReach=beat(4.57,4.92,5.20,5.48,t);
        s.press=beat(4.91,5.04,5.15,5.31,t);
        s.open=ease(5.03,5.55,t);
        s.boot=ease(5.49,5.84,t);
        s.workMix=ease(5.52,setUp,t);
        s.setup=t<setUp;
        walk(s,140*approach,68,beat(.54,.72,1.45,1.62,t),25);
        walk(s,-284*pull,83,beat(1.95,2.16,3.64,3.88,t),21);
        walk(s,112*behind,71,beat(3.95,4.14,4.79,4.99,t),26);
        s.footLY-=15*s.seat;s.footRY-=18*s.seat;
        s.footLX-=4*s.seat;s.footRX+=9*s.seat;
        return s;
    }
    const storyNotes={
        cheer:"她先蹲低蓄力，张开翅膀往上一蹦！",
        sulk:"她鼓起脸把身子扭开了。给她一颗星糖，或安静陪她一会儿。",
        mend:"她从眼角偷看了一下，慢慢伸手接过和好糖。"
    };
    class DragonStage {
        constructor(pet){this.pet=pet;this.office=null;this.mood=null;this.taps=[];this.timer=0;this.showTimer=0;}
        get sulking(){return this.mood?.kind==="sulk";}
        get canPresent(){
            return !!this.office && !this.office.exit && this.office.age>=setUp &&
                   !this.pet.action&&!this.pet.game.round&&!this.pet.work.round&&!this.mood;
        }
        onMonitor(point){
            if(!point||!this.canPresent)return false;
            return Math.abs(point.worldX-dock-desk.screenX)<64 && Math.abs(point.worldY-desk.y-desk.screenY+59)<51;
        }
        clearShow(){
            clearTimeout(this.showTimer);this.showTimer=0;
            if(this.office)this.office.showAge=null;
        }
        showMonitor(){
            if(!this.canPresent||this.office.showAge!=null)return;
            const p=this.pet;
            this.office.showAge=p.scene.reduced ? .96 : 0;p.attention=null;p.quiet=0;
            p.note("她把小显示器转向你，指指屏幕，再抬起眼睛看看你。");
            if(p.scene.reduced){
                p.advance(0,true);
                this.showTimer=setTimeout(()=>{this.clearShow();if(p.scene.active){p.advance(0,true);p.note(p.baseNote());}},1500);
            }
        }
        changed(){this.pet.onStory?.();}
        clearMood(){
            clearTimeout(this.timer);this.timer=0;
            this.mood=null;this.taps=[];
        }
        startOffice(preview=false){
            this.office={age:this.pet.scene.reduced?setUp:0,preview,exit:null,beat:-1};
        }
        leaveOffice(){
            const o=this.office;
            if(!o||o.exit)return;
            this.clearShow();
            if(this.pet.scene.reduced||o.age<.06){this.office=null;return;}
            o.exit={age:0,from:officePose(o.age)};
        }
        finishSetup(){
            if(this.office && !this.office.exit && this.office.age<setUp){
                this.office.age=setUp;this.office.beat=setupNotes.length;
            }
        }
        sync(prior,next){
            const scopeChanged=prior.scope!==next.scope;
            const different=scopeChanged||prior.state!==next.state||prior.offline!==next.offline;
            if(different||!next.active||prior.reduced!==next.reduced)this.clearShow();
            if(scopeChanged||next.offline){this.office=null;this.clearMood();}
            if(!next.active){
                this.clearMood();
                if(this.office?.preview||this.office?.exit)this.office=null;
                else this.finishSetup();
                if(different&&!busy.has(next.state))this.office=null;
                return;
            }
            if(different && (urgent.has(next.state)||next.offline||(busy.has(next.state)&&!busy.has(prior.state))))this.clearMood();
            if(different && this.mood?.origin==="agent" && next.state!=="succeeded")this.clearMood();
            if(!next.offline && busy.has(next.state)){
                if(!this.office||this.office.exit)this.startOffice();
                this.office.preview=false;
            }else if(!next.offline && urgent.has(next.state)){
                this.finishSetup();
                if(this.office)this.office.preview=false;
            }else if(different && this.office)this.leaveOffice();
            if(different&&!next.offline&&next.state==="succeeded")this.setMood("cheer","agent");
            if(next.reduced){
                if(this.office?.exit)this.office=null;else this.finishSetup();
                if(this.mood && !this.timer)this.reducedHold();
            }else if(this.timer){clearTimeout(this.timer);this.timer=0;}
        }
        setMood(kind,origin="pet"){
            const previous=kind==="mend"?this.sample():null;
            this.clearMood();this.mood={kind,origin,age:0,fromPuff:previous?.puff,fromTurn:previous?.turn};
            this.pet.quiet=0;this.pet.slept=false;
            this.pet.attention=null;this.pet.note(storyNotes[kind]);this.changed();
            if(this.pet.scene.reduced)this.reducedHold();
        }
        reducedHold(){
            const mood=this.mood;if(!mood)return;
            mood.age={cheer:2.28,sulk:2.05,mend:.94}[mood.kind];
            this.timer=setTimeout(()=>{
                if(this.mood!==mood||!this.pet.scene.active)return;
                this.endMood();
                this.pet.advance(0,true);
            },mood.kind==="sulk"?6100:1600);
        }
        endMood(){
            const prior=this.mood?.kind;this.clearMood();this.pet.quiet=0;this.changed();
            this.pet.note(prior==="sulk"?"她悄悄挪回来，用尾巴轻轻碰了碰你。":this.pet.baseNote());
        }
        tailTap(){
            if(this.sulking)return true;
            const now=this.pet.scene.reduced?performance.now()/1000:this.pet.time;
            this.taps=this.taps.filter(t=>now-t<=2.65);this.taps.push(now);
            if(this.taps.length<3)return false;
            this.perform("tease");return true;
        }
        refuse(){
            if(!this.sulking)return;
            this.mood.lastKnock=this.mood.age;
            this.pet.note("她把小爪收得更紧，偷偷瞄了一眼星糖。");
        }
        perform(which){
            const p=this.pet;
            if(!p.scene.active||!p.rig||!["desk","tease","cheer","mend"].includes(which))return;
            if(which==="mend"&&!this.sulking)return;
            this.clearShow();
            p.game.clear();p.work.clear();clearTimeout(p.stillTimer);
            p.action=p.nextCare=p.heldStar=p.attention=null;p.slept=false;p.quiet=0;
            if(which==="desk"){
                this.clearMood();
                this.startOffice(!busy.has(p.scene.state)||p.scene.offline);
                p.note(p.scene.reduced?"她已经坐好，小电脑亮着。":setupNotes[0][1]);this.changed();
            }else{
                this.finishSetup();
                this.setMood(which==="tease"?"sulk":which==="mend"?"mend":"cheer");
            }
            if(p.scene.reduced)p.advance(0,true);
        }
        interrupt(){
            this.clearShow();
            this.finishSetup();
            if(this.office?.preview&&this.pet.scene.reduced)this.office=null;
            if(this.mood){this.clearMood();this.changed();}
        }
        tick(dt){
            const o=this.office,p=this.pet;
            if(o && !p.scene.reduced){
                if(o.exit){o.exit.age+=dt;if(o.exit.age>=.98)this.office=null;}
                else{
                    const before=o.age;o.age+=dt;
                    if(o.showAge!=null){
                        o.showAge+=dt;
                        if(o.showAge>=2.13){this.clearShow();p.note(p.baseNote());}
                    }
                    if(before<5.04&&o.age>=5.04)p.onTone?.("switch");
                    const i=setupNotes.findLastIndex(([time])=>time<=o.age);
                    const next=o.age>=setUp?setupNotes.length:i;
                    if(next!==o.beat){
                        o.beat=next;
                        if(next===setupNotes.length)this.changed();
                        if(!this.mood&&!p.action&&!p.game.round&&!p.work.round)p.note(this.officeNote()||p.baseNote());
                    }
                    if(o.preview && o.age>=10.4 && o.showAge==null && !this.mood && !p.action && !p.game.round && !p.work.round)this.leaveOffice();
                }
            }
            const m=this.mood;
            if(!m)return;
            if(!p.scene.reduced){
                const before=m.age;m.age+=dt;
                if(m.kind==="cheer"&&before<.55&&m.age>=.55)p.onTone?.("jump");
            }
            if(m.kind==="mend"&&!m.accepted&&m.age>=.87){m.accepted=true;p.reward();p.onTone?.("sweet");}
            if(p.scene.reduced)return;
            const length={cheer:2.92,sulk:7.25,mend:2.35}[m.kind];
            if(m.age>=length)this.endMood();
        }
        officeNote(){
            const o=this.office;
            if(!o||o.exit)return "";
            if(o.age>=setUp)return o.preview?"她坐稳了，对着小电脑敲起了键盘。":"";
            return setupNotes[Math.max(0,o.beat)]?.[1]||"";
        }
        sample(){
            const o=this.office,m=this.mood;
            const s=o?(o.exit?{...o.exit.from}:officePose(o.age)):neutral();
            if(o?.exit){
                const t=ease(0,.98,o.exit.age);
                s.handle=mix(s.handle,865,t);s.roll+=t*1.65;s.desk*=1-ease(.47,.98,o.exit.age);
                s.x*=1-t;s.scale=mix(s.scale,1,t);s.seat*=1-t;s.stool*=1-t;
                s.grip=s.look=s.effort=s.reach=s.press=s.switchReach=0;s.setup=false;s.workMix=1;
                s.tilt*=1-t;s.ruffle*=1-t;
                s.footLX*=1-t;s.footRX*=1-t;s.footLY*=1-t;s.footRY*=1-t;s.bodyY*=1-t;
            }
            s.typing=!!o&&!o.exit&&(o.preview||this.pet.scene.state==="working"||this.pet.scene.state==="running");
            s.paused=urgent.has(this.pet.scene.state)&&!o?.preview;
            s.displayMode=o?.preview?"working":this.pet.scene.state;
            if(o?.showAge!=null){
                s.present=beat(0,.42,1.64,2.13,o.showAge);
                s.meet=beat(.67,.98,1.57,2.03,o.showAge);
            }
            s.keyStroke=this.pet.scene.reduced?0:Math.sin(this.pet.time*10.2);
            s.keyLights=s.typing&&!m&&!this.pet.action&&!this.pet.game.round&&s.present<.06&&
                        (!this.pet.work.round||this.pet.work.round.id==="running");
            if(s.dock>.97&&!s.paused&&!this.pet.scene.reduced){
                const swing=Math.sin(this.pet.time*2.7)*s.seat;
                s.footLY-=3*swing;s.footRY+=4*swing;
            }
            if(!m)return s;
            const t=m.age;s.kind=m.kind;s.age=t;
            if(m.kind==="cheer"){
                const stretch=beat(.02,.44,2.34,2.85,t);
                s.scale=Math.min(s.scale,1-.15*stretch);
                s.air=97*arc(.55,1.45,t)+34*arc(1.69,2.17,t);
                s.footLY-=s.air*.12;s.footRY-=s.air*.27;s.footRX+=s.air*.09;
                s.x+=12*arc(.55,1.45,t)-5*arc(1.69,2.17,t);
                s.crouch=beat(.06,.35,.39,.56,t)+.93*beat(1.40,1.53,1.54,1.69,t)+.55*beat(2.13,2.26,2.28,2.51,t);
                s.bodyY+=26*s.crouch;
            }else{
                const sulk=m.kind==="sulk";
                const turn=sulk?beat(.08,.71,5.63,7.17,t):-m.fromTurn*(1-ease(.03,.95,t));
                s.turn=-turn;s.x-=19*turn;s.tilt-=.028*turn;
                s.peek=sulk?beat(2.71,2.96,3.16,3.45,t)+beat(4.53,4.83,5.03,5.35,t):ease(.04,.53,t);
                s.puff=sulk?beat(.13,.72,5.77,7.13,t):m.fromPuff*(1-ease(.04,.87,t));
                s.snub=Number.isFinite(m.lastKnock)?beat(0,.12,.24,.67,t-m.lastKnock):0;
            }
            return s;
        }
        pose(g,s){
            if(s.desk>.001 && !s.kind){
                const act=s.workMix,reach=s.reach||0,effort=s.effort||0,look=s.look||0;
                for(const hand of ["leftX","leftY","rightX","rightY"])g[hand]*=act;
                g.gazeX=mix(g.gazeX,.98,look);g.gazeY=mix(g.gazeY,.43,look);
                g.head+=7*look-12*effort;g.lean+=11*look-29*effort;g.nod+=7*effort;
                g.leftX-=29*effort;g.leftY-=29*effort;g.rightX+=29*reach;g.rightY-=11*reach;
                g.squash+=.7*effort;g.lid*=1-.25*effort;g.wing+=10*effort;g.tailLift-=19*effort;
                g.rightX+=28*s.switchReach;g.rightY-=38*s.switchReach;
                g.leftY-=21*s.seat;g.rightY-=21*s.seat;
                const freeHands=!this.pet.action&&!this.pet.game.round&&(!this.pet.work.round||this.pet.work.round.id==="running");
                if(s.typing&&freeHands)g.keyContact=s.boot;
                if(s.typing && !this.pet.action&&!this.pet.game.round&&!this.pet.work.round && this.pet.scene.state!=="running"){
                    const keys=s.boot*(1-(this.pet.scene.state==="working"?.28:0));
                    const phase=this.pet.time*10;
                    g.leftY=mix(g.leftY,8-13*Math.max(0,Math.sin(phase)),keys);
                    g.rightY=mix(g.rightY,10-13*Math.max(0,-Math.sin(phase)),keys);
                    g.gazeX=mix(g.gazeX,.69,keys*.85);g.gazeY=mix(g.gazeY,.45,keys*.82);
                    g.leftX=mix(g.leftX,15,keys);g.rightX=mix(g.rightX,-18,keys);
                }
                if(s.present>0){
                    g.gazeX=mix(g.gazeX,.96-.9*s.meet,s.present);g.gazeY=mix(g.gazeY,.48-.64*s.meet,s.present);
                    g.head+=5*s.present;g.rightY-=30*s.present;g.smile+=.72*s.meet;g.keyContact*=1-s.present;
                }
            }
            if(!s.kind)return;
            const t=s.age;
            if(s.kind==="cheer"){
                const air=s.air/97, salute=beat(1.92,2.22,2.46,2.85,t);
                g.squash+=1.42*s.crouch-.42*air;g.leftX-=32*air;g.rightX+=39*air+14*salute;
                g.leftY-=86*air+14*salute;g.rightY-=105*air+44*salute;
                g.wing+=29*air-9*s.crouch;g.head-=7*air-3*salute;g.nod+=7*s.crouch-9*air;
                g.tailLift-=28*arc(.72,1.67,t);g.tailTip+=26*arc(.8,1.71,t);
                g.gazeY=-.8*air;g.smile+=1.43*air+.91*salute;g.jaw+=1.48*air;g.blush+=.92*(air+salute);
                g.lid*=1-.77*s.crouch;g.lid*=1-.94*air;g.winkL+=.87*salute;
            }else{
                const h=s.puff,look=-h+1.67*s.peek;
                g.gazeX=mix(g.gazeX,look,Math.max(h,s.peek));g.gazeY=mix(g.gazeY,.19,h);
                g.head-=12*h;g.lean-=16*h;g.nod-=6*h;g.leftX+=66*h;g.rightX-=72*h;
                g.leftY-=31*h;g.rightY-=49*h;g.leftWing-=20*h;g.rightWing+=8*h;
                g.tailTip-=36*h;g.tailLift-=14*h;g.squint+=.37*h;g.lid*=1-.38*h;
                g.lid*=1-.55*s.snub;g.head-=4*s.snub;g.leftWing-=6*s.snub;
                g.pout=h;g.blush+=h;g.smile=mix(g.smile,-.32,h);
                if(s.kind==="mend"){
                    const accept=beat(.19,.78,.96,1.62,t),sweet=beat(.89,1.35,1.80,2.33,t);
                    g.rightX+=17*accept;g.rightY-=45*accept;g.head+=6*accept;
                    g.lid*=1-.72*sweet;g.smile+=sweet;g.blush+=.65*sweet;g.nod+=6*sweet;
                    g.jaw+=.53*sweet*(.5+.5*Math.sin(17*t));g.wing+=10*sweet;
                }
            }
        }
    }
    DragonStage.desk=desk;
    window.DragonStage=DragonStage;
})();
