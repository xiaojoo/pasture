import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
/* =========================================================
   POWER ROOM
========================================================= */

export function createPowerHouse(){

    const group =
    new THREE.Group();

    group.position.set(
        -42,
        0,
        -2
    );


    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            9,
            5,
            8
        ),

        new THREE.MeshStandardMaterial({
            color:0x626a6d,
            metalness:.5,
            roughness:.6
        })

    );

    body.position.y =
    2.5;

    body.castShadow =
    true;

    group.add(
        body
    );


    const roof =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            10,
            .5,
            9
        ),

        new THREE.MeshStandardMaterial({
            color:0x394245
        })

    );

    roof.position.y =
    5.3;

    group.add(
        roof
    );


    /* 黄色警示标志 */

    const warning =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            1.6,
            1.6,
            .12
        ),

        new THREE.MeshStandardMaterial({
            color:0xffc928,
            emissive:0x6d4c00,
            emissiveIntensity:.5
        })

    );

    warning.position.set(
        0,
        3,
        4.1
    );

    group.add(
        warning
    );


    scene.add(
        group
    );

    buildings.power =
    {
        group
    };
}

createPowerHouse();
