import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { camera, renderer } from './scene.js';
/* =========================================================
   CAMERA
========================================================= */

export const controls =
new OrbitControls(
    camera,
    renderer.domElement
);

controls.enableDamping =
true;

// A drag used to coast for 90 frames (~0.64 s at 140 fps) after release, first step 5.01
// units of camera travel. Every one of those frames resamples the road paint, which is what
// "stops flickering after a while" actually was. Halving the tail and slowing the rotation
// shortens the window the shimmer is visible in; it does not remove the shimmer itself.
controls.dampingFactor =
.12;

// OrbitControls turns 360 * rotateSpeed / clientHeight degrees per pixel of pointer travel, and
// the frame shows 55 degrees across clientHeight pixels, so the picture slides
// 360 * rotateSpeed / 55 = 6.5 * rotateSpeed pixels per pointer pixel -- independent of window
// size. At .75 that is 4.9 screen pixels per pixel of drag: one frame of a ordinary drag moves
// every thin bright line by more than twice its own width, and a two-pixel line that jumps four
// pixels simply is not in the same place twice -- which is what reads as the white trim
// strobing. .45 brings it to 2.9 and the strobing goes with it. A hard flick still strobes;
// nothing short of motion blur survives that.
controls.rotateSpeed =
.45;

controls.minDistance =
18;

// The overview stands outside the gate and looks down the whole axis -- gate, forecourt,
// house, rear avenue -- which is ~174 units from target to camera. The old 150 ceiling was
// written when the overview was a three-quarter look at the yard from 90 out, and OrbitControls
// silently pulls the camera in to whatever it allows, so a 150 limit would have arrived at the
// new view as "the picture is right but you cannot get any further back".
controls.maxDistance =
320;

controls.maxPolarAngle =
Math.PI * .48;

controls.target.set(
    0,
    4,
    -4
);
