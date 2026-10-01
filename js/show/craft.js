// The light-show airframe, as a function of nothing but three.js.
//
// It lives apart from `fleet.js` because "does this read as a drone when you get close"
// is a claim about geometry -- part count, wingspan, and how big the lamp is next to the
// frame -- and a claim about geometry should be measurable, not something only a window
// can confirm. `fleet.js` imports it and owns the positions and the lights' colours.
import * as THREE from 'three';

// One set of numbers, and the wingspan derived from it rather than remembered: the
// first version of this file quoted 0.63 m while drawing 1.06 m, because the span was a
// second hand calculation of a diagonal nobody had measured.
export const ARM = 0.17;                  // metres, centre to motor, per axis
export const PROP_R = 0.095;              // blade half-length, so a prop is 0.19 m
export const LED_SIZE = 0.03;             // the bead the audience sees
// The span of a quad is the circle its props sweep, and that circle does not turn with
// the blades. Measuring the bounding box did: 0.62 m with the blades one way, 0.72 m
// another, so the number on the show bar would not sit still (and a readout that jitters
// is a readout nobody believes). Hub positions are stable, so: the widest pair of motor
// centres, plus one blade length beyond each. Still taken out of the built object rather
// than from a formula about a diagonal -- the two earlier hand calculations of that
// diagonal were 0.63 m and 0.67 m, and the frame drew neither.
export function wingspan(c) {
  const hubs = [];
  c.traverse(o => {
    if (o.userData && o.userData.dir !== undefined) hubs.push(o.position);
  });
  if (hubs.length < 2) return 0;
  let d = 0;
  for (const a of hubs) for (const b of hubs) d = Math.max(d, Math.hypot(a.x - b.x, a.z - b.z));
  return d + 2 * PROP_R;
}

export function buildCraft() {
  const g = new THREE.Group();
  const bodyMat = new THREE.MeshStandardMaterial({ color: 0x1b2a22, roughness: .75, metalness: .2 });
  const frameMat = new THREE.MeshStandardMaterial({ color: 0x2c3b33, roughness: .85, metalness: .15 });
  const motorMat = new THREE.MeshStandardMaterial({ color: 0x9aa6ae, roughness: .35, metalness: .85 });
  const bladeMat = new THREE.MeshStandardMaterial({ color: 0x12171c, roughness: .6, side: THREE.DoubleSide });

  const body = new THREE.Mesh(new THREE.BoxGeometry(0.10, 0.035, 0.13), bodyMat);
  g.add(body);
  const canopy = new THREE.Mesh(new THREE.SphereGeometry(0.035, 10, 8), bodyMat);
  canopy.scale.set(1, .55, 1.25);
  canopy.position.set(0, .025, -.012);
  g.add(canopy);
  // The nose exists because the show turns each aircraft onto its track; without a
  // front, a paused airframe has no readable orientation.
  const nose = new THREE.Mesh(new THREE.ConeGeometry(0.013, 0.036, 6),
    new THREE.MeshStandardMaterial({ color: 0xd94b3b, roughness: .6 }));
  nose.rotation.x = -Math.PI / 2;
  nose.position.set(0, 0, -0.082);
  g.add(nose);

  const props = [];
  for (const [sx, sz] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    const arm = new THREE.Mesh(new THREE.BoxGeometry(ARM * 1.5, 0.012, 0.022), frameMat);
    arm.rotation.y = Math.atan2(sz, sx);
    arm.position.set(sx * ARM * 0.5, 0, sz * ARM * 0.5);
    g.add(arm);

    const mx = sx * ARM, mz = sz * ARM;
    const motor = new THREE.Mesh(new THREE.CylinderGeometry(0.016, 0.019, 0.028, 10), motorMat);
    motor.position.set(mx, 0.017, mz);
    g.add(motor);

    // Two blades on a hub: a spinning prop reads as a disc, a stopped one still reads
    // as a prop, which is the difference between a paused show and a paper one.
    const prop = new THREE.Group();
    for (const s of [0, Math.PI]) {
      const blade = new THREE.Mesh(new THREE.BoxGeometry(PROP_R * 2, 0.003, 0.013), bladeMat);
      blade.rotation.y = s;
      blade.position.set(Math.cos(s) * PROP_R, 0, -Math.sin(s) * PROP_R);
      prop.add(blade);
    }
    prop.add(new THREE.Mesh(new THREE.CylinderGeometry(0.007, 0.007, 0.013, 8), motorMat));
    prop.position.set(mx, 0.034, mz);
    prop.userData = { dir: sx * sz < 0 ? 1 : -1 };   // diagonal pairs counter-rotate
    g.add(prop);
    props.push(prop);

    const guard = new THREE.Mesh(new THREE.TorusGeometry(PROP_R + 0.008, 0.0025, 4, 18), frameMat);
    guard.rotation.x = Math.PI / 2;
    guard.position.set(mx, 0.012, mz);
    g.add(guard);
  }

  for (const s of [-1, 1]) {
    const skid = new THREE.Mesh(new THREE.BoxGeometry(0.011, 0.032, 0.14), frameMat);
    skid.position.set(s * 0.045, -0.031, 0);
    g.add(skid);
  }

  // The light is what the audience reads at 60 m; it is sized as a lamp on a frame, not
  // as the frame itself, which is what used to make a close look read as a glowing ball.
  const led = new THREE.Mesh(new THREE.SphereGeometry(LED_SIZE, 10, 8),
    new THREE.MeshBasicMaterial({ color: 0xffffff }));
  led.position.y = -0.022;
  g.add(led);
  const halo = new THREE.PointLight(0xffffff, 0.9, 9, 2);
  halo.position.y = -0.038;
  g.add(halo);

  g.userData = { led, halo, props };
  return g;
}

// What the close look is claiming, as numbers: enough separate parts to read as an
// airframe, a wingspan in the metre's tenths, and a lamp smaller than the frame.
export function craftSummary(c) {
  let meshes = 0;
  c.traverse(o => { if (o.isMesh) meshes++; });
  const box = new THREE.Box3().setFromObject(c);
  const size = box.getSize(new THREE.Vector3());
  // Same rule as wingspan(): the summary describes the aircraft, whatever scale the
  // show happens to be drawing it at.
  const k = c.scale.x || 1;
  // `span` is wingspan()'s number, not the box's: the box of a spinning model changes
  // every frame, and the two have to be the same number or the bar and the gate disagree.
  return { meshes, span: wingspan(c), height: size.y / k, led: LED_SIZE * 2 };
}
