// The sky: one graded dome instead of the flat colour the scene used to paint behind
// everything. The same shader carries the sun, the moon, the stars and the cloud layer, and
// it hands the horizon colour back to the fog so the far hills dissolve into the sky
// instead of ending on a hard line.
import * as THREE from 'three';
import { scene } from './scene.js';

// Larger than the land (HAZE_R) and inside the camera far plane; the dome is the
// backdrop for a range that now sits a thousand metres out rather than four hundred.
export const SKY_RADIUS = 2200;

const VERT = /* glsl */`
varying vec3 vDir;
void main(){
  vec4 wp = modelMatrix * vec4(position, 1.0);
  vDir = wp.xyz - cameraPosition;
  gl_Position = projectionMatrix * viewMatrix * wp;
}
`;

const FRAG = /* glsl */`
varying vec3 vDir;

uniform vec3  uTop;
uniform vec3  uHorizon;
uniform vec3  uGround;
uniform vec3  uSunDir;
uniform vec3  uSunCol;
uniform vec3  uMoonDir;
uniform vec3  uMoonCol;
uniform vec3  uCloudCol;
uniform float uSunVis;
uniform float uMoonVis;
uniform float uStars;
uniform float uCloudAmt;
uniform float uTime;

float h21(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float h31(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123); }

float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x),
             mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm4(vec2 p){
  float s = 0.0, a = 0.55;
  for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p *= 2.07; a *= 0.5; }
  return s;
}

// GLSL leaves pow(0.0, y) undefined. On a real driver it returns 0; on SwiftShader it
// returns NaN, which then propagates through the mix and paints the whole upper hemisphere
// black -- which is exactly what it did here. Every power in this shader goes through pw().
float pw(float x, float e){ return pow(max(x, 1e-6), e); }

void main(){
  vec3  d   = normalize(vDir);
  float up  = max(d.y, 0.0);
  float dn  = max(-d.y, 0.0);

  // Zenith to horizon, then into the haze that sits over the land below the horizon.
  // The exponent is deliberately small: at the overview pitch only the band from d.y=0 to
  // about d.y=0.19 is on screen, and a 0.42 falloff keeps all of it within a few percent of
  // the horizon colour, which is what made the first version look like flat grey again.
  vec3 col = mix(uHorizon, uTop, pw(up, 0.2));
  col = mix(col, uGround, pw(min(dn * 2.5, 1.0), 0.7));

  if (uStars > 0.001) {
    vec3  sp   = d * 150.0;
    vec3  cell = floor(sp);
    float r    = h31(cell);
    if (r > 0.9775) {
      float dd  = length(sp - (cell + 0.5));
      float mag = 0.22 + 0.78 * h31(cell + 3.7);
      float s   = smoothstep(0.5, 0.02, dd) * mag * uStars * smoothstep(0.0, 0.03, d.y);
      col += vec3(0.88, 0.92, 1.0) * s * 3.6;
    }
  }

  if (uCloudAmt > 0.001 && d.y > 0.012) {
    // Clouds are painted onto an imaginary plane over the valley, which is what makes them
    // squash toward the horizon the way a real overcast does. One fbm call, not two: the
    // sky covers most of the frame when the camera is low and this runs per pixel.
    vec2  cp = d.xz / d.y * 0.052 + vec2(uTime * 0.0032, uTime * 0.0011);
    float n  = fbm4(cp);
    float c  = smoothstep(0.56, 0.90, n);
    c *= smoothstep(0.012, 0.26, d.y);
    col = mix(col, uCloudCol, c * uCloudAmt);
  }

  float sd = max(dot(d, normalize(uSunDir)), 0.0);
  col += uSunCol * smoothstep(0.99993, 0.999975, sd) * 1.35 * uSunVis;
  col += uSunCol * pw(sd, 260.0) * 0.22 * uSunVis;
  col += uSunCol * pw(sd, 14.0) * 0.085 * uSunVis;
  col += uSunCol * pw(sd, 3.0) * 0.030 * uSunVis;

  float md = max(dot(d, normalize(uMoonDir)), 0.0);
  col += uMoonCol * smoothstep(0.99991, 0.999965, md) * 1.15 * uMoonVis;
  col += uMoonCol * pw(md, 900.0) * 0.16 * uMoonVis;
  col += uMoonCol * pw(md, 26.0) * 0.045 * uMoonVis;

  gl_FragColor = vec4(col, 1.0);
  #include <colorspace_fragment>
}
`;

