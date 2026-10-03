// Water mains. What was here were five straight runs shot across the yard between building
// centres -- a pipe leaving a barn wall at 30 degrees, crossing the drive and the front court,
// and ending in the middle of the paddock. Nothing in a farm's water system runs like that:
// the mains hug the road, and the branches come off them at right angles into one building.
//
// Second rule, added when the roads were re-planned: a pipe may hug a road but it may not run
// in one -- every stretch that entered a road band dove under it and came up again on the far
// side, with a ramp either side, and later the same rule covered the yard fences too. This
// round the whole network went below grade, so the dive, the ramps, the stoup posts and the
// road-band arithmetic all went with it. What is left is the route, at one depth.
//
// The route is still derived from js/world/layout.js (the same verge offset the pole line uses,
// each building's own site plus its measured eave so a terminus stops outside the wall), so a
// building that moves takes its branch with it.
//
// js/ui/water-visual.js repaints each circuit's material when a valve opens, so segments of
// one line share one material and different lines never do -- and nothing here is merged,
// because a merged mesh cannot be lit per circuit. The pipes are out of sight now; the
// circuits still exist in the model, which is what the water panel reads.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { SITES, PLOTS, PIPE_X, HOUSE_Z, HOUSE_FRONT, RES_LINE_Z, EAVE } from './layout.js';

const verge = PIPE_X;    // 干管贴着大道路肩外沿，左右各一条
const DEPTH = -0.95;     // 管轴线高：全场埋地。比电缆（-0.55）低一层，两条在路口交叉不打架
const R = 0.13;          // 管外径
const CLEAR = 0.5;       // 管口停在檐外这么远：伸进屋里就穿过了屋顶，收在墙里看不见

export const waterPipes = [];

const mats = new Map();
function matFor(line) {
  let m = mats.get(line);
  if (!m) {
    m = new THREE.MeshStandardMaterial({ color: 0x178bb8, emissive: 0x064d68, emissiveIntensity: 0.45, roughness: 0.55 });
    mats.set(line, m);
  }
  return m;
}

function tube(A, B, line) {
  const a = new THREE.Vector3(A[0], A[1], A[2]), b = new THREE.Vector3(B[0], B[1], B[2]);
  const dir = new THREE.Vector3().subVectors(b, a);
  const len = dir.length();
  if (len < 0.01) return;
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(R, R, len, 9), matFor(line));
  mesh.position.copy(a).add(b).multiplyScalar(0.5);
  mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
  mesh.userData.line = line;
  scene.add(mesh);
  waterPipes.push(mesh);
}

function elbow(p, line) {
  const m = new THREE.Mesh(new THREE.SphereGeometry(R * 1.35, 10, 8), matFor(line));
  m.position.set(p[0], DEPTH, p[1]);
  m.userData.line = line;
  scene.add(m);
  waterPipes.push(m);
}

// 一条折线：分段 + 拐点加弯头，全部在同一个深度上。
function run(line, pts) {
  for (let i = 0; i + 1 < pts.length; ++i) {
    tube([pts[i][0], DEPTH, pts[i][1]], [pts[i + 1][0], DEPTH, pts[i + 1][1]], line);
    elbow(pts[i + 1], line);
  }
}

const tap = (site, off) => (off.z !== undefined ? site.z + off.z + Math.sign(off.z) * CLEAR
                                                : site.x + off.x + Math.sign(off.x) * CLEAR);

/* ---- 两条干管：沿大道左右路肩，铺到主屋门前为止。
   收在 HOUSE_FRONT 而不是 HOUSE_Z：铺到屋心就等于从房子底下穿过去（挪房子之后第一版就是这么
   量的，最后 10 m 落在房屋包围盒里）。---- */
run('main', [[-verge, 60], [-verge, HOUSE_Z + HOUSE_FRONT]]);
run('main', [[verge, 60], [verge, HOUSE_Z + HOUSE_FRONT]]);

/* ---- 屋后那条：六户住宅的给水。走廊就是住宅排后檐（-88.9）和主屋前檐（-93.95）之间那
   5.05 m，干管走在中线 z=-91 上，六户各自往后院接一口（1.6 m 短接管）。
   上一版这条还兼着给水那一排，现在那三栋搬到东边去了。 ---- */
const FEED_Z = -91;
run('main', [[-70, FEED_Z], [70, FEED_Z]]);
for (const p of PLOTS) run('house', [[p.x, FEED_Z], [p.x, RES_LINE_Z + EAVE.res.z - CLEAR]]);

/* ---- 主横道那条分配干管：从大院里的牛舍、仓库，出东墙经过农机房，一路铺到东头的水塔/配电/
   泵房门口。给水那一排的门朝南朝着这条道，所以三口的短接管只有 1~3 m —— 干管就在家门口。
   全场埋地之后，"穿不穿过栅栏、压不压在车行道上"都不再是问题，这个模块也就不再需要读
   ROADS 和 FENCE_LINES 了 —— 它以前那 60 行剖面/收坡代码，全是为了在地面上绕开这两样。 ---- */
const LANE_Z = -27;
run('main', [[-100, LANE_Z], [112, LANE_Z]]);
for (const k of ['tower', 'power', 'pump']) run('main', [[SITES[k].x, LANE_Z], [SITES[k].x, tap(SITES[k], EAVE[k])]]);
// 农机房在大院东墙外，和牛舍、仓库排在同一条线上；它的水从干管往南接一口。
run('main', [[SITES.machine.x, LANE_Z], [SITES.machine.x, tap(SITES.machine, EAVE.machine)]]);
for (const k of ['barn', 'warehouse']) run('main', [[SITES[k].x, LANE_Z], [SITES[k].x, tap(SITES[k], EAVE[k])]]);
// 牛槽在牛栏那一片里：从干管向南穿进圈，槽子立在圈当中。
run('barn-trough', [[-95, LANE_Z], [-95, -10]]);

/* ---- 主屋门前那一口：从西干管横接过来，管口停在檐外 0.55 m。---- */
run('house', [[-verge, HOUSE_Z + HOUSE_FRONT], [-9.6, HOUSE_Z + HOUSE_FRONT]]);
