// The advanced layout window: a picture goes in, an outline of it comes out, and the
// outline is what the aircraft are arranged on. This is how the real show tools work --
// a designer draws or uploads a logo, the tool traces it, samples N points along it at
// the spacing the safety rule allows, and the operator drags the result into the sky.
//
// What is *not* faked here: the trace is a real marching-squares contour of the image's
// own alpha/luminance mask, the resampling is real arc-length spacing, and the point
// count is the fleet size, so a 24-aircraft show cannot be shown a 200-point outline it
// has no airframes for.
//
// The one thing this window cannot do yet is put an outline on the radio: the SHOWPLAN
// record carries shape numbers, not point lists, so an image act is judged and flown in
// the browser and refused at the upload button with the reason on it. See the task on
// the board.
import * as THREE from 'three';

const MAX_SIDE = 220;              // the trace runs on a downscaled copy: 48k cells
let host = null;
let three = null;
let cloud = [];                    // local NED metres, the current outline
let params = { threshold: 150, mode: 'edge', scale: 24, alt: 45, drones: 24 };
// The window is opened from a plan, and it starts with that plan's fleet size so the
// outline it traces has one point per aircraft.
{
  const n = Number(new URLSearchParams(location.search).get('n'));
  if (n >= 1 && n <= 128) params.drones = Math.round(n);
}
const els = {};

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

// --- the mask ----------------------------------------------------------------
function maskOf(img) {
  const k = Math.min(1, MAX_SIDE / Math.max(img.width, img.height));
  const w = Math.max(2, Math.round(img.width * k));
  const h = Math.max(2, Math.round(img.height * k));
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  const g = c.getContext('2d', { willReadFrequently: true });
  g.drawImage(img, 0, 0, w, h);
  const px = g.getImageData(0, 0, w, h).data;
  const m = new Uint8Array(w * h);
  for (let i = 0; i < w * h; ++i) {
    const a = px[i * 4 + 3];
    // A transparent pixel is empty no matter how dark it is; a painted one is filled by
    // how far it is from white, because a logo on white paper has no alpha channel.
    const lum = a < 24 ? 255 : (px[i * 4] * 0.299 + px[i * 4 + 1] * 0.587 + px[i * 4 + 2] * 0.114);
    m[i] = lum < params.threshold ? 1 : 0;
  }
  return { m, w, h };
}

// --- marching squares, then chaining the segments into polylines --------------
function contours({ m, w, h }) {
  const at = (x, y) => (x < 0 || y < 0 || x >= w || y >= h ? 0 : m[y * w + x]);
  const segs = [];
  for (let y = 0; y < h - 1; ++y) {
    for (let x = 0; x < w - 1; ++x) {
      const tl = at(x, y), tr = at(x + 1, y), br = at(x + 1, y + 1), bl = at(x, y + 1);
      const idx = tl | (tr << 1) | (br << 2) | (bl << 3);
      if (idx === 0 || idx === 15) continue;
      const top = [x + 0.5, y], right = [x + 1, y + 0.5], bottom = [x + 0.5, y + 1], left = [x, y + 0.5];
      const put = (a, b) => segs.push([a, b]);
      switch (idx) {
        case 1: case 14: put(left, top); break;
        case 2: case 13: put(top, right); break;
        case 3: case 12: put(left, right); break;
        case 4: case 11: put(right, bottom); break;
        case 5: put(left, top); put(right, bottom); break;
        case 6: case 9: put(top, bottom); break;
        case 7: case 8: put(left, bottom); break;
        case 10: put(top, right); put(left, bottom); break;
        default: break;
      }
    }
  }
  // Chain by endpoint identity: the midpoints above are shared exactly, so a hash of
  // the two halves is enough and no epsilon search is needed.
  const key = p => `${p[0].toFixed(2)},${p[1].toFixed(2)}`;
  const byStart = new Map();
  for (const s of segs) {
    const k = key(s[0]);
    if (!byStart.has(k)) byStart.set(k, []);
    byStart.get(k).push(s);
  }
  const used = new Set();
  const lines = [];
  for (const s of segs) {
    if (used.has(s)) continue;
    const line = [s[0], s[1]];
    used.add(s);
    for (let guard = 0; guard < 20000; ++guard) {
      const tail = key(line[line.length - 1]);
      const next = (byStart.get(tail) || []).find(c => !used.has(c));
      if (!next) break;
      used.add(next);
      line.push(next[1]);
      if (key(next[1]) === key(line[0])) break;      // closed
    }
    lines.push(line);
  }
  return lines;
}

