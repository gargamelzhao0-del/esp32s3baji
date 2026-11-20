/* Replay the production renderer at its 30 Hz design rate with a fixed seed.
   No fixture uses real Agent sessions, network access or persistent pet progress. */
function on(frame,callback){return (pet,i)=>{if(i===frame)callback(pet);};}
function care(kind,side){return on(0,pet=>pet.care(kind,side));}
function enter(pet,state,atDesk=false){
    pet.setScene({...pet.scene,state:state==="offline"?"idle":state,offline:state==="offline"});
    if(atDesk)pet.stage.finishSetup();
}
function deskRound(state,start,keys){
    return (pet,i)=>{
        if(i===0)enter(pet,state,true);
        if(i===start)pet.work.start();
        if(keys.includes(i))pet.work.key(" ");
    };
}
const motionCases=[
    ["idle",129,[0,80],()=>{}],
    ["five",90,[22,46],(pet,i)=>{
        if(i===0)pet.game.start("five",-1);
        if(i===40){const hand=pet.game.palm();pet.game.press({worldX:hand.x,worldY:hand.y});}
    }],
    ["glowbug",85,[17,40],(pet,i)=>{
        if(i===0)pet.game.start("fly",1);
        if(i===18)pet.game.key(" ");
    }],
    ["pat",60,[22],care("pat",-1)],
    ["feed",120,[14,33,68],care("feed")],
    ["bubble",140,[100],(pet,i)=>{
        if(i===0)pet.care("feed");
        if(i===92){const b=pet.offeredBubble();if(b)pet.popBubble({worldX:b.x,worldY:b.y});}
    }],
    ["star",84,[26,43],care("play")],
    ["wing",51,[8],care("wing",1)],
    ["tail",65,[21],care("tail",1)],
    ["sneeze",66,[31],(pet,i)=>{if(i===0 || i===12)pet.care("nose");}],
    ["cart",220,[26,68,78,144,196],on(0,pet=>enter(pet,"running"))],
    ["monitor",103,[48,62],(pet,i)=>{
        if(i===0)enter(pet,"working",true);
        if(i===30)pet.stage.showMonitor();
    }],
    ["writing",190,[41,74,119],on(0,pet=>enter(pet,"writing",true))],
    ["reading",146,[85,114],on(0,pet=>enter(pet,"reading",true))],
    ["search",70,[35],on(0,pet=>enter(pet,"searching",true))],
    ["cheer",90,[21,35,58],on(0,pet=>pet.stage.perform("cheer"))],
    ["sulk",120,[32,65],(pet,i)=>{
        if(i===0)pet.stage.perform("tease");
        if(i===45)pet.stage.perform("mend");
    }],
    ["waiting",66,[14],on(0,pet=>enter(pet,"waiting"))],
    ["stamp",128,[54,69],deskRound("writing",36,[58])],
    ["beat",100,[55],deskRound("running",35,[42,55,71])],
    ["turn",90,[61],deskRound("reading",34,[57])],
    ["notice",110,[27,52,83],(pet,i)=>{
        if(i===20)pet.attend({x:-.72,y:.1});
        if(i===45)pet.attend({x:.73,y:-.32});
        if(i===78)pet.releaseAttention();
    }],
    ["brush",98,[27,66],(pet,i)=>{
        const forehead=pet.rig.anchors.forehead,x=Math.sin(i*.16);
        if(i===1)pet.auditStroke=pet.beginStroke({x:-.2,worldX:forehead.x,worldY:forehead.y},33);
        if(i>2 && i<54)pet.stroke(pet.auditStroke,{region:"head",x,worldX:forehead.x+37*x,worldY:forehead.y},i*1000/30);
        if(i===60)pet.endStroke(pet.auditStroke);
    }],
    ["nap",196,[128,150],(pet,i)=>{
        if(i===0)pet.quiet=21;
        if(i===140)pet.attend({x:.4,y:0});
    }],
    ...["idle","working","searching","compact","waiting","error","limited","ended","succeeded","cancelled","offline"]
        .map(state=>["work-"+state,122,[45,69],deskRound(state,32,[56])]),
    ["handoff",143,[73,75,103],(pet,i)=>{
        if(i===0)enter(pet,"working");
        if(i===74)enter(pet,"waiting");
        if(i===99)enter(pet,"running");
    }]
];

