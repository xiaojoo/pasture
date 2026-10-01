// The inspection drone: model, flight state and the patrol route.
//
// Waypoints are the real building positions from js/world/*, so the route
// passes over things that exist. Flight is kinematic (position/attitude
// integrated from a commanded velocity) -- enough for piloting, FPV and
// telemetry without pretending to be a flight controller.
import * as THREE from 'three';
import { scene } from '../core/scene.js';

export const GROUND_STATION = { x: -20, z: -17, name: '主屋（地面站）' };

export const ROUTE = [
  { name: '主屋', x: -20, z: -17 },
  { name: '牛舍', x: 25, z: -25 },
  { name: '饲料仓', x: 26, z: 18 },
  { name: '水塔', x: 45, z: 25 },
  { name: '水泵房', x: 43, z: 5 },
  { name: '牛栏', x: -25, z: 38 },
  { name: '农机库', x: -28, z: 23 },
  { name: '配电房', x: -42, z: -2 },
];

// The rig's origin is the landing-skid height, so pos.y reads as altitude
// above ground and the gimbal camera stays above the surface when landed.
const GEAR = 1.05;

const PATROL_ALT = 18;
const SPEED = 9;
const MAX_TILT = 0.34;
const LINK_LIMIT = 120;

export const input = { fwd: 0, side: 0, climb: 0, yaw: 0 };

// Camera options plus the DOM element that frames the FPV inset, so the border
// and the WebGL scissor rectangle are positioned from the same numbers.
export const view = { fpv: false, follow: false, frame: null };

export const drone = {
  mode: 'IDLE',
  source: 'manual',
  // The board's own answer to "who is flying the sticks": 1 while it is forwarding
  // them to the flight controller, 0 when it has released them, null when no frame
  // has said either. `rcWhy` is the reason the board last refused an axis line.
  ovr: null,
  rcWhy: '',
  // The last mission phase the board reported, kept for the fault line: "the board is
  // still in GROUND" is only useful if the panel can say GROUND without a frame in hand.
  boardMode: '',
  // The board's own safety switch and the reason it gave. Without these a launch that
  // never happened reads as "the board ignored me" instead of "the board is still locked
  // and here is what it said".
  boardSafe: null,
  boardWhy: '',
  // The board's own above-ground height, once it has reported one. Null means "no
  // frame", which is different from 0 ("on the pad").
  boardAgl: null,
  pos: new THREE.Vector3(GROUND_STATION.x, 0, GROUND_STATION.z),
  vel: new THREE.Vector3(),
  yaw: 0,
  targetAlt: PATROL_ALT,
  battery: 100,
  rssi: -42,
  wp: 0,
  hover: 0,
  photos: 0,
  flightSeconds: 0,
  alert: '',
};

/* ---------- model ---------- */
const bodyMat = new THREE.MeshStandardMaterial({ color: 0x2b3138, roughness: 0.55, metalness: 0.35 });
const armMat = new THREE.MeshStandardMaterial({ color: 0x1d2227, roughness: 0.7, metalness: 0.3 });
const motorMat = new THREE.MeshStandardMaterial({ color: 0x8d949c, roughness: 0.4, metalness: 0.8 });
const propMat = new THREE.MeshStandardMaterial({ color: 0x0f1215, roughness: 0.6, transparent: true, opacity: 0.92 });
const carbonMat = new THREE.MeshStandardMaterial({ color: 0x3a4048, roughness: 0.8 });
const glassMat = new THREE.MeshStandardMaterial({ color: 0x0b1520, roughness: 0.15, metalness: 0.6 });

export const rig = new THREE.Group();
const props = [];

