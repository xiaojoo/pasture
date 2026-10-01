// The layout window: pick a preset 3D model (or trace a picture), get one point per
// aircraft, then drag any of those points to where you actually want that aircraft.
//
// Two things this window is careful about, because both were wrong before:
//
//  * The cloud is genuinely three-dimensional. The model generators in `show/models.js`
//    return a height per point, and the readout prints the shape's own vertical extent,
//    so "flat" and "3D" are numbers on screen rather than an impression from a picture.
//    The traced-outline path stays flat and says 平面 on the row that offers it -- an
//    outline lifted out of a picture has no third dimension to give it.
//  * A dragged point belongs to one aircraft. Points are assigned to stations in order,
//    so moving one must not reshuffle the fleet underneath it; `movePoint` copies and
//    replaces a single index for exactly that reason.
//
// What still cannot go over the radio: the SHOWPLAN record carries shape *numbers*, not
// point lists, so a model act is judged and flown in the browser and refused at the
// upload button with the reason on it.
import * as THREE from 'three';
import { MODELS, modelCloud, cloudSpan, minSpacing, movePoint, clampPoint, rayPlane, toScene, fromScene, ALT_MIN, ALT_MAX }
  from '../show/models.js';

const MAX_SIDE = 220;              // the trace runs on a downscaled copy: 48k cells
let host = null;
let three = null;
let cloud = [];                    // local NED metres, the current outline
let moved = new Set();             // stations the operator has dragged
let params = { source: 'model', model: 'sphere', threshold: 150, mode: 'edge', scale: 24, alt: 45, drones: 24 };
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
      const tl = at(x, y), tr = at(x + 1, y), br = at(x + 1, y + 1), bl = at(x + 1, y + 1);
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

// --- -> NED cloud -------------------------------------------------------------
function build() {
  moved = new Set();
  if (params.source === 'model') {
    cloud = modelCloud(params.model, params.drones, params.scale, params.alt);
    return;
  }
  if (!lastImage) { cloud = []; return; }
  const g = maskOf(lastImage);
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
  cloud = pts.slice(0, params.drones).map(p => ({ n: (p[1] - cy) * k, e: (p[0] - cx) * k, d: -params.alt }));
}