window.auditPetMotion=async function(name){
    const [,length,shots,input]=motionCases.find(entry=>entry[0]===name);
    const canvas=document.createElement("canvas");
    canvas.width=612;canvas.height=540;
    const pet=new GptCompanion(canvas,{manual:true,seed:8271});
    await pet.ready;
    pet.setScene({active:true,state:"idle",scope:"qa",offline:false,reduced:false});
    const rig=pet.rig,points=rig.mesh.points,ids=rig.mesh.triangles,triangles=[];
    for(let i=0;i<ids.length;i+=3){
        const a=points[ids[i]],b=points[ids[i+1]],c=points[ids[i+2]];
        const u=(a.u+b.u+c.u)/3,v=(a.v+b.v+c.v)/3;
        if(rig.texture[(Math.round(v)*569+Math.round(u))*4+3]<220)continue;
        const area=(b.u-a.u)*(c.v-a.v)-(c.u-a.u)*(b.v-a.v);
        triangles.push({a,b,c,u,v,area});
    }
    const results=[],pictures=[],failures=[];
    let previous=null;
    for(let frame=0;frame<length;frame++){
        input(pet,frame);pet.advance(1/30);
        const factor=canvas.width/816*pet.pose.stage.scale;
        let maximum=0,flipped=0,stretched=0,hotX=0,hotY=0;
        for(const {a,b,c,u,v,area} of triangles){
            const bx=(b.x-a.x)/factor,by=(b.y-a.y)/factor,cx=(c.x-a.x)/factor,cy=(c.y-a.y)/factor;
            const j0=(bx*(c.v-a.v)-cx*(b.v-a.v))/area,j1=(cx*(b.u-a.u)-bx*(c.u-a.u))/area;
            const j2=(by*(c.v-a.v)-cy*(b.v-a.v))/area,j3=(cy*(b.u-a.u)-by*(c.u-a.u))/area;
            const determinant=j0*j3-j1*j2,sum=j0*j0+j1*j1+j2*j2+j3*j3;
            const sigma=Math.sqrt((sum+Math.sqrt(Math.max(0,sum*sum-4*determinant*determinant)))/2);
            if(determinant<=0)flipped++;
            if(sigma>1.5)stretched++;
            if(sigma>maximum){maximum=sigma;hotX=u;hotY=v;}
        }
        const {width,height}=canvas;
        const bytes=canvas.getContext("2d").getImageData(0,0,width,height).data;
        const rgba=new Uint32Array(bytes.buffer);
        let opaque=0,holes=0,changed=0;
        for(let j=0;j<rgba.length;j++){
            const alpha=bytes[4*j+3];
            if(alpha>=128)opaque++;
            if(previous && rgba[j]!==previous[j])changed++;
            if(!alpha && j>=width && j<rgba.length-width && j%width>0 && j%width<width-1 &&
               bytes[4*(j-1)+3]>224 && bytes[4*(j+1)+3]>224 &&
               bytes[4*(j-width)+3]>224 && bytes[4*(j+width)+3]>224)holes++;
        }
        previous=rgba;
        const eye=rig.anchors.eyes,stage=pet.pose.stage;
        const eyeIndex=Math.floor(eye.x*width/816)+Math.floor(eye.y*height/720)*width;
        if(bytes[4*eyeIndex+3]<128)failures.push([frame,"face disappeared"]);
        if(flipped)failures.push([frame,"body mesh folded"]);
        const right=rig.anchors.right;
        if(stage.grip>.999 && Math.hypot(right.x-stage.handle,right.y-window.DragonStage.desk.y)>1){
            failures.push([frame,"palm lost cart handle"]);
        }
        if(pet.pose.pad*stage.workMix>.999 && stage.present<.001 && !stage.kind){
            const page=DragonWriting.layout(rig.anchors.left,pet.pose.writing,pet.pose.penPark);
            if(Math.hypot(right.x-page.tipX-10,right.y-page.tipY+17)>1)failures.push([frame,"pen grip separated"]);
        }
        results.push([frame,maximum.toFixed(3),flipped,stretched,Math.round(hotX),Math.round(hotY),opaque,holes,changed]);
        if(shots.includes(frame))pictures.push([frame,canvas.toDataURL().split(",")[1]]);
    }
    return {results,pictures,failures};
};
