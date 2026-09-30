import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   TREES
========================================================= */

export function createTree(
    x,
    z,
    scale=1
){

    const group =
    new THREE.Group();

    const trunk =
    new THREE.Mesh(

        new THREE.CylinderGeometry(
            .4,
            .65,
            4,
            8
        ),

        new THREE.MeshStandardMaterial({
            color:0x60432d
        })

    );

    trunk.position.y =
    2;

    trunk.castShadow =
    true;

    group.add(
        trunk
    );


    const crown =
    new THREE.Mesh(

        new THREE.SphereGeometry(
            3.2,
            10,
            8
        ),

        new THREE.MeshStandardMaterial({
            color:0x2e653b,
            roughness:1
        })

    );

    crown.position.y =
    5.2;

    crown.castShadow =
    true;

    group.add(
        crown
    );


    group.position.set(
        x,
        0,
        z
    );

    group.scale.setScalar(
        scale
    );

    scene.add(
        group
    );
}


[
    [-55,-48,1.1],
    [-48,-28,.9],
    [-54,15,1.2],
    [-50,43,1],
    [-38,49,.8],
    [-5,48,1.1],
    [8,48,.9],
    [50,48,1.2],
    [55,35,.9],
    [54,8,1.1],
    [52,-18,.9],
    [46,-48,1.1],
    [12,-50,.8],
    [-15,-50,1],
    [0,32,.8]
].forEach(
    p =>
    createTree(
        p[0],
        p[1],
        p[2]
    )
);