export const u = {
  Top: { value: new THREE.Color(0x0a1428) },
  Horizon: { value: new THREE.Color(0x16324a) },
  Ground: { value: new THREE.Color(0x081116) },
  SunDir: { value: new THREE.Vector3(0, 1, 0) },
  SunCol: { value: new THREE.Color(0xfff3d6) },
  MoonDir: { value: new THREE.Vector3(0, 1, 0) },
  MoonCol: { value: new THREE.Color(0xd8e6f6) },
  CloudCol: { value: new THREE.Color(0x9fb0bd) },
  SunVis: { value: 0 },
  MoonVis: { value: 1 },
  Stars: { value: 1 },
  CloudAmt: { value: 0.34 },
  Time: { value: 0 },
};

// The shader's uniforms are spelled uTop, uHorizon, ... and this object's keys are Top,
// Horizon, ... -- three binds by name, so handing the material `u` directly left every one of
// them unbound and the dome painted the GL default: flat black, day and night alike. That is
// the black band above the hills in every screenshot this project has ever taken.
//
// The keys stay unprefixed because js/world/river.js reads skyU.Horizon and js/state/time.js
// writes u.Top.value; the material gets a map onto the SAME wrapper objects, so a mutation
// through either name is seen by the shader.
const matUniforms = Object.fromEntries(Object.entries(u).map(([k, v]) => ['u' + k, v]));

const skyMat = new THREE.ShaderMaterial({
  uniforms: matUniforms,
  vertexShader: VERT,
  fragmentShader: FRAG,
  side: THREE.BackSide,
  depthWrite: false,
  fog: false,
});

export const skyDome = new THREE.Mesh(new THREE.SphereGeometry(SKY_RADIUS, 40, 24), skyMat);
skyDome.renderOrder = -1;
skyDome.frustumCulled = false;
scene.add(skyDome);

// The two moods. Everything the day/night switch used to set by hand on scene.background
// now comes from here, and the fog reads its colour off the same numbers so the seam
// between land and sky is the haze rather than an edge.
const PALETTE = {
  day: {
    top: 0x3a76bd,
    // Late afternoon: the haze over a sunlit valley is warm, not the pale cold blue a
    // midday model gives you, and the fog colour is copied from this same number so the
    // hills dissolve into the same air the sky is made of.
    horizon: 0xd6c7ac,
    sun: 0xffe9c4,
    cloud: 0xf6ecdd,
    cloudAmt: 0.34,
    stars: 0,
    sunVis: 1,
    moonVis: 0,
    fogNear: 320,
    fogFar: 1600,
  },
  night: {
    top: 0x081020,
    horizon: 0x27415c,
    sun: 0xfff4da,
    cloud: 0x40566e,
    cloudAmt: 0.30,
    stars: 1,
    sunVis: 0,
    moonVis: 1,
    fogNear: 380,
    fogFar: 1600,
  },
};

export function applySky(mode, sunPosition) {
  const p = PALETTE[mode === 'day' ? 'day' : 'night'];

  u.Top.value.setHex(p.top);
  u.Horizon.value.setHex(p.horizon);
  // The land below the horizon is painted with the fog colour on purpose: the terrain stops
  // at the haze line, and anything past it has to be exactly the colour the hills fade to or
  // the edge of the map reads as a ring around the world.
  u.Ground.value.copy(u.Horizon.value);
  u.SunCol.value.setHex(p.sun);
  u.CloudCol.value.setHex(p.cloud);
  u.CloudAmt.value = p.cloudAmt;
  u.Stars.value = p.stars;
  u.SunVis.value = p.sunVis;
  u.MoonVis.value = p.moonVis;

  if (sunPosition) u.SunDir.value.copy(sunPosition);
  // The directional light is the sun by day and the moon by night, so the disc in the sky
  // and the shadows on the ground are always arguing about the same body.
  if (mode === 'day') u.MoonDir.value.copy(u.SunDir.value).multiplyScalar(-1);
  else u.MoonDir.value.copy(sunPosition || u.SunDir.value);

  scene.fog.color.copy(u.Horizon.value);
  scene.fog.near = p.fogNear;
  scene.fog.far = p.fogFar;
  scene.background = null;
  skyDome.visible = true;

  return p;
}

export function horizonColor() {
  return u.Horizon.value.clone();
}

export function advanceSky(dt) {
  u.Time.value += dt;
}
