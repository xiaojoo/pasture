// The land. It used to be one flat 180x180 board floating over a solid colour, which is why
// the horizon looked like a cut-out.
//
// Two rules hold this together:
//
//  1. Inside PLATEAU_R the height is exactly zero -- not "nearly". Every building, road,
//     fence post, cow and grid line in js/world/* was placed against a flat y=0, so the
//     ranch keeps sitting on it untouched and only the wild land outside moves.
//  2. The river is a carved feature with its own longitudinal profile, so the water falls
//     from the foothills into the lake and out through the south-west instead of floating
//     over hollows. The channel cross-section is always deeper than the water line and
//     always banks above it.
//
// The mesh is a polar grid rather than a square one: the detail is wanted where the eye is
// (the valley rim) and not out at the haze line, so the same view costs a tenth of the
// triangles a Cartesian field would.
import * as THREE from 'three';
import { scene } from './scene.js';
import { fbm, ridge, smoothstep, clamp, mix } from './noise.js';
import { groundTexture } from './textures.js';

export const PLATEAU_R = 130;      // flat to here
export const HAZE_R = 1400;        // far enough out that the fog has taken the land completely
const ANG = 160;

/* =========================================================
   THE RIVER
   Catmull-Rom through these, all of it outside the plateau. It runs down the east side, which
   is the side the reference puts it on: from the visitor's eye the water and the fall sit to
   the right of the axis, and the meadow between them and the pens stays open.

   The channel comes out of the caprock bench at FALL_S and drops into the valley. That step is
   the whole reason the profile is discontinuous here rather than a smooth ramp -- a waterfall
   is a discontinuity in the water surface, and anything that smooths it away gives you a rapid.
========================================================= */

// The east quadrant, and pulled in close enough to the axis that the fall lands at about
// three quarters of the frame width from the solved overview -- which is where the reference
// puts it. Further out and it slides off the edge of the picture entirely.
const RIVER_PTS = [
  [238, -612], [216, -524], [196, -446], [176, -372],
  [156, -306],
  [140, -248],          // FALL_S: the lip
  [136, -186], [146, -118], [166, -46], [196, 30], [234, 116], [282, 214], [338, 322],
];

const RIVER_SAMPLES = 300;
const curve = new THREE.CatmullRomCurve3(
  RIVER_PTS.map(p => new THREE.Vector3(p[0], 0, p[1])), false, 'catmullrom', 0.5);

const riverPts = curve.getSpacedPoints(RIVER_SAMPLES);   // length RIVER_SAMPLES + 1
export const riverLength = curve.getLength();

// Where the water runs, in order, so the surface mesh and anything that grows on its banks
// can read the same stations the carving used.
export const riverSamples = riverPts.map((p, i) => ({
  x: p.x, z: p.z, s: i / RIVER_SAMPLES,
}));

export function riverPoint(s) {
  return riverPts[Math.round(clamp(s, 0, 1) * RIVER_SAMPLES)];
}

// The lip of the fall, in station fraction. js/world/falls.js places the cliff on this
// same number, so the water, the rock and the carved channel cannot drift apart.
export const FALL_S = 5 / 12;
export const FALL_TOP = 74;      // water surface on the caprock bench
// The lip in world coordinates, for the terrain's own colouring: the gorge wall has to come
// out sandstone-coloured whether or not it happens to lie inside a table's rim band.
export const fallAt = riverPoint(FALL_S);
export const FALL_POOL = 4;      // water surface in the plunge pool

