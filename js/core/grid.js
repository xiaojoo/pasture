import * as THREE from 'three';
import { scene } from './scene.js';
/* =========================================================
   GRID
========================================================= */

export const grid =
new THREE.GridHelper(
    170,
    34,
    0x61805e,
    0x41623f
);

grid.position.y =
.025;

grid.material.transparent =
true;

grid.material.opacity =
.13;

scene.add(
    grid
);
