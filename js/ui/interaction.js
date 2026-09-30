import * as THREE from 'three';
import { camera, renderer, scene } from '../core/scene.js';
import { focusBuilding } from './camera-focus.js';
/* =========================================================
   BUILDING INTERACTION
========================================================= */

export const raycaster =
new THREE.Raycaster();

export const pointer =
new THREE.Vector2();

export let selectedBuilding =
null;


export const buildingInfo =
document.getElementById(
    "buildingInfo"
);

export const buildingName =
document.getElementById(
    "buildingName"
);

export const buildingDesc =
document.getElementById(
    "buildingDesc"
);


export const buildingNames = {

house:[
    "牧场主屋",
    "办公 / 生活 / 监控中心"
],

barn:[
    "牲畜牛舍",
    "286 头牲畜 · 环境监测"
],

warehouse:[
    "饲料仓库",
    "饲料库存与温湿度监测"
],

machine:[
    "农机库",
    "拖拉机 / 收割机 / 农机设备"
]

};


renderer.domElement.addEventListener(
    "pointerdown",
    event => {

        pointer.x =
        event.clientX /
        innerWidth * 2 - 1;

        pointer.y =
        -(event.clientY /
        innerHeight) * 2 + 1;


        raycaster.setFromCamera(
            pointer,
            camera
        );


        const objects =
        raycaster.intersectObjects(
            scene.children,
            true
        );


        let hit = null;


        for(
            const obj of objects
        ){

            let current =
            obj.object;


            while(
                current &&
                current !== scene
            ){

                if(
                    current.userData &&
                    current.userData.building
                ){

                    hit =
                    current.userData.building;

                    break;

                }

                current =
                current.parent;

            }

            if(hit)
                break;

        }


        if(!hit)
            return;


        selectedBuilding =
        hit;


        const data =
        buildingNames[hit];


        buildingName.innerText =
        data[0];

        buildingDesc.innerText =
        data[1];

        buildingInfo.classList.add(
            "show"
        );

    }
);


export function openSelectedBuilding(){

    if(
        !selectedBuilding
    )
        return;


    if(
        selectedBuilding === "house"
    ){

        focusBuilding(
            "house"
        );

    }else if(
        selectedBuilding === "barn"
    ){

        focusBuilding(
            "barn"
        );

    }

    buildingInfo.classList.remove(
        "show"
    );

};
