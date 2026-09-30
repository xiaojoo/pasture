import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   POWER CABLES
========================================================= */

export const powerCables = [];

export function createCable(
    a,
    b
){

    const curve =
    new THREE.LineCurve3(
        a,
        b
    );

    const geometry =
    new THREE.TubeGeometry(
        curve,
        20,
        .065,
        6,
        false
    );

    const material =
    new THREE.MeshStandardMaterial({
        color:0xe2b744,
        emissive:0x634e12,
        emissiveIntensity:.6
    });

    const cable =
    new THREE.Mesh(
        geometry,
        material
    );

    scene.add(
        cable
    );

    powerCables.push(
        cable
    );

    return cable;
}


createCable(
    new THREE.Vector3(
        -42,5,-2
    ),
    new THREE.Vector3(
        -20,7,-17
    )
);

createCable(
    new THREE.Vector3(
        -42,5,-2
    ),
    new THREE.Vector3(
        -28,6,23
    )
);

createCable(
    new THREE.Vector3(
        -20,7,-17
    ),
    new THREE.Vector3(
        25,7,-25
    )
);

createCable(
    new THREE.Vector3(
        -20,7,-17
    ),
    new THREE.Vector3(
        26,6,18
    )
);
