import './core/scene.js';
import './core/sky.js';
import './core/camera.js';
import './core/lighting.js';
import './core/ground.js';
import './core/grid.js';
import './world/road.js';
import './world/materials.js';
import './world/gate.js';
import './world/house.js';
import './world/barn.js';
import './world/warehouse.js';
import './world/machine-shed.js';
import './world/pump-house.js';
import './world/power-house.js';
import './world/water-tower.js';
import './world/pipes.js';
import './world/trees.js';
import './world/fence.js';
import './world/street-row.js';
// cables and street lights run last among the world modules: both place their posts by
// measuring what is already in the scene, so they must see the houses, the yards and the
// road bands.
import './world/cables.js';
// a lamp that runs earlier lands inside the six residential compounds and gets handed a
// half-empty obstacle list, so it goes after cables.
import './world/street-lights.js';
import './world/cows.js';
import './world/river.js';
import './world/falls.js';
import './world/nature.js';
import './state/time.js';
import './ui/drawer-data.js';
import './ui/drawer.js';
import './ui/water-visual.js';
import './ui/interaction.js';
import './ui/camera-focus.js';
import './ui/toast.js';
import './core/resize.js';
import './core/animation.js';
import './state/board-link.js';
import './state/phone-link.js';
import './globals.js';

import { setTimeMode } from './state/time.js';
import { runPreset } from './ui/esp-panel.js';
import { PRESETS } from './state/firmware.js';
import { updateWaterVisual } from './ui/water-visual.js';
import { toggleShowPage } from './ui/show-page.js';
import { openDesigner } from './ui/show-design.js';
/* =========================================================
   DEFAULT

   Day, not night. The night default was right when the point of opening the page was the
   lighting board; the point now is a composition that only exists in raking sunlight -- the
   avenue shadows across the drive, the red face of the caprock, the gilding on one side of
   every crown. 夜晚 is one click away and the board behaviour is unchanged.
========================================================= */

setTimeMode(
    "day"
);

updateWaterVisual();

// Entering the system is entering the boards: the first preset is loaded into the
// simulator and started without anybody pressing a switch. Which board is running
// is one decision, made once here and afterwards in the 开发板 picker -- except in the
// popped-out windows, which are the same page opened for the choreography and must not
// start a second set of simulators against the first one.
const query = new URLSearchParams(location.search);
if (query.get('show') === 'adv') {
  document.body.classList.add('show-adv');
  openDesigner();
} else if (query.get('show') === '1') {
  document.body.classList.add('show-solo');
  toggleShowPage(true);
} else {
  runPreset(PRESETS[0].id);
}
