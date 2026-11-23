// Render the unmodified review canvas and its CSS overlay in seven adjacent cells.
// Chrome can capture all seven bubble offsets in a single compositor snapshot.
(steps) => {
  const source = document.querySelector('.bezel');
  const canvas = source.querySelector('canvas');
  const bubble = source.querySelector('.cloud-bubble');
  const strip = document.createElement('div');
  strip.style.cssText = 'position:fixed;left:0;top:0;display:flex;background:#000;z-index:1000';
  document.body.append(strip);
  const mirrors = [];
  for (let i = 0; i < steps; i++) {
    const cell = document.createElement('div');
    cell.style.cssText = 'width:368px;height:368px;overflow:hidden;background:black';
    strip.append(cell);
    const bezel = i === 0 ? source : source.cloneNode(true);
    cell.append(bezel);
    const frame = bezel.querySelector('canvas');
    const label = bezel.querySelector('.cloud-bubble');
    if (i > 0) {
      frame.removeAttribute('id');
      frame.style.cssText = 'width:368px;height:368px;border-radius:50%;image-rendering:pixelated;image-rendering:crisp-edges';
      label.removeAttribute('id');
      mirrors.push([frame.getContext('2d'), label]);
    }
    const animation = label.getAnimations()[0];
    animation.pause();
    // Sample the original animated layer at known displacements; this retains
    // Chrome's shadow and subpixel text compositing during a CSS transform.
    let low = 0, high = 1200;
    for (let k = 0; k < 22; k++) {
      animation.currentTime = (low + high) / 2;
      const y = new DOMMatrix(getComputedStyle(label).transform).m42;
      if (-y < 3 * i / (steps - 1)) low = animation.currentTime;
      else high = animation.currentTime;
    }
    animation.currentTime = i === 0 ? 0 : i === steps - 1 ? 1200 : (low + high) / 2;
  }
  return (selector) => {
    document.querySelector(selector).click();
    for (const [context, label] of mirrors) {
      context.clearRect(0, 0, canvas.width, canvas.height);
      context.drawImage(canvas, 0, 0);
      label.textContent = bubble.textContent;
    }
    return [document.querySelector('#hudMode').textContent, document.querySelector('#hudFrame').textContent];
  };
}
