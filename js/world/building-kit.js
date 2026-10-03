// Building parts. Everything here is assembled from boxes and prisms, but assembled the way
// a crew would: a foundation the wall sits on, wall plates and corner boards, a roof that
// overhangs the walls it is supposed to keep dry, windows with a frame, a sill and a mullion
// rather than a glowing rectangle, and the clutter a real roof grows by now.
//
// The old buildings were one box plus a four-sided cone, which is why they read as
// placeholders: a cone of radius 15 on a 23x16 body did not even fit the walls it capped.
import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { trimMat, fasciaMat, glassMat, frameMat, roofMat, gableMat, stoneMat, ridgeMat } from './materials.js';

export function box(g, w, h, d, mat, x, y, z, ry = 0) {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  m.position.set(x, y, z);
  if (ry) m.rotation.y = ry;
  // Only the masses cast. A shadow map pass over every sill and mullion would double the
  // draw calls to buy nothing you can see.
  m.castShadow = Math.max(w, h, d) > 0.7;
  m.receiveShadow = true;
  g.add(m);
  return m;
}

// A roof panel spanning (z0,y0) to (z1,y1) in cross-section, extruded along X.
function roofSlab(g, { lenX, z0, y0, z1, y1, thick, mat, overhang = 0.35 }) {
  const dz = z1 - z0;
  const dy = y1 - y0;
  const L = Math.hypot(dz, dy);
  const m = new THREE.Mesh(new THREE.BoxGeometry(lenX, thick, L + overhang * 2), mat);
  m.position.set(0, (y0 + y1) / 2, (z0 + z1) / 2);
  m.rotation.x = Math.atan2(-dy, dz);
  m.castShadow = true;
  m.receiveShadow = true;
  metreUV(m, ROOF_U, ROOF_V);
  g.add(m);
  return m;
}

/* =========================================================
   TILING A ROOF BY METRES
   Same argument js/world/road.js carries: a BoxGeometry's UV is 0..1 whatever the box is,
   so with a shared repeat the courses on a 25 m barn roof and a 2 m vent cap are the same
   count -- the barn gets tiles a metre and a half deep and the vent gets stripes. Rewriting
   the UV from the vertex positions in metres makes one course the same physical height
   everywhere, which is the only version a roof can be.
========================================================= */

const ROOF_U = 2.6;   // metres per texture repeat across the slope (6 half-barrels)
const ROOF_V = 2.8;   // metres per repeat up the slope (8 courses)

export function metreUV(mesh, perU = ROOF_U, perV = ROOF_V) {
  const p = mesh.geometry.attributes.position;
  const uv = mesh.geometry.attributes.uv;
  if (!p || !uv) return mesh;
  for (let i = 0; i < uv.count; ++i) {
    uv.setXY(i, p.getX(i) / perU, p.getZ(i) / perV);
  }
  uv.needsUpdate = true;
  return mesh;
}

// A quadrilateral slab: four corners in metres, `thick` of visual thickness dropped straight
// down from the eave. This is what a hip roof is made of -- four of these, two trapezoids and
// two triangles, and no cone in sight. Corners wind counter-clockwise seen from outside.
function panel(g, c, thick, mat) {
  const [p0, p1, p2, p3] = c;
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const norm = a => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

  const eave = sub(p1, p0);
  const U = norm([eave[0], 0, eave[2]]);                       // along the eave, level
  const n = norm(cross(eave, sub(p2, p0)));                     // out of the roof
  const V = cross(n, U);                                        // up the slope

  const pos = [], uvs = [], idx = [];
  const put = (pt, top) => {
    pos.push(pt[0], pt[1] + (top ? 0 : -thick), pt[2]);
    const d = sub(pt, p0);
    uvs.push((d[0] * U[0] + d[1] * U[1] + d[2] * U[2]) / ROOF_U,
             (d[0] * V[0] + d[1] * V[1] + d[2] * V[2]) / ROOF_V);
    return pos.length / 3 - 1;
  };
  const t = c.map(pt => put(pt, true));
  const b = c.map(pt => put(pt, false));
  const q = (a, bb, cc, d) => idx.push(a, bb, cc, a, cc, d);
  q(t[0], t[1], t[2], t[3]);
  q(b[3], b[2], b[1], b[0]);
  for (let k = 0; k < 4; ++k) {
    const a = k, nx = (k + 1) % 4;
    q(t[nx], t[a], b[a], b[nx]);
  }

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  geo.setIndex(idx);
  geo.computeVertexNormals();
  const m = new THREE.Mesh(geo, mat);
  m.castShadow = true;
  m.receiveShadow = true;
  g.add(m);
  return m;
}

