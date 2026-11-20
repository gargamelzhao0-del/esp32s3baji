(() => {
    "use strict";
    const find=id=>document.getElementById(id);
    const names={plain:"原装",bow:"樱花结",moon:"月牙夹",leaf:"嫩叶夹"};
    const wearKey="ai-baji.gpt.accessory.v1";
    function draw(canvas,id){
        const c=canvas.getContext("2d");
        c.clearRect(0,0,canvas.width,canvas.height);
        window.DragonArt.icon(c,id,canvas.width/2,canvas.height/2,Math.min(canvas.width,canvas.height)/60);
    }
    class DragonPocket {
        static readWear(){
            try{const id=localStorage.getItem(wearKey);return Object.hasOwn(names,id)?id:"plain";}catch{return "plain";}
        }
        constructor(pet,stateLabel){
            this.pet=pet;this.stateLabel=stateLabel;this.panel=find("pet-pocket");this.tab="desk";this.lastId="";
            this.tabs=[...this.panel.querySelectorAll("[data-pocket-tab]")];
            this.panel.querySelectorAll("canvas[data-art]").forEach(c=>{draw(c,c.dataset.art);c.setAttribute("aria-hidden","true");});
            draw(find("pocket-open").querySelector("canvas"),"pocket");
            find("pocket-open").addEventListener("click",()=>this.open());
            find("pocket-close").addEventListener("click",()=>this.close(true));
            find("desk-start").addEventListener("click",()=>{this.close();this.work();});
            find("show-monitor").addEventListener("click",()=>{
                this.close();pet.stage.showMonitor();pet.canvas.focus({preventScroll:true});
            });
            find("work-button").addEventListener("click",()=>this.work());
            this.panel.addEventListener("click",e=>{
                if(e.target===this.panel){this.close(true);return;}
                const b=e.target.closest("button");if(!b)return;
                if(b.dataset.pocketTab)this.select(b.dataset.pocketTab);
                if(b.dataset.story){
                    this.close();pet.stage.perform(b.dataset.story);
                    pet.canvas.focus({preventScroll:true});
                }
                if(b.dataset.toy){
                    this.close();const id=b.dataset.toy;
                    if(id==="five"||id==="fly")pet.game.start(id);else pet.care(id);
                    pet.canvas.focus({preventScroll:true});
                }
                if(b.dataset.wear){
                    const id=b.dataset.wear;pet.accessory=id;
                    pet.stage.finishSetup();
                    try{localStorage.setItem(wearKey,id);}catch{}
                    this.close();pet.advance(0,true);
                    pet.note(id==="plain"?"她理了理额前的碎发。":"她戴好"+names[id]+"，朝你歪了歪头。");
                    if(!pet.game.round&&!pet.work.round&&!pet.scene.reduced)pet.attend({x:-.72,y:-.45});
                    pet.canvas.focus({preventScroll:true});this.sync();
                }
            });
            this.panel.addEventListener("keydown",e=>{
                if(e.key==="Escape"){e.preventDefault();this.close(true);return;}
                if(e.target.matches("[role=tab]")&&["ArrowLeft","ArrowRight","Home","End"].includes(e.key)){
                    e.preventDefault();let next=this.tabs.indexOf(e.target)+(e.key==="ArrowLeft"?-1:1);
                    if(e.key==="Home")next=0;if(e.key==="End")next=this.tabs.length-1;
                    const tab=this.tabs[(next+this.tabs.length)%this.tabs.length];this.select(tab.dataset.pocketTab);tab.focus();
                }
            });
            this.panel.addEventListener("focusout",e=>{
                if(e.relatedTarget&&!this.panel.contains(e.relatedTarget)&&e.relatedTarget!==find("pocket-open"))this.close();
            });
            this.select("desk");
        }
        work(){
            if(this.pet.work.round)this.pet.work.end();else this.pet.work.start();
            this.pet.canvas.focus({preventScroll:true});
        }
        open(){
            this.panel.hidden=false;find("pocket-open").setAttribute("aria-expanded","true");
            this.sync();this.tabs.find(b=>b.dataset.pocketTab===this.tab).focus({preventScroll:true});
        }
        close(restore=false){
            if(this.panel.hidden)return;this.panel.hidden=true;
            find("pocket-open").setAttribute("aria-expanded","false");
            if(restore)find("pocket-open").focus({preventScroll:true});
        }
        select(id){
            this.tab=id;
            for(const b of this.tabs){
                const selected=b.dataset.pocketTab===id;
                b.setAttribute("aria-selected",String(selected));b.tabIndex=selected?0:-1;
                find(b.getAttribute("aria-controls")).hidden=!selected;
            }
        }
        sync(){
            const w=this.pet.work,id=w.id,item=w.item,r=w.round;
            const moody=this.pet.stage.sulking, mood=find("story-mood");
            const icon=mood.querySelector("canvas"), art=moody?"candy":"tail";
            if(icon.dataset.art!==art){icon.dataset.art=art;draw(icon,art);}
            mood.dataset.story=moody?"mend":"tease";mood.querySelector("span").textContent=moody?"递和好糖":"逗逗尾巴";
            if(id!==this.lastId){
                if(["waiting","error","limited","offline"].includes(id))this.close();
                draw(find("work-button").querySelector("canvas"),id);draw(find("desk-drawing"),id);this.lastId=id;
            }
            find("desk-state").textContent=this.stateLabel();
            find("show-monitor").hidden=!this.pet.stage.canPresent;
            find("desk-start").textContent=r?"收起这次互动":item.name+" ›";
            const button=find("work-button");button.querySelector("span").textContent=r?"收起":item.short;
            button.setAttribute("aria-label",r?"收起"+item.name:item.name);button.setAttribute("aria-pressed",String(!!r));
            const cue=find("work-cue");cue.hidden=!r;cue.textContent=w.cue();find("screen").classList.toggle("work-open",!!r);
            this.panel.querySelectorAll("[data-wear]").forEach(b=>b.setAttribute("aria-pressed",String(b.dataset.wear===this.pet.accessory)));
        }
    }
    window.DragonPocket=DragonPocket;
})();
