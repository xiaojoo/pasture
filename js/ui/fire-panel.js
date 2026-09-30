// The 消防系统 panel: what the panel's four loops and three outputs are doing.
//
// The fake switch that used to sit on the 自动喷淋 row only moved its own class, so
// when the board is reporting, it is replaced by the pump permit the board sends. Nothing here
// claims to control a riser the board cannot reach.
import { fireAlertText, fireAlarmCount, fireHealthyLoops, firePanel, fireStale, ZONE_NAMES } from '../state/fire.js';
import { waterSystem } from '../world/water-tower.js';
import { boardStatusText } from './esp-panel.js';
import { cardGrid, cell, put, putRow, statusLine } from './status-cards.js';

// The frame's own words are the firmware's enum names; a 9-character NORMAL does not
// fit a card that is a third of the drawer, and the drawer is Chinese anyway.
const LEVEL_TEXT = { NORMAL: '正常', ALARM: '单路报警', EMERGENCY: '火警确认', FAULT: '监管故障' };

let host = null;
let timer = 0;
const refs = {};

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

function card(id, html) {
  const node = document.getElementById(id);
  if (node) node.innerHTML = html;
}

function refresh() {
  if (!host || !host.isConnected) return;
  // Quiet is not zero: the simulator holds one program, so another board's
  // firmware silences this one, and its last frame must not stay up as current.
  const live = !fireStale();

  ZONE_NAMES.forEach((name, i) => {
    put(refs[`z${i}`], live ? firePanel.zoneText[i] : '-', null,
      live && firePanel.zones[i] === '1' ? 'dr-dead'
        : live && firePanel.zones[i] !== '0' ? 'dr-warn'
        : live && firePanel.zones[i] === '0' ? 'dr-live' : '');
  });
  put(refs.mcp, live ? (firePanel.manualCall ? '已按下' : '正常') : '-', null,
    live && firePanel.manualCall ? 'dr-dead' : live ? 'dr-live' : '');
  put(refs.flow, live ? (firePanel.flow ? '有水流动' : '无流动') : '-', null,
    live && firePanel.flow ? 'dr-warn' : '');
  put(refs.level, live ? (LEVEL_TEXT[firePanel.level] || '未上报') : '等待', null,
    live && (firePanel.level === 'ALARM' || firePanel.level === 'EMERGENCY')
      ? 'dr-dead' : live && firePanel.level === 'FAULT' ? 'dr-warn'
      : actOk());
  const bell = !live ? ['-', null]
    : firePanel.bellFailed ? ['未证明', null]
    : firePanel.siren ? ['鸣响中', null]
    : [String(firePanel.sirenSeconds), '秒自检'];
  put(refs.bell, bell[0], bell[1], live && firePanel.bellFailed ? 'dr-dead' : '');
  put(refs.pump, live
    ? (firePanel.pumpPermit ? (firePanel.pumpFeedback ? '许可闭合' : '许可已给') : '许可断开')
    : '-', null, live && firePanel.pumpPermit ? 'dr-live' : '');
  put(refs.arm, live ? (firePanel.armed ? '自动' : 'BYPASS') : '-', null,
    live && !firePanel.armed ? 'dr-warn' : '');
  put(refs.sil, live ? (firePanel.silenced ? firePanel.silenceLeft : `${firePanel.silences}/3`) : '-',
    live ? (firePanel.silenced ? '秒后复响' : '次已用') : null);
  put(refs.source, live ? '开发板' : '等待运行', null, live ? 'dr-live' : '');

  // The long answers get a line of their own, because a 30-75 second compile string
  // in a card that is 117 px wide would either be cut off or push the row apart.
  refs.status.textContent = live
    ? `时钟 ${firePanel.clockValid ? firePanel.clock : '无时钟'} · 已运行 ${firePanel.uptime}s`
    : boardStatusText('fire');

  refs.warn.textContent = live ? fireAlertText() : '这块板还没有运行成功：下面的状态都是空的';

  card('fwHealth', `${live ? fireHealthyLoops() : '-'} <small>/4 回路</small>`);
  card('fwAlarms', `${live ? fireAlarmCount() : '-'} <small>路</small>`);
  card('fwPump', live ? (firePanel.pumpPermit ? '许可' : '待机') : '-');
  card('fwTank', `${waterSystem.level} <small>%</small>`);

  putRow('fwPumpRow', live ? (firePanel.pumpPermit ? (firePanel.pumpFeedback ? '运行中' : '许可已给') : '自动待机') : '自动待机',
    firePanel.pumpPermit ? 'warning' : '');
  putRow('fwLoopRow', live ? `${fireHealthyLoops()}/4 回路正常` : '等开发板上报',
    fireHealthyLoops() < 4 ? 'warning' : '');
  putRow('fwAlarmRow', live ? (fireAlarmCount() ? `${fireAlarmCount()} 路报警${firePanel.why ? ` · ${firePanel.why}` : ''}` : '当前没有报警事件') : '当前没有报警事件',
    fireAlarmCount() ? 'warning' : '');

  const sprk = document.getElementById('fwSprinkler');
  if (sprk) {
    sprk.textContent = live ? (firePanel.pumpPermit ? '开启' : '关闭') : '等待上报';
    sprk.className = `device-status${firePanel.pumpPermit ? ' warning' : ''}`;
  }
  const desc = document.getElementById('fwSprinklerDesc');
  if (desc) {
    desc.textContent = live
      ? (firePanel.pumpPermit ? `喷淋泵许可由面板给出${firePanel.pumpFeedback ? '，回流已证明' : ''}`
                              : '许可断开，喷淋泵不会被启动')
      : '全园区覆盖';
  }
}

function actOk() {
  if (fireStale()) return '';
  return firePanel.level === 'NORMAL' ? 'dr-live' : '';
}

export function mountFirePanel(mount) {
  mount.textContent = '';
  host = mount;

  // Same cards as 消防实时状态 above, three to a row: twelve of them at two per row
  // was six screens of scrolling to read one panel's status.
  const grid = cardGrid();
  ZONE_NAMES.forEach((name, i) => { refs[`z${i}`] = cell(grid, `${name}回路`); });
  for (const [k, label] of [['mcp', '手动报警'], ['flow', '水流开关'], ['level', '面板状态'],
                            ['bell', '警铃'], ['pump', '泵许可'], ['arm', '钥匙'],
                            ['sil', '静音'], ['source', '数据源']]) {
    refs[k] = cell(grid, label);
  }
  mount.appendChild(grid);

  refs.status = statusLine(mount, '');

  refs.warn = el('div', 'dr-alert');
  mount.appendChild(refs.warn);

  mount.appendChild(el('small', 'esp-note',
    '回路档位：0 正常 · 1 报警 · 2 断路 · 3 短路 · 4 读数模糊。消防水池水位读的是水塔，不是这块板——面板不测的那个量在这里就不出现。'));

  refresh();
  if (!timer) timer = setInterval(refresh, 200);
}
