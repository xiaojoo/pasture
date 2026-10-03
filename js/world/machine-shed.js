import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { buildings } from './house.js';
import { concreteMat, corrugatedMat, roofMat, woodMat, frameMat, fasciaMat, trimMat } from './materials.js';
import { SITES } from './layout.js';
import { box, gableRoof, plinth, cornerBoards, mergeGroup } from './building-kit.js';
import { rollUpDoor } from './warehouse.js';
/* =========================================================
   MACHINE SHED
   The door is the story here: it stands open, because a shed with its door shut is just
   another box. Behind it there is a dark bay, and out of it a concrete apron the tractor
   actually drives on.

   The bay used to be a lie: the body was one solid box and the "dark bay" was a black panel
   on the front face. That is fine while nothing has to be in there; the drone is parked
   inside now, so the walls became walls (back + two sides + two front jambs around the
   opening) and there is a real, empty interior with a concrete floor at FLOOR.
========================================================= */

const W = 16;
const D = 9;
const H = 4.6;
const T = 0.3;                 // 墙厚
const FLOOR = 0.5;             // 室内地坪 = 台基顶
const RISE = 2.3;              // 屋脊起坡
const OV = 1.15;               // 檐口外伸

const dz = D / 2;
/* 门洞净宽按无人机的旋翼扫掠圆定的：量到四个桨盘绕机头转一圈的外接半径 4.3 m（支臂在
   ±1.90 对角、桨叶再往外 1.4，斜置的支臂让这个圆比静止时的包围盒大一圈），整机扫掠直径
   8.6 m。原来 7.4 m 的门（那是拖拉机那扇门的历史尺寸）过不去 —— 偏航角只差 9° 的时候
   机身外接半宽就到 3.8，已经压到 3.7 的门柱上，量到入库最后一段 46 帧刮门柱。
   9.2 m 给出 0.3 m 的余量；两侧剩下的窗间墙还有 3.4 m，结构上是一扇正常的农具库大门。 */
const DOOR_W = 9.2;            // 门洞净宽

/* 停机位和屋顶净空交给 drone.js：它停在哪、多高之前不能爬，都由这个建筑说，
   不在两处各写一遍字面量（这项目里已经因为这个错过一次：牛舍一挪，水管留在墙里两米）。
   门在 +z 面，所以"出库"就是往 +z 滑到墙外。 */
/* 停机位取 z=0.6 是倒推出来的：无人机桨盘落在地面上的投影量到 5.5 x 6.7 m，机身中心
   往前 3.35 m，前墙内表面在 z=4.35，所以要整架都在屋里，停机位得退到 1.0 以内。 */
const PAD_LOCAL = { x: 0, z: 0.6 };
const sin = Math.sin(SITES.machine.ry), cos = Math.cos(SITES.machine.ry);
const toWorld = (p) => ({
  x: SITES.machine.x + p.x * cos + p.z * sin,
  z: SITES.machine.z - p.x * sin + p.z * cos,
});
export const HANGAR = {
  pad: { ...toWorld(PAD_LOCAL), floor: FLOOR },
  centre: { x: SITES.machine.x, z: SITES.machine.z },
  floor: FLOOR,
  ridgeTop: 0,                 // 由 measureShell() 按真实几何量出来填
  eaveY: 0,                    // 屋顶最低那一点（檐口板下沿）：过廊道的净高以它为准
  hx: W / 2 + OV,
  hz: dz + OV,
  wallTop: H + 0.15,           // 室内顶 = 墙上沿，也就是门洞的净高
  out: toWorld({ x: 0, z: dz + 9 }),   // 门外 9 m：机身长 6.7 m，站近了尾巴扫到门框
};

/* 库的"平面范围"是屋顶外沿而不是墙外沿：檐口外伸 OV 再加板厚，量到屋顶在 z 方向伸到
   6.03、x 方向到 9.15，而墙只在 4.5 和 8 —— 无人机从檐口下面过的时候，桨盘是切在檐板上
   的。这三个数在这里量一次，drone.js 不再自己猜。 */
