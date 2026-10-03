// The wild land's covering: a forest on the foothills, boulders on the steep ground, and
// reeds along the river bank. All of it is instanced -- a thousand trees has to cost one
// draw call each, not a thousand -- and all of it is placed by the same seeded generator, so
// the ridge line is the same ridge line in every screenshot.
//
// Nothing here grows on the ranch (r < PLATEAU_R): the pasture the twin models is kept
// exactly as cleared as it was.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { terrainHeight, riverEdge, riverSamples, waterY, riverHalfWidth, PLATEAU_R } from '../core/ground.js';
import { prng, fbm } from '../core/noise.js';

const rnd = prng(20261002);
const dummy = new THREE.Object3D();

function slopeAt(x, z) {
  const e = 3.5;
  const dx = terrainHeight(x + e, z) - terrainHeight(x - e, z);
  const dz = terrainHeight(x, z + e) - terrainHeight(x, z - e);
  return Math.hypot(dx, dz) / (2 * e);
}

function gather(count, lo, hi, want, out) {
  let made = 0;
  let tries = 0;
  while (made < count && tries < count * 22) {
    ++tries;
    const th = rnd() * Math.PI * 2;
    const r = lo + rnd() * (hi - lo);
    const x = Math.cos(th) * r;
    const z = Math.sin(th) * r;
    const y = terrainHeight(x, z);
    const v = want(x, y, z, r);
    if (!v) continue;
    out.push({ x, y, z, ...v });
    ++made;
  }
  return made;
}

function dryOut(x, z) {
  return fbm(x * 0.011, z * 0.011, 3, 5);
}

function farFromRiver(x, z, margin) {
  const e = riverEdge(x, z);
  return !e || e.d > e.hw + margin;
}

/* =========================================================
   CONIFERS -- the dark belt up the foothills
========================================================= */

const firs = [];
gather(820, 275, 900, (x, y, z) => {
  if (y > 120 || slopeAt(x, z) > 0.85) return null;
  if (!farFromRiver(x, z, 6)) return null;
  // trees thicken in the wet hollows and thin out on the dry shoulders
  if (rnd() > 0.35 + dryOut(x, z) * 0.9) return null;
  const h = 8 + rnd() * 9;
  return { h, w: h * (0.19 + rnd() * 0.06), tone: 0.72 + rnd() * 0.5 };
}, firs);

const firGeo = new THREE.ConeGeometry(1, 1, 7);
firGeo.translate(0, 0.5, 0);
const firMesh = new THREE.InstancedMesh(
  firGeo,
  new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 1 }),
  firs.length
);
firMesh.castShadow = false;
firMesh.receiveShadow = false;
{
  const c = new THREE.Color();
  firs.forEach((t, i) => {
    dummy.position.set(t.x, t.y, t.z);
    dummy.rotation.set(0, rnd() * 6.28, 0);
    dummy.scale.set(t.w, t.h, t.w);
    dummy.updateMatrix();
    firMesh.setMatrixAt(i, dummy.matrix);
    c.setHex(0x2f4a30).multiplyScalar(t.tone);
    firMesh.setColorAt(i, c);
  });
}
scene.add(firMesh);

/* =========================================================
   BROADLEAF -- clumps on the valley floor and lower slopes
========================================================= */

const copses = [];
gather(300, 250, 430, (x, y, z) => {
  if (y > 80 || slopeAt(x, z) > 0.5) return null;
  if (!farFromRiver(x, z, 8)) return null;
  if (dryOut(x, z) > 0.62) return null;
  const h = 5 + rnd() * 5;
  return { h, w: h * (0.34 + rnd() * 0.18), tone: 0.78 + rnd() * 0.55 };
}, copses);

const trunkGeo = new THREE.CylinderGeometry(0.22, 0.36, 1, 5);
trunkGeo.translate(0, 0.5, 0);
const crownGeo = new THREE.IcosahedronGeometry(1, 0);
const barkMat = new THREE.MeshStandardMaterial({ color: 0x4a3626, roughness: 1 });
const crownMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 1, flatShading: true });

