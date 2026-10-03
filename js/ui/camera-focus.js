import * as THREE from 'three';
import { controls } from '../core/camera.js';
import { camera } from '../core/scene.js';
import { SITES } from '../world/layout.js';
/* =========================================================
   CAMERA FOCUS
========================================================= */

// The overview is the photograph: the camera stands on the road outside the gate, looks
// straight down the axis, and gets the boundary fence in the foreground, the arch under it,
// the drive between the front allée, the house at the head of the forecourt, and the rear
// avenue climbing away behind it. That is only readable if the axis is the frame's vertical
// centre line, which is what the reference is -- the track's centroid sits within half a
// pixel of the frame centre over its whole visible length.
//
// The height and the pitch are solved, not tuned. The reference puts the gate base at 78% of
// frame height, the house front at 49% and the top of the rear avenue at 30%. Over the ground
// distances in js/world/layout.js those are three equations in (height, distance, pitch) at a
// fixed 55-degree lens, and .probe/solve-cam.mjs solves them: pitch 24.0 degrees, camera 41 up
// and 101 back along the axis. The target is then a point 90 units down that same ray, chosen
// so the orbit pivot lands on the forecourt rather than out in the sky.
export const initialCamera =
new THREE.Vector3(
    0,
    41,
    101
);

export const initialTarget =
new THREE.Vector3(
    0,
    4,
    19
);


export function resetCamera(){

    camera.position.copy(
        initialCamera
    );

    controls.target.copy(
        initialTarget
    );

    controls.update();

};


// Written against the site table rather than as four numbers per building: when a yard moves
// in js/world/layout.js its own camera moves with it, instead of quietly framing empty grass.
const VIEWS = {
  // The house is looked at from the drive, square on, so the facade and its symmetry read.
  // Offsets are relative to the site, not absolute: the house moved to the head of the avenue
  // and an absolute pair here would have framed the empty forecourt it left behind.
  house: { at: [SITES.house.x, 15, SITES.house.z + 56], look: [SITES.house.x, 5, SITES.house.z + 4] },
  // The cattle shed from the lane side, which is the side its doors face: the barn is turned
  // ry=+pi/2, so its local +z (the big door, the apron, the loft) is world +x — the avenue and
  // the end of the mid lane. The first version of this line read "-36" and framed the blank
  // gable, from a camera standing inside the west pasture.
  barn: { at: [SITES.barn.x + 36, 14, SITES.barn.z + 16], look: [SITES.barn.x, 5, SITES.barn.z] },
};

export function focusBuilding(type){

    const v = VIEWS[type];
    if (!v) return;

    camera.position.set(...v.at);
    controls.target.set(...v.look);
    controls.update();

};
