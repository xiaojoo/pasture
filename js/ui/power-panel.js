// The 电路系统 cabinet block: what the ranch is drawing, and what the switchboard
// board has decided about it.
//
// The numbers here are the board's measurements, not the scene's guesses.
import { powerGrid, powerFaultText, powerStale } from '../state/power.js';
import { boardStatusText } from './esp-panel.js';
import { cardGrid, cell, put, putRow, statusLine } from './status-cards.js';

let host = null;
let timer = 0;
const refs = {};

// The readings this block shows, in the order a person walks past the cabinet:
// three phases, then what it adds up to, then the protective quantities, then the
// two feeders and what the protection layer decided.
const CELLS = [
  ['ua', 'A相电压'], ['ub', 'B相电压'], ['uc', 'C相电压'],
  ['kw', '总功率'], ['load', '负载率'], ['hz', '频率'],
  ['rcd', '漏电流'], ['temp', '柜温'], ['day', '今日电量'],
  ['breaker', '总闸'], ['pump', '水泵回路'], ['yard', '照明回路'],
  ['prot', '保护'], ['source', '数据源'],
];

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

// The two lighting switches used to be taken away from the operator while this board
// reported -- but this board does not command the lamps, it feeds them, and taking
// the switch off a board that does not own it is exactly the "板子接管了我就不能自己
// 控制" he rejected. The switches stay clickable; the 照明回路 card says whether the
// cabinet is actually closed.

// One of the three phases, coloured by how far it is from the other two rather
// than by a threshold: a leg that has collapsed has to be findable at a glance.
function voltText(i) {
  const v = powerGrid.volts[i] || 0;
  return v > 0 ? v.toFixed(0) : '-';
}

function actClass() {
  if (powerStale()) return '';
  if (powerGrid.act === 'TRIP') return 'dr-dead';
  if (powerGrid.act === 'SHED' || powerGrid.act === 'ALARM') return 'dr-warn';
  return 'dr-live';
}

function card(id, html) {
  const node = document.getElementById(id);
  if (node) node.innerHTML = html;
}

// The four cards and the chips are the same numbers. Written here rather than in
// the drawer markup, because the markup is only rebuilt when the drawer opens and
// a board's reading changes twice a second.
// No local stand-in for the four cards. The app can guess what a ranch draws, and a
// guess sitting where a meter reading goes is how a dashboard gets believed and then
// disbelieved: the cards are the board's, or they are dashes.
function writeCards(live) {
  card('pwKw', live ? `${powerGrid.kw.toFixed(1)} <small>kW</small>` : '-- <small>kW</small>');
  card('pwLoad', live ? `${powerGrid.loadPct.toFixed(0)} <small>%</small>` : '-- <small>%</small>');
  card('pwVolt', live ? `${powerGrid.volts[0].toFixed(0)} <small>V</small>` : '-- <small>V</small>');
  card('pwDay', live && powerGrid.kwhToday > 0 ? `${powerGrid.kwhToday.toFixed(1)} <small>kWh</small>`
                                               : '-- <small>kWh</small>');
}

