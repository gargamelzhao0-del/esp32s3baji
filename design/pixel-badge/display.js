"use strict";

window.PixelBadge = (() => {
  const side = 216,
    center = 108,
    radius = 96;
  const faceSide = 184,
    faceCenter = 92,
    faceRadius = 82,
    originX = 23,
    originY = (view) => (view === "close" ? 22 : 18),
    W = 138,
    H = 132;
  const blank = (w, h) => {
    const c = document.createElement("canvas");
    c.width = w;
    c.height = h;
    return c;
  };
  const plate = blank(side, side),
    shell = blank(side, side),
    sky = plate.getContext("2d");
  function rect(ctx, x, y, w, h, color) {
    ctx.fillStyle = color;
    ctx.fillRect(x, y, w, h);
  }
  function shine(ctx, x, y, color, size = 2) {
    rect(ctx, x, y - size, 1, 2 * size + 1, color);
    rect(ctx, x - size, y, 2 * size + 1, 1, color);
  }
  function staircase(ctx, points, bottom, color) {
    for (let i = 1; i < points.length; i++) {
      const [a, b] = points[i - 1],
        [x, y] = points[i];
      for (let u = a; u < x; u++) {
        const h = Math.round((b + ((u - a) * (y - b)) / (x - a)) / 2) * 2;
        rect(ctx, u, h, 1, bottom - h, color);
      }
    }
  }
  function pine(x, y, n, color) {
    rect(sky, x, y - 2 * n, 1, 2 * n, color);
    for (let i = 1; i <= n; i++) {
      const h = Math.ceil(i * 0.55);
      rect(sky, x - h, y - 2 * n + 2 * i - 2, h * 2 + 1, 2, color);
    }
  }
  function buildLandscape() {
    // A quiet night ridge leaves the silhouette and the instrument panel legible.
    for (const [y, color] of [
      [0, "#111c29"],
      [57, "#192837"],
      [94, "#243847"],
      [124, "#304851"],
    ])
      rect(sky, 0, y, side, side - y, color);
    let seed = 141;
    const random = () =>
      (seed = (1664525 * seed + 1013904223) >>> 0) / 4294967296;
    for (let i = 0; i < 83; i++)
      rect(
        sky,
        Math.floor(random() * side),
        Math.floor(random() * 130),
        1,
        1,
        i % 3 ? "#5d777e" : "#95aca8",
      );
    for (const [x, y] of [
      [45, 73],
      [65, 39],
      [168, 86],
    ])
      shine(sky, x, y, "#b2c9bc", 2);
    for (let y = -7; y <= 7; y++)
      for (let x = -7; x <= 7; x++)
        if (x * x + y * y <= 49 && (x - 3) ** 2 + (y + 2) ** 2 > 39)
          rect(sky, 165 + x, 61 + y, 1, 1, y > 3 ? "#c7a794" : "#efd9b1");
    staircase(
      sky,
      [
        [0, 135],
        [28, 122],
        [44, 126],
        [65, 109],
        [87, 126],
        [105, 118],
        [131, 128],
        [159, 108],
        [176, 120],
        [191, 116],
        [216, 130],
      ],
      173,
      "#344654",
    );
    staircase(
      sky,
      [
        [0, 139],
        [33, 148],
        [59, 127],
        [82, 138],
        [113, 145],
        [143, 130],
        [167, 140],
        [192, 126],
        [216, 139],
      ],
      176,
      "#263644",
    );
    rect(sky, 0, 154, side, 62, "#26353f");
    for (let i = 0; i < 42; i++)
      rect(
        sky,
        Math.floor(random() * side),
        156 + Math.floor(random() * 24),
        2 + Math.floor(random() * 8),
        1,
        i % 4 ? "#31454a" : "#53686c",
      );
    for (const [x, y, n] of [
      [25, 159, 9],
      [33, 157, 6],
      [12, 161, 12],
      [178, 157, 5],
      [190, 160, 10],
      [198, 160, 13],
    ])
      pine(x, y, n, "#192730");
    const world = sky.getImageData(0, 0, side, side),
      rim = shell.getContext("2d").createImageData(side, side);
    const colors = [
      "142029",
      "43545b",
      "76858b",
      "a9b9b8",
      "c5d3cf",
      "e2e8de",
      "526167",
      "647578",
      "222f37",
      "9aacae",
    ].map((h) => [1, 3, 5].map((i) => parseInt(("#" + h).slice(i, i + 2), 16)));
    for (let y = 0; y < side; y++)
      for (let x = 0; x < side; x++) {
        const dx = x + 0.5 - center,
          dy = y + 0.5 - center,
          d = Math.hypot(dx, dy),
          j = (y * side + x) * 4;
        if (d < radius) continue;
        world.data[j + 3] = 0;
        if (d > 105) continue;
        const light = dx * 0.48 + dy * 0.82;
        let k;
        if (d > 103.5) k = 0;
        else if (d > 101.5) k = light < 0 ? 4 : 1;
        else if (d > 98.5) k = light < -34 ? 5 : light < 15 ? 3 : 2;
        else if (d > 97) k = light < -30 ? 2 : 7;
        else k = 8;
        const c = colors[k];
        rim.data[j] = c[0];
        rim.data[j + 1] = c[1];
        rim.data[j + 2] = c[2];
        rim.data[j + 3] = 255;
      }
    sky.putImageData(world, 0, 0);
    shell.getContext("2d").putImageData(rim, 0, 0);
  }
  buildLandscape();
  function atFaceResolution(image) {
    const result = blank(faceSide, faceSide),
      ctx = result.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(image, 0, 0, faceSide, faceSide);
    return result;
  }
  const facePlate = atFaceResolution(plate),
    faceShell = atFaceResolution(shell);
  const disc = blank(faceSide, faceSide),
    mask = disc.getContext("2d");
  for (let y = 0; y < faceSide; y++)
    for (let x = 0; x < faceSide; x++) {
      if (Math.hypot(x + 0.5 - faceCenter, y + 0.5 - faceCenter) < faceRadius)
        mask.fillRect(x, y, 1, 1);
    }

  const letters = {
    ">": "100010001010100",
    _: "000000000000111",
    "%": "101001010100101",
    "~": "000010101000000",
    "-": "000000111000000",
    Q: "010101101111011",
    A: "010101111101101",
    B: "110101110101110",
    C: "011100100100011",
    D: "110101101101110",
    E: "111100110100111",
    F: "111100110100100",
    G: "011100101101011",
    H: "101101111101101",
    I: "111010010010111",
    K: "101101110101101",
    L: "100100100100111",
    M: "101111111101101",
    N: "101111111111101",
    O: "010101101101010",
    P: "110101110100100",
    R: "110101110101101",
    S: "011100010001110",
    T: "111010010010010",
    U: "101101101101111",
    V: "101101101101010",
    W: "101101111111101",
    Y: "101101010010010",
    X: "101101010101101",
    Z: "111001010100111",
    0: "111101101101111",
    1: "010110010010111",
    2: "110001010100111",
    3: "110001010001110",
    4: "101101111001001",
    5: "111100110001110",
    6: "011100110101010",
    7: "111001010010010",
    8: "010101010101010",
    9: "010101011001110",
    ":": "000010000010000",
    " ": "000000000000000",
  };
  function label(ctx, word, cx, y, color) {
    let x = Math.round(cx - (word.length * 4 - 1) / 2);
    for (const letter of word) {
      const glyph = letters[letter] || letters[" "];
      for (let p = 0; p < 15; p++)
        if (glyph[p] === "1")
          rect(ctx, x + (p % 3), y + Math.floor(p / 3), 1, 1, color);
      x += 4;
    }
  }
  // Native 12px Fusion Pixel glyphs, under the same OFL as the bundled font.
  const han = {
    剩: [1151, 1288, 1407, 1322, 1387, 1322, 1387, 1304, 1068, 1099, 1544],
    余: [32, 80, 396, 1787, 32, 32, 1022, 32, 292, 1571, 48],
    额: [1992, 319, 2021, 1118, 1363, 1356, 1395, 1374, 274, 658, 1118],
    度: [64, 2046, 274, 2046, 274, 498, 2, 1018, 274, 226, 1821],
    上: [32, 32, 32, 32, 992, 32, 32, 32, 32, 32, 2047],
    下: [2047, 32, 32, 32, 160, 288, 544, 32, 32, 32, 32],
    文: [16, 32, 2047, 260, 260, 136, 136, 80, 32, 216, 1799],
  };
  const numerals = {
    0: [6, 9, 9, 9, 9, 9, 6],
    1: [2, 6, 2, 2, 2, 2, 7],
    2: [6, 9, 1, 2, 4, 8, 15],
    3: [14, 1, 1, 6, 1, 1, 14],
    4: [2, 6, 10, 10, 15, 2, 2],
    5: [15, 8, 8, 14, 1, 1, 14],
    6: [6, 8, 8, 14, 9, 9, 6],
    7: [15, 1, 2, 2, 4, 4, 4],
    8: [6, 9, 9, 6, 9, 9, 6],
    9: [6, 9, 9, 7, 1, 1, 6],
    "%": [9, 9, 2, 2, 4, 9, 9],
    "-": [0, 0, 0, 14, 0, 0, 0],
    "~": [0, 0, 5, 10, 0, 0, 0],
  };
  function instrument(ctx, telemetry, code) {
    const mint = "#a6ddc8",
      lilac = "#cec3e8";
    label(
      ctx,
      ">_ " + (code === "IDLE" ? "CODEX" : code) + " " + telemetry.mode,
      faceCenter,
      16,
      "#c7dbd4",
    );
    rect(ctx, 0, 149, faceSide, 35, "#15222b");
    rect(ctx, 31, 149, 122, 1, "#50616a");
    rect(ctx, 92, 152, 1, 13, "#344650");
    function meter(word, x, numberX, barX, value, color) {
      const left = x - word.length * 6;
      for (let i = 0; i < word.length; i++) {
        han[word[i]].forEach((bits, y) => {
          for (let dx = 0; dx < 12; dx++)
            if (bits & (1 << dx))
              rect(ctx, left + 12 * i + dx, 151 + y, 1, 1, "#b4c2c7");
        });
      }
      let pen = numberX - Math.floor((5 * value.text.length - 1) / 2);
      for (const ch of value.text) {
        numerals[ch].forEach((bits, y) => {
          for (let dx = 0; dx < 4; dx++)
            if (bits & (8 >> dx))
              rect(
                ctx,
                pen + dx,
                163 + y,
                1,
                1,
                value.percent === null ? "#86969d" : color,
              );
        });
        pen += 5;
      }
      rect(ctx, barX, 149, 36, 1, "#405059");
      if (value.percent !== null)
        rect(ctx, barX, 149, Math.round((36 * value.percent) / 100), 1, color);
      else
        for (let i = 2; i < 36; i += 5)
          rect(ctx, barX + i, 149, 1, 1, "#65747b");
    }
    meter("剩余额度", 68, 77, 49, telemetry.quota, mint);
    meter("上下文", 114, 107, 98, telemetry.context, lilac);
  }
  const icons = {
    keys: [
      "..............",
      "..oooooooooo..",
      ".ovvvvvvvvvvo.",
      ".owovvvvvvvvo.",
      ".ovwovvvvvvvo.",
      ".owovvvggvgvo.",
      ".ovvvvvvvvvvo.",
      "..oooooooooo..",
    ],
    lens: [
      "....oooo......",
      "..oowwwwoo....",
      "..owwvvvwo....",
      ".owwvvvvvwo...",
      ".owvvvvvvwo...",
      "..owvvvvwo....",
      "..oowwwwoo....",
      "....ooooopo...",
      ".........oppo.",
      "..........ooo.",
    ],
    cheek: [
      "....oooooo....",
      "..oowwwwwwoo..",
      ".owwwwwwwwwwo.",
      ".owwowwwowwwo.",
      ".owwpwwwwppwo.",
      "..owwwowwwwo..",
      "...ooowwooo...",
      "..............",
      "..........oo..",
      ".........oppo.",
      "......oooppo..",
      ".....oppppo...",
      "......oooo....",
    ],
    moon: [
      "......ooo.....",
      "....ogwgo.....",
      "...ogwgo......",
      "..ogwgo.......",
      "..ogwgo....g..",
      "..owwgo...gwg.",
      "..owwwgo...g..",
      "..ogwwwoo.....",
      "...ogwwwwwooo.",
      "....ogwwwwwgo.",
      ".....ooggggo..",
      ".......oooo...",
    ],
    book: [
      ".oooo..oooo...",
      ".owwwoowggwo..",
      ".owwwowgwwwo..",
      ".owwwowwwgwo..",
      ".owggowgwwwo..",
      ".owwwowwwgwo..",
      ".owggowgwwwo..",
      ".owwwowwwwwo..",
      ".oooowwooooo..",
      ".ovvvoovvvvo..",
      "..ooooooooo...",
    ],
    star: [
      "......w.......",
      ".....wvw......",
      ".....vvv......",
      "....vvwvv.....",
      "...vwwwwvv....",
      ".vvwwvvwwwvv..",
      "vvwwvggvwwwvv.",
      "..vvwggwwvv...",
      "...vvwwvv.....",
      "....vvwv......",
      ".....vv.......",
      ".....v........",
    ],
    hand: [
      ".....oo.......",
      "....opp.o.....",
      "....oppopo....",
      "...ooppopo.o..",
      "..opoppoppopo.",
      "..oppwppppopo.",
      "..oppppppppo..",
      "o.oppppppppo..",
      "opoppppppppo..",
      ".oppppppppo...",
      ".oppppppppo...",
      "..oppppppo....",
      "...ovvvvo.....",
      "...ovvvvo.....",
      "....oooo......",
    ],
    candy: [
      "..oo......oo..",
      ".ovvo....ovvo.",
      ".ovwvo..ovwvo.",
      "..ovvoooovvo..",
      "...oowwwwoo...",
      "...ovwwwvvvo..",
      "..ovwwvvvvvo..",
      "..ovwvvwwvvo..",
      "..ovvvwwvvvo..",
      "...ovvvvvvo...",
      "...oovvvoo....",
      "..ovvoooovvo..",
      ".ovwvo..ovwvo.",
      "..ooo....ooo..",
    ],
    nose: [
      ".......ooo....",
      "......opppo...",
      ".....oppppo...",
      ".....opppo....",
      "....oppppo....",
      ".ooooppppo....",
      "opppopppoooo..",
      ".oppppppppppo.",
      "..opppppppppo.",
      "..opppppppppo.",
      "...opppppppo..",
      "....ovvvvvo...",
      "....oooooo....",
    ],
    comet: [
      ".........g....",
      "........gwg...",
      "......ggwwwgg.",
      ".....gwwwgwwwg",
      "......ggwwwgg.",
      "...v....gwg...",
      "..vwv....g....",
      ".vwv....v.....",
      "..v..v.v......",
      "....vwv.......",
      "...vwv........",
      "....v.........",
    ],
  };
  for (const ico of document.querySelectorAll("[data-icon]")) {
    const ctx = ico.getContext("2d"),
      pattern = icons[ico.dataset.icon],
      inks = {
        o: "#47364e",
        v: "#9976b2",
        w: "#fff0d4",
        p: "#e7b69d",
        g: "#ac7b55",
      };
    pattern.forEach((row, y) =>
      [...row].forEach((s, x) => {
        if (inks[s]) rect(ctx, x, y, 1, 1, inks[s]);
      }),
    );
  }

  class Display {
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = canvas.getContext("2d");
      this.buffer = blank(faceSide, faceSide);
      this.paint = this.buffer.getContext("2d");
      this.zoom = 0;
      this.overlay = null;
    }
    resize(space) {
      const dpr = Math.min(window.devicePixelRatio || 1, 3),
        room = Math.min(555, Math.max(140, space - 22));
      const zoom = Math.max(2, Math.floor((room * dpr) / faceSide)),
        css = Math.min(room, (faceSide * zoom) / dpr);
      this.canvas.style.width = css + "px";
      if (zoom === this.zoom) return;
      this.zoom = zoom;
      this.canvas.width = this.canvas.height = faceSide * zoom;
      const layer = blank(faceSide * zoom, faceSide * zoom),
        g = layer.getContext("2d");
      const strength = zoom <= 2 ? 0.14 : zoom === 3 ? 0.19 : 0.24;
      g.fillStyle = `rgba(75,91,101,${strength})`;
      for (let n = 0; n < faceSide * zoom; n += zoom) {
        g.fillRect(n, 0, 1, faceSide * zoom);
        g.fillRect(0, n, faceSide * zoom, 1);
      }
      g.globalCompositeOperation = "destination-in";
      g.imageSmoothingEnabled = false;
      g.drawImage(disc, 0, 0, layer.width, layer.height);
      g.clearRect(0, 149 * zoom, layer.width, layer.height);
      g.clearRect(0, 0, layer.width, 22 * zoom);
      this.overlay = layer;
    }
    draw({
      sheet,
      hands,
      clip,
      frame,
      view,
      grid,
      code,
      telemetry,
      tick,
      motion,
      action,
      sparks,
    }) {
      if (!this.zoom) return;
      const ctx = this.paint;
      ctx.clearRect(0, 0, faceSide, faceSide);
      ctx.drawImage(facePlate, 0, 0);
      if (motion) {
        const phase = Math.floor(tick / 420) % 8;
        if (phase === 1 || phase === 2)
          shine(ctx, 45, 40, "#dcbed9", phase === 1 ? 1 : 2);
      }
      if (sheet) {
        const tile = frame + (clip.offset || 0);
        const r =
          Math.floor(tile / 8) +
          (view === "close" && !clip.paired ? Math.ceil(clip.frames / 8) : 0);
        const column = (tile % 8) + (clip.paired && view === "close" ? 8 : 0);
        ctx.drawImage(
          sheet,
          column * W,
          r * H,
          W,
          H,
          originX,
          originY(view),
          W,
          H,
        );
        if (hands) {
          const row =
            Math.floor(hands.frame / 8) +
            (view === "close" ? Math.ceil(hands.clip.frames / 8) : 0);
          ctx.drawImage(
            hands.sheet,
            (hands.frame % 8) * W,
            row * H,
            W,
            H,
            originX,
            originY(view),
            W,
            H,
          );
        }
        this.expression(ctx, clip, frame, view, action, motion, sparks);
      }
      instrument(ctx, telemetry, code);
      ctx.globalCompositeOperation = "destination-in";
      ctx.drawImage(disc, 0, 0);
      ctx.globalCompositeOperation = "source-over";
      ctx.drawImage(faceShell, 0, 0);
      this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
      this.ctx.imageSmoothingEnabled = false;
      this.ctx.drawImage(
        this.buffer,
        0,
        0,
        this.canvas.width,
        this.canvas.height,
      );
      if (grid) this.ctx.drawImage(this.overlay, 0, 0);
    }
    expression(ctx, clip, frame, view, id, motion, sparks) {
      const [ox, oy] =
        view === "full" ? [28, 12] : clip.closeOrigin || [113, 34.5];
      const step = view === "full" ? 4 : 2.75;
      const at = (x, y) => [
        Math.round(
          originX +
            (x - ox) / step -
            (view === "close" ? clip.closePan?.[frame] || 0 : 0),
        ),
        Math.round(originY(view) + (y - oy) / step),
      ];
      const a = clip.anchors[frame];
      if (id.startsWith("cheek") && frame >= 5 && frame <= 17) {
        const side = id === "cheekLeft" ? -1 : 1;
        const [x, y] = at(
          a[2] + side * (91 + Math.min(frame - 5, 8)),
          a[3] - 20 - (frame - 5) * 2,
        );
        rect(ctx, x, y, 2, 1, "#dea2ba");
        rect(ctx, x + 3, y, 2, 1, "#dea2ba");
        rect(ctx, x, y + 1, 5, 1, "#efbfce");
        rect(ctx, x + 1, y + 2, 3, 1, "#e7acbf");
        rect(ctx, x + 2, y + 3, 1, 1, "#c389a9");
      } else if (id === "doze" && frame >= 15) {
        const lift = motion ? Math.floor((frame - 15) / 5) : 1;
        const [x, y] = at(a[0] + 78 + lift * 2, a[1] - 36 - lift * 4);
        label(ctx, "Z", x, y, "#cebddf");
      } else if (id === "brush") {
        for (const spark of sparks) {
          const [x, y] = at(spark.x, spark.y);
          shine(
            ctx,
            x,
            y - 2 - Math.floor(spark.age / 90),
            spark.age < 210 ? "#fff2be" : "#d6a8c3",
            spark.age < 210 ? 1 : 0,
          );
        }
      } else if (id === "reading" || id === "readingMoon") {
        const [x, y] = at(...clip.book[frame]);
        rect(ctx, x + 3, y - 2, 2, 1, "#ffe9a6");
        rect(ctx, x + 4, y - 1, 1, 2, "#ffe9a6");
      } else if (
        clip.target &&
        ["lensFind", "palmTap", "keyChime"].includes(id)
      ) {
        const [x, y] = at(...clip.target[frame]);
        if (id === "keyChime") {
          for (const beat of [4, 14]) {
            const rise = frame - beat;
            if (rise < 0 || rise > 8) continue;
            rect(ctx, x + 5, y - 4 - rise, 1, 3, "#a5e6ce");
            rect(ctx, x + 6, y - 4 - rise, 2, 1, "#a5e6ce");
            rect(ctx, x + 3, y - 2 - rise, 2, 2, "#ffe6aa");
          }
        } else if (id === "lensFind" && frame >= 7 && frame < 23) {
          const lift = Math.floor((frame - 7) / 3);
          shine(ctx, x + 4 + lift, y - 4 - lift, "#ffe7aa", frame < 18 ? 2 : 1);
          if (frame > 11 && frame < 18)
            rect(ctx, x + 3, y - 7 - lift, 1, 1, "#e0cfed");
        } else if (id === "palmTap" && frame >= 5 && frame < 12) {
          const step = Math.floor((frame - 5) / 2);
          for (const [dx, dy] of [
            [-1, 0],
            [-1, -1],
            [0, -1],
          ])
            rect(
              ctx,
              x + dx * (4 + step),
              y + dy * (4 + step),
              dx ? 2 : 1,
              dy ? 2 : 1,
              "#ffe9ad",
            );
        }
      }
    }
    sourcePoint(event, view, clip, frame, unbounded = false) {
      const box = this.canvas.getBoundingClientRect();
      const screenX = ((event.clientX - box.left) / box.width) * faceSide,
        screenY = ((event.clientY - box.top) / box.height) * faceSide;
      const x = screenX - originX,
        y = screenY - originY(view);
      const inside =
        Math.hypot(screenX - faceCenter, screenY - faceCenter) < faceRadius &&
        x >= 0 &&
        x < W &&
        y >= 0 &&
        y < H &&
        screenY >= 23 &&
        screenY < 149;
      if (!inside && !unbounded) return null;
      const [ox, oy] = clip.closeOrigin || [113, 34.5];
      const pan = clip.closePan?.[frame] || 0;
      return view === "full"
        ? { x: 28 + x * 4, y: 12 + y * 4, inside }
        : { x: ox + (x + pan) * 2.75, y: oy + y * 2.75, inside };
    }
  }
  return Display;
})();
