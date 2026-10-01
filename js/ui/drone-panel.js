// The 无人机巡检 drawer: telemetry readout, a hold-to-fly direction pad, the
// action buttons, the patrol route and the two camera modes.
import {
  GROUND_STATION, ROUTE, drone, pilot, pilotCommand, setManualAxes, stick, view,
} from '../world/drone.js';
import { telemetry, boardStatusText } from './esp-panel.js';
import { cmd, paintAck } from './cmd.js';
import { boardRunning } from '../state/boards.js';
import { cardGrid, cell, put, sectionLabel, statusLine } from './status-cards.js';

function boardLive() {
  return !!telemetry.drone && drone.source === 'board';
}

// The modes where the plan is flying the aircraft. LOITER (cancelled, waiting) and HOLD
// (a hand on the stick) are the two ends of manual, and GROUND / DESCEND are neither.
const AUTO_MODES = ['CLIMB', 'TRANSIT', 'DWELL', 'PATROL', 'RETURN', 'RTL'];
// The button's state comes from the board's own phase whenever the board is talking, and
// only from the scene while it is not. Lit off the page's own click is the thing that got
// this drawer distrusted: 「按下就亮」 and 「板子在飞」 are two different claims.
// Read off the frame rather than `drone.boardMode`: that mirror is applied on the board
// link's 200 ms interval, and measured against the live board the frame already said
// `mode=CLIMB` while the button still read unlit -- so the press that should have been the
// cancel went out as 巡检 again, one press behind the aircraft for the rest of the run.
function liveMode() {
  const f = telemetry.drone;
  return f ? String(f.mode || '').toUpperCase() : drone.mode;
}
function autoOn() {
  return AUTO_MODES.includes(liveMode());
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

  put(host.t.mode, liveMode(), null, liveMode() === 'IDLE' ? '' : 'dr-live');
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

  // Three answers, not one string: the line never reached the board (no simulator,
  // board not running), the board read it and refused it, and the board is flying it.
  const undelivered = stick.sent === false ? `摇杆没送到板子：${stick.why}` : '';
  const refused = drone.rcWhy ? `板子拒绝这一下摇杆：${drone.rcWhy}` : '';
  // LOITER is the board saying "cancelled, and staying cancelled". The operator has to be
  // able to tell that from an automatic sortie at a glance, or the pad looks like it is
  // fighting the mission rather than replacing it.
  const cancelled = liveMode() === 'LOITER' || liveMode() === 'HOLD';
  // Two sentences, both always on screen. They used to be one slot that picked whichever
  // fact was newest, so the instant a direction was pressed the cancellation line was
  // overwritten by 「摇杆：板子在照它飞」 -- which is the exact moment the operator needs to
  // be told the sortie is off. Same for a refusal.
  const sortie = cancelled
    ? '自动巡航已取消 · 板子停在 LOITER 等人工：推方方键或在手机上推杆就接管，再按一次「自动巡检」接着飞'
    : autoOn() ? '自动巡航中 · 板子按航线飞：按「自动巡检」就取消，取消后由方方键 / 手机接管'
      : telemetry.drone ? `板子现在的相：${liveMode() || '未上报'}`
        : '无人机板没上报：飞机在地面待命，指令只改本地场景';
  const authority = drone.ovr === 1 ? '摇杆：无人机板在照它飞（DRONE 帧 ovr=1）'
    : drone.ovr === 0 ? '摇杆：已交回板子的任务飞行（ovr=0）'
      : '摇杆：只有本地场景在动（无人机板没上报）';
  const line = undelivered || refused || authority;
  host.sortie.textContent = sortie;
  host.sortie.classList.toggle('dr-warn', cancelled);
  host.stick.textContent = line;
  host.stick.classList.toggle('dr-live', !undelivered && !refused && drone.ovr === 1);
  host.stick.classList.toggle('dr-warn', !!undelivered || !!refused);
  paintAck('drone', host.ack);
  // The one button that means two things says which one it currently is, off the board's
  // mode rather than off the last click.
  if (host.patrol) {
    const on = autoOn();
    host.patrol.setAttribute('aria-pressed', String(on));
    host.patrol.title = on
      ? '自动巡航中：再点一次取消，飞机停在原地由摇杆/手机接管（恢复自动就再点回来）'
      : '按航线自动巡检：起飞后自己走完 ' + ROUTE.length + ' 个航点';
  }

  for (let i = 0; i < host.routeEls.length; i++) {
    host.routeEls[i].classList.toggle('cur', i === drone.wp && drone.mode === 'PATROL');
    host.routeEls[i].classList.toggle('done', i < drone.wp);
  }
}