// Four slopes meeting on a ridge. w > d, so the ridge runs along X and is w - d long; the two
// ends close as triangles, which is what makes it read as a hip rather than a gable.
export function hipRoof(g, { w, d, wallTop, rise, ov = 1.15, thick = 0.32, mat, cx = 0 }) {
  const ex = w / 2 + ov;
  const ez = d / 2 + ov;
  const y0 = wallTop;
  const y1 = wallTop + rise;
  const rx = Math.max(ex - ez, 0.5);

  const fe = [cx + ex, y0 - 0.15, ez];        // front eave corners
  const fw = [cx - ex, y0 - 0.15, ez];
  const be = [cx + ex, y0 - 0.15, -ez];       // back eave corners
  const bw = [cx - ex, y0 - 0.15, -ez];
  const rgt = [cx + rx, y1, 0];               // ridge ends
  const lft = [cx - rx, y1, 0];

  panel(g, [fw, fe, rgt, lft], thick, mat);
  panel(g, [be, bw, lft, rgt], thick, mat);
  panel(g, [fe, be, rgt, rgt], thick, mat);
  panel(g, [bw, fw, lft, lft], thick, mat);

  // fascia all the way round, and a cap on the ridge
  box(g, ex * 2, 0.5, 0.16, fasciaMat, cx, y0 - 0.4, ez);
  box(g, ex * 2, 0.5, 0.16, fasciaMat, cx, y0 - 0.4, -ez);
  box(g, 0.16, 0.5, ez * 2, fasciaMat, cx + ex, y0 - 0.4, 0);
  box(g, 0.16, 0.5, ez * 2, fasciaMat, cx - ex, y0 - 0.4, 0);
  box(g, rx * 2 + 0.3, 0.34, 0.5, fasciaMat, cx, y1 + 0.18, 0);
  return { ridgeY: y1, ridgeHalf: rx };
}

// Two slopes meeting at a ridge. `rise` is how far the ridge stands above the wall plate.
export function gableRoof(g, { w, d, wallTop, rise, ov = 1.1, thick = 0.34, mat }) {
  const half = d / 2 + ov;
  const y0 = wallTop;
  const y1 = wallTop + rise;
  const a = roofSlab(g, { lenX: w + ov * 2, z0: 0, y0: y1, z1: half, y1: y0 - 0.15, thick, mat });
  const b = roofSlab(g, { lenX: w + ov * 2, z0: 0, y0: y1, z1: -half, y1: y0 - 0.15, thick, mat });

  // fascia: the board nailed to the rafter tails, without which an eave looks like paper
  box(g, w + ov * 2, 0.5, 0.16, fasciaMat, 0, y0 - 0.15 - 0.1, half);
  box(g, w + ov * 2, 0.5, 0.16, fasciaMat, 0, y0 - 0.15 - 0.1, -half);

  // ridge cap
  box(g, w + ov * 2 + 0.2, 0.22, 0.5, fasciaMat, 0, y1 + 0.12, 0);

  // gable ends: the triangular wall that closes the box under the roof
  for (const sx of [-1, 1]) {
    const shape = new THREE.Shape();
    shape.moveTo(-half, 0);
    shape.lineTo(half, 0);
    shape.lineTo(0, rise);
    shape.closePath();
    const tri = new THREE.Mesh(new THREE.ShapeGeometry(shape), gableMat);
    tri.position.set(sx * w / 2, y0, 0);
    tri.rotation.y = sx * Math.PI / 2;
    tri.receiveShadow = true;
    g.add(tri);
  }
  return { a, b, ridgeY: y1 };
}

