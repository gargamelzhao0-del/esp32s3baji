/* The desk is drawn in two depth layers. Its origin is the centre of the brass pull handle. */
(() => {
    "use strict";
    const paths=new Map(), tau=2*Math.PI;
    const rim="#453f4d", gold="#f3cb91", green="#92b8ac";
    function path(c,d,fill,line,width=3){
        let p=paths.get(d);if(!p){p=new Path2D(d);paths.set(d,p);}
        if(fill){c.fillStyle=fill;c.fill(p);}
        if(line){c.strokeStyle=line;c.lineWidth=width;c.stroke(p);}
    }
    function oval(c,x,y,rx,ry,fill,line,width=3){
        c.beginPath();c.ellipse(x,y,rx,ry,0,0,tau);
        c.fillStyle=fill;c.fill();
        if(line){c.strokeStyle=line;c.lineWidth=width;c.stroke();}
    }
    function box(c,x,y,w,h,r,fill,line,width=3){
        c.beginPath();c.roundRect(x,y,w,h,r);
        if(fill){c.fillStyle=fill;c.fill();}
        if(line){c.strokeStyle=line;c.lineWidth=width;c.stroke();}
    }
    function wheel(c,x,y,roll){
        box(c,x-5,y-15,10,15,4,"#cbbda5",rim);
        oval(c,x,y,10,10,"#5a5561",rim);
        c.save();c.translate(x,y);c.rotate(-roll*33);
        oval(c,0,0,5.7,5.7,"#cabca7");path(c,"M-3 0 L3 0",null,"#857a83",2);c.restore();
    }
    function monitor(c,s,t){
        c.save();c.translate(235,-19);c.scale(.89,.89);
        path(c,"M-9 -26 L8 -26 L10 -6 L30 -3 Q35 2 27 4 L-29 4 Q-38 2 -30 -2 L-9 -7 Z","#afa7aa",rim);
        // A hinged display rotates from a thin edge to the full painted face.
        c.save();c.transform(1,0,.18*(1-s.open),.18+.82*s.open,0,-13);
        box(c,-68,-102,132,102,12,"#c69b97",rim,4);
        box(c,-72,-106,132,102,12,"#f3ddbc",rim,4);
        box(c,-63,-96,112,78,7,s.boot>.05?"#294544":"#445052","#847f78",3);
        c.save();c.globalAlpha*=s.boot;
        const x=-52,y=-76;
        if(s.paused){
            box(c,-19,-73,7,26,2,"#f1c996");box(c,-4,-73,7,26,2,"#f1c996");
        }else{
            c.save();c.translate(x,y);path(c,"M0 -4 L8 2 L0 8 M14 8 L24 8",null,"#c3f0d1",4);c.restore();
            const lineCount=1+Math.floor(4*s.boot);
            for(let i=0;i<lineCount-1;i++){
                const length=[45,31,56,24][i];
                box(c,x+4+(i%2)*6,y+17+i*8,length,3.5,1.7,i===1?"#dab4c5":"#91c0ae");
            }
            if(t%1.08<.77)box(c,23,-35,6,5,1,"#f1d3aa");
        }
        c.restore();
        oval(c,44,-11,2.8,2.8,s.boot>.3?"#a6e6bb":"#ab9a9a");
        path(c,"M-57 -10 L-45 -10",null,"#ae8e8d",2.6);
        c.restore();c.restore();
    }
    function plant(c,roll){
        c.save();c.translate(291,-17);
        c.rotate(.10*Math.sin(roll*25)*Math.sin(Math.PI*Math.min(1,roll)));
        path(c,"M0 -11 Q0 -28 -8 -33 M0 -17 Q2 -30 12 -34",null,"#789887",3);
        path(c,"M-3 -21 Q-19 -21 -18 -37 Q-4 -37 -3 -21 Z","#99b6a7","#596e67",2.5);
        path(c,"M2 -23 Q3 -39 18 -39 Q21 -28 2 -23 Z","#bed1a3","#607568",2.5);
        path(c,"M-13 -15 L12 -15 L9 1 Q0 6 -9 1 Z","#d79e91",rim,2.6);
        box(c,-15,-18,29,6,2,"#efb5a4",rim,2.5);c.restore();
    }
    function furniture(c,s,t,front){
        if(s.desk<.008)return;
        const desk=window.DragonStage.desk;
        c.save();c.translate(s.handle,desk.y);c.globalAlpha*=s.desk;c.lineCap="round";c.lineJoin="round";
        if(!front){
            // The stool emerges from under the desk and sits behind her skirt.
            c.save();c.translate(124-93*s.stool,0);
            path(c,"M-45 78 L-54 22",null,rim,10);
            path(c,"M-45 78 L-54 22",null,"#c39e86",5);
            box(c,-66,15,27,31,9,"#ceabb5",rim);
            path(c,"M-28 77 L-38 151 M26 77 L37 151",null,rim,12);
            path(c,"M-28 77 L-38 151 M26 77 L37 151",null,"#c39e86",6);
            box(c,-49,67,94,19,8,"#aa8fa3",rim);
            box(c,-51,62,97,15,7,"#d8b5bd",rim);c.restore();
            path(c,"M35 111 L273 105 L274 113 L34 119 Z","#827e7d",rim,2.5);
            oval(c,130,157,170,9,"rgba(97,138,122,.20)");
        }else{
            path(c,"M35 25 L28 141 L39 141 L49 26 M260 22 L266 140 L277 140 L274 20 Z","#aa9383",rim,3);
            wheel(c,32,146,s.roll);wheel(c,272,145,s.roll);
            path(c,"M207 0 C205 47 252 31 247 57 C242 82 226 72 232 95",null,"#685e6b",3);
            path(c,"M161 19 L278 16 L274 66 Q247 79 165 69 Z","#799c93",rim,3.2);
            path(c,"M164 22 L275 20 L272 52 Q217 58 164 51 Z",green,rim,2.8);
            box(c,201,31,37,6,3,"#edc89e","#686871",2);
            sparkle(c,258,39,5.8,"#e4daba");
            // Wooden top, front edge and drawer are in front of the dress.
            path(c,"M-26 9 L-5 -26 Q-2 -30 8 -30 L280 -30 Q290 -30 294 -23 L311 4 L306 13 L-20 22 Z","#edd4ac",rim,4);
            path(c,"M-26 7 Q-23 11 -17 11 L307 1 L306 13 Q303 17 294 17 L-17 27 Q-27 25 -26 17 Z","#cba283",rim,3.5);
            path(c,"M-14 15 L296 5",null,"#f6dfb7",2.4);
            // Handle contacts the right palm at local (0,0).
            path(c,"M-8 14 L-17 5 Q-23 -4 -14 -7 L9 -7 Q16 -6 16 1 L14 11",null,rim,8);
            path(c,"M-8 14 L-17 5 Q-23 -4 -14 -7 L9 -7 Q16 -6 16 1 L14 11",null,gold,4.8);
            monitor(c,s,t);plant(c,s.roll);
            oval(c,desk.buttonX,desk.buttonY+2,10,5,"#6d6566",rim,2);
            oval(c,desk.buttonX,desk.buttonY-2+4*s.press,9,4.5,s.boot>.5?"#eab6a7":"#dfa298",rim,2);
            if(s.dock>.12){
                c.save();c.globalAlpha*=s.dock;
                path(c,"M11 -14 L133 -16 L147 1 L2 4 Z","#607774",rim,2.9);
                for(let row=0;row<2;row++)for(let col=0;col<9;col++){
                    const active=s.boot>.6&&s.typing&&Math.floor(t*8)%12===col+row;
                    box(c,13+col*13-row*3,-11+row*6,9,3.6,1.4,active?gold:"#b4cabe");
                }
                c.restore();
            }
        }
        c.restore();
    }
    function sparkle(c,x,y,r,color,turn=0){
        c.save();c.translate(x,y);c.rotate(turn);c.scale(r,r);
        path(c,"M0 -1 Q.12 -.16 .74 0 Q.12 .16 0 1 Q-.12 .16 -.74 0 Q-.12 -.16 0 -1 Z",color);
        c.restore();
    }
    function effects(c,s,a){
        if(!s.kind)return;
        const t=s.age;
        c.save();c.lineCap="round";c.lineJoin="round";
        if(s.kind==="cheer"){
            const elapsed=t-.49;
            if(elapsed>0&&elapsed<1.94){
                c.globalAlpha=Math.min(1,(1.94-elapsed)*2.3);
                const colors=["#eabac5","#f9da9a","#a9d5be","#cabddd"];
                for(let i=0;i<14;i++){
                    const sign=i%2?1:-1,j=Math.floor(i/2);
                    const x=392+sign*(137+j*7+(24+j*10)*elapsed);
                    const y=358-((213+j*19)*elapsed)+143*elapsed*elapsed+22*(j%3);
                    c.save();c.translate(x,y);c.rotate(sign*(.31*j+elapsed*(2+j*.33)));
                    if(j%3===0)sparkle(c,0,0,13,colors[i%4]);
                    else if(j===2)path(c,"M-3 -14 Q6 -7 0 0 Q-6 7 3 11",null,colors[i%4],5);
                    else box(c,-3.8,-8,7.6,16,2,colors[i%4]);
                    c.restore();
                }
            }
        }else if(s.kind==="sulk"){
            for(const offset of [.32,1.34,3.61]){
                const f=(t-offset)/.91;if(f<=0||f>=1)continue;
                c.save();c.globalAlpha=.82*Math.sin(Math.PI*f);
                c.translate(a.eyes.x+104+44*f,a.eyes.y-41-46*f);c.scale(.6+.58*f,.6+.58*f);
                path(c,"M-17 11 Q-5 7 -6 0 Q-6 -9 3 -7 Q8 -20 18 -12 Q30 -11 27 -2 Q35 8 24 12 Q15 16 10 10 Q0 15 -17 11 Z","#f3d8da","#a68292",2.8);
                c.restore();
            }
        }else if(s.kind==="mend"&&t<1.42){
            const ease=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
            const take=ease(t/.87),bite=ease((t-.87)/.5);
            const x=(a.right.x+113*(1-take))*(1-bite)+a.mouth.x*bite;
            const y=(a.right.y-42-28*Math.sin(Math.PI*take))*(1-bite)+(a.mouth.y+1)*bite;
            c.translate(x,y);c.scale(1-.75*bite,1-.75*bite);c.rotate(.28-.55*take);
            path(c,"M-11 -6 L-24 -13 L-21 -2 L-25 8 L-11 5 M12 -5 L23 -11 L21 0 L25 9 L12 6 Z","#e9b4c0","#926e80",2.5);
            path(c,"M0 -15 L5 -7 L15 -6 L10 2 L11 13 L1 9 L-8 14 L-9 4 L-16 -3 L-6 -6 Z",gold,"#9c785e",2.8);
        }
        c.restore();
    }
    window.DragonStageArt={furniture,effects};
})();