// Water surface height along the river: a bench up top, a chute over the caprock, then a long
// easy run out to the haze.
//
// The drop is a ramp over 0.035 of the station (about 20 m of reach, 70 m of fall, so 74
// degrees), not a step. A step is what a waterfall is, but the land is a mesh sampled on a
// polar grid that does not know where the lip is: with a discontinuity, whether the terrain
// shows a cliff face or a 70-metre ramp depends on whether two grid nodes happen to straddle
// the station, and js/world/falls.js has to guess a rock height to cover the difference. A
// steep ramp is representable on the grid, so the carved gorge and the rock face agree.
export function waterY(s) {
  const t = clamp(s, 0, 1);
  const bench = mix(96, FALL_TOP, smoothstep(0, FALL_S, t));
  const run = mix(FALL_POOL, -5.4, smoothstep(FALL_S, 1, t));
  return mix(bench, run, smoothstep(FALL_S, FALL_S + 0.035, t));
}

export function riverHalfWidth(s) {
  const t = clamp(s, 0, 1);
  const lake = 34 * Math.exp(-(((t - 0.62) / 0.085) ** 2));
  return (12 + 10 * t + lake) * smoothstep(0, 0.035, t) * (1 - 0.25 * smoothstep(0.93, 1, t));
}

const RBOX = riverPts.reduce((b, p) => ({
  minX: Math.min(b.minX, p.x), maxX: Math.max(b.maxX, p.x),
  minZ: Math.min(b.minZ, p.z), maxZ: Math.max(b.maxZ, p.z),
}), { minX: 1e9, maxX: -1e9, minZ: 1e9, maxZ: -1e9 });

// Nearest point on the river: index (-> s), distance, half width and water height.
function nearRiver(x, z) {
  if (x < RBOX.minX - 210 || x > RBOX.maxX + 210 || z < RBOX.minZ - 210 || z > RBOX.maxZ + 210) return null;
  let bi = 0, bd = 1e18;
  for (let i = 0; i < riverPts.length; ++i) {
    const dx = riverPts[i].x - x;
    const dz = riverPts[i].z - z;
    const d2 = dx * dx + dz * dz;
    if (d2 < bd) { bd = d2; bi = i; }
  }
  const d = Math.sqrt(bd);
  const s = bi / RIVER_SAMPLES;
  const hw = riverHalfWidth(s);
  if (d > hw * 4.2) return null;
  return { s, d, hw, wy: waterY(s) };
}

/* =========================================================
   HEIGHT
========================================================= */

/* =========================================================
   THE CAPROCK
   The reference is a chapada landscape: a valley floor, and around its eastern and northern
   rim a set of tables -- sandstone masses with a flat top and a near-vertical red flank, the
   kind of thing a river falls off of. A ridge of noise cannot make that, because a ridge is
   round and a table has a *slope discontinuity*: the flank goes from 0 to 60 degrees inside a
   few tens of metres and then stops dead.

   So the tables are placed by hand, by centre, radius and height, and the flank is one
   smoothstep across the rim. The river's fall is then put on the lip of table 0, which is why
   FALL_S and MESAS[0] have to be read from the same numbers by js/world/falls.js.
========================================================= */

const MESAS = [
  { x: 170, z: -372, r: 150, h: 96 },    // the one the river falls over: its rim is where the lip is
  { x: 620, z: -190, r: 140, h: 60 },
  { x: 210, z: -830, r: 250, h: 96 },
  { x: -480, z: -770, r: 215, h: 88 },
  { x: -720, z: -280, r: 190, h: 84 },
  { x: 760, z: 230, r: 170, h: 60 },
];

function mesaField(x, z) {
  let h = 0;
  for (const m of MESAS) {
    const d = Math.hypot(x - m.x, z - m.z) / m.r;
    if (d >= 1) continue;
    // 0.62 -> 0.97 of the radius is the flank; inside 0.62 is the flat top.
    h = Math.max(h, m.h * (1 - smoothstep(0.74, 0.965, d)));
  }
  return h;
}

