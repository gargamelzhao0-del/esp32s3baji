const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const test=require("node:test");

// The driver can run before its portrait has decoded. Exercise its clocks without a canvas.
class DeferredImage { decode(){return new Promise(()=>{});} }
const env={window:{},Image:DeferredImage,setTimeout,clearTimeout};
for(const name of ["props","stage","work","play","companion"]){
    const filename=path.join(__dirname,"../design/gpt-pet",name+".js");
    vm.runInNewContext(fs.readFileSync(filename,"utf8"),env,{filename});
}
function companion(){
    const pet=new env.window.GptCompanion(null,{manual:true,seed:81});
    pet.scene.active=true;
    pet.rig={anchors:{eyes:{x:405,y:315}}};
    return pet;
}
function tick(pet,seconds){
    const frames=Math.ceil(seconds*30);
    for(let i=0;i<frames;i++)pet.updateClocks(seconds/frames);
}
function head(x){return {region:"head",worldX:x,worldY:172,x:(x-405)/204};}

test("resting a finger retains contact, reversing a stroke changes its direction, and release settles once",()=>{
    const pet=companion(),c=pet.beginStroke(head(400),0);
    tick(pet,3);assert.equal(pet.action.contact,c);assert.equal(pet.bond,1);
    pet.stroke(c,head(464),3000);assert(c.vx>0);
    tick(pet,.20);pet.stroke(c,head(356),3200);assert(c.vx<0);assert.equal(c.sparks.length,2);
    pet.endStroke(c);tick(pet,1.4);assert.equal(pet.action,null);assert.equal(pet.bond,1);
    const slip=pet.beginStroke(head(410),5000);
    pet.stroke(slip,{region:"surround"},5040);tick(pet,.7);assert.equal(pet.bond,1);
});
test("release momentum determines flight time and the star meets either palm inside the drawing area",()=>{
    function throwAfter(pause,dir){
        const pet=companion(),from={worldX:405,worldY:430};
        const release={worldX:405+120*dir,worldY:180};
        pet.holdStar(release,110,from,0);pet.releaseStar(release,110+pause);
        const cast=pet.action.cast,palm={x:405+dir*98,y:413};
        assert.equal(cast.side,dir);
        for(let n=0;n<=60;n++){
            const pt=env.window.dragonStarPath(.18+n/50,cast,palm);
            assert(pt.y>=34 && pt.x>=24 && pt.x<=792);
            if(n===0){assert.equal(pt.x,release.worldX);assert.equal(pt.y,release.worldY);}
            if(n===60){assert.equal(pt.x,palm.x+3*dir);assert.equal(pt.y,palm.y-14);}
        }
        tick(pet,cast.flight-.05);assert.equal(pet.bond,0);
        tick(pet,.10);assert.equal(pet.bond,1);
        return cast.flight;
    }
    assert(throwAfter(0,-1)>throwAfter(480,1)+.4);
});