// The swept barn roof: steep lower slope, shallow upper one, which is what lets a barn keep
// a wide door and a narrow loft at the same time.
export function gambrelRoof(g, { w, d, wallTop, lower, upper, ov = 1.25, thick = 0.34, mat }) {
  const eave = d / 2 + ov;
  const breakZ = d * 0.17;
  const y0 = wallTop;
  const y1 = wallTop + lower;
  const y2 = wallTop + lower + upper;
  for (const sz of [-1, 1]) {
    roofSlab(g, { lenX: w + ov * 2, z0: sz * eave, y0: y0 - 0.2, z1: sz * breakZ, y1, thick, mat });
    roofSlab(g, { lenX: w + ov * 2, z0: sz * breakZ, y0: y1, z1: 0, y1: y2, thick, mat });
    box(g, w + ov * 2, 0.55, 0.18, fasciaMat, 0, y0 - 0.35, sz * eave);
  }
  box(g, w + ov * 2 + 0.2, 0.24, 0.55, fasciaMat, 0, y2 + 0.12, 0);
  return { ridgeY: y2 };
}

// A window: frame, glass, sill, head board and a mullion cross. `face` says which way the
// wall it sits in is pointing; the unit is placed just proud of that wall.
export function windowUnit(g, { x, y, z, w, h, face = 'z', dir = 1, glass, shutters = false, shuttersMat }) {
  const f = 0.16;
  const t = 0.1;
  const at = (dx, dy, dz, bw, bh, bd, mat) => {
    const p = face === 'z' ? [x + dx, y + dy, z + dz * dir] : [x + dz * dir, y + dy, z + dx];
    const s = face === 'z' ? [bw, bh, bd] : [bd, bh, bw];
    return box(g, s[0], s[1], s[2], mat, p[0], p[1], p[2]);
  };

  at(0, 0, t / 2, w + f * 2, h + f * 2, t, frameMat);          // casing
  const pane = at(0, 0, t + 0.03, w, h, 0.07, glass || glassMat); // glass
  at(0, -h / 2 - f - 0.06, t + 0.05, w + f * 2 + 0.5, 0.3, 0.42, trimMat);  // sill
  at(0, h / 2 + f + 0.12, t + 0.02, w + f * 2 + 0.2, 0.2, 0.24, trimMat);   // head board
  at(0, 0, t + 0.07, 0.1, h, 0.05, frameMat);                  // mullions
  at(0, 0, t + 0.07, w, 0.1, 0.05, frameMat);

  if (shutters) {
    const sw = w / 2 + 0.12;
    at(-w / 2 - sw / 2 - 0.06, 0, t + 0.02, sw, h + 0.1, 0.1, shuttersMat || trimMat);
    at(w / 2 + sw / 2 + 0.06, 0, t + 0.02, sw, h + 0.1, 0.1, shuttersMat || trimMat);
  }
  return pane;
}

export function doorUnit(g, { x, y, z, w, h, face = 'z', dir = 1, mat, step = true, panels = true }) {
  const f = 0.22;
  const onZ = face === 'z';
  const put = (ox, oy, bw, bh, bd, m, dz = 0) => {
    if (onZ) return box(g, bw, bh, bd, m, x + ox, y + oy, z + dz * dir);
    return box(g, bd, bh, bw, m, x + dz * dir, y + oy, z + ox);
  };
  put(0, 0, w + f * 2, h + f, 0.26, frameMat);                  // casing
  const leaf = put(0, 0, w, h, 0.14, mat, 0.2);                 // door itself
  if (panels) {
    put(0, h * 0.22, w * 0.66, h * 0.34, 0.06, mat, 0.29);
    put(0, -h * 0.22, w * 0.66, h * 0.34, 0.06, mat, 0.29);
  }
  put(w * 0.34, 0, 0.12, 0.12, 0.12, frameMat, 0.32);           // knob
  if (step) {
    put(0, -h / 2 - 0.16, w + 1.5, 0.32, 1.1, trimMat, 0.45);   // threshold
    put(0, -h / 2 - 0.46, w + 2.1, 0.3, 1.7, trimMat, 0.8);     // bottom step
  }
  return leaf;
}

