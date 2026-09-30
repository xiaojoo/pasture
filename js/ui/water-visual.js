import { waterPipes } from '../world/pipes.js';
import { waterSystem } from '../world/water-tower.js';
/* =========================================================
   WATER VISUAL
========================================================= */

// Each run is lit by its own valve: the barn cycle and the house supply are
// independent circuits, so a watering cycle must not brighten the house line.
export function updateWaterVisual(){

    const main =
    waterSystem.pumpOn;

    const live = {
        main,
        barn: main && waterSystem.valveOn && waterSystem.barnOn,
        feed: main && waterSystem.valveOn,
        "barn-trough": main && waterSystem.valveOn && waterSystem.barnOn,
        house: main && waterSystem.houseOn,
    };

    waterPipes.forEach(
        p => {

            const active =
            live[p.userData.line] ?? (main && waterSystem.valveOn);

            p.material.color.set(
                active
                ? 0x179bd0
                : 0x44565b
            );

            p.material.emissive.set(
                active
                ? 0x075a78
                : 0x111718
            );

            p.material.emissiveIntensity =
                active ? .65 : .05;

        }
    );

};
