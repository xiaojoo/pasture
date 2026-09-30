import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { camera, renderer } from './scene.js';
/* =========================================================
   CAMERA
========================================================= */

export const controls =
new OrbitControls(
    camera,
    renderer.domElement
);

controls.enableDamping =
true;

controls.dampingFactor =
.06;

controls.minDistance =
18;

controls.maxDistance =
150;

controls.maxPolarAngle =
Math.PI * .48;

controls.target.set(
    0,
    0,
    0
);
