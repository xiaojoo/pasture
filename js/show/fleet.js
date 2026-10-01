// The show fleet in the 3D scene: one airframe per station, each with its own
// WS2812 under the arm, flying the path `show/core.js` baked for it.
//
// The scene is the ground station's picture of the show, not the authority for it.
// Every position here is read out of the compiled plan at the show clock's time, so
// what the operator sees and what an aircraft would be told are the same numbers --
// and when the clock stops, the fleet stops where the plan says it is.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { controls } from '../core/camera.js';
import { showCompilePlan, showPathLane, showLane, showDuration, showWorstPair } from './core.js';
import { buildCraft, craftSummary, wingspan } from './craft.js';

// The surveyed launch point. The plan's local NED frame hangs off this, so a change
// here moves the whole show, not individual formations.
export const SHOW_ORIGIN = { x: 0, z: -96 };

// Nose-in to the people watching: the show flies north-east-up from a pad at the origin
// and the audience stands on the origin side of it, so a held formation faces +z.
const SHOW_HEADING = Math.PI;


// What a close look needs: the orbit distance the scene allows by default (18 m) is
// three airframes away, so focusing pins a much nearer limit and restores it after.
// Both are multiplied by the display scale below: at ×6 the model is six metres across
// on screen, and a camera 0.85 m away would be *inside* it.
const FOCUS_MIN = 0.35;
const DEFAULT_MIN = 18;

// The airframe is 0.67 m across and a show is flown ~170 m from where the operator
// stands: measured, that projects to 5.5 × 1.0 pixels, which is why 「看不到」 is the
// correct complaint about a physically honest model. So the model is drawn enlarged --
// but the enlargement has a ceiling, and it is not taste: an aircraft that is drawn
// wider than the gap next to it reaches across into its neighbour, and the field stops
// reading as a formation and starts reading as a plate of crossed arms (measured at 6×
// on an 18 m ring: 4.32 m drawn, 4.70 m gap).
// The auto value therefore comes off the show's own tightest pair, and the ± buttons
// override it until the next load.
let displayScale = 1;
let displayAuto = true;
let tightest = 0;
const AUTO_FRACTION = 0.55;      // a drawn airframe may cover at most this of its gap
const AUTO_MAX = 16;

function craftSpan() {
  const c = craft.find(x => x.visible) || craft[0];
  return c ? wingspan(c) : 0.67;
}
export function showDisplayScale() { return displayScale; }
export function showDisplayInfo() {
  return { scale: displayScale, auto: displayAuto, tightest, suggested: autoScale(),
    ceiling: scaleCeiling() };
}
function autoScale() {
  const span = craftSpan();
  // No measurement means draw it the size it is. This used to return AUTO_MAX for the
  // unknown case, and "unknown" is the normal state of a one-act show: `showWorstPair`
  // only measures transitions between consecutive acts, so a single act has nothing to
  // measure, and 49 aircraft on a 4.51 m ring came out drawn 10.73 m wide with 217 pairs
  // of bodies reaching through each other. Not knowing is a reason to be small.
  if (!tightest || !span) return 1;
  return Math.max(1, Math.min(AUTO_MAX, AUTO_FRACTION * tightest / span));
}

// The tightest two stations in any one act. The show's worst pair measures the
// transitions between acts and says nothing at all when there is only one act, but the
// picture has to live inside the *formation* whether or not anything moves.
function stationGap(pos) {
  let worst = Infinity;
  for (const act of pos) {
    for (let a = 0; a < act.length; ++a) for (let b = a + 1; b < act.length; ++b) {
      const d = Math.hypot(act[a].n - act[b].n, act[a].e - act[b].e, act[a].d - act[b].d);
      if (d < worst) worst = d;
    }
  }
  return worst;
}

// The most the manual knob may do: one airframe drawn exactly as wide as the tightest gap
// beside it. Past that the field is not a formation any more, and the ± buttons used to be
// able to walk straight there (×16 on a 4.51 m ring).
function scaleCeiling() {
  const span = craftSpan();
  if (!tightest || !span) return 1;
  return Math.max(1, Math.min(AUTO_MAX, Math.floor(tightest / span * 10) / 10));
}
function applyScale() {
  for (const c of craft) c.scale.setScalar(displayScale);
  if (focus >= 0) showFocus(focus, true);    // re-seat the close-up for the new size
}
export function setShowDisplayScale(k) {
  displayAuto = false;
  // Clamped by what the field can take, not by the widget's top stop.
  displayScale = Math.max(1, Math.min(scaleCeiling(), Math.round(Number(k) * 10) / 10 || 1));
  applyScale();
  return displayScale;
}
export function setShowDisplayAuto(on) {
  displayAuto = on !== false;
  if (displayAuto) displayScale = Math.round(autoScale() * 10) / 10;
  applyScale();
  return displayScale;
}

