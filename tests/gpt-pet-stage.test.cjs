const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const test=require("node:test");
const vm=require("node:vm");
const sandbox={window:{},setTimeout,clearTimeout,performance};
function load(name){
    const filename=path.join(__dirname,"../design/gpt-pet",name+".js");
    vm.runInNewContext(fs.readFileSync(filename,"utf8"),sandbox,{filename});
}
load("stage");load("play");
function create(){
    const pet={time:0,bond:0,scene:{active:true,state:"idle",scope:"a",offline:false,reduced:false},
        rig:{anchors:{left:{x:300,y:480},right:{x:490,y:480},eyes:{x:400,y:300}}},
        work:{clear(){},round:null},note(){},baseNote(){return "";},reward(){this.bond++;},
        onPlay(){},random(){return .5;},advance(){}};
    const stage=pet.stage=new sandbox.window.DragonStage(pet);
    pet.game=new sandbox.window.DragonPlay(pet);
    function scene(patch){
        const old=pet.scene;pet.scene={...old,...patch};stage.sync(old,pet.scene);
    }
    function tick(seconds){
        const n=Math.ceil(seconds*60);
        for(let i=0;i<n;i++){pet.time+=seconds/n;pet.game.tick(seconds/n);stage.tick(seconds/n);}
    }
    return {pet,stage,scene,tick};
}
test("the same cart survives tool events and heartbeats, and confirmation yields the hands immediately",()=>{
    const {pet,stage,scene,tick}=create();
    scene({state:"working"});const cart=stage.office;
    tick(2);const grip=stage.sample();assert.equal(grip.grip,1);
    scene({state:"reading"});scene({});assert.equal(stage.office,cart);
    tick(1.8);assert(grip.handle-stage.sample().handle>320);
    scene({state:"waiting"});assert.equal(stage.sample().boot,1);assert.equal(stage.sample().grip,0);
    assert(stage.sample().paused);scene({state:"running"});assert.equal(stage.office,cart);
    scene({state:"running",scope:"b"});assert.notEqual(stage.office,cart);assert.equal(stage.office.age,0);
    scene({offline:true});assert.equal(stage.office,null);assert(pet.scene.offline);
});
test("a planted foot holds its ground through a stride while the other foot rises",()=>{
    const {stage,scene}=create();scene({state:"working"});
    let run=0,longest=0,drift=0,previous;
    // Sample the constant-size approach away from its contact blending endpoints.
    for(let t=.76;t<1.42;t+=1/120){
        stage.office.age=t;const s=stage.sample();
        if(s.footLY===0 && s.footRY<-.5){
            const x=s.x+s.footLX;
            if(run)drift=Math.max(drift,Math.abs(x-previous));
            previous=x;longest=Math.max(longest,++run);
        }else run=0;
    }
    assert(longest>=12);assert(drift<.001);
});
test("only an explicit success edge automatically starts the two-hop celebration",()=>{
    const {stage,scene,tick}=create();
    for(const state of ["working","ended","cancelled"]){scene({state});assert.equal(stage.mood,null);}
    scene({state:"succeeded"});const mood=stage.mood;assert.equal(mood.origin,"agent");
    tick(.30);assert(stage.sample().crouch>.75);assert.equal(stage.sample().air,0);
    tick(.70);assert(stage.sample().air>95);scene({});assert.equal(stage.mood,mood);
    tick(.55);assert.equal(stage.sample().air,0);tick(.38);assert(stage.sample().air>30);
    scene({state:"cancelled"});assert.equal(stage.mood,null);
    stage.perform("cheer");assert.equal(stage.mood.origin,"pet");assert.equal(stage.pet.scene.state,"cancelled");
});
test("three recent tail taps start a finite sulk; forgiving early preserves the current pose and credits one candy",()=>{
    const {pet,stage,scene,tick}=create();
    assert.equal(stage.tailTap(),false);tick(2.72);
    assert.equal(stage.tailTap(),false);tick(.2);assert.equal(stage.tailTap(),false);tick(.2);
    assert(stage.tailTap());tick(.36);const initial=stage.sample();assert(initial.puff>0&&initial.puff<1);
    stage.refuse();tick(.15);assert(stage.sample().snub>.9);
    const before=stage.sample();stage.perform("mend");assert.equal(stage.sample().puff,before.puff);
    assert.equal(stage.sample().turn,before.turn);tick(1.1);assert.equal(pet.bond,1);
    tick(1.4);stage.perform("mend");assert.equal(stage.mood,null);assert.equal(pet.bond,1);
    stage.perform("tease");tick(5.9);stage.refuse();tick(1.42);assert.equal(stage.mood,null);
    scene({state:"writing"});stage.perform("tease");scene({state:"reading"});assert(stage.sulking);
    scene({state:"error"});assert.equal(stage.mood,null);
});
test("touching opposite palms on two successful invitations earns a local jump after the second hand releases",()=>{
    const {pet,stage,tick}=create();
    for(const side of [-1,1]){
        pet.game.start("five",side);const a=pet.game.palm();
        pet.game.press({worldX:a.x,worldY:a.y});assert.equal(stage.mood,null);
        tick(1.45);
    }
    assert.equal(pet.game.round,null);assert.equal(stage.mood.kind,"cheer");
    assert.equal(stage.mood.origin,"pet");assert.equal(pet.scene.state,"idle");assert.equal(pet.bond,2);
});
test("reduced motion opens a still workstation and cleans up the mood timer when the pet page hides",()=>{
    const {stage,scene}=create();
    scene({state:"working",reduced:true});assert.equal(stage.sample().setup,false);assert.equal(stage.sample().boot,1);
    stage.perform("tease");assert(stage.sample().puff>.95);assert(stage.timer);
    scene({active:false});assert.equal(stage.timer,0);assert.equal(stage.mood,null);
    scene({active:true});assert.equal(stage.sample().setup,false);
});
test("showing the seated monitor is a local gesture and a new Agent event immediately takes over the display",()=>{
    const {pet,stage,scene,tick}=create();
    scene({state:"reading"});assert.equal(stage.canPresent,false);tick(6.3);
    assert(stage.onMonitor({worldX:559,worldY:451}));assert(!stage.onMonitor({worldX:318,worldY:510}));
    stage.showMonitor();tick(.99);assert(stage.sample().present>.99);assert.equal(pet.scene.state,"reading");
    scene({state:"limited"});assert.equal(stage.sample().present,0);
    assert.equal(stage.sample().displayMode,"limited");assert(stage.sample().paused);
});