function arm(x, z, index) {
  const g = new THREE.Group();
  const boom = new THREE.Mesh(new THREE.BoxGeometry(2.5, 0.18, 0.34), armMat);
  boom.position.set(x * 0.5, 0, z * 0.5);
  boom.lookAt(x, 0, z);
  boom.castShadow = true;
  g.add(boom);

  const motor = new THREE.Mesh(new THREE.CylinderGeometry(0.3, 0.36, 0.44, 16), motorMat);
  motor.position.set(x, 0.16, z);
  motor.castShadow = true;
  g.add(motor);

  const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.12, 0.16, 10), carbonMat);
  hub.position.set(x, 0.44, z);
  g.add(hub);

  const rotor = new THREE.Group();
  rotor.position.set(x, 0.46, z);
  for (let b = 0; b < 2; b++) {
    const blade = new THREE.Mesh(new THREE.BoxGeometry(2.9, 0.05, 0.26), propMat);
    blade.position.set(b ? -0.72 : 0.72, 0, 0);
    blade.rotation.y = Math.PI / 2;
    rotor.add(blade);
  }
  g.add(rotor);
  props.push({ rotor, dir: index % 2 ? -1 : 1 });

  const led = new THREE.Mesh(
    new THREE.SphereGeometry(0.17, 10, 8),
    new THREE.MeshStandardMaterial({ color: 0x111111, emissive: new THREE.Color(index < 2 ? 0xff2b2b : 0x2bff88), emissiveIntensity: 2 }),
  );
  led.position.set(x * 1.06, -0.12, z * 1.06);
  g.add(led);
  return g;
}

rig.add(arm(1.9, 1.9, 0), arm(-1.9, 1.9, 1), arm(1.9, -1.9, 2), arm(-1.9, -1.9, 3));

const hull = new THREE.Mesh(new THREE.BoxGeometry(2.7, 0.72, 3.5), bodyMat);
hull.castShadow = true;
rig.add(hull);

const canopy = new THREE.Mesh(new THREE.BoxGeometry(1.7, 0.5, 2.1), carbonMat);
canopy.position.set(0, 0.55, -0.2);
canopy.castShadow = true;
rig.add(canopy);

const antenna = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.045, 1.1, 6), armMat);
antenna.position.set(0.55, 0.6, 1.2);
antenna.rotation.z = 0.32;
rig.add(antenna);

for (const sx of [-1, 1]) {
  const skid = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.16, 3.2), armMat);
  skid.position.set(sx * 1.05, -0.95, 0);
  rig.add(skid);
  for (const sz of [-1.1, 1.1]) {
    const leg = new THREE.Mesh(new THREE.BoxGeometry(0.14, 0.55, 0.14), armMat);
    leg.position.set(sx * 1.05, -0.62, sz);
    rig.add(leg);
  }
}

const gimbal = new THREE.Group();
gimbal.position.set(0, -0.55, -1.5);
const camBody = new THREE.Mesh(new THREE.BoxGeometry(0.72, 0.6, 0.66), bodyMat);
camBody.castShadow = true;
gimbal.add(camBody);
const lens = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.22, 0.26, 14), glassMat);
lens.rotation.x = Math.PI / 2;
lens.position.z = -0.4;
gimbal.add(lens);
rig.add(gimbal);

const tail = new THREE.Mesh(
  new THREE.SphereGeometry(0.14, 8, 6),
  new THREE.MeshStandardMaterial({ color: 0x111111, emissive: new THREE.Color(0xffffff), emissiveIntensity: 1.6 }),
);
tail.position.set(0, 0.1, 1.8);
rig.add(tail);

rig.position.set(drone.pos.x, GEAR, drone.pos.z);
scene.add(rig);

export const fpvCamera = new THREE.PerspectiveCamera(96, 16 / 9, 0.1, 600);

/* ---------- flight ---------- */
// The names the scene uses for "a hand is on the sticks": its own MANUAL, plus the two the
// board reports -- LOITER (sortie cancelled, waiting) and HOLD (cancelled and being flown).
const STICK_MODES = ['MANUAL', 'LOITER', 'HOLD'];

