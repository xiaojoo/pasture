import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { prng } from '../core/noise.js';
import { mergeMeshes } from './building-kit.js';
import { ALLEE_FRONT, ALLEE_REAR, ALLEE_X, LANES, ROADS, halfTrack } from './layout.js';
/* =========================================================
   TREES
   One cylinder and one sphere is a lollipop, not a tree. The fifteen spots the placeholder
   used are kept -- they were chosen so the yard reads as fenced-in -- but each one is now
   built up: a tapered butt, branches the crown actually sits on, and a crown made of a few
   lobes at different heights so the silhouette is irregular the way a real crown is.
========================================================= */

const rnd = prng(4711);

const barkMat = new THREE.MeshStandardMaterial({ color: 0x5d4330, roughness: 1 });

// A handful of shared foliage materials instead of one per tree: the crowns need to differ
// in colour, and a hundred cloned materials would be a hundred shader programs.
const leafMats = [0x2e653b, 0x3d6b32, 0x4a7340, 0x356041, 0x59743c, 0x2b5236]
  .map(c => new THREE.MeshStandardMaterial({ color: c, roughness: 1, flatShading: true }));
const firMats = [0x2f5233, 0x27472c, 0x395c36, 0x214229]
  .map(c => new THREE.MeshStandardMaterial({ color: c, roughness: 1, flatShading: true }));

// Blue gum bark sheds in strips, so it is a mottled grey-cream over a dark inner layer -- not
// chalk. Chalk at 24 m reads as a telegraph pole.
const paleBarkMat = new THREE.MeshStandardMaterial({ color: 0xa9998a, roughness: 1, flatShading: true });
const eucMats = [0x6d8f6b, 0x5b7f5a, 0x7b9a74, 0x506f51, 0x86a479]
  .map(c => new THREE.MeshStandardMaterial({ color: c, roughness: 1, flatShading: true }));

const pick = a => a[Math.floor(rnd() * a.length)];

function lobedCrown(g, r, y, lobes = 4) {
  const m = pick(leafMats);
  for (let k = 0; k < lobes; ++k) {
    const a = (k / lobes) * Math.PI * 2 + rnd() * 1.4;
    const s = r * (0.6 + rnd() * 0.45);
    const lobe = new THREE.Mesh(new THREE.IcosahedronGeometry(s, 1), m);
    lobe.position.set(Math.cos(a) * r * 0.42, y + (rnd() - 0.3) * r * 0.7, Math.sin(a) * r * 0.42);
    lobe.castShadow = true;
    g.add(lobe);
  }
}

