/* Pixel badge working props. Positions are in the same rig units as the solved
   hands; a full-body display pixel covers about five of these units. */
window.BadgeWorkArt = (() => {
  const ink = "#423a49", paper = "#fff0e6", gold = "#ffe9a6";
  const night = "#41536b", mint = "#c5e7d4", teal = "#77a5ab";
  function path(c, geometry, color, outline, thickness=4) {
    const p = new Path2D(geometry);
    if (color) { c.fillStyle=color; c.fill(p); }
    if (outline) { c.strokeStyle=outline; c.lineWidth=thickness; c.stroke(p); }
  }
  function box(c,x,y,w,h,color) {
    c.fillStyle=color; c.fillRect(x,y,w,h);
  }
  function chart(c, moon) {
    path(c,"M9 -7 Q39 -25 72 -27 L70 24 Q39 23 10 35 Z",moon ? night : "#514256");
    if (moon) {
      path(c,"M53 -16 C29 -18 23 10 39 16 Q54 23 64 5 Q49 12 43 2 Q37 -8 53 -16Z",gold);
      box(c,16,16,6,6,mint);
    } else {
      path(c,"M18 19 L32 -1 L46 7 L62 -13",null,"#f0c573",4);
      for (const [x,y] of [[18,19],[32,-1],[46,7],[62,-13]]) {
        box(c,x-5,y-3,10,6,gold); box(c,x-2,y-6,4,12,gold);
      }
    }
  }
  function book(c,left,turn,moon) {
    const at=badgeBookOrigin(left);
    c.save(); c.translate(at.x,at.y); c.rotate(-.05);
    path(c,"M-86 -21 Q-40 -34 0 -15 Q41 -39 87 -31 L87 41 Q38 39 2 59 Q-40 41 -86 46Z",night,ink,6);
    path(c,"M-80 24 Q-39 22 0 43 Q38 23 80 24 L79 36 Q37 33 1 51 Q-38 33 -80 39Z","#dec3c1","#a57579",3);
    path(c,"M-81 -28 Q-36 -34 0 -13 L0 44 Q-38 26 -79 33Z","#f3d6b9","#b89096",3);
    path(c,"M0 -13 Q42 -38 81 -34 L78 33 Q40 27 1 45Z",paper,"#b89096",3);
    // A chapter band and two strokes survive downsampling on the facing page.
    path(c,"M-69 -13 Q-48 -15 -23 -7",null,"#587b90",7);
    path(c,"M-66 2 Q-43 0 -19 9 M-65 15 Q-49 14 -33 19",null,"#a57579",4);
    path(c,"M0 -11 L1 44",null,"#b89096",4);
    chart(c,turn>0 ? !moon : moon);
    if (turn>0 && turn<1) {
      const width=77*Math.cos(Math.PI*turn), curl=badgePageCurl(turn);
      const leaf=new Path2D();
      leaf.moveTo(1,-13); leaf.quadraticCurveTo(width*.52,-28-curl,width,-31-curl);
      leaf.lineTo(width*.96,34-curl); leaf.quadraticCurveTo(width*.44,30-curl,1,45); leaf.closePath();
      c.fillStyle=width>0 ? paper : "#dfc0ac"; c.fill(leaf);
      c.save(); c.clip(leaf);
      if(width>1) {
        c.transform(width/77,-curl/77,0,1,0,0); chart(c,moon);
      } else if(width < -12) {
        path(c,`M${width*.8} ${-6-curl*.8} Q${width*.48} ${-5-curl*.49} ${width*.19} ${1-curl*.2}`,null,"#b89096",4);
      }
      c.restore(); c.strokeStyle="#a57579"; c.lineWidth=3; c.stroke(leaf);
    }
    const tail=6*Math.sin(2*Math.PI*turn)*Math.sin(Math.PI*turn);
    path(c,`M7 43 L20 39 L${21+tail} 67 L${14+tail} 62 L${8+tail} 70Z`,"#d8788c","#78586b",3);
    c.restore();
  }
  function pad(c) {
    path(c,"M-88 -25 L72 -42 L80 -35 L90 32 L-71 55 L-80 48Z","#587b90",ink,5);
    path(c,"M-77 35 L80 18 L84 30 L-72 47Z","#dec3c1","#a57579",3);
    path(c,"M-83 -24 L72 -39 L82 22 L-73 42Z",paper,"#b89096",3);
    path(c,"M-58 -23 L-47 38",null,"#d8788c",4);
    // Two dark ring ends with highlights, separated from her left thumb.
    for(const y of [-17,8,31]){
      path(c,`M-67 ${y} L-80 ${y+2} Q-90 ${y+3} -86 ${y+12} L-75 ${y+13}`,null,ink,6);
      box(c,-84,y+2,8,3,"#cdc0d5");
    }
    path(c,"M60 -37 L72 -39 L75 -23 Z","#f0c573","#b89096",2.5);
    path(c,"M-34 27 L37 18",null,"#dec3c1",3);
  }
  function quill(c) {
    // The original writing solver places the nib tip at local (-5,8).
    path(c,"M-3 -6 Q-10 -32 4 -50 Q15 -70 37 -74 L29 -52 L19 -44 L25 -46 L21 -34 L11 -30 L14 -29 Q10 -16 1 -7Z",teal,night,4.5);
    path(c,"M0 -8 Q9 -39 30 -65 M11 -38 L8 -51 M16 -47 L27 -54",null,mint,4);
    path(c,"M-5 -12 L5 -9 L2 -1 L-5 8 L-7 0Z","#f0c573","#78586b",2.5);
  }
  function lens(c,a,opacity) {
    if(opacity<.012) return;
    c.save(); c.globalAlpha=opacity;
    const x=a.right.x+badgeLensOffset.x, y=a.right.y+badgeLensOffset.y, r=31;
    // Read only the underlying painted scene, before adding the rim or grip.
    // The visible eye and its blink are magnified by the glass on this frame.
    const m=c.getTransform(), cx=m.a*x+m.e, cy=m.d*y+m.f;
    const sx=r*m.a/1.42, sy=r*m.d/1.42;
    c.save(); c.beginPath(); c.arc(x,y,r,0,2*Math.PI); c.clip();
    c.drawImage(c.canvas,cx-sx,cy-sy,2*sx,2*sy,x-r,y-r,2*r,2*r);
    c.fillStyle="rgba(156,209,203,.16)"; c.fillRect(x-r,y-r,2*r,2*r); c.restore();
    c.translate(x,y); c.rotate(.48);
    path(c,"M-7 26 L7 26 L9 57 L5 64 L-6 64 L-9 57Z","#a57579",ink,4);
    box(c,-7,35,14,7,"#f0c573");
    c.beginPath(); c.arc(0,0,r,0,Math.PI*2); c.lineWidth=9; c.strokeStyle=ink; c.stroke();
    c.lineWidth=4; c.strokeStyle="#f0c573"; c.stroke();
    c.beginPath(); c.arc(0,0,r-3,Math.PI*1.03,Math.PI*1.47);
    c.lineWidth=4; c.strokeStyle=mint; c.stroke();
    c.restore();
  }
  function terminal(c,a,pressure) {
    c.save(); c.translate(a.left.x-8,a.left.y-8);
    // The raised screen and the key deck have different outlines at 1x.
    path(c,"M1 -21 L10 -30 L76 -30 L82 -24 L82 1 L136 1 L144 9 L141 39 L134 44 L5 44 L-2 36Z",teal,ink,6);
    path(c,"M8 -20 L14 -24 L71 -24 L75 -19 L75 9 L8 9Z",night,ink,4);
    path(c,"M18 -16 L29 -8 L18 0",null,mint,6);
    box(c,35,-2,15,5,mint);
    if(pressure>.77) box(c,57,-14,8,7,teal);
    box(c,9,17,82,16,"#587b90");
    for(const x of [14,33,52,71]) box(c,x,19,12,8,mint);
    const key=badgeKey(a.left,pressure), capY=key.y-a.left.y+4;
    box(c,99,10,24,21,"#352f41");
    box(c,100,capY+6,22,7,key.travel>=1 ? "#f0c573" : "#587b90");
    box(c,100,capY,22,7,key.travel>=1 ? gold : "#9bcbbd");
    c.restore();
  }
  function tickets(c,left,phase) {
    for(let j=0;j<3;j++) {
      const {x,y}=badgeTicket(left,j,phase);
      c.save(); c.translate(x,y);
      const start=10+32*j, color=["#9284b3",teal,"#ed9b97"][j];
      path(c,`M0 4 L${start} 4 L${start+4} -7 L${start+25} -7 L${start+29} 4 L111 4 L120 13 L120 43 L0 43Z`,color,ink,4);
      if(j===2) {
        path(c,"M5 7 L108 7 L116 15 L116 37 L5 37Z","#f3d6b9");
        path(c,"M15 21 L23 29 L36 14",null,night,5);
        box(c,49,15,38,5,"#78586b"); box(c,49,26,24,4,"#a57579");
        path(c,"M104 6 L105 17 L117 17Z",gold);
      } else {
        box(c,4,36,112,4,color);
      }
      c.restore();
    }
  }
  return {book,pad,quill,lens,terminal,tickets};
})();
