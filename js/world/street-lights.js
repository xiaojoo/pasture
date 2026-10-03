import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { ROADS, ALLEE_X, ALLEE_REAR } from './layout.js';
/* =========================================================
   STREET LIGHTS

   Three things were wrong here, and all three are the same shape: the lamp was modelled once
   pointing east and then copied twenty times without asking where each one's road is.

     - Every arm pointed +X. The main road runs north-south, so the whole west row reached
       toward the carriageway but the whole east row reached away from it; the cross-road row
       runs east-west, so those six reached *along* their road instead of over it. Measured:
       19 of 20 heads landed on grass, and the head of the sampled lamp sat 1.5 units OUTSIDE
       the kerb. A head hanging sideways off a thin dark pole is exactly what reads as a
       yellow ball floating in the air.
     - The arm was 1.1 long, so even pointing the right way it could not clear the kerb.
     - The "breathing" in core/animation.js assigned `intensity = 1.75` every frame,
       overwriting the 90 the lamp is built with. Under r155+ physical light units with
       inverse-square decay that is nothing: the pool under a head measured identical to the
       same pixels with every lamp switched off (gain 1.00). Breathing now scales around the
       designed intensity instead of replacing it.

   So each lamp is now given the direction of its road, the arm is long enough to hang the
   head just inside the kerb, and the bare ball has a hood and a drop stem so it reads as a
   luminaire rather than a sphere.
========================================================= */

export const streetLights = [];

export const LAMP_HEIGHT = 5.6;
export const ARM_REACH = 2.55;

// Candela. Chosen by measurement, not by eye: this is the value at which the pool under a
// head is clearly separated from the moonlit road beside it without blowing out the kerb.
export const STREET_LAMP_INTENSITY = 420;
export const STREET_LAMP_RANGE = 26;

// A farm lane is lit from a creosoted post, not a galvanised mast. The metal reads as a
// car park and sits oddly against timber fencing and clay tile.
const poleMat = new THREE.MeshStandardMaterial({
    color:0x5b4a38,
    metalness:0,
    roughness:.94
});

const hoodMat = new THREE.MeshStandardMaterial({
    color:0x39433f,
    metalness:.6,
    roughness:.5,
    side:THREE.DoubleSide
});

export function createStreetLight(x, z, towardX = 1, towardZ = 0){

    const group =
    new THREE.Group();

    group.position.set(x, 0, z);

    // rotation.y = θ sends local +X to (cos θ, 0, -sin θ), which is what makes the pair of
    // arguments read as the world direction the head should hang over.
    group.rotation.y =
    Math.atan2(-towardZ, towardX);


    /* 灯杆：下粗上细，带底座 */

    const pole =
    new THREE.Mesh(
        new THREE.CylinderGeometry(.14, .2, LAMP_HEIGHT, 10),
        poleMat
    );

    pole.position.y =
    LAMP_HEIGHT / 2;

    pole.castShadow =
    true;

    group.add(pole);

    const base =
    new THREE.Mesh(
        new THREE.CylinderGeometry(.3, .36, .42, 10),
        poleMat
    );

    base.position.y =
    .21;

    base.castShadow =
    true;

    group.add(base);

    const bolt =
    new THREE.Mesh(
        new THREE.CylinderGeometry(.36, .36, .07, 10),
        hoodMat
    );

    bolt.position.y =
    .45;

    group.add(bolt);


    /* 灯臂：从杆顶伸到路缘内侧 */

    const arm =
    new THREE.Mesh(
        new THREE.BoxGeometry(ARM_REACH + .25, .11, .11),
        poleMat
    );

    arm.position.set(
        ARM_REACH / 2 - .1,
        LAMP_HEIGHT - .1,
        0
    );

    arm.castShadow =
    true;

    group.add(arm);

    // the brace under the arm, without which a 2.5 m cantilever looks like it should snap
    const brace =
    new THREE.Mesh(
        new THREE.BoxGeometry(1.5, .07, .07),
        poleMat
    );

    brace.position.set(.85, LAMP_HEIGHT - .62, 0);
    brace.rotation.z = -.34;

    group.add(brace);


    /* 灯头：吊杆 + 灯罩 + 灯泡 */

    const stem =
    new THREE.Mesh(
        new THREE.CylinderGeometry(.06, .06, .42, 6),
        poleMat
    );

    stem.position.set(ARM_REACH, LAMP_HEIGHT - .32, 0);

    group.add(stem);

    const hood =
    new THREE.Mesh(
        new THREE.CylinderGeometry(.2, .52, .3, 12, 1, true),
        hoodMat
    );

    hood.position.set(ARM_REACH, LAMP_HEIGHT - .5, 0);

    hood.castShadow =
    true;

    group.add(hood);

    const cap =
    new THREE.Mesh(
        new THREE.CylinderGeometry(.2, .2, .06, 12),
        hoodMat
    );

    cap.position.set(ARM_REACH, LAMP_HEIGHT - .34, 0);

    group.add(cap);

    const bulb =
    new THREE.Mesh(
        new THREE.SphereGeometry(.2, 12, 8),
        new THREE.MeshStandardMaterial({
            color:0xffd98c,
            emissive:0xffa32c,
            emissiveIntensity:2
        })
    );

    bulb.position.set(
        ARM_REACH,
        LAMP_HEIGHT - .62,
        0
    );

    group.add(bulb);


    const light =
    new THREE.PointLight(
        0xffbd5c,
        STREET_LAMP_INTENSITY,
        STREET_LAMP_RANGE
    );

    light.position.set(
        ARM_REACH,
        LAMP_HEIGHT - .7,
        0
    );

    // ~20 lamps are placed along the roads; a shadow-casting point light costs six extra
    // render passes each, so the pools of light are unshadowed and the moonlight
    // directional carries the scene's shadows.
    light.castShadow =
    false;

    group.add(
        light
    );


    scene.add(
        group
    );

    streetLights.push({
        group,
        bulb,
        light,
        baseIntensity: STREET_LAMP_INTENSITY
    });
}