function steerTo(x, z, dt) {
  const dx = x - drone.pos.x;
  const dz = z - drone.pos.z;
  const d = Math.hypot(dx, dz) || 1;
  drone.vel.x = (dx / d) * SPEED;
  drone.vel.z = (dz / d) * SPEED;
  // The gimbal and the nose are on -Z, so the heading must point -Z at the
  // waypoint -- atan2(dx, dz) would fly it backwards.
  const want = Math.atan2(-dx, -dz);
  let delta = want - drone.yaw;
  while (delta > Math.PI) delta -= Math.PI * 2;
  while (delta < -Math.PI) delta += Math.PI * 2;
  drone.yaw += delta * Math.min(1, dt * 2.6);
  return d;
}

function descend(dt) {
  drone.vel.set(0, 0, 0);
  drone.pos.y = Math.max(0, drone.pos.y - 5 * dt);
  if (drone.pos.y <= 0.001) {
    drone.pos.y = 0;
    drone.mode = 'IDLE';
    drone.alert = '';
  }
}

export function takeoff() {
  if (drone.mode === 'IDLE') {
    drone.mode = 'MANUAL';
    drone.source = 'manual';
    drone.targetAlt = 8;
    drone.alert = '';
  }
}

export function land() {
  if (drone.mode !== 'IDLE') {
    drone.mode = 'LAND';
    drone.source = 'manual';
  }
}

export function hover() {
  if (drone.mode === 'PATROL' || drone.mode === 'RTL') {
    drone.mode = 'MANUAL';
    drone.source = 'manual';
    drone.alert = '';
  }
  drone.vel.set(0, 0, 0);
  input.fwd = input.side = input.climb = input.yaw = 0;
}

export function startPatrol() {
  if (drone.mode === 'IDLE') drone.pos.y = 0;
  drone.mode = 'PATROL';
  drone.source = 'manual';
  drone.targetAlt = PATROL_ALT;
  drone.alert = '';
}

export function returnHome() {
  drone.mode = 'RTL';
  drone.source = 'manual';
  drone.alert = '';
}

export function setManualAxes(axes) {
  Object.assign(input, axes);
  sendAxes(true);
  if (drone.mode === 'IDLE') return;
  // A stick pushed while the sortie is cancelled does not rename the cancellation: the
  // board answers those two moments with HOLD, and the panel has to be able to still say
  // "auto is off" while the operator is flying.
  if (!STICK_MODES.includes(drone.mode)) {
    drone.mode = 'MANUAL';
    drone.alert = '';
  }
  drone.source = 'manual';
}

// A board-driven command: the firmware decides the mode, the scene mirrors it.
const num = v => (v === undefined || v === '' || Number.isNaN(+v) ? NaN : +v);

// What the operator last asked for, and whether the board has caught up. The eight
// seconds is the ack window: long enough for the firmware's 20 ms mission job plus a
// climb to show up in a frame, short enough that a command the board will never
// take stops being reported as "pending" and starts being reported as a fault.
export const pilot = { want: '', at: 0, acked: false, sent: false, why: '' };

const PILOT_ACK_S = 8;

// How a command reaches the board. The transport is a UI concern -- it is the
// simulator's serial panel being typed into -- so the scene does not import it: the
// board link installs the writer at start-up and this module only says *what* to send.
// An uninstalled hook is not a silent failure: `pilot.why` keeps the reason.
let commandSender = null;
export function setCommandSender(fn) { commandSender = typeof fn === 'function' ? fn : null; }

// Verbs the firmware's own ground-command grammar accepts (firmware/drone/src/main.cpp,
// onGroundCommand). A verb that is not in this table is a scene-only move and is left
// out of the wire rather than sent as something the board would ignore.
const WIRE_VERB = {
  takeoff: 'takeoff', land: 'land', patrol: 'patrol',
  // The board's verbs for these two are `hold` and `rtl`; sending the page's word
  // for them is how 悬停 and 返航 came to be honoured locally and ignored in the air.
  // `hold` is also the cancel: the board stays in LOITER after it, so the sticks are
  // live and nothing resumes on its own -- see mission.cpp's mode watchdog.
  hover: 'hold', rtl: 'rtl',
  // The way back from a cancel. Without an entry here the board's `resume` verb had no
  // button anywhere, so cancelling a sortie was a one-way action.
  resume: 'resume',
};

