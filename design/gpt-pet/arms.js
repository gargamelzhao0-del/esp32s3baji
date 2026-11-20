/* The body, sleeves, instruments and gripping thumbs have separate depth passes.
   Distances are in portrait pixels until the final uniform stage transform. */
(() => {
    "use strict";
    const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));

    function solve(shoulder,wrist,side,scale){
        let dx=(wrist.x-shoulder.x)/scale,dy=(wrist.y-shoulder.y)/scale;
        const distance=Math.hypot(dx,dy),reach=139;
        if(distance>reach){dx*=reach/distance;dy*=reach/distance;}
        const length=Math.hypot(dx,dy),ux=length>1?dx/length:side,uy=length>1?dy/length:0;
        // The elbow projects outward in a fold, then aligns with the wrist at full reach.
        const bend=side*Math.min(24,Math.sqrt(Math.max(0,reach*reach-length*length))*.19);
        return {sx:shoulder.x,sy:shoulder.y,ex:shoulder.x+(dx*.48+uy*bend)*scale,
                ey:shoulder.y+(dy*.48-ux*bend)*scale,x:shoulder.x+dx*scale,y:shoulder.y+dy*scale,side,scale};
    }
    function sleeve(c,a,atlas){
        const {sx,sy,ex,ey,x,y,scale,side}=a;
        const dx=(x-sx)/scale,dy=(y-sy)/scale,bx=(ex-sx)/scale,by=(ey-sy)/scale;
        const len=Math.max(1,Math.hypot(dx-bx,dy-by)),nx=-(dy-by)/len,ny=(dx-bx)/len;
        const root=Math.max(1,Math.hypot(bx,by)),rx=-by/root,ry=bx/root;
        c.save();c.translate(sx,sy);c.scale(scale,scale);
        c.beginPath();c.moveTo(rx*12,ry*12);
        c.quadraticCurveTo(bx+rx*16,by+ry*16,bx+nx*14,by+ny*14);
        c.quadraticCurveTo(dx+nx*19,dy+ny*19,dx,dy+3);
        c.quadraticCurveTo(dx-nx*19,dy-ny*19,bx-nx*15,by-ny*15);
        c.quadraticCurveTo(bx-rx*16,by-ry*16,-rx*12,-ry*12);
        c.quadraticCurveTo(-bx*.21,-by*.21,rx*12,ry*12);c.closePath();
        const silk=c.createLinearGradient(bx-rx*15,by-ry*15,bx+rx*15+.01,by+ry*15);
        silk.addColorStop(0,"#c3b6ce");silk.addColorStop(.36,"#f4edf6");silk.addColorStop(.64,"#fffaf8");silk.addColorStop(1,"#d5c9de");
        c.fillStyle=silk;c.fill();c.strokeStyle="#786a7c";c.lineWidth=1.9;c.stroke();
        c.beginPath();c.moveTo(bx-rx*10,by-ry*10);
        c.quadraticCurveTo(bx+4,by+4,(dx+bx)*.5,(dy+by)*.5);c.strokeStyle="#c4b9d1";c.stroke();
        c.translate(dx,dy);
        const mirror=-side;
        c.scale(mirror,1);
        const reach=clamp((Math.hypot(dx,dy)-60)/46);
        c.rotate(Math.atan2(dy-by*.53*reach,(dx-bx*.53*reach)*mirror)-Math.atan2(55,-29));
        c.drawImage(atlas,120,100,66,75,-20,-62,66,75);
        c.restore();
    }
    function palm(c,a,open,front,atlas){
        c.save();c.translate(a.x,a.y);c.scale(-a.side*a.scale,a.scale);
        const tilt=clamp((a.x-a.ex)*a.side/(75*a.scale),-.7,.8)*.23;
        c.rotate(tilt);
        if(front){
            c.beginPath();c.moveTo(13,1);c.quadraticCurveTo(20,-7,14,-11);
            c.quadraticCurveTo(10,-14,7,-8);c.lineTo(1,-2);c.quadraticCurveTo(-1,5,6,4);c.closePath();
            c.fillStyle="#f9e9df";c.fill();c.lineWidth=1.6;c.strokeStyle="#94747a";c.stroke();
        }else{
            const change=clamp((open-.36)/.30);
            if(change<1){c.save();c.globalAlpha*=1-change;c.drawImage(atlas,0,103,48,49,-24,-22,48,49);c.restore();}
            if(change>0){
                c.globalAlpha*=change;c.rotate(.62*open);
                c.drawImage(atlas,55,103,55,51,-27,-23,55,51);
            }
        }
        c.restore();
    }
    class DragonArms{
        constructor(atlas){this.atlas=atlas;this.hands={};}
        draw(c,p,pass){
            const s=p.stage,t=p.actionTime;
            for(const hand of ["left","right"]){
                const a=this.hands[hand],right=hand==="right",selected=p.round?.side===a.side;
                const offering=p.round?.kind==="five" && selected;
                const keyboard=!s.kind && p.keyContact>.06;
                const tool=!s.kind && s.workMix>.05 && (p.book>.10 || p.pad>.10 || (right && p.lens>.10));
                const furniture=keyboard || (right && Math.max(s.grip,s.switchReach,s.present)>.04);
                const treat=(p.action==="feed" && right && t>.52 && t<1.36) ||
                    (p.action==="play" && a.side===(p.cast?.side??1) && t>=1.38 && t<2.13);
                let open=0;
                if(offering)open=clamp(p.round.phase==="offer"?p.round.age/.27:1-p.round.age*.63);
                else if(selected && p.round?.kind==="fly")open=.9;
                else if(!right && p.work?.id==="waiting")open=.88;
                const depth=s.desk>.005 && (tool||furniture||treat)?"table":"body";
                a.depth=depth;
                if(pass===depth){
                    sleeve(c,a,this.atlas);palm(c,a,open,false,this.atlas);
                }else if(pass==="grip" && (tool||treat)){
                    const wrap=!(tool && (p.book>.15 || (!right && p.pad>.15)));
                    palm(c,a,0,wrap,this.atlas);
                }
            }
        }
    }
    window.DragonArms=Object.assign(DragonArms,{solve});
})();