let group = null;
let craft = [];
let plan = null;
let compiled = null;
let clock = 0;
let flying = false;
// True while the clock is only standing on the first formation for editing, i.e. nobody
// has asked for the show to run yet. It is what keeps 待命 honest after the park above.
let parked = false;
let wind = 0;

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
    c.scale.setScalar(displayScale);
    group.add(c);
    craft.push(c);
  }
  for (let i = 0; i < craft.length; ++i) craft[i].visible = i < want;
  // The gap the enlargement has to live inside: the tightest pair *anywhere in the
  // baked show*, which is the same number the verdict line quotes -- a cap set by the
  // station spacing alone would still let two aircraft cross arms mid-transition.
  tightest = 0;
  if (compiled) {
    const w = showWorstPair(compiled.pos, compiled.pos.length, p.drones, p.separation);
    const over = Number.isFinite(w.clearance) ? w.clearance : Infinity;
    const held = stationGap(compiled.pos);
    const worst = Math.min(over, held);
    tightest = Number.isFinite(worst) ? worst : 0;
  }
  if (displayAuto) setShowDisplayAuto(true);
  else applyScale();
  // Park on the first formation, not on the pad column. Act one's 走位 starts with every
  // aircraft stacked at the origin -- that is what the pack does -- so at t=0 the whole
  // show projects to a single point and the screen reads as 「太小了」 with nothing on it.
  // The operator editing a shape has to see the shape; 起飞 still runs it from the column.
  clock = p.acts.length ? p.acts[0].move : 0;
  parked = true;
  showPlace(clock);
  if (focus >= want) showFocus(-1);   // the station counted away under the camera
  return !!compiled;
}

