import { applySky } from '../core/sky.js';
import { ambient, sun } from '../core/lighting.js';
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

        ambient.color.set(
            0xcfe6d6
        );

        // The old near-black bounce colour is what made every north-facing slope a
        // silhouette: with no environment map this light is the only thing filling the
        // shadows, and the hills have a lot of shadow.
        ambient.groundColor.set(
            0x46523f
        );

        ambient.intensity =
        1.15;

        sun.color.set(
            0xffe0b4
        );

        // Late afternoon, from the left of the overview. The reference is lit at about
        // twenty-seven degrees of elevation out of the west-ish, which is what throws the
        // avenue shadows across the drive and gilds one side of every crown; the old pose was
        // (50,90,40) -- fifty-four degrees, near overhead, so nothing cast a shadow long
        // enough to read and the whole place looked like a noon catalogue.
        sun.position.set(
            -150,
            78,
            62
        );

        sun.intensity =
        3.3;

        // The dome, the sun disc and the fog colour all come out of one call now, so the
        // haze the far hills dissolve into is by construction the same colour as the sky
        // behind them.
        applySky('day', sun.position);

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

        ambient.color.set(
            0x5d7f9c
        );

        ambient.groundColor.set(
            0x1d2c24
        );

        ambient.intensity =
        .62;

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

        // At night the directional light is the moon, and applySky puts the disc in the
        // sky at the same place the shadows come from.
        applySky('night', sun.position);


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
