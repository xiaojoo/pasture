import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { roofMat } from './materials.js';
/* =========================================================
   FEED STORAGE
========================================================= */

export function createWarehouse(){

    const group =
    new THREE.Group();

    group.position.set(
        26,
        0,
        18
    );

    group.userData.type =
    "warehouse";


    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            18,
            7,
            13
        ),

        new THREE.MeshStandardMaterial({
            color:0x9a8569,
            roughness:.9
        })

    );

    body.position.y =
    3.5;

    body.castShadow =
    true;

    group.add(
        body
    );


    const roof =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            19,
            .6,
            14
        ),

        roofMat
    );

    roof.position.y =
    7.2;

    roof.rotation.z =
    .025;

    group.add(
        roof
    );


    /* 门 */

    const door =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            5,
            4,
            .2
        ),

        new THREE.MeshStandardMaterial({
            color:0x4d3829
        })

    );

    door.position.set(
        0,
        2,
        6.6
    );

    group.add(
        door
    );


    scene.add(
        group
    );

    buildings.warehouse =
    {
        group
    };
}

createWarehouse();
