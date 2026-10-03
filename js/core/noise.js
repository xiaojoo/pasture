// Deterministic value noise. Everything that shapes the land, the clouds and the paint on
// the walls comes through here with an integer seed, so the same world is rebuilt pixel for
// pixel on every load and a screenshot can be compared with the one before it.

function hash2(xi, yi) {
  let h = (xi | 0) * 374761393 + (yi | 0) * 668265263;
  h = (h ^ (h >> 13)) * 1274126177;
  return ((h ^ (h >> 16)) >>> 0) / 4294967295;
}

function fade(t) {
  return t * t * (3 - 2 * t);
}

// 0..1
export function noise2(x, y) {
  const xi = Math.floor(x);
  const yi = Math.floor(y);
  const tx = fade(x - xi);
  const ty = fade(y - yi);
  const a = hash2(xi, yi);
  const b = hash2(xi + 1, yi);
  const c = hash2(xi, yi + 1);
  const d = hash2(xi + 1, yi + 1);
  return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

// 0..1, layered
export function fbm(x, y, octaves = 4, seed = 0) {
  let sum = 0;
  let amp = 1;
  let norm = 0;
  let fx = x + seed * 7.13;
  let fy = y + seed * 3.71;
  for (let i = 0; i < octaves; ++i) {
    sum += amp * noise2(fx, fy);
    norm += amp;
    amp *= 0.5;
    fx *= 2.03;
    fy *= 2.03;
  }
  return sum / norm;
}

// 0..1, ridged: flat valley floors with sharp crests, which is what real ranges look like
// rather than the lumpy hills plain fbm gives.
export function ridge(x, y, octaves = 4, seed = 0) {
  let sum = 0;
  let amp = 1;
  let norm = 0;
  let fx = x + seed * 11.7;
  let fy = y + seed * 5.3;
  for (let i = 0; i < octaves; ++i) {
    const n = 1 - Math.abs(noise2(fx, fy) * 2 - 1);
    sum += amp * n * n;
    norm += amp;
    amp *= 0.5;
    fx *= 2.07;
    fy *= 2.07;
  }
  return sum / norm;
}

export function smoothstep(e0, e1, x) {
  const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

export function clamp(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

export function mix(a, b, t) {
  return a + (b - a) * t;
}

// A seeded generator for scattering things (trees, rocks, cows' jitter) so a reload puts
// them back in the same place.
export function prng(seed = 1) {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13; s >>>= 0;
    s ^= s >> 17;
    s ^= s << 5; s >>>= 0;
    return s / 4294967295;
  };
}
