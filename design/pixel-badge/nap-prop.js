"use strict";

// The tucked cloud opens at her cuff. When she leans over it, both hands
// lie on its surface and the cheek settles into the lower of its two lobes.
window.NapProp = (() => {
  const inks = {
    edge:"#484659", seam:"#6b789b", shadow:"#829ab9", crease:"#96b6ce", cloth:"#a4c9df",
    light:"#c8e0e8", piping:"#e7eee5", moon:"#ffe5ab", ochre:"#c19483",
  };
  const gripRuns = new Map();
  function position(display,clip,frame,view) {
    const row = clip?.cushion?.[frame];
    if (!row || !row[4]) return null;
    const [lx,ly,rx,ry,visible,opened,weight] = row;
    const left = display.facePoint(view,clip,frame,lx,ly);
    const right = display.facePoint(view,clip,frame,rx,ry);
    const close = view === "close";
    const a = visible/100, open = opened/100;
    const scale=close ? 4/2.75 : 1;
    // The cuffs rest inside the cushion. Its finished width and its belly
    // belong to the sewn object and stay the same when her hands relax.
    const w=Math.max(1,Math.round(scale*(10+37*open)*a));
    const h=Math.max(2,Math.round(scale*(6+16*open)*a));
    const between=(right.sx-left.sx)*.54;
    const x=Math.round(left.sx+(between+2*scale*open)*a-w/2);
    const hem=Math.min(148,Math.round(left.sy+(right.sy-left.sy)*.45+
      scale*(3+8*open)*a));
    const y=hem-h+1;
    const hands=[Math.round(left.sx-x),Math.round(left.sy-y),
      Math.round(right.sx-x),Math.round(right.sy-y)];
    return {x,y,w,h,open,weight:weight/100,close,hands,
      grip:clip.gripMasks[close?1:0][frame]};
  }
  function contains(p,x,y,contact,motion) {
    x=Math.floor(x); y=Math.floor(y);
    if (!p || p.open<.9 || x<p.x || x>=p.x+p.w || y<p.y || y>=p.y+p.h) return false;
    if (!gripRuns.has(p.grip)) gripRuns.set(p.grip,atob(p.grip));
    const mask=gripRuns.get(p.grip);
    const row=y-(p.close?22:18), col=x-23;
    for (let i=0;i<mask.length;i+=3) {
      if (mask.charCodeAt(i)>row) break;
      if (mask.charCodeAt(i)===row && col>=mask.charCodeAt(i+1) && col<mask.charCodeAt(i+2)) return false;
    }
    const stamp=clothStamp(p,contact,motion);
    return stamp.pixels[4*((y-p.y)*p.w+x-p.x)+3]!==0;
  }
  function pressure(contact,motion) {
    if (contact?.part!=="pillow") return 0;
    if (contact.phase==="down") return motion ? 1+Math.min(2,Math.floor(contact.age/40)) : 2;
    if (!motion) return 0;
    return Math.max(0,1+Math.min(2,Math.floor(contact.heldMs/40))-Math.floor(contact.age/40));
  }
  const stamps = new Map();
  function clothStamp(p,contact,motion) {
    const level=pressure(contact,motion);
    const column=level ? Math.max(0,Math.min(p.w-1,Math.round(contact.sx-p.x))) : 0;
    const key=[p.w,p.h,p.open,p.weight,p.close,...p.hands,level,column].join("/");
    let stamp=stamps.get(key);
    if (!stamp) {
      const sprite=document.createElement("canvas");
      sprite.width=p.w; sprite.height=p.h;
      const ink=sprite.getContext("2d",{willReadFrequently:true});
      paintFabric(ink,p,level,column);
      // This canvas contains only local procedural ink, also on file://.
      stamp={sprite,pixels:ink.getImageData(0,0,p.w,p.h).data};
      if (stamps.size>=256) stamps.delete(stamps.keys().next().value);
      stamps.set(key,stamp);
    }
    return stamp;
  }
  function pillow(ctx,p,contact,motion) {
    if (p) ctx.drawImage(clothStamp(p,contact,motion).sprite,p.x,p.y);
  }
  // Each lobe is an ellipse in the pattern. Taking the outer envelope leaves
  // the overlaps seamless, including when a fingertip presses across a join.
  const lobes = [
    [.13,.59,.13,.24], [.28,.47,.18,.25], [.49,.53,.25,.23],
    [.68,.49,.16,.24], [.82,.41,.135,.33], [.915,.55,.085,.19],
    [.50,.63,.46,.355],
  ];
  const smallMoon=["..xxx",".xx..","xx...","xx..x",".xxxx"];
  const largeMoon=["...xxxx","..xxx..",".xxx...","xxx....","xxx....",".xx...x",".xxx.xx","..xxxx."];
  function paintFabric(ctx,{w,h,open,weight,close,hands},level,column) {
    const top=[], bottom=[];
    const finger=column/Math.max(1,w-1);
    for(let x=0;x<w;x++) {
      const u=(x+.5)/w;
      let crest=1, belly=0;
      for(const [cx,cy,rx,ry] of lobes) {
        const dx=(u-cx)/rx;
        if (Math.abs(dx)>=1) continue;
        const span=ry*Math.sqrt(1-dx*dx);
        crest=Math.min(crest,cy-span);
        belly=Math.max(belly,cy+span);
      }
      const roll=Math.sqrt(Math.max(0,1-(2*u-1)**2));
      const cheek=Math.max(0,1-((u-.46)/.27)**2);
      const touch=Math.max(0,1-((u-finger)/.18)**2);
      const pressed=weight*cheek*(close?4:2)+level*touch;
      const first=Math.round(((.50-.32*roll)*(1-open)+crest*open)*(h-1)+pressed);
      const last=Math.round(((.50+.32*roll)*(1-open)+belly*open)*(h-1));
      top[x]=Math.min(first,last); bottom[x]=last;
    }
    function stitch(x,y,color) {
      if (x<0 || x>=w || y<top[x] || y>bottom[x]) return;
      ctx.fillStyle=inks[color]; ctx.fillRect(x,y,1,1);
    }
    for(let x=0;x<w;x++) {
      const u=(x+.5)/w, first=top[x], last=bottom[x];
      for(let y=first;y<=last;y++) {
        const v=(y-first)/Math.max(1,last-first);
        const border=x===0 || x===w-1 || y===first || y===last ||
          y<top[x-1] || y<top[x+1] || y>bottom[x-1] || y>bottom[x+1];
        let color="cloth";
        if (border) color="edge";
        else if (y===last-1 && (u<.19 || u>.68)) color="seam";
        else if (v>.82-.16*(2*u-1)**2) color="shadow";
        else if (y===first+1 && (u<.23 || u>.64)) color="piping";
        else if (v<.25+.16*Math.cos(5*Math.PI*(u-.81))) color="light";
        stitch(x,y,color);
      }
    }
    if(open<.45 || w<15) {
      const x=Math.floor(w*.46);
      for(let y=top[x]+2;y<bottom[x]-1;y++) stitch(x,y,"seam");
      return;
    }
    if(open>.9) {
      // A shallow fold under each palm follows the contact point. The same
      // foreground that draws the fingers hides the inner part of this shadow.
      const reach=close?4:3;
      for(let hand=0;hand<2;hand++) {
        const side=hand===0?1:-1;
        for(let i=-1;i<=reach;i++) {
          const x=hands[2*hand]+i*side;
          const y=hands[2*hand+1]+(close?3:2)+Math.round(i*.35);
          stitch(x,y,"crease");
        }
      }
    }
    // The crescent is a warm patch on the exposed front lobe. An offset
    // outline and three short seam stitches read as fabric at badge size.
    const moon=close?largeMoon:smallMoon;
    const cx=Math.round(w*.82), cy=top[cx]+Math.round((bottom[cx]-top[cx])*.54);
    const left=cx-Math.floor(moon[0].length/2), upper=cy-Math.floor(moon.length/2);
    for (const [offset,color] of [[1,"ochre"],[0,"moon"]]) {
      moon.forEach((line,j)=>Array.from(line).forEach((dot,i)=>{
        if(dot==="x") stitch(left+i,upper+j+offset,color);
      }));
    }
    const starX=left+moon[0].length+1, starY=upper+2;
    stitch(starX,starY-1,"piping"); stitch(starX,starY,"moon"); stitch(starX,starY+1,"piping");
    stitch(starX-1,starY,"piping"); stitch(starX+1,starY,"piping");
    // A short crescent of seam under the cheek records the local load.
    if(weight>.35) {
      const start=Math.round(w*.34), end=Math.round(w*.51);
      for(let x=start;x<=end;x++) stitch(x,top[x]+1,weight>.7?"shadow":"light");
    }
    for(const u of [.20,.27,.34]) {
      const x=Math.floor(w*u);
      stitch(x,bottom[x]-2,"light");
      if(close) stitch(x+1,bottom[x+1]-2,"light");
    }
  }
  const small=["xxxxx","...xx","..xx.",".xx..","xxxxx"];
  const large=["xxxxxxx","xxxxxxx",".....xx","....xx.","...xx..","..xx...",".xx....","xxxxxxx","xxxxxxx"];
  function glyph(ctx,x,y,rows,color) {
    for (const outline of [true,false]) {
      ctx.fillStyle=outline?"#3e324d":color;
      rows.forEach((row,dy) => {
        for (let dx=0; dx<row.length; dx++) if (row[dx]==="x") {
          const r=outline?1:0;
          ctx.fillRect(x+dx-r,y+dy-r,1+2*r,1+2*r);
        }
      });
    }
  }
  function sleepMarks(ctx,id,frame,view,motion) {
    const peek=id==="peekLeft" || id==="peekRight";
    if (peek ? frame>3 : id!=="doze" || frame<15) return;
    const close=view==="close", x=close?32:43, y=close?59:53;
    const period=motion && id==="doze" ? Math.round(1-Math.cos((frame-15)*Math.PI/10)) : 0;
    if (!peek || frame===0) glyph(ctx,x+10,y+10-(period%2),small,"#c9e6df");
    glyph(ctx,x-Math.floor(period/2),y-period-(peek?frame:0),large,"#fff0c1");
  }
  return {position,contains,pillow,sleepMarks};
})();
