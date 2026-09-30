import * as THREE from 'three';
import { ambient, sun } from '../core/lighting.js';
import { scene } from '../core/scene.js';
import { showToast } from '../ui/toast.js';
import { buildings } from '../world/house.js';
import { streetLights } from '../world/street-lights.js';
import { powerGrid } from './power.js';
import { boardRunning } from './boards.js';
/* =========================================================
   TIME
========================================================= */

export let isNight = true;

export let streetLightOn = true;
export let houseLightOn = true;

export function setTimeMode(mode){

    isNight =
    mode === "night";

    const day =
    mode === "day";


    document
        .getElementById("dayBtn")
        .classList.toggle(
            "active",
            day
        );

    document
        .getElementById("nightBtn")
        .classList.toggle(
            "active",
            !day
        );


    if(day){

        scene.background =
        new THREE.Color(
            0x8db7cc
        );

        scene.fog.color =
        new THREE.Color(
            0x8db7cc
        );

        scene.fog.near =
        70;

        scene.fog.far =
        180;

        ambient.color.set(
            0xb7d7c0
        );

        ambient.groundColor.set(
            0x15251a
        );

        ambient.intensity =
        1.05;

        sun.color.set(
            0xfff1d2
        );

        sun.position.set(
            50,
            90,
            40
        );

        sun.intensity =
        2.7;

        streetLights.forEach(
            l => {

                l.light.visible =
                false;

                l.bulb.material
                    .emissiveIntensity =
                    .15;

            }
        );

        // The windows go through the same two predicates the night branch uses, so
        // "the lighting board has not reported running yet" cannot be bright in the
        // day and dark at night.
        setHouseLights(houseLightOn);

        showToast(
            "已切换到白天模式"
        );

    }else{

        scene.background =
        new THREE.Color(
            0x06120c
        );

        scene.fog.color =
        new THREE.Color(
            0x0a1a16
        );

        // The ranch spans ~130 units from this camera; the daytime fog range
        // washed the whole world into the near-black fog colour at night.
        scene.fog.near =
        150;

        scene.fog.far =
        460;

        ambient.color.set(
            0x5d7f9c
        );

        ambient.groundColor.set(
            0x16241a
        );

        ambient.intensity =
        .55;

        sun.color.set(
            0xa8c8ff
        );

        sun.position.set(
            -60,
            70,
            -45
        );

        sun.intensity =
        .55;


        // One path for the lamps in every mode: both setters re-read the two
        // predicates below, so switching to night cannot light the yard past the
        // gate that the board is supposed to hold it behind.
        setStreetLights(streetLightOn);
        setHouseLights(houseLightOn);

        showToast(
            "已切换到夜晚模式"
        );

    }

    syncLightSwitches();

};


export function syncLightSwitches(){

    const street =
    document.getElementById("swStreet");

    if(street)
        street.classList.toggle("on",streetLightOn);

    const house =
    document.getElementById("swHouse");

    if(house)
        house.classList.toggle("on",houseLightOn);

};


// Whether each lamp actually has light to give. Three things have to agree, and the
// first is the one that used to be missing: the lighting board has to be up and
// reporting before its decision counts as a decision. Before that the ranch is dark
// and the drawer says why, which beats showing a lit yard that nobody switched on.
function streetFed(){

    return streetLightOn &&
        powerGrid.yardFed &&
        boardRunning('light');

}


function houseFed(){

    return houseLightOn &&
        powerGrid.yardFed &&
        boardRunning('light');

}


export function setStreetLights(on){

    streetLightOn =
    !!on;

    // The lighting board decides whether a lamp should be on; the cabinet decides
    // whether it has power to be on with. Both have to be true, and the switch
    // above keeps showing the request rather than the feed, because a switch that
    // lies about what the operator asked for is how a shed gets blamed on the
    // wrong board.
    const fed =
    streetFed();

    streetLights.forEach(
        l => {

            l.light.visible =
            isNight &&
            fed;

            l.bulb.material
                .emissiveIntensity =
                fed
                ? 2
                : .05;

        }
    );

    syncLightSwitches();

};


export function setHouseLights(on){

    houseLightOn =
    !!on;

    const fed =
    houseFed();

    buildings.house.windows.forEach(
        w => {

            w.material
                .emissiveIntensity =
                fed
                ? (isNight ? 1.7 : .2)
                : .02;

        }
    );

    syncLightSwitches();

};


// Called when the cabinet's yard feeder changes under us: the lamps have to come
// back on their own when the feeder closes, or the shed leaves the ranch dark with
// every switch on screen saying it should be lit.
export function reapplyFeeders(){
    setStreetLights(streetLightOn);
    setHouseLights(houseLightOn);
};


export function toggleStreetLights(){

    setStreetLights(
        !streetLightOn
    );

    showToast(
        streetLightOn
        ? "路灯系统已开启"
        : "路灯系统已关闭"
    );
};


export function toggleHouseLights(){

    setHouseLights(
        !houseLightOn
    );

    showToast(
        houseLightOn
        ? "建筑室内灯已开启"
        : "建筑室内灯已关闭"
    );
};
