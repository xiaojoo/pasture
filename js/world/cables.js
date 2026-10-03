// Distribution network — entirely below grade, and now with nothing above it at all.
//
// It started as four straight tubes between four literals that named no building and hung at
// three different heights. The fix was a pole line on the same orthogonal grid the roads use,
// then (too many poles) every third position, then (still in the way) the stretches through
// something that rejected a pole went into a duct, then (bury it all) the last pole came down
// and what stayed visible was a row of pull pits -- and this round those came down too.
//
// So the conductors run at UNDER_Y the whole way and the surface shows nothing: the topology
// the lighting board and the per-building drops were written against is unchanged, but seeing
// it now needs a cutaway.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { sheathMat } from './materials.js';
import { SITES, PLOTS, POLE_X, ALLEE_REAR } from './layout.js';

const UNDER_Y = -0.55;      // 电缆埋深（管轴线）：比水干管（-0.95）浅一层，两条在路口交叉不打架
const R = 0.11;             // 三芯电缆画成一束的外径

const LINE_Z = -28.5;       // 主横道的北肩：从大院里一路铺到东头给水那一排门口
const RES_POLE_Z = -62.8;   // 住宅：横道北侧
// 原来那条 'plant'（z=26.5，从 -20 到 96）删了：它伺候的是排在厂区那条线上的水塔/配电/泵房
// 和仓库，这一轮四栋全搬走，那条走道就成了没人接的电缆。现在走道跟着 cattle 横道走，
// 西头进院（牛舍、仓库在院里），东头到泵房门口。
const LINES = [
  { id: 'lane', axis: 'x', at: LINE_Z, from: -104, to: 112 },
  { id: 'residence', axis: 'x', at: RES_POLE_Z, from: -78, to: 78 },
  { id: 'avenue-w', axis: 'z', at: -POLE_X, from: 20, to: ALLEE_REAR[0] },
  { id: 'avenue-e', axis: 'z', at: POLE_X, from: 20, to: ALLEE_REAR[0] },
];

export const powerCables = [];

/* 地上原来还剩一排接头坑（每 ~24 m 一座混凝土坑 + 一块铸铁盖，是"这条线埋在哪儿"的唯一证据）。
   这一轮他点名清掉 —— 那就是电线杆底座似的东西，全场看着像到处埋了检查井。清掉之后电气在画面里
   彻底没有可见载体：走道和支线的拓扑还在（模型里有几何、抽屉里各回路的状态照旧），但要看见它
   就得切剖面。他要是回头问"电缆的井呢"，出处就是这里。 */

function duct(pts) {
  const v = pts.map(p => new THREE.Vector3(p[0], UNDER_Y, p[1]));
  const curve = new THREE.CatmullRomCurve3(v, false, 'centripetal');
  const len = v.reduce((a, p, i) => i ? a + p.distanceTo(v[i - 1]) : 0, 0);
  const m = new THREE.Mesh(new THREE.TubeGeometry(curve, Math.max(6, Math.round(len / 2)), R, 6, false), sheathMat);
  m.userData.line = 'feeder';
  scene.add(m);
  powerCables.push(m);
  return len;
}

export const cableStats = { runs: 0, metres: 0, services: 0 };

const routes = [];
for (const L of LINES) {
  const A = L.axis === 'x' ? [L.from, L.at] : [L.at, L.from];
  const B = L.axis === 'x' ? [L.to, L.at] : [L.at, L.to];
  cableStats.metres += duct([A, B]);
  cableStats.runs++;
  routes.push({ axis: L.axis, a: A, b: B });
}

/* ---- 服务支线：每栋建筑从最近的走道接出去 ----
   端点仍然由 SITES 算，所以建筑一挪，支线跟着挪 —— 和架空版本是同一条约定。 */
const served = [...Object.entries(SITES), ...PLOTS.map(p => [`plot ${p.id}`, { x: p.x, z: p.z }])];
for (const [, s] of served) {
  let best = null;
  for (const rt of routes) {
    const horiz = rt.axis === 'x';
    const along = horiz ? s.x : s.z;
    const lo = Math.min(horiz ? rt.a[0] : rt.a[1], horiz ? rt.b[0] : rt.b[1]);
    const hi = Math.max(horiz ? rt.a[0] : rt.a[1], horiz ? rt.b[0] : rt.b[1]);
    const t = Math.min(hi, Math.max(lo, along));
    const p = horiz ? [t, rt.a[1]] : [rt.a[0], t];
    const d = Math.hypot(s.x - p[0], s.z - p[1]);
    if (!best || d < best.d) best = { p, d };
  }
  if (!best) continue;
  cableStats.metres += duct([best.p, [s.x, s.z]]);
  cableStats.services++;
}
cableStats.metres = +cableStats.metres.toFixed(1);
