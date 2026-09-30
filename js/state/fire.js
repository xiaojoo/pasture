// The fire panel board's state as the dashboard sees it.
//
// A leaf, like state/power.js: nothing here imports another state module. The panel
// reports four supervised loops plus the call point, the flow switch and the three
// outputs, and every number below is one of those -- there is no water-tank level or
// riser pressure in this frame because this board does not measure either.
export const firePanel = {
  // 0 healthy · 1 alarm · 2 open · 3 short · 4 unreadable, in lib/fire_logic.h's order.
  zones: ['0', '0', '0', '0'],
  zoneText: ['正常', '正常', '正常', '正常'],
  manualCall: false,
  flow: false,

  level: '',
  why: '',
  armed: true,
  siren: false,
  strobe: false,
  pumpPermit: false,
  pumpFeedback: false,
  silenced: false,
  supervision: false,
  bellFailed: false,
  sirenSeconds: 0,
  silences: 0,
  silenceLeft: 0,

  link: 'down',
  clockValid: true,
  clock: '',
  uptime: 0,
  lastSeen: 0,
};

export const ZONE_NAMES = ['主屋', '牛舍', '仓库', '配电房'];

const ZONE_MEANING = {
  '0': '正常',
  '1': '报警',
  '2': '断路',
  '3': '短路',
  '4': '读数模糊',
};

const num = (v, fallback = 0) => {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
};

export function applyFireFrame(t) {
  if (!t) return false;
  const z = String(t.z || '0000');
  firePanel.zones = [z[0] || '0', z[1] || '0', z[2] || '0', z[3] || '0'];
  firePanel.zoneText = firePanel.zones.map(c => ZONE_MEANING[c] || '未知');
  firePanel.manualCall = t.mcp === '1';
  firePanel.flow = t.flow === '1';
  firePanel.level = t.lvl || '';
  firePanel.why = t.why || '';
  firePanel.armed = t.arm !== '0';
  firePanel.siren = t.siren === '1';
  firePanel.strobe = t.strb === '1';
  firePanel.pumpPermit = t.pump === '1';
  firePanel.pumpFeedback = t.pfb === '1';
  firePanel.silenced = t.sil === '1';
  firePanel.supervision = t.sup === '1';
  firePanel.bellFailed = t.bellfail === '1';
  firePanel.sirenSeconds = num(t.sirenm);
  firePanel.silences = num(t.sil_n);
  firePanel.silenceLeft = num(t.sil_left);
  firePanel.link = t.link || 'down';
  firePanel.clock = t.t || '';
  firePanel.clockValid = t.clock !== 'unset';
  firePanel.uptime = num(t.up);
  firePanel.lastSeen = Date.now();
  return true;
}

export function fireStale() {
  // Same reason as the switchboard board: one simulator holds one program, so this
  // board going quiet must show as quiet rather than as its last frame forever.
  return !firePanel.lastSeen || Date.now() - firePanel.lastSeen > 3000;
}

export function fireAlarmCount() {
  return firePanel.zones.filter(c => c === '1').length + (firePanel.manualCall ? 1 : 0);
}

export function fireHealthyLoops() {
  return firePanel.zones.filter(c => c === '0').length;
}

// One line, in the order a person reading a panel wants it: what is wrong, then what
// the panel did about it.
export function fireAlertText() {
  if (fireStale()) return '';
  if (firePanel.level === 'EMERGENCY') {
    return `火警已确认：${firePanel.why || ''} · 泵许可${firePanel.pumpFeedback ? '已闭合' : '闭合中'}`;
  }
  if (firePanel.level === 'ALARM') {
    return `单回路报警，等第二意见：${firePanel.zoneText.join('/')}`;
  }
  if (firePanel.bellFailed) return '警铃被命令但未证明动作：去听铃';
  if (firePanel.level === 'FAULT') {
    return firePanel.armed ? `监管故障：${firePanel.why || '回路异常'}` : '面板处于 BYPASS，输出不响应';
  }
  if (firePanel.silenced) return `已静音 ${firePanel.silenceLeft}s，到点会自动再响`;
  return '';
}
