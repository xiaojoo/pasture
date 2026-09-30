// Applies whatever the boards report to the scene. There is no takeover switch:
// a board that is running is the authority for its subsystem, and one that is not
// running is reported as absent rather than quietly replaced by the app.
import { activePreset, telemetry } from '../ui/esp-panel.js';
import { PRESETS } from '../state/firmware.js';
import { applyBoardCommand, ROUTE, drone } from '../world/drone.js';
import { applyBoardWater, waterBoardLive } from '../state/water.js';
import { boardRunning } from '../state/boards.js';
import { applyPowerFrame, powerGrid, powerStale } from '../state/power.js';
import { applyFireFrame, fireAlarmCount, fireHealthyLoops, firePanel, fireStale } from '../state/fire.js';
import { reapplyFeeders } from '../state/time.js';
import { waterSystem } from '../world/water-tower.js';

// The five menu subtitles used to be typed HTML that said "3 个预设" and
// "配电正常 · 12 个设备" forever, whatever was or was not plugged in. They now say
// what the boards say, including "未接管" and "已静默", which are the two answers a
// static page can never give.
const MENU = {
  board: document.getElementById('menuBoard'),
  power: document.getElementById('menuPower'),
  drone: document.getElementById('menuDrone'),
  water: document.getElementById('menuWater'),
  fire: document.getElementById('menuFire'),
};

const MODE_TEXT = { IDLE: '待命', PATROL: '巡航', TRANSIT: '巡航', DWELL: '悬停', DESCEND: '降落', RETURN: '返航', GROUND: '地面', LANDED: '待命' };

function syncMenu() {
  if (MENU.board) {
    MENU.board.textContent = `${PRESETS.length} 个预设 · ` +
      (activePreset ? `${activePreset.label.split(' ·')[0]} 在仿真` : '未载入');
  }
  if (MENU.power) {
    MENU.power.textContent = powerStale() ? '配电板未上报'
      : powerGrid.act === 'TRIP' ? `配电跳闸 · ${powerGrid.why}`
      : powerGrid.act === 'SHED' ? `配电减载 · ${powerGrid.loadPct.toFixed(0)}%`
      : `配电正常 · ${powerGrid.kw.toFixed(1)} kW · ${powerGrid.loadPct.toFixed(0)}%`;
  }
  if (MENU.fire) {
    MENU.fire.textContent = fireStale() ? '消防板未上报'
      : firePanel.level === 'EMERGENCY' ? `火警确认 · ${fireAlarmCount()} 路`
      : firePanel.level === 'ALARM' ? `单路报警 · ${fireAlarmCount()} 路`
      : firePanel.level === 'FAULT' ? `监管故障 · ${firePanel.why || '回路异常'}`
      : `${fireHealthyLoops()}/4 回路正常 · 水池 ${waterSystem.level}%`;
  }
  if (MENU.drone) {
    MENU.drone.textContent = `${ROUTE.length} 个航点 · ${MODE_TEXT[drone.mode] || drone.mode}`;
  }
  if (MENU.water) {
    // Three states, not two: "本地定时器" used to be written while the board was
    // still compiling, and the timer was not running either.
    MENU.water.textContent = `水压 ${waterSystem.pressure.toFixed(2)} MPa · ` +
      (!boardRunning('water') ? '等水利板运行成功'
        : waterBoardLive() ? '板子在管' : '本地定时器');
  }
}

setInterval(() => {
  if (telemetry.drone) applyBoardCommand(telemetry.drone);
  if (telemetry.water) applyBoardWater(telemetry.water);
  // Re-applied on every frame rather than on an edge: the lamps have to come back
  // when the cabinet closes the yard feeder again, and an edge needs a private
  // copy of the state to compare against -- which goes out of sync the moment
  // anything else writes that state, leaving the yard dark and lit in the report.
  if (telemetry.fire) applyFireFrame(telemetry.fire);
  if (telemetry.power) {
    applyPowerFrame(telemetry.power);
    reapplyFeeders();
  }
  syncMenu();
}, 200);
