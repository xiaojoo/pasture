// The stock. Cattle stand in the dusty corral to the left of the axis and graze the pasture
// beyond it, the sheep are folded into the pens on the right, and the chickens run free on the
// grass by the barns because that is how a fazenda keeps chickens.
//
// Two different build paths, and the split is by count, not by taste:
//
//   Big stock (cattle, horses) are assembled as objects and stay individually addressable,
//   because core/animation.js gives each one its own idle bob out of phase with the others.
//
//   Sheep and poultry are merged into one geometry per species and drawn as an InstancedMesh.
//   Forty of them as forty groups is eighty draw calls for animals nobody can resolve
//   individually at the overview distance; instanced they cost two.
//
// The materials are hoisted to module scope. They used to be constructed inside the builder,
// so every cow allocated four new MeshStandardMaterials -- 28 materials for 7 animals, each
// with its own shader variant, for a herd that is meant to grow.
import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { scene } from '../core/scene.js';
import { prng } from '../core/noise.js';
import { PADDOCKS } from './layout.js';

const rnd = prng(20261002);

const hide = [0xf2efe6, 0xe8e2d2, 0x2b2724, 0x6b4a30, 0xbfae94].map(
  c => new THREE.MeshStandardMaterial({ color: c, roughness: 1, flatShading: true }));
const flesh = new THREE.MeshStandardMaterial({ color: 0xd8c6b4, roughness: 1 });
const hoof = new THREE.MeshStandardMaterial({ color: 0x4a3d31, roughness: 1 });
const wool = new THREE.MeshStandardMaterial({ color: 0xece7dc, roughness: 1, flatShading: true });
const sheepFace = new THREE.MeshStandardMaterial({ color: 0x2f2b28, roughness: 1 });
const henWhite = new THREE.MeshStandardMaterial({ color: 0xf0eadb, roughness: 1 });
const henRust = new THREE.MeshStandardMaterial({ color: 0xb4653a, roughness: 1 });

export const cows = [];

function legs(g, span, drop, r = 0.1, mat = hoof) {
  for (let i = 0; i < 4; ++i) {
    const m = new THREE.Mesh(new THREE.CylinderGeometry(r, r * 1.15, drop, 6), mat);
    m.position.set(i < 2 ? span : -span, drop / 2, i % 2 === 0 ? drop * 0.36 : -drop * 0.36);
    m.castShadow = true;
    g.add(m);
  }
}

export function createCow(x, z, scale = 0.8, coat = 0) {
  const group = new THREE.Group();
  const body = new THREE.Mesh(new THREE.BoxGeometry(2.2, 1.15, 1.1), hide[coat]);
  body.position.y = 1.15;
  body.castShadow = true;
  group.add(body);

  // the shoulder hump a Nelore carries, which is what makes the silhouette Brahman-cross
  // rather than a shoebox with a head on it
  const hump = new THREE.Mesh(new THREE.SphereGeometry(0.52, 8, 6), hide[coat]);
  hump.position.set(0.62, 1.72, 0);
  hump.scale.set(1, 0.62, 1.05);
  group.add(hump);

  const head = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.85, 0.8), hide[coat === 2 ? 0 : 2]);
  head.position.set(1.35, 1.4, 0);
  head.castShadow = true;
  group.add(head);
  const muzzle = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.34, 0.56), flesh);
  muzzle.position.set(1.78, 1.24, 0);
  group.add(muzzle);
  for (const sz of [-1, 1]) {
    const ear = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.1, 0.16), flesh);
    ear.position.set(1.24, 1.72, sz * 0.44);
    ear.rotation.x = sz * 0.5;
    group.add(ear);
    const horn = new THREE.Mesh(new THREE.ConeGeometry(0.06, 0.34, 5), flesh);
    horn.position.set(1.3, 1.86, sz * 0.3);
    horn.rotation.z = -0.5;
    horn.rotation.x = sz * 0.6;
    group.add(horn);
  }
  const dewlap = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.6, 0.42), flesh);
  dewlap.position.set(1.06, 0.98, 0);
  group.add(dewlap);
  legs(group, 0.7, 0.9);
  const tail = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.03, 1.15, 5), hoof);
  tail.position.set(-1.2, 1.2, 0);
  tail.rotation.z = 0.42;
  group.add(tail);

  group.position.set(x, 0, z);
  group.rotation.y = rnd() * Math.PI * 2;
  group.scale.setScalar(scale);
  scene.add(group);
  cows.push(group);
  return group;
}