function hills(x, z, r) {
  // rolling valley floor and foothills. Held down close in: the reference's foreground is one
  // open sweep of mown-green pasture from the fence line to the house, and a 34-unit swell
  // starting 130 m out breaks that into lumps you can see over.
  let h = (fbm(x * 0.0068, z * 0.0068, 4, 3) - 0.47) * 34;
  h += (fbm(x * 0.019, z * 0.019, 3, 11) - 0.5) * 12;
  h *= 0.34 + 0.66 * smoothstep(150, 430, r);
  // The tables sit on top of the swelling, not instead of it.
  h += mesaField(x, z);
  // Placed by angle, not by taste. At the overview pitch the frame tops out about 16
  // degrees above the horizon line, so a ridge 800 out has to stay under roughly 90 units
  // or it fills the sky band and the "realistic sky" turns into a strip of shadowed
  // hillside -- which is what the first version of this did. Foothills near, range held
  // back past 500, crests sitting at 2-6 degrees of elevation.
  const m = smoothstep(700, 950, r) * (1 - 0.3 * smoothstep(1250, 1400, r));
  h += Math.pow(ridge(x * 0.0011, z * 0.0011, 5, 8), 1.1) * 175 * m;
  return h;
}

// 平台内外的判据只能有一处。地形网格是极坐标的，顶点由 cos/sin × 半径乘出来，斜边回到
// 130 时带 1e-14 的舍入 —— 判据不留这个余量，r=130 那一环就有几个顶点被判成"平台外"，
// 于是河道的岸高（6~8 m）漏到平台边缘上，再顺着 92→130 这一圈 38 m 宽的斜板往里铺：
// 住宅横道东头被埋掉 0.44 m、牛舍横道东头被埋掉 2.59 m，从上面看就是"道路缺了一块"。
// 1e-6 比浮点误差大十个量级，又比任何真实几何小六个量级，不会把真的坡放进来。
const PLATEAU_EPS = 1e-6;
export const onPlateau = (x, z) => Math.hypot(x, z) <= PLATEAU_R + PLATEAU_EPS;

export function terrainHeight(x, z) {
  const r = Math.hypot(x, z);
  if (r <= PLATEAU_R + PLATEAU_EPS) return 0;

  let h = hills(x, z, r) * smoothstep(PLATEAU_R, PLATEAU_R + 105, r);

  const v = nearRiver(x, z);
  if (v) {
    const { s, d, hw, wy } = v;
    // The blend reaches four half-widths out. Narrower than this and the valley is only a
    // couple of mesh quads wide at the far end, which reads as a scratch rather than a cleft.
    const w = 1 - smoothstep(hw, hw * 4.0, d);
    if (w > 0) {
      // The gorge is fifteen metres deep along its whole length except across the fall,
      // where it shoals to about three. That is not hydrology, it is scenery: the water
      // surface is what js/world/falls.js draws the sheet on, and with a 15 m bed the
      // sheet would hang fifteen metres clear of the rock face behind it and read as a
      // white banner in mid air instead of as water going over a lip.
      const bed = wy - mix(15.0, 7.0, s) * (1 - 0.78 * Math.exp(-(((s - FALL_S) / 0.05) ** 2)));
      const bank = wy + mix(4.0, 6.5, s);
      const floor = mix(bed, bank, Math.pow(clamp(d / hw, 0, 1), 0.55));
      h = mix(h, floor, w);
    }
  }
  return h;
}

// Height of the water at the deepest point of the channel under (x, z), or null.
export function riverWaterY(x, z) {
  const v = nearRiver(x, z);
  return v && v.d <= v.hw * 1.05 ? v.wy : null;
}

// How far (x, z) sits from the river, so trees keep out of the channel and reeds keep to
// the bank. Null means "nowhere near any water".
export function riverEdge(x, z) {
  const v = nearRiver(x, z);
  return v ? { d: v.d, hw: v.hw, wy: v.wy } : null;
}

/* =========================================================
   COLOUR
   Vertex colours carry the hue; the tiled texture only breaks up the fill. Rock shows
   through where the face is steep, dry grass where the ground is high and thin.
========================================================= */