function lengthOf(line) {
  let L = 0;
  for (let i = 0; i + 1 < line.length; ++i) L += Math.hypot(line[i + 1][0] - line[i][0], line[i + 1][1] - line[i][1]);
  return L;
}

function resample(line, n) {
  const total = lengthOf(line);
  if (total <= 0 || n < 1) return [];
  const out = [];
  const step = total / n;
  let walked = 0, seg = 0, into = 0;
  for (let i = 0; i < n; ++i) {
    const want = i * step;
    while (seg + 1 < line.length) {
      const d = Math.hypot(line[seg + 1][0] - line[seg][0], line[seg + 1][1] - line[seg][1]);
      if (into + d > want - 1e-9) break;
      into += d; seg++;
    }
    const d = Math.hypot(line[seg + 1][0] - line[seg][0], line[seg + 1][1] - line[seg][1]) || 1;
    const t = Math.min(1, (want - into) / d);
    out.push([line[seg][0] + (line[seg + 1][0] - line[seg][0]) * t,
              line[seg][1] + (line[seg + 1][1] - line[seg][1]) * t]);
  }
  return out;
}

// Filled mode: every painted cell is a candidate, and the fleet is spread over them by
// farthest-point sampling -- the same reason a parametric shape spaces itself evenly.
function filled({ m, w, h }, n) {
  const all = [];
  for (let y = 0; y < h; ++y) for (let x = 0; x < w; ++x) if (m[y * w + x]) all.push([x + 0.5, y + 0.5]);
  if (!all.length || n < 1) return [];
  const pick = [all[0]];
  const best = new Float64Array(all.length).fill(Infinity);
  while (pick.length < n) {
    const last = pick[pick.length - 1];
    let bi = -1, bd = -1;
    for (let i = 0; i < all.length; ++i) {
      const d = Math.hypot(all[i][0] - last[0], all[i][1] - last[1]);
      if (d < best[i]) best[i] = d;
      if (best[i] > bd) { bd = best[i]; bi = i; }
    }
    if (bi < 0 || bd <= 0) break;
    pick.push(all[bi]);
    best[bi] = -1;
  }
  return pick;
}

// --- image -> NED cloud -------------------------------------------------------
function build(img) {
  const g = maskOf(img);
  const lines = contours(g).filter(l => l.length > 2).sort((a, b) => lengthOf(b) - lengthOf(a));
  const pts = params.mode === 'edge' ? [] : filled(g, params.drones);
  if (params.mode === 'edge') {
    if (!lines.length) { cloud = []; return; }
    // The fleet is split between the outlines in proportion to how long each one is, so
    // a logo with a thin ring around it does not put 23 aircraft on the ring and one on
    // the letter inside it.
    const total = lines.reduce((s, l) => s + lengthOf(l), 0);
    let left = params.drones;
    lines.forEach((l, i) => {
      const take = i === lines.length - 1 ? left : Math.max(1, Math.round(params.drones * lengthOf(l) / total));
      const n = Math.min(take, left);
      pts.push(...resample(l, n));
      left -= n;
    });
    if (left > 0 && lines.length) pts.push(...resample(lines[0], left));
  }
  const xs = pts.map(p => p[0]), ys = pts.map(p => p[1]);
  const cx = (Math.min(...xs) + Math.max(...xs)) / 2;
  const cy = (Math.min(...ys) + Math.max(...ys)) / 2;
  const span = Math.max(1, Math.max(Math.max(...xs) - Math.min(...xs), Math.max(...ys) - Math.min(...ys)));
  const k = (2 * params.scale) / span;
  // Image y grows downward; north grows toward the audience's left-hand side of the
  // picture as drawn, so the outline reads the same way up from the ground as on screen.
  cloud = pts.slice(0, params.drones).map(p => ({ n: (p[1] - cy) * k, e: (p[0] - cx) * k, d: -params.alt }));
}