function refresh() {
  if (!host || !host.isConnected) return;
  // Stale means this board has gone quiet, which on a single-simulator app means
  // another board's firmware is loaded. Show '-', not the last frame, because a
  // switchboard frozen at "39 %" is exactly what gets trusted by the next reader.
  const live = !powerStale();
  writeCards(live);

  put(refs.ua, live ? voltText(0) : '-', live ? 'V' : null, live && powerGrid.phaseLoss ? 'dr-warn' : '');
  put(refs.ub, live ? voltText(1) : '-', live ? 'V' : null, live && powerGrid.phaseLoss ? 'dr-warn' : '');
  put(refs.uc, live ? voltText(2) : '-', live ? 'V' : null, live && powerGrid.phaseLoss ? 'dr-warn' : '');
  put(refs.kw, live ? powerGrid.kw.toFixed(1) : '-', live ? 'kW' : null);
  put(refs.load, live ? powerGrid.loadPct.toFixed(0) : '-', live ? '%' : null,
    live && powerGrid.loadPct > 90 ? 'dr-warn' : '');
  put(refs.hz, live && powerGrid.hz ? powerGrid.hz.toFixed(2) : '-', live && powerGrid.hz ? 'Hz' : null,
    live && powerGrid.hz && (powerGrid.hz < 49 || powerGrid.hz > 51) ? 'dr-warn' : '');
  put(refs.rcd, live ? powerGrid.rcdMa.toFixed(0) : '-', live ? 'mA' : null,
    live && powerGrid.rcdMa > 30 ? 'dr-dead' : live ? 'dr-live' : '');
  put(refs.temp, live ? powerGrid.tempC.toFixed(0) : '-', live ? '°C' : null,
    live && powerGrid.tempC > 75 ? 'dr-warn' : '');
  put(refs.day, live ? powerGrid.kwhToday.toFixed(1) : '-', live ? 'kWh' : null);

  put(refs.breaker, live ? (powerGrid.breakerOn ? '合闸' : '分闸') : '-', null,
    live ? (powerGrid.breakerOn ? 'dr-live' : 'dr-dead') : '');
  put(refs.pump, live ? (powerGrid.pumpFed ? '带电' : '已切除') : '-', null,
    live ? (powerGrid.pumpFed ? 'dr-live' : 'dr-warn') : '');
  put(refs.yard, live ? (powerGrid.yardFed ? '带电' : '已切除') : '-', null,
    live ? (powerGrid.yardFed ? 'dr-live' : 'dr-warn') : '');
  put(refs.prot, live ? (powerGrid.act || '等待上报') : '等待上报', null, live ? actClass() : '');
  put(refs.source, live ? '开发板' : '等待运行', null, live ? 'dr-live' : '');

  refs.status.textContent = live ? `运行 ${powerGrid.uptime}s · 三相 ${powerGrid.amps.map(a => a.toFixed(0)).join('/')} A`
    : boardStatusText('power');

  // The two boards can disagree, and the disagreement is the useful part: the
  // water board says it asked for the pump, this one says whether it got power.
  let note = powerFaultText();
  if (!live && powerGrid.lastSeen) note = `这块板已停止上报（最后一帧 ${Math.round((Date.now() - powerGrid.lastSeen) / 1000)} 秒前）`;
  if (live && powerGrid.held) {
    note = `需求仍在等：${powerGrid.why || '保护'}${powerGrid.shedSec ? ` 已 ${powerGrid.shedSec}s` : ''}` +
           (powerGrid.lockSec ? ` · 重合闸锁 ${powerGrid.lockSec}s` : '');
  }
  if (live && powerGrid.coil) note = `接触器异常：${powerGrid.coil === 'welded' ? '分闸后仍闭合' : '合闸后未闭合'}`;
  refs.warn.textContent = note;

  // The 电力设备 rows below: these three nodes are in the drawer's own markup and
  // used to keep their typed HTML ("正常", "38%") whatever the board said, because
  // the refs they were written through were never assigned. They are written by id
  // now, which is the only handle that survives a drawer rebuild.
  putRow('pwCabinet', !live ? '等待上报'
    : powerGrid.act === 'TRIP' ? '跳闸'
    : powerGrid.act === 'SHED' ? '减载'
    : powerGrid.act === 'ALARM' ? '越限'
    : '正常', live && powerGrid.act && powerGrid.act !== 'NORMAL' ? 'warning' : '');
  putRow('pwFeeder', !live ? '--' : !powerGrid.breakerOn ? '分闸' : `${powerGrid.loadPct.toFixed(0)}%`,
    live && !powerGrid.breakerOn ? 'warning' : '');
  const feederDesc = document.getElementById('pwFeederDesc');
  if (feederDesc) {
    feederDesc.textContent = live
      ? `负载 ${powerGrid.loadPct.toFixed(0)}% · 温度 ${powerGrid.tempC.toFixed(0)}°C · ${powerGrid.amps.map(a => a.toFixed(0)).join('/')} A`
      : '等待这块板的上报';
  }
}

export function mountPowerPanel(mount) {
  mount.textContent = '';
  host = mount;

  const grid = cardGrid();
  for (const [k, label] of CELLS) refs[k] = cell(grid, label);
  mount.appendChild(grid);

  refs.status = statusLine(mount, '');

  refs.warn = el('div', 'dr-alert');
  mount.appendChild(refs.warn);

  mount.appendChild(el('small', 'esp-note',
    '界面只读这块板的上报：跳闸后的重合闸要走柜门上的本地开关，或硬件上发 MQTT ranch/power/cmd=reclose；浏览器仿真只有上行通道。'));

  refresh();
  if (!timer) timer = setInterval(refresh, 200);
}