// The concrete the walls actually stand on.
export function plinth(g, w, d, h, mat) {
  return box(g, w + 0.5, h, d + 0.5, mat, 0, h / 2, 0);
}

export function cornerBoards(g, w, d, h, y = 0) {
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    box(g, 0.34, h, 0.34, trimMat, sx * (w / 2 - 0.05), y + h / 2, sz * (d / 2 - 0.05));
  }
}

// Belt course: the horizontal trim that marks a floor line, or just breaks up a tall wall.
export function beltCourse(g, w, d, y) {
  box(g, w + 0.42, 0.26, 0.2, trimMat, 0, y, d / 2 + 0.06);
  box(g, w + 0.42, 0.26, 0.2, trimMat, 0, y, -d / 2 - 0.06);
  box(g, 0.2, 0.26, d + 0.42, trimMat, w / 2 + 0.06, y, 0);
  box(g, 0.2, 0.26, d + 0.42, trimMat, -w / 2 - 0.06, y, 0);
}

export function downpipe(g, w, d, h, sx = 1, sz = 1) {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(0.14, 0.14, h, 7), fasciaMat);
  m.position.set(sx * (w / 2 - 0.1), h / 2, sz * (d / 2 + 0.12));
  m.castShadow = true;
  g.add(m);
  const elbow = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.3, 0.9), fasciaMat);
  elbow.position.set(sx * (w / 2 - 0.1), 0.35, sz * (d / 2 + 0.5));
  g.add(elbow);
}

// Roof furniture: a vent and a condenser. Two things every working roof has and no
// placeholder one does.
export function roofVent(g, x, y, z, r = 0.42, h = 0.9) {
  const c = new THREE.Mesh(new THREE.CylinderGeometry(r, r * 1.15, h, 9), roofMat);
  c.position.set(x, y, z);
  c.castShadow = true;
  g.add(c);
  const cap = new THREE.Mesh(new THREE.CylinderGeometry(r * 1.35, r * 1.35, 0.12, 9), fasciaMat);
  cap.position.set(x, y + h / 2 + 0.1, z);
  g.add(cap);
}

export function condenser(g, x, y, z, s = 1) {
  const body = box(g, 2.1 * s, 1.1 * s, 1.5 * s, fasciaMat, x, y, z);
  const fan = new THREE.Mesh(new THREE.CylinderGeometry(0.62 * s, 0.62 * s, 0.12, 12), roofMat);
  fan.position.set(x, y + 0.58 * s, z);
  g.add(fan);
  return body;
}

// A porch: posts, a shed roof, a deck. The one piece of a house that tells you where to go in.
export function porch(g, { x, z, w, d, h, dir = 1 }) {
  box(g, w, 0.26, d, trimMat, x, 0.15, z + dir * d / 2);
  for (const sx of [-1, 1]) {
    box(g, 0.26, h, 0.26, trimMat, x + sx * (w / 2 - 0.2), h / 2 + 0.2, z + dir * (d - 0.25));
  }
  const roof = box(g, w + 0.5, 0.2, d + 0.7, roofMat, x, h + 0.4, z + dir * d / 2);
  roof.rotation.x = dir * 0.1;
  box(g, w + 0.5, 0.3, 0.14, fasciaMat, x, h + 0.28, z + dir * (d + 0.35));
  for (const sx of [-1, 1]) {
    box(g, 0.2, 0.2, d, trimMat, x + sx * (w / 2 - 0.1), h + 0.15, z + dir * d / 2);
  }
}