// --- the 3D view --------------------------------------------------------------
function mountThree(host3d) {
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(52, 1, 0.1, 4000);
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(devicePixelRatio || 1);
  host3d.appendChild(renderer.domElement);
  const grid = new THREE.GridHelper(240, 24, 0x2c5f45, 0x16301f);
  scene.add(grid);
  const group = new THREE.Group();
  scene.add(group);
  let yaw = 0.5, pitch = 0.32, dist = 150;
  const place = () => {
    camera.position.set(Math.sin(yaw) * Math.cos(pitch) * dist, Math.sin(pitch) * dist, Math.cos(yaw) * Math.cos(pitch) * dist);
    camera.lookAt(0, 0, 0);
  };
  let drag = null;
  renderer.domElement.addEventListener('pointerdown', e => { drag = [e.clientX, e.clientY, yaw, pitch]; });
  window.addEventListener('pointerup', () => { drag = null; });
  window.addEventListener('pointermove', e => {
    if (!drag) return;
    yaw = drag[2] - (e.clientX - drag[0]) * 0.006;
    pitch = Math.max(-0.2, Math.min(1.3, drag[3] + (e.clientY - drag[1]) * 0.005));
  });
  renderer.domElement.addEventListener('wheel', e => {
    e.preventDefault();
    dist = Math.max(30, Math.min(600, dist * (e.deltaY > 0 ? 1.1 : 0.9)));
  }, { passive: false });
  const resize = () => {
    const r = host3d.getBoundingClientRect();
    renderer.setSize(r.width, r.height, false);
    camera.aspect = r.width / Math.max(1, r.height);
    camera.updateProjectionMatrix();
  };
  const paint = () => {
    group.children.length = 0;
    if (!cloud.length) return;
    const geo = new THREE.BufferGeometry();
    // NED to the scene: east is x, up is -d, north is -z -- the same mapping the fleet
    // uses, so what is traced here is what flies there.
    const arr = new Float32Array(cloud.length * 3);
    cloud.forEach((p, i) => { arr[i * 3] = p.e; arr[i * 3 + 1] = -p.d; arr[i * 3 + 2] = -p.n; });
    geo.setAttribute('position', new THREE.BufferAttribute(arr, 3));
    group.add(new THREE.Points(geo, new THREE.PointsMaterial({ color: 0x6ee7a0, size: 2.6, sizeAttenuation: true })));
    if (params.mode === 'edge') {
      const line = new THREE.BufferGeometry();
      line.setAttribute('position', new THREE.BufferAttribute(arr.slice(), 3));
      group.add(new THREE.Line(line, new THREE.LineBasicMaterial({ color: 0x2f8f63, transparent: true, opacity: .6 })));
    }
  };
  const tick = () => { place(); resize(); renderer.render(scene, camera); requestAnimationFrame(tick); };
  tick();
  return { paint };
}

// --- the window ---------------------------------------------------------------
function field(label, value, min, max, step, onInput) {
  const row = el('label', 'sd-field');
  row.appendChild(el('i', null, label));
  const i = el('input', 'sd-input');
  i.type = 'number'; i.min = min; i.max = max; i.step = step; i.value = value;
  i.addEventListener('input', () => { onInput(Number(i.value)); });
  row.appendChild(i);
  return row;
}

function modePicker() {
  const wrap = el('div', 'sp-pick');
  const btn = el('button', 'sp-pick-btn');
  btn.type = 'button';
  const list = el('div', 'sp-pick-list');
  const names = { edge: '轮廓', fill: '填充' };
  const paint = () => {
    btn.textContent = names[params.mode];
    list.textContent = '';
    for (const k of Object.keys(names)) {
      const row = el('div', 'sp-pick-opt' + (k === params.mode ? ' sel' : ''), names[k]);
      row.addEventListener('click', e => {
        e.stopPropagation();
        params.mode = k;
        wrap.classList.remove('open');
        repaint();
        retrace();
      });
      list.appendChild(row);
    }
  };
  btn.addEventListener('click', e => {
    e.stopPropagation();
    const was = wrap.classList.contains('open');
    document.querySelectorAll('.sp-pick.open').forEach(n => n.classList.remove('open'));
    if (!was) {
      const r = btn.getBoundingClientRect();
      list.style.position = 'fixed';
      list.style.left = `${Math.round(r.left)}px`;
      list.style.top = `${Math.round(r.bottom + 4)}px`;
      list.style.minWidth = `${Math.round(r.width)}px`;
      wrap.classList.add('open');
    }
    paint();
  });
  wrap.appendChild(btn); wrap.appendChild(list);
  paint();
  els.mode = wrap;
  return wrap;
}

