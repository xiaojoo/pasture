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
  if (drone.mode === 'IDLE') return;
  if (drone.mode !== 'MANUAL') {
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
export const pilot = { want: '', at: 0, acked: false };

const PILOT_ACK_S = 8;

const WANTED_MODES = {
  takeoff: ['CLIMB', 'TRANSIT', 'DWELL', 'PATROL'],
  patrol: ['CLIMB', 'TRANSIT', 'DWELL', 'PATROL'],
  hover: ['HOLD', 'DWELL', 'TRANSIT'],
  rtl: ['RTL', 'TRANSIT'],
  land: ['LAND', 'DESCEND', 'GROUND', 'LANDED', 'IDLE'],
};

function pilotPending() {
  return !!pilot.want && !pilot.acked && (Date.now() - pilot.at) / 1000 < PILOT_ACK_S;
}

function pilotAgrees(mode) {
  if (!pilotPending()) return true;
  return (WANTED_MODES[pilot.want] || []).includes(mode);
}

export function pilotCommand(verb) {
  if (verb === 'takeoff') takeoff();
  else if (verb === 'patrol') startPatrol();
  else if (verb === 'hover') hover();
  else if (verb === 'rtl') returnHome();
  else if (verb === 'land') land();
  else return;
  pilot.want = verb;
  pilot.at = Date.now();
  pilot.acked = false;
}

export function applyBoardCommand(cmd) {
  const mode = String(cmd.mode || '').toUpperCase();
  drone.source = 'board';

  const alt = num(cmd.agl !== undefined && cmd.agl !== '' ? cmd.agl : cmd.alt);
  if (alt >= 0) drone.targetAlt = Math.min(45, alt);

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
  // A command the operator issued is honoured by the scene and the board is expected
  // to catch up. While it has not, the board's mission state is not allowed to
  // overwrite the aircraft out from under the stick -- the browser has no downlink
  // into the compiled firmware, so a takeover that stomped every local command would
  // not be the board flying the ranch, it would be the board refusing to be flown.
  // The measurements above this point are still taken from the board either way.
  if (!pilotAgrees(mode)) {
    drone.alert = `板子仍在 ${mode || '未上报'}，本地指令 ${pilot.want} 未被执行`;
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
  } else if (mode === 'GROUND' || mode === 'LANDED' || mode === 'IDLE') {
    drone.mode = 'IDLE';
  }
  if (pilot.want && pilotAgrees(mode)) {
    pilot.acked = true;
    drone.alert = `板子已执行 ${pilot.want}`;
  }
}

export function releaseBoardFlight() {
  drone.source = 'manual';
  drone.alert = '';
  pilot.want = '';
  pilot.acked = false;
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
  if (link > LINK_LIMIT && d.mode === 'MANUAL') {
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
