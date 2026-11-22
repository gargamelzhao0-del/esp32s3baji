/* One deformable rest mesh. The browser and the offline motion preview use the same renderer. */
(() => {
    "use strict";
    const W = 569, H = 640;
    const world = {width: 816, height: 720, x: 124, y: 61};
    const columns = 29, rows = 34;

    function smooth(a, b, v) {
        const t = Math.max(0, Math.min(1, (v - a) / (b - a)));
        return t * t * (3 - 2 * t);
    }
    function oval(x, y, cx, cy, rx, ry) {
        const dx = (x - cx) / rx, dy = (y - cy) / ry;
        return Math.exp(-2 * (dx * dx + dy * dy));
    }

    // Weights follow the painted silhouette. The feet stay planted during a breath.
    function skinPoint(x, y) {
        const left = x < 272;
        const edge=left?x:W-1-x;
        // Follow the diagonal of the membrane out to its inked tip.
        const membrane=(1-smooth(103,150,edge))*(1-smooth(25,63,Math.abs(y-(438-edge*.50))));
        const root=left ? oval(x,y,57,412,113,49) : oval(x,y,513,407,111,44);
        const wing=Math.max(root,membrane);
        const trail = smooth(421, 603, y);
        const tailEdge = 465 - 84 * smooth(530, 623, y);
        return {
            u:x, v:y, x:0, y:0, left, wing,
            head: 1 - smooth(314, 395, y),
            upper: 1 - smooth(370, 626, y),
            aboveFeet:1-smooth(525,623,y),
            footL: oval(x,y,232,609,48,89),
            footR: oval(x,y,305,609,48,89),
            cheek: oval(x,y,left?188:351,306,35,27),
            face: oval(x,y,271,281,151,104),
            lock: oval(x,y,left?118:421,374,49,150) * smooth(210,370,y),
            tassel: oval(x,y,411,295,13,52) * smooth(238,312,y),
            hem: oval(x,y,left?139:397,539,96,112),
            tuft: oval(x,y,246,35,62,42),
            tail: smooth(tailEdge-12,tailEdge+36,x) * smooth(412,490,y),
            tailEnd:trail
        };
    }

    function project(n, p, m, out) {
        const x=n.u, y=n.v;
        const hx=x-274, hy=y-339;
        const wingX=x-(n.left?143:422), wingY=y-381;
        const wingSin=n.left?m.leftSin:-m.rightSin, wingCos=n.left?m.leftCos:m.rightCos;
        const headX=(m.headCos-1)*hx - m.headSin*hy;
        const headY=m.headSin*hx + (m.headCos-1)*hy;
        const breath=p.breath, squash=p.squash;
        let dx = p.side + n.upper*p.lean + n.head*(headX + p.headX);
        let dy = -p.jump + n.upper*breath + n.head*(headY + p.nod);
        dx += n.wing*((wingCos-1)*wingX - wingSin*wingY);
        dy += n.wing*(wingSin*wingX + (wingCos-1)*wingY);
        dx += n.lock*p.hair + n.tassel*p.tassel + n.tuft*p.tuft;
        dx += n.hem*(p.hair*0.28 + (n.left?-1:1)*squash*2.1);
        dx += n.tail*(p.tailBend*(1-n.tailEnd) + m.tailTip*n.tailEnd);
        dy += n.tail*p.tailLift*(0.18+0.82*n.tailEnd);
        dx += (x-277)*squash*0.023*n.upper;
        dy += (626-y)*squash*0.025;
        const s=p.stage;
        dx+=s.turn*(n.head*(17+(x-274)*.14)+23*n.face);
        dx+=(n.left?-1:1)*n.cheek*p.pout*12;
        dy+=n.cheek*p.pout*4.5;
        const floor=695;
        out.x=397+(world.x+x+dx-397)*s.scale+s.x+n.footL*s.footLX+n.footR*s.footRX;
        out.y=floor+(world.y+y+dy-floor)*s.scale-s.air+(s.bodyY+30*s.seat)*n.aboveFeet+
              n.footL*s.footLY+n.footR*s.footRY;
        out.x+=s.tilt*(floor-out.y);
        if(p.contact && n.head>.01 && p.press>.005){
            const nx=(out.x-p.contact.x)/85, ny=(out.y-p.contact.y)/72;
            const falloff=Math.max(0,1-nx*nx-ny*ny);
            const weight=n.head*falloff*falloff;
            out.x+=13*p.brushX*weight;
            out.y+=(5*p.press+8*p.brushY)*weight;
        }
    }

    function makeMesh(pixels) {
        const points=[], triangles=[];
        const at=(row,col)=>row*(columns+1)+col;
        for(let row=0;row<=rows;row++) {
            for(let col=0;col<=columns;col++) points.push(skinPoint(col*(W-1)/columns,row*(H-1)/rows));
        }
        for(let row=0;row<rows;row++) {
            for(let col=0;col<columns;col++) {
                const a=at(row,col), b=a+1, c=at(row+1,col), d=c+1;
                let visible=false;
                for(let y=Math.floor(points[a].v);y<=Math.ceil(points[c].v)&&!visible;y++) {
                    for(let x=Math.floor(points[a].u);x<=Math.ceil(points[b].u);x++) {
                        if(pixels[(y*W+x)*4+3]) { visible=true; break; }
                    }
                }
                if(visible) triangles.push(a,b,c,b,d,c);
            }
        }
        const strain=[];
        for(let i=0;i<triangles.length;i+=3){
            const a=points[triangles[i]],b=points[triangles[i+1]],c=points[triangles[i+2]];
            const u=b.u-a.u,v=b.v-a.v,w=c.u-a.u,z=c.v-a.v;
            const det=u*z-v*w;
            strain.push({a,b,c,u,v,w,z,ia:z/det,ib:-w/det,ic:-v/det,id:u/det});
        }
        return {points,triangles,strain};
    }

    // Project an over-strained triangle towards a rigid rotation about its current
    // centre. Neighbours share vertices, so alternate traversal to avoid directional drift.
    function relaxSilhouette(mesh,scale){
        const list=mesh.strain,limit=.22;
        for(let pass=0;pass<10;pass++){
            let worst=0;
            for(let j=0;j<list.length;j++){
                const q=list[pass%2?list.length-1-j:j],{a,b,c}=q;
                const bx=(b.x-a.x)/scale,by=(b.y-a.y)/scale,cx=(c.x-a.x)/scale,cy=(c.y-a.y)/scale;
                const xx=bx*q.ia+cx*q.ic,xy=bx*q.ib+cx*q.id;
                const yx=by*q.ia+cy*q.ic,yy=by*q.ib+cy*q.id;
                const h=xx+yy,k=yx-xy,norm=Math.hypot(h,k);
                const cos=norm>1e-6?h/norm:1,sin=norm>1e-6?k/norm:0;
                const error=(xx-cos)**2+(xy+sin)**2+(yx-sin)**2+(yy-cos)**2;
                if(error<=limit*limit)continue;
                worst=Math.max(worst,error);
                const weight=(1-limit/Math.sqrt(error))*scale;
                const ux=(cos*q.u-sin*q.v-bx)*weight,uy=(sin*q.u+cos*q.v-by)*weight;
                const vx=(cos*q.w-sin*q.z-cx)*weight,vy=(sin*q.w+cos*q.z-cy)*weight;
                const mx=(ux+vx)/3,my=(uy+vy)/3;
                a.x-=mx;a.y-=my;b.x+=ux-mx;b.y+=uy-my;c.x+=vx-mx;c.y+=vy-my;
            }
            if(worst<.24*.24)break;
        }
    }

    // Scan-convert triangles into one transparent frame. Adjacent triangles share
    // edges exactly. Bilinear sampling handles the illustration's translucent edge.
    function paintTriangle(a,b,c,src,dst,stride,height) {
        if(a.y>b.y) [a,b]=[b,a];
        if(b.y>c.y) [b,c]=[c,b];
        if(a.y>b.y) [a,b]=[b,a];
        const abY=b.y-a.y, acY=c.y-a.y, bcY=c.y-b.y;
        const abX=b.x-a.x, acX=c.x-a.x;
        const det=abX*acY-acX*abY;
        if(Math.abs(det)<0.00001) return;
        const duX=((b.u-a.u)*acY-(c.u-a.u)*abY)/det;
        const dvX=((b.v-a.v)*acY-(c.v-a.v)*abY)/det;
        const duY=((c.u-a.u)*abX-(b.u-a.u)*acX)/det;
        const dvY=((c.v-a.v)*abX-(b.v-a.v)*acX)/det;
        const startY=Math.max(0,Math.ceil(a.y-0.5));
        const endY=Math.min(height,Math.ceil(c.y-0.5));
        for(let y=startY;y<endY;y++) {
            const py=y+0.5, dy=py-a.y;
            const longX=a.x+dy*acX/acY;
            const shortX=py<b.y ? a.x+dy*abX/abY : b.x+(py-b.y)*(c.x-b.x)/bcY;
            const x0=Math.max(0,Math.ceil(Math.min(longX,shortX)-0.5));
            const x1=Math.min(stride,Math.ceil(Math.max(longX,shortX)-0.5));
            let u=a.u+(x0+0.5-a.x)*duX+dy*duY;
            let v=a.v+(x0+0.5-a.x)*dvX+dy*dvY;
            let out=(y*stride+x0)*4;
            for(let x=x0;x<x1;x++,u+=duX,v+=dvX,out+=4) {
                const ix=Math.min(W-2,Math.max(0,u|0)), iy=Math.min(H-2,Math.max(0,v|0));
                const top=(iy*W+ix)*4, below=top+W*4;
                const a0=src[top+3], a1=src[top+7], a2=src[below+3], a3=src[below+7];
                const alphaSum=a0+a1+a2+a3;
                if(!alphaSum) continue;
                const fx=u-ix, fy=v-iy;
                let w0=(1-fx)*(1-fy), w1=fx*(1-fy), w2=(1-fx)*fy, w3=fx*fy;
                if(alphaSum!==1020) {
                    w0*=a0; w1*=a1; w2*=a2; w3*=a3;
                    const alpha=w0+w1+w2+w3;
                    if(alpha<0.5) continue;
                    dst[out+3]=alpha;
                    w0/=alpha; w1/=alpha; w2/=alpha; w3/=alpha;
                } else {
                    dst[out+3]=255;
                }
                dst[out]=src[top]*w0+src[top+4]*w1+src[below]*w2+src[below+4]*w3;
                dst[out+1]=src[top+1]*w0+src[top+5]*w1+src[below+1]*w2+src[below+5]*w3;
                dst[out+2]=src[top+2]*w0+src[top+6]*w1+src[below+2]*w2+src[below+6]*w3;
            }
        }
    }

    // Bind a contact to the actual triangle visible on screen, including local folds.
    function bindPoint(x,y) {
        const col=x*columns/(W-1), row=y*rows/(H-1);
        const ix=Math.floor(col), iy=Math.floor(row), u=col-ix, v=row-iy;
        const a=iy*(columns+1)+ix, b=a+1, c=a+columns+1;
        return u+v<1 ? {i:a,j:b,k:c,wi:1-u-v,wj:u,wk:v} :
                       {i:b,j:c+1,k:c,wi:1-v,wj:u+v-1,wk:1-u};
    }
    function readPoint(bind,points,sx,sy,out) {
        const a=points[bind.i], b=points[bind.j], c=points[bind.k];
        out.x=(a.x*bind.wi+b.x*bind.wj+c.x*bind.wk)/sx;
        out.y=(a.y*bind.wi+b.y*bind.wj+c.y*bind.wk)/sy;
    }
    class DragonRig {
        constructor(canvas,image,armArt) {
            this.canvas=canvas;
            const backing=document.createElement("canvas");
            backing.width=W; backing.height=H;
            const art=backing.getContext("2d",{willReadFrequently:true});
            art.drawImage(image,0,0);
            art.drawImage(armArt,0,0,263,98,145,360,263,98);
            this.arms=new window.DragonArms(armArt);
            this.texture=art.getImageData(0,0,W,H).data;
            this.face=new window.DragonFace(image);
            this.mesh=makeMesh(this.texture);
            this.ctx=canvas.getContext("2d",{alpha:true});
            this.frame=this.ctx.createImageData(canvas.width,canvas.height);
            const probe=document.createElement("canvas");probe.width=probe.height=3;
            this.depthProbe=probe.getContext("2d",{willReadFrequently:true});
            this.landmarks={
                mouth:bindPoint(270,320), left:bindPoint(178,427), right:bindPoint(374,427),
                leftShoulder:bindPoint(217,375),rightShoulder:bindPoint(332,375),
                forehead:bindPoint(263,173), fringe:bindPoint(153,192), eyes:bindPoint(270,279), nose:bindPoint(270,305),
                wingL:bindPoint(51,413), wingR:bindPoint(516,410), tail:bindPoint(506,547),
                footL:bindPoint(232,615), footR:bindPoint(305,615)
            };
            this.anchors=Object.fromEntries(Object.keys(this.landmarks).map(k=>[k,{x:0,y:0}]));
            const mask=this.ctx.createLinearGradient(this.canvas.width*.927,0,this.canvas.width,0);
            mask.addColorStop(0,"transparent");mask.addColorStop(1,"#000");this.edge=mask;
        }

        addLandmark(name,x,y){
            this.landmarks[name]=bindPoint(x,y);
            this.anchors[name]={x:0,y:0};
        }

        pin(hand,x,y,amount){
            if(amount<=.003)return;
            const at=this.anchors[hand],w=Math.min(1,amount);
            at.x+=(x-at.x)*w;at.y+=(y-at.y)*w;
        }

        hiddenByDesk(key,at){
            const p=this.rendered,s=p?.stage;
            if(!s || s.desk<.5 || this.arms.hands[key]?.depth==="table")return false;
            if(at.x<s.handle-34 || at.x>s.handle+320 || at.y<380 || at.y>688)return false;
            const c=this.depthProbe;
            c.resetTransform();c.clearRect(0,0,3,3);c.translate(1.5-at.x,1.5-at.y);
            window.DragonStageArt.furniture(c,s,p.time,true);
            return c.getImageData(1,1,1,1).data[3]>150;
        }

        draw(p) {
            const sx=this.canvas.width/world.width, sy=this.canvas.height/world.height;
            const rad=Math.PI/180, headRad=p.head*rad;
            const leftRad=22*Math.tanh((p.wing+p.leftWing)/22)*rad;
            const rightRad=22*Math.tanh((p.wing+p.rightWing)/22)*rad;
            const m={headCos:Math.cos(headRad),headSin:Math.sin(headRad),
                     leftCos:Math.cos(leftRad),leftSin:Math.sin(leftRad),
                     rightCos:Math.cos(rightRad),rightSin:Math.sin(rightRad),tailTip:22*Math.tanh(p.tailTip/22)};
            this.face.update(this.texture,W,p);
            for(const n of this.mesh.points) {
                project(n,p,m,n);
            }
            relaxSilhouette(this.mesh,p.stage.scale);
            for(const n of this.mesh.points){
                n.x*=sx; n.y*=sy;
            }
            const s=p.stage,desk=window.DragonStage.desk;
            const {points,triangles}=this.mesh;
            for(const [name,point] of Object.entries(this.landmarks))readPoint(point,points,sx,sy,this.anchors[name]);
            for(const hand of ["left","right"]){
                this.anchors[hand].x+=s.scale*p[hand+"X"];this.anchors[hand].y+=s.scale*p[hand+"Y"];
            }
            this.pin("right",s.handle,desk.y,s.grip);
            this.pin("right",s.handle+desk.buttonX,desk.y+desk.buttonY-5+4*s.press,s.switchReach);
            if(!s.kind && s.boot>0 && s.desk>.99 && !s.switchReach && p.keyContact>.003){
                const phase=s.keyStroke;
                const accents=p.work?.id==="running"?p.work:null;
                for(const [hand,sign,keyX] of [["left",-1,desk.keyLeft],["right",1,desk.keyRight]]){
                    const hit=accents?.lastSide===sign?Math.min(1,accents.beatGlow/.25):0;
                    this.pin(hand,s.handle+keyX,desk.y-23-12*Math.max(0,sign*phase)+4*hit,
                             p.keyContact);
                }
            }
            if(s.present>0)this.pin("right",s.handle+desk.screenX-65,desk.y+desk.screenY-26,s.present);
            const writing=s.kind?0:Math.min(1,p.pad*s.workMix*(1-s.present));
            if(writing>.003){
                const page=window.DragonWriting.layout(this.anchors.left,p.writing,p.penPark);
                this.pin("right",page.tipX+10,page.tipY-17,writing);
            }
            if(p.action==="feed"){
                const bite=smooth(.68,1.18,p.actionTime)*(1-smooth(1.30,1.65,p.actionTime));
                this.pin("right",this.anchors.mouth.x+8*s.scale,this.anchors.mouth.y+9*s.scale,bite);
            }
            for(const [hand,side] of [["left",-1],["right",1]]){
                // Both hands can follow facial landmarks; a tool's right grip
                // additionally sees the already solved left support.
                const contact=p[hand+"Grip"];
                if(contact){
                    const grip=contact(this.anchors);
                    this.pin(hand,grip.x,grip.y,grip.weight);
                }
                const at=this.anchors[hand];
                const arm=window.DragonArms.solve(this.anchors[hand+"Shoulder"],at,side,s.scale);
                this.arms.hands[hand]=arm;at.x=arm.x;at.y=arm.y;
            }
            const dst=this.frame.data;
            dst.fill(0);
            for(let i=0;i<triangles.length;i+=3) {
                paintTriangle(points[triangles[i]],points[triangles[i+1]],points[triangles[i+2]],
                              this.texture,dst,this.canvas.width,this.canvas.height);
            }
            this.ctx.putImageData(this.frame,0,0);
            this.ctx.save();
            this.ctx.setTransform(sx,0,0,sy,0,0);
            this.arms.draw(this.ctx,p,"body");
            window.drawDragonProps(this.ctx,p,this.anchors,this.arms);
            this.ctx.restore();
            this.rendered=p;
            if(s.desk>.005 && s.handle+314>765){
                this.ctx.save();this.ctx.globalCompositeOperation="destination-out";this.ctx.fillStyle=this.edge;
                this.ctx.fillRect(this.canvas.width*.927,0,this.canvas.width*.073+1,this.canvas.height);this.ctx.restore();
            }
        }
    }
    DragonRig.world=world;
    window.DragonRig=DragonRig;
})();
