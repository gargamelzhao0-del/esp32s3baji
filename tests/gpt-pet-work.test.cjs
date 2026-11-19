const {test}=require("node:test");
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const env={window:{},setTimeout,clearTimeout};
vm.runInNewContext(fs.readFileSync(path.join(__dirname,"../design/gpt-pet/work.js"),"utf8"),env);
function create(state,reduced=false){
    const pet={scene:{active:true,state,reduced},game:{clear(){}},bond:0,rig:{anchors:{
        left:{x:292,y:460},right:{x:505,y:463},eyes:{x:394,y:339}}},
        onWork(){},advance(){},baseNote(){return "";},note(){},reward(){this.bond++;}};
    const w=new env.window.DragonWork(pet);
    w.start();return {pet,w};
}
function point(w,dx=0){const a=w.point();return {worldX:a.x+dx,worldY:a.y};}
test("the page follows the finger; a short or cancelled drag leaves the book open",()=>{
    const {w,pet}=create("reading");
    w.down(point(w));w.move(point(w,-38));assert(w.round.progress>.28);
    w.release(point(w,-38),18);assert(w.round.progress>.2);w.tick(.4);assert.equal(w.round.progress,0);assert.equal(pet.bond,0);
    w.down(point(w));w.move(point(w,-90));w.cancelDrag();w.tick(.4);assert.equal(w.round.progress,0);
    w.down(point(w));w.release(point(w,-100),44);assert.equal(w.round.phase,"done");
    w.key(" ");assert.equal(pet.bond,1);
});
test("three correct alternating beats are necessary, including fresh hand positions",()=>{
    const {w,pet}=create("running");
    const notes=[];pet.onTone=id=>notes.push(id);
    w.key("ArrowRight");assert.equal(w.round.beats,0);
    w.key("ArrowLeft");assert.equal(w.round.beats,1);
    const old=point(w);
    pet.rig.anchors.left.x+=140;pet.rig.anchors.right.x+=140;
    w.release(old);assert.equal(w.round.beats,1);
    w.key("ArrowRight");w.key("ArrowLeft");assert.equal(w.round.phase,"done");
    assert.equal(pet.bond,1);assert.deepEqual(notes,["beatLeft","beatRight","beatEnd"]);
});
test("every state has an accessible local action, with separate success and ordinary ending",()=>{
    const names=new Set();
    for(const id of Object.keys(env.window.DragonWork.catalog)){
        const {w,pet}=create(id,true);names.add(w.item.name);
        w.key("Enter");if(id==="running"){w.key("Enter");w.key("Enter");}
        assert.equal(w.round.phase,"done",id);assert.equal(pet.scene.state,id);
        w.key("Escape");assert.equal(w.round,null);assert.equal(pet.bond,1);
    }
    assert.equal(names.size,14);
});