/* =========================================================
   中式大屋顶
   一张矩形檐口逐层内缩到正脊，蒙成曲面。两件事决定它读不读得出是中式：
   1. 举折 -- 坡度不是常数，檐口近处平、往脊上越来越陡，所以高度用 v^curve 而不是线性插值；
   2. 起翘 -- 檐口线在中段沉下去、到四角甩上来，用「到最近角点的距离」的幂函数当抬升权重。
   两者都不做就是一个四棱锥；只做起翘不做举折，是一个平棚上加了四个翘尖。

   屋顶是有厚度的：每一层都往下掉 thick，所以从下面看是檐口板而不是纸片。
========================================================= */
export function sweptHipRoof(g, {
  w, d, wallTop, rise, ov = 1.5, lift = 0.85, thick = 0.3, mat,
  curve = 1.75, cornerPower = 4, ringN = 8, cols = 64, cx = 0,
} = {}) {
  const hx = w / 2 + ov;
  const hz = d / 2 + ov;
  const rx = Math.max(hx - hz, 0.35);          // 脊的半长：内缩到进深为 0 时剩下的那段

  // 一圈矩形上的第 t 份：返回 [x, z, 角部权重, 周长]。权重按「到本边两端角点的距离」算，
  // 所以角点是峰值 1、边中最低（cornerPower=4 时 0.125），檐口是连续甩上去的。
  const ringAt = (v, t) => {
    const ax = hx + (rx - hx) * v;
    const az = hz * (1 - v);
    const sides = [[-ax, -az, ax, -az], [ax, -az, ax, az], [ax, az, -ax, az], [-ax, az, -ax, -az]];
    const lens = sides.map(s => Math.hypot(s[2] - s[0], s[3] - s[1]));
    const per = lens.reduce((a, b) => a + b, 0) || 1;
    let u = t * per;
    for (let i = 0; i < 4; ++i) {
      if (u <= lens[i] || i === 3) {
        const s = Math.min(1, u / (lens[i] || 1));
        const [x0, z0, x1, z1] = sides[i];
        return [x0 + (x1 - x0) * s, z0 + (z1 - z0) * s,
          Math.pow(s, cornerPower) + Math.pow(1 - s, cornerPower), per];
      }
      u -= lens[i];
    }
  };
  const heightAt = (v, cw) => wallTop + rise * Math.pow(v, curve) + lift * cw * Math.pow(1 - v, 2);

  // 每圈 cols 个点、top/bot 两排。列不是按周长等分，而是每边等分 —— 因为四个角点必须
  // 各落在一个采样上：按周长等分时角点落在两个采样中间，那条弦把檐口的角切掉一块（这个
  // 屋顶上角点到弦的垂距 0.36 m），翘角翘不起来，而且垂脊沿真角点走、屋面沿弦走，脊就
  // 甩在瓦面之外（量出来有 2 个采样落在网格外，脊心到瓦面 0.289 m 对设计 0.154 m）。
  // 按边等分之后角点正好是网格顶点，脊和瓦面共用同一批点：0.149/0.153/0.154，无一落空。
  const segN = Math.max(2, Math.round(cols / 4));   // 每条边的分段数
  const colsAt = (v) => {
    const ax = hx + (rx - hx) * v, az = hz * (1 - v);
    const cums = [0, 2 * ax, 2 * ax + 2 * az, 4 * ax + 2 * az, 4 * (ax + az)];
    const cu = [];
    for (let i = 0; i < 4; ++i) for (let j = 0; j < segN; ++j) cu.push(cums[i] + (j / segN) * (cums[i + 1] - cums[i]));
    return { cu, per: cums[4] || 1 };
  };

  const pos = [], uvs = [], idx = [];
  const put = (p, y, uu, vv) => { pos.push(p.x + cx, y, p.z); uvs.push(uu, vv); return pos.length / 3 - 1; };
  const quad = (a, b, c, dd) => idx.push(a, b, c, a, c, dd);

  // 坡长按 0 号列的三维距离逐圈累加，瓦的行距才是按米的；
  // 直接拿 v 乘一个名义坡长，会让檐口那几行比脊上那几行宽一倍。
  const rows = [];
  let upAcc = 0;
  for (let i = 0; i <= ringN; ++i) {
    const v = i / ringN;
    const { cu, per } = colsAt(v);
    const pts = cu.map(c => { const p = ringAt(v, c / per); return { x: p[0], z: p[1], y: heightAt(v, p[2]) }; });
    if (i) {
      const q = rows[i - 1];
      upAcc += Math.hypot(pts[0].y - q.y0, pts[0].z - q.z0, pts[0].x - q.x0);
    }
    const top = [], bot = [];
    for (let k = 0; k < cu.length; ++k) {
      const uu = cu[k] / ROOF_U;
      const vv = upAcc / ROOF_V;
      top[k] = put(pts[k], pts[k].y, uu, vv);
      bot[k] = put(pts[k], pts[k].y - thick, uu, vv);
    }
    rows.push({ top, bot, x0: pts[0].x, z0: pts[0].z, y0: pts[0].y });
  }

  for (let i = 0; i < ringN; ++i) {
    const A = rows[i], B = rows[i + 1], n = A.top.length;
    for (let k = 0; k < n; ++k) {
      const k2 = (k + 1) % n;
      quad(A.top[k], A.top[k2], B.top[k2], B.top[k]);          // 外面
      quad(B.bot[k], B.bot[k2], A.bot[k2], A.bot[k]);           // 里面（从下看是檐口底）
    }
  }
  const R = rows[0];
  for (let k = 0; k < R.top.length; ++k) {
    const k2 = (k + 1) % R.top.length;
    quad(R.top[k2], R.top[k], R.bot[k], R.bot[k2]);             // 檐口收边，屋顶不再是纸片
  }

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  geo.setIndex(idx);
  geo.computeVertexNormals();
  const m = new THREE.Mesh(geo, mat);
  m.castShadow = true;
  m.receiveShadow = true;
  g.add(m);

  const ridgeY = wallTop + rise;
  // 四条垂脊走的是角点轨迹，而角点轨迹在 xz 上是直线（ax、az 都随 v 线性变），在 y 上是
  // 逐圈线性的 —— 也就是说它就是屋面网格的那条棱。所以这里按半圈采样、y 取两圈之间的线性
  // 插值，脊和瓦面用的是同一根折线；用 heightAt 的解析值当控制点会让脊按举折曲线走，比
  // 网格鼓出来 0.135 m（0.289 对 0.154），看上去就是"脊梁没贴合屋顶"。
  const cornerT = (v) => {
    const ax = hx + (rx - hx) * v;
    const az = hz * (1 - v);
    const P = 4 * (ax + az) || 1;
    return [0, 2 * ax / P, (2 * ax + 2 * az) / P, (4 * ax + 2 * az) / P];
  };
  const nodeY = [];
  for (let i = 0; i <= ringN; ++i) nodeY.push(heightAt(i / ringN, 1));   // 角点权重恒为 1
  const chordY = (v) => {
    const b = Math.min(ringN - 1, Math.floor(v * ringN));
    const f = v * ringN - b;
    return nodeY[b] + (nodeY[b + 1] - nodeY[b]) * f;
  };
  const hipCurves = [0, 1, 2, 3].map(ci => {
    const pts = [];
    for (let i = 0; i <= ringN * 2; ++i) {
      const v = i / (ringN * 2);
      const p = ringAt(v, cornerT(v)[ci]);
      pts.push(new THREE.Vector3(p[0] + cx, chordY(v) + thick * 0.55, p[1]));
    }
    return new THREE.CatmullRomCurve3(pts);
  });
  ridgeAndSpines(g, { cx, rx, ridgeY, hipCurves });
  return { ridgeY, ridgeHalf: rx, cornerY: heightAt(0, 1), hx, hz };
}

