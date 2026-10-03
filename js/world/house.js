import * as THREE from 'three';
import { scene } from '../core/scene.js';
import {
  box, sweptHipRoof, colonnade, latticeWindow, stonePodium, mergeGroup,
} from './building-kit.js';
import {
  plasterMat, greyBrickMat, stoneMat, tileMat, woodDarkMat, lacquerMat, ridgeMat,
} from './materials.js';
import { HOUSE_W, HOUSE_D, HOUSE_WALL_H, SITES, Y } from './layout.js';
/* =========================================================
   主屋
   一正两厢的三合院：正房坐北朝南对着车道，两侧厢房往前凸出来把院子抱住。这是"宅子"的
   读法 —— 原来那栋是一整条殖民粉墙加一个四坡顶，转视角时它只有正面，侧面和背面全是布景。

   尺度仍然从车道推出来（js/world/layout.js 的 HOUSE_W = 4.5 倍路面宽），只是分配方式换了：
   正房面阔十九米五开间，厢房各五米一进深，合起来正好还是那个量出来的 29.25 米。

   屋顶是 building-kit.js 的 sweptHipRoof：举折 + 起翘。正脊两端的吻兽和四条垂脊由它一起
   生成，所以这里不再单独摆脊。

   窗必须是几何而不是贴图：格心的斜棂在斜视角下要能看出是木条，一贴图就糊成一片灰。
   同时这些扇心共享一个自发光材质，state/time.js 直接驱动它的 emissiveIntensity —— 这个
   契约不能断，之前的版本把 houseGlass 声明了却没传进窗，结果"室内灯"开关写在一个自发光
   是黑色的材质上，永远不亮。
========================================================= */

export const buildings = {};

const houseGlass = new THREE.MeshStandardMaterial({
  color: 0xffd98a,
  emissive: 0xffaa32,
  emissiveIntensity: 0.2,
  roughness: 0.42,
});

const lanternMat = new THREE.MeshStandardMaterial({
  color: 0xffd7c0, emissive: 0xd83a1c, emissiveIntensity: 1.1, roughness: 0.6,
});

