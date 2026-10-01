// The preset 3D models the outline designer offers instead of an uploaded picture.
//
// Each model is a pure function of (机数, 尺寸, 中心高度) returning the same
// `{n, e, d}` local-NED points the plan and the safety checks already speak -- `d` is
// negative up, so a model that varies `d` is the difference between a shape drawn on a
// flat sheet and one a fleet can actually fly.
//
// Pure on purpose: the point count, the height spread and the spacing a model produces
// are all things a headless check can measure, and "is it really 3D" should not depend
// on somebody looking at a window.
import { SHOW_MAX_DRONES } from './core.js';

// `z` says in one line what makes this one three-dimensional, because the picker is
// where an operator decides whether the shape needs height at all.
export const MODELS = [
  { id: 'sphere', name: '球壳', z: '上下都有机：赤道密、两极收' },
  { id: 'helix', name: '螺旋塔', z: '一圈一层高' },
  { id: 'dome', name: '圆顶', z: '中间最高、边缘落地' },
  { id: 'torus', name: '环面', z: '管截面是圆的' },
  { id: 'cube', name: '立方框', z: '十二根棱' },
  { id: 'flag', name: '飘动的旗', z: '面内按正弦起伏' },
  { id: 'hourglass', name: '沙漏', z: '两头宽、中间收腰' },
  { id: 'ringflat', name: '单层圆环', z: '同一个高度（平面）' },
];

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const TAU = Math.PI * 2;

// A model returns unit coordinates in [-1,1] on all three axes; the caller scales them.
// Written as unit shapes so `尺寸` and `中心高度` stay the only two numbers an operator
// has to reason about.
const UNITS = {
  sphere(k, n) {
    // Fibonacci sphere: even spacing without clustering at the poles, which is what
    // the safety rule on separation cares about.
    const g = Math.PI * (3 - Math.sqrt(5));
    const y = 1 - (k / Math.max(1, n - 1)) * 2;
    const r = Math.sqrt(Math.max(0, 1 - y * y));
    const th = g * k;
    return [Math.cos(th) * r, y, Math.sin(th) * r];   // e, up, n
  },
  helix(k, n) {
    const t = k / Math.max(1, n - 1);
    const th = t * TAU * Math.max(1, Math.round(n / 8));
    const y = t * 2 - 1;
    const r = 0.55 + 0.45 * Math.sin(t * Math.PI);
    return [Math.cos(th) * r, y, Math.sin(th) * r];
  },
  dome(k, n) {
    const g = Math.PI * (3 - Math.sqrt(5));
    // Only the upper hemisphere, but the crown has to reach up: y in [0,1] then centred.
    const y = 0.15 + 0.85 * (1 - (k / Math.max(1, n - 1)));
    const r = Math.sqrt(Math.max(0, 1 - y * y));
    const th = g * k;
    return [Math.cos(th) * r, y * 2 - 1, Math.sin(th) * r];
  },
  torus(k, n) {
    const rings = clamp(Math.round(Math.sqrt(n) / 1.6), 3, 12);
    const per = Math.max(2, Math.ceil(n / rings));
    // The tube's phase has to advance with the ring index as well: taking it from the
    // within-ring index alone puts every ring's first point at v = 0, and two rings
    // that happen to line up in plan overlap exactly -- which the separation rule then
    // refuses for a shape the operator can see is open.
    const i = k % per, j = Math.floor(k / per);
    const u = (j / rings) * TAU;
    const v = ((i + j * 0.37) / per) * TAU;
    const R = 0.72, rr = 0.28;
    return [(R + rr * Math.cos(v)) * Math.cos(u), Math.sin(v) * rr, (R + rr * Math.cos(v)) * Math.sin(u)];
  },
  cube(k, n) {
    // Twelve edges, points distributed over them in the order an operator can predict.
    const e = k % 12, t = (Math.floor(k / 12) + 0.5) / Math.max(1, Math.ceil(n / 12));
    const a = t * 2 - 1;
    const sgn = [-1, 1];
    const edges = [
      [a, -1, -1], [a, 1, -1], [a, -1, 1], [a, 1, 1],
      [-1, a, -1], [1, a, -1], [-1, a, 1], [1, a, 1],
      [-1, -1, a], [-1, 1, a], [1, -1, a], [1, 1, a],
    ][e];
    return edges;
  },
  flag(k, n) {
    const cols = clamp(Math.round(Math.sqrt(n * 1.8)), 2, 40);
    const rows = Math.max(2, Math.ceil(n / cols));
    const i = k % cols, j = Math.floor(k / cols) % rows;
    const x = cols > 1 ? (i / (cols - 1)) * 2 - 1 : 0;
    const y = rows > 1 ? 1 - (j / (rows - 1)) * 2 : 0;
    const wave = Math.sin(x * Math.PI * 1.5) * 0.28;
    return [x, y, wave];                                  // flag ripples in n
  },
  hourglass(k, n) {
    const t = k / Math.max(1, n - 1);
    const y = t * 2 - 1;
    const r = 0.28 + 0.72 * Math.abs(y);
    const th = t * TAU * Math.max(2, Math.round(n / 6));
    return [Math.cos(th) * r, y, Math.sin(th) * r];
  },
  ringflat(k, n) {
    const th = (k / Math.max(1, n)) * TAU;
    return [Math.cos(th), 0, Math.sin(th)];
  },
};

