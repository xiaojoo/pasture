// The phone door on the browser side.
//
// The telemetry lives here -- in the page that scrapes the simulator frames -- so it
// is reported out to the server, and the phones read it back. Commands come the other
// way and are applied through the very same functions the on-screen buttons call, so
// a phone cannot do anything the drawer could not. Anything the browser genuinely
// cannot send to a board (there is no downlink into the simulator's serial) is
// reported as refused rather than pretending to have been carried out.
import { telemetry, boardStatusText } from '../ui/esp-panel.js';
import { boardUp } from './boards.js';
import { drone, setManualAxes, pilotCommand } from '../world/drone.js';
import { cmd } from '../ui/cmd.js';
import { waterSystem } from '../world/water-tower.js';
import { barnSchedule, setBarnSchedule, setHouseSupply } from './water.js';
import { powerGrid } from './power.js';
import { firePanel } from './fire.js';
import { setStreetLights, setHouseLights, setTimeMode, isNight } from './time.js';

const REPORT_MS = 500;
const POLL_MS = 250;

// What a phone may ask for, and what happens when it does. `none` means the page has
// no way to carry it out in the browser, and the reason is sent back so the app can
// say it on screen instead of showing a spinner that never resolves.
const CMDS = {
  drone: {
    takeoff: () => pilotCommand('takeoff'),
    patrol: () => pilotCommand('patrol'),
    hover: () => pilotCommand('hover'),
    resume: () => pilotCommand('resume'),
    // The three the firmware has always accepted. They have no local scene action, so
    // they go straight to the board and the drawer's ack line is what says whether it
    // landed -- which is also what the phone's 「板子回话」 row reads.
    arm: () => cmd.droneVerb('arm=1'),
    safe: () => cmd.droneVerb('safe=1'),
    camera: () => cmd.droneVerb('camera=1'),
    rtl: () => pilotCommand('rtl'),
    land: () => pilotCommand('land'),
    axes: v => setManualAxes({ fwd: num(v.fwd), side: num(v.side), climb: num(v.climb), yaw: num(v.yaw) }),
  },
  light: {
    street: v => { setStreetLights(!!v); cmd.lightStreet(!!v); },
    house: v => { setHouseLights(!!v); cmd.lightHouse(!!v); },
  },
  water: {
    barn: v => { setBarnSchedule({ on: !!v }); cmd.waterBarn(!!v); },
    house: v => { setHouseSupply(!!v); cmd.waterHouse(!!v); },
    period: v => { setBarnSchedule({ periodSec: clamp(num(v), 4, 120) }); cmd.waterPeriod(num(v)); },
    run: v => { setBarnSchedule({ runSec: clamp(num(v), 1, 60) }); cmd.waterRun(num(v)); },
  },
  scene: {
    night: () => setTimeMode('night'),
    day: () => setTimeMode('day'),
  },
  power: {
    reclose: () => cmd.powerReclose(),
    resume: () => cmd.powerResume(),
  },
  fire: {
    silence: () => cmd.fireSilence(),
    // A remote reset goes through the same gate as the key by the door, so the
    // board can and does refuse it: the refusal comes back as ack=no:reset.
    reset: () => cmd.fireReset(),
  },
};

const num = v => (Number.isFinite(Number(v)) ? Number(v) : 0);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// What the phones see. Every number here is either a board's own frame or a scene
// quantity the boards drive -- nothing is computed for the phone that the drawer is
// not already showing.
function snapshot() {
  return {
    boardsUp: { ...boardUp },
    // The command table travels with the state so the management console can offer
    // exactly what this page is able to carry out. A list kept over there would go
    // stale the moment a command is added or refused here.
    cmds: Object.entries(CMDS).map(([board, table]) => ({ board, cmds: Object.keys(table) })),
    boards: {
      light: telemetry.light ? { ...telemetry.light, status: boardStatusText('light') } : null,
      drone: telemetry.drone ? { ...telemetry.drone, status: boardStatusText('drone') } : null,
      water: telemetry.water ? { ...telemetry.water, status: boardStatusText('water') } : null,
      power: telemetry.power ? { ...telemetry.power, status: boardStatusText('power') } : null,
      fire: telemetry.fire ? { ...telemetry.fire, status: boardStatusText('fire') } : null,
      // The show airframe reports too, and the console's verdict panel reads its
      // `acts=`/`ck=` back to confirm an upload landed.
      show: telemetry.show ? { ...telemetry.show, status: boardStatusText('show') } : null,
    },
    scene: {
      night: isNight,
      streetOn: !!document.getElementById('swStreet')?.classList.contains('on'),
      houseOn: !!document.getElementById('swHouse')?.classList.contains('on'),
      barnOn: waterSystem.barnOn,
      houseWaterOn: waterSystem.houseOn,
      barnScheduleOn: barnSchedule.on,
      periodSec: barnSchedule.periodSec,
      runSec: barnSchedule.runSec,
      tankLevel: waterSystem.level,
      pressure: +waterSystem.pressure.toFixed(2),
      yardFed: powerGrid.yardFed,
      pumpFed: powerGrid.pumpFed,
      drone: {
        mode: drone.mode,
        alt: +drone.pos.y.toFixed(1),
        x: +drone.pos.x.toFixed(1),
        z: +drone.pos.z.toFixed(1),
        speed: +Math.hypot(drone.vel.x, drone.vel.z).toFixed(1),
        battery: +drone.battery.toFixed(0),
        rssi: drone.rssi,
        photos: drone.photos,
        flightSeconds: Math.round(drone.flightSeconds),
        wp: drone.wp,
        alert: drone.alert || '',
      },
      fire: { level: firePanel.level, zones: firePanel.zones.join(''), pumpPermit: firePanel.pumpPermit },
    },
  };
}

async function post(path, body) {
  const r = await fetch(path, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: body === undefined ? '' : JSON.stringify(body),
  });
  if (!r.ok) throw new Error(`${path} HTTP ${r.status}`);
  return r.json();
}

let lastAck = null;
let ok = false;

async function report() {
  try {
    await post('/api/report', { ...snapshot(), ack: lastAck });
    ok = true;
  } catch (e) {
    ok = false;
  }
}

async function drain() {
  let out;
  try {
    const r = await fetch('/api/cmds');
    out = await r.json();
  } catch { return; }
  for (const c of out.cmds || []) {
    const table = CMDS[c.board];
    const fn = table && table[c.cmd];
    let res;
    if (!fn) res = { refused: `没有 ${c.board}.${c.cmd} 这条指令` };
    else {
      try { res = fn(c.value); } catch (e) { res = { refused: `执行失败：${e.message}` }; }
    }
    lastAck = {
      id: c.id, board: c.board, cmd: c.cmd, at: Date.now(),
      applied: !!(res && res.refused) ? false : true,
      refused: res && res.refused ? res.refused : '',
    };
  }
}

// Only starts once the page can reach the server, so a phone that is not on the same
// network costs nothing.
setInterval(async () => {
  if (!ok) {
    try {
      const r = await fetch('/api/hello');
      if (!r.ok) return;
      ok = true;
    } catch { return; }
  }
  await report();
}, REPORT_MS);

setInterval(drain, POLL_MS);