/* 路灯沿路网排：每条路都排，不是只排牌坊那一段。
   灯杆立在路缘外 VERGE 米、灯头朝路心伸回来，左右交替，节距 PITCH。
   位置全部从 ROADS 推 —— 上一版是 6 颗排在大道 22..46 一段、另外 4 颗排在 z=22.5，
   而 z=22.5 上根本没有路（横道在 32 / −22 / −70 / 0），所以那 4 颗是立在草地上的。
   站不进去的格子跳过（别的车行道、建筑包围盒、树行），跳完由剩下的灯自己撑开间距。 */
const VERGE = 2.2;
const PITCH = 22;

const blocked = [];
for (const R of ROADS) {
  const m = 0.6;
  blocked.push(R.axis === 'z'
    ? { x0: R.at - R.w / 2 - m, x1: R.at + R.w / 2 + m, z0: Math.min(R.from, R.to) - m, z1: Math.max(R.from, R.to) + m }
    : { x0: Math.min(R.from, R.to) - m, x1: Math.max(R.from, R.to) + m, z0: R.at - R.w / 2 - m, z1: R.at + R.w / 2 + m });
}
const bb = new THREE.Box3();
scene.children.forEach(c => {
  if (!(c.userData && c.userData.building)) return;
  bb.setFromObject(c);
  if (!bb.isEmpty()) blocked.push({ x0: bb.min.x - 1.2, x1: bb.max.x + 1.2, z0: bb.min.z - 1.2, z1: bb.max.z + 1.2 });
});
// 屋后那条按树行：灯杆不能站在树坑里
for (const sx of [-1, 1]) {
  blocked.push({ x0: sx * ALLEE_X - 1.7, x1: sx * ALLEE_X + 1.7, z0: ALLEE_REAR[0] - 1, z1: ALLEE_REAR[1] + 1 });
}
/* 明管不当障碍，试过又撤了：把水干管加进这张表（左右各让 0.8 m）之后，大道 6 颗灯全没了
   —— 干管在 x=±5.8、灯杆落在 ±6.7，本来就一直共用这条肩，让 0.8 就把整条大道抹掉了。
   这一轮之后这个问题不存在了：水管全部埋到 -0.95，地上没有管子可让。 */
const isBlocked = (x, z) => blocked.some(b => x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1);

export const lampStats = { perRoad: {}, placed: 0, skipped: 0 };

for (const R of ROADS) {
  const a = Math.min(R.from, R.to), b = Math.max(R.from, R.to), len = b - a;
  const n = Math.max(2, Math.round(len / PITCH));
  let put = 0;
  for (let i = 0; i < n; ++i) {
    const t = a + (i + 0.5) * (len / n);
    const side = i % 2 ? 1 : -1;
    // 站人的带：先试路缘外 2.2 m，被树行占了就退到路缘和树行中间那条缝（大道两侧都是
    // 这种情况：灯杆 6.7、按树行 7.4，两颗正好抢同一条带，大道 7 个位置只活下来 2 个）。
    let placed = false;
    for (const verge of [VERGE, 0.6]) {
      const off = (R.w / 2 + verge) * side;
      const x = R.axis === 'z' ? R.at + off : t;
      const z = R.axis === 'z' ? t : R.at - off;
      if (isBlocked(x, z)) continue;
      createStreetLight(x, z, R.axis === 'z' ? -side : 0, R.axis === 'z' ? 0 : side);
      put++; placed = true;
      break;
    }
    if (!placed) lampStats.skipped++;
  }
  lampStats.perRoad[R.id] = put;
  lampStats.placed += put;
}
