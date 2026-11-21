"use strict";

window.PixelBadge = (() => {
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
  const facePlate = blank(faceSide, faceSide);
  const faceShell = blank(faceSide, faceSide);
  function rect(ctx, x, y, w, h, color) {
    ctx.fillStyle = color;
    ctx.fillRect(x, y, w, h);
  }
  function shine(ctx, x, y, color, size = 2) {
    rect(ctx, x, y - size, 1, 2 * size + 1, color);
    rect(ctx, x - size, y, 2 * size + 1, 1, color);
  }
  function ridge(ctx, points, bottom, color) {
    ctx.fillStyle = color;
    for (let i = 1; i < points.length; i++) {
      const [a, b] = points[i - 1],
        [x, y] = points[i];
      for (let col = a; col < x; col++) {
        const top = Math.round(b + ((y - b) * (col - a)) / (x - a));
        ctx.fillRect(col, top, 1, bottom - top);
      }
    }
  }
  function pine(ctx, x, base, h, shade, light) {
    const top = base - h;
    rect(ctx, x, top, 1, h, shade);
    for (let tier = 1; tier <= 4; tier++) {
      const end = Math.round(h * (0.09 + 0.2 * tier));
      const length = Math.round(h * (0.13 + 0.04 * tier));
      for (let row = 0; row < length; row++) {
        const half = Math.floor((row + 1) * (0.45 + 0.075 * tier));
        rect(ctx, x - half, top + end - length + row, half * 2 + 1, 1, shade);
      }
      if (h > 16) rect(ctx, x - 1, top + end - length + 2, 1, 2, light);
    }
  }
  function buildLandscape() {
    const ctx = facePlate.getContext("2d");
    // A few stepped bands between the indigo zenith and the rose horizon.
    const stops = [
      [0, "25223c"],
      [38, "302943"],
      [66, "48405e"],
      [84, "705773"],
      [100, "bd8493"],
      [117, "deaaab"],
    ];
    for (let band = 1; band < stops.length; band++) {
      const [y0, low] = stops[band - 1],
        [y1, high] = stops[band];
      const rgb = [0, 2, 4].map((i) => [
        parseInt(low.slice(i, i + 2), 16),
        parseInt(high.slice(i, i + 2), 16),
      ]);
      for (let y = y0; y < y1; y++) {
        const t = (Math.floor((y - y0) / 3) * 3) / (y1 - y0);
        const color = rgb.map(([a, b]) => Math.round(a + (b - a) * t));
        rect(ctx, 0, y, faceSide, 1, "rgb(" + color.join(",") + ")");
      }
    }
    let seed = 8041;
    const random = () =>
      (seed = (Math.imul(1664525, seed) + 1013904223) >>> 0) / 4294967296;
    for (let i = 0; i < 104; i++) {
      const x = Math.floor(random() * faceSide),
        y = 11 + Math.floor(random() * 82);
      if (y < 23 && Math.abs(x - faceCenter) < 35) continue;
      rect(ctx, x, y, 1, 1, ["#94839e", "#be9fab", "#cec1cc"][i % 3]);
    }
    for (const [x, y] of [
      [42, 43],
      [32, 65],
      [148, 76],
      [124, 33],
    ])
      shine(ctx, x, y, "#e8c9ce", 1);
    // The moon has a lit limb and a darker underside, all on the display grid.
    for (let dy = -8; dy <= 8; dy++)
      for (let dx = -8; dx <= 8; dx++) {
        if (dx * dx + dy * dy <= 67 && (dx - 3) ** 2 + (dy + 3) ** 2 >= 60)
          rect(
            ctx,
            149 + dx,
            58 + dy,
            1,
            1,
            dy > 5 ? "#dbab99" : dx < -6 ? "#f2c59f" : "#f7dfb3",
          );
      }
    for (const [x, y, w] of [
      [11, 81, 31],
      [24, 78, 12],
      [141, 83, 25],
      [153, 80, 16],
      [11, 89, 14],
      [130, 88, 14],
    ]) {
      rect(ctx, x, y, w, 1, "#ad8498");
      rect(ctx, x + 3, y + 2, Math.ceil(w / 2), 1, "#906f88");
    }
    ridge(
      ctx,
      [
        [0, 97],
        [16, 93],
        [23, 96],
        [42, 80],
        [55, 90],
        [66, 83],
        [88, 100],
        [104, 89],
        [119, 97],
        [145, 79],
        [162, 94],
        [173, 88],
        [184, 91],
      ],
      119,
      "#736080",
    );
    ridge(
      ctx,
      [
        [0, 109],
        [17, 103],
        [28, 106],
        [49, 93],
        [67, 105],
        [82, 102],
        [99, 112],
        [123, 96],
        [138, 100],
        [150, 89],
        [169, 105],
        [184, 98],
      ],
      122,
      "#544660",
    );
    // The little dome and its two lit windows remain visible left of the dragon.
    rect(ctx, 22, 93, 16, 12, "#40384e");
    rect(ctx, 23, 89, 14, 5, "#5d4b67");
    rect(ctx, 25, 86, 10, 4, "#756179");
    rect(ctx, 27, 85, 6, 2, "#a58490");
    rect(ctx, 29, 82, 1, 3, "#c3a0a0");
    rect(ctx, 31, 87, 2, 5, "#453c55");
    rect(ctx, 20, 94, 20, 2, "#9c7885");
    rect(ctx, 24, 99, 2, 3, "#e4b596");
    rect(ctx, 33, 99, 2, 3, "#e4b596");
    ridge(
      ctx,
      [
        [0, 112],
        [19, 103],
        [33, 105],
        [48, 101],
        [72, 116],
        [99, 110],
        [126, 113],
        [152, 106],
        [166, 100],
        [184, 106],
      ],
      121,
      "#473e57",
    );
    rect(ctx, 0, 115, faceSide, 28, "#745f78");
    rect(ctx, 0, 118, faceSide, 5, "#826a80");
    for (let i = 0; i < 89; i++) {
      const x = Math.floor(random() * faceSide),
        y = 116 + Math.floor(random() * 19);
      rect(
        ctx,
        x,
        y,
        2 + Math.floor(random() * 7),
        1,
        i % 3 ? "#6a5874" : "#ab8391",
      );
    }
    for (const [x, y, h] of [
      [10, 127, 23],
      [18, 128, 16],
      [23, 123, 12],
      [34, 119, 9],
      [159, 119, 10],
      [168, 124, 17],
      [176, 128, 24],
    ])
      pine(ctx, x, y, h, "#3d374e", "#57475b");
    ridge(
      ctx,
      [
        [0, 122],
        [15, 126],
        [35, 128],
        [55, 135],
        [75, 138],
        [104, 139],
        [135, 135],
        [150, 129],
        [163, 129],
        [184, 119],
      ],
      177,
      "#4b3e54",
    );
    ridge(
      ctx,
      [
        [0, 132],
        [12, 129],
        [28, 135],
        [48, 139],
        [80, 144],
        [112, 143],
        [143, 137],
        [169, 133],
        [184, 129],
      ],
      180,
      "#393246",
    );
    for (const [x, y, h] of [
      [12, 139, 22],
      [23, 136, 12],
      [29, 132, 9],
      [151, 131, 10],
      [163, 137, 14],
      [172, 139, 20],
    ])
      pine(ctx, x, y, h, "#322e42", "#534155");
    for (const [x, y] of [
      [36, 135],
      [40, 137],
      [144, 134],
      [150, 132],
    ]) {
      rect(ctx, x, y, 1, 3, "#786674");
      rect(ctx, x, y - 1, 2, 1, "#d2a0a4");
    }
    // Small stepped contact shadow where the skirt meets the near bank.
    rect(ctx, 76, 143, 33, 1, "#3e3247");
    rect(ctx, 69, 144, 45, 1, "#342c41");
    rect(ctx, 75, 145, 34, 1, "#2e283b");
    const rimCtx = faceShell.getContext("2d");
    const rim = rimCtx.createImageData(faceSide, faceSide);
    const enamel = [
      "342d41",
      "66546d",
      "95869a",
      "bba9bb",
      "dbccce",
      "f5e4db",
      "876e88",
      "44384c",
    ].map((hex) => [0, 2, 4].map((i) => parseInt(hex.slice(i, i + 2), 16)));
    for (let y = 0; y < faceSide; y++)
      for (let x = 0; x < faceSide; x++) {
        const dx = x + 0.5 - faceCenter,
          dy = y + 0.5 - faceCenter,
          d = Math.hypot(dx, dy);
        if (d < faceRadius || d > 89.5) continue;
        const light = 0.48 * dx + 0.82 * dy;
        let tone;
        if (d > 88.5) tone = 0;
        else if (d > 86.9) tone = light < 0 ? 4 : 1;
        else if (d > 84.2) tone = light < -28 ? 5 : light < 18 ? 3 : 2;
        else if (d > 83) tone = light < -24 ? 2 : 6;
        else tone = 7;
        const k = (y * faceSide + x) * 4;
        rim.data.set(enamel[tone], k);
        rim.data[k + 3] = 255;
      }
    rimCtx.putImageData(rim, 0, 0);
  }
  buildLandscape();
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
    "?": "010101001010000",
    "!": "010010010000010",
    ".": "000000000000010",
    "+": "000010111010000",
    ",": "000000000010100",
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
    rect(ctx, 0, 149, faceSide, 35, "#252331");
    rect(ctx, 31, 149, 122, 1, "#615266");
    rect(ctx, 92, 152, 1, 13, "#494151");
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
      const strength = zoom <= 2 ? 0.15 : zoom === 3 ? 0.18 : 0.21;
      g.fillStyle = `rgba(149,129,155,${strength})`;
      // Paint the mesh once so the intersections do not stamp dark dots into the eyes.
      g.beginPath();
      for (let n = 0; n < faceSide * zoom; n += zoom) {
        g.rect(n, 0, 1, faceSide * zoom);
        g.rect(0, n, faceSide * zoom, 1);
      }
      g.fill();
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
      starToy,
      contact,
      emotes = [],
      bob = 0,
    }) {
      if (!this.zoom) return;
      const ctx = this.paint;
      ctx.clearRect(0, 0, faceSide, faceSide);
      ctx.drawImage(facePlate, 0, 0);
      if (motion) {
        const phase = Math.floor(tick / 420) % 8;
        if (phase === 1 || phase === 2)
          shine(ctx, 42, 43, "#eed1d3", phase === 1 ? 1 : 2);
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
          originY(view) + bob,
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
            originY(view) + bob,
            W,
            H,
          );
        }
        this.expression(ctx, clip, frame, view, action, motion, sparks, bob);
        this.emotes(ctx, clip, frame, view, emotes, bob, motion);
      }
      if (starToy || contact) {
        ctx.save();
        ctx.beginPath();
        ctx.rect(0, 23, faceSide, 126);
        ctx.clip();
        starToy?.draw(ctx);
        if (contact) this.contactMark(ctx, contact, motion);
        ctx.restore();
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
      if (grid) {
        this.ctx.globalCompositeOperation = "multiply";
        this.ctx.drawImage(this.overlay, 0, 0);
        this.ctx.globalCompositeOperation = "source-over";
      }
    }
    facePoint(view, clip, frame, x, y) {
      const [ox, oy] =
        view === "full" ? [28, 12] : clip.closeOrigin || [113, 34.5];
      const step = view === "full" ? 4 : 2.75;
      return {
        sx:
          originX +
          (x - ox) / step -
          (view === "close" ? clip.closePan?.[frame] || 0 : 0),
        sy: originY(view) + (y - oy) / step,
      };
    }
    contactMark(ctx, { sx, sy, part, age, phase }, motion) {
      if (
        !Number.isFinite(sx) ||
        !Number.isFinite(sy) ||
        !part ||
        part === "outside"
      )
        return;
      const x = Math.round(sx),
        y = Math.round(sy);
      const pressed = phase === "down";
      if (!pressed && (age >= 260 || !motion)) return;
      // Marks start on the touched pixel. Leave the middle clear for the feature being touched.
      const advance = motion ? Math.min(3, Math.floor(age / 55)) : 0;
      const r = pressed ? (advance ? 3 : 4) : 3 + advance;
      const tool = [
        "book",
        "palmTap",
        "keyChime",
        "lensFind",
        "bubblePop",
      ].includes(part);
      const light = tool ? "#d9efd5" : "#ffe0cf";
      const ink = tool ? "#536e70" : "#8a576d";
      if (pressed) {
        rect(ctx, x - r - 1, y, 2, 1, ink);
        rect(ctx, x - r, y - 1, 1, 1, light);
        rect(ctx, x + r, y, 2, 1, ink);
        rect(ctx, x + r, y - 1, 1, 1, light);
      } else {
        for (const sign of [-1, 1]) {
          rect(ctx, x + sign * r, y - 1 - advance, 1, 1, light);
          if (advance < 3)
            rect(ctx, x + sign * (r + 1), y - advance, 1, 1, ink);
        }
      }
    }
    expression(ctx, clip, frame, view, id, motion, sparks, bob = 0) {
      const at = (x, y) => {
        const pos = this.facePoint(view, clip, frame, x, y);
        return [Math.round(pos.sx), Math.round(pos.sy) + bob];
      };
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
    emotes(ctx, clip, frame, view, events, bob, motion) {
      if (!events?.length || !clip?.anchors?.[frame]) return;
      const a = clip.anchors[frame];
      const at = (x, y) => {
        const pos = this.facePoint(view, clip, frame, x, y);
        return [Math.round(pos.sx), Math.round(pos.sy) + bob];
      };
      for (const e of events) {
        const age = e.age || 0,
          life = e.life || 900;
        if (age >= life) continue;
        const side = e.side || 1;
        let x = a[0] + side * 38,
          y = a[1] - 14;
        if (e.kind === "heart") {
          x = a[0] + side * 22;
          y = a[1] - 28;
        } else if (e.kind === "sparkle") {
          x = a[0] + side * 30;
          y = a[1] - 24;
        } else if (e.kind === "ellipsis") {
          x = a[0] + side * 40;
          y = a[1] - 12;
        } else if (e.kind === "dots") {
          x = a[0] + side * 30;
          y = a[1] - 28;
        }
        if (e.bubble) {
          x = a[0] + side * 46;
          y = a[1] - 30;
        }
        if (e.zone === "cheek") {
          x = a[2] + side * 24;
          y = a[3] - 8;
        } else if (e.zone === "wing") {
          const left = side < 0;
          x = left ? a[4] : a[6];
          y = (left ? a[5] : a[7]) - 22;
        } else if (e.zone === "tail") {
          x = a[8];
          y = a[9] - 24;
        } else if (e.zone === "nose") {
          x = a[2];
          y = a[3] - 14;
        } else if (e.zone === "hand") {
          const palms = clip.palms?.[frame];
          if (palms) {
            const left = side < 0;
            x = left ? palms[0] : palms[2];
            y = (left ? palms[1] : palms[3]) - 16;
          } else {
            x = a[2] + side * 32;
            y = a[3] + 14;
          }
        } else if (e.zone === "book") {
          const book = clip.book?.[frame];
          if (book) {
            [x, y] = book;
            y -= 12;
          } else {
            x = a[2];
            y = a[3] + 18;
          }
        } else if (e.zone === "lens") {
          const target = clip.target?.[frame];
          if (target) {
            [x, y] = target;
            y -= 16;
          } else {
            x = a[6];
            y = a[7] - 20;
          }
        }
        let [sx, sy] = at(x, y);
        sx = Math.max(20, Math.min(164, sx));
        sy = Math.max(25, Math.min(145, sy));
        sy -= motion ? Math.min(4, Math.floor(age / 110)) : 0;
        ctx.save();
        ctx.globalAlpha = motion
          ? Math.max(0, Math.min(1, Math.min(age / 120, (life - age) / 180)))
          : 1;
        this.drawEmote(ctx, e.kind, sx, sy, e);
        ctx.restore();
      }
    }

    drawEmote(ctx, kind, x, y, event) {
      if (event?.bubble) {
        rect(ctx, x - 1, y - 1, 13, 13, "#dfd4e7");
        rect(ctx, x, y, 11, 11, "#352f41");
        if (kind === "question") label(ctx, "?", x + 4, y + 3, "#ffe9a6");
        else if (kind === "exclaim") label(ctx, "!", x + 4, y + 3, "#ffd4d5");
        else {
          rect(ctx, x + 3, y + 5, 1, 1, "#e1d1f2");
          rect(ctx, x + 5, y + 5, 1, 1, "#e1d1f2");
          rect(ctx, x + 7, y + 5, 1, 1, "#e1d1f2");
        }
        return;
      }
      if (kind === "heart") {
        const pattern = [".o.o.", "owogo", "ogggg", ".ogo."];
        const colors = { o: "#6f4a62", w: "#fff2e8", g: "#e8849b" };
        pattern.forEach((row, dy) =>
          [...row].forEach((ink, dx) => {
            if (colors[ink])
              rect(ctx, x + dx - 2, y + dy - 2, 1, 1, colors[ink]);
          }),
        );
      } else if (kind === "sparkle") {
        shine(ctx, x, y, "#ffe9a6", 2);
        shine(ctx, x + 5, y - 3, "#d6a8c3", 1);
      } else if (kind === "question") {
        label(ctx, "?", x + 1, y + 1, "#352f41");
        label(ctx, "?", x, y, "#ffe9a6");
      } else if (kind === "exclaim") {
        label(ctx, "!", x + 1, y + 1, "#352f41");
        label(ctx, "!", x, y, "#ffd4d5");
      } else if (kind === "ellipsis") {
        rect(ctx, x + 1, y + 1, 1, 1, "#352f41");
        rect(ctx, x + 4, y + 1, 1, 1, "#352f41");
        rect(ctx, x + 7, y + 1, 1, 1, "#352f41");
        rect(ctx, x, y, 1, 1, "#e1d1f2");
        rect(ctx, x + 3, y, 1, 1, "#e1d1f2");
        rect(ctx, x + 6, y, 1, 1, "#e1d1f2");
      } else if (kind === "sweat") {
        rect(ctx, x, y, 2, 4, "#9bcbbd");
        rect(ctx, x + 1, y - 1, 1, 2, "#c5e7d4");
        rect(ctx, x, y + 3, 2, 1, "#587b90");
      } else if (kind === "note") {
        rect(ctx, x, y, 1, 5, "#cec3e8");
        rect(ctx, x - 2, y + 4, 3, 2, "#cec3e8");
        rect(ctx, x + 3, y + 1, 1, 4, "#a095ac");
        rect(ctx, x + 2, y + 4, 2, 1, "#a095ac");
      } else if (kind === "blush") {
        rect(ctx, x - 2, y, 4, 1, "#efbfce");
        rect(ctx, x - 1, y + 1, 3, 1, "#e7acbf");
      } else if (kind === "dots") {
        rect(ctx, x - 2, y + 1, 1, 1, "#352f41");
        rect(ctx, x + 1, y, 1, 1, "#352f41");
        rect(ctx, x + 4, y + 1, 1, 1, "#352f41");
        rect(ctx, x - 3, y, 1, 1, "#e1d1f2");
        rect(ctx, x, y - 1, 1, 1, "#e1d1f2");
        rect(ctx, x + 3, y, 1, 1, "#e1d1f2");
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
      const point =
        view === "full"
          ? { x: 28 + x * 4, y: 12 + y * 4 }
          : { x: ox + (x + pan) * 2.75, y: oy + y * 2.75 };
      return { ...point, sx: screenX, sy: screenY, inside };
    }
  }
  return Display;
})();