// Where every aircraft sits at show time `t`, and what colour it should be.
function showPlace(t) {
  if (!plan || !compiled) return;
  const acts = plan.acts;
  let acc = 0;
  let idx = 0;
  for (let i = 0; i < acts.length; ++i) {
    // Every act has its 走位 in the clock, the first one included: the aircraft's own
    // show.cpp starts act 1 from the pad column and spreads out over `acts[0].move_s`,
    // and the duration and the fuel margin both count it. Skipping it here made the
    // rehearsal finish `acts[0].move` seconds early against what the pack flies.
    const span = acts[i].move + acts[i].hold;
    if (t < acc + span || i === acts.length - 1) { idx = i; break; }
    acc += span;
  }
  const act = acts[idx];
  const moveT = act.move;
  const local = t - acc;
  const u = Math.min(1, Math.max(0, local / (moveT || 1)));
  const held = local > moveT;

  for (let k = 0; k < craft.length && k < plan.drones; ++k) {
    const to = compiled.pos[idx][k];
    // Act one starts above the origin, at the formation's own height -- the same
    // starting point `actEnds()` gives the airframe, so the rehearsal and the pack
    // agree about where a show begins.
    const from = idx === 0 ? { n: 0, e: 0, d: to.d } : compiled.pos[idx - 1][k];
    const p = held ? to : showPathLane(from, to, u, showLane(to, plan.separation));
    const c = craft[k];
    // NED to the scene: north is -z, east is +x, up is +y. Same convention the
    // inspection aircraft uses, so the two scenes can share one camera.
    c.position.set(SHOW_ORIGIN.x + p.e, -p.d, SHOW_ORIGIN.z - p.n);
    if (!held) {
      const nx = showPathLane(from, to, Math.min(1, u + 0.02), showLane(to, plan.separation));
      const dn = nx.n - p.n, de = nx.e - p.e;
      if (dn || de) c.rotation.y = Math.atan2(-dn, -de);
    } else {
      // Once it is on station every aircraft turns the same way. Without this each one
      // froze at whatever its own arrival direction happened to be -- measured, 24
      // aircraft holding 24 different headings across the full 6.28 rad, which is what
      // made a clean ring read as a mess. Eased rather than set so the nose does not
      // snap at the moment of arrival.
      const d = ((SHOW_HEADING - c.rotation.y + Math.PI * 3) % (Math.PI * 2)) - Math.PI;
      c.rotation.y += d * 0.15;
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

// Frame the whole fleet: the show flies 96 m north of the ranch, so opening the
// choreography page with the camera where the barn was left it looking at a roof while
// the formation drew itself off screen. Distance comes off the loaded show's own radius
// and the camera's own field of view, not a constant.
export function showFrame() {
  if (!controls || !controls.object || !compiled) return null;
  // Framing the whole show and following one aircraft are two different cameras; asking
  // for the first has to let go of the second rather than fight it on the next frame.
  if (focus >= 0) showFocus(-1);
  // Framed off the show's own cloud, not off where the aircraft happen to be this second:
  // at t=0 every one of them is still in the pad column, and "fit the fleet" would put
  // the camera 18 m from a single point and call the show out of frame.
  const pts = [];
  for (const act of compiled.pos) for (const p of act) {
    pts.push(new THREE.Vector3(SHOW_ORIGIN.x + p.e, -p.d, SHOW_ORIGIN.z - p.n));
  }
  if (!pts.length) return null;
  const c = new THREE.Vector3();
  for (const p of pts) c.add(p);
  c.divideScalar(pts.length);
  let r = 1;
  for (const p of pts) r = Math.max(r, p.distanceTo(c));
  const half = Math.tan(controls.object.fov * Math.PI / 360);
  const h = innerHeight || 1;
  const W = innerWidth || 1;
  // What the operator can actually see. The desk is an overlay at the bottom: the show's
  // scale is fitted to the **window**, and the panel costs the picture position, not size.
  // Fitting to the strip of sky the panel leaves is what made 24 aircraft render 251x73 px.
  const dock = parseFloat(getComputedStyle(document.documentElement)
    .getPropertyValue('--show-dock')) || 0;
  // The camera row is in the header now, so the band the picture may occupy has a ceiling as
  // well as a floor. It is only a ceiling when it is actually anchored there -- the first
  // 40 px -- because below 1240 px the row goes back to the bottom edge (bottom.css), where
  // treating its box as a ceiling would push the show off the screen. `getComputedStyle`
  // cannot tell the two apart: for an absolutely positioned element with `top:auto` it
  // reports the *used* pixel value, not the word auto.
  const rowEl = document.querySelector('.bottom');
  const rowBox = rowEl ? rowEl.getBoundingClientRect() : null;
  const topInset = rowBox && rowBox.height > 0 && rowBox.top <= 40
    ? Math.round(rowBox.bottom) : 0;
  const availH = Math.max(200, h - 24);
  const availW = Math.max(200, W - 24);
  const halfH = half * (W / h);
  const dir = new THREE.Vector3(0.3, 0.45, 1).normalize();
  const cam = controls.object;
  const boxOf = () => {
    cam.updateMatrixWorld(true);
    return pts.reduce((a, p) => {
      const q = p.clone().project(cam);
      return { l: Math.min(a.l, (1 + q.x) / 2 * W), r: Math.max(a.r, (1 + q.x) / 2 * W),
        t: Math.min(a.t, (1 - q.y) / 2 * h), b: Math.max(a.b, (1 - q.y) / 2 * h) };
    }, { l: 1e9, r: -1e9, t: 1e9, b: -1e9 });
  };
  // Fit by the *projected box*, not by the cloud's radius: the radius is measured from the
  // centroid of every act's stations (a ring and a grid are not concentric about it), and
  // the box is what actually has to land inside the window.
  let dist = Math.max(DEFAULT_MIN,
    Math.max(r / half * (h / availH), r / halfH * (W / availW)) * 1.15);
  let box = null;
  for (let round = 0; round < 4; ++round) {
    cam.clearViewOffset();          // measure the unshifted view, then decide the shift
    controls.target.copy(c);
    cam.position.copy(c).addScaledVector(dir, dist);
    box = boxOf();
    const over = Math.max((box.r - box.l) / availW, (box.b - box.t) / availH);
    if (over <= 1) break;
    dist *= over * 1.02;
  }
  // Slide the picture up out of the panel with a **lens shift**, not by moving the camera.
  // Translating the camera 16 m to lift the cloud by 240 px turned a 707x264 px ring into
  // 707x95 px: the camera ended up looking *up* at the formation, so it was seen edge-on.
  // A view offset moves the frustum window instead -- same camera, same angle, same size
  // (measured: 694x301 px with the desk open and the same 694x301 with it closed).
  if (box) {
    const mid = (topInset + (h - dock)) / 2;
    const shift = Math.round((box.t + box.b) / 2 - mid);
    if (shift !== 0) cam.setViewOffset(W, h, 0, shift, W, h);
  }
  box = boxOf();
  return { radius: +r.toFixed(1), dist: +dist.toFixed(1), dockPx: dock, topInset,
    availW, availH, pxW: Math.round(box.r - box.l), pxH: Math.round(box.b - box.t),
    left: Math.round(box.l), right: Math.round(box.r), top: Math.round(box.t),
    bottom: Math.round(box.b), deskTop: Math.round(h - dock),
    centre: controls.target.toArray().map(v => +v.toFixed(1)) };
}

export function showUpdate(dt) {
  // The props turn whenever the show is running *or* being looked at: a frozen
  // propeller is the difference between an aircraft on a stand and one paused.
  const spin = flying || focus >= 0 ? 1 : 0.12;
  for (const c of craft) {
    for (const p of (c.userData.props || [])) p.rotation.y += p.userData.dir * dt * 26 * spin;
  }
  if (focus >= 0) followFocus();
  if (flying) clock = Math.min(showDuration(plan), clock + dt);
  // Placed on every frame, flying or not: the held heading is eased, and a view that
  // stopped being updated the moment the show paused would freeze with noses half-turned.
  if (plan && compiled) showPlace(clock);
  // Guarded, because on a first visit with no saved programme `plan` is null and this line
  // is reached anyway: it threw inside the very first frame, one step past the
  // requestAnimationFrame at the top of animate(), so the loop kept spinning but never
  // rendered -- and the modules imported after it in init.js, the board link among them,
  // never ran at all. Every button then reported the board ignoring it, which was true.
  if (plan && clock >= showDuration(plan)) flying = false;
}

// --- looking at one airframe ------------------------------------------------
//
// The scene's orbit limit is 18 m, which is three airframes away: "zoom in and see a
// drone" cannot be done inside that. Focusing drops the limit while it lasts and
// restores it afterwards, and it moves the camera *with* the aircraft -- following the
// target alone would leave the camera behind and the aircraft would fly out of the
// frame it is supposed to be inspecting.
let focus = -1;
const focusAt = new THREE.Vector3();
const focusPrev = new THREE.Vector3();
let focusHave = false;

export function showFocus(i, reseat) {
  const k = Number.isInteger(i) ? i : -1;
  const want = k >= 0 && k < craft.length && craft[k].visible ? k : -1;
  if (want === focus && !reseat) return focus;
  focus = want;
  focusHave = false;
  if (controls) {
    // The close look is sized off the *drawn* model, not the aircraft: six metres of
    // drawing with a camera 0.85 m from it is a screen full of one motor.
    controls.minDistance = focus >= 0 ? FOCUS_MIN * displayScale : DEFAULT_MIN;
    if (focus >= 0 && craft[focus]) {
      craft[focus].getWorldPosition(focusAt);
      if (controls.object) {
        controls.object.position.set(focusAt.x + 0.85 * displayScale,
          focusAt.y + 0.4 * displayScale, focusAt.z + 0.85 * displayScale);
      }
      controls.target.copy(focusAt);
      focusPrev.copy(focusAt);
      focusHave = true;
    } else if (focus < 0 && controls.object) {
      // Leaving puts the orbit floor back to 18 m while the camera is still ~7 m from
      // the aircraft it was inspecting; the next drag then snapped the view outward on
      // its own. Walk it back along the same line of sight instead, so exiting ends on
      // a frame the operator was already looking at rather than a jump.
      const d = controls.object.position.distanceTo(controls.target);
      if (d > 0.001 && d < DEFAULT_MIN) {
        const dir = controls.object.position.clone().sub(controls.target).divideScalar(d);
        controls.object.position.copy(controls.target).addScaledVector(dir, DEFAULT_MIN);
      }
    }
  }
  return focus;
}

// What one airframe is made of, for the dock's readout: the claim "zoom in and it is a
// drone" is a part count and a wingspan, and it should be printed rather than asserted.
export function showCraftInfo() {
  const c = craft.find(x => x.visible) || craft[0];
  if (!c) return null;
  const s = craftSummary(c);
  // The prop angles go out with the part count because "zoom in and it is a drone" is a
  // claim about four blades turning in two directions, and that is only worth anything
  // if something can read it back off the scene.
  const props = (c.userData.props || []).map(p => ({ dir: p.userData.dir, rot: +p.rotation.y.toFixed(4) }));
  return { ...s, wing: wingspan(c), props };
}

export function showFocusState() {
  return { focus, count: craft.filter(c => c.visible).length };
}

function followFocus() {
  if (!controls || focus < 0 || !craft[focus]) return;
  craft[focus].getWorldPosition(focusAt);
  if (focusHave) {
    const dx = focusAt.x - focusPrev.x, dy = focusAt.y - focusPrev.y, dz = focusAt.z - focusPrev.z;
    controls.object.position.set(controls.object.position.x + dx,
      controls.object.position.y + dy, controls.object.position.z + dz);
    controls.target.copy(focusAt);
  } else {
    controls.target.copy(focusAt);
    focusHave = true;
  }
  focusPrev.copy(focusAt);
}

export function showStart() {
  if (!plan || !compiled) return false;
  clock = 0;
  parked = false;
  flying = true;
  return true;
}

export function showStop() {
  flying = false;
}

export function showPreview(t) {
  clock = Math.max(0, Math.min(showDuration(plan || { acts: [] }), t));
  flying = false;
  parked = false;      // the scrub bar or the timeline asked for this instant by name
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
    parked,
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
  showFocus(-1);
  if (group) scene.remove(group);
  group = null;
  craft = [];
  plan = null;
  compiled = null;
  flying = false;
  clock = 0;
}
