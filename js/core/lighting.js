import * as THREE from 'three';
import { scene } from './scene.js';
/* =========================================================
   LIGHTING
========================================================= */

export const ambient =
new THREE.HemisphereLight(
    0xb7d7c0,
    0x15251a,
    .7
);

scene.add(
    ambient
);


export const sun =
new THREE.DirectionalLight(
    0xfff1d2,
    2.5
);

sun.position.set(
    50,
    90,
    40
);

sun.castShadow =
true;

sun.shadow.mapSize.width =
2048;

sun.shadow.mapSize.height =
2048;

sun.shadow.camera.left =
-100;

sun.shadow.camera.right =
100;

sun.shadow.camera.top =
100;

sun.shadow.camera.bottom =
-100;

scene.add(
    sun
);
