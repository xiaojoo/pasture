import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   STREET LIGHTS
========================================================= */

export const streetLights = [];

export function createStreetLight(
    x,
    z
){

    const group =
    new THREE.Group();

    group.position.set(
        x,
        0,
        z
    );


    const pole =
    new THREE.Mesh(

        new THREE.CylinderGeometry(
            .11,
            .15,
            5.5,
            10
        ),

        new THREE.MeshStandardMaterial({
            color:0x29332f,
            metalness:.65,
            roughness:.4
        })

    );

    pole.position.y =
    2.75;

    pole.castShadow =
    true;

    group.add(
        pole
    );


    const arm =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            1.1,
            .08,
            .08
        ),

        new THREE.MeshStandardMaterial({
            color:0x29332f
        })

    );

    arm.position.set(
        .48,
        5.3,
        0
    );

    group.add(
        arm
    );


    const bulb =
    new THREE.Mesh(

        new THREE.SphereGeometry(
            .28,
            12,
            8
        ),

        new THREE.MeshStandardMaterial({

            color:0xffd98c,

            emissive:0xffa32c,

            emissiveIntensity:2

        })

    );

    bulb.position.set(
        .9,
        5.15,
        0
    );

    group.add(
        bulb
    );


    const light =
    new THREE.PointLight(
        0xffbd5c,
        90,
        20
    );

    light.position.set(
        .9,
        5,
        0
    );

    // ~20 lamps are placed along the roads; a shadow-casting point light costs
    // six extra render passes each, so the pools of light are unshadowed and
    // the moonlight directional carries the scene's shadows.
    light.castShadow =
    false;

    group.add(
        light
    );


    scene.add(
        group
    );


    streetLights.push({
        group,
        bulb,
        light
    });
}


/* 两侧主路 */

for(
    let z=-60;
    z<=60;
    z+=20
){

    createStreetLight(
        -6.5,
        z
    );

    createStreetLight(
        6.5,
        z
    );
}


/* 横向道路 */

for(
    let x=-45;
    x<=45;
    x+=18
){

    createStreetLight(
        x,
        -10
    );
}
