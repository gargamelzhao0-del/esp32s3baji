/* The enamel dial is painted once, independently of the character's frame clock. */
(() => {
    "use strict";
    const canvas = document.getElementById("night-sky");
    const c = canvas.getContext("2d");
    const size = 466;
    c.scale(canvas.width / size, canvas.height / size);
    const sky = c.createLinearGradient(100, 35, 310, 416);
    sky.addColorStop(0, "#0d1a30");
    sky.addColorStop(.42, "#273554");
    sky.addColorStop(.7, "#565071");
    sky.addColorStop(1, "#1b273d");
    c.fillStyle = sky;
    c.fillRect(0, 0, size, size);

    function cloud(outline, upper, lower) {
        const wash = c.createLinearGradient(0, 220, 0, 386);
        wash.addColorStop(0, upper);
        wash.addColorStop(1, lower);
        const ridge = new Path2D(outline);
        c.strokeStyle = upper;
        c.lineWidth = 1.4;
        c.stroke(ridge);
        ridge.lineTo(size + 5, size);
        ridge.lineTo(-5, size);
        ridge.closePath();
        c.fillStyle = wash;
        c.fill(ridge);
    }
    cloud("M-5 225 Q11 217 23 226 Q23 212 37 216 Q47 185 68 207 Q83 202 89 219 Q111 211 117 236 Q136 229 151 247 Q196 230 223 259 Q257 222 291 245 Q311 231 325 240 Q333 209 358 211 Q362 192 381 198 Q399 169 420 186 Q439 180 445 202 Q461 193 471 206", "#9383a2", "#353b5d");
    cloud("M-5 260 Q14 236 39 254 Q57 240 71 264 Q95 257 110 279 Q137 267 158 287 Q206 268 238 293 Q264 271 294 285 Q311 253 337 265 Q359 231 382 250 Q402 232 416 244 Q444 212 471 233", "#716e91", "#30374f");
    cloud("M-5 301 Q19 282 36 293 Q49 276 70 291 Q85 288 95 305 Q120 286 148 305 Q177 294 202 317 Q251 296 282 312 Q309 284 336 296 Q359 270 382 283 Q400 262 428 276 Q453 251 471 263", "#515875", "#1d293e");
    c.lineCap="round";
    c.lineWidth=1;
    c.strokeStyle="#f5d1c265";
    c.stroke(new Path2D("M22 227 Q30 214 37 219 Q46 192 62 205 M66 210 Q78 204 84 216 M356 215 Q362 197 379 203 Q393 180 406 185 M421 191 Q435 185 443 205 M44 255 Q58 246 69 258 M374 251 Q383 240 394 245"));
    c.strokeStyle="#c5b9d44d";
    c.stroke(new Path2D("M6 290 Q19 283 27 288 M55 280 Q64 278 71 286 M392 273 Q410 265 426 275 M434 269 Q450 256 460 259"));

    const moonlight = c.createRadialGradient(110, 100, 2, 110, 100, 44);
    moonlight.addColorStop(0, "#ffda9b42");
    moonlight.addColorStop(1, "#ffe9c100");
    c.fillStyle = moonlight;
    c.fillRect(65, 55, 90, 90);
    c.fillStyle = "#f3d4a1";
    c.fill(new Path2D("M116 79 C98 82 94 107 115 113 Q124 116 132 111 C122 129 97 125 90 108 C85 94 97 78 116 79Z"));

    function sparkle(x, y, r) {
        c.save();
        c.translate(x, y);
        c.scale(r, r);
        c.fill(new Path2D("M0 -1 Q.08 -.16 .72 0 Q.09 .12 0 1 Q-.1 .14 -.72 0 Q-.09 -.14 0 -1Z"));
        c.restore();
    }
    let seed = 2197;
    function random() {
        seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
        return seed / 4294967296;
    }
    for (let i = 0; i < 125; i++) {
        const x = random() * size, y = 16 + random() * 222;
        c.fillStyle = i % 3 ? "#e0e8f4" : "#ffd89d";
        c.globalAlpha = .33 + random() * .55;
        if (i % 11 === 0) sparkle(x, y, 2 + random() * 3);
        else {
            c.beginPath();
            c.arc(x, y, .35 + random() * .65, 0, Math.PI * 2);
            c.fill();
        }
    }
    c.globalAlpha = 1;
    const chart = [[300,82],[323,74],[344,94],[367,100],[355,124]];
    c.beginPath();
    for (const [x,y] of chart) c.lineTo(x,y);
    c.strokeStyle = "#ecd1b580"; c.lineWidth = .75; c.stroke();
    c.fillStyle = "#f5ddb6";
    for (const [x,y] of chart) sparkle(x,y,3.2);

    // Leave the bottom of the dial dark behind the status and live counters.
    const shade = c.createLinearGradient(0, 290, 0, 439);
    shade.addColorStop(0, "#0a142200"); shade.addColorStop(.66, "#0c1627dd");
    shade.addColorStop(1, "#091321");
    c.fillStyle = shade; c.fillRect(0, 290, size, size - 290);
})();