export function createHorse(x, z, scale = 1, coat = 3) {
  const group = new THREE.Group();
  const body = new THREE.Mesh(new THREE.BoxGeometry(2.5, 1.25, 0.95), hide[coat]);
  body.position.y = 1.72;
  body.castShadow = true;
  group.add(body);
  const neck = new THREE.Mesh(new THREE.BoxGeometry(0.5, 1.35, 0.5), hide[coat]);
  neck.position.set(1.3, 2.42, 0);
  neck.rotation.z = 0.52;
  neck.castShadow = true;
  group.add(neck);
  const head = new THREE.Mesh(new THREE.BoxGeometry(0.95, 0.45, 0.42), hide[coat]);
  head.position.set(1.86, 2.92, 0);
  head.rotation.z = 0.3;
  group.add(head);
  for (const sz of [-1, 1]) {
    const ear = new THREE.Mesh(new THREE.ConeGeometry(0.09, 0.26, 5), hide[coat]);
    ear.position.set(1.62, 3.2, sz * 0.16);
    group.add(ear);
  }
  const mane = new THREE.Mesh(new THREE.BoxGeometry(1.25, 0.16, 0.2), hoof);
  mane.position.set(1.16, 2.86, 0);
  mane.rotation.z = 0.52;
  group.add(mane);
  legs(group, 0.85, 1.12, 0.13);
  const tail = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.04, 1.35, 6), hoof);
  tail.position.set(-1.36, 1.9, 0);
  tail.rotation.z = 0.95;
  group.add(tail);

  group.position.set(x, 0, z);
  group.rotation.y = rnd() * Math.PI * 2;
  group.scale.setScalar(scale);
  scene.add(group);
  cows.push(group);
  return group;
}

/* =========================================================
   INSTANCED SMALL STOCK
   Build one animal as a group, fold it into a single geometry with one group per material,
   then place N copies. The folding happens on the built object so the merged silhouette is
   the animal that was designed, not a hand-written approximation of it -- and keeping the
   material boundaries means the sheep is still a white carcase with a black face rather than
   one flat colour.
========================================================= */

function instanced(build, spots) {
  const src = build();
  src.updateMatrix(true);

  // Collect (transformed geometry, material) pairs from the built animal, then merge per
  // material so each tone becomes one InstancedMesh and the two-tone animal survives.
  const byMaterial = new Map();
  for (const o of src.children) {
    if (!o.isMesh) continue;
    o.updateMatrix();
    const g = o.geometry.clone().applyMatrix4(o.matrix);
    if (!byMaterial.has(o.material)) byMaterial.set(o.material, []);
    byMaterial.get(o.material).push(g);
  }

  const d = new THREE.Object3D();
  const meshes = [];
  for (const [mat, geos] of byMaterial) {
    const merged = mergeGeometries(geos, false);
    geos.forEach(g => g.dispose());
    if (!merged) continue;
    const mesh = new THREE.InstancedMesh(merged, mat, spots.length);
    spots.forEach((s, i) => {
      d.position.set(s.x, s.y || 0, s.z);
      d.rotation.set(0, s.ry || 0, 0);
      d.scale.setScalar(s.sc || 1);
      d.updateMatrix();
      mesh.setMatrixAt(i, d.matrix);
    });
    mesh.instanceMatrix.needsUpdate = true;
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    mesh.name = 'flock';
    scene.add(mesh);
    meshes.push(mesh);
  }
  return meshes;
}

function buildSheep() {
  const g = new THREE.Group();
  const body = new THREE.Mesh(new THREE.IcosahedronGeometry(0.62, 1), wool);
  body.position.y = 0.78;
  body.scale.set(1.35, 0.95, 0.95);
  g.add(body);
  const head = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.34, 0.3), sheepFace);
  head.position.set(0.82, 0.86, 0);
  g.add(head);
  for (let i = 0; i < 4; ++i) {
    const l = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 0.44, 5), sheepFace);
    l.position.set(i < 2 ? 0.36 : -0.36, 0.22, i % 2 === 0 ? 0.22 : -0.22);
    g.add(l);
  }
  return g;
}

