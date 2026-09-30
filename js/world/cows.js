import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   COWS
========================================================= */

export const cows = [];

export function createCow(
    x,
    z,
    scale=.8
){

    const group =
    new THREE.Group();


    const body =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            2.2,
            1.15,
            1.1
        ),

        new THREE.MeshStandardMaterial({
            color:0xe7e0d4,
            roughness:1
        })

    );

    body.position.y =
    1.15;

    group.add(
        body
    );


    const head =
    new THREE.Mesh(

        new THREE.BoxGeometry(
            .8,
            .85,
            .8
        ),

        new THREE.MeshStandardMaterial({
            color:0xd5cabc
        })

    );

    head.position.set(
        1.35,
        1.4,
        0
    );

    group.add(
        head
    );


    /* 腿 */

    for(
        let i=0;
        i<4;
        i++
    ){

        const leg =
        new THREE.Mesh(

            new THREE.CylinderGeometry(
                .1,
                .12,
                .9,
                6
            ),

            new THREE.MeshStandardMaterial({
                color:0x594838
            })

        );

        leg.position.set(
            i<2 ? .7 : -.7,
            .45,
            i%2===0 ? .35 : -.35
        );

        group.add(
            leg
        );

    }


    group.position.set(
        x,
        0,
        z
    );

    group.rotation.y =
    Math.random() *
    Math.PI * 2;

    group.scale.setScalar(
        scale
    );

    scene.add(
        group
    );

    cows.push(
        group
    );
}


[
    [-35,29],
    [-25,38],
    [-14,28],
    [-36,46],
    [-17,48],
    [-8,37],
    [-30,51]
].forEach(
    p =>
    createCow(
        p[0],
        p[1],
        .7 + Math.random()*.15
    )
);