export function createTree(x, z, scale = 1, kind) {
  const group = new THREE.Group();
  const h = 4.6 + rnd() * 1.6;

  const trunk = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.62, h, 7), barkMat);
  trunk.position.y = h / 2;
  trunk.castShadow = true;
  group.add(trunk);

  // the flare at the butt, which is what makes a tree look rooted rather than stuck in
  const flare = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.95, 0.55, 7), barkMat);
  flare.position.y = 0.24;
  group.add(flare);

  const species = kind || (rnd() > 0.42 ? 'broad' : 'fir');

  if (species === 'euc') {
    // A blue gum: dead straight, self-pruned to the top third, and the crown is a narrow
    // drooping column rather than a ball.
    //
    // The first pass at this read as white coat stands because the bark was chalked at
    // 0xcfc6b6 and the crown was seven small lobes on a 24 m stem. Real gum bark is a mottle
    // of grey, cream and shed brown, and what carries the tree's identity at a distance is
    // the *silhouette of the crown*, which has to occupy the top third of the stem and be
    // dense enough to read as one mass.
    const H = 24 * scale;
    const u = H / 24;
    group.remove(trunk, flare);

    const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.26 * u, 0.52 * u, H, 9), paleBarkMat);
    stem.position.y = H / 2;
    stem.castShadow = true;
    group.add(stem);
    const butt = new THREE.Mesh(new THREE.CylinderGeometry(0.5 * u, 0.95 * u, 2.2 * u, 9), barkMat);
    butt.position.y = 1.1 * u;
    group.add(butt);

    const crownBase = H * 0.68;
    const crownH = H - crownBase;
    for (let k = 0; k < 13; ++k) {
      const t = k / 12;
      // widest just under the middle of the crown, tapering to the tip: a column, not a ball
      const r = (1.62 * u) * (0.62 + 0.62 * Math.sin(Math.PI * (0.18 + 0.76 * t))) * (0.8 + rnd() * 0.36);
      const a = k * 2.399 + rnd() * 0.7;              // golden-angle spiral, no two in line
      const lobe = new THREE.Mesh(new THREE.IcosahedronGeometry(r, 1), pick(eucMats));
      lobe.position.set(Math.cos(a) * r * 0.34, crownBase + t * crownH, Math.sin(a) * r * 0.34);
      lobe.scale.set(1, 0.86, 1);
      lobe.castShadow = true;
      group.add(lobe);
    }
    const tip = new THREE.Mesh(new THREE.ConeGeometry(0.8 * u, 3.4 * u, 6), pick(eucMats));
    tip.position.y = H + 1.1 * u;
    group.add(tip);
    group.position.set(x, 0, z);
    group.rotation.y = rnd() * Math.PI * 2;
    scene.add(group);
    return group;
  }

  if (species === 'fir') {
    // conifer: four stacked cones, each narrower, with a bare spire at the top
    const m = pick(firMats);
    let y = h * 0.42;
    for (let k = 0; k < 4; ++k) {
      const r = 2.5 - k * 0.52;
      const ch = 2.5 - k * 0.25;
      const cone = new THREE.Mesh(new THREE.ConeGeometry(r, ch, 8), m);
      cone.position.y = y + ch / 2;
      cone.castShadow = true;
      group.add(cone);
      y += ch * 0.58;
    }
    const spire = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.14, 1.3, 5), barkMat);
    spire.position.y = y + 0.5;
    group.add(spire);
  } else {
    // two or three limbs so the crown is held up rather than balanced on the stem
    for (let k = 0; k < 3; ++k) {
      const a = rnd() * Math.PI * 2;
      const br = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.22, 2.1, 5), barkMat);
      br.position.set(Math.cos(a) * 0.55, h * 0.82, Math.sin(a) * 0.55);
      br.rotation.set(Math.sin(a) * 0.62, 0, -Math.cos(a) * 0.62);
      group.add(br);
    }
    lobedCrown(group, 2.5, h * 0.98, 4);
  }

  group.position.set(x, 0, z);
  group.rotation.y = rnd() * Math.PI * 2;
  group.scale.setScalar(scale);
  scene.add(group);
  return group;
}

export function createBush(x, z, scale = 1) {
  const g = new THREE.Group();
  const m = pick(leafMats);
  const n = 2 + Math.floor(rnd() * 3);
  for (let k = 0; k < n; ++k) {
    const r = 0.55 + rnd() * 0.6;
    const b = new THREE.Mesh(new THREE.IcosahedronGeometry(r, 1), m);
    b.position.set((rnd() - 0.5) * 1.7, r * 0.75, (rnd() - 0.5) * 1.7);
    b.castShadow = true;
    g.add(b);
  }
  g.position.set(x, 0, z);
  g.scale.setScalar(scale);
  scene.add(g);
  return g;
}

/* =========================================================
   THE AVENUES
   The single strongest thing in the reference is that the drive is planted, not merely
   bordered: two rows of blue gum going straight up the axis behind the house, and a shorter
   pair flanking the front drive. Spacing is even because it is planted, and the rows are
   mirror images because the whole composition is one axis.
========================================================= */

const planted = [];
export { planted };

function alley(z0, z1, xOff, scale, step, kind) {
  const n = Math.max(1, Math.round(Math.abs(z1 - z0) / step));
  for (let i = 0; i <= n; ++i) {
    const z = z0 + (z1 - z0) * (i / n);
    for (const sx of [-1, 1]) {
      // 支路横穿树行的那一格让位：一棵按树站在车道口上，长出来就是挡住路的绿墙
      const crossed = LANES.some(L => Math.abs(z - L.z) < 4.2
        && sx * xOff >= L.xFrom && sx * xOff <= L.xTo);
      if (crossed) continue;
      planted.push(createTree(sx * xOff, z, scale * (0.88 + rnd() * 0.24), kind));
    }
  }
}

