// Procedural canvas textures. Nothing here is downloaded: every map is drawn at load time
// from a seeded generator, so the ranch looks the same on the tenth reload as it did on the
// first, and there is no asset pipeline to break.
//
// Each builder returns a THREE.Texture. Materials that want relief pass the same texture
// to bumpMap as well, which is what gives a wall its relief without a second image.
//
// ONE RULE, and it is the reason the first version of this file made the roads sparkle:
// every feature drawn here is at least three texels across. A texture that carries per-texel
// noise is already aliased before it reaches the GPU, and the mip chain is then built out of
// that aliased image -- so no filtering setting can recover it, and the surface changes
// character as the view turns. The original asphalt map had a noise term with a period of
// 0.53 texels and the gravel map was 2600 dots averaging about one pixel across. Measured as
// interior luminance change per 0.7 degrees of azimuth, the carriageway shimmered 54% harder
// than the lawn beside it.
import * as THREE from 'three';
import { renderer } from './scene.js';
import { noise2, fbm, prng } from './noise.js';

const ANISO = renderer.capabilities.getMaxAnisotropy();

function finish(canvas, rx, ry) {
  const t = new THREE.CanvasTexture(canvas);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(rx, ry);
  t.anisotropy = ANISO;
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

function surface(size, paint) {
  const c = document.createElement('canvas');
  c.width = c.height = size;
  const g = c.getContext('2d');
  paint(g, size);
  return c;
}

// Blank canvas plus an ImageData to fill in.
function pixels(size) {
  const c = surface(size, () => {});
  const g = c.getContext('2d');
  return { c, g, img: g.createImageData(size, size) };
}

// Two noise terms, both wider than three texels at scale 1: fbm's soft blotching (finest
// octave about eight texels) and one pass at about six. `scale` lets a builder that wants a
// tighter grain ask for it without going below the limit.
function speckle(x, y, base, spread, seed, scale = 1) {
  const n = fbm(x * 0.03 * scale + seed, y * 0.03 * scale - seed, 3);
  const m = noise2(x * 0.16 * scale + seed * 3, y * 0.12 * scale + seed);
  return base * (0.88 + 0.17 * n) + (m - 0.5) * spread;
}

/* =========================================================
   GROUND
   A neutral mottle: the terrain's own vertex colours carry the hue, and this only breaks
   up the flat fill so a hundred metres of pasture is not one flat polygon.
========================================================= */

export function groundTexture(repeat = 300) {
  const S = 256;
  const { c, g, img } = pixels(S);
  for (let y = 0; y < S; ++y) {
    for (let x = 0; x < S; ++x) {
      const i = (y * S + x) * 4;
      const v = 196 + (fbm(x * 0.028, y * 0.028, 3) - 0.5) * 96
        + (noise2(x * 0.15, y * 0.11) - 0.5) * 34;
      img.data[i] = v * 0.97;
      img.data[i + 1] = v;
      img.data[i + 2] = v * 0.9;
      img.data[i + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0);
  return finish(c, repeat, repeat);
}

/* =========================================================
   MASONRY AND PLASTER
========================================================= */

// Rendered plaster: soft mottling plus trowel sweeps, every one of them wide enough to
// survive being minified.
export function plasterTexture(rx = 3, ry = 1) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const v = speckle(x, y, 244, 16, 1.7);
        img.data[i] = Math.min(255, v);
        img.data[i + 1] = Math.min(255, v * 0.965);
        img.data[i + 2] = Math.min(255, v * 0.88);
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);

    g.globalAlpha = 0.05;
    g.lineWidth = 5;
    for (let k = 0; k < 26; ++k) {
      const y = (k * 247) % s;
      g.strokeStyle = k % 2 ? '#ffffff' : '#6b5f4d';
      g.beginPath();
      g.moveTo(0, y);
      g.lineTo(s, y + ((k % 5) - 2) * 2);
      g.stroke();
    }
    g.globalAlpha = 1;
  });
  return finish(c, rx, ry);
}

