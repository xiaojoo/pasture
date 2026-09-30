// The 无人机巡检 drawer: telemetry readout, a hold-to-fly direction pad, the
// action buttons, the patrol route and the two camera modes.
import {
  GROUND_STATION, ROUTE, drone, pilot, pilotCommand, setManualAxes, view,
} from '../world/drone.js';
import { telemetry, boardStatusText } from './esp-panel.js';
import { boardRunning } from '../state/boards.js';
import { cardGrid, cell, put, sectionLabel, statusLine } from './status-cards.js';

function boardLive() {
  return !!telemetry.drone && drone.source === 'board';
}

let host = null;
let timer = 0;
let frame = null;
const held = { fwd: 0, back: 0, left: 0, right: 0, up: 0, down: 0, yawL: 0, yawR: 0 };

function applyAxes() {
  setManualAxes({
    fwd: held.fwd - held.back,
    side: held.right - held.left,
    climb: held.up - held.down,
    yaw: held.yawR - held.yawL,
  });
}

const KEYS = {
  KeyW: 'fwd', KeyS: 'back', KeyA: 'left', KeyD: 'right',
  ArrowUp: 'fwd', ArrowDown: 'back', ArrowLeft: 'left', ArrowRight: 'right',
  Space: 'up', ShiftLeft: 'down', KeyQ: 'yawL', KeyE: 'yawR',
};

window.addEventListener('keydown', e => {
  const k = KEYS[e.code];
  if (!k || e.target.closest('input,textarea,[contenteditable]')) return;
  e.preventDefault();
  if (!held[k]) { held[k] = 1; applyAxes(); }
});
window.addEventListener('keyup', e => {
  const k = KEYS[e.code];
  if (!k || !held[k]) return;
  held[k] = 0;
  applyAxes();
});
window.addEventListener('blur', () => {
  for (const k of Object.keys(held)) held[k] = 0;
  applyAxes();
});

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

function action(label, handler, cls) {
  const b = el('button', 'esp-btn' + (cls ? ' ' + cls : ''), label);
  b.addEventListener('click', handler);
  return b;
}

function padButton(label, key, cls) {
  const b = el('button', 'dr-pad-btn' + (cls ? ' ' + cls : ''), label);
  b.title = label;
  const on = e => { e.preventDefault(); if (!held[key]) { held[key] = 1; applyAxes(); } };
  const off = () => { if (held[key]) { held[key] = 0; applyAxes(); } };
  b.addEventListener('pointerdown', on);
  b.addEventListener('pointerup', off);
  b.addEventListener('pointerleave', off);
  b.addEventListener('pointercancel', off);
  return b;
}
window.addEventListener('pointerup', () => {
  let any = false;
  for (const k of Object.keys(held)) if (held[k]) { held[k] = 0; any = true; }
  if (any) applyAxes();
});

function ensureFrame() {
  if (frame) return frame;
  frame = el('div', 'dr-fpv');
  const head = el('div', 'dr-fpv-head');
  head.appendChild(el('span', null, 'FPV 云台'));
  frame.appendChild(head);
  frame.appendChild(el('div', 'dr-fpv-foot', '机头朝飞行方向 · 云台保持水平'));
  document.getElementById('app').appendChild(frame);
  view.frame = frame;
  return frame;
}

function toggleView(key, label) {
  const b = action(label, () => {
    view[key] = !view[key];
    if (key === 'fpv') {
      ensureFrame();
      frame.classList.toggle('show', view.fpv);
    }
    b.setAttribute('aria-pressed', String(view[key]));
  });
  b.setAttribute('aria-pressed', String(view[key]));
  return b;
}

// While the board is flying, the sticks and action buttons are greyed out
// rather than left to silently fight the firmware.
function lockManual(on) {
  // Never a lock: the buttons stay clickable while the board flies. What the class
  // marks is an unacknowledged command, not a disabled control.
  for (const b of host.manualEls) {
    b.classList.toggle('dr-pending', on && !!pilot.want && !pilot.acked);
  }
}

