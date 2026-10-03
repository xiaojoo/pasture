// The street row. js/world/layout.js puts six plots along the drive, three on each hand;
// this module builds them, because a farm lane with nothing on either side is a lane through
// a field, not a village street.
//
// Each plot is the house only. It used to be the pair a Chinese street is made of -- a
// boundary wall with a gate tower, and the house set back behind it -- but the wall hid the
// plinth and the lower bay of every facade from the road, and the gate tower's posts and
// piers were built *on* the wall, so the whole line came out as one assembly.
//
// Nothing here carries a coordinate: the plot centre, its facing and its frontage all come
// from layout.js. Move the road and the street moves with it, which is the whole reason this
// file exists separately.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import {
  box, sweptHipRoof, colonnade, latticeWindow, stonePodium, mergeGroup,
} from './building-kit.js';
import {
  plasterMat, greyBrickMat, stoneMat, tileMat, lacquerMat, woodDarkMat,
} from './materials.js';
import { PLOTS, PLOT_FRONT, PLOT_DEPTH, Y } from './layout.js';

/* ---- 一座房子：台基、白墙、檐柱额枋、翘角黛瓦顶、沿街开格扇 ---- */
function houseOnPlot(g, { bays, front, depth }) {
  const wallH = 4.1;
  stonePodium(g, { w: front, d: depth, h: 0.8, mat: stoneMat, steps: 4 });
  const y0 = 0.8;

  const body = box(g, front, wallH, depth, plasterMat, 0, y0 + wallH / 2, 0);
  body.userData.noMerge = false;
  box(g, front + 0.24, 1.0, depth + 0.24, greyBrickMat, 0, y0 + 0.5, 0);      // 勒脚
  box(g, front + 0.3, 0.3, depth + 0.3, greyBrickMat, 0, y0 + wallH + 0.03, 0); // 檐墙压顶：盖过墙顶，不与墙顶面同高

  colonnade(g, { w: front + 2.4, d: depth + 2.4, h: wallH, y0, baysX: bays - 1, baysZ: 1, mat: lacquerMat });

  const roof = sweptHipRoof(g, {
    w: front, d: depth, wallTop: y0 + wallH + 0.42, rise: 2.9, ov: 1.9,
    lift: 1.05, thick: 0.28, mat: tileMat,
  });

  // 沿街（局部 +Z）开格扇：明间是门，次间和梢间是窗
  const fz = depth / 2 + 0.02;
  const bay = front / bays;
  for (let i = 0; i < bays; ++i) {
    const x = -front / 2 + bay * (i + 0.5);
    if (i === Math.floor(bays / 2)) {
      box(g, bay * 0.8, wallH * 0.78, 0.16, woodDarkMat, x, y0 + wallH * 0.39, fz + 0.12);
      box(g, bay * 0.94, 0.24, 0.5, woodDarkMat, x, y0 + wallH * 0.8, fz + 0.2);  // 门罩
      for (const sx of [-1, 1]) box(g, 0.16, wallH * 0.8, 0.16, lacquerMat, x + sx * bay * 0.47, y0 + wallH * 0.4, fz + 0.1);
    } else {
      latticeWindow(g, { x, y: y0 + wallH * 0.52, z: fz, w: bay * 0.62, h: wallH * 0.44, mat: woodDarkMat });
    }
  }
  // 山墙各两扇，背立面三扇：只有正面的房子会在转视角时被发现是布景
  for (const sz of [-1, 1]) {
    for (const x of [-front / 3, front / 3]) {
      latticeWindow(g, { x, y: y0 + wallH * 0.52, z: sz * (depth / 2 + 0.02), dir: sz, w: bay * 0.55, h: wallH * 0.4, mat: woodDarkMat });
    }
  }
  for (const sx of [-1, 1]) {
    for (const z of [-depth / 4, depth / 4]) {
      latticeWindow(g, { x: sx * (front / 2 + 0.02), y: y0 + wallH * 0.52, z, face: 'x', dir: sx, w: depth * 0.2, h: wallH * 0.4, mat: woodDarkMat });
    }
  }
  return roof;
}

export const streetHouses = [];

for (const p of PLOTS) {
  const g = new THREE.Group();
  g.position.set(p.x, Y.floor, p.z);
  g.rotation.y = p.ry;
  g.name = `street-${p.id}`;
  g.userData.type = 'building';
  g.userData.building = p.id;

  houseOnPlot(g, { bays: p.bays, front: PLOT_FRONT, depth: PLOT_DEPTH });

  scene.add(g);
  mergeGroup(g);
  streetHouses.push(g);
}
