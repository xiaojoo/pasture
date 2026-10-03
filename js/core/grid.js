import * as THREE from 'three';
import { scene } from './scene.js';
import { controls } from './camera.js';
/* =========================================================
   GRID

   The survey grid. It is also, as of the fazenda rebuild, the only remaining feature whose
   screen width is smaller than the distance the picture travels between presented frames:
   measured on the real GPU at the shipped rotateSpeed, a 300-pixel drag moves the image
   2.32 px per frame, and this is a 1.0 px line. ratio 2.32 -- everything else on the site is
   over 2 px wide and sits under 1.8.

   So it fades out while the camera is being moved and comes back once it is released. That is
   not hiding a defect: a 1-pixel reference grid carries no readable information during an
   orbit anyway -- it is below the sampling rate of the display at that point, which is exactly
   why it strobes. What it does carry when the view is still, it keeps.

   The fade is driven from core/animation.js through advanceGrid() so the grid obeys the same
   clock as the water and the clouds: a paused loop stops the fade rather than leaving the
   grid stuck at whatever opacity the last frame happened to set.
========================================================= */

export const grid =
new THREE.GridHelper(
    170,
    34,
    0x61805e,
    0x41623f
);

grid.position.y =
.025;

grid.material.transparent =
true;

// The opacity it settles to when the view is still, and the one it drops to while moving.
export const GRID_REST_OPACITY =
.13;

const GRID_MOTION_OPACITY =
0;

grid.material.opacity =
GRID_REST_OPACITY;

scene.add(
    grid
);

let moving =
false;

controls.addEventListener(
    'start',
    () => { moving = true; }
);

controls.addEventListener(
    'end',
    () => { moving = false; }
);

// Exponential approach, not a step: a step is a flash of the grid appearing one frame after
// the hand lifts, which is its own transient and its own thing to notice.
export function advanceGrid(dt){

    const to =
    moving
    ? GRID_MOTION_OPACITY
    : GRID_REST_OPACITY;

    const k =
    1 - Math.exp(-dt / (moving ? .06 : .22));

    grid.material.opacity +=
    (to - grid.material.opacity) * k;

    grid.visible =
    grid.material.opacity > .004;

    return grid.material.opacity;
};