// Concrete block / poured foundation: grey, blunt, joints several texels wide.
export function concreteTexture(rx = 4, ry = 1) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const v = speckle(x, y, 190, 30, 5.1);
        img.data[i] = v * 0.95;
        img.data[i + 1] = v * 0.96;
        img.data[i + 2] = v * 0.97;
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
    g.strokeStyle = 'rgba(40,42,44,0.28)';
    g.lineWidth = 4;
    for (let r = 0; r < 4; ++r) {
      g.beginPath(); g.moveTo(0, r * s / 4); g.lineTo(s, r * s / 4); g.stroke();
    }
    for (let r = 0; r < 4; ++r) {
      const off = (r % 2) * s / 8;
      for (let k = 0; k < 4; ++k) {
        const x = off + k * s / 4;
        g.beginPath(); g.moveTo(x, r * s / 4); g.lineTo(x, (r + 1) * s / 4); g.stroke();
      }
    }
  });
  return finish(c, rx, ry);
}

/* =========================================================
   TIMBER
========================================================= */

// Vertical barn boards: seams and long grain. The grain used to be `noise2(x * 2.4, ...)`,
// a period of 0.42 texels -- that is the shimmer this file is now written to avoid.
export function plankTexture(rx = 1, ry = 1, tint = [1, 0.98, 0.95]) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    const planks = 6;
    const pw = s / planks;
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const p = Math.floor(x / pw);
        const local = (x - p * pw) / pw;
        const tone = 0.84 + prng(p * 977 + 13)() * 0.26;
        let v = speckle(x, y, 200, 26, 2.3 + p, 0.5) * tone;
        v *= 0.95 + 0.09 * noise2(x * 0.08 + p * 31, y * 0.02);
        if (local < 0.1) v *= 0.6 + local * 4;
        if (local > 0.9) v *= 0.64 - (local - 0.9) * 4;
        img.data[i] = Math.min(255, v * tint[0]);
        img.data[i + 1] = Math.min(255, v * tint[1]);
        img.data[i + 2] = Math.min(255, v * tint[2]);
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}

// Horizontal lap siding: the lit lip of each board and the shadow line under the lap.
export function lapboardTexture(rx = 1, ry = 1, tint = [1, 1, 1]) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    const rows = 6;
    const rh = s / rows;
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const r = Math.floor(y / rh);
        const local = (y - r * rh) / rh;
        const tone = 0.88 + prng(r * 613 + 7)() * 0.2;
        let v = speckle(x, y, 200, 22, 8.4 + r, 0.7) * tone;
        v *= 0.96 + 0.07 * noise2(x * 0.05, y * 0.06 + r * 17);
        if (local > 0.84) v *= 1.1;                        // lit lip of the board above
        if (local < 0.12) v *= 0.6;                        // shadow under the lap
        img.data[i] = Math.min(255, v * tint[0]);
        img.data[i + 1] = Math.min(255, v * tint[1]);
        img.data[i + 2] = Math.min(255, v * tint[2]);
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}

/* =========================================================
   ROOFING
========================================================= */

// Shingle courses. The slot between courses is the one deliberately sharp feature a roof
// has; it is kept at a seventh of a course, about four texels, so the course still resolves.
export function shingleTexture(rx = 6, ry = 4) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    const courses = 5;
    const ch = s / courses;
    const tabs = 4;
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const r = Math.floor(y / ch);
        const local = (y - r * ch) / ch;
        const col = Math.floor((x + (r % 2) * s / (tabs * 2)) / (s / tabs));
        const tone = 0.84 + prng(r * 131 + col * 17 + 3)() * 0.3;
        let v = speckle(x, y, 190, 34, 4.2 + r) * tone;
        if (local > 0.86) v *= 0.5;
        img.data[i] = Math.min(255, v * 0.98);
        img.data[i + 1] = Math.min(255, v * 0.9);
        img.data[i + 2] = Math.min(255, v * 0.84);
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}

// Standing-seam metal: a bright rib and a shadow flank per seam, with weathering streaks
// running down the sheet.
export function seamMetalTexture(rx = 8, ry = 1, tint = [1, 1, 1]) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    const ribs = 5;
    const rw = s / ribs;
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const local = (x % rw) / rw;
        let v = speckle(x, y, 200, 20, 6.6, 0.8);
        v *= 0.94 + 0.11 * noise2(x * 0.02, y * 0.05);
        if (local < 0.1) v *= 1.3;
        else if (local < 0.22) v *= 0.6;
        else if (local > 0.9) v *= 0.72;
        img.data[i] = Math.min(255, v * tint[0]);
        img.data[i + 1] = Math.min(255, v * tint[1]);
        img.data[i + 2] = Math.min(255, v * tint[2]);
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}

