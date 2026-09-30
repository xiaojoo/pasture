import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { roofMat, wallMat } from './materials.js';
/* =========================================================
   HOUSE
========================================================= */

export const buildings = {};

export function createHouse(){

    const group =
    new THREE.Group();

    group.position.set(
        -20,
        0,
        -17
    );

    group.userData.type =
    "house";


    /* 房体 */

    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            23,
            7,
            16
        ),

        wallMat
    );

    body.position.y =
    3.5;

    body.castShadow =
    true;

    body.receiveShadow =
    true;

    body.userData.building =
    "house";

    group.add(
        body
    );


    /* 屋顶 */

    const roof =
    new THREE.Mesh(

        new THREE.ConeGeometry(
            15,
            5,
            4
        ),

        roofMat
    );

    roof.rotation.y =
    Math.PI / 4;

    roof.position.y =
    9;

    roof.castShadow =
    true;

    roof.userData.building =
    "house";

    group.add(
        roof
    );


    /* 门 */

    const door =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            3,
            4.4,
            .25
        ),

        new THREE.MeshStandardMaterial({
            color:0x573823
        })

    );

    door.position.set(
        0,
        2.2,
        8.1
    );

    group.add(
        door
    );


    /* 窗户 */

    const windowMat =
    new THREE.MeshStandardMaterial({

        color:0xffd98a,

        emissive:0xffaa32,

        emissiveIntensity:1.7

    });


    const windows =
    [];


    for(
        let x=-7;
        x<=7;
        x+=7
    ){

        const window =
        new THREE.Mesh(

            new THREE.BoxGeometry(
                3.5,
                2.5,
                .18
            ),

            windowMat.clone()
        );

        window.position.set(
            x,
            4.3,
            8.15
        );

        group.add(
            window
        );

        windows.push(
            window
        );

    }


    /* 烟囱 */

    const chimney =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            1.6,
            4,
            1.6
        ),

        new THREE.MeshStandardMaterial({
            color:0x66534a
        })

    );

    chimney.position.set(
        5,
        10,
        -2
    );

    chimney.castShadow =
    true;

    group.add(
        chimney
    );


    scene.add(
        group
    );

    buildings.house = {
        group,
        windows
    };

    return group;
}

createHouse();
