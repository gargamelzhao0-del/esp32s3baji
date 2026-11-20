/* Draw the same little objects in the pocket, on the desk and on the moving rig. */
(() => {
    "use strict";
    const tau=Math.PI*2, ink="#53475d", paper="#fff0cf", mint="#a7d6c1", gold="#f5c56e", rose="#e9a7b2";
    const paths=new Map();
    function path(c,d,fill,stroke=ink,width=2.6){
        if(!paths.has(d))paths.set(d,new Path2D(d));
        if(fill){c.fillStyle=fill;c.fill(paths.get(d));}
        if(stroke){c.strokeStyle=stroke;c.lineWidth=width;c.stroke(paths.get(d));}
    }
    function dot(c,x,y,r,color){
        c.beginPath();c.arc(x,y,r,0,tau);c.fillStyle=color;c.fill();
    }
    function glint(c,x,y,size,color=paper){
        c.save();c.translate(x,y);c.scale(size,size);
        path(c,"M0 -1 Q.13 -.13 .8 0 Q.12 .14 0 1 Q-.13 .14 -.8 0 Q-.12 -.14 0 -1Z",color,null);c.restore();
    }
    function paw(c){
        path(c,"M-8 8 Q-6 -1 -1 -2 Q3 -4 7 1 Q16 9 9 13 Q5 15 1 12 Q-8 17 -8 8Z","#a8667d",null);
        for(const [x,y,r] of [[-11,-1,3.2],[-6,-8,3.5],[2,-10,3.4],[9,-5,3.1]])dot(c,x,y,r,"#a8667d");
    }
    function blades(c,angle){
        c.save();c.rotate(angle);
        for(const color of [mint,rose,gold,"#beb4da"]){path(c,"M0 0 L-6 -20 L11 -16 L8 -7Z",color);c.rotate(tau/4);}
        dot(c,0,0,3.3,paper);c.restore();
    }
    const art={
        cart(c){
            path(c,"M-18 4 L20 4 L18 9 L-19 9Z","#efce9f");
            path(c,"M-15 10 L-15 21 M15 10 L15 21",null,mint,3.8);
            dot(c,-15,22,3.4,"#cfbac2");dot(c,15,22,3.4,"#cfbac2");
            path(c,"M-6 -21 L18 -21 L18 -5 L-6 -5 Z",paper);
            path(c,"M-3 -17 L15 -17 L15 -9 L-3 -9Z","#618d83",null);
            path(c,"M4 -4 L4 1 L10 1 M-16 -2 L-7 -2",null,gold,2.8);
        },
        tail(c){
            path(c,"M-19 4 Q-19 20 -1 19 Q20 18 21 -4 Q22 -17 15 -22 Q14 -2 0 0 Q-10 2 -15 -10 Q-20 -6 -19 4Z","#d7cde2");
            path(c,"M-17 7 Q-10 11 -5 6 Q-1 11 4 6 Q9 8 13 0 M-11 13 Q-7 17 -1 12",null,"#ac9cb7",2);
            glint(c,-12,-16,5,gold);
        },
        candy(c){
            path(c,"M-9 -5 L-23 -12 L-20 0 L-23 9 L-9 5 M10 -5 L23 -12 L20 0 L23 9 L10 5Z",rose);
            path(c,"M0 -16 L6 -6 L16 -5 L10 3 L11 14 L1 9 L-9 15 L-9 5 L-17 -4 L-6 -6Z",gold);
            glint(c,-3,-4,4,paper);
        },
        pocket(c){path(c,"M-20 -11 Q0 -6 20 -11 L18 12 Q14 22 0 24 Q-14 21 -18 12Z","#ab9ab7");
            path(c,"M-20 -12 Q0 -18 20 -12 L18 -4 Q0 2 -18 -4Z","#ded0db");glint(c,1,6,7,paper);},
        idle(c){path(c,"M-4 -16 Q-7 -25 0 -25 Q7 -25 4 -16",null,gold,3.3);
            path(c,"M-16 8 Q-12 2 -12 -4 Q-11 -16 0 -16 Q13 -16 14 -3 Q14 4 18 8 L19 11 L-18 11Z",gold);
            dot(c,1,15,4.5,"#9c7561");path(c,"M-8 -6 Q-8 -10 -4 -11",null,paper);},
        working(c){path(c,"M-7 9 Q-8 5 -13 0 Q-21 -16 -6 -22 Q7 -28 16 -15 Q21 -5 11 6 L8 11Z","#ffda89");
            path(c,"M-8 9 L10 9 L7 18 L-5 18Z","#a3aba6");path(c,"M-5 13 L8 13 M-3 -6 L1 3 L6 -7",null,"#a78259",2);},
        reading(c){path(c,"M-23 -14 Q-11 -18 0 -10 Q12 -19 23 -15 L22 13 Q12 9 0 18 Q-12 10 -22 14Z","#99c5b6");
            path(c,"M-19 -15 Q-9 -17 0 -9 L0 13 Q-10 7 -18 10Z M1 -9 Q11 -17 19 -15 L18 9 Q10 7 1 13Z",paper);
            path(c,"M10 -13 L14 -14 L13 -4 L11 -6 L9 -3Z",rose,null);},
        writing(c){path(c,"M-10 4 Q-5 0 -7 -7 Q-14 -21 0 -22 Q14 -21 8 -8 Q5 -2 10 4Z",rose);
            path(c,"M-18 5 Q0 2 18 5 L19 14 L-19 14Z",gold);path(c,"M-19 14 L19 14 L17 19 L-18 19Z","#ad8693");},
        running(c){path(c,"M-18 -11 L15 -11 L22 12 L-22 12Z",mint);
            path(c,"M-11 -6 L-5 -6 M1 -6 L8 -6 M-13 1 L-7 1 M-2 1 L5 1 M-8 7 L10 7",null,ink,3.4);},
        searching(c){path(c,"M1 -2 L17 16 Q20 20 16 22 Q13 23 11 20 L-4 1Z",gold);
            dot(c,-6,-6,14.5,ink);dot(c,-6,-6,11,"#a1d2cf");glint(c,-6,-7,8,paper);},
        waiting(c){path(c,"M-22 -12 Q-22 -15 -19 -15 L19 -15 Q22 -15 22 -12 L21 14 L-21 14Z",paper);
            path(c,"M-20 -13 L0 2 L20 -13 M-18 12 L-6 1 M18 12 L6 1",null,"#ae8d80",2);dot(c,0,3,5.4,rose);},
        error(c){c.rotate(-.48);path(c,"M-16 -9 L16 -9 Q24 -9 24 0 Q24 10 16 10 L-16 10 Q-24 10 -24 0 Q-24 -9 -16 -9Z","#e9c4aa");
            path(c,"M-7 -7 L8 -7 L8 8 L-7 8Z",rose,null);glint(c,0,0,5,paper);
            for(const x of [-17,16])for(const y of [-3,4])dot(c,x,y,1.4,ink);},
        limited(c){path(c,"M12 -7 Q27 -13 24 0 Q23 8 12 6",null,"#dbbd91",4.8);
            path(c,"M-18 -9 Q-2 -5 16 -9 L13 8 Q10 17 -3 17 Q-15 16 -17 6Z",mint);
            path(c,"M-16 -8 Q-3 -14 15 -9 Q0 -2 -16 -8Z","#aa795f");glint(c,-5,3,5,paper);},
        compact(c){path(c,"M-23 -3 L24 -18 L7 19 L0 7 L-8 10 L-7 2Z","#c5dcd4");
            path(c,"M-7 2 L24 -18 L0 7 L-8 10Z",paper);},
        ended(c){path(c,"M-18 -17 L13 -20 Q19 -20 20 -14 L20 15 L-14 21 Q-19 20 -19 16Z","#aa99ba");
            path(c,"M-13 14 L18 9 L19 15 L-13 20Z",paper);path(c,"M6 -18 L11 -19 L12 8 L8 4 L6 8Z",mint,null);},
        cancelled(c){path(c,"M-18 19 Q-4 3 3 -15 Q14 -28 20 -22 Q23 -13 13 -4 L9 -3 L11 -1 Q3 4 -5 5Z","#d6cae9");
            path(c,"M-20 22 Q0 -1 16 -18",null,ink,2);},
        succeeded(c){path(c,"M-19 21 L-9 -12 L14 5Z",gold);path(c,"M-15 6 L0 12 L5 9 L-13 0Z",rose,null);
            path(c,"M-8 -13 Q5 -14 14 4 Q6 7 -8 -7Z","#a97c88");glint(c,9,-15,7,paper);},
        offline(c){path(c,"M0 -2 L0 24",null,"#ac8b70",4);blades(c,0);},
        bow(c){path(c,"M-3 -2 Q-15 -20 -29 -15 L-27 9 Q-19 15 -3 5 M3 -2 Q16 -18 27 -10 L24 14 Q14 16 3 5",rose);
            path(c,"M-4 4 L-9 22 L-14 16 L-20 17 L-13 3 M3 4 L11 22 L14 15 L20 16 L13 3","#d5869b");
            path(c,"M-5 -5 Q0 -9 5 -4 L5 6 Q-1 10 -6 4Z","#f3c5c5");},
        moon(c){path(c,"M4 -22 Q-14 -17 -11 -1 Q-7 15 13 12 Q8 24 -7 20 Q-26 15 -24 -3 Q-21 -22 4 -22Z",gold);
            glint(c,10,-9,10,paper);path(c,"M15 8 Q22 16 16 23",null,"#b79b76",2);dot(c,16,24,3.4,rose);},
        leaf(c){path(c,"M-3 14 Q-22 9 -24 -13 Q-8 -17 -1 -1 Q7 -18 25 -18 Q26 3 6 9 L0 17Z",mint);
            path(c,"M-15 -7 L0 13 L17 -11",null,"#71998a",2.4);},
        plain(c){path(c,"M-14 15 L15 -14",null,"#b6aaa8",3);},
    };
    function icon(c,id,x,y,scale=1,angle=0){
        if(!art[id])return;c.save();c.translate(x,y);c.rotate(angle);c.scale(scale,scale);
        c.lineJoin="round";c.lineCap="round";art[id](c);c.restore();
    }
    function work(c,p,a){
        const r=p.work;
        if(!r)return;
        const pos=window.DragonWork.target(r.id,a,r);
        const done=r.phase==="done", t=p.still && done ? .62 : r.age;
        const life=done?Math.max(0,1-Math.max(0,t-1.52)/.53):1;
        const kick=done?Math.sin(Math.min(1,t/.85)*Math.PI):0;
        c.save();c.globalAlpha=life;
        if(!done && !r.passive){
            c.save();c.translate(pos.x,pos.y);c.strokeStyle=r.miss>0?"#efb3b3":"#ebcd8c";c.lineWidth=3.8;
            const rad=(r.id==="running"?24:31)+1.8*Math.sin(t*3);
            c.shadowColor="#494338";c.shadowBlur=2;
            for(const start of [-1.18,1.96]){c.beginPath();c.arc(0,0,rad,start,start+1.18);c.stroke();}
            c.restore();
        }
        switch(r.id){
            case "reading":
                if(!done&&!(r.progress>.05))glint(c,pos.x+9,pos.y-6,8,gold);
                break;
            case "running":{
                const mid=(a.left.x+a.right.x)/2;
                for(const side of [-1,1]){
                    const x=mid+side*46, lit=!done&&side===(r.beats===1?1:-1);
                    c.fillStyle=lit?gold:mint;c.strokeStyle=ink;c.lineWidth=2.6;
                    c.beginPath();c.roundRect(x-12,pos.y-10,24,17,4);c.fill();c.stroke();
                    if(lit)glint(c,x,pos.y-2,5,paper);
                }
                if(r.beatGlow>0){const x=mid+r.lastSide*46;c.fillStyle=gold;c.font="bold 25px Georgia";c.fillText("♪",x+3,pos.y-33+(r.beatGlow/.39)*12);}
                break;
            }
            case "writing":
                if(done&&t>.26){c.save();c.translate(pos.x,pos.y);c.scale(.80,.80);paw(c);c.restore();}
                if(!done||t<.84)icon(c,"writing",pos.x+7,pos.y-17+(done?15*Math.sin(Math.min(1,t/.55)*Math.PI):0),.83,-.12);break;
            case "searching":
                glint(c,pos.x,pos.y,done?16:12,gold);glint(c,pos.x+15,pos.y-13,5,paper);
                if(done){c.strokeStyle=mint;c.lineWidth=3;c.beginPath();c.arc(pos.x,pos.y,18+12*kick,0,tau);c.stroke();}break;
            case "succeeded":
                icon(c,r.id,pos.x,pos.y,1,.18*kick);
                if(done)for(let i=0;i<13;i++){
                    const u=Math.min(t,1.64),dx=(i%5-2)*34+Math.sin(i*2.6)*21;
                    const x=pos.x+dx*u,y=pos.y-(80+i%4*27)*u+70*u*u;
                    c.save();c.translate(x,y);c.rotate(i+u*3);c.fillStyle=[rose,mint,gold][i%3];c.fillRect(-3,-5,6,10);c.restore();
                }break;
            case "compact":icon(c,r.id,pos.x+100*Math.min(1,done?t/1.3:0),pos.y-70*kick,1,-.25*kick);break;
            case "idle":icon(c,r.id,pos.x,pos.y,1.1,done?Math.sin(t*15)*Math.exp(-2*t)*.90:.06*Math.sin(t*2));break;
            case "offline":
                c.save();c.translate(pos.x,pos.y);path(c,"M0 0 L0 29",null,"#aa8d72",4);
                blades(c,done?(1-Math.exp(-2*t))*12:0);c.restore();break;
            case "limited":
                icon(c,r.id,pos.x,pos.y-5*kick,1.14);
                for(let i=0;i<2;i++){const lift=(t*12+i*8)%19;c.save();c.globalAlpha=life*(.75-lift*.026);c.translate(pos.x-7+i*12,pos.y-15-lift);path(c,"M0 0 Q-6 -5 0 -11 Q5 -16 1 -20",null,"#eee3d2",2.8);c.restore();}break;
            case "waiting":
                icon(c,r.id,pos.x+16*kick,pos.y+5*kick,1.13,-.13*kick);
                if(done&&t<.98)glint(c,pos.x+24,pos.y-17-12*kick,8*kick,gold);break;
            case "error":{
                const stick=done?Math.min(1,t/.63):0;
                icon(c,r.id,pos.x+35*stick,pos.y+42*stick-15*kick,1.11,-.18*stick);
                if(done&&t>.43&&t<1.05)glint(c,pos.x+49,pos.y+21,7*kick,gold);break;
            }
            case "ended":{
                c.save();c.translate(pos.x,pos.y);c.scale(1.28,1.28);
                const shut=done?Math.min(1,t/.75):0;
                path(c,"M-22 -11 L0 -6 L21 -13 L21 15 L0 21 L-22 14Z","#a68eae");
                path(c,"M0 -6 Q11 -15 21 -13 L20 12 Q10 12 0 18Z",paper);
                c.save();c.transform(1-1.96*shut,-.16*Math.sin(shut*Math.PI),0,1,0,0);
                path(c,"M0 -6 Q-12 -15 -22 -13 L-21 14 Q-13 11 0 18Z",shut>.52?"#bcabca":paper);
                c.restore();c.restore();break;
            }
            case "cancelled":{
                const packed=done?Math.min(1,t/.78):0;
                icon(c,r.id,pos.x+15*(1-packed),pos.y-22+24*packed,.90,-.2*packed);
                c.save();c.translate(pos.x,pos.y+11);
                path(c,"M-17 -6 L17 -9 L15 12 L-15 16Z",mint);
                path(c,"M-15 -3 L15 -6",null,paper,2);c.restore();break;
            }
            default:icon(c,r.id,pos.x,pos.y-9*kick,1.07,kick*.16);
                if(done&&r.id==="working")for(let i=0;i<5;i++){const ang=-Math.PI+i*Math.PI/4;
                    glint(c,pos.x+Math.cos(ang)*(31+11*kick),pos.y+Math.sin(ang)*(32+14*kick),4+3*kick,gold);}
        }
        c.restore();
    }
    function cranes(helpers,t,a){
        const places=[];
        for(let i=0;i<Math.min(2,helpers);i++){
            const side=i?1:-1;
            places.push({x:a.eyes.x+side*(200+10*Math.sin(t*1.8+i)),y:a.eyes.y-99+13*Math.sin(t*2.3+i*2)});
        }
        return places;
    }
    function flock(c,p,a){
        if(!p.helpers)return;
        cranes(p.helpers,p.time,a).forEach((spot,i)=>{
            const response=p.craneIndex===i?Math.max(0,1-p.craneAge/1.3):0;
            c.save();c.translate(spot.x,spot.y-response*14);c.scale(i?-1.14:1.14,1.14);
            c.rotate(.10*Math.sin(p.time*2+i)+response*.32);
            const flap=Math.sin(p.time*5+i)*.26+response*.32;
            path(c,"M-22 4 L-7 -1 L2 -4 L11 -15 L17 -16 L22 -11 L16 -11 L12 -2 L2 9 L-9 9Z","#d2e3d6");
            c.save();c.rotate(flap);path(c,"M-8 4 L-17 -22 L5 -3 L1 6Z",i?"#e4b9c8":"#b8acce");c.restore();
            if(response>0)glint(c,20,-22,7*response,gold);
            c.restore();
        });
    }
    window.DragonArt={icon,work,cranes,flock};
})();
