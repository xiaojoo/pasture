import * as THREE from 'three';
/* =========================================================
   THREE
========================================================= */

export const container =
document.getElementById("scene");

export const scene =
new THREE.Scene();

scene.background =
new THREE.Color(0x07140c);

scene.fog =
new THREE.Fog(
    0x07140c,
    70,
    180
);


// near 0.1 bought nothing: controls.minDistance is 18, so nothing is ever within 18 m of the
// target, and a 0.1 near plane throws away depth precision that the seven ground layers need
// (see the Y ladder in js/world/layout.js). far stays at 2600 because js/core/sky.js draws a
// dome outside the land.
export const camera =
new THREE.PerspectiveCamera(
    55,
    innerWidth / innerHeight,
    .5,
    2600
);

// The overview used to sit at 65,48,65: a 27.6 degree downward pitch, which puts the true
// horizon exactly on the top edge of the frame. That is why the old "sky" was a flat band --
// it was the void outside the ground plane, not sky. 11.5 degrees of pitch keeps a readable
// three-quarter view of the yard and still leaves about the top eighth of the frame for sky,
// cloud and the crest of the range.
camera.position.set(
    78,
    27,
    78
);


export const renderer =
new THREE.WebGLRenderer({
    antialias:true
});

renderer.setPixelRatio(
    Math.min(
        devicePixelRatio,
        2
    )
);

renderer.setSize(
    innerWidth,
    innerHeight
);

renderer.shadowMap.enabled =
true;

renderer.shadowMap.type =
THREE.PCFSoftShadowMap;

renderer.outputColorSpace =
THREE.SRGBColorSpace;

container.appendChild(
    renderer.domElement
);
