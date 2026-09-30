import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   WATER PIPES
========================================================= */

export const waterPipes = [];

export function createPipe(
    a,
    b,
    line
){

    const dir =
    new THREE.Vector3()
        .subVectors(b,a);

    const len =
    dir.length();

    const geometry =
    new THREE.CylinderGeometry(
        .13,
        .13,
        len,
        8
    );

    const material =
    new THREE.MeshStandardMaterial({
        color:0x178bb8,
        emissive:0x064d68,
        emissiveIntensity:.45
    });

    const mesh =
    new THREE.Mesh(
        geometry,
        material
    );

    mesh.position
        .copy(a)
        .add(b)
        .multiplyScalar(.5);

    mesh.userData.line =
    line ||
    "main";


    mesh.quaternion
        .setFromUnitVectors(
            new THREE.Vector3(0,1,0),
            dir.normalize()
        );

    scene.add(
        mesh
    );

    waterPipes.push(
        mesh
    );

    return mesh;
}


createPipe(
    new THREE.Vector3(
        45,2,25
    ),
    new THREE.Vector3(
        43,2,5
    ),
    "main"
);

createPipe(
    new THREE.Vector3(
        43,2,5
    ),
    new THREE.Vector3(
        25,2,-25
    ),
    "barn"
);

createPipe(
    new THREE.Vector3(
        43,2,5
    ),
    new THREE.Vector3(
        26,2,18
    ),
    "feed"
);

createPipe(
    new THREE.Vector3(
        25,2,-25
    ),
    new THREE.Vector3(
        -20,2,-17
    ),
    "barn-trough"
);


/* 主屋独立一路：从水泵房直接过去，不经牛舍 */

createPipe(
    new THREE.Vector3(
        43,2,5
    ),
    new THREE.Vector3(
        12,2,-2
    ),
    "house"
);


createPipe(
    new THREE.Vector3(
        12,2,-2
    ),
    new THREE.Vector3(
        -20,2,-17
    ),
    "house"
);
