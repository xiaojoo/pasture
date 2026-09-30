// The show fleet in the 3D scene: one airframe per station, each with its own
// WS2812 under the arm, flying the path `show/core.js` baked for it.
//
// The scene is the ground station's picture of the show, not the authority for it.
// Every position here is read out of the compiled plan at the show clock's time, so
// what the operator sees and what an aircraft would be told are the same numbers --
// and when the clock stops, the fleet stops where the plan says it is.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { showCompilePlan, showPathLane, showLane, showDuration } from './core.js';

// The surveyed launch point. The plan's local NED frame hangs off this, so a change
// here moves the whole show, not individual formations.
export const SHOW_ORIGIN = { x: 0, z: -96 };

const ARM = 0.34;          // metres, a 250-class light-show airframe
const LED_SIZE = 0.5;

let group = null;
let craft = [];
let plan = null;
let compiled = null;
let clock = 0;
let flying = false;
let wind = 0;

function buildCraft() {
  const g = new THREE.Group();

  const body = new THREE.Mesh(
    new THREE.BoxGeometry(0.26, 0.1, 0.26),
    new THREE.MeshStandardMaterial({ color: 0x1b2a22, roughness: .8, metalness: .1 }));
  g.add(body);

  const armMat = new THREE.MeshStandardMaterial({ color: 0x2c3b33, roughness: .9 });
  for (const [sx, sz] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    const arm = new THREE.Mesh(new THREE.BoxGeometry(ARM * 1.4, 0.03, 0.05), armMat);
    arm.rotation.y = Math.PI / 4;
    arm.position.set(sx * ARM * 0.5, 0, sz * ARM * 0.5);
    g.add(arm);
    const rotor = new THREE.Mesh(
      new THREE.TorusGeometry(0.11, 0.012, 5, 14),
      new THREE.MeshBasicMaterial({ color: 0x6ee7a0, transparent: true, opacity: .22 }));
    rotor.rotation.x = Math.PI / 2;
    rotor.position.set(sx * ARM, 0.02, sz * ARM);
    g.add(rotor);
  }

  // The light the audience actually sees: one emissant bead under the arm, plus a
  // small point light so the formation reads as volume at night rather than as
  // dots on a flat plane.
  const led = new THREE.Mesh(
    new THREE.SphereGeometry(LED_SIZE * 0.5, 8, 8),
    new THREE.MeshBasicMaterial({ color: 0xffffff }));
  led.position.y = -0.12;
  g.add(led);
  const halo = new THREE.PointLight(0xffffff, 0.9, 9, 2);
  halo.position.y = -0.16;
  g.add(halo);

  g.userData = { led, halo };
  return g;
}

export function showFleetCount() {
  return craft.length;
}

// (Re)build the fleet for a plan. The meshes are pooled up to the count so changing
// the aircraft number does not rebuild the whole group every keystroke.
export function showLoad(p) {
  plan = p;
  compiled = showCompilePlan(p);
  if (!group) {
    group = new THREE.Group();
    scene.add(group);
  }
  const want = compiled ? p.drones : 0;
  while (craft.length < want) {
    const c = buildCraft();
    group.add(c);
    craft.push(c);
  }
  for (let i = 0; i < craft.length; ++i) craft[i].visible = i < want;
  clock = 0;
  showPlace(0);
  return !!compiled;
}

// Where every aircraft sits at show time `t`, and what colour it should be.
function showPlace(t) {
  if (!plan || !compiled) return;
  const acts = plan.acts;
  let acc = 0;
  let idx = 0;
  for (let i = 0; i < acts.length; ++i) {
    const span = (i === 0 ? 0 : acts[i].move) + acts[i].hold;
    if (t < acc + span || i === acts.length - 1) { idx = i; break; }
    acc += span;
  }
  const act = acts[idx];
  const moveT = idx === 0 ? act.move : act.move;
  const local = t - acc;
  const u = idx === 0 ? 1 : Math.min(1, Math.max(0, local / (moveT || 1)));
  const held = local > moveT;

  for (let k = 0; k < craft.length && k < plan.drones; ++k) {
    const to = compiled.pos[idx][k];
    const from = idx === 0 ? to : compiled.pos[idx - 1][k];
    const p = held ? to : showPathLane(from, to, u, showLane(to, plan.separation));
    const c = craft[k];
    // NED to the scene: north is -z, east is +x, up is +y. Same convention the
    // inspection aircraft uses, so the two scenes can share one camera.
    c.position.set(SHOW_ORIGIN.x + p.e, -p.d, SHOW_ORIGIN.z - p.n);
    if (!held) {
      const nx = showPathLane(from, to, Math.min(1, u + 0.02), showLane(to, plan.separation));
      const dn = nx.n - p.n, de = nx.e - p.e;
      if (dn || de) c.rotation.y = Math.atan2(-dn, -de);
    }
    const col = new THREE.Color(act.colour);
    c.userData.led.material.color.copy(col);
    c.userData.halo.color.copy(col);
    // A formation that has not arrived yet burns dimmer: the audience reads the
    // move, and the operator can see which aircraft the clock says is still in
    // transition without a second instrument.
    const level = held ? 1 : 0.45;
    c.userData.halo.intensity = 0.9 * level;
  }
}

export function showUpdate(dt) {
  if (!flying) return;
  clock = Math.min(showDuration(plan), clock + dt);
  showPlace(clock);
  if (clock >= showDuration(plan)) flying = false;
}

export function showStart() {
  if (!plan || !compiled) return false;
  clock = 0;
  flying = true;
  return true;
}

export function showStop() {
  flying = false;
}

export function showPreview(t) {
  clock = Math.max(0, Math.min(showDuration(plan || { acts: [] }), t));
  flying = false;
  showPlace(clock);
}

export function showState() {
  const acts = plan ? plan.acts : [];
  let acc = 0, idx = 0;
  for (let i = 0; i < acts.length; ++i) {
    const span = acts[i].move + acts[i].hold;
    if (clock < acc + span) { idx = i; break; }
    acc += span;
    idx = i;
  }
  const act = acts[idx];
  const local = clock - acc;
  const u = act ? Math.min(1, Math.max(0, local / (act.move || 1))) : 0;
  return {
    flying,
    t: clock,
    total: plan ? showDuration(plan) : 0,
    act: idx + 1,
    actCount: acts.length,
    phase: !act ? '空' : u < 1 && idx > 0 && local < act.move ? '走位' : '保持',
    wind,
    craft: craft.filter(c => c.visible).length,
  };
}

export function showSetWind(v) {
  wind = Math.max(0, Math.min(12, Number(v) || 0));
}

export function showDispose() {
  if (group) scene.remove(group);
  group = null;
  craft = [];
  plan = null;
  compiled = null;
  flying = false;
  clock = 0;
}
