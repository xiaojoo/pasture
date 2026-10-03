// The drive. It used to be four strips of asphalt crossing the yard at right angles with a
// cream centre line on them, which is the layout of a housing estate, not of a farm.
//
// The fazenda is one spine: a dirt track straight in from the gate to the front door, a cross
// drive in front of the house that reaches the two yards, and the same track climbing out the
// back between the eucalyptus. Everything is laid as a ribbon along a curve so the bends are
// the same code as the straight, and everything is tiled by world metres for the reason
// js/world/road.js has carried since the split: a PlaneGeometry's 0..1 UV stretched over a
// 6.5 x 160 m strip gives 40 texels per metre lengthwise against 680 across, and no amount of
// anisotropic filtering closes a 16:1 gap.
//
// The wheel ruts are vertex colours, not a second mesh and not a stripe. A rut is a smooth
// darkening over a metre and a half; a thin high-contrast repeating line on a surface this
// long is exactly the thing that strobed when the camera turned, which is what the centre-line
// fade in the previous version existed to paper over.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { dirtTexture } from '../core/textures.js';
import { ROADS, SHOULDER_W, Y } from './layout.js';

const TILE = 2.0;   // metres of ground per texture tile

// No polygonOffset: the layers are separated in world space now (layout.js Y), and an offset
// guess in depth shimmers on its own when the view turns at a grazing angle.
function paved(map, color, roughness, bumpScale) {
  return new THREE.MeshStandardMaterial({
    map,
    bumpMap: map,
    bumpScale,
    color,
    roughness,
  });
}

// The ribbon carries per-vertex rut shading, so it needs vertexColors. A material asking for
// colours a geometry does not have renders black -- which is how the cattle yard once came out
// as a hole in the ground.
const dirtMat = paved(dirtTexture(1, 1), 0xc9a273, 1, 0.35);
const rutMat = dirtMat.clone();
rutMat.vertexColors = true;

// Worn grass at the edge of the carriageway: same hue family as the pasture, dragged toward
// mud, so the shoulder reads as "this is where the wheels go" rather than as a painted band.
const shoulderMat = paved(dirtTexture(1, 1), 0x8a8a52, 1, 0.3);

/* =========================================================
   RIBBON
   A strip of given width following a polyline in the ground plane, with UVs in metres and a
   vertex colour that is dark in the two rut lines and pale on the crown between them.

   The strip is divided ACROSS as well as along, because the ruts sit at a quarter and three
   quarters of the way over: with only the two edge vertices there is nothing for the shading
   to land on, the whole carriageway comes out one flat value, and the "wheel ruts" in the
   comment above are a lie. Five columns put a sample at 0.25 and 0.75, which is close enough
   to the rut centres for the pair to read as lanes rather than as a gradient.
========================================================= */

const COLS = 5;

function ribbon(points, width, y, { ruts = true, vcol = null } = {}) {
  const pos = [];
  const col = [];
  const uv = [];
  const idx = [];
  let along = 0;

  for (let i = 0; i < points.length; ++i) {
    const p = points[i];
    const a = points[Math.max(0, i - 1)];
    const b = points[Math.min(points.length - 1, i + 1)];
    let tx = b[0] - a[0];
    let tz = b[1] - a[1];
    const tl = Math.hypot(tx, tz) || 1;
    tx /= tl;
    tz /= tl;
    if (i) along += Math.hypot(p[0] - a[0], p[1] - a[1]);

    const w = (typeof width === 'function' ? width(i / (points.length - 1)) : width) / 2;
    for (let c = 0; c < COLS; ++c) {
      const across = c / (COLS - 1);
      const off = (across * 2 - 1) * w;
      pos.push(p[0] + tz * off, y, p[1] - tx * off);
      uv.push(off / TILE + 0.5, along / TILE);
      // The ruts are the wheel tracks, a quarter and three quarters of the way across. The
      // dip is shallow on purpose: a rut is a tonal change in dirt, and a thin dark line on a
      // 160 m strip is the same high-contrast repeating feature the old centre line was.
      let shade = 1;
      if (ruts) {
        const d = Math.min(Math.abs(across - 0.25), Math.abs(across - 0.75)) / 0.25;
        shade = 0.86 + 0.14 * Math.min(1, d * d);
      }
      // the outer edge wears into the shoulder
      shade *= 1 - 0.06 * Math.abs(across * 2 - 1);
      if (vcol) shade *= vcol(across, i / (points.length - 1));
      col.push(shade, shade * 0.995, shade * 0.975);
    }

    if (i < points.length - 1) {
      const r0 = i * COLS, r1 = r0 + COLS;
      for (let c = 0; c < COLS - 1; ++c) {
        idx.push(r0 + c, r1 + c, r1 + c + 1, r0 + c, r1 + c + 1, r0 + c + 1);
      }
    }
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

function lay(name, points, width, y, opts) {
  const m = new THREE.Mesh(ribbon(points, width, y, opts), opts?.shoulder ? shoulderMat : rutMat);
  m.name = name;
  m.receiveShadow = true;
  scene.add(m);
  return m;
}

/* =========================================================
   路网：一条南北大道 + 三条东西横道，全部轴对齐（js/world/layout.js 的 ROADS）。
   只有大道带路肩：横道若也各带一层肩，就会和院子的地面、前庭圆在同一高度上重叠，
   那正是这次要修的共面闪烁。横道的磨损感交给顶点色，不再多铺一层薄片。
========================================================= */
function band(R) {
  return R.axis === 'z' ? [[R.at, R.from], [R.at, R.to]] : [[R.from, R.at], [R.to, R.at]];
}

for (const R of ROADS) {
  const pts = band(R);
  lay(`road-${R.id}`, pts, R.w, R.y);
  if (R.id.startsWith('avenue')) {
    lay(`shoulder-${R.id}`, pts, R.w + SHOULDER_W * 2, Y.shoulder, { shoulder: true, ruts: false });
  }
}

/* 门前原来有两块圆形土面：一块是牌坊底下那个压土圈（js/world/gate.js 里，这一轮一起删了），
   一块就是这里的圆形前庭（`forecourt`，r=13.05，抬在 Y.plaza 上）。他这一轮点名"这两块地面圆形
   去掉，保持和别的一样" —— 那就是全场地面只剩地形 + 路带 + 路肩三层，圆面不再是一种地面语言。
   代价：辙印原来在这个圆里"散开"（不画车辙），现在从牌坊到主屋是一条等宽带子到底；
   前庭那个"到了门口要下马/停车"的空间暗示，只能靠牌坊本身和两侧树行的收口去表达了。

   再往上一轮，院子里被牲口踩实的那两块土板也删了（牛栏、羊圈全部是草地）——
   代价是参考图里"牛栏是一片尘土、和草场色差比路还大"那个特征没有了。 */