// 屋后:高按树,贴着车辙外沿,一路往上坡
alley(ALLEE_REAR[0], ALLEE_REAR[1], halfTrack() + 2.9, 1.0, 5.6, 'euc');
// 门前:矮一档,把牌坊到主屋这段框成一条走廊
alley(ALLEE_FRONT[0], ALLEE_FRONT[1], halfTrack() + 9.8, 0.62, 6.0, 'euc');

// two ornaments square on the axis at the head of the forecourt, which is where the
// reference puts the only trees that are allowed to be symmetric about the door
planted.push(createTree(-6.4, 21.5, 0.42, 'euc'), createTree(6.4, 21.5, 0.42, 'euc'));

// the pasture's own scatter: big isolated crowns out in the fields, clear of both yards
// （70,34 那一棵原本站在厂区横道的车行道上 —— 路带宽 28.13..35.87、这条支路从 -60 铺到 90，
// 它的树心正好落在里面；挪到 76,44）
[
  [-68, 8, 1.25], [-72, 30, 1.1], [-62, 56, 1.3], [-30, 62, 1.15],
  [66, 12, 1.2], [76, 44, 1.05], [58, 58, 1.3], [26, 64, 1.1],
  [-42, -14, 1.2], [-8, -46, 1.05], [22, -40, 1.3], [48, -30, 1.15],
  [-70, -12, 1.25], [74, -6, 1.1],
].forEach(p => planted.push(createTree(p[0], p[1], p[2])));

/* 防风林。原来这一堆是"服务院后面那道屏风"，钉在 (-62,-20)、(60,-22)、(66,-28) 这些点上 ——
   牲口大院和给水那一排一搬，这几棵就分别站进了 cattle 横道的车行带里、干管线上（z=-27）、
   和仓库山墙底下。现在按新位置重排：大院西侧墙外一道（挡风、也是院子的背景），东边给水那一排
   后面一道。西缘不能过 -118：plateau 的圆缘在 (-118,-40) 已经是 r=124.5。 */
[
  [-116, -40, 1.0], [-118, -20, 1.1], [-115, 0, .95], [-117, 20, 1.05],
  [70, -48, 1.0, 'fir'], [86, -50, 1.1], [102, -46, .95, 'fir'],
].forEach(p => planted.push(createTree(p[0], p[1], p[2], p[3])));

// shrubs along the yard fence and by the house porch, where a ranch would plant them
[
  [-16, 18.5, 1.0], [-24, 18.5, .8], [-32, 18.5, 1.1], [-40, 18.5, .9], [-48, 18.5, 1.0],
  [16, 18.5, 1.1], [24, 18.5, .85], [32, 18.5, 1.0], [40, 18.5, .9], [48, 18.5, 1.05],
  [-11, 12, 1.1], [11, 12, .85], [-13, -12, .95], [13, -12, 1.1],
].forEach(p => planted.push(createBush(p[0], p[1], p[2])));

/* =========================================================
   树篱与果园行
   参考图里那条路不是"路边有树"，是"被植物夹住"：车辙两边各一道齐腰绿篱，过路口断一口，
   支路两侧再各排一行果树。绿篱画成"方身 + 窄帽"两段长方体（修剪过的篱笆本来就是个
   长方体），一条篱两个网格、合并后一次画完 —— 不用几百颗灌木球去堆同样的效果。
========================================================= */
const hedgeMat = new THREE.MeshStandardMaterial({ color: 0x2b5236, roughness: 1, flatShading: true });
const hedgeCapMat = new THREE.MeshStandardMaterial({ color: 0x4a7340, roughness: 1, flatShading: true });
const hedges = [];
const placedHedges = [];
let hedgeMetres = 0;

