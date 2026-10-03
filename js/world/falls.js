// The fall. js/core/ground.js carves a channel whose water surface steps from the caprock
// bench down to the valley at one station; a step in a *carved* surface is only ever as steep
// as the terrain mesh, and the mesh is sampled every twelve metres or so, so the carving alone
// gives you a chute, not a waterfall. This module puts the thing the water actually falls over
// on top of that station: the sheet of water, the pool it lands in, and the mist.
//
// Everything is read from ground.js's fallGeometry(), which is derived from the same curve the
// channel was carved from. Placing the cliff by eye instead would mean the water, the rock and
// the gorge could drift apart -- and the drift would be invisible in a still frame.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { fallGeometry } from '../core/ground.js';
import { u as skyU } from '../core/sky.js';

const F = fallGeometry();

// Downstream unit vector and the cliff line that runs across it.
const TX = -F.nz, TZ = -F.nx;
const SPAN = Math.max(F.width * 1.7, 34);

/* =========================================================
   GEOMETRY HELPERS
   A curved band across the gorge: for each station along the cliff line, one vertical quad
   from y0(u) to y1(u). Used for the wall, the lip shelf and the water sheet, so all three
   agree on where the edge of the world is.

   uvu / uvv are METRES PER TEXTURE REPEAT on each axis, which is the only way a bedding plane
   can be made to come out the same physical thickness on a 45 m wall and on a 27 m sheet.
   The alternative -- a repeat count -- ties the feature size to the size of the mesh, and a
   band that is 2 m thick on the cliff becomes 0.2 m on the sheet, which is under a pixel and
   shatters as soon as the view turns.
========================================================= */

