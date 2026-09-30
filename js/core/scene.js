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


export const camera =
new THREE.PerspectiveCamera(
    55,
    innerWidth / innerHeight,
    .1,
    500
);

camera.position.set(
    65,
    48,
    65
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