const trunks = new THREE.InstancedMesh(trunkGeo, barkMat, copses.length);
const crowns = new THREE.InstancedMesh(crownGeo, crownMat, copses.length);
{
  const c = new THREE.Color();
  copses.forEach((t, i) => {
    dummy.position.set(t.x, t.y, t.z);
    dummy.rotation.set(0, rnd() * 6.28, 0);
    dummy.scale.set(t.w * 0.16, t.h * 0.55, t.w * 0.16);
    dummy.updateMatrix();
    trunks.setMatrixAt(i, dummy.matrix);

    dummy.position.set(t.x, t.y + t.h * 0.62, t.z);
    dummy.scale.set(t.w, t.w * 0.82, t.w);
    dummy.updateMatrix();
    crowns.setMatrixAt(i, dummy.matrix);
    c.setHex(0x4d7238).multiplyScalar(t.tone);
    crowns.setColorAt(i, c);
  });
}
scene.add(trunks, crowns);

/* =========================================================
   BOULDERS
========================================================= */

const stones = [];
gather(320, PLATEAU_R + 2, 900, (x, y, z) => {
  const steep = slopeAt(x, z);
  if (steep < 0.2 && y < 30) return null;
  if (!farFromRiver(x, z, 3)) return null;
  const s = 1.1 + rnd() * 3.4;
  return { s, tone: 0.78 + rnd() * 0.5 };
}, stones);

const rockGeo = new THREE.IcosahedronGeometry(1, 0);
const rockMesh = new THREE.InstancedMesh(
  rockGeo,
  new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.95, flatShading: true }),
  stones.length
);
{
  const c = new THREE.Color();
  stones.forEach((t, i) => {
    dummy.position.set(t.x, t.y + t.s * 0.35, t.z);
    dummy.rotation.set(rnd() * 3, rnd() * 6.28, rnd() * 3);
    dummy.scale.set(t.s, t.s * (0.6 + rnd() * 0.5), t.s * (0.75 + rnd() * 0.5));
    dummy.updateMatrix();
    rockMesh.setMatrixAt(i, dummy.matrix);
    c.setHex(0x6e6a63).multiplyScalar(t.tone);
    rockMesh.setColorAt(i, c);
  });
}
scene.add(rockMesh);

/* =========================================================
   REEDS
   A short flared blade, scattered along the bank wherever the ground sits between the water
   line and a couple of metres above it -- the only place reeds actually grow.
========================================================= */

const reedPlace = [];
for (let i = 2; i < riverSamples.length - 2; ++i) {
  const p = riverSamples[i];
  const a = riverSamples[i - 2];
  const b = riverSamples[i + 2];
  let tx = b.x - a.x;
  let tz = b.z - a.z;
  const tl = Math.hypot(tx, tz) || 1;
  tx /= tl;
  tz /= tl;
  const hw = riverHalfWidth(p.s);
  const wy = waterY(p.s);
  for (let k = 0; k < 6; ++k) {
    const side = k % 2 ? 1 : -1;
    // The bank rises several metres above the water line, so the reed belt is placed by
    // height rather than by distance: anywhere the ground is between the water and half a
    // metre under the top of the bank.
    const off = hw * (1.05 + rnd() * 0.9);
    const along = (rnd() - 0.5) * 9;
    const x = p.x + tz * side * off + tx * along;
    const z = p.z - tx * side * off + tz * along;
    const y = terrainHeight(x, z);
    if (y < wy + 0.1 || y > wy + 4.2) continue;
    reedPlace.push({ x, y, z, h: 1.3 + rnd() * 1.9, tone: 0.6 + rnd() * 0.7 });
  }
}

const reedGeo = new THREE.CylinderGeometry(0.03, 0.16, 1, 3);
reedGeo.translate(0, 0.5, 0);
const reedMesh = new THREE.InstancedMesh(
  reedGeo,
  new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 1 }),
  Math.max(1, reedPlace.length)
);
{
  const c = new THREE.Color();
  reedPlace.forEach((t, i) => {
    dummy.position.set(t.x, t.y, t.z);
    dummy.rotation.set((rnd() - 0.5) * 0.3, rnd() * 6.28, (rnd() - 0.5) * 0.3);
    dummy.scale.set(t.h * 0.22, t.h, t.h * 0.22);
    dummy.updateMatrix();
    reedMesh.setMatrixAt(i, dummy.matrix);
    c.setHex(0x6f7a3a).multiplyScalar(t.tone);
    reedMesh.setColorAt(i, c);
  });
  if (!reedPlace.length) reedMesh.visible = false;
}
scene.add(reedMesh);

export const natureCounts = {
  firs: firs.length,
  copses: copses.length,
  stones: stones.length,
  reeds: reedPlace.length,
};