// 正脊两端抬高、收尾（吻兽）。脊和垂脊都落在瓦面之上，因为它们是按同一张曲面采样出来的。
function ridgeAndSpines(g, { cx, rx, ridgeY, hipCurves }) {
  box(g, rx * 2 + 0.9, 0.42, 0.5, ridgeMat, cx, ridgeY + 0.30, 0);
  box(g, rx * 2 + 1.3, 0.26, 0.66, ridgeMat, cx, ridgeY + 0.60, 0);
  for (const sx of [-1, 1]) {
    box(g, 0.42, 0.95, 0.62, ridgeMat, cx + sx * (rx + 0.45), ridgeY + 0.78, 0);   // 正吻
    box(g, 0.3, 0.4, 0.3, ridgeMat, cx + sx * (rx + 0.45), ridgeY + 1.36, 0);
  }
  for (const c of hipCurves) {
    const tube = new THREE.Mesh(new THREE.TubeGeometry(c, 14, 0.2, 6, false), ridgeMat);
    tube.castShadow = true;
    g.add(tube);
  }
}

/* 檐柱 + 额枋 + 斗拱暗示：一圈柱子顶着檐口，柱头之间一道阑额，
   柱头上面两层小方木叠出挑（斗拱在这里是"读得出有"，不是按清式科数摆全）。 */