function measureShell(group) {
  group.updateWorldMatrix(true, true);
  const inv = new THREE.Matrix4().copy(group.matrixWorld).invert();
  const shell = new THREE.Box3();
  const b = new THREE.Box3();
  let found = false;
  group.traverse((m) => {
    if (!m.isMesh) return;
    b.setFromObject(m).applyMatrix4(inv);
    // 只收"整块都待在屋顶层里"的：坡屋顶、檐板、山墙、屋脊帽。卷帘门门罩的上沿到 4.95，
    // 按 max.y 筛会把它算进来，x 范围就从 9.15 被撑到 10.48 —— 场外 1 m 也算"在库里"了。
    if (b.min.y < H + 0.15 - 0.5) return;
    shell.union(b);
    found = true;
  });
  if (!found) return;
  HANGAR.hx = Math.max(Math.abs(shell.min.x), Math.abs(shell.max.x));
  HANGAR.hz = Math.max(Math.abs(shell.min.z), Math.abs(shell.max.z));
  HANGAR.ridgeTop = shell.max.y;
  HANGAR.eaveY = shell.min.y;
}

export function createMachineShed() {
  const group = new THREE.Group();
  group.position.set(SITES.machine.x, 0, SITES.machine.z);
  group.rotation.y = SITES.machine.ry;
  group.userData.type = 'machine';
  group.userData.building = 'machine';

  plinth(group, W, D, FLOOR, concreteMat);
  // 四面墙，前墙被门洞切成两块 —— 中间是空的，能看见里面的地坪和后背板
  const wallY = H / 2 + 0.15;
  const back = box(group, W, H, T, woodMat, 0, wallY, -dz + T / 2);
  back.userData.building = 'machine';
  for (const sx of [-1, 1]) box(group, T, H, D - 2 * T, woodMat, sx * (W / 2 - T / 2), wallY, 0);
  const jamb = (W - DOOR_W) / 2;
  for (const sx of [-1, 1]) box(group, jamb, H, T, woodMat, sx * (DOOR_W / 2 + jamb / 2), wallY, dz - T / 2);
  cornerBoards(group, W, D, H, 0.15);

  gableRoof(group, { w: W, d: D, wallTop: H + 0.15, rise: RISE, ov: OV, thick: 0.32, mat: roofMat });

  // the bay still has to read dark, so the back wall gets an inner skin rather than being
  // seen as lit cladding from inside
  box(group, W - 2 * T, H - 0.2, 0.1, new THREE.MeshStandardMaterial({ color: 0x0d1310, roughness: 1 }),
    0, wallY, -dz + T + 0.06);
  // the door slid open against the wall, ribbed the same way as the cladding
  const slid = box(group, 6.6, 4.9, 0.16, corrugatedMat, -7.2, 2.5, dz + 0.16);
  slid.rotation.y = 0.14;
  // 原来门洞上方还有一根深色门楣（y 4.89~5.21）。门洞就在 +z 面，屋面在这个位置的下沿
  // 只到 5.06 —— 门楣从屋面里穿出来，航拍看就是"屋顶上有条黑线"。撤掉门楣，两侧立柱
  // 收到墙上沿以内（4.85 < 5.06），不再穿屋面。
  for (const sx of [-1, 1]) box(group, 0.26, 4.9, 0.34, frameMat, sx * (DOOR_W / 2 + 0.2), 2.4, dz + 0.12);
  box(group, 11, 0.12, 4.6, concreteMat, 0, 0.08, dz + 2.3);

  rollUpDoor(group, { x: W / 2, y: 2.3, z: -2.6, w: 4.0, h: 4.2, face: 'x', dir: 1 });

  for (const sx of [-1, 1]) {
    for (const z of [-3.6, 0, 3.6]) {
      box(group, 0.14, 1.2, 2.0, frameMat, sx * (W / 2 + 0.1), 4.7, z);
    }
  }
  for (const sx of [-1, 1]) {
    box(group, 0.34, 4.4, 0.34, trimMat, sx * (W / 2 - 0.2), 2.2, -dz + 0.2);
  }
  box(group, 1.1, 1.3, 0.9, fasciaMat, W / 2 - 2.4, 5.2, -dz + 0.9);

  mergeGroup(group);

  scene.add(group);
  buildings.machine = { group };
  measureShell(group);
}

createMachineShed();