export function mountDronePanel(mount) {
  ensureFrame();
  mount.textContent = '';
  host = { mount, alert: null, stick: null, routeEls: [], t: {}, manualEls: [] };

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
  // One button, two ends. Pressed once it launches and flies the route; pressed again it
  // cancels the plan and the aircraft stays where it is, under the sticks or the phone.
  // Its pressed state is the board's own mode, not what was last clicked -- a light that
  // says "on" because the page pressed it is the thing that got distrusted here before.
  // It deliberately does NOT carry `dr-go`: that class paints a permanent green fill, and
  // measured against the live board the button's computed style was then the same in
  // GROUND, CLIMB and LOITER (rgba(110,231,160,.14) / border .38 in all three) -- which is
  // what he read as 「一直是选择状态」. Neutral when off, filled when the frame says an
  // auto phase; see the `[aria-pressed="true"]` rule in esp-panel.css.
  host.patrol = action('自动巡检', () => pilotCommand(autoOn() ? 'hover' : 'patrol'), 'dr-toggle');
  host.patrol.setAttribute('aria-pressed', 'false');
  acts.appendChild(host.patrol);
  acts.appendChild(action('返航', () => pilotCommand('rtl')));
  acts.appendChild(action('降落', () => pilotCommand('land'), 'dr-stop'));
  host.manualEls = [...acts.querySelectorAll('.esp-btn')];
  // The board's own answer, in the row that asks: 起飞 pressed and the aircraft not
  // lifting has three different causes -- the page did not send it, the board did not
  // know it, the board said no -- and only the board can tell them apart.
  acts.appendChild(el('span', 'cmd-line'));
  host.ack = acts.lastChild;
  wrap.appendChild(acts);

  // Three commands the firmware has accepted all along and no screen ever sent:
  // 解锁 (arm=1), 上保险 (safe=1) and 拍照 (camera=1). The first two move the safety, so
  // they get the same second click the fire panel's 静音/复位 do; the phone has had all
  // three, which is how the gap showed up.
  const boardOps = el('div', 'esp-ctl');
  boardOps.appendChild(el('span', 'dr-lbl', '板上操作'));
  for (const [labelText, line, note] of [
    ['解锁', 'arm=1', '把保险拉开：随后按「起飞」它才推得动电机；再点「上保险」就收回去'],
    ['上保险', 'safe=1', '和机上的保险开关同一个动作：在地面锁住电机'],
    ['拍照', 'camera=1', '让飞控立刻拍一张，计入帧里的 shots='],
  ]) {
    // One click each. 解锁 and 上保险 are the same pair the aircraft's own safety switch
    // is, and each undoes the other, so a confirmation here would make the operator
    // press twice to do what the board lets them reverse by pressing the other button.
    const b = action(labelText, () => cmd.droneVerb(line));
    b.title = note;
    boardOps.appendChild(b);
  }
  wrap.appendChild(boardOps);

  const cams = el('div', 'esp-ctl');
  cams.appendChild(el('span', 'dr-lbl', '视角'));
  cams.appendChild(toggleView('fpv', 'FPV 画中画'));
  cams.appendChild(toggleView('follow', '跟随视角'));
  cams.appendChild(el('small', 'esp-note', 'WASD 平移 · Q/E 偏航 · 空格上升 · Shift 下降'));
  wrap.appendChild(cams);

  // Who the board says is flying the sticks, put next to the pad that sends them:
  // a control whose authority is invisible reads as a broken control. The sortie's own
  // state sits directly above it and neither one is ever used to paint over the other.
  host.sortie = el('div', 'dr-alert dr-sortie');
  wrap.appendChild(host.sortie);
  host.stick = el('div', 'dr-alert dr-stick');
  wrap.appendChild(host.stick);

  const body = el('div', 'dr-body');

  const pad = el('div', 'dr-pad');
  const padCell = (label, key, cls) => (key ? padButton(label, key, cls) : el('span', 'dr-pad-gap', label));
  // The middle of the pad used to be a *label* — 悬停 — over a cell that did nothing.
  // Now that 自动巡检 is one button with two ends, that dead word would be ambiguous as
  // well as inert, so the middle releases the sticks where they are: with the sortie still
  // automatic the board takes the plan back, with it cancelled the aircraft stops where the
  // hand left it.
  const release = el('button', 'dr-pad-btn dr-pad-c', '交回摇杆');
  release.title = '立刻松开所有方向（rc=0,0,0,0）：没取消自动就交回航线，取消了就停在原地';
  release.addEventListener('click', () => {
    for (const k of Object.keys(held)) held[k] = 0;
    applyAxes();
  });
  for (const row of [
    [padCell('上升', 'up', 'dr-pad-v'), padCell('↑ 前进', 'fwd'), padCell('偏航 →', 'yawR', 'dr-pad-v')],
    [padCell('← 左移', 'left'), release, padCell('右移 →', 'right')],
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
