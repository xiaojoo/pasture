// Building materials. The four originals were flat colours on a box, which is the main
// reason the ranch read as a diagram; each one now carries a procedurally drawn surface and
// a bump map derived from it, so a wall has courses, a board has a seam, and a roof has
// something to catch the light.
import * as THREE from 'three';
import {
  plasterTexture, concreteTexture, plankTexture, lapboardTexture,
  seamMetalTexture, corrugatedTexture, brickTexture,
  gravelTexture, tileTexture, dirtTexture,
} from '../core/textures.js';

function textured(tex, color, roughness = 0.9, metalness = 0) {
  return new THREE.MeshStandardMaterial({
    color,
    map: tex,
    bumpMap: tex,
    bumpScale: 0.25,
    roughness,
    metalness,
  });
}

/* 主屋外墙：殖民粉墙。石灰浆刷的，所以纹理要粗、颜色要暖，
   不是乳胶漆那种平。参考图里这面粉在金色斜阳下偏鲑鱼色，阴影侧偏藕色。 */
export const wallMat = textured(plasterTexture(3, 2), 0xe6a48d, 0.94);

/* 牛舍木板：牲口棚的外板是风雨灰化的立板，不是美式谷仓的红木板 */
export const woodMat = textured(plankTexture(3, 1, [1, 0.97, 0.93]), 0x9a8570, 0.97);

/* 屋顶瓦：烧土半筒瓦，一行一行搭接。原来的 shingle 是沥青片，远看像纸板。
   repeat 给 1：瓦的密度由 js/world/building-kit.js 的 metreUV 按米算，不在这里乘次数，
   否则同一张瓦在 25 m 棚顶上一瓦一米、在 2 m 通风帽上就变成一瓦八厘米。 */
export const roofMat = textured(tileTexture(1, 1), 0xb8542f, 0.82);

/* 棚屋顶：同一款瓦，颜色沉一点，因为牲口棚的瓦更旧 */
export const barnRoofMat = textured(tileTexture(1, 1), 0xa94e2c, 0.85);

/* 百叶窗：刷蓝的木板。参考图里整栋房子只有这一处冷色，所以它必须够蓝。 */
export const shutterMat = textured(plankTexture(1, 6, [0.98, 0.99, 1]), 0x2a6fae, 0.86);

/* 风雨原木：牌坊、栅栏、牲口棚的柱子，用的是没刨过的圆木和旧枕木 */
export const timberMat = textured(plankTexture(2, 1, [0.9, 0.88, 0.86]), 0x7a6a58, 0.98);
export const darkTimberMat = textured(plankTexture(2, 1, [0.82, 0.8, 0.78]), 0x4e4034, 0.98);

/* 干草/泥地：牛栏里被踩实的那层土 */
export const dirtMat = new THREE.MeshStandardMaterial({
  map: dirtTexture(8, 8),
  bumpMap: dirtTexture(8, 8),
  bumpScale: 0.3,
  color: 0xa97d52,
  roughness: 1,
});

export const metalMat = textured(seamMetalTexture(6, 2, [1, 1, 1]), 0x8d979c, 0.55, 0.55);

export const concreteMat = textured(concreteTexture(3, 1), 0xa8a49c, 0.98);
export const brickMat = textured(brickTexture(2, 2), 0xb08878, 0.95);
export const sidingMat = textured(lapboardTexture(2, 1), 0xc9c3b4, 0.9);
export const corrugatedMat = textured(corrugatedTexture(8, 2, [1, 1, 1]), 0x9aa2a4, 0.62, 0.5);
export const metalRoofMat = textured(seamMetalTexture(9, 2, [1, 1, 1]), 0x77828a, 0.55, 0.6);

/* 白色木装修：檐口、窗套、角柱、腰线 */
export const trimMat = new THREE.MeshStandardMaterial({
  color: 0xf4efe4,
  roughness: 0.72,
});

export const fasciaMat = new THREE.MeshStandardMaterial({
  color: 0xe4dfd2,
  roughness: 0.78,
});

/* 窗框 */
export const frameMat = new THREE.MeshStandardMaterial({
  color: 0x4a4239,
  roughness: 0.6,
});

// Daylight glass. Dark, slightly blue, and a little metallic so it reads as a reflection
// rather than a hole in the wall. The house's own windows are emissive clones of a separate
// material -- see world/house.js -- because the lighting board drives those.
export const glassMat = new THREE.MeshStandardMaterial({
  color: 0x2b3a42,
  roughness: 0.12,
  metalness: 0.42,
});

export const gableMat = new THREE.MeshStandardMaterial({
  map: wallMat.map,
  bumpMap: wallMat.bumpMap,
  bumpScale: 0.25,
  color: 0xe6a48d,
  roughness: 0.94,
  side: THREE.DoubleSide,
});

const doorTex = plankTexture(1, 1, [1, 0.95, 0.9]);

export const doorMat = new THREE.MeshStandardMaterial({
  color: 0x5c3a26,
  map: doorTex,
  bumpMap: doorTex,
  bumpScale: 0.3,
  roughness: 0.8,
});

/* =========================================================
   中式古建筑
   民居换了形制，材料表就得跟着换：黛瓦是青灰偏蓝的，不是陶土橙；墙是白灰刷上去的粉墙，
   底下一米是青砖勒脚；柱子是木的、上朱漆，台基是石头。屋顶的瓦密度仍然交给
   building-kit.js 按米铺，这里只给 1，免得同一张瓦在大殿和门房上是一样宽的瓦。
========================================================= */
export const tileMat = textured(tileTexture(1, 1), 0x4d545c, 0.78);          // 黛瓦
export const ridgeMat = new THREE.MeshStandardMaterial({ color: 0x35393f, roughness: 0.8 });  // 正脊、垂脊
export const plasterMat = textured(plasterTexture(3, 2), 0xe9e5da, 0.95);    // 白粉墙
export const greyBrickMat = textured(brickTexture(2, 2), 0x8d8f8a, 0.96);    // 青砖（勒脚、院墙）
export const stoneMat = textured(concreteTexture(3, 1), 0xc4c0b4, 0.95);     // 白石台基、踏跺
export const lacquerMat = textured(plankTexture(2, 1, [0.95, 0.9, 0.88]), 0x6f2f24, 0.6); // 朱漆柱、额枋
export const woodDarkMat = textured(plankTexture(2, 1, [0.85, 0.82, 0.8]), 0x3f312a, 0.86); // 格扇、门、挂落

/* 埋地配电：电缆外护套。杆、横担、瓷瓶跟着电线一起进土里了，接头坑的铸铁盖这一轮也清了，
   所以地上没有任何电气材料 —— 见 js/world/cables.js 的开头。 */
export const sheathMat = new THREE.MeshStandardMaterial({ color: 0x1d2226, roughness: .62, metalness: .1 });

/* 开关站碎石：按 2 米一格铺，和 js/world/road.js 的沥青同一密度。
   路面自己带材质，因为它的 UV 是按米缩放的，不是按 repeat 次数。 */
const gravelTex = gravelTexture(6, 2.75);

export const gravelMat = new THREE.MeshStandardMaterial({
  map: gravelTex,
  bumpMap: gravelTex,
  bumpScale: 0.4,
  color: 0xa9a094,
  roughness: 1,
});
