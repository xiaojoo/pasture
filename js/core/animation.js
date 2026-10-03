import * as THREE from 'three';
import { controls } from './camera.js';
import { camera, renderer, scene } from './scene.js';
import { isNight } from '../state/time.js';
import { advanceSky } from './sky.js';
import { advanceWater } from '../world/river.js';
import { advanceFalls } from '../world/falls.js';
import { advanceGrid } from './grid.js';
import { cows } from '../world/cows.js';
import { streetLights } from '../world/street-lights.js';
import { water } from '../world/water-tower.js';
import { drone, fpvCamera, updateDrone, view } from '../world/drone.js';
import { updateWaterClock } from '../state/water.js';
import { showUpdate } from '../show/fleet.js';
/* =========================================================
   ANIMATION
========================================================= */

export const clock =
new THREE.Clock();

const size =
new THREE.Vector2();

const chase =
new THREE.Vector3();

// Whether the previous frame was already being flown by the follow view: the frame that
// turns it on is placed on the rig instead of eased onto it.
let wasFollow =
false;

const UP =
new THREE.Vector3(
    0,
    1,
    0
);

// getElapsedTime() already consumes getDelta(), so the frame step is derived
// from the elapsed time itself rather than by calling both.
let lastTime =
0;


export function renderFpv(){

    if(!view.fpv)
        return;

    renderer.getSize(
        size
    );

    const w =
    Math.round(
        size.x * .27
    );

    const h =
    Math.round(
        size.y * .36
    );

    const x =
    Math.round(
        size.x * .50
    );

    const y =
    Math.round(
        size.y * (1 - .04 - .36)
    );

    fpvCamera.aspect =
    w / h;

    fpvCamera.updateProjectionMatrix();

    renderer.setScissorTest(
        true
    );

    renderer.setViewport(
        x,
        y,
        w,
        h
    );

    renderer.setScissor(
        x,
        y,
        w,
        h
    );

    renderer.autoClear =
    false;

    renderer.clearDepth();

    renderer.render(
        scene,
        fpvCamera
    );

    renderer.autoClear =
    true;

    renderer.setScissorTest(
        false
    );

    renderer.setViewport(
        0,
        0,
        size.x,
        size.y
    );

    if(view.frame)
        view.frame.style.cssText =
            `left:${x}px;top:${size.y - y - h}px;width:${w}px;height:${h}px`;

}


export function animate(){

    requestAnimationFrame(
        animate
    );


    const time =
    clock.getElapsedTime();

    const dt =
    Math.min(
        .05,
        time - lastTime
    );

    lastTime =
    time;


    updateDrone(
        dt,
        time
    );


    updateWaterClock(
        dt
    );


    // The show clock advances with the frame, not with the choreography page: the
    // fleet keeps performing after the operator closes the page to watch it.
    showUpdate(
        dt
    );


    // Clouds drift and the river runs off the same clock the fleet does, so a paused
    // animation loop stops the weather too rather than desynchronising it.
    advanceSky(
        dt
    );

    advanceWater(
        dt
    );

    advanceFalls(
        dt,
        time
    );

    advanceGrid(
        dt
    );


    if(view.follow){

        const k =
        Math.min(
            1,
            dt * 2.6
        );

        chase.set(
            0,
            6.5,
            -15
        ).applyAxisAngle(
            UP,
            drone.yaw
        ).add(
            drone.pos
        );

        if (
            wasFollow
        ){
            camera.position.lerp(
                chase,
                k
            );

            controls.target.lerp(
                drone.pos,
                k
            );
        } else {
            // The first frame of the follow view is placed, not eased. Lerping from wherever
            // the operator had orbited to -- measured 112 m out, on the far side of the barn --
            // sends the camera flying through the scene for about a second, and across that
            // transit the picture's best-fit shift reverses direction 5.8 times a second.
            // That is the 抖 he sees when he switches view; once the camera is on the rig the
            // same lerp measures 0.2 reversals a second.
            camera.position.copy(
                chase
            );

            controls.target.copy(
                drone.pos
            );
        }

        wasFollow =
        true;

    }

    else if (
        wasFollow
    ){
        wasFollow =
        false;
    }


    /* 水位 */

    water.position.y =
    8 +
    Math.sin(
        time * 1.3
    ) * .025;


    /* 路灯呼吸 */

    if(isNight){

        streetLights.forEach(
            (lamp,index) => {

                if(
                    lamp.light.visible
                ){

                    // Scale the intensity the lamp was built with. This used to assign
                    // `1.75 + sin(...)` outright, which silently replaced the designed
                    // 90 candela with a value that produced no measurable pool at all --
                    // and it meant the constructor's number was decoration.
                    lamp.light.intensity =
                    lamp.baseIntensity * (
                        1 +
                        Math.sin(
                            time * 2 +
                            index
                        ) * .06
                    );

                }

            }
        );

    }


    /* 牛轻微移动 */

    cows.forEach(
        (cow,index) => {

            cow.position.y =
            Math.sin(
                time * .6 +
                index
            ) * .025;

        }
    );


    controls.update();

    renderer.render(
        scene,
        camera
    );

    renderFpv();

}


animate();
