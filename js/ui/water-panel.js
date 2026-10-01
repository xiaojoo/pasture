// The 水利系统 control block: a timed barn cycle and a house supply that is its own
// valve, both of which hold shut until the water board reports running.
import { waterSystem } from '../world/water-tower.js';
import { barnSchedule, setBarnSchedule, setHouseSupply, waterBoardLive, waterState } from '../state/water.js';
import { boardRunning } from '../state/boards.js';
import { telemetry, boardStatusText } from './esp-panel.js';
import { cardGrid, cell, put, sectionLabel, statusLine } from './status-cards.js';
import { cmd, paintAck, parseLim } from './cmd.js';

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

// Where a stepper's number comes from. Once this board reports its programme the row
// shows and changes *the board's* number, inside the range the board accepts; until
// then the row is the page's own demo cycle and says 本页, because a 4-second period is
// not something a real valve controller would take.
const BOARD_RANGE = { periodSec: { key: 'per', min: 60, max: 86400 }, runSec: { key: 'run', min: 5, max: 3600 } };
const limNow = {};

function stepper(label, key, min, max, step, unit, wire) {
  const row = el('div', 'wt-row');
  row.appendChild(el('span', 'wt-row-l', label));
  const minus = el('button', 'wt-step', '−');
  const val = el('span', 'wt-row-v');
  const plus = el('button', 'wt-step', '+');
  const board = BOARD_RANGE[key];
  const fromBoard = () => (board && Number.isFinite(limNow[board.key]) ? limNow[board.key] : null);
  const draw = () => {
    const v = fromBoard();
    val.textContent = v === null ? `${barnSchedule[key]} ${unit} · 本页` : `${v} ${unit} · 板子`;
    val.title = v === null
      ? `本页的演示周期；${wire ? '板子认的范围另有限制，发给它会被退回' : '只改本页'}`
      : `这块板正在跑的周期，改它就改板子（认 ${board.min}..${board.max} ${unit}）`;
  };
  const move = dir => {
    const cur = fromBoard();
    if (cur !== null) {
      // A ×1.5 step, because a fixed number of seconds is useless across a range that
      // runs from a minute to a day, and the new number is printed before it is sent.
      const next = Math.min(board.max, Math.max(board.min,
        Math.round(dir > 0 ? cur * 1.5 : cur / 1.5)));
      setBarnSchedule({ [key]: next });
      wire(next);
      draw();
      return;
    }
    const next = dir > 0 ? Math.min(max, barnSchedule[key] + step) : Math.max(min, barnSchedule[key] - step);
    setBarnSchedule({ [key]: next });
    if (wire) wire(next);
    draw();
  };
  minus.addEventListener('click', () => move(-1));
  plus.addEventListener('click', () => move(1));
  row.append(minus, val, plus);
  draw();
  refs[key] = { draw };
  return row;
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
  // The programme as this board reports it, then the rows that show it.
  Object.assign(limNow, parseLim(live && telemetry.water ? telemetry.water.lim : ''));
  if (refs.periodSec) refs.periodSec.draw();
  if (refs.runSec) refs.runSec.draw();
  paintAck('water', refs.cmd);
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
    // The board's own words for this are resume and stop: the programme is enabled
    // or it is not. Switching it here means telling the board, not just painting
    // the scene -- and the ack line below is what says whether it listened.
    setBarnSchedule({ on: v });
    cmd.waterBarn(v);
  }, 'dr-go'));
  ctl.appendChild(toggle('主屋常开', () => waterSystem.houseOn, v => {
    setHouseSupply(v);
    cmd.waterHouse(v);
  }));
  const once = el('button', 'esp-btn', '立即放水一次');
  once.title = '放水一次，不改牛舍的周期计划';
  once.addEventListener('click', cmd.waterOnce);
  ctl.appendChild(once);
  refs.cmd = el('span', 'cmd-line');
  ctl.appendChild(refs.cmd);
  mount.appendChild(ctl);

  const rows = el('div', 'wt-rows');
  rows.appendChild(stepper('发水周期', 'periodSec', 4, 120, 2, 's', cmd.waterPeriod));
  rows.appendChild(stepper('单次时长', 'runSec', 1, 60, 1, 's', cmd.waterRun));
  mount.appendChild(rows);

  mount.appendChild(el('small', 'esp-note',
    '两路互不影响：牛舍按周期开合电磁阀，主屋是独立一路，随时可单独关。' +
    '步进改的周期和时长也发给板子，但板子只认周期 60..86400 秒、单次 5..3600 秒——' +
    '比它小的演示值会被退回，上面那行照实说是哪条没认。'));

  refresh();
  if (!timer) timer = setInterval(refresh, 200);
}