export function createHouse() {
  const group = new THREE.Group();
  group.position.set(SITES.house.x, Y.floor, SITES.house.z);
  group.rotation.y = SITES.house.ry;
  group.userData.type = 'house';
  group.userData.building = 'house';

  const WW = (HOUSE_W - 19) / 2;         // 厢房面宽，保证三间合计仍是量出来的 4.5 倍路面
  const W = HOUSE_W - WW * 2;            // 正房面阔
  const D = HOUSE_D;
  const WD = 13;                         // 厢房进深：往前凸，把院子抱进来
  const H = HOUSE_WALL_H;
  const POD = 0.95;                      // 台基高

  /* ---- 一整块台基托住三座房子 ---- */
  const front = WD / 2 + 1.2;
  const back = D / 2 + 1.2;
  const pd = new THREE.Mesh(new THREE.BoxGeometry(HOUSE_W + 2.6, POD, front + back), stoneMat);
  pd.position.set(0, POD / 2, (front - back) / 2);
  pd.receiveShadow = true;
  pd.castShadow = true;
  group.add(pd);
  const cap = box(group, HOUSE_W + 3.2, 0.24, front + back + 0.6, stoneMat, 0, POD - 0.05 + 0.12, (front - back) / 2);
  cap.receiveShadow = true;
  // 正中三段踏跺，正对轴线
  for (let i = 0; i < 4; ++i) {
    box(group, 5.4 - i * 0.2, POD / 4, 0.55, stoneMat, 0, POD - (i + 0.5) * (POD / 4), front + 0.3 + i * 0.55);
  }
  // 原来踏跺两侧还有一对望柱加一根寻杖，撤掉了：它们立在 z = front+2.2 = 9.9，而台基+上枋
  // 的外沿只到 front+0.3 = 8.3 —— 柱子底下既没有台基也没有地，柱底悬在 0.95 m 空中，寻杖
  // 底面 1.83 而最上一级踏跺的面在 0.95，中间空 0.88 m，从斜上方看就是一根杆子吊在台阶上。
  // （寻杖还写在这个 for 循环里，等于同一处叠了两片共面石皮 —— 正是 mergeGroup 之后
  // 深度测试抛硬币会闪的那种。）住宅排那份栏杆在 building-kit 的 stonePodium 里，一并撤了。

  const windows = [];

  /* ---- 正房 ---- */
  const y0 = POD + 0.24;
  box(group, W, H, D, plasterMat, 0, y0 + H / 2, 0).userData.building = 'house';
  box(group, W + 0.26, 1.05, D + 0.26, greyBrickMat, 0, y0 + 0.52, 0);          // 勒脚
  // 压顶要盖过墙顶：它的顶面如果和墙顶同高，两片共面会被并进同一个 merged buffer，
  // 深度测试变成抛硬币，转视角那一条白墙边就闪。
  box(group, W + 0.34, 0.3, D + 0.34, greyBrickMat, 0, y0 + H + 0.03, 0);        // 檐墙压顶
  colonnade(group, { w: W + 2.6, d: D + 2.6, h: H, y0, baysX: 4, baysZ: 1, mat: lacquerMat });
  const main = sweptHipRoof(group, {
    w: W, d: D, wallTop: y0 + H + 0.44, rise: 3.5, ov: 2.0, lift: 1.2, thick: 0.3, mat: tileMat,
  });

  // 明间三扇屏门，次间梢间各一扇格心窗
  const fz = D / 2;
  for (const x of [-2.1, -0.7, 0.7, 2.1]) {
    box(group, 1.3, H * 0.74, 0.14, woodDarkMat, x, y0 + H * 0.37, fz + 0.28);
  }
  box(group, 5.6, 0.26, 0.5, woodDarkMat, 0, y0 + H * 0.78, fz + 0.3);           // 门罩
  for (const sx of [-1, 1]) box(group, 0.2, H * 0.8, 0.2, lacquerMat, sx * 2.9, y0 + H * 0.4, fz + 0.3);
  for (const x of [-7.0, -4.4, 4.4, 7.0]) {
    windows.push(latticeWindow(group, {
      x, y: y0 + H * 0.52, z: fz + 0.24, w: 1.9, h: H * 0.44, mat: woodDarkMat, glow: houseGlass,
    }));
  }
  for (const sz of [-1, 1]) {
    for (const x of [-W / 3, W / 3]) {
      windows.push(latticeWindow(group, {
        x, y: y0 + H * 0.52, z: sz * (D / 2 + 0.02), dir: sz, w: 2.0, h: H * 0.4,
        mat: woodDarkMat, glow: houseGlass,
      }));
    }
  }
  for (const sx of [-1, 1]) {
    for (const z of [-2.6, 0, 2.6]) {
      windows.push(latticeWindow(group, {
        x: sx * (W / 2 + 0.02), y: y0 + H * 0.52, z, face: 'x', dir: sx, w: 1.9, h: H * 0.4,
        mat: woodDarkMat, glow: houseGlass,
      }));
    }
  }

  /* ---- 两厢：面阔五米、进深十三米，往前凸把院子抱住 ---- */
  for (const sx of [-1, 1]) {
    const wx = sx * (W / 2 + WW / 2);
    const wz = (front - back) / 2 + 0.6;
    box(group, WW, H * 0.82, WD, plasterMat, wx, y0 + H * 0.41, wz).userData.building = 'house';
    box(group, WW + 0.24, 0.9, WD + 0.24, greyBrickMat, wx, y0 + 0.45, wz);
    sweptHipRoof(group, {
      w: WW, d: WD, wallTop: y0 + H * 0.82 + 0.36, rise: 2.5, ov: 1.15, lift: 0.85,
      thick: 0.26, mat: tileMat, cx: wx,
    });
    // 厢房朝院子那一侧开门开窗（内山墙）
    const ix = wx - sx * (WW / 2 + 0.02);
    for (const z of [wz - 3.4, wz, wz + 3.4]) {
      windows.push(latticeWindow(group, {
        x: ix, y: y0 + H * 0.42, z, face: 'x', dir: -sx, w: 1.7, h: H * 0.36,
        mat: woodDarkMat, glow: houseGlass,
      }));
    }
    windows.push(latticeWindow(group, {
      x: wx, y: y0 + H * 0.42, z: wz + WD / 2 + 0.02, dir: 1, w: 2.2, h: H * 0.36,
      mat: woodDarkMat, glow: houseGlass,
    }));
  }

  /* ---- 檐下两盏红灯笼：整栋房子里唯一会自己亮的暖点 ---- */
  const lanterns = [];
  for (const sx of [-1, 1]) {
    const l = new THREE.Mesh(new THREE.SphereGeometry(0.34, 12, 10), lanternMat);
    l.position.set(sx * 5.6, y0 + H - 0.15, fz + 1.5);
    l.scale.y = 1.22;
    group.add(l);
    lanterns.push(l);
    box(group, 0.1, 0.5, 0.1, ridgeMat, sx * 5.6, y0 + H + 0.35, fz + 1.5);
  }

  /* ---- 院心：一口石缸 + 两条石凳，院子不能是空的 ---- */
  const urn = new THREE.Mesh(new THREE.CylinderGeometry(1.15, 0.9, 1.0, 16), stoneMat);
  urn.position.set(0, POD + 0.74, front - 3.4);
  urn.castShadow = true;
  group.add(urn);
  for (const sx of [-1, 1]) box(group, 2.6, 0.42, 0.5, stoneMat, sx * 4.4, POD + 0.45, front - 3.0);

  mergeGroup(group, [...windows, ...lanterns]);

  scene.add(group);
  buildings.house = { group, windows, lantern: lanterns[0], lanterns, glass: houseGlass };
  return group;
}

createHouse();