export function colonnade(g, { w, d, h, y0 = 0, baysX = 4, baysZ = 2, mat, r = 0.28 }) {
  const posts = [];
  const at = (x, z) => {
    const col = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.92, r, h, 10), mat);
    col.position.set(x, y0 + h / 2, z);
    col.castShadow = true;
    g.add(col);
    const plinth = new THREE.Mesh(new THREE.CylinderGeometry(r * 1.5, r * 1.7, 0.3, 12), stoneMat);
    plinth.position.set(x, y0 + 0.15, z);
    g.add(plinth);
    posts.push([x, z]);
  };
  for (let i = 0; i <= baysX; ++i) {
    const x = -w / 2 + (w * i) / baysX;
    at(x, d / 2); at(x, -d / 2);
  }
  for (let i = 1; i < baysZ; ++i) {
    const z = -d / 2 + (d * i) / baysZ;
    at(w / 2, z); at(-w / 2, z);
  }
  // 阑额：柱头之间一道横枋，前后左右各一条
  for (const sz of [1, -1]) box(g, w + 0.3, 0.42, 0.24, mat, 0, y0 + h - 0.3, sz * d / 2);
  for (const sx of [1, -1]) box(g, 0.24, 0.42, d + 0.3, mat, sx * w / 2, y0 + h - 0.3, 0);
  // 斗拱暗示：每个柱头上一斗一升，出挑 0.34
  for (const [x, z] of posts) {
    box(g, 0.62, 0.2, 0.62, mat, x, y0 + h + 0.02, z);
    box(g, 0.9, 0.16, 0.34, mat, x, y0 + h + 0.2, z);
    box(g, 0.34, 0.16, 0.9, mat, x, y0 + h + 0.2, z);
  }
  return posts;
}

/* 格扇窗：外框 + 竖抹头分成几扇 + 每扇上半是斜棂格子、下半是裙板。
   格子用一组细木条拼，不用贴图 —— 这个技能不发纹理，而且格子在斜视角下必须是真几何，
   不然一转动就糊成一片灰。 */
export function latticeWindow(g, { x, y, z, w, h, face = 'z', dir = 1, mat, glow, panes = 3 }) {
  const onZ = face === 'z';
  const put = (ox, oy, bw, bh, m, dz = 0) => onZ
    ? box(g, bw, bh, 0.12, m, x + ox, y + oy, z + dz * dir)
    : box(g, 0.12, bh, bw, m, x + dz * dir, y + oy, z + ox);
  put(0, 0, w + 0.24, h + 0.24, mat);                       // 外框
  const pane = put(0, 0, w, h, glow || glassMat, 0.06);      // 扇心：调用方拿它驱动室内灯
  const pw = w / panes;
  for (let i = 1; i < panes; ++i) put(-w / 2 + i * pw, 0, 0.1, h, mat, 0.14);
  put(0, h * 0.16, w, 0.1, mat, 0.14);                      // 上抹头：格子与裙板之分
  for (let i = 0; i < panes; ++i) {
    const cxx = -w / 2 + pw * (i + 0.5);
    for (let k = -2; k <= 2; ++k) put(cxx + (k * pw) / 5.5, h * 0.4, 0.06, h * 0.36, mat, 0.16);
    for (let k = 1; k <= 2; ++k) put(cxx, h * 0.4 - h * 0.12 + k * h * 0.12, pw * 0.86, 0.06, mat, 0.16);
  }
  for (const sx of [-1, 1]) put(sx * (w / 2 + 0.16), 0, 0.3, h + 0.2, mat, 0.1);  // 抱框
  put(0, -h / 2 - 0.18, w + 0.6, 0.22, mat, 0.12);                                // 下槛
  return pane;
}

