import * as THREE from 'three';
import { scene } from '../core/scene.js';
/* =========================================================
   FENCE
========================================================= */

export function createFenceLine(
    x1,
    z1,
    x2,
    z2
){

    const posts = 14;

    for(
        let i=0;
        i<=posts;
        i++
    ){

        const t =
        i / posts;

        const x =
        x1 + (x2-x1)*t;

        const z =
        z1 + (z2-z1)*t;

        const post =
        new THREE.Mesh(

            new THREE.BoxGeometry(
                .16,
                1.5,
                .16
            ),

            new THREE.MeshStandardMaterial({
                color:0x65492f
            })

        );

        post.position.set(
            x,
            .75,
            z
        );

        post.castShadow =
        true;

        scene.add(
            post
        );

    }


    const curve =
    new THREE.LineCurve3(

        new THREE.Vector3(
            x1,
            1.1,
            z1
        ),

        new THREE.Vector3(
            x2,
            1.1,
            z2
        )

    );


    const wire =
    new THREE.Mesh(

        new THREE.TubeGeometry(
            curve,
            30,
            .035,
            5,
            false
        ),

        new THREE.MeshStandardMaterial({
            color:0x8b765a
        })

    );

    scene.add(
        wire
    );
}


/* 牲畜区域 */

createFenceLine(
    -45,
    20,
    -5,
    20
);

createFenceLine(
    -45,
    20,
    -45,
    55
);

createFenceLine(
    -5,
    20,
    -5,
    55
);

createFenceLine(
    -45,
    55,
    -5,
    55
);