// --- the 3D view, with draggable stations ------------------------------------
function mountThree(host3d) {
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(52, 1, 0.1, 4000);
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(devicePixelRatio || 1);
  host3d.appendChild(renderer.domElement);
  scene.add(new THREE.GridHelper(240, 24, 0x2c5f45, 0x16301f));
  const group = new THREE.Group();
  scene.add(group);
  let yaw = 0.5, pitch = 0.32, dist = 150;
  // The eye looks at the middle of the formation, not at the ground origin: a show at
  // 45 m with its apex at 69 m was cut off the top of the view, and a station you
  // cannot see is a station you cannot drag.
  const target = new THREE.Vector3();
  const place = () => {
    camera.position.set(target.x + Math.sin(yaw) * Math.cos(pitch) * dist,
                        target.y + Math.sin(pitch) * dist,
                        target.z + Math.cos(yaw) * Math.cos(pitch) * dist);
    camera.lookAt(target);
  };
  const fit = () => {
    target.set(0, 0, 0);
    let r = 0;
    if (cloud.length) {
      const c = [0, 0, 0];
      for (const p of cloud) { const s = toScene(p); c[0] += s[0]; c[1] += s[1]; c[2] += s[2]; }
      c[0] /= cloud.length; c[1] /= cloud.length; c[2] /= cloud.length;
      target.set(c[0], c[1], c[2]);
      for (const p of cloud) {
        const s = toScene(p);
        r = Math.max(r, Math.hypot(s[0] - c[0], s[1] - c[1], s[2] - c[2]));
      }
    }
    // Fit the bounding sphere to ~80% of the vertical field of view, so a model swap
    // or an altitude change keeps every aircraft in frame.
    const half = Math.tan(camera.fov * Math.PI / 360);
    dist = Math.max(12, Math.min(600, r / (half * 0.8) || 12));
  };

  // Orbit by default; drag a station when the pointer lands on one. The two have to
  // agree in one place, or grabbing an aircraft would also spin the model.
  let orbit = null;
  let drag = null;
  const ray = new THREE.Raycaster();
  const ndc = new THREE.Vector2();
  const handles = [];

  const toNdc = e => {
    const r = renderer.domElement.getBoundingClientRect();
    ndc.set(((e.clientX - r.left) / Math.max(1, r.width)) * 2 - 1,
            -((e.clientY - r.top) / Math.max(1, r.height)) * 2 + 1);
    ray.setFromCamera(ndc, camera);
  };

  renderer.domElement.addEventListener('pointerdown', e => {
    toNdc(e);
    const hit = handles.length ? ray.intersectObjects(handles, false)[0] : null;
    if (hit) {
      drag = { i: hit.object.userData.i, n0: cloud[hit.object.userData.i].n,
               e0: cloud[hit.object.userData.i].e, d0: cloud[hit.object.userData.i].d };
      renderer.domElement.setPointerCapture(e.pointerId);
      return;
    }
    orbit = [e.clientX, e.clientY, yaw, pitch];
  });
  window.addEventListener('pointerup', () => { orbit = null; drag = null; });
  window.addEventListener('pointermove', e => {
    if (drag) {
      toNdc(e);
      // Drag in the screen plane through the point: one gesture reaches both sideways
      // and up, which is what a designer's hand expects. A second axis control would be
      // two chances to get it wrong.
      const nrm = new THREE.Vector3(0, 0, -1).applyQuaternion(camera.quaternion);
      const p = cloud[drag.i];
      const s = toScene(p);
      const hit = rayPlane(ray.ray.origin.x, ray.ray.origin.y, ray.ray.origin.z,
        ray.ray.direction.x, ray.ray.direction.y, ray.ray.direction.z,
        s[0], s[1], s[2], nrm.x, nrm.y, nrm.z);
      if (!hit) return;                     // parallel to the plane: no answer, not a wrong one
      cloud = movePoint(cloud, drag.i, fromScene(hit[0], hit[1], hit[2]));
      moved.add(drag.i);
      paint();
      // The readout and the undo button follow the drag: a point that turns amber but
      // leaves 「撤掉所有手动点」 disabled is a change you can see and cannot take back.
      status();
      return;
    }
    if (!orbit) return;
    yaw = orbit[2] - (e.clientX - orbit[0]) * 0.006;
    pitch = Math.max(-0.2, Math.min(1.3, orbit[3] + (e.clientY - orbit[1]) * 0.005));
  });
  renderer.domElement.addEventListener('wheel', e => {
    e.preventDefault();
    dist = Math.max(12, Math.min(600, dist * (e.deltaY > 0 ? 1.1 : 0.9)));
  }, { passive: false });

  const resize = () => {
    const r = host3d.getBoundingClientRect();
    renderer.setSize(r.width, r.height, false);
    camera.aspect = r.width / Math.max(1, r.height);
    camera.updateProjectionMatrix();
  };

  const paint = () => {
    group.children.length = 0;
    handles.length = 0;
    if (!cloud.length) return;
    const geo = new THREE.BufferGeometry();
    // NED to the scene: east is x, up is -d, north is -z -- the same mapping the fleet
    // uses, so what is arranged here is what flies there.
    const arr = new Float32Array(cloud.length * 3);
    cloud.forEach((p, i) => { const s = toScene(p); arr[i * 3] = s[0]; arr[i * 3 + 1] = s[1]; arr[i * 3 + 2] = s[2]; });
    geo.setAttribute('position', new THREE.BufferAttribute(arr, 3));
    group.add(new THREE.Points(geo, new THREE.PointsMaterial({ color: 0x6ee7a0, size: 2.2, sizeAttenuation: true })));
    if (params.mode === 'edge' && params.source === 'image') {
      const line = new THREE.BufferGeometry();
      line.setAttribute('position', new THREE.BufferAttribute(arr.slice(), 3));
      group.add(new THREE.Line(line, new THREE.LineBasicMaterial({ color: 0x2f8f63, transparent: true, opacity: .6 })));
    }
    // One sphere per station: raycasting a point cloud needs a threshold that changes
    // with zoom, and a handle you can see is the same thing with the affordance shown.
    const g = new THREE.SphereGeometry(0.9, 10, 8);
    cloud.forEach((p, i) => {
      const m = new THREE.Mesh(g, new THREE.MeshBasicMaterial({
        color: moved.has(i) ? 0xffcf6b : 0x0d1a14,
        transparent: true, opacity: moved.has(i) ? .95 : .35,
      }));
      const s = toScene(p);
      m.position.set(s[0], s[1], s[2]);
      m.userData = { i };
      group.add(m);
      handles.push(m);
    });
  };

  const tick = () => { place(); resize(); renderer.render(scene, camera); requestAnimationFrame(tick); };
  tick();

  // A seam for the probe, in the same spirit as the page's `ranchSend`: dragging is
  // screen-space maths with three sign conventions in it, and "the point followed the
  // cursor" is only evidence if something can ask where the cursor is for a given
  // station. It reports; it does not move anything. `ndc` outside +-1 means that
  // station is off the top/bottom/side of the view, so "I could not grab it" and
  // "I could not see it" are two different answers instead of one silent failure.
  window.showDesignDebug = () => {
    place();
    const r = renderer.domElement.getBoundingClientRect();
    return {
      rect: { left: r.left, top: r.top, width: r.width, height: r.height },
      vp: [innerWidth, innerHeight],
      count: cloud.length,
      handles: handles.length,
      dist: +dist.toFixed(1),
      target: [target.x, target.y, target.z].map(v => +v.toFixed(1)),
      // How many stations land inside the frustum at all: "cannot grab it" and "cannot
      // see it" are different faults, and only the second one is a framing bug. The
      // canvas maps the frustum exactly, so the ndc test is the whole question.
      onscreen: (() => {
        let n = 0;
        for (const p of cloud) {
          const s = toScene(p);
          const v = new THREE.Vector3(s[0], s[1], s[2]).project(camera);
          if (Math.abs(v.x) <= 1 && Math.abs(v.y) <= 1 && v.z < 1) ++n;
        }
        return n;
      })(),
      moved: [...moved],
      // The same NED-to-scene mapping the view uses, projected to screen pixels.
      at: i => {
        const p = cloud[i];
        if (!p) return null;
        const s = toScene(p);
        const v = new THREE.Vector3(s[0], s[1], s[2]).project(camera);
        return { cloud: { ...p }, ndc: [v.x, v.y],
          screen: [r.left + (v.x + 1) / 2 * r.width, r.top + (1 - v.y) / 2 * r.height] };
      },
    };
  };
  return { paint, fit };
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

function picker(host3d, items, get, set) {
  const wrap = el('div', 'sp-pick');
  const btn = el('button', 'sp-pick-btn');
  btn.type = 'button';
  const list = el('div', 'sp-pick-list');
  const paint = () => {
    const cur = items.find(o => o.id === get());
    btn.textContent = cur ? cur.name : '?';
    btn.title = cur && cur.z ? cur.z : '';
    list.textContent = '';
    for (const o of items) {
      const row = el('div', 'sp-pick-opt' + (o.id === get() ? ' sel' : ''));
      row.appendChild(el('b', null, o.name));
      if (o.z) row.appendChild(el('span', 'sp-pick-z', o.z));
      row.addEventListener('click', e => {
        e.stopPropagation();
        set(o.id);
        wrap.classList.remove('open');
        retrace();
        paint();
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
  return wrap;
}

let lastImage = null;
// The two numbers the designer is held to, and the undo, in one place: a drag changes
// the extent and the closest pair just as a model swap does, so both routes report it.
function status() {
  const sp = cloudSpan(cloud);
  els.count.textContent = cloud.length
    ? `${cloud.length} / ${params.drones} 个点 · 占 ${sp.w.toFixed(0)}×${sp.d.toFixed(0)} m，高低差 ${sp.h.toFixed(1)} m` +
      ` · 最近两点 ${minSpacing(cloud).toFixed(2)} m`
    : '还没有点';
  els.manual.textContent = moved.size ? `已手动挪动 ${moved.size} 个点` : '没有手动挪动的点';
  els.reset.disabled = !moved.size;
}

function retrace() {
  build();
  // A new outline is a new size and a new height, so the eye has to be re-armed to it;
  // the angles the designer left them at are kept.
  three.fit();
  three.paint();
  status();
}

function mount(target) {
  if (host) return;
  host = el('div', 'show-design');
  const head = el('div', 'sd-head');
  head.appendChild(el('h2', null, '✦ 编排队形编排'));
  head.appendChild(el('small', 'sd-note', '选一个 3D 模型 → 定机数与尺寸 → 拖动任意一个点 → 送进节目单'));
  host.appendChild(head);

  const body = el('div', 'sd-body');
  const col = el('div', 'sd-col');

  const mRow = el('label', 'sd-field');
  mRow.appendChild(el('i', null, '模型'));
  mRow.appendChild(picker(null, MODELS, () => params.model, v => { params.model = v; }));
  col.appendChild(mRow);

  const srcRow = el('label', 'sd-field');
  srcRow.appendChild(el('i', null, '来源'));
  srcRow.appendChild(picker(null, [
    { id: 'model', name: '预留 3D 模型', z: '有高度差' },
    { id: 'image', name: '图片描轮廓（平面）', z: '所有点同一高度' },
  ], () => params.source, v => { params.source = v; drop.style.display = v === 'image' ? '' : 'none'; }));
  col.appendChild(srcRow);

  const drop = el('div', 'sd-drop', '点这里选图片，或把图片拖进来（描出来的是平面轮廓）');
  const file = el('input', 'sd-file');
  file.type = 'file';
  file.accept = 'image/*';
  file.addEventListener('change', () => load(file.files && file.files[0]));
  drop.addEventListener('click', () => file.click());
  drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('hot'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('hot'));
  drop.addEventListener('dragend', () => drop.classList.remove('hot'));
  drop.addEventListener('drop', e => {
    e.preventDefault();
    drop.classList.remove('hot');
    load(e.dataTransfer.files && e.dataTransfer.files[0]);
  });
  drop.style.display = 'none';
  col.appendChild(drop);
  col.appendChild(file);

  const modeRow = el('label', 'sd-field');
  modeRow.appendChild(el('i', null, '取点'));
  modeRow.appendChild(picker(null, [{ id: 'edge', name: '轮廓' }, { id: 'fill', name: '填充' }],
    () => params.mode, v => { params.mode = v; }));
  col.appendChild(modeRow);
  col.appendChild(field('阈值', params.threshold, 10, 250, 5, v => { params.threshold = v; retrace(); }));
  col.appendChild(field('机数', params.drones, 1, 128, 1, v => { params.drones = Math.round(v); retrace(); }));
  col.appendChild(field('水平尺寸 (m)', params.scale, 4, 90, 1, v => { params.scale = v; retrace(); }));
  col.appendChild(field('中心高度 (m)', params.alt, ALT_MIN, ALT_MAX, 1, v => { params.alt = v; retrace(); }));

  els.count = el('div', 'sd-count', '还没有点');
  col.appendChild(els.count);
  els.manual = el('div', 'sd-count', '没有手动挪动的点');
  col.appendChild(els.manual);
  els.reset = el('button', 'sd-btn', '撤掉所有手动点');
  els.reset.disabled = true;
  els.reset.addEventListener('click', retrace);
  col.appendChild(els.reset);

  const send = el('button', 'sd-btn sd-send', '送进节目单');
  send.addEventListener('click', () => {
    if (cloud.length < 2) { els.count.textContent = '至少要有两个点'; return; }
    if (!window.opener) { els.count.textContent = '这个窗口是自己开的，没有节目单可写'; return; }
    window.opener.postMessage({ type: 'show-outline', points: cloud, alt: params.alt, scale: params.scale },
      location.origin);
    els.count.textContent = `已送回 ${cloud.length} 个点：回到编排窗口看那一幕`;
  });
  col.appendChild(send);
  col.appendChild(el('small', 'sd-note',
    '点可以直接拖：抓住一个点就是拖动那一架机的目的位置，拖过的点变成黄色。' +
    '高度受 10–120 m 限制，模型超高时会整体压低而不是把飞机顶到天上。'));
  body.appendChild(col);

  const stage = el('div', 'sd-stage');
  body.appendChild(stage);
  host.appendChild(body);
  target.appendChild(host);
  three = mountThree(stage);
  retrace();
}

function load(file) {
  if (!file || !/^image\//.test(file.type)) { els.count.textContent = '那不是图片'; return; }
  const url = URL.createObjectURL(file);
  const img = new Image();
  img.onload = () => {
    URL.revokeObjectURL(url);
    lastImage = img;
    retrace();
  };
  img.onerror = () => { els.count.textContent = '图片读不出来'; URL.revokeObjectURL(url); };
  img.src = url;
}

export function openDesigner() { mount(document.getElementById('app')); }
