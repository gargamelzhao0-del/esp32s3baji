/* Facial articulation for the CrossPet white dragon. Coordinates are in the 569 × 640 rest portrait. */
(() => {
    "use strict";
    const area = {x: 160, y: 239, width: 222, height: 105};
    const leftSkin = new Path2D(
        "M169 268 Q175 264 177 259 L181 254 L186 257 L188 251 L193 256 " +
        "Q214 250 230 257 Q239 260 243 271 Q248 292 234 305 " +
        "Q215 312 202 303 Q185 298 179 279 Z"
    );
    const rightSkin = new Path2D(
        "M296 270 Q300 259 310 254 Q329 248 342 251 L345 247 L350 251 " +
        "L353 249 Q358 252 365 256 L370 253 L366 263 Q360 288 350 299 " +
        "Q336 307 319 304 Q303 302 298 287 Z"
    );

    function makeSurface() {
        const canvas = document.createElement("canvas");
        canvas.width = area.width;
        canvas.height = area.height;
        return canvas;
    }

    function drawEye(ctx, centerX, centerY, facing, p) {
        const wink = facing > 0 ? p.winkL : p.winkR;
        const open = Math.max(0, Math.min(1.1, p.lid * (1 - p.blink) * (1 - wink)));
        const shut = 1 - Math.min(1, open);
        const corner = -7 + shut * 13;
        const crown = -27 * open + shut * (3 - 12 * p.smile);
        const bottom = 30 * open + shut * (3 - 12 * p.smile);
        const irisX = 4 + facing * p.gazeX * 9 + 3.7 * p.squint;
        const irisY = 0.5 + p.gazeY * 5;
        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.scale(facing, 1);
        // Only the inner corner dips. The near and far eye retain a common side-eye.
        ctx.rotate(.13*p.pout);

        // The upper lid actually crosses the iris. Highlights travel with the pupil.
        const opening = new Path2D();
        opening.moveTo(-32, corner);
        opening.bezierCurveTo(-20, crown - 1, 14, crown - 2, 32, corner + 1);
        opening.bezierCurveTo(26, bottom, -15, bottom + 3 * open, -32, corner);
        if (open > 0.035) {
            ctx.save();
            ctx.clip(opening);
            const sclera = ctx.createLinearGradient(0,-22,0,21);
            sclera.addColorStop(0,"#c4b7d0"); sclera.addColorStop(0.52,"#faf1f4");
            sclera.addColorStop(1,"#fffaf4"); ctx.fillStyle=sclera;
            ctx.fillRect(-36, -33, 73, 68);
            const color = ctx.createLinearGradient(0, irisY - 25, 0, irisY + 25);
            color.addColorStop(0, "#3b345e");
            color.addColorStop(0.33, "#73639b");
            color.addColorStop(0.68, "#b79cd9");
            color.addColorStop(1, "#f2d2e7");
            ctx.beginPath();
            ctx.ellipse(irisX, irisY, 19.5, 24, -0.03, 0, 2 * Math.PI);
            ctx.fillStyle = color;
            ctx.fill();
            ctx.lineWidth = 1.6;
            ctx.strokeStyle = "#7d728e";
            ctx.stroke();
            ctx.beginPath();
            ctx.ellipse(irisX, irisY - 4, 6, 10, 0, 0, Math.PI * 2);
            ctx.fillStyle = "#675982";
            ctx.fill();
            ctx.beginPath();
            ctx.ellipse(irisX + 2, irisY + 13, 10, 5, -0.2, 0, Math.PI * 2);
            ctx.fillStyle = "#dfd5fa";
            ctx.fill();
            ctx.strokeStyle="#fbddb8";ctx.lineWidth=1.8;
            ctx.beginPath();ctx.ellipse(irisX,irisY+2,15,17,0,.38,1.96);ctx.stroke();
            ctx.fillStyle = "#fffafa";
            ctx.beginPath();
            ctx.ellipse(irisX - facing * 6, irisY - 11, 4.9, 6.2, 0.2, 0, 2 * Math.PI);
            ctx.fill();
            ctx.beginPath();
            ctx.arc(irisX + facing * 7, irisY + 7, 2.4, 0, Math.PI * 2);
            ctx.fill();
            if(p.light>0.03) {
                ctx.globalAlpha=Math.min(1,p.light);
                ctx.fillStyle="#fff2b2";
                const glintX=irisX+facing*p.gazeX*7,glintY=irisY+6;
                ctx.beginPath();
                ctx.moveTo(glintX,glintY-6);ctx.quadraticCurveTo(glintX+.7,glintY-1,glintX+4.4,glintY);
                ctx.quadraticCurveTo(glintX+1,glintY+.7,glintX,glintY+5.7);
                ctx.quadraticCurveTo(glintX-.8,glintY+1,glintX-4.4,glintY);
                ctx.quadraticCurveTo(glintX-.8,glintY-1,glintX,glintY-6);ctx.fill();
            }
            ctx.restore();
        }
        ctx.fillStyle = "#4e4153";
        ctx.beginPath();
        ctx.moveTo(-36, corner - 3);
        ctx.quadraticCurveTo(-29, corner - 2, -26, corner - 8);
        ctx.lineTo(-27, corner - 12);
        ctx.lineTo(-22, corner - 9);
        ctx.bezierCurveTo(-7, crown - 6, 20, crown - 6, 33, corner + 2);
        ctx.bezierCurveTo(14, crown, -12, crown + 1, -30, corner + 3);
        ctx.closePath();
        ctx.fill();
        ctx.restore();
    }

    class DragonFace {
        constructor(image) {
            this.clean = makeSurface();
            const clean = this.clean.getContext("2d");
            clean.translate(-area.x, -area.y);
            clean.drawImage(image, 0, 0);
            const skin = clean.createLinearGradient(0, 251, 0, 310);
            skin.addColorStop(0, "#fbf0ed");
            skin.addColorStop(1, "#fdf2ee");
            clean.fillStyle = skin;
            clean.fill(leftSkin);
            clean.fill(rightSkin);
            clean.fillStyle = "#fdf2ee";
            clean.beginPath();
            clean.ellipse(271, 319, 15, 6, 0, 0, Math.PI * 2);
            clean.fill();
            this.ctx = makeSurface().getContext("2d", {willReadFrequently: true});
            this.rouge=document.createElement("canvas"); this.rouge.width=this.rouge.height=40;
            const cheek=this.rouge.getContext("2d");
            const tint=cheek.createRadialGradient(20,20,1,20,20,20);
            tint.addColorStop(0,"rgba(227,120,149,0.72)"); tint.addColorStop(1,"rgba(235,142,162,0)");
            cheek.fillStyle=tint; cheek.fillRect(0,0,40,40);
        }

        update(texture, width, p) {
            const ctx = this.ctx;
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.clearRect(0, 0, area.width, area.height);
            ctx.drawImage(this.clean, 0, 0);
            ctx.setTransform(1, 0, 0, 1, -area.x, -area.y);
            if (p.blush > 0.02) {
                ctx.save(); ctx.globalAlpha=Math.min(0.84,p.blush);
                for (const x of [201, 342]) ctx.drawImage(this.rouge,x-18-4*p.pout,299,36+8*p.pout,14+9*p.pout);
                ctx.lineWidth=1.4;ctx.lineCap="round";ctx.strokeStyle="#c4869b";
                ctx.beginPath();
                for(const cheek of [198,342])for(const offset of [-5,0,5]){
                    ctx.moveTo(cheek+offset,304);ctx.lineTo(cheek+offset-2,307);
                }
                ctx.stroke();
                ctx.restore();
            }
            drawEye(ctx, 208, 280, 1, p);
            drawEye(ctx, 332, 277, -1, p);
            const smile = p.smile;
            const jaw = p.jaw;
            const lipWidth = 3.5 + 4.5 * Math.max(0, smile) + jaw * 3.3+5*p.pout;
            ctx.beginPath();
            const mouthX=270-4*p.pout;
            ctx.moveTo(mouthX - lipWidth, 319 - smile+2*p.pout);
            ctx.quadraticCurveTo(mouthX, 320 + 6 * smile-7*p.pout, mouthX + lipWidth, 319 - smile+p.pout);
            ctx.lineCap = "round";
            ctx.lineWidth = 1.7+.9*p.pout;
            ctx.strokeStyle = "#b68c91";
            if (jaw > 0.055) {
                ctx.quadraticCurveTo(270, 320 + jaw * 12, 270 - lipWidth, 319 - smile);
                const lip=ctx.createLinearGradient(0,317,0,326);
                lip.addColorStop(0,"#986e7e"); lip.addColorStop(0.62,"#b88395"); lip.addColorStop(1,"#edb8bb");
                ctx.fillStyle = lip;
                ctx.fill();
                ctx.stroke();
                if(smile>.45 && jaw>.24){
                    ctx.save();ctx.clip();
                    ctx.fillStyle="#ffcad3";ctx.beginPath();
                    ctx.ellipse(271,321+jaw*7,lipWidth*.7,2.4,0,0,Math.PI*2);ctx.fill();
                    ctx.fillStyle="#fff9ed";ctx.beginPath();ctx.moveTo(270+lipWidth*.22,319);
                    ctx.lineTo(270+lipWidth*.52,319);ctx.lineTo(270+lipWidth*.32,322.4);ctx.fill();ctx.restore();
                }
            } else ctx.stroke();
            if(p.pout>.06){
                ctx.save();ctx.globalAlpha=Math.min(1,p.pout);ctx.lineWidth=2.4;ctx.strokeStyle="#bd8f96";
                ctx.beginPath();ctx.moveTo(181,300);ctx.quadraticCurveTo(173,307,181,312);
                ctx.moveTo(355,298);ctx.quadraticCurveTo(362,305,356,310);ctx.stroke();ctx.restore();
            }

            const patch = ctx.getImageData(0, 0, area.width, area.height).data;
            const stride = area.width * 4;
            for (let row = 0; row < area.height; row++) {
                texture.set(patch.subarray(row * stride, (row + 1) * stride),
                            ((area.y + row) * width + area.x) * 4);
            }
        }
    }
    window.DragonFace = DragonFace;
})();
