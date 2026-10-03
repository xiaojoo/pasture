// The entrance. A fazenda announces itself with a pair of unmilled posts, a beam laid across
// them, and the name of the place painted on boards hung under the beam -- which is the one
// thing in this composition that tells you what you are looking at, so it is built here rather
// than left to a label in the drawer.
//
// Everything is measured off js/world/layout.js: the opening is 1.3 carriageway widths, so the
// posts stand just clear of the worn dirt and the gate leaves swing over the whole track.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { timberMat, darkTimberMat } from './materials.js';
import { box, mergeGroup } from './building-kit.js';
import { signTexture } from '../core/textures.js';
import { GATE_OPEN, GATE_Z } from './layout.js';

const POST_H = 6.0;
const POST = GATE_OPEN / 2;

// One board per line, and the canvas aspect matched to the board's, or the capitals are
// stretched sideways on the wider lower one.
const boardMats = [
  new THREE.MeshStandardMaterial({ map: signTexture(['青溪牧场'], { w: 1024, h: 320 }), roughness: 0.9 }),
  new THREE.MeshStandardMaterial({ map: signTexture(['QINGXI RANCH'], { w: 1024, h: 240 }), roughness: 0.9 }),
];

export function createGate() {
  const g = new THREE.Group();
  g.position.set(0, 0, GATE_Z);
  g.name = 'gate';

  /* ---- the two posts, and the ground stakes they are braced to ---- */
  for (const sx of [-1, 1]) {
    const post = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.46, POST_H, 9), timberMat);
    post.position.set(sx * POST, POST_H / 2, 0);
    post.castShadow = true;
    post.receiveShadow = true;
    g.add(post);

    const cap = new THREE.Mesh(new THREE.SphereGeometry(0.4, 9, 6, 0, 6.2832, 0, 1.57), timberMat);
    cap.position.set(sx * POST, POST_H - 0.05, 0);
    g.add(cap);

    // the knee the beam actually rests on, so the beam is not floating against the post
    box(g, 1.1, 0.3, 0.9, darkTimberMat, sx * POST, POST_H + 0.12, 0);

    // diagonal brace down to a stake outside the track
    const brace = box(g, 0.26, 3.4, 0.26, timberMat, sx * (POST + 1.15), 1.55, -0.5);
    brace.rotation.z = sx * 0.52;
    brace.rotation.x = -0.2;
    box(g, 0.3, 0.9, 0.3, darkTimberMat, sx * (POST + 2.2), 0.35, -1.0);
  }

  /* ---- the beam, running past the posts at both hands ---- */
  const beam = box(g, GATE_OPEN + 4.6, 0.52, 0.6, timberMat, 0, POST_H + 0.52, 0);
  box(g, GATE_OPEN + 4.6, 0.16, 0.78, darkTimberMat, 0, POST_H + 0.86, 0);

  /* ---- the name boards, hung under the beam on the axis ---- */
  [[3.0, 0.94, 5.5], [4.6, 1.08, 4.35]].forEach(([w, h, y], i) => {
    const board = new THREE.Mesh(new THREE.BoxGeometry(w, h, 0.14), boardMats[i]);
    board.position.set(0, y, 0.42);
    board.castShadow = true;
    g.add(board);
    for (const sx of [-1, 1]) box(g, 0.1, 0.1, 0.3, darkTimberMat, sx * (w / 2 - 0.35), y + h / 2 + 0.16, 0.3);
  });

  /* ---- the two leaves: stiles, four rails, one brace each ---- */
  const leafW = POST - 0.34;
  for (const sx of [-1, 1]) {
    const cx = sx * (0.16 + leafW / 2);
    box(g, 0.2, 3.3, 0.22, darkTimberMat, sx * 0.2, 1.75, 0);              // meeting stile
    box(g, 0.22, 3.3, 0.22, darkTimberMat, sx * (POST - 0.2), 1.75, 0);    // hanging stile
    for (const y of [0.55, 1.45, 2.35, 3.05]) {
      box(g, leafW, 0.2, 0.16, darkTimberMat, cx, y, 0);
    }
    const b = box(g, leafW + 0.3, 0.18, 0.14, timberMat, cx, 1.8, 0.02);
    b.rotation.z = sx * 0.62;
  }
  // the prop the left leaf rests on when the gate is standing open
  box(g, 0.24, 1.1, 0.24, timberMat, -POST + 0.9, 0.55, -1.3);

  /* 牌坊底下原来有一圈"踩实的压土圈"（r=POST+2.4 的圆面，抬在 Y.floor 上）。这一轮他点名
   去掉，要"保持和别的一样" —— 那就是牌坊直接立在草地上，只有大道的车辙带从门中间穿过去。 */

  mergeGroup(g);
  scene.add(g);
  return g;
}

createGate();
