// The 水利系统 control block: a timed barn cycle and a house supply that is its own
// valve, both of which hold shut until the water board reports running.
import { waterSystem } from '../world/water-tower.js';
import { barnSchedule, setBarnSchedule, setHouseSupply, waterBoardLive, waterState } from '../state/water.js';
import { boardRunning } from '../state/boards.js';
import { telemetry, boardStatusText } from './esp-panel.js';
import { cardGrid, cell, put, sectionLabel, statusLine } from './status-cards.js';
import { showToast } from './toast.js';

let host = null;
let timer = 0;
const refs = {};

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

function toggle(label, get, set, cls) {
  const b = el('button', 'esp-btn' + (cls ? ' ' + cls : ''), label);
  b.addEventListener('click', () => {
    set(!get());
    b.setAttribute('aria-pressed', String(get()));
  });
  b.dataset.get = '';
  refs[label] = { b, get };
  return b;
}

function stepper(label, key, min, max, step, unit) {
  const row = el('div', 'wt-row');
  row.appendChild(el('span', 'wt-row-l', label));
  const minus = el('button', 'wt-step', '−');
  const val = el('span', 'wt-row-v');
  const plus = el('button', 'wt-step', '+');
  const draw = () => { val.textContent = `${barnSchedule[key]} ${unit}`; };
  minus.addEventListener('click', () => {
    setBarnSchedule({ [key]: Math.max(min, barnSchedule[key] - step) });
    draw();
  });
  plus.addEventListener('click', () => {
    setBarnSchedule({ [key]: Math.min(max, barnSchedule[key] + step) });
    draw();
  });
  row.append(minus, val, plus);
  draw();
  refs[key] = { draw };
  return row;
}

// The board's decision wins while it is reporting, but the buttons are never taken
// away from the operator -- he asked for that twice. What they get instead is the
// answer out loud: pressing one while the board drives says so rather than
// moving the button's own light and nothing else.
function localIgnored(what) {
  showToast(`${what}由水利板决定，本地指令未生效`);
}

function refresh() {
  if (!host || !host.isConnected) return;
  const live = waterBoardLive();
  const up = boardRunning('water');
  refs['牛舍定时'].b.setAttribute('aria-pressed', String(barnSchedule.on));
  refs['主屋常开'].b.setAttribute('aria-pressed', String(waterSystem.houseOn));

  put(refs.barn, waterSystem.barnOn ? '放水' : '关闭', null, waterSystem.barnOn ? 'dr-live' : '');
  put(refs.house, waterSystem.houseOn ? '供水' : '关闭', null, waterSystem.houseOn ? 'dr-live' : '');
  put(refs.flow, (waterState.barnFlow + waterState.houseFlow).toFixed(1), 'm³/h');
  put(refs.press, waterSystem.pressure.toFixed(2), 'MPa', waterSystem.pressure < 0.2 ? 'dr-warn' : '');
  put(refs.level, waterSystem.level, '%', waterSystem.level < 25 ? 'dr-warn' : '');
  put(refs.next, !up ? '等板子' : barnSchedule.on || live ? waterState.nextSec.toFixed(0) : '停用',
    up && (barnSchedule.on || live) ? '秒' : null);
  put(refs.cycles, waterState.cycles, '次');
  put(refs.source, live ? '开发板' : up ? '本地定时' : '等待运行', null, live ? 'dr-live' : '');

  refs.status.hidden = live || up;
  if (!live && !up) refs.status.textContent = boardStatusText('water');
  refs.warn.textContent = !live && telemetry.water === null
    ? '水利板还没运行成功：两块阀先关着，等串口出 WATER 上报'
    : '';
}

export function mountWaterPanel(mount) {
  mount.textContent = '';
  host = mount;

  sectionLabel(mount, '水流控制 · 牛舍定时 / 主屋独立');
  const grid = cardGrid();
  refs.barn = cell(grid, '牛舍阀');
  refs.house = cell(grid, '主屋阀');
  refs.flow = cell(grid, '总流量');
  refs.press = cell(grid, '管道压力');
  refs.level = cell(grid, '水塔水位');
  refs.next = cell(grid, '下次动作');
  refs.cycles = cell(grid, '已循环');
  refs.source = cell(grid, '控制源');
  mount.appendChild(grid);

  refs.status = statusLine(mount, '');

  refs.warn = el('div', 'dr-alert');
  mount.appendChild(refs.warn);

  const ctl = el('div', 'esp-ctl');
  ctl.appendChild(toggle('牛舍定时', () => barnSchedule.on, v => {
    if (waterBoardLive()) { localIgnored('牛舍定时'); return; }
    setBarnSchedule({ on: v });
  }, 'dr-go'));
  ctl.appendChild(toggle('主屋常开', () => waterSystem.houseOn, v => {
    if (waterBoardLive()) { localIgnored('主屋供水'); return; }
    setHouseSupply(v);
  }));
  mount.appendChild(ctl);

  const rows = el('div', 'wt-rows');
  rows.appendChild(stepper('发水周期', 'periodSec', 4, 120, 2, 's'));
  rows.appendChild(stepper('单次时长', 'runSec', 1, 60, 1, 's'));
  mount.appendChild(rows);

  mount.appendChild(el('small', 'esp-note', '两路互不影响：牛舍按周期开合电磁阀，主屋是独立一路，随时可单独关。'));

  refresh();
  if (!timer) timer = setInterval(refresh, 200);
}