// The stick channel. The console is a serial line, not a joystick: the firmware
// releases the RC channels when the axis lines stop arriving (500 ms), and a key
// that is merely *held* produces no further keystrokes, so the axes are re-issued
// on a timer while any of them is non-zero and handed back once with a zero line.
const RC_PERIOD_MS = 150;
// `sent` starts as null, not false: a stick nobody has touched is not a delivery
// that failed, and the panel says different things about the two.
export const stick = { sent: null, why: '', line: '' };
let rcNext = 0;
let rcZero = true;   // the board already has the channels back

const axis = v => Math.round((Number(v) || 0) * 100) / 100;

function sendAxes(force) {
  // Not installed yet is not a failed delivery: the board link installs the writer
  // when it loads, and a first animation frame that beats it must not leave a
  // refusal on the panel that nothing ever clears.
  if (!commandSender) return;
  const line = `rc=${axis(input.fwd)},${axis(input.side)},${axis(input.climb)},${axis(input.yaw)}`;
  const zero = line === 'rc=0,0,0,0';
  if (zero && rcZero && !force) return;
  const now = Date.now();
  if (!force && now < rcNext) return;
  rcNext = now + RC_PERIOD_MS;
  rcZero = zero;
  stick.line = line;
  const why = commandSender(line);
  stick.sent = !why;
  stick.why = why || '';
}

function sendToBoard(verb) {
  const wire = WIRE_VERB[verb];
  if (!wire) { pilot.sent = false; pilot.why = ''; return; }
  if (!commandSender) { pilot.sent = false; pilot.why = '没有接到仿真器的下行通道'; return; }
  const why = commandSender(wire);
  pilot.sent = !why;
  pilot.why = why || '';
}

const WANTED_MODES = {
  takeoff: ['CLIMB', 'TRANSIT', 'DWELL', 'PATROL'],
  patrol: ['CLIMB', 'TRANSIT', 'DWELL', 'PATROL'],
  // LOITER is the board's word for "cancelled, and staying cancelled"; HOLD is the same
  // moment with a hand on the stick. A cancel that the panel could not see acknowledged
  // would sit there looking like an unanswered button.
  hover: ['LOITER', 'HOLD', 'DWELL', 'TRANSIT'],
  resume: ['CLIMB', 'TRANSIT', 'DWELL', 'PATROL'],
  rtl: ['RTL', 'TRANSIT'],
  land: ['LAND', 'DESCEND', 'GROUND', 'LANDED', 'IDLE'],
};

function pilotExpired() {
  return !!pilot.want && !pilot.acked && (Date.now() - pilot.at) / 1000 >= PILOT_ACK_S;
}

function pilotPending() {
  return !!pilot.want && !pilot.acked && !pilotExpired();
}

function pilotAgrees(mode) {
  if (!pilotPending()) return true;
  return (WANTED_MODES[pilot.want] || []).includes(mode);
}

// A request the board never answered stops being a request. Without this, `pilot.want`
// outlived its own window and the next board frame took the "nothing is pending" branch
// -- which is permissive by design -- so a GROUND frame was read as *agreement* and the
// panel announced 「板子已执行 takeoff」 for an aircraft that never left the pad. The
// timeout is a fault, and it is reported as one, once.
function expirePilot() {
  if (!pilotExpired()) return;
  const verb = pilot.want;
  pilot.want = '';
  // Say what the board is actually on. "还在 GROUND" is only half a sentence: the frame
  // carries `safe=` and its own `why=`, and with the safety still thrown that is the
  // thing the operator has to do next.
  const stuck = drone.boardMode || '未上报';
  const reason = pilot.why || ('板子还在 ' + stuck
    + (stuck === 'GROUND' && drone.boardSafe === 1
      ? `（保险开着${drone.boardWhy ? '：' + drone.boardWhy : ''}，先按「解锁」再起飞）` : ''));
  drone.alert = `板子 ${PILOT_ACK_S} 秒内没有认「${verb}」：${reason}`;
  pilot.why = '';
}

