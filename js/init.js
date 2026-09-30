import './core/scene.js';
import './core/camera.js';
import './core/lighting.js';
import './core/ground.js';
import './core/grid.js';
import './world/road.js';
import './world/materials.js';
import './world/house.js';
import './world/barn.js';
import './world/warehouse.js';
import './world/machine-shed.js';
import './world/pump-house.js';
import './world/power-house.js';
import './world/water-tower.js';
import './world/pipes.js';
import './world/cables.js';
import './world/street-lights.js';
import './world/trees.js';
import './world/fence.js';
import './world/cows.js';
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
========================================================= */

setTimeMode(
    "night"
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
