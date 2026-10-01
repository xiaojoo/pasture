import { drawer, drawerBody, drawerData, drawerSubtitle, drawerTitle } from './drawer-data.js';
import { mountEspPanel, setDockOpen, runPreset, wireLightingSwitches } from './esp-panel.js';
import { mountDronePanel } from './drone-panel.js';
import { mountWaterPanel } from './water-panel.js';
import { wireWaterSwitches } from './water-visual.js';
import { mountPowerPanel } from './power-panel.js';
import { mountFirePanel } from './fire-panel.js';
/* =========================================================
   DRAWER
========================================================= */

export function openDrawer(type){

    const data =
    drawerData[type];

    if(!data)
        return;


    drawerTitle.innerHTML =
    data.title;

    drawerSubtitle.innerHTML =
    data.subtitle;

    drawerBody.innerHTML =
    data.html;

    // innerHTML replaced the panel markup, so each rich panel is rebuilt against
    // the live state it reads.
    const mounts = {
        board: ["espHost", mountEspPanel],
        drone: ["droneHost", mountDronePanel],
        water: ["waterHost", mountWaterPanel],
        power: ["powerHost", mountPowerPanel],
        fire: ["fireHost", mountFirePanel],
    };

    const mount =
    mounts[type];

    if(mount){

        const host =
        document.getElementById(mount[0]);

        if(host)
            mount[1](host);

    }

    // The lighting switches live in this drawer's rebuilt markup, so they are wired
    // here rather than once at page load: the node a listener was put on last time is
    // gone, and a switch that flips the picture but never reaches the board is the bug
    // this whole pass was sent to find.
    if(type === "power")
        wireLightingSwitches();

    // Same story in the 水利 drawer: two master switches whose inline handler named a
    // module binding that is not on `window`, so pressing them only logged a ReferenceError.
    if(type === "water")
        wireWaterSwitches();


    if(type === "board")
        setDockOpen(
            true
        );

    // Opening a subsystem is what starts its board: no switch, no second click. The
    // simulator instance is created on demand and kept afterwards, so the boards you
    // have visited stay reporting and the ones you have not cost nothing.
    const BOARD_OF = { power: "power", fire: "fire", water: "water", drone: "drone" };

    if(BOARD_OF[type])
        runPreset(
            BOARD_OF[type]
        );


    drawer.classList.add(
        "open"
    );
};


export function closeDrawer(){

    setDockOpen(
        false
    );

    drawer.classList.remove(
        "open"
    );

};