// Corrugated sheeting. Four flutes to a two-metre tile is a 500 mm rib -- the real thing is
// finer, but a quarter-metre rib seen from 150 metres lands on one and a half pixels, which
// is the worst place a periodic feature can be: too coarse to mip away and too fine to
// resolve, so it beats against the pixel grid as a comb. Half as many flutes, and half the
// contrast between crest and trough.
export function corrugatedTexture(rx = 10, ry = 2, tint = [1, 1, 1]) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const flute = 0.5 + 0.5 * Math.sin((x / s) * Math.PI * 2 * 4);
        const v = 168 + flute * 58 + (noise2(x * 0.05, y * 0.02) - 0.5) * 30;
        img.data[i] = Math.min(255, v * tint[0]);
        img.data[i + 1] = Math.min(255, v * tint[1]);
        img.data[i + 2] = Math.min(255, v * tint[2]);
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}

/* =========================================================
   BRICK, ASPHALT, GRAVEL
========================================================= */

export function brickTexture(rx = 3, ry = 2) {
  const S = 256;
  const c = surface(S, (g, s) => {
    g.fillStyle = '#8d8177';
    g.fillRect(0, 0, s, s);
    const rows = 6;
    const rh = s / rows;
    const cols = 3;
    const bw = s / cols;
    const rnd = prng(23);
    for (let r = 0; r < rows; ++r) {
      const off = (r % 2) * bw / 2;
      for (let k = -1; k <= cols; ++k) {
        const x = off + k * bw;
        const y = r * rh;
        const t = rnd();
        g.fillStyle = `rgb(${126 + t * 46},${62 + t * 30},${48 + t * 24})`;
        g.fillRect(x + 3, y + 3, bw - 6, rh - 6);
        g.fillStyle = `rgba(0,0,0,${0.05 + rnd() * 0.1})`;
        g.fillRect(x + 3, y + 3, bw - 6, rh - 6);
      }
    }
  });
  return finish(c, rx, ry);
}

// Asphalt: aggregate blotching, nothing finer than about five texels. The version this
// replaces added a per-texel hash of +-45 on top of everything, which is what turned a
// carriageway sixty metres out into a screen of sparkles.
export function asphaltTexture(rx = 26, ry = 26) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const v = 118 + (fbm(x * 0.05, y * 0.05, 3) - 0.5) * 96
          + (noise2(x * 0.13, y * 0.1) - 0.5) * 30;
        img.data[i] = v * 0.92;
        img.data[i + 1] = v * 0.94;
        img.data[i + 2] = v * 0.96;
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}

// Road shoulder and switchyard gravel: stones two to six texels across, so each one survives
// several mip levels instead of existing in one frame and not the next.
export function gravelTexture(rx = 20, ry = 20) {
  const S = 256;
  const c = surface(S, (g, s) => {
    g.fillStyle = '#8b8375';
    g.fillRect(0, 0, s, s);
    const rnd = prng(91);
    for (let k = 0; k < 1400; ++k) {
      const x = rnd() * s;
      const y = rnd() * s;
      const r = 1.6 + rnd() * 4.4;
      const t = 0.62 + rnd() * 0.5;
      g.fillStyle = `rgba(${Math.round(150 * t)},${Math.round(142 * t)},${Math.round(128 * t)},0.85)`;
      g.beginPath();
      g.arc(x, y, r, 0, 6.2832);
      g.fill();
    }
  });
  return finish(c, rx, ry);
}

