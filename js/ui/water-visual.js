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

    paintSwitches();

};

// The two master switches in the 水利 drawer. They came over from the single-file page
// with an inline `onclick="waterSystem.pumpOn = !waterSystem.pumpOn"`; `waterSystem` is a
// module binding and is not on `window`, so every press threw
// `ReferenceError: waterSystem is not defined` and neither the state nor the pipes moved.
//
// Their lit bar is repainted from the state on every pass rather than trusted to the
// markup: `drawerData` is an object of template strings evaluated once at import, so the
// `on` class written into it is the state as it was on page load. A switch that keeps
// showing last-load position after you have flipped it -- or after the board has driven
// the lines -- is the same lie the lighting switches used to tell.
const SWITCHES = [
  ['swPump', 'pumpOn', '1 号泵'],
  ['swValve', 'valveOn', '东区总阀'],
];

export function wireWaterSwitches() {
  for (const [id, key, label] of SWITCHES) {
    const el = document.getElementById(id);
    if (!el || el.dataset.wired) continue;
    el.dataset.wired = '1';
    el.title = `${label}：场景管路的总开关，只管 3D 里这一段画不画水。水利板没有开泵/开阀的动词，`
      + '它自己的泵在帧里以 pump= 报，改不动';
    el.addEventListener('click', () => {
      waterSystem[key] = !waterSystem[key];
      updateWaterVisual();
    });
  }
  paintSwitches();
  return SWITCHES.every(([id]) => !!document.getElementById(id));
}

function paintSwitches() {
  for (const [id, key] of SWITCHES) {
    const el = document.getElementById(id);
    if (!el) continue;
    el.classList.toggle('on', !!waterSystem[key]);
    const row = el.closest('.device');
    const desc = row && row.querySelector('.device-desc');
    if (desc) desc.textContent = `场景管路：${waterSystem[key] ? '通水' : '断水'}`;
  }
}