function refresh() {
  if (!host || !host.mount.isConnected) return;
  lockManual(false);
  const speed = Math.hypot(drone.vel.x, drone.vel.z);
  const w = ROUTE[drone.wp];
  const dist = Math.hypot(w.x - drone.pos.x, w.z - drone.pos.z);
  const link = Math.hypot(drone.pos.x - GROUND_STATION.x, drone.pos.z - GROUND_STATION.z);

  put(host.t.mode, drone.mode, null, drone.mode === 'IDLE' ? '' : 'dr-live');
  // 高度 is the aircraft's height above the pad in the flying mode. The scene's own
  // position is a render coordinate with no vertical limit of its own; the board's AGL
  // is the measurement, so once there is a frame it wins -- the two used to disagree by
  // whatever the local climb was clamped to.
  put(host.t.alt, (drone.source === 'board' && drone.boardAgl !== null ? drone.boardAgl : drone.pos.y).toFixed(1), 'm');
  put(host.t.speed, speed.toFixed(1), 'm/s');
  put(host.t.batt, drone.battery.toFixed(0), '%', drone.battery <= 15 ? 'dr-warn' : '');
  put(host.t.link, drone.rssi, `dBm · ${link.toFixed(0)} m`, link > 90 ? 'dr-warn' : '');
  put(host.t.wp, w.name, `${dist.toFixed(0)} m`);
  put(host.t.shot, drone.photos, '张');
  put(host.t.time, `${Math.floor(drone.flightSeconds / 60)}′${String(Math.round(drone.flightSeconds % 60)).padStart(2, '0')}″`);
  const up = boardRunning('drone');
  host.status.hidden = up;
  if (!up) host.status.textContent = boardStatusText('drone');
  host.alert.textContent = !telemetry.drone
    ? '无人机板还没运行成功：飞机在地面待命，指令只改本地场景'
    : drone.alert;

  for (let i = 0; i < host.routeEls.length; i++) {
    host.routeEls[i].classList.toggle('cur', i === drone.wp && drone.mode === 'PATROL');
    host.routeEls[i].classList.toggle('done', i < drone.wp);
  }
}

export function mountDronePanel(mount) {
  ensureFrame();
  mount.textContent = '';
  host = { mount, alert: null, routeEls: [], t: {}, manualEls: [] };

  sectionLabel(mount, '无人机巡检 · 实时遥测');
  const grid = cardGrid();
  host.t = {
    mode: cell(grid, '模式'), alt: cell(grid, '高度'), speed: cell(grid, '地速'), batt: cell(grid, '电量'),
    link: cell(grid, '链路'), wp: cell(grid, '下一航点'), shot: cell(grid, '拍照'), time: cell(grid, '飞行时长'),
  };
  mount.appendChild(grid);

  host.status = statusLine(mount, '');

  host.alert = el('div', 'dr-alert');
  mount.appendChild(host.alert);

  const wrap = el('div', 'esp-panel');
  const acts = el('div', 'esp-ctl');
  // Every one of these goes through pilotCommand, which is the same call into the
  // scene plus a note of what was asked. The buttons are always live: the
  // browser cannot reach the firmware over a one-way serial link, so a greyed stick
  // would be a grounded aircraft with nobody holding the controls. The panel says
  // which of the two the board has agreed to.
  acts.appendChild(action('起飞', () => pilotCommand('takeoff'), 'dr-go'));
  acts.appendChild(action('自动巡检', () => pilotCommand('patrol'), 'dr-go'));
  acts.appendChild(action('悬停', () => pilotCommand('hover')));
  acts.appendChild(action('返航', () => pilotCommand('rtl')));
  acts.appendChild(action('降落', () => pilotCommand('land'), 'dr-stop'));
  host.manualEls = [...acts.querySelectorAll('.esp-btn')];
  wrap.appendChild(acts);

  const cams = el('div', 'esp-ctl');
  cams.appendChild(el('span', 'dr-lbl', '视角'));
  cams.appendChild(toggleView('fpv', 'FPV 画中画'));
  cams.appendChild(toggleView('follow', '跟随视角'));
  cams.appendChild(el('small', 'esp-note', 'WASD 平移 · Q/E 偏航 · 空格上升 · Shift 下降'));
  wrap.appendChild(cams);

  const body = el('div', 'dr-body');

  const pad = el('div', 'dr-pad');
  const padCell = (label, key, cls) => (key ? padButton(label, key, cls) : el('span', 'dr-pad-gap', label));
  for (const row of [
    [padCell('上升', 'up', 'dr-pad-v'), padCell('↑ 前进', 'fwd'), padCell('偏航 →', 'yawR', 'dr-pad-v')],
    [padCell('← 左移', 'left'), padCell('悬停', null, 'dr-pad-c'), padCell('右移 →', 'right')],
    [padCell('下降', 'down', 'dr-pad-v'), padCell('↓ 后退', 'back'), padCell('← 偏航', 'yawL', 'dr-pad-v')],
  ]) for (const c of row) pad.appendChild(c);
  host.manualEls.push(...pad.querySelectorAll('.dr-pad-btn'));
  body.appendChild(pad);

  const route = el('ol', 'dr-route');
  for (const [i, w] of ROUTE.entries()) {
    const li = el('li', 'dr-wp');
    li.appendChild(el('span', 'dr-wp-i', String(i + 1)));
    li.appendChild(el('span', 'dr-wp-n', w.name));
    route.appendChild(li);
    host.routeEls.push(li);
  }
  body.appendChild(route);
  wrap.appendChild(body);

  mount.appendChild(wrap);
  refresh();
  if (!timer) timer = setInterval(refresh, 120);
}