function band({ u0, u1, segs, yOf, yOfTop, push = 0, uvu = 12, uvv = 12 }) {
  const span = Math.abs(u1 - u0) || 1;
  const pos = [], uv = [], idx = [];
  for (let i = 0; i <= segs; ++i) {
    const t = i / segs;
    const u = u0 + (u1 - u0) * t;
    // bow the line slightly upstream so the cliff is not a flat sheet
    const bow = -0.02 * (u * u) / SPAN;
    const cx = F.x + TX * u - F.nx * bow + push * F.nx;
    const cz = F.z + TZ * u - F.nz * bow + push * F.nz;
    const yb = yOf(t, u), yt = yOfTop(t, u);
    pos.push(cx, yb, cz, cx, yt, cz);
    uv.push(t * span / uvu, 0, t * span / uvu, (yt - yb) / uvv);
    if (i < segs) {
      const k = i * 2;
      idx.push(k, k + 1, k + 2, k + 1, k + 3, k + 2);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

/* ---- no rock wall here, on purpose ----
   The first version built a rectangular sandstone panel across the gorge and it floated: its
   bottom sat at the pool level while the carved channel floor at the lip was eleven metres
   higher, so from the gate it read as a billboard propped in front of a cliff. The land
   already IS the cliff -- js/core/ground.js shoals the channel across the fall and turns the
   chute into a 74-degree face, and the same file now colours that face sandstone. What is
   left to draw is the water.
/* =========================================================
   THE SHEET
   Water going over a lip is not a solid colour and it is not transparent either: it is
   streaks, and the streaks move. Vertical UV scrolling with a soft alpha is the cheapest
   thing that reads as that, and it stays out of the depth fight the river surface is already
   in with the terrain under it -- so depthWrite is off and it is drawn after the water.

   Nine streaks across a twenty-seven metre fall, not sixty: the sheet is a few tens of pixels
   wide from the overview, and a repeating feature denser than about one cycle per four pixels
   is the thing this scene has already been measured flickering on.
========================================================= */

const sheetH = F.top - F.bottom;

const sheetMat = new THREE.ShaderMaterial({
  transparent: true,
  depthWrite: false,
  side: THREE.DoubleSide,
  uniforms: {
    uTime: { value: 0 },
    uStreak: { value: 0.5 },
    uFogColor: { value: new THREE.Color(0x16324a) },
    uFogNear: { value: 300 },
    uFogFar: { value: 1500 },
    uLight: { value: new THREE.Color(0xfff4da) },
  },
  vertexShader: /* glsl */`
    varying vec3 vW; varying vec2 vUv;
    void main(){
      vec4 wp = modelMatrix * vec4(position, 1.0);
      vW = wp.xyz; vUv = uv;
      gl_Position = projectionMatrix * viewMatrix * wp;
    }`,
  fragmentShader: /* glsl */`
    varying vec3 vW; varying vec2 vUv;
    uniform float uTime, uStreak;
    uniform vec3 uFogColor, uLight;
    uniform float uFogNear, uFogFar;

    float h(float x){ return fract(sin(x * 91.7) * 43758.545); }

    void main(){
      // uv.y is 1 at the lip and 0 at the pool, so 'down' is how far the water has fallen.
      float down = 1.0 - clamp(vUv.y, 0.0, 1.0);
      float s = 0.0;
      s += smoothstep(0.40, 0.92, fract(vUv.x * 9.0 + h(floor(vUv.x * 9.0)) * 3.0 + uTime * 1.9));
      s += 0.7 * smoothstep(0.48, 0.95, fract(vUv.x * 5.0 + h(floor(vUv.x * 5.0)) * 5.0 + uTime * 1.35));
      s += 0.45 * smoothstep(0.60, 0.98, fract(vUv.x * 3.0 + uTime * 0.9));
      s = clamp(s, 0.0, 1.0);

      // It leaves the lip as a sheet and arrives as a spray: wider, softer and brighter on
      // the way down.
      float spread = 0.55 + 0.45 * down;
      float a = (0.30 + 0.42 * s) * spread;
      a *= 1.0 - smoothstep(0.86, 1.0, down) * 0.5;   // feather into the mist at the bottom

      vec3 col = mix(vec3(0.86, 0.90, 0.93), vec3(1.0), s) * uLight;
      col = mix(col, uFogColor, clamp((length(vW - cameraPosition) - uFogNear) / (uFogFar - uFogNear), 0.0, 1.0));
      gl_FragColor = vec4(col, clamp(a * uStreak, 0.0, 0.92));
      #include <colorspace_fragment>
    }`,
});

const sheet = new THREE.Mesh(
  band({
    u0: -F.width * 0.52, u1: F.width * 0.52, segs: 12,
    yOf: () => F.bottom,
    yOfTop: () => F.top,
    push: 0.4, uvu: F.width, uvv: sheetH,
  }),
  sheetMat
);
sheet.name = 'fall-sheet';
sheet.renderOrder = 3;
scene.add(sheet);

/* ---- the plunge pool: brighter than the river, because it is full of air ---- */
const pool = new THREE.Mesh(
  new THREE.CircleGeometry(F.width * 0.95, 26),
  new THREE.MeshStandardMaterial({ color: 0xdfe9ea, roughness: 0.35, transparent: true, opacity: 0.85 })
);
pool.rotation.x = -Math.PI / 2;
pool.position.set(F.x + F.nx * F.width * 0.35, F.bottom + 0.5, F.z + F.nz * F.width * 0.35);
pool.scale.set(1.25, 1, 1);
pool.name = 'plunge-pool';
scene.add(pool);

/* ---- mist: three soft billboards that never resolve into a shape ---- */
const mistMat = new THREE.SpriteMaterial({
  color: 0xe8f0f2,
  transparent: true,
  opacity: 0.34,
  depthWrite: false,
  map: (() => {
    const c = document.createElement('canvas');
    c.width = c.height = 128;
    const g = c.getContext('2d');
    const rad = g.createRadialGradient(64, 64, 4, 64, 64, 62);
    rad.addColorStop(0, 'rgba(255,255,255,0.95)');
    rad.addColorStop(0.55, 'rgba(255,255,255,0.35)');
    rad.addColorStop(1, 'rgba(255,255,255,0)');
    g.fillStyle = rad;
    g.fillRect(0, 0, 128, 128);
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    return t;
  })(),
});

const mist = [];
for (let i = 0; i < 5; ++i) {
  const s = new THREE.Sprite(mistMat.clone());
  const u = (i - 2) * F.width * 0.42;
  s.position.set(F.x + TX * u + F.nx * F.width * 0.5, F.bottom + 4 + i % 2 * 6,
    F.z + TZ * u + F.nz * F.width * 0.5);
  s.scale.setScalar(34 + (i % 3) * 13);
  s.renderOrder = 4;
  s.name = 'mist';
  scene.add(s);
  mist.push(s);
}

export const fallSummary = { ...F };

export function advanceFalls(dt, t) {
  sheetMat.uniforms.uTime.value += dt;
  sheetMat.uniforms.uStreak.value = 0.62 + 0.38 * (skyU.SunVis.value > 0.5 ? 1 : 0.55);
  sheetMat.uniforms.uFogColor.value.copy(skyU.Horizon.value);
  mist.forEach((m, i) => {
    m.material.opacity = 0.24 + 0.12 * (0.5 + 0.5 * Math.sin(t * 0.7 + i * 1.9));
  });
}