// The altitude the plan may not leave: the same 10..120 m the go/no-go check uses, so a
// model cannot hand the designer a cloud that is guaranteed to be refused.
export const ALT_MIN = 10, ALT_MAX = 120;

// -> [{n, e, d}] in local metres, `d` negative up, `alt` the centre of the shape.
export function modelCloud(id, count, scale, alt) {
  const unit = UNITS[id] || UNITS.sphere;
  const n = clamp(Math.round(count) || 1, 1, SHOW_MAX_DRONES);
  const s = Math.max(1, Number(scale) || 20);
  const a = clamp(Number(alt) || 40, ALT_MIN, ALT_MAX);
  // Half the vertical extent has to fit between the centre and the guardrail, or the
  // top row of a 60 m sphere would sit at 220 m and the plan would be refused for
  // something the operator never asked for.
  const room = Math.max(1, Math.min(a - ALT_MIN, ALT_MAX - a));
  const zs = Math.min(s, room);
  const out = [];
  for (let k = 0; k < n; ++k) {
    const [ue, uy, un] = unit(k, n);
    out.push({ e: ue * s, n: un * s, d: -(a + uy * zs) });
  }
  return out;
}

// What a cloud occupies, for the readout: an operator deciding whether 24 aircraft fit
// needs the box, not a count.
export function cloudSpan(cloud) {
  if (!cloud || !cloud.length) return { w: 0, d: 0, h: 0, alt: 0 };
  let en = Infinity, ex = -Infinity, nn = Infinity, nx = -Infinity, dn = Infinity, dx = -Infinity;
  for (const p of cloud) {
    en = Math.min(en, p.e); ex = Math.max(ex, p.e);
    nn = Math.min(nn, p.n); nx = Math.max(nx, p.n);
    dn = Math.min(dn, p.d); dx = Math.max(dx, p.d);
  }
  return { w: ex - en, d: nx - nn, h: dx - dn, alt: -(dn + dx) / 2 };
}

// The number the safety rule actually judges, computed here so the designer can say
// "this tight a shape needs this much separation" before the plan is refused.
export function minSpacing(cloud) {
  let best = Infinity;
  for (let i = 0; i < (cloud || []).length; ++i) {
    for (let j = i + 1; j < cloud.length; ++j) {
      const d = Math.hypot(cloud[i].e - cloud[j].e, cloud[i].n - cloud[j].n, cloud[i].d - cloud[j].d);
      if (d < best) best = d;
    }
  }
  return Number.isFinite(best) ? best : 0;
}

// The scene maps local NED to three.js as x = east, y = up, z = -north. Both directions
// of that live here because the pointer path needs both, and a sign written twice is a
// sign that can be wrong twice: a drag that moves a point *down* when the cursor goes
// up is exactly that mistake.
export function toScene(p) {
  return [p.e, -p.d, -p.n];
}

export function fromScene(x, y, z) {
  return clampPoint({ e: x, d: -y, n: -z });
}

// Ray-plane intersection, in whatever frame the caller is in. Screen-plane dragging is
// the only gesture that moves a point both sideways and up without a second control, so
// the maths is worth having somewhere a check can reach.
export function rayPlane(ox, oy, oz, dx, dy, dz, px, py, pz, nx, ny, nz) {
  const den = dx * nx + dy * ny + dz * nz;
  if (Math.abs(den) < 1e-6) return null;         // parallel: no answer, not a wrong one
  const t = ((px - ox) * nx + (py - oy) * ny + (pz - oz) * nz) / den;
  if (t < 0) return null;                         // behind the camera
  return [ox + dx * t, oy + dy * t, oz + dz * t];
}

// A drag replaces one point; everything else keeps its station, so the aircraft-to-point
// assignment the plan already made does not shuffle under the operator's hand.
export function movePoint(cloud, i, to) {
  const out = (cloud || []).map(p => ({ ...p }));
  if (i < 0 || i >= out.length) return out;
  out[i] = { n: to.n, e: to.e, d: to.d };
  return out;
}

// Keep a dragged point inside the same guardrail the models respect.
export function clampPoint(p) {
  const d = -clamp(-p.d, ALT_MIN, ALT_MAX);
  return { n: p.n, e: p.e, d };
}