export function pilotCommand(verb) {
  if (verb === 'takeoff') takeoff();
  else if (verb === 'patrol') startPatrol();
  else if (verb === 'hover') hover();
  else if (verb === 'rtl') returnHome();
  else if (verb === 'land') land();
  // Resuming is not something the page can draw: the board stopped the plan and the
  // scene has been sitting wherever the operator left it. Without this branch the verb
  // fell out of the chain below and the button looked pressed and did nothing -- the
  // exact shape 悬停 and 返航 used to have.
  else if (verb === 'resume') { /* wire only; the frame will move the scene */ }
  else return;
  pilot.want = verb;
  pilot.at = Date.now();
  pilot.acked = false;
  sendToBoard(verb);
  if (pilot.sent) drone.alert = `已把 ${verb} 发给板子，等它确认`;
  else if (pilot.why) drone.alert = `本地已 ${verb}，但没送到板子：${pilot.why}`;
}

export function applyBoardCommand(cmd) {
  const mode = String(cmd.mode || '').toUpperCase();
  drone.source = 'board';
  // 1 = the board is flying the sticks right now, 0 = it has the channels back,
  // null = nothing has ever said. Those three have to stay distinguishable: "not flying
  // them" and "cannot tell" look identical from the pad. And a frame that does not carry
  // the key at all must not erase an answer the board already gave -- measured once with a
  // phone axis line in the air: the frame read `ovr=1` while the scene's mirror of it had
  // gone back to null, because the previous frame happened not to carry the field.
  if (cmd.ovr !== undefined && cmd.ovr !== '') drone.ovr = +cmd.ovr ? 1 : 0;
  drone.rcWhy = String(cmd.rcwhy || '');
  if (cmd.safe !== undefined && cmd.safe !== '') drone.boardSafe = +cmd.safe ? 1 : 0;
  drone.boardWhy = String(cmd.why || '');

  const alt = num(cmd.agl !== undefined && cmd.agl !== '' ? cmd.agl : cmd.alt);
  if (alt >= 0) drone.targetAlt = Math.min(45, alt);
  // The scene's own altitude is what the panel calls 高度, and in the air the board's
  // AGL is the only honest value for it: a scene that kept its own number would show
  // the aircraft clamped to 8 m while the board's frame said 26.
  const agl = num(cmd.agl);
  if (agl >= 0) drone.boardAgl = agl;

  const wp = num(cmd.wp);
  // The firmware counts the pad as waypoint 1, this scene's route starts at the
  // first building, so clamp instead of wrapping: a wrap would teleport the
  // model to the pad on the last leg of the patrol.
  if (wp >= 1) drone.wp = Math.min(ROUTE.length - 1, Math.round(wp) - 1);

  const shots = num(cmd.shots);
  if (shots >= 0) drone.photos = Math.round(shots);

  const batt = num(cmd.batt);
  if (batt >= 0) drone.battery = Math.min(100, batt);

  const rssi = num(cmd.rssi);
  if (rssi <= 0) drone.rssi = Math.round(rssi);

  // The phase names are the firmware's, not invented here: TRANSIT and DWELL are
  // both "on patrol" to a viewer, DESCEND is the last leg of a landing, and
  // GROUND / LANDED are two different kinds of not-flying that render the same.
  //
  // A command the operator issued is honoured by the scene while the board catches up
  // with it. `pilotCommand` now types the verb into the board's console, so the board
  // does change mode -- but the frame that command was issued from is still in flight,
  // and taking it as the final word would drop the scene to GROUND the instant the
  // stick was pushed. Within the window the board's *measurements* above are still
  // taken; only its mode is held back.
  drone.boardMode = mode;
  if (!pilotAgrees(mode)) {
    // Only a request that has run out of time is a fault. While it is still inside the
    // window, the frame in hand was produced *before* the command was typed into the
    // board's console -- announcing 未被执行 off that frame made every successful
    // takeoff lead with a rejection for its first 200 ms.
    if (pilotExpired()) drone.alert = `板子仍在 ${mode || '未上报'}，本地指令 ${pilot.want} 未被执行`;
    return;
  }

  const flying = mode === 'CLIMB' || mode === 'TRANSIT' || mode === 'DWELL' || mode === 'PATROL';
  if (flying) {
    if (drone.mode === 'IDLE') drone.pos.y = Math.max(drone.pos.y, 0.02);
    drone.mode = 'PATROL';
    drone.alert = '';
  } else if (mode === 'RTL') {
    drone.mode = 'RTL';
  } else if (mode === 'LAND' || mode === 'DESCEND') {
    drone.mode = 'LAND';
  } else if (mode === 'LOITER' || mode === 'HOLD') {
    // The cancelled state has to look cancelled. Without this branch the scene kept its
    // old PATROL mode and steered on to the next waypoint while the board sat in LOITER --
    // so the one picture the operator watches contradicted the button he had just pressed.
    // These two are flown by the sticks, which is the same kinematics as MANUAL.
    drone.mode = mode;
    drone.vel.set(0, 0, 0);
    drone.alert = '';
  } else if (mode === 'GROUND' || mode === 'LANDED' || mode === 'IDLE') {
    // A ground frame from the board ends a local flight only when the operator is not
    // mid-command. Otherwise the acknowledgement of the takeoff that is still climbing
    // to us would ground the aircraft that just lifted.
    if (!pilotPending()) drone.mode = 'IDLE';
  }
  if (pilot.want && pilotPending() && pilotAgrees(mode)) {
    pilot.acked = true;
    drone.alert = `板子已执行 ${pilot.want}`;
  }
}