function buildHen() {
  const g = new THREE.Group();
  const body = new THREE.Mesh(new THREE.SphereGeometry(0.24, 8, 6), henWhite);
  body.position.y = 0.3;
  body.scale.set(1.25, 0.95, 0.8);
  g.add(body);
  const tail = new THREE.Mesh(new THREE.ConeGeometry(0.13, 0.34, 5), henRust);
  tail.position.set(-0.28, 0.44, 0);
  tail.rotation.z = 0.7;
  g.add(tail);
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.11, 7, 5), henWhite);
  head.position.set(0.24, 0.52, 0);
  g.add(head);
  const comb = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.07, 0.05), henRust);
  comb.position.set(0.24, 0.63, 0);
  g.add(comb);
  const beak = new THREE.Mesh(new THREE.ConeGeometry(0.045, 0.13, 5), henRust);
  beak.position.set(0.36, 0.5, 0);
  beak.rotation.z = -1.57;
  g.add(beak);
  for (const sz of [-1, 1]) {
    const l = new THREE.Mesh(new THREE.CylinderGeometry(0.022, 0.022, 0.16, 4), henRust);
    l.position.set(0.06, 0.08, sz * 0.07);
    g.add(l);
  }
  return g;
}

/* =========================================================
   WHERE THEY STAND
========================================================= */

const inBox = (b, n, pad) => {
  const out = [];
  for (let i = 0; i < n; ++i) {
    out.push({
      x: b.x0 + pad + rnd() * (b.x1 - b.x0 - pad * 2),
      z: b.z0 + pad + rnd() * (b.z1 - b.z0 - pad * 2),
    });
  }
  return out;
};

// 这一轮之后全场牲口都在西边那一个大围栏里，里面分成几片（layout.js 的 PADDOCKS）。
const pad = id => PADDOCKS.find(p => p.id === id);
// 牛栏：踩实的那片，挨着牛舍
inBox(pad('cattle'), 11, 3.2).forEach((p, i) => createCow(p.x, p.z, 0.74 + rnd() * 0.14, i % 5 === 4 ? 3 : (i % 3 === 0 ? 2 : 0)));
// 放牛：隔壁那片散吃草的（原来这 8 头是写死的八个字面量、散在场区各处，有 3 头跑到东边
// 没有栅栏的草地上 —— 栅栏只圈牲口，那它们就该在栅栏里面）
inBox(pad('grazing'), 8, 3.6).forEach((p, i) => createCow(p.x, p.z, 0.78 + rnd() * 0.12, i % 2 ? 0 : 2));
// 马不圈（他这一轮的规矩：全场只有羊群、牛群有栅栏），但原来那三匹是钉在 (-30,52)、(-22,58)、
// (-38,57) 三个坐标上的：仓库挪到 (-40, 47.2) 之后，第一匹整匹站在仓库的包围盒里，第三匹离
// 仓库北墙 1.8 m，第二匹正好压在 z=58 那道地界栅栏的横杆上。现在放大道另一侧的空草地上。
[[36, 14], [46, 20], [28, 8]].forEach((p, i) => createHorse(p[0], p[1], 0.95 + i * 0.03, [3, 4, 3][i]));

// 羊 + 鸡：圈在南边那片（羊圈没有车行门，没有路经过它，牲口是从隔壁赶进去的）
instanced(buildSheep, inBox(pad('sheep'), 24, 1.8).map(p => ({
  x: p.x, z: p.z, ry: rnd() * 6.28, sc: 0.9 + rnd() * 0.25,
})));

// 鸡：全部圈在栅栏里面。原来两群都写在羊圈外的空地上（一群贴着羊圈东栅栏外 2 m，一群在
// 大道西侧的空当里），转视角时就是"牲口有墙、鸡散在外面"。现在两群都并进羊圈这一片。
instanced(buildHen, inBox(pad('sheep'), 26, 1.5).map(p => ({ x: p.x, z: p.z, ry: rnd() * 6.28, sc: 0.9 + rnd() * 0.3 })));
