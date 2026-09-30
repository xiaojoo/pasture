// The markup drives buttons with inline onclick="fn()", which resolves names
// against the global scope, so the exported functions are published here too.
import { setTimeMode, toggleHouseLights, toggleStreetLights } from './state/time.js';
import { toggleShowPage } from './ui/show-page.js';
import { closeDrawer, openDrawer } from './ui/drawer.js';
import { updateWaterVisual } from './ui/water-visual.js';
import { openSelectedBuilding } from './ui/interaction.js';
import { focusBuilding, resetCamera } from './ui/camera-focus.js';
import { showToast } from './ui/toast.js';

window.setTimeMode = setTimeMode;
window.toggleShowPage = toggleShowPage;
window.toggleStreetLights = toggleStreetLights;
window.toggleHouseLights = toggleHouseLights;
window.openDrawer = openDrawer;
window.closeDrawer = closeDrawer;
window.updateWaterVisual = updateWaterVisual;
window.openSelectedBuilding = openSelectedBuilding;
window.resetCamera = resetCamera;
window.focusBuilding = focusBuilding;
window.showToast = showToast;
