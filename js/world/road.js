import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   ROAD
========================================================= */

export function createRoad(
    x,
    z,
    width,
    length,
    rotation=0
){

    const road =
    new THREE.Mesh(

        new THREE.PlaneGeometry(
            width,
            length
        ),

        new THREE.MeshStandardMaterial({
            color:0x5b5d55,
            roughness:1
        })

    );

    road.rotation.x =
    -Math.PI / 2;

    road.rotation.z =
    rotation;

    road.position.set(
        x,
        .04,
        z
    );

    road.receiveShadow =
    true;

    scene.add(
        road
    );
}


/* 主干道 */

createRoad(
    0,
    0,
    9,
    150
);


/* 横向道路 */

createRoad(
    0,
    -5,
    135,
    8
);


/* 支路 */

createRoad(
    -29,
    -31,
    7,
    55,
    Math.PI / 2
);

createRoad(
    30,
    27,
    7,
    55,
    Math.PI / 2
);