export function releaseBoardFlight() {
  drone.source = 'manual';
  drone.alert = '';
  pilot.want = '';
  pilot.acked = false;
  pilot.why = '';
  if (drone.mode === 'IDLE') drone.targetAlt = 0;
}

function integrate(dt) {
  const dy = drone.targetAlt - drone.pos.y;
  drone.vel.y = Math.max(-6, Math.min(6, dy * 0.9));
  drone.pos.addScaledVector(drone.vel, dt);
  drone.pos.x = Math.max(-95, Math.min(95, drone.pos.x));
  drone.pos.z = Math.max(-95, Math.min(95, drone.pos.z));
  if (drone.pos.y < 0) { drone.pos.y = 0; drone.vel.y = 0; }
  if (drone.pos.y > 45) drone.pos.y = 45;
}

export function updateDrone(dt, time) {
  const d = drone;
  // Checked here rather than in `applyBoardCommand`: that only runs when a frame
  // arrives, so a board that goes quiet would leave a pending command looking pending
  // for ever instead of turning into the fault it is.
  expirePilot();
  // The sticks are a held thing, not an event: keep the line coming while a key or
  // a pad button is down, and let the released one go out on its own edge.
  sendAxes(false);

  if (d.mode === 'IDLE') {
    // "On the ground" has to mean on the ground. The board reports LANDED, and a
    // model that froze wherever it last was commanded reads as a stuck sim
    // rather than as a finished flight.
    if (d.pos.y > 0) {
      d.pos.y = Math.max(0, d.pos.y - 3.0 * dt);
      d.vel.set(0, 0, 0);
      rig.position.set(d.pos.x, d.pos.y + GEAR, d.pos.z);
    }
    props.forEach(p => { p.rotor.rotation.y += p.dir * dt * 1.2; });
    rig.position.set(d.pos.x, d.pos.y + GEAR, d.pos.z);
    rig.rotation.set(0, d.yaw, 0);
    gimbal.rotation.x = -0.25 + Math.sin(time * 0.6) * 0.05;
    updateFpv();
    return;
  }

  d.flightSeconds += dt;
  if (d.source !== 'board') {
    d.battery = Math.max(0, d.battery - dt * (0.55 + Math.hypot(d.vel.x, d.vel.z) * 0.035));
  }

  if (d.mode === 'PATROL') {
    const w = ROUTE[d.wp];
    const dist = steerTo(w.x, w.z, dt);
    if (dist < 3) {
      d.vel.x *= 0.6;
      d.vel.z *= 0.6;
      // A board-driven drone mirrors the firmware's own waypoint counter.
      if (d.source !== 'board') {
        d.hover += dt;
        if (d.hover > 1.6) {
          d.hover = 0;
          d.photos++;
          d.wp = (d.wp + 1) % ROUTE.length;
        }
      }
    }
  } else if (d.mode === 'RTL') {
    const dist = steerTo(GROUND_STATION.x, GROUND_STATION.z, dt);
    if (dist < 2.5 && d.source !== 'board') {
      d.vel.set(0, 0, 0);
      d.targetAlt = 0;
      if (d.pos.y < 0.6) d.mode = 'LAND';
    }
  } else if (d.mode === 'LAND') {
    descend(dt);
  } else {
    const cos = Math.cos(d.yaw), sin = Math.sin(d.yaw);
    d.vel.x = input.side * SPEED * cos + input.fwd * SPEED * sin;
    d.vel.z = -input.side * SPEED * sin + input.fwd * SPEED * cos;
    d.yaw += input.yaw * dt * 1.5;
    // The climb stick moves the target, so releasing it holds the altitude that
    // was reached -- and a takeoff command is not overwritten on frame one.
    d.targetAlt = Math.max(0, Math.min(45, d.targetAlt + input.climb * 9 * dt));
  }

  if (d.mode !== 'LAND') integrate(dt);

  const link = Math.hypot(d.pos.x - GROUND_STATION.x, d.pos.z - GROUND_STATION.z);
  if (d.source !== 'board') d.rssi = Math.round(-44 - link * 0.46);
  if (link > LINK_LIMIT && STICK_MODES.includes(d.mode)) {
    d.mode = 'RTL';
    d.alert = '链路丢失，自动返航';
  }
  if (d.battery <= 15 && d.mode === 'PATROL') {
    d.mode = 'RTL';
    d.alert = '低电量 15%，自动返航';
  }
  if (d.battery <= 2 && d.mode !== 'LAND') {
    d.mode = 'LAND';
    d.alert = '电量耗尽，就地降落';
  }

  const spin = d.mode === 'MANUAL' && Math.abs(input.climb) < 0.05 ? 26 : 44;
  props.forEach(p => { p.rotor.rotation.y += p.dir * dt * spin; });

  rig.position.set(d.pos.x, d.pos.y + GEAR, d.pos.z);
  // Nose on -Z: forward speed dips the nose (+Z rotation lifts it), and rolling
  // right means the +X arm drops.
  rig.rotation.set(d.vel.z * 0.022, d.yaw, -d.vel.x * 0.022);
  gimbal.rotation.x = -0.5 + Math.sin(time * 0.8) * 0.06;
  updateFpv();
}

const _fpvPos = new THREE.Vector3();
const _fpvLook = new THREE.Vector3();

// The FPV view follows the gimbal, not the airframe: the lens keeps the horizon
// level while the body banks.
function updateFpv() {
  rig.updateMatrixWorld(true);
  gimbal.updateMatrixWorld();
  // The lens cylinder is spun 90 degrees about X to lie along Z, so its own -Z
  // points at the sky; the gimbal frame is the one that carries the view axis.
  _fpvPos.set(0, 0, -0.62).applyMatrix4(gimbal.matrixWorld);
  _fpvLook.set(0, 0, -14).applyMatrix4(gimbal.matrixWorld);
  fpvCamera.position.copy(_fpvPos);
  fpvCamera.up.set(0, 1, 0);
  fpvCamera.lookAt(_fpvLook);
}