let lastImage = null;
function retrace() {
  if (!lastImage) return;
  build(lastImage);
  three.paint();
  els.count.textContent = `${cloud.length} / ${params.drones} 个点已摆好 · 图 ${lastImage.width}×${lastImage.height} px`;
}

function repaint() {
  if (els.mode) els.mode.querySelector('.sp-pick-btn').textContent = { edge: '轮廓', fill: '填充' }[params.mode];
}

function mount(target) {
  if (host) return;
  host = el('div', 'show-design');
  const head = el('div', 'sd-head');
  head.appendChild(el('h2', null, '✦ 图片轮廓编排'));
  const note = el('small', 'sd-note', '上传图片 → 描出轮廓 → 按机数均分点位 → 送进节目单');
  head.appendChild(note);
  host.appendChild(head);

  const body = el('div', 'sd-body');
  const col = el('div', 'sd-col');
  const drop = el('div', 'sd-drop', '点这里选图片，或把图片拖进来');
  const file = el('input', 'sd-file');
  file.type = 'file';
  file.accept = 'image/*';
  file.addEventListener('change', () => load(file.files && file.files[0]));
  drop.addEventListener('click', () => file.click());
  drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('hot'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('hot'));
  drop.addEventListener('drop', e => {
    e.preventDefault();
    drop.classList.remove('hot');
    load(e.dataTransfer.files && e.dataTransfer.files[0]);
  });
  col.appendChild(drop);
  col.appendChild(file);
  col.appendChild(field('阈值', params.threshold, 10, 250, 5, v => { params.threshold = v; retrace(); }));
  const modeRow = el('label', 'sd-field');
  modeRow.appendChild(el('i', null, '取点'));
  modeRow.appendChild(modePicker());
  col.appendChild(modeRow);
  col.appendChild(field('尺寸 (m)', params.scale, 4, 90, 1, v => { params.scale = v; retrace(); }));
  col.appendChild(field('高度 (m)', params.alt, 10, 120, 1, v => { params.alt = v; retrace(); }));
  col.appendChild(field('机数', params.drones, 1, 128, 1, v => { params.drones = Math.round(v); retrace(); }));
  els.count = el('div', 'sd-count', '还没有图片');
  col.appendChild(els.count);
  const send = el('button', 'sd-btn sd-send', '送进节目单');
  send.addEventListener('click', () => {
    if (cloud.length < 2) { els.count.textContent = '先描出一条至少两个点的轮廓'; return; }
    if (!window.opener) { els.count.textContent = '这个窗口是自己开的，没有节目单可写'; return; }
    window.opener.postMessage({ type: 'show-outline', points: cloud, alt: params.alt, scale: params.scale },
      location.origin);
    els.count.textContent = `已送回 ${cloud.length} 个点：回到编排窗口看那一幕`;
  });
  col.appendChild(send);
  body.appendChild(col);

  const stage = el('div', 'sd-stage');
  body.appendChild(stage);
  host.appendChild(body);
  target.appendChild(host);
  three = mountThree(stage);
}

function load(file) {
  if (!file || !/^image\//.test(file.type)) { els.count.textContent = '那不是图片'; return; }
  const url = URL.createObjectURL(file);
  const img = new Image();
  img.onload = () => {
    URL.revokeObjectURL(url);
    lastImage = img;
    els.count.textContent = `读入 ${img.width}×${img.height} px`;
    retrace();
  };
  img.onerror = () => { els.count.textContent = '图片读不出来'; URL.revokeObjectURL(url); };
  img.src = url;
}

export function openDesigner() { mount(document.getElementById('app')); }
