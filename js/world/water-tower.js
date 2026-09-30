import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   WATER TOWER
========================================================= */

export const waterSystem =
{
    pumpOn:true,
    valveOn:true,
    pressure:.42,
    level:72
};


export const waterTower =
new THREE.Group();

waterTower.position.set(
    45,
    0,
    25
);


export const towerLegMaterial =
new THREE.MeshStandardMaterial({
    color:0x515e62,
    metalness:.7,
    roughness:.35
});


for(
    let i=0;
    i<4;
    i++
){

    const a =
    Math.PI / 4 +
    i * Math.PI / 2;

    const leg =
    new THREE.Mesh(

        new THREE.CylinderGeometry(
            .18,
            .22,
            8,
            10
        ),

        towerLegMaterial
    );

    leg.position.set(
        Math.cos(a)*3,
        4,
        Math.sin(a)*3
    );

    leg.castShadow =
    true;

    waterTower.add(
        leg
    );
}


export const tankBody =
new THREE.Mesh(

    new THREE.CylinderGeometry(
        4.2,
        4.2,
        6,
        24
    ),

    new THREE.MeshStandardMaterial({
        color:0x66777c,
        metalness:.7,
        roughness:.32
    })

);

tankBody.position.y =
9;

tankBody.castShadow =
true;

waterTower.add(
    tankBody
);


export const water =
new THREE.Mesh(

    new THREE.CylinderGeometry(
        3.9,
        3.9,
        4,
        24
    ),

    new THREE.MeshStandardMaterial({

        color:0x229bd2,

        transparent:true,

        opacity:.72,

        emissive:0x07537a,

        emissiveIntensity:.35

    })

);

water.position.y =
8;

waterTower.add(
    water
);


scene.add(
    waterTower
);