function hedge(axis, perp, from, to) {
  // 只有真的延伸到这条走廊的横道才断口：cattle 那条只到东岸 4.5，所以东边这道篱在 z=-22 不断
  const gaps = ROADS.filter(r => r.axis !== axis).map(r => {
    const lo = Math.min(r.from, r.to), hi = Math.max(r.from, r.to);
    return perp < lo || perp > hi ? null : { c: r.at, half: r.w / 2 + 1.3 };
  }).filter(Boolean);
  let segs = [[Math.min(from, to), Math.max(from, to)]];
  for (const g of gaps) segs = segs.flatMap(([a, b]) => {
    const out = [];
    if (g.c - g.half > a) out.push([a, Math.min(b, g.c - g.half)]);
    if (g.c + g.half < b) out.push([Math.max(a, g.c + g.half), b]);
    return out;
  });
  for (const [a, b] of segs) {
    const L = b - a;
    if (L < 2.5) continue;
    const g = new THREE.Group();
    const body = new THREE.Mesh(new THREE.BoxGeometry(0.95, 0.78, L), hedgeMat);
    body.position.y = 0.39;
    const cap = new THREE.Mesh(new THREE.BoxGeometry(0.6, 0.36, L), hedgeCapMat);
    cap.position.y = 0.96;
    for (const m of [body, cap]) { m.castShadow = true; m.receiveShadow = true; g.add(m); }
    if (axis === 'z') g.position.set(perp, 0, (a + b) / 2);
    else { g.position.set((a + b) / 2, 0, perp); g.rotation.y = Math.PI / 2; }
    scene.add(g);
    hedges.push(g);
    placedHedges.push({ axis, perp, a, b });
    hedgeMetres += L;
  }
}

// 大道两侧：从门前一直夹到屋后树行的起点
for (const sx of [-1, 1]) hedge('z', sx * (ALLEE_X + 1.6), ALLEE_REAR[1], ALLEE_FRONT[1]);
// 厂区前沿那道没做：横道北缘 35.87 到仓库屋檐南缘 36.8 之间只剩 0.93 m，
// 而那条带子上已经有电缆走道（z=26.5）和牧场北栅栏（z=27.5）在抢，硬塞一道篱就是四样东西叠着。

// 果园行：原来这两行是夹着 mid 那条牛舍门口便道排的（z=0 两侧 ±5.6）。mid 这一轮删了，
// 果园跟着搬到大院东墙和大道之间那条空当里，改一行，走 cattle 横道的南侧。
// 为什么只留一行：北侧 ±5.6 那条线正好是水干管（z=-27）和路灯（z=-27.8）在的地方。
for (let x = -46; x <= -16; x += 4.4) planted.push(createTree(x, -13, 0.4 + rnd() * 0.12));
// 主屋前院：西侧一道侧篱夹出进门的那块地，篱外再一行小树。东侧这一轮让给了农机房 ——
// 它竖过来之后包围盒从 x=18.9 起，而篱在 17.5..18.5、树心在 22，三样东西叠在一条
// 2.7 m 宽的夹缝里（上一版量出来房子和农机房重叠 7.7 m²，就是这么来的）。
hedge('z', -18, -108, -92);
for (let z = -104; z <= -94; z += 4) planted.push(createTree(-22, z, 0.44 + rnd() * 0.1));

// Measured off the built objects, before the merge folds every trunk and crown into shared
// meshes and there is nothing left to measure. A tree's height is the topmost point of its
// highest part, not 24 x scale written out again somewhere else.
export const treeSpots = planted.map(g => {
  let top = 0;
  for (const o of g.children) {
    if (!o.isMesh) continue;
    if (!o.geometry.boundingBox) o.geometry.computeBoundingBox();
    top = Math.max(top, o.position.y + o.geometry.boundingBox.max.y * o.scale.y);
  }
  return { x: +g.position.x.toFixed(2), z: +g.position.z.toFixed(2), h: +(top * g.scale.y).toFixed(2) };
});

// Two hundred and fifty separate trunks and crowns is two hundred and fifty draw calls for
// scenery that never moves, so they are folded into one mesh per shared material.
mergeMeshes(scene, planted.flatMap(g => g.children.filter(o => o.isMesh)), true);
mergeMeshes(scene, hedges.flatMap(g => g.children.filter(o => o.isMesh)), true);
export const plantedStats = { trees: planted.length, hedgeMetres: +hedgeMetres.toFixed(1), hedgeRuns: hedges.length, segments: placedHedges.map(h => ({ ...h, perp: +h.perp.toFixed(2), a: +h.a.toFixed(1), b: +h.b.toFixed(1) })) };
