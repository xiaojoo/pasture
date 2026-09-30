import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { roofMat } from './materials.js';
/* =========================================================
   PUMP HOUSE
========================================================= */

export function createPumpHouse(){

    const group =
    new THREE.Group();

    group.position.set(
        43,
        0,
        5
    );

    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            10,
            5,
            8
        ),

        new THREE.MeshStandardMaterial({
            color:0x789184,
            roughness:.9
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
            11,
            .5,
            9
        ),

        roofMat
    );

    roof.position.y =
    5.3;

    group.add(
        roof
    );


    scene.add(
        group
    );

    buildings.pump =
    {
        group
    };
}

createPumpHouse();
