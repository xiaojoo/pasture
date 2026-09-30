import * as THREE from 'three';
import { controls } from '../core/camera.js';
import { camera } from '../core/scene.js';
/* =========================================================
   CAMERA FOCUS
========================================================= */

export const initialCamera =
new THREE.Vector3(
    65,
    48,
    65
);


export function resetCamera(){

    camera.position.copy(
        initialCamera
    );

    controls.target.set(
        0,
        0,
        0
    );

    controls.update();

};


export function focusBuilding(type){

    if(
        type === "house"
    ){

        camera.position.set(
            15,
            18,
            27
        );

        controls.target.set(
            -20,
            4,
            -17
        );

    }


    if(
        type === "barn"
    ){

        camera.position.set(
            55,
            20,
            10
        );

        controls.target.set(
            25,
            4,
            -25
        );

    }


    controls.update();

};
