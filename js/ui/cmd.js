// The page talking back to a board, and the one way it says what came home.
//
// Four panels could each invent their own wording for "the wire carried it", "the
// board did not know that verb" and "the board did it" -- and then a reader would
// have to learn four vocabularies to know whether a switch actually moved anything.
// So every board button on the page comes through here.
import { sendSerialCommand, telemetry } from './esp-panel.js';

const BOARD_NAME = {
  light: '照明', water: '水利', power: '配电', fire: '消防', drone: '无人机', show: '编队',
};

// The board answers in its next frame, which can be a second away, so the answer is
// read on every refresh against the command we last sent at it -- matched by verb,
// because an `ack` left over from somebody else's button is not an answer to this one.
const last = new Map();
const ACK_WAIT_MS = 6000;

export function boardCmd(board, line) {
  // Snapshot what every board was already answering, so an answer that appears later on
  // a board this button never addressed can be told apart from one that was already there.
  const seen = new Map(Object.entries(telemetry).map(([k, f]) => [k, f && f.ack ? String(f.ack) : '']));
  const why = sendSerialCommand(board, line);
  const verb = String(line).split(/[=\s]/)[0];
  last.set(board, { verb, line, sent: !why, why: why || '', at: Date.now(), seen });
}

// { text, cls } or null when nothing has ever been sent at this board.
export function cmdStatus(board) {
  const sent = last.get(board);
  if (!sent) return null;
  const name = BOARD_NAME[board] || board;
  if (!sent.sent) return { text: `没送到${name}板：${sent.why}`, cls: 'cmd-warn' };

  const f = telemetry[board];
  const ack = f && f.ack ? String(f.ack) : '';
  const i = ack.indexOf(':');
  // The board answers with the value it kept (`ok:uv=0.85`), so this matches the verb
  // we sent and shows whatever came attached to it.
  const body = i > 0 ? ack.slice(i + 1) : '';
  if (i > 0 && (body === sent.verb || body.startsWith(`${sent.verb}=`))) {
    // Remember that it answered. The board only carries the ack for six seconds, and
    // when it drops off the wire the fact does not become false again: an expiry is an
    // expiry, not a refusal, and a line that flips to 「没回话」 under a held stick says
    // the board ignored a command it had already carried out.
    sent.answered = ack.slice(0, i) === 'ok' ? body : `no:${body}`;
  }
  if (sent.answered) {
    return String(sent.answered).indexOf('no:') === 0
      ? { text: `${name}板不认「${shown(board, sent.verb, sent.verb)}」这条指令`, cls: 'cmd-warn' }
      : { text: `${name}板已执行 ${shown(board, sent.verb, sent.answered)}`, cls: 'cmd-ok' };
  }
  // Look for the theft on every repaint, not only after the wait has run out: a board
  // carries its `ack` for six seconds, the same six seconds this panel waits, so by the
  // time the wait expires the evidence is off the wire. Measured with the lighting and
  // water boards open together -- the water board answered `no:auto` one second after
  // the lighting button was pressed, and the lighting board's console never grew.
  for (const [k, f] of Object.entries(telemetry)) {
    if (k === board || !f || !f.ack) continue;
    const a = String(f.ack);
    if (a === (sent.seen && sent.seen.get(k) || '')) continue;
    const j = a.indexOf(':');
    const body = j > 0 ? a.slice(j + 1) : '';
    if (body !== sent.verb && !body.startsWith(`${sent.verb}=`)) continue;
    sent.stolen = BOARD_NAME[k] || k;
    break;
  }
  if (sent.stolen) {
    // With more than one simulator instance open this runtime writes the serial console
    // to the instance that was started last: the button's line reaches a different board
    // than the one it belongs to. That is not a timeout, and calling it one would send
    // the operator to check the wrong wire.
    return { text: `${name}板没收到这条 ${sent.verb}：是${sent.stolen}板接走的` +
      `（同时开着几块仿真时，串口只写到最后开启的那一块；只留${name}这块板再按）`, cls: 'cmd-warn' };
  }
  if (Date.now() - sent.at > ACK_WAIT_MS) {
    return { text: `${name}板 ${ACK_WAIT_MS / 1000} 秒内没回话（${sent.verb}）`, cls: 'cmd-warn' };
  }
  return { text: `已把 ${sent.verb} 发给${name}板，等它回话`, cls: '' };
}

