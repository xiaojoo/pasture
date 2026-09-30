import { drawer, drawerBody, drawerData, drawerSubtitle, drawerTitle } from './drawer-data.js';
import { mountEspPanel, setDockOpen, runPreset } from './esp-panel.js';
import { mountDronePanel } from './drone-panel.js';
import { mountWaterPanel } from './water-panel.js';
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
