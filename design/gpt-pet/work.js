/* Local play uses the observed Agent state. Only the source can change that state. */
(() => {
    "use strict";
    const entries={
        idle:["摇铃","摇摇小铃铛","碰一下铃铛","她听到铃声，两只翅膀一前一后地竖了起来。"],
        working:["点灯","点亮灵感灯","碰亮小灯泡","她望着亮起来的小灯，眼睛也跟着亮了。"],
        reading:["翻页","陪她翻一页","从书角向左划","她按住书脊，顺着你翻开的新一页读下去。"],
        writing:["盖章","盖个梅花爪印","点纸上的印章","啪嗒。她把本子捧起来，看了看新盖的小爪印。"],
        running:["合奏","一起敲三拍","点亮起的琴键","最后一拍合上了。她用尾巴补了一记轻轻的尾音。"],
        searching:["寻星","找镜边的星屑","点那颗小星星","找到了。她把镜片往那一点星光挪了挪。"],
        compact:["折纸","一起折纸飞机","点折起的纸角","她把纸角压平，目送小飞机飞过肩头。"],
        waiting:["回应","碰一碰等你的手","碰她举起的手","她点点头，把信抱在怀里，陪你等电脑上的确认。"],
        error:["贴贴","给她贴枚星星贴","碰星星贴纸","她摸摸贴好的星星，绷着的肩膀慢慢松开了。"],
        limited:["递茶","递一杯暖茶","碰杯子上的热气","她捧住小茶杯，吹了吹热气，安静歇一会儿。"],
        ended:["收本","和她合上手帐","点手帐的封角","她把手帐轻轻合起来，抚平书封上的丝带。"],
        succeeded:["庆祝","拉响庆祝礼花","点纸筒的小拉绳","她踮起脚，在落下的彩纸里接住一片金色的。"],
        cancelled:["收笔","把笔放回笔袋","碰羽毛笔","她把羽毛笔收好，把没写完的那页留了下来。"],
        offline:["吹风","吹转纸风车","拨一下纸风车","风车转起来了。她鼓起脸颊，又补了一口气。"]
    };
    const catalog=Object.fromEntries(Object.entries(entries).map(([id,[short,name,cue,done]])=>[id,{id,short,name,cue,done}]));
    const clamp=n=>Math.max(0,Math.min(1,n));
    function ease(a,b,x){const t=clamp((x-a)/(b-a));return t*t*(3-2*t);}
    const pulse=(a,b,c,t)=>ease(a,b,t)*(1-ease(b,c,t));
    function target(id,a,r) {
        const cx=(a.left.x+a.right.x)/2, cy=(a.left.y+a.right.y)/2;
        switch(id){
            case "reading":return {x:cx+51,y:cy+9};
            case "writing":return {x:cx+14,y:cy+32};
            case "running":return {x:cx+[-46,46,-46][Math.min(2,r.beats||0)],y:cy+22};
            case "searching":{
                const gather=r.phase==="done"?ease(0,.73,r.age):0;
                return {x:a.right.x+12+(56+12*Math.sin(r.age*1.35))*(1-gather),
                        y:a.right.y-31-(44+9*Math.sin(r.age*1.9))*(1-gather)};
            }
            case "waiting":return {x:a.left.x+12,y:a.left.y-19};
            case "ended":case "compact":return {x:cx+12,y:cy+4};
            case "error":return {x:a.left.x-29,y:a.left.y-29};
            case "working":return {x:a.right.x+27,y:a.right.y-79};
            default:return {x:a.right.x+14,y:a.right.y-26};
        }
    }
    class DragonWork {
        constructor(pet){this.pet=pet;this.round=null;this.timer=0;}
        get id(){return this.pet.scene.offline?"offline":this.pet.scene.state;}
        get item(){return catalog[this.id]||catalog.idle;}
        clear(){
            clearTimeout(this.timer);
            if(this.round){this.round=null;this.pet.onWork(null);}
        }
        end(){
            this.clear();this.pet.quiet=0;this.pet.note(this.pet.baseNote());
            this.redraw();
        }
        redraw(){
            if(this.pet.scene.active && this.pet.scene.reduced)this.pet.advance(0,true);
        }
        start(){
            const p=this.pet;
            if(!p.scene.active||!p.rig)return;
            const moving=p.pose?.stage?.setup || p.stage?.mood;
            p.stage?.interrupt();
            this.clear();p.game.clear();clearTimeout(p.stillTimer);
            p.action=p.nextCare=p.heldStar=p.attention=null;
            p.quiet=0;p.slept=false;
            if(moving&&!p.scene.reduced)p.advance(0,true);
            this.round={id:this.item.id,phase:"offer",age:0,beats:0,progress:0,drag:null,returning:false,miss:0,beatGlow:0,lastSide:-1};
            this.changed();
            p.note(this.item.name+"。"+this.item.cue+"。");
        }
        changed(){this.pet.onWork(this.round);this.redraw();}
        cue(){
            const r=this.round;
            if(!r)return "";
            if(r.phase==="done")return {reading:"翻到下一页了",running:"三拍合上了",waiting:"她收到啦",writing:"小爪印盖好了"}[r.id]||"她收到啦";
            return r.id==="running"?(r.beats+1)+"/3 · "+(r.beats===1?"敲右边":"敲左边"):this.item.cue;
        }
        point(){return target(this.id,this.pet.rig.anchors,this.round||{age:0,beats:0});}
        hit(point){
            if(!point)return false;
            const a=this.point();
            const radius=this.id==="running"?Math.min(44,point.hitRadius||42):Math.min(66,point.hitRadius||49);
            return Math.hypot(a.x-point.worldX,a.y-point.worldY)<=radius;
        }
        direct(point){
            return !this.round&&!this.pet.action&&!this.pet.game.round&&!this.pet.stage?.mood&&!this.pet.pose?.stage?.setup&&
                ["reading","writing","running","waiting"].includes(this.id)&&this.hit(point);
        }
        down(point){
            const r=this.round;
            if(r?.id==="reading" && r.phase==="offer" && this.hit(point)){
                r.returning=false;r.drag={x:point.worldX,from:r.progress,moved:false};
            }
        }
        move(point){
            const r=this.round;
            if(!r?.drag||!point)return;
            const distance=r.drag.x-point.worldX;
            if(Math.abs(distance)>13)r.drag.moved=true;
            r.progress=clamp(r.drag.from+distance/124);
            if(this.pet.scene.reduced && r.drag.moved)r.progress=0;
        }
        cancelDrag(){
            const r=this.round;if(!r?.drag)return;
            r.drag=null;r.returning=r.progress>0;
        }
        release(point,travel=0){
            const r=this.round;
            if(!r || r.phase!=="offer" || !point)return;
            if(r.drag){
                this.move(point);
                const drag=r.drag;this.cancelDrag();
                if(drag.moved && drag.x-point.worldX>=70){this.finish();return;}
                if(drag.moved || travel>14)return;
            }
            if(travel>18 && r.id!=="offline")return;
            if(!this.hit(point)){
                r.miss=.32;
                if(r.id==="running")this.pet.note(r.beats===1?"她留住拍子，等你敲右边的亮键。":"她留住拍子，等你敲左边的亮键。");
                this.redraw();return;
            }
            if(r.id==="running"){
                r.lastSide=r.beats===1?1:-1;r.beats++;r.beatGlow=.39;r.miss=0;
                this.pet.onTone?.(["beatLeft","beatRight","beatEnd"][r.beats-1]);
                if(r.beats<3){this.changed();this.pet.note(this.cue()+"。");return;}
            }
            this.finish();
        }
        key(key){
            const r=this.round;if(!r)return false;
            if(key==="Escape"){this.end();return true;}
            const arrow=key==="ArrowLeft"||key==="ArrowRight";
            if(key!==" "&&key!=="Enter"&&!(arrow&&r.id==="running"))return false;
            const a=this.point(), p={worldX:a.x,worldY:a.y};
            if(arrow && (key==="ArrowRight")!==(r.beats===1))p.worldX+=(r.beats===1?-92:92);
            this.release(p);return true;
        }
        finish(){
            const r=this.round;if(!r||r.phase!=="offer")return;
            r.startProgress=r.progress;r.phase="done";r.age=0;r.drag=null;r.returning=false;
            if(r.id!=="running")this.pet.onTone?.(r.id);
            this.pet.reward();this.pet.note(this.item.done);this.changed();
            if(this.pet.scene.reduced)this.timer=setTimeout(()=>this.end(),1650);
        }
        tick(dt){
            const r=this.round;if(!r||!dt||this.pet.scene.reduced)return;
            r.age+=dt;r.miss=Math.max(0,r.miss-dt);r.beatGlow=Math.max(0,r.beatGlow-dt);
            if(r.returning){
                r.progress*=Math.exp(-dt*15);
                if(r.progress<.008){r.progress=0;r.returning=false;}
            }
            if(r.phase==="done"){
                r.progress=r.startProgress+(1-r.startProgress)*ease(0,.84,r.age);
                if(r.age>=2.05)this.end();
            }
        }
        pose(g){
            const r=this.round;if(!r)return;
            const t=this.pet.scene.reduced && r.phase==="done" ? .60 : r.age;
            const a=this.point(), eye=this.pet.rig.anchors.eyes;
            const act=r.phase==="done"?pulse(0,.27,1.72,t):0;
            g.gazeX=Math.max(-1,Math.min(1,(a.x-eye.x)/153));
            g.gazeY=Math.max(-1,Math.min(1,(a.y-eye.y)/165));
            g.head+=g.gazeX*2.8;g.smile+=.27*act;g.blush+=.44*act;
            switch(r.id){
                case "reading":{
                    const turn=this.pet.scene.reduced && r.phase==="done" ? .62 : r.progress;
                    g.gazeX=.75-1.45*turn;g.rightX-=24*Math.sin(Math.PI*turn);
                    g.rightY-=18*Math.sin(Math.PI*turn);g.nod+=5*act;break;
                }
                case "writing":g.rightY+=15*act-12*pulse(0,.12,.27,t)*act;g.nod+=6*act;g.winkL+=.58*act;break;
                case "running":{
                    const beat=Math.min(1,r.beatGlow/.28);
                    g[r.lastSide<0?"leftY":"rightY"]+=19*beat;
                    g.nod+=5*beat;g.tailTip+=19*Math.sin(t*8)*act;g.wing+=10*act;break;
                }
                case "searching":g.rightX+=13*act;g.rightY-=13*act;g.winkL+=.88*act;g.light+=.65*act;break;
                case "waiting":g.leftX+=19*act;g.leftY-=7*act;g.nod+=9*act;g.lid*=1-.36*act;break;
                case "limited":case "error":g.lid*=1-.70*act;g.nod-=5*act;g.wing+=8*act;g.rightY-=21*act;break;
                case "succeeded":case "compact":g.jump+=19*act;g.wing+=17*act;g.rightY-=26*act;g.gazeY-=1.4*act;break;
                case "offline":g.jaw+=.62*act;g.headX+=6*act;g.tailBend+=12*act;break;
                case "idle":g.head-=7*act;g.leftWing+=19*act;g.rightWing+=13*pulse(.23,.61,1.52,t)*act;break;
                case "working":g.light+=act;g.jaw+=.46*act;g.rightY-=17*act;g.gazeY-=.75*act;break;
                default:g.nod+=7*act;g.leftX+=12*act;g.rightX-=11*act;g.lid*=1-.37*act;
            }
        }
    }
    DragonWork.catalog=catalog;DragonWork.target=target;
    window.DragonWork=DragonWork;
})();