// One place decides what an operator's verb becomes on the wire. The drawers and the
// phone door both read this, so a button and a phone cannot drift apart -- that drift
// is exactly what made 悬停 and 返航 look pressed and do nothing.
export const cmd = {
  lightStreet: on => boardCmd('light', `street=${on ? 1 : 0}`),
  lightHouse: on => boardCmd('light', `house=${on ? 1 : 0}`),
  lightAuto: () => boardCmd('light', 'auto'),
  lightClear: () => boardCmd('light', 'clear'),
  lightClock: iso => boardCmd('light', `time=${iso}`),
  waterBarn: on => boardCmd('water', on ? 'resume' : 'stop'),
  waterHouse: on => boardCmd('water', `house=${on ? 1 : 0}`),
  waterOnce: () => boardCmd('water', 'barn=next'),
  waterPeriod: v => boardCmd('water', `period=${v}`),
  waterRun: v => boardCmd('water', `run=${v}`),
  powerPump: on => boardCmd('power', `pump=${on ? 1 : 0}`),
  powerLit: on => boardCmd('power', `lit=${on ? 1 : 0}`),
  powerReclose: () => boardCmd('power', 'reclose'),
  powerStop: () => boardCmd('power', 'stop'),
  powerResume: () => boardCmd('power', 'resume'),
  powerShed: v => boardCmd('power', `shed=${v}`),
  powerSet: (key, v) => boardCmd('power', `${key}=${v}`),
  fireSilence: () => boardCmd('fire', 'silence'),
  fireReset: () => boardCmd('fire', 'reset'),
  fireTest: () => boardCmd('fire', 'test'),
  fireConfirm: v => boardCmd('fire', `confirm=${v}`),
  droneAxes: (f, s, c, y) => boardCmd('drone', `rc=${f},${s},${c},${y}`),
  droneVerb: wire => boardCmd('drone', wire),
};

// A board publishes the thresholds it actually holds as `lim=uv0.85/ov1.10/...`: each
// name rides with its own number, so a reordered list cannot silently mis-pair them.
export function parseLim(text) {
  const out = {};
  for (const part of String(text || '').split('/')) {
    const m = /^([a-z]+)(-?[\d.]+)$/.exec(part);
    if (m) out[m[1]] = Number(m[2]);
  }
  return out;
}

// The verb that travels is not the word the operator read on the button: answering
// 「已执行 hold」 under a button labelled 取消自动 makes the reader re-learn the table to
// check that the right thing happened. Scoped per board on purpose -- `resume` is
// 恢复自动巡航 on the drone, 牛舍定时 on the water board, and one global word for it would
// misname two of them. Verbs that carry a value (`uv=0.87`, `confirm=25`) show the value,
// because that is the part the operator is checking.
const VERB_TEXT = {
  drone: {
    hold: '取消自动', resume: '恢复自动巡航', takeoff: '起飞', patrol: '自动巡检', land: '降落',
    rtl: '返航', arm: '解锁', safe: '上保险', camera: '拍照',
  },
  water: { resume: '牛舍定时开', stop: '牛舍定时关' },
  power: { reclose: '重合闸', stop: '全部停止', resume: '恢复自动' },
  fire: { silence: '静音', reset: '复位', test: '试验' },
};
// A verb in the table is one whose value carries nothing the operator is checking
// (`arm=1`, `camera=1`), so the label replaces the whole body. Everything not listed
// shows what the board kept (`uv=0.87`, `pump=1`, `period=5400`), because that number is
// the point of the row.
const shown = (board, verb, body) => ((VERB_TEXT[board] || {})[verb])
  || ((body || '').includes('=') ? body : (body || verb));

// Paint one panel's answer line. Called from each panel's own refresh, because the
// board answers a second after the click, not on it.
export function paintAck(board, node) {
  if (!node) return;
  const s = cmdStatus(board);
  node.classList.remove('cmd-ok', 'cmd-warn');
  if (!s) { node.textContent = ''; return; }
  node.textContent = s.text;
  if (s.cls) node.classList.add(s.cls);
}

// A safety action gets two clicks. This arms the button in place instead of putting a
// modal over the panel that has to show the answer -- the thing you are confirming is
// the thing the dialog would cover.
export function armConfirm(btn, action, seconds = 5) {
  let armed = false;
  let timer = 0;
  const label = btn.textContent;
  const disarm = () => {
    clearTimeout(timer);
    armed = false;
    btn.classList.remove('dr-armed');
    btn.textContent = label;
  };
  btn.addEventListener('click', () => {
    if (!armed) {
      armed = true;
      btn.classList.add('dr-armed');
      btn.textContent = `再点确认：${label}`;
      timer = setTimeout(disarm, seconds * 1000);
      return;
    }
    disarm();
    action();
  });
}