/* 台基：石头台子 + 前后踏跺。房子不是坐在地上，是坐在台子上。
   原来踏跺两侧还有一圈栏杆（两只望柱 + 一根寻杖），撤掉了：望柱立在台基边沿上、
   寻杖横在 1.71~1.85 m，而最上一级踏跺的面在 0.8 m —— 中间空着 0.9 m，从斜上方看就是
   一根杆子悬在台阶上头；而且它正好穿过檐柱，柱子和杆子叠在一起更像一块碎片。 */
export function stonePodium(g, { w, d, h = 0.9, mat, steps = 5 }) {
  box(g, w + 2.4, h, d + 2.4, mat, 0, h / 2, 0);
  // 上枋往下沉 0.05：它的底面如果正好压在台基顶面同一个 y 上，两块同材质的共面会被
  // mergeGroup 并进同一个 buffer，深度测试就成了抛硬币 —— 转视角时那一片会闪。
  box(g, w + 3.0, 0.22, d + 3.0, mat, 0, h - 0.05 + 0.11, 0);      // 上枋
  for (const sz of [1, -1]) {
    for (let i = 0; i < steps; ++i) {
      box(g, 5.2 - i * 0.2, h / steps, 0.5, mat, 0, (h / steps) * (steps - i) - h / steps / 2,
        sz * (d / 2 + 1.2 + 0.5 * i + 0.25));
    }
  }
  return h;
}

/* =========================================================
   STATIC MERGE
   A gable roof, a row of sills and a dozen corner boards is fifty draw calls for something
   that never moves. Merging by material takes the ranch from ~880 calls to a few dozen,
   which is most of the frame time when the renderer has no real GPU behind it.
   Anything that has to stay addressable -- an emissive window the lighting board drives,
   the tank the water level animates -- is passed in `keep` and left alone.
========================================================= */

export function mergeMeshes(parent, meshes, world = false) {
  const buckets = new Map();
  for (const m of meshes) {
    if (!m.isMesh || m.userData.noMerge) continue;
    const g = m.geometry;
    if (!g.attributes.position || !g.attributes.normal || !g.attributes.uv) continue;
    if (g.attributes.color || g.attributes.uv1 || g.attributes.uv2) continue;
    const key = m.material.uuid + '|' + (g.index ? 'i' : 'n');
    let list = buckets.get(key);
    if (!list) { list = []; buckets.set(key, list); }
    list.push(m);
  }

  let before = 0;
  let after = 0;
  for (const [, list] of buckets) {
    if (list.length < 2) continue;
    const geos = list.map(m => {
      if (world) m.updateWorldMatrix(true, false);
      else m.updateMatrix();
      return m.geometry.clone().applyMatrix4(world ? m.matrixWorld : m.matrix);
    });
    const merged = mergeGeometries(geos, false);
    if (!merged) { geos.forEach(x => x.dispose()); continue; }
    const out = new THREE.Mesh(merged, list[0].material);
    out.castShadow = list.some(m => m.castShadow);
    out.receiveShadow = list.some(m => m.receiveShadow);
    out.userData.merged = list.length;
    parent.add(out);
    for (const m of list) { m.removeFromParent(); before++; }
    geos.forEach(x => x.dispose());
    after++;
  }
  return { before, after };
}

export function mergeGroup(g, keep = []) {
  const k = new Set(keep);
  return mergeMeshes(g, g.children.filter(o => o.isMesh && !k.has(o)));
}
