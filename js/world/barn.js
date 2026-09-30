import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { roofMat, woodMat } from './materials.js';
/* =========================================================
   BARN
========================================================= */

export function createBarn(){

    const group =
    new THREE.Group();

    group.position.set(
        25,
        0,
        -25
    );

    group.userData.type =
    "barn";


    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            25,
            9,
            17
        ),

        woodMat
    );

    body.position.y =
    4.5;

    body.castShadow =
    true;

    body.receiveShadow =
    true;

    body.userData.building =
    "barn";

    group.add(
        body
    );


    const roof =
    new THREE.Mesh(

        new THREE.ConeGeometry(
            17,
            6,
            4
        ),

        roofMat
    );

    roof.rotation.y =
    Math.PI / 4;

    roof.position.y =
    11;

    roof.castShadow =
    true;

    group.add(
        roof
    );


    /* 牛舍大门 */

    const door =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            7,
            6,
            .25
        ),

        new THREE.MeshStandardMaterial({
            color:0x38291f
        })

    );

    door.position.set(
        0,
        3,
        8.65
    );

    group.add(
        door
    );


    /* 通风窗 */

    for(
        let x=-8;
        x<=8;
        x+=8
    ){

        const w =
        new THREE.Mesh(

            new THREE.BoxGeometry(
                3,
                1.5,
                .18
            ),

            new THREE.MeshStandardMaterial({
                color:0x7f9b94,
                metalness:.3
            })

        );

        w.position.set(
            x,
            5.5,
            8.7
        );

        group.add(
            w
        );

    }


    scene.add(
        group
    );

    buildings.barn =
    {
        group
    };

    return group;
}

createBarn();
