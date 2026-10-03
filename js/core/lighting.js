import * as THREE from 'three';
import { scene } from './scene.js';
/* =========================================================
   LIGHTING
========================================================= */

export const ambient =
new THREE.HemisphereLight(
    0xcfe6d6,
    0x46523f,
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

// The ranch is now 130 units of plateau with a 6-unit fence and a 24-unit tree on it, and the
// sun is low enough that a shadow reaches well past the object casting it. At the old +/-100
// the east row of the avenue and the whole cattle yard fell outside the map and stopped
// shadowing at all.
sun.shadow.camera.left =
-150;

sun.shadow.camera.right =
150;

sun.shadow.camera.top =
150;

sun.shadow.camera.bottom =
-150;

// A 27-degree sun across a 2048 map over 300 units is 14.6 texels per metre of shadow, and
// the depth step per texel along a shallow ray is coarse enough for the ground to eat its own
// shadow. Normal bias fixes that without the peter-panning a plain depth bias would give the
// thin fence rails.
sun.shadow.normalBias =
0.06;

scene.add(
    sun
);
