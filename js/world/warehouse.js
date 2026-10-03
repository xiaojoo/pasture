import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { concreteMat, corrugatedMat, metalRoofMat, roofMat, timberMat, trimMat, frameMat, fasciaMat } from './materials.js';
import { SITES } from './layout.js';
import { box, gableRoof, plinth, cornerBoards, downpipe, mergeGroup } from './building-kit.js';
/* =========================================================
   FEED STORAGE
   A box with a lid before: no plinth for the grain to be kept off, no overhang for the
   doors to be dry under, and siding that could have been anything. Now it is what it says
   -- corrugated steel on a kerb of concrete, a shallow pitch that sheds toward the apron,
   two roll-up doors with the track showing, and louvers because feed sweats.
========================================================= */

export function rollUpDoor(g, { x, y, z, w, h, dir = 1, face = 'z' }) {
  const put = (ox, oy, bw, bh, bd, m, dz = 0) => {
    if (face === 'z') return box(g, bw, bh, bd, m, x + ox, y + oy, z + dz * dir);
    return box(g, bd, bh, bw, m, x + dz * dir, y + oy, z + ox);
  };
  put(0, 0, w + 0.6, h + 0.5, 0.24, frameMat, 0.06);
  const panel = put(0, 0, w, h, 0.18, corrugatedMat, 0.22);
  for (let k = 1; k < 6; ++k) {
    put(0, -h / 2 + (h * k) / 6, w - 0.1, 0.07, 0.06, frameMat, 0.32);
  }
  put(0, h / 2 + 0.35, w + 0.9, 0.3, 0.5, trimMat, 0.2);
  for (const sx of [-1, 1]) {
    put(sx * (w / 2 + 0.25), 0, 0.22, h + 0.6, 0.42, frameMat, 0.2);
  }
  return panel;
}

export function createWarehouse() {
  const group = new THREE.Group();
  group.position.set(SITES.warehouse.x, 0, SITES.warehouse.z);
  group.rotation.y = SITES.warehouse.ry;
  group.userData.type = 'warehouse';
  group.userData.building = 'warehouse';

  const W = 18;
  const D = 13;
  const H = 7;

  plinth(group, W, D, 0.55, concreteMat);
  const body = box(group, W, H, D, timberMat, 0, H / 2 + 0.2, 0);
  body.userData.building = 'warehouse';
  cornerBoards(group, W, D, H, 0.2);

  gableRoof(group, { w: W, d: D, wallTop: H + 0.2, rise: 2.6, ov: 1.15, thick: 0.32, mat: roofMat });

  const dz = D / 2;
  rollUpDoor(group, { x: -4.4, y: 2.5, z: dz, w: 4.4, h: 4.6 });
  rollUpDoor(group, { x: 4.4, y: 2.5, z: dz, w: 4.4, h: 4.6 });
  box(group, W + 3, 0.12, 3.4, concreteMat, 0, 0.08, dz + 1.6);   // truck apron

  // canopy over the doors, so the loading bay works in the rain
  for (const sx of [-1, 1]) {
    box(group, 0.2, 0.2, 2.4, frameMat, sx * 7.4, 5.6, dz + 1.1, 0);
    box(group, 0.24, 5.6, 0.24, frameMat, sx * 7.4, 2.8, dz + 2.2);
  }
  const canopy = box(group, W + 1.2, 0.18, 2.9, roofMat, 0, 5.9, dz + 1.2);
  canopy.rotation.x = -0.12;
  box(group, W + 1.2, 0.3, 0.14, fasciaMat, 0, 5.78, dz + 2.6);

  for (const sx of [-1, 1]) {
    for (const z of [-3.4, 0, 3.4]) {
      box(group, 0.14, 1.5, 2.2, frameMat, sx * (W / 2 + 0.1), 5.4, z);
      for (let k = 0; k < 5; ++k) {
        box(group, 0.1, 0.1, 2.0, fasciaMat, sx * (W / 2 + 0.18), 4.85 + k * 0.28, z);
      }
    }
  }
  downpipe(group, W, D, H + 0.2, 1, -1);
  downpipe(group, W, D, H + 0.2, -1, -1);

  mergeGroup(group);

  scene.add(group);
  buildings.warehouse = { group };
}

createWarehouse();
