/* Short, locally synthesized notes. The audio context is created by a user gesture. */
(() => {
    "use strict";
    const notes={
        idle:[[784,0,.34],[1175,.075,.30]], working:[[523,0,.16],[784,.10,.23]],
        reading:[[370,0,.085]], writing:[[196,0,.095],[392,.025,.09]],
        beatLeft:[[523,0,.17]],beatRight:[[659,0,.17]],beatEnd:[[784,0,.28]],
        waiting:[[440,0,.18],[554,.11,.21]],succeeded:[[523,0,.21],[659,.09,.22],[784,.18,.33]],
        searching:[[740,0,.12],[988,.08,.22]],compact:[[494,0,.16],[659,.10,.21]],
        error:[[392,0,.21],[440,.12,.23]], limited:[[349,0,.25]], ended:[[392,0,.22]],
        cancelled:[[330,0,.16]],offline:[[523,0,.14],[698,.08,.19]],
        switch:[[330,0,.045],[660,.07,.12]],jump:[[523,0,.10],[784,.06,.13],[1046,.13,.17]],sweet:[[880,0,.14],[1046,.11,.18]],
        bubblePop:[[1175,0,.05],[1760,.045,.18]]
    };
    class DragonChimes{
        constructor(){
            this.Context=window.AudioContext||window.webkitAudioContext;this.audio=null;
            try{this.enabled=localStorage.getItem("ai-baji.chimes.v1")==="on";}catch{this.enabled=false;}
            document.addEventListener("visibilitychange",()=>{
                if(document.hidden && this.audio?.state==="running")this.audio.suspend().catch(()=>{});
            });
        }
        toggle(){
            this.enabled=!this.enabled;
            try{localStorage.setItem("ai-baji.chimes.v1",this.enabled?"on":"off");}catch{}
            if(this.enabled)this.play("working");
            else if(this.audio)this.audio.suspend().catch(()=>{});
        }
        async play(id){
            if(!this.enabled||!this.Context||document.hidden||!notes[id])return;
            try{
                this.audio??=new this.Context();
                if(this.audio.state!=="running")await this.audio.resume();
                if(!this.enabled||document.hidden)return;
                const now=this.audio.currentTime;
                for(const [hz,delay,length] of notes[id]){
                    const osc=this.audio.createOscillator(),gain=this.audio.createGain(),start=now+delay;
                    osc.type=id==="writing"?"triangle":"sine";osc.frequency.setValueAtTime(hz,start);
                    gain.gain.setValueAtTime(.0001,start);gain.gain.exponentialRampToValueAtTime(.075,start+.012);
                    gain.gain.exponentialRampToValueAtTime(.0001,start+length);
                    osc.connect(gain);gain.connect(this.audio.destination);
                    osc.onended=()=>{osc.disconnect();gain.disconnect();};osc.start(start);osc.stop(start+length+.012);
                }
            }catch{ /* The visual response also works with unavailable audio output. */ }
        }
    }
    window.DragonChimes=DragonChimes;
})();
