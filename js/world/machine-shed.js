import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { metalMat } from './materials.js';
/* =========================================================
   MACHINE SHED
========================================================= */

export function createMachineShed(){

    const group =
    new THREE.Group();

    group.position.set(
        -28,
        0,
        23
    );

    group.userData.type =
    "machine";


    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            20,
            6,
            13
        ),

        metalMat
    );

    body.position.y =
    3;

    body.castShadow =
    true;

    group.add(
        body
    );


    const roof =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            21,
            .5,
            14
        ),

        new THREE.MeshStandardMaterial({
            color:0x404b4e,
            metalness:.5
        })

    );

    roof.position.y =
    6.3;

    group.add(
        roof
    );


    const door =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            7,
            4.5,
            .2
        ),

        new THREE.MeshStandardMaterial({
            color:0x252e2f
        })

    );

    door.position.set(
        0,
        2.25,
        6.6
    );

    group.add(
        door
    );


    scene.add(
        group
    );

    buildings.machine =
    {
        group
    };
}

createMachineShed();
