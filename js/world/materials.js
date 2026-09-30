import * as THREE from 'three';
/* =========================================================
   BUILDING MATERIALS
========================================================= */

export const wallMat =
new THREE.MeshStandardMaterial({
    color:0xc6aa82,
    roughness:.9
});

export const roofMat =
new THREE.MeshStandardMaterial({
    color:0x4b352b,
    roughness:.85
});

export const woodMat =
new THREE.MeshStandardMaterial({
    color:0x774a32,
    roughness:.9
});

export const metalMat =
new THREE.MeshStandardMaterial({
    color:0x59636a,
    metalness:.65,
    roughness:.4
});
