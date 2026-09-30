import * as THREE from 'three';
import { scene } from './scene.js';
/* =========================================================
   GROUND
========================================================= */

export const ground =
new THREE.Mesh(

    new THREE.PlaneGeometry(
        180,
        180
    ),

    new THREE.MeshStandardMaterial({
        color:0x41663c,
        roughness:1
    })

);

ground.rotation.x =
-Math.PI / 2;

ground.receiveShadow =
true;

scene.add(
    ground
);
