// The water: a ribbon laid along the channel core/ground.js carved for it.
//
// It is a hand-written shader rather than a MeshStandardMaterial because there is no
// environment map in this scene -- the sky is a dome, not a cubemap -- and a low-roughness
// standard material with nothing to reflect just goes black. So the surface mixes the sky's
// own horizon colour in by Fresnel and puts a specular streak along the sun, and it fades
// with the same smoothstep three's linear fog uses so the river hazes out exactly like the
// hills beside it.
import * as THREE from 'three';
import { scene } from '../core/scene.js';
import { u as skyU } from '../core/sky.js';
import { riverSamples, waterY, riverHalfWidth } from '../core/ground.js';

const VERT = /* glsl */`
varying vec3 vW;
void main(){
  vec4 wp = modelMatrix * vec4(position, 1.0);
  vW = wp.xyz;
  gl_Position = projectionMatrix * viewMatrix * wp;
}
`;

const FRAG = /* glsl */`
varying vec3 vW;

uniform vec3  uSky;
uniform vec3  uDeep;
uniform vec3  uSunCol;
uniform vec3  uSunDir;
uniform vec3  uFogColor;
uniform float uTime;
uniform float uSunVis;
uniform float uFogNear;
uniform float uFogFar;

// pow(0.0, y) is undefined in GLSL and SwiftShader answers NaN, which paints the whole
// surface black. Every power goes through pw().
float pw(float x, float e){ return pow(max(x, 1e-6), e); }

void main(){
  // Two crossed wave trains: enough to break the reflection into a moving sheen without
  // reading as wallpaper.
  vec3 n = normalize(vec3(
      0.055 * sin(vW.x * 0.62 + uTime * 1.70) + 0.036 * sin(vW.z * 1.31 - uTime * 1.05),
      1.0,
      0.055 * sin(vW.z * 0.71 - uTime * 1.35) + 0.036 * sin(vW.x * 1.13 + uTime * 0.85)));

  vec3  V = normalize(cameraPosition - vW);
  float fres = pw(1.0 - clamp(dot(V, n), 0.0, 1.0), 3.0);
  vec3  col = mix(uDeep, uSky, clamp(0.06 + fres * 0.95, 0.0, 1.0));

  vec3  H = normalize(V + normalize(uSunDir));
  float ndh = max(dot(n, H), 0.0);
  col += uSunCol * pw(ndh, 260.0) * 1.6 * uSunVis;
  col += uSunCol * pw(ndh, 16.0) * 0.10 * uSunVis;

  float d = length(cameraPosition - vW);
  float f = clamp((d - uFogNear) / max(uFogFar - uFogNear, 1.0), 0.0, 1.0);
  f = f * f * (3.0 - 2.0 * f);
  col = mix(col, uFogColor, f);

  gl_FragColor = vec4(col, mix(0.90, 1.0, fres));
  #include <colorspace_fragment>
}
`;

const uniforms = {
  uSky: { value: new THREE.Color(0x16324a) },
  uDeep: { value: new THREE.Color(0x16323c) },
  uSunCol: { value: new THREE.Color(0xfff3d6) },
  uSunDir: { value: new THREE.Vector3(0, 1, 0) },
  uFogColor: { value: new THREE.Color(0x16324a) },
  uTime: { value: 0 },
  uSunVis: { value: 0 },
  uFogNear: { value: 170 },
  uFogFar: { value: 820 },
};

const waterMat = new THREE.ShaderMaterial({
  uniforms,
  vertexShader: VERT,
  fragmentShader: FRAG,
  transparent: true,
  depthWrite: false,
});

function build() {
  const pos = [];
  const idx = [];
  const pts = riverSamples;

  for (let i = 0; i < pts.length; ++i) {
    const p = pts[i];
    const a = pts[Math.max(0, i - 1)];
    const b = pts[Math.min(pts.length - 1, i + 1)];
    let tx = b.x - a.x;
    let tz = b.z - a.z;
    const tl = Math.hypot(tx, tz) || 1;
    tx /= tl;
    tz /= tl;

    const hw = riverHalfWidth(p.s) * 0.94;
    const y = waterY(p.s) + 0.05;

    pos.push(p.x - tz * hw, y, p.z + tx * hw);
    pos.push(p.x + tz * hw, y, p.z - tx * hw);

    if (i < pts.length - 1) {
      const k = i * 2;
      idx.push(k, k + 2, k + 3, k, k + 3, k + 1);
    }
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

export const river = new THREE.Mesh(build(), waterMat);
river.name = 'river';
river.renderOrder = 2;
scene.add(river);

export function advanceWater(dt) {
  uniforms.uTime.value += dt;
  // The water reflects the sky without an environment map, so it borrows the horizon
  // colour -- at three quarters strength, because a mirror-bright river reads as wet
  // concrete at this viewing angle.
  // 0.72 of the horizon colour was right when the horizon was a cold pale blue. The day sky
  // is now a warm haze over a sunlit valley, and mirroring it at that strength turned the
  // river into a cream ribbon: a lowland river reads blue because it is deep and it
  // scatters, not because it is a mirror. Halved, with a bluer deep tone.
  uniforms.uSky.value.copy(skyU.Horizon.value).multiplyScalar(0.40);
  uniforms.uFogColor.value.copy(skyU.Horizon.value);
  uniforms.uSunCol.value.copy(skyU.SunCol.value);
  uniforms.uSunDir.value.copy(skyU.SunVis.value > 0.5 ? skyU.SunDir.value : skyU.MoonDir.value);
  uniforms.uSunVis.value = Math.max(skyU.SunVis.value, skyU.MoonVis.value * 0.45);
  uniforms.uFogNear.value = scene.fog.near;
  uniforms.uFogFar.value = scene.fog.far;
  uniforms.uDeep.value.setHex(skyU.SunVis.value > 0.5 ? 0x155063 : 0x0c1c26);
}
