// Water supply state: the barn runs on a timed cycle, the house line is a
// separate valve that nothing else touches.
import { waterSystem } from '../world/water-tower.js';
import { updateWaterVisual } from '../ui/water-visual.js';
import { boardRunning } from './boards.js';

export const barnSchedule = { on: true, periodSec: 12, runSec: 4, elapsed: 0 };

export const waterState = {
  lastSeen: 0,
  nextSec: 0,
  barnFlow: 0,
  houseFlow: 0,
  cycles: 0,
  alert: '',
};

// Both valves start shut. The barn's timed cycle below is the app's own behaviour,
// and running it from page load moved water on nobody's authority -- the board that
// owns these two valves had not even compiled. They open when it reports running.
waterSystem.barnOn = false;
waterSystem.houseOn = false;

let lastBarn = null;
let lastHouse = null;

function repaint() {
  updateWaterVisual();
}

export function setBarnSchedule(patch) {
  Object.assign(barnSchedule, patch);
  if (barnSchedule.periodSec > 0) {
    barnSchedule.elapsed = Math.min(barnSchedule.elapsed, barnSchedule.periodSec);
  }
}

export function waterBoardLive() {
  // Three seconds is six WATER frames: long enough to ride out a publish, short
  // enough that a board which stopped reporting stops being believed.
  return !!waterState.lastSeen && Date.now() - waterState.lastSeen < 3000;
}

export function setHouseSupply(on) {
  if (waterBoardLive() || !boardRunning('water')) return;
  waterSystem.houseOn = !!on;
  repaint();
}

// The board's own report wins while it is driving the water system.
export function applyBoardWater(t) {
  waterState.lastSeen = Date.now();
  waterSystem.barnOn = t.barn === 'on';
  waterSystem.houseOn = t.house === 'on';
  waterState.barnFlow = Number(t.flow) || 0;
  waterState.nextSec = Number(t.next) || 0;
  repaint();
}

export function releaseBoardWater() {
  waterState.lastSeen = 0;
  waterState.alert = '';
}

export function updateWaterClock(dt) {
  const up = boardRunning('water');
  if (!up) {
    waterSystem.barnOn = false;
    waterSystem.houseOn = false;
    waterState.nextSec = 0;
  } else if (!barnSchedule.on) {
    waterSystem.barnOn = false;
    waterState.nextSec = 0;
  } else if (!waterBoardLive()) {
    barnSchedule.elapsed += dt;
    if (barnSchedule.elapsed >= barnSchedule.periodSec) {
      barnSchedule.elapsed = 0;
      waterState.cycles++;
    }
    waterSystem.barnOn = barnSchedule.elapsed < barnSchedule.runSec;
    waterState.nextSec = waterSystem.barnOn
      ? barnSchedule.runSec - barnSchedule.elapsed
      : barnSchedule.periodSec - barnSchedule.elapsed;
  }

  waterState.barnFlow = waterSystem.barnOn ? 3.4 : 0;
  if (!waterBoardLive()) waterState.houseFlow = waterSystem.houseOn ? 1.1 : 0;

  if (waterSystem.barnOn !== lastBarn || waterSystem.houseOn !== lastHouse) {
    lastBarn = waterSystem.barnOn;
    lastHouse = waterSystem.houseOn;
    repaint();
  }
}