// Sandstone caprock: horizontal bedding planes with case-hardened bands, which is what a
// chapada cliff is made of. Bands are nine to thirty texels tall -- the same three-texel floor
// every other map in this file keeps, because a cliff covers a lot of pixels and a per-texel
// noise on it would be the loudest shimmer in the scene.
export function strataTexture(rx = 3, ry = 3) {
  const S = 256;
  const { c, g, img } = pixels(S);
  const bands = [];
  const rnd = prng(619);
  let y = 0;
  while (y < S) { const h = 9 + rnd() * 22; bands.push([y, y + h, 0.82 + rnd() * 0.34]); y += h; }
  for (let py = 0; py < S; ++py) {
    let tone = 1;
    for (const [a, b, t] of bands) if (py >= a && py < b) { tone = t * (0.9 + 0.2 * (py - a) / (b - a)); break; }
    for (let px = 0; px < S; ++px) {
      const i = (py * S + px) * 4;
      const v = tone * (0.93 + 0.14 * fbm(px * 0.028, py * 0.05 + 4, 3));
      img.data[i] = Math.min(255, 246 * v);
      img.data[i + 1] = Math.min(255, 240 * v);
      img.data[i + 2] = Math.min(255, 231 * v);
      img.data[i + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0);
  return finish(c, rx, ry);
}

// Farm track: baked earth with a skin of small stones and a scatter of dried grass. Same
// discipline as the gravel -- no per-texel noise, stones three to seven texels -- because this
// covers a 160 m ribbon and a high-frequency map on a surface that long is the shimmer the
// asphalt used to have. Warmer and less grey than the switchyard gravel, and flatter in value:
// a dirt track has no specular grain to catch the sun, only the wheel ruts, and those are
// vertex colours in js/world/road.js rather than something drawn here.
export function dirtTexture(rx = 20, ry = 20) {
  const S = 256;
  const { c, g, img } = pixels(S);
  for (let y = 0; y < S; ++y) {
    for (let x = 0; x < S; ++x) {
      const i = (y * S + x) * 4;
      const n = speckle(x, y, 0.92, 0.22, 71);
      const fine = fbm(x * 0.055 + 3, y * 0.055 - 5, 3);
      const v = n * (0.9 + 0.2 * fine);
      img.data[i] = Math.min(255, 248 * v);
      img.data[i + 1] = Math.min(255, 243 * v);
      img.data[i + 2] = Math.min(255, 234 * v);
      img.data[i + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0);
  const rnd = prng(4307);
  for (let k = 0; k < 320; ++k) {
    const x = rnd() * S;
    const y = rnd() * S;
    const r = 1.7 + rnd() * 5.1;
    const t = 0.7 + rnd() * 0.45;
    g.fillStyle = `rgba(${Math.round(196 * t)},${Math.round(168 * t)},${Math.round(132 * t)},0.8)`;
    g.beginPath();
    g.arc(x, y, r, 0, 6.2832);
    g.fill();
  }
  // straw: short and pale, four texels minimum so it survives a mip level
  g.strokeStyle = 'rgba(206,186,138,0.5)';
  for (let k = 0; k < 90; ++k) {
    const x = rnd() * S;
    const y = rnd() * S;
    const a = rnd() * 6.2832;
    const l = 4 + rnd() * 6;
    g.lineWidth = 1.6;
    g.beginPath();
    g.moveTo(x, y);
    g.lineTo(x + Math.cos(a) * l, y + Math.sin(a) * l);
    g.stroke();
  }
  return finish(c, rx, ry);
}

// Clay roof tile: courses of half-cylinders with the sun catching the crest of each one, which
// is what a colonial roof is made of. The shingle map beside it is a flat laminated look and
// reads as asphalt paper at this distance.
export function tileTexture(rx = 20, ry = 20) {
  const S = 256;
  const { c, g, img } = pixels(S);
  const COURSES = 8;          // rows of tiles per tile of texture
  const perCourse = 6;        // half-barrels per row
  for (let y = 0; y < S; ++y) {
    for (let x = 0; x < S; ++x) {
      const i = (y * S + x) * 4;
      const cy = (y / S) * COURSES;
      const row = Math.floor(cy);
      const inRow = cy - row;
      const cx = (x / S) * perCourse + (row % 2 ? 0.5 : 0);
      const barrel = cx - Math.floor(cx);
      // roundness across the barrel, and the lapped shadow at the bottom of each course
      const round = Math.sin(barrel * Math.PI);
      const lap = inRow < 0.16 ? 0.62 + 2.4 * inRow : 1;
      const shade = (0.72 + 0.34 * round) * lap;
      const grain = 0.94 + 0.12 * noise2(x * 0.09 + row * 7, y * 0.05);
      const v = shade * grain;
      img.data[i] = Math.min(255, 250 * v);
      img.data[i + 1] = Math.min(255, 246 * v);
      img.data[i + 2] = Math.min(255, 241 * v);
      img.data[i + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0);
  return finish(c, rx, ry);
}

// A painted board: dark ground, pale heavy capitals, one or two lines. Used for the farm
// gate's name board.
//
// The lettering is deliberately not crisp white on black. A hanging sign is a thin,
// high-contrast, repeating feature on a surface you look at from a hundred metres, which is
// precisely the profile that strobed on the old centre line -- measured at 54% interior
// luminance change per 0.7 degrees of azimuth. Off-white on weathered brown at a 3:1 ratio
// still reads as a name board at the overview distance and does not flip between mip levels.
export function signTexture(lines, { w = 1024, h = 512, bg = '#4a382c', fg = '#e8e0cf' } = {}) {
  const c = surface(1, () => {});
  c.width = w;
  c.height = h;
  const g = c.getContext('2d');
  g.fillStyle = bg;
  g.fillRect(0, 0, w, h);
  // board grain, four texels minimum
  const rnd = prng(812);
  g.strokeStyle = 'rgba(0,0,0,0.16)';
  g.lineWidth = 4;
  for (let k = 0; k < 26; ++k) {
    const y = rnd() * h;
    g.beginPath();
    g.moveTo(0, y);
    g.bezierCurveTo(w * 0.3, y + (rnd() - 0.5) * 14, w * 0.7, y + (rnd() - 0.5) * 14, w, y);
    g.stroke();
  }
  g.fillStyle = fg;
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  const lh = h / (lines.length + 0.4);
  lines.forEach((t, i) => {
    // Fit by width, then clamp by height, so a longer name never gets cropped to nothing.
    let size = lh * 0.86;
    g.font = `700 ${size}px "Arial Black", Impact, system-ui, sans-serif`;
    const over = g.measureText(t).width / (w * 0.9);
    if (over > 1) {
      size /= over;
      g.font = `700 ${size}px "Arial Black", Impact, system-ui, sans-serif`;
    }
    g.fillText(t, w / 2, lh * (i + 0.7));
  });
  const t = new THREE.CanvasTexture(c);
  t.anisotropy = ANISO;
  t.colorSpace = THREE.SRGBColorSpace;
  t.generateMipmaps = true;
  t.minFilter = THREE.LinearMipmapLinearFilter;
  return t;
}

// Road centre line. The bar has to span the whole U of the plane -- U is the carriageway
// width axis, V is the length axis where the dashes repeat. Cropping the bar to the middle
// of U makes the marking 1/5 of the plane's width, which is eight centimetres: under a pixel
// on screen, and it strobes as the camera moves.
export function dashTexture(repeatY = 18) {
  const S = 64;
  const c = surface(S, (g, s) => {
    g.clearRect(0, 0, s, s);
    const grad = g.createLinearGradient(0, 0, s, 0);
    grad.addColorStop(0, 'rgba(232,226,207,0.55)');
    grad.addColorStop(0.12, 'rgb(232,226,207)');
    grad.addColorStop(0.88, 'rgb(232,226,207)');
    grad.addColorStop(1, 'rgba(232,226,207,0.55)');
    g.fillStyle = grad;
    g.fillRect(0, s * 0.1, s, s * 0.42);
  });
  const t = finish(c, 1, repeatY);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// Ripples for the river: used as a bump map that the water scrolls along itself.
export function rippleTexture(rx = 26, ry = 6) {
  const S = 256;
  const c = surface(S, (g, s) => {
    const { img } = pixels(s);
    for (let y = 0; y < s; ++y) {
      for (let x = 0; x < s; ++x) {
        const i = (y * s + x) * 4;
        const a = Math.sin((x / s) * 12.5 + Math.sin(y / s * 7.0) * 2.2);
        const b = Math.sin((y / s) * 9.4 + Math.sin(x / s * 5.1) * 1.7);
        const v = 128 + (a * 0.6 + b * 0.4) * 62 + (noise2(x * 0.1, y * 0.1) - 0.5) * 40;
        img.data[i] = img.data[i + 1] = img.data[i + 2] = v;
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  });
  return finish(c, rx, ry);
}
