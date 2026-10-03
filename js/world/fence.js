// Stock fencing. What was here was fourteen thin posts and a single tube of wire per line,
// which is a boundary marker for people, not an enclosure for cattle.
//
// A paddock fence is posts every few metres with three rails and a diagonal in each panel: the
// diagonal is what stops a four-metre panel racking into a parallelogram under a cow leaning
// on it, and without it the panel is just three planks pretending to be a fence. The X reads as
// those diagonals seen from both ends of a panel.
//
// Post spacing is a metre and a half of rail per screen pixel at the overview distance, so the
// whole line stays well above the shimmer threshold that the old single wire sat under.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { timberMat, darkTimberMat } from './materials.js';
import { mergeMeshes } from './building-kit.js';
import { FENCE_LINES, GATE_W } from './layout.js';

const SPAN = 3.2;          // metres between posts
const RAILS = [0.85, 1.5, 2.15];
const parts = [];

function post(x, z, h = 2.5) {
  const m = new THREE.Mesh(new THREE.BoxGeometry(0.22, h, 0.22), timberMat);
  m.position.set(x, h / 2, z);
  m.castShadow = true;
  m.receiveShadow = true;
  parts.push(m);
}

function rail(x1, z1, x2, z2, y, mat, thick = 0.17) {
  const L = Math.hypot(x2 - x1, z2 - z1);
  const m = new THREE.Mesh(new THREE.BoxGeometry(L, thick, 0.1), mat);
  m.position.set((x1 + x2) / 2, y, (z1 + z2) / 2);
  m.rotation.y = -Math.atan2(z2 - z1, x2 - x1);
  m.castShadow = true;
  parts.push(m);
  return m;
}

// One straight run. `gaps` are fractions along it where posts and rails are left out for a
// gate -- a paddock you cannot drive into is a field, not a yard.
export function createFenceRun(x1, z1, x2, z2, { braces = true, gaps = [], gapBays = 1.1 } = {}) {
  const L = Math.hypot(x2 - x1, z2 - z1);
  const n = Math.max(1, Math.round(L / SPAN));
  const dx = (x2 - x1) / n, dz = (z2 - z1) / n;
  const centres = gaps.map(t => t * n);
  const inGap = i => centres.some(c => Math.abs(i - c) < gapBays);

  for (let i = 0; i <= n; ++i) {
    if (inGap(i - 0.5) || inGap(i + 0.5)) continue;
    post(x1 + dx * i, z1 + dz * i);
  }
  for (let i = 0; i < n; ++i) {
    if (inGap(i + 0.5)) continue;
    const ax = x1 + dx * i, az = z1 + dz * i;
    const bx = ax + dx, bz = az + dz;
    for (const y of RAILS) rail(ax, az, bx, bz, y, darkTimberMat);
    if (braces && i % 2 === 0) {
      // The diagonal is in the plane of the panel: the rail builder lays the box along the
      // run, and Euler order XYZ applies rotation.z innermost, so tilting about local Z
      // raises one end of the plank inside that plane. Two mirrored planks are the X.
      rail(ax, az, bx, bz, 1.5, timberMat, 0.13).rotation.z = 0.62;
      rail(bx, bz, ax, az, 1.5, timberMat, 0.13).rotation.z = 0.62;
    }
  }
  // the gate posts stand either side of each gap, taller than the line
  for (const gap of centres) {
    for (const s of [-gapBays / 2, gapBays / 2]) {
      const i = gap + s;
      post(x1 + dx * i, z1 + dz * i, 3.1);
    }
  }
}

/* 大门两边那道地界栅栏这一轮撤了（他点名"大门两边的栅栏也清除掉"）：原来它从 x=-80 拉到
   牌坊左边、再从牌坊右边拉到 x=80，牌坊开在中间。撤掉之后牌坊单独立在大道头上，
   地界线本身不再是画面里的一条东西 —— FENCE_Z 还留着，因为 gate.js 拿它定牌坊的落位。 */

/* 全场就这一片牲口栅栏：西边那个大院，外围四条边 + 里面几条分隔，一条一条从 layout.js 的
   FENCE_LINES 来。按圈画矩形是不行的 —— 相邻两片共用的那条边会被画两遍，两根柱子立在同一个
   y 上，那就是这项目最早那套共面闪烁。门宽跟着门外那条路走。
   农机房那边没有栅栏（他上一轮的规矩，这一轮它搬到大院东墙外，规矩照旧）。 */
for (const f of FENCE_LINES) {
  const gaps = f.gateAt !== undefined ? [f.gateAt] : (f.gates || []);
  const bays = (f.gateW ?? GATE_W) / (2 * SPAN);
  const A = f.axis === 'x' ? [f.from, f.at] : [f.at, f.from];
  const B = f.axis === 'x' ? [f.to, f.at] : [f.at, f.to];
  createFenceRun(A[0], A[1], B[0], B[1], { gaps, gapBays: bays });
}


mergeMeshes(scene, parts, true);
