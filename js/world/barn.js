import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { woodMat, barnRoofMat, timberMat, concreteMat, trimMat, frameMat, glassMat } from './materials.js';
import { box, gambrelRoof, gableRoof, windowUnit, plinth, cornerBoards, roofVent, mergeGroup } from './building-kit.js';
import { SITES } from './layout.js';
/* =========================================================
   BARN
   The cattle shed stands to the right of the axis with its front to the lane, and it is
   roofed in clay tile like everything else here -- a swept gambrel under a corrugated lid was
   a Wisconsin barn, and this is a galpão: a steep gable over weathered vertical boards, open
   at the sides where the stock lie down out of the sun.
========================================================= */

export function createBarn() {
  const group = new THREE.Group();
  group.position.set(SITES.barn.x, 0, SITES.barn.z);
  group.rotation.y = SITES.barn.ry;
  group.userData.type = 'barn';
  group.userData.building = 'barn';

  const W = 22;
  const D = 13;
  const H = 6.6;

  plinth(group, W, D, 0.7, concreteMat);

  // wainscot: the kick plate a barn always has at ground level
  box(group, W + 0.3, 2.3, D + 0.3, timberMat, 0, 1.15, 0);

  const body = box(group, W, H - 2.0, D, woodMat, 0, 2.0 + (H - 2.0) / 2, 0);
  body.userData.building = 'barn';

  cornerBoards(group, W, D, H - 2.0, 2.0);
  box(group, W + 0.42, 0.3, 0.22, trimMat, 0, 2.0, D / 2 + 0.06);
  box(group, W + 0.42, 0.3, 0.22, trimMat, 0, 2.0, -D / 2 - 0.06);

  const roof = gableRoof(group, {
    w: W, d: D, wallTop: H, rise: 3.2, ov: 1.3, mat: barnRoofMat,
  });

  /* 牛舍大门：双扇、对角撑、滑轨 */
  const dz = D / 2;
  for (const sx of [-1, 1]) {
    const leaf = box(group, 3.5, 6.2, 0.24, woodMat, sx * 1.85, 3.1, dz + 0.2);
    leaf.userData.building = 'barn';
    // Z-brace: two boards, one from each corner to the far end of the rail
    const b1 = box(group, 4.6, 0.34, 0.1, trimMat, sx * 1.85, 3.1, dz + 0.35);
    b1.rotation.z = sx * 0.62;
    const b2 = box(group, 4.6, 0.34, 0.1, trimMat, sx * 1.85, 3.1, dz + 0.35);
    b2.rotation.z = -sx * 0.62;
    box(group, 0.3, 6.4, 0.12, trimMat, sx * 3.62, 3.1, dz + 0.3);
  }
  box(group, 7.9, 0.4, 0.4, trimMat, 0, 6.45, dz + 0.28);            // header
  // 原来这里还有一根 8.6 长的深色滑轨（y=6.85）和两只吊挂。檐口在 6.6，屋面从这个高度
  // 往檐外是继续往下走的，所以这根轨道悬在屋面之上 —— 航拍看下来就是"屋顶上有条黑线"。
  // 轨道和吊挂一起去掉，浅色门楣留着，门还是那扇双扇大门。
  box(group, 8.4, 0.14, 1.6, concreteMat, 0, 0.09, dz + 0.9);        // apron

  /* 草料阁门与吊臂 */
  const loft = roof.ridgeY - 1.2;
  box(group, 3.2, 2.6, 0.2, woodMat, 0, loft, dz + 0.16);
  box(group, 3.6, 0.26, 0.3, trimMat, 0, loft + 1.55, dz + 0.2);
  box(group, 0.18, 0.18, 2.4, frameMat, 0, loft + 1.25, dz + 1.3);
  const pulley = new THREE.Mesh(new THREE.TorusGeometry(0.34, 0.07, 6, 12), frameMat);
  pulley.position.set(0, loft + 1.25, dz + 2.5);
  pulley.rotation.y = Math.PI / 2;
  group.add(pulley);

  /* 通风窗：原来那两扇的位置不动 */
  for (const x of [-8, 8]) {
    windowUnit(group, { x, y: 5.5, z: dz, w: 2.8, h: 1.3, glass: glassMat });
  }
  for (const sx of [-1, 1]) {
    for (const z of [-5, 0, 5]) {
      windowUnit(group, { x: sx * W / 2, y: 4.6, z, w: 1.9, h: 1.4, face: 'x', dir: sx, glass: glassMat });
    }
    // loft door on each gable end, which is where a barn's light comes from
    box(group, 0.2, 3.0, 3.4, woodMat, sx * (W / 2 + 0.12), loft, 0);
    box(group, 0.16, 3.4, 0.3, trimMat, sx * (W / 2 + 0.24), loft, -1.8);
    box(group, 0.16, 3.4, 0.3, trimMat, sx * (W / 2 + 0.24), loft, 1.8);
  }

  /* 屋顶通风塔 */
  const cupola = new THREE.Group();
  box(cupola, 2.4, 1.9, 2.4, trimMat, 0, 1.0, 0);
  for (const sx of [-1, 1]) box(cupola, 0.14, 1.5, 2.0, frameMat, sx * 0.9, 1.05, 0);
  box(cupola, 2.9, 0.16, 2.9, barnRoofMat, 0, 2.35, 0);
  const cap = box(cupola, 1.6, 0.7, 1.6, barnRoofMat, 0, 2.85, 0);
  cap.rotation.y = Math.PI / 4;
  cupola.position.y = roof.ridgeY - 0.2;
  group.add(cupola);
  roofVent(group, -7.5, roof.ridgeY - 0.9, 1.2, 0.5, 1.1);

  mergeGroup(group);

  scene.add(group);
  buildings.barn = { group };
  return group;
}

createBarn();