const C = {
  pasture: new THREE.Color(0x6f9a4d),
  meadow: new THREE.Color(0x8ab05c),
  dry: new THREE.Color(0xb0a267),
  forest: new THREE.Color(0x52734a),
  rock: new THREE.Color(0x7d766c),
  scree: new THREE.Color(0x9c9488),
  mud: new THREE.Color(0x7d6c50),
  // Sandstone that has been baked by a wet season and a dry one: the flank of a table is red,
  // and it is red wherever the slope goes past vertical-ish, not wherever the height does.
  cliff: new THREE.Color(0x9c5c3a),
};

function colorAt(x, z, h, steep, out) {
  if (onPlateau(x, z)) {
    // the ranch itself: the pasture it always was, just no longer one flat fill
    const p = fbm(x * 0.05, z * 0.05, 3, 21);
    out.copy(C.pasture).lerp(C.meadow, 0.25 + p * 0.5);
    if (p > 0.62) out.lerp(C.dry, (p - 0.62) * 1.4);
    return out;
  }

  const patch = fbm(x * 0.011, z * 0.011, 3, 5);
  out.copy(C.pasture).lerp(C.forest, smoothstep(0.3, 0.75, patch));
  out.lerp(C.meadow, 0.35 * fbm(x * 0.03, z * 0.03, 2, 9));
  if (patch > 0.66) out.lerp(C.dry, (patch - 0.66) * 2.1);

  // The caprock flank reads red before the generic grey rock does, and it is gated on the
  // mesa's own rim rather than on height alone -- a steep valley side at 40 m is still grass.
  //
  // The band is 1 across the flank and 0 on the top and the toe. It used to be written as
  // `1 - smoothstep(...) * smoothstep(...)`, which is 0 exactly where the cliff is and 1
  // everywhere else: the ramp ran the wrong way, so the red never appeared and every table
  // came out as a green bowl.
  let onRim = 0;
  for (const m of MESAS) {
    const d = Math.hypot(x - m.x, z - m.z) / m.r;
    if (d < 0.55 || d > 1.02) continue;
    onRim = Math.max(onRim, smoothstep(0.60, 0.665, d) * (1 - smoothstep(0.93, 0.995, d)));
  }
  const fd = Math.hypot(x - fallAt.x, z - fallAt.z);
  const gorge = fd < 140 ? smoothstep(0.28, 0.5, steep) * (1 - smoothstep(60, 140, fd)) : 0;
  onRim = Math.max(onRim, gorge);
  if (onRim > 0 && steep > 0.30) out.lerp(C.cliff, onRim * smoothstep(0.30, 0.62, steep) * 0.9);

  out.lerp(C.rock, smoothstep(0.34, 0.62, steep) * (1 - onRim * 0.6));
  out.lerp(C.scree, smoothstep(130, 280, h) * 0.6);

  // a waterline of silt and sand wherever the bank meets the river
  const v = nearRiver(x, z);
  if (v && v.d < v.hw * 1.5 && h < v.wy + 2.4) {
    out.lerp(C.mud, smoothstep(v.hw * 1.5, v.hw * 0.7, v.d) * 0.8);
  }
  return out;
}

/* =========================================================
   MESH
========================================================= */

const RADII = [46, 92, PLATEAU_R];
{
  let r = PLATEAU_R;
  while (r < HAZE_R - 120) { r *= 1.05; RADII.push(Math.min(r, HAZE_R - 80)); }
  RADII.push(HAZE_R);
}

const SIZE = HAZE_R * 2;

