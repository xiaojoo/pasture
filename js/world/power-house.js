import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { concreteMat, metalMat, frameMat, fasciaMat, trimMat, gravelMat, metalRoofMat } from './materials.js';
import { SITES } from './layout.js';
import { box, plinth, mergeGroup } from './building-kit.js';
/* =========================================================
   POWER ROOM
   A switchroom is a windowless box with a flat roof, a parapet so nobody can walk off it,
   louvers for the transformers, and a bolt-on conduit that goes somewhere. The yellow plate
   stays where it was -- it is the one thing on this building the drawer already points at.
========================================================= */

export function createPowerHouse() {
  const group = new THREE.Group();
  group.position.set(SITES.power.x, 0, SITES.power.z);
  group.rotation.y = SITES.power.ry;
  group.userData.type = 'power';
  group.userData.building = 'power';

  const W = 9;
  const D = 8;
  const H = 5;

  plinth(group, W, D, 0.6, concreteMat);
  const body = box(group, W, H, D, concreteMat, 0, H / 2 + 0.2, 0);
  body.userData.building = 'power';

  // flat deck with a parapet, the only roof an electrical room gets
  box(group, W + 0.7, 0.34, D + 0.7, metalRoofMat, 0, H + 0.35, 0);
  for (const sz of [-1, 1]) box(group, W + 0.9, 0.62, 0.22, fasciaMat, 0, H + 0.8, sz * (D / 2 + 0.32));
  for (const sx of [-1, 1]) box(group, 0.22, 0.62, D + 0.9, fasciaMat, sx * (W / 2 + 0.32), H + 0.8, 0);

  // louvered intake and extract on the long faces
  for (const sz of [-1, 1]) {
    box(group, 3.0, 1.6, 0.14, frameMat, -1.6, 3.3, sz * (D / 2 + 0.08));
    for (let k = 0; k < 5; ++k) {
      box(group, 2.8, 0.14, 0.24, fasciaMat, -1.6, 2.65 + k * 0.33, sz * (D / 2 + 0.18));
    }
  }
  box(group, 0.14, 1.6, 3.0, frameMat, W / 2 + 0.08, 3.3, 0);
  for (let k = 0; k < 5; ++k) {
    box(group, 0.24, 0.14, 2.8, fasciaMat, W / 2 + 0.18, 2.65 + k * 0.33, 0);
  }

  // service door with a frame and a kick plate
  box(group, 2.0, 3.4, 0.2, frameMat, 2.6, 1.7, D / 2 + 0.1);
  box(group, 1.6, 3.1, 0.14, metalMat, 2.6, 1.7, D / 2 + 0.24);
  box(group, 1.6, 0.5, 0.06, trimMat, 2.6, 0.45, D / 2 + 0.33);
  box(group, 0.14, 0.14, 0.14, trimMat, 1.95, 1.7, D / 2 + 0.33);

  // the cable mast the four runs leave from, and the meter can under it
  box(group, 0.22, 1.9, 0.22, metalMat, 0, H + 1.0, 0);
  box(group, 1.3, 0.9, 0.3, metalMat, -3.4, 2.4, D / 2 + 0.2);
  for (const sx of [-1, 1]) box(group, 0.12, 1.1, 0.12, frameMat, -3.4 + sx * 0.5, 1.2, D / 2 + 0.2);

  // hazard stripes at the kerb, where a vehicle would back in
  for (let k = 0; k < 8; ++k) {
    box(group, 0.62, 0.16, 0.62, k % 2 ? trimMat : frameMat, -W / 2 + 0.6 + k * 1.05, 0.09, D / 2 + 0.9);
  }

  const warning = new THREE.Mesh(
    new THREE.BoxGeometry(1.6, 1.6, .12),
    new THREE.MeshStandardMaterial({ color: 0xffc928, emissive: 0x6d4c00, emissiveIntensity: .5 })
  );
  warning.position.set(0, 3, D / 2 + 0.1);
  group.add(warning);

  // the switchyard apron: gravel, in front of the door and clear of the road
  box(group, 12, 0.1, 5.5, gravelMat, 0, 0.05, D / 2 + 3.0);

  mergeGroup(group, [warning]);

  scene.add(group);
  buildings.power = { group };
}

createPowerHouse();
