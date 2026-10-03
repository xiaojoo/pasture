import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { concreteMat, sidingMat, roofMat, frameMat, fasciaMat, trimMat, metalMat } from './materials.js';
import { SITES } from './layout.js';
import { box, gableRoof, plinth, doorUnit, windowUnit, mergeGroup } from './building-kit.js';
/* =========================================================
   PUMP HOUSE
   Small, but it is the building with the most switchgear on it: the control box by the door
   with its conduit running down to the motor, and the louvered gable vent that keeps the
   motor from cooking. The water itself is already plumbed -- js/world/pipes.js makes this
   the hub of all six runs -- so nothing is added here that would double up a pipe.
========================================================= */

export function createPumpHouse() {
  const group = new THREE.Group();
  group.position.set(SITES.pump.x, 0, SITES.pump.z);
  group.rotation.y = SITES.pump.ry;
  group.userData.type = 'pump';
  group.userData.building = 'pump';

  const W = 10;
  const D = 8;
  const H = 5;

  plinth(group, W, D, 0.5, concreteMat);
  const body = box(group, W, H, D, sidingMat, 0, H / 2 + 0.15, 0);
  body.userData.building = 'pump';

  gableRoof(group, { w: W, d: D, wallTop: H + 0.15, rise: 2.1, ov: 1.0, thick: 0.3, mat: roofMat });

  const dz = D / 2;
  doorUnit(group, { x: -2.2, y: 1.6, z: dz, w: 2.0, h: 3.0, mat: frameMat, panels: false });
  windowUnit(group, { x: 2.6, y: 3.2, z: dz, w: 1.6, h: 1.2 });

  // louvered gable vent over the door
  box(group, 2.2, 1.1, 0.14, frameMat, 0, H + 0.75, dz + 0.1);
  for (let k = 0; k < 4; ++k) {
    box(group, 2.0, 0.12, 0.22, fasciaMat, 0, H + 0.37 + k * 0.25, dz + 0.2);
  }

  // control box, conduit down to the motor, and the disconnect above it
  box(group, 1.1, 1.4, 0.32, metalMat, -4.1, 2.7, dz + 0.22);
  box(group, 0.16, 1.5, 0.16, metalMat, -4.1, 1.2, dz + 0.22);
  box(group, 0.5, 0.6, 0.24, trimMat, -4.1, 3.75, dz + 0.2);
  box(group, 0.14, 0.14, 1.0, metalMat, -2.6, 4.1, dz + 0.3);

  // the pad the pump sits on, and the guard rail at the far end
  box(group, W + 1.8, 0.12, 2.4, concreteMat, 0, 0.07, dz + 1.1);
  for (const sx of [-1, 1]) {
    box(group, 0.14, 1.0, 0.14, trimMat, sx * 4.6, 0.5, -dz - 0.3);
  }
  box(group, 9.4, 0.12, 0.12, trimMat, 0, 1.0, -dz - 0.3);

  mergeGroup(group);

  scene.add(group);
  buildings.pump = { group };
}

createPumpHouse();