function build() {
  const pos = [0, 0, 0];
  const uv = [0.5, 0.5];
  const idx = [];

  for (let k = 0; k < RADII.length; ++k) {
    const r = RADII[k];
    for (let a = 0; a < ANG; ++a) {
      const th = (a / ANG) * Math.PI * 2;
      const x = Math.cos(th) * r;
      const z = Math.sin(th) * r;
      pos.push(x, terrainHeight(x, z), z);
      uv.push(x / SIZE + 0.5, z / SIZE + 0.5);
    }
  }

  // Winding is chosen so every face's normal points +Y on flat ground: (centre, next, this)
  // for the fan and the reverse quad order for the rings. Reversed, computeVertexNormals
  // hands back a downward normal, FrontSide culls the whole map and the land renders black.
  for (let a = 0; a < ANG; ++a) {
    const n = (a + 1) % ANG;
    idx.push(0, 1 + n, 1 + a);
  }
  // rings
  for (let k = 0; k < RADII.length - 1; ++k) {
    const r0 = 1 + k * ANG;
    const r1 = 1 + (k + 1) * ANG;
    for (let a = 0; a < ANG; ++a) {
      const an = (a + 1) % ANG;
      idx.push(r0 + a, r1 + an, r1 + a, r0 + a, r0 + an, r1 + an);
    }
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();

  const nrm = g.attributes.normal;
  const p = g.attributes.position;
  const col = new Float32Array(p.count * 3);
  const tmp = new THREE.Color();
  for (let i = 0; i < p.count; ++i) {
    const x = p.getX(i);
    const h = p.getY(i);
    const z = p.getZ(i);
    colorAt(x, z, h, 1 - nrm.getY(i), tmp);
    col[i * 3] = tmp.r;
    col[i * 3 + 1] = tmp.g;
    col[i * 3 + 2] = tmp.b;
  }
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  return g;
}

const gtex = groundTexture(256);

// No bumpMap here even though the other surfaces have one: the ground covers most of the
// frame, and a derivative-based bump over that many pixels costs more than it is worth.
export const ground = new THREE.Mesh(
  build(),
  new THREE.MeshStandardMaterial({
    color: 0xffffff,
    map: gtex,
    roughness: 1,
    vertexColors: true,
  })
);

ground.receiveShadow = true;
ground.name = 'terrain';
scene.add(ground);

// A quick sanity read for the probe: the profile of the valley at a few stations along the
// river, so "the water is inside its channel" is a number and not a hope.
export function riverProfileSample() {
  const rep = [];
  for (const s of [0.06, 0.2, FALL_S - 0.01, FALL_S + 0.01, 0.55, 0.8, 0.97]) {
    const i = Math.round(s * RIVER_SAMPLES);
    const p = riverPts[i];
    const wy = waterY(s);
    const hw = riverHalfWidth(s);
    rep.push({
      s,
      x: Math.round(p.x), z: Math.round(p.z),
      water: +wy.toFixed(2),
      bed: +terrainHeight(p.x, p.z).toFixed(2),
      bank: +terrainHeight(p.x + hw * 1.6, p.z).toFixed(2),
      halfWidth: +hw.toFixed(1),
    });
  }
  return rep;
}

export function lakeCenter() {
  const i = Math.round(0.62 * RIVER_SAMPLES);
  return { x: riverPts[i].x, z: riverPts[i].z, r: riverHalfWidth(0.62) };
}

// The lip and the pool, in world coordinates and metres, for js/world/falls.js and for the
// probe. Read off the same curve the channel was carved from, so the cliff cannot end up in
// a field the river never touched.
export function fallGeometry() {
  const lip = riverPoint(FALL_S);
  const back = riverPoint(FALL_S - 0.02);
  const front = riverPoint(FALL_S + 0.06);
  const dir = new THREE.Vector2(front.x - back.x, front.z - back.z);
  const len = Math.hypot(dir.x, dir.y) || 1;
  return {
    x: lip.x, z: lip.z,
    nx: dir.x / len, nz: dir.y / len,          // downstream
    top: waterY(FALL_S), bottom: waterY(FALL_S + 0.05),
    drop: waterY(FALL_S) - waterY(FALL_S + 0.05),
    width: riverHalfWidth(FALL_S) * 2,
    poolWidth: riverHalfWidth(FALL_S + 0.09) * 2,
    // how far the water travels while it falls -- the footprint the rock face has to cover
    reach: Math.hypot(riverPoint(FALL_S + 0.035).x - lip.x, riverPoint(FALL_S + 0.035).z - lip.z),
  };
}
