// ESP32 board panel, mounted at the bottom of the 电路系统 drawer.
//
// It is a *view* of the simulated board, not a copy: the wiring diagram is
// drawn from the board element's own config (svgUrl + pin coordinates), the
// live level comes from the LED element's `value`, and the onboard GPIO2 is
// read from the board's serial printout because the built-in LED has no
// element on the canvas. The Velxio iframe therefore lives in a persistent
// dock outside the drawer -- the drawer body is replaced on every open, and a
// re-inserted iframe would reload and kill the running simulation.
import { setHouseLights, setStreetLights, streetLightOn, houseLightOn, reapplyFeeders } from '../state/time.js';
import { cmd, paintAck } from './cmd.js';
import { PRESETS, parseTelemetry, lastTelemetryLine, presetCode } from '../state/firmware.js';
import { setBoardUp } from '../state/boards.js';
import { cardGrid, cell, put, statusLine } from './status-cards.js';

// Latest telemetry frame from the embedded board, published for the drone and
// water panels so they do not each scrape the serial buffer.
//
// `light` is the lighting board's own LIGHT frame. Where it exists it is the
// authority for the scene's two light switches: the LED on the diagram only
// shows what one pin is doing, and the board decides more than that.
export const telemetry = { drone: null, water: null, light: null, power: null, fire: null, show: null };
// Which preset the simulator is holding right now. One iframe, one program, so the
// menu has to say which one that is rather than implying all five are running.
export let activePreset = null;

// The app's own locale route. Note the missing trailing slash: /zh-cn/editor/
// falls through to the marketing homepage, /zh-cn/editor serves the editor with
// documentElement.lang=zh-CN and the velxio_locale cookie set.
const SIM_SRC = '/zh-cn/editor';
const MOUNT_ID = 'espHost';

// Chrome we do not want in the dock. The starter overlay stays in the layout
// (visibility, not display) because the board picker still drives it by click;
// getBoundingClientRect keeps working for the same reason.
const HIDE_CSS = `
.new-project-overlay{visibility:hidden !important}
button[title="Toggle menu"],button[aria-label="Toggle menu"]{display:none !important}
`;

const WORDS = {
  compile: /^(compile|编译)/i,
  run: /^(run|运行)/i,
  stop: /^(stop|pause|停止|暂停)/i,
};

const st = {
  street: { on: false, edges: 0, last: 0, period: 0 },
  room: { on: false, edges: 0, last: 0, period: 0 },
  serial: null, cfgKey: '', partsKey: '',
  selected: 'ESP32 DevKit V1', preset: PRESETS[0], boards: [],
};

let dock = null;
let view = null;
let panel = null;

const NS = 'http://www.w3.org/2000/svg';
const mk = (tag, at) => {
  const n = document.createElementNS(NS, tag);
  for (const k in at) n.setAttribute(k, at[k]);
  return n;
};
const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* ---------- one dock, one simulator instance per board ---------- */
// The ranch has five boards and this page used to have one simulator, which made
// "every drawer is driven by its board" something you could only read one at a time.
// Each board gets its own frame now: created the first time you open its drawer, kept
// alive afterwards, so several can run at once. That is also what makes the memory
// gate below load-bearing instead of decorative.
const frames = new Map();
let active = null;

export function simWindow(board) {
  const f = frames.get(board || active);
  return f && f.frame ? f.frame.contentWindow : null;
}
export function simDoc(board) {
  const w = simWindow(board);
  return w ? w.document : null;
}

export function boardPhase(board) {
  const f = frames.get(board);
  return f ? f.phase : 'idle';
}

// ---------- the console's other direction: typing at a board ----------
//
// Everything else in this file reads the board. This writes to it, through the one
// port that exists: the simulator's own serial monitor, whose input row is a real
// line into the emulated UART. It is a *view* being driven -- no simulator internals,
// no framework hooks -- because the compiled firmware is the only thing that can
// decide what a command means, and the firmware reads its console.
//
// The line ending is forced to newline: `consoleCommandPump` in the firmware frames on
// '\n', and the panel's own selector defaults to "no line ending", which would leave
// the verb sitting in the firmware's buffer until the next command arrived.
const NEWLINE = ['nl', 'both', '换行', '新行'];

function setNativeValue(input, value) {
  // React holds this input's value in its own state and only updates it through the
  // setter it installed on the element. Assigning `.value` directly changes the DOM but
  // leaves React's copy stale, so re-rendering -- which happens on the next keystroke
  // anywhere in the panel -- would put the old text back. Going through the prototype's
  // setter and then announcing the change is what a real keystroke does.
  const proto = Object.getPrototypeOf(input);
  const desc = Object.getOwnPropertyDescriptor(proto, 'value');
  if (desc && desc.set) desc.set.call(input, value);
  else input.value = value;
  input.dispatchEvent(new Event('input', { bubbles: true }));
}

function lineEndingSelect(row) {
  // Named by its options, not by position: the editor stacks other selects in the same
  // panel (language, log filter, board kind), and only this one offers "no line ending".
  return [...row.querySelectorAll('select')].find(s => [...s.options].some(o => o.value === 'none')) || null;
}

function sendButton(row) {
  // The editor exposes this slot for its own extensions, which makes it a stabler hook
  // than the label -- a language the regex has not met would otherwise find no button.
  return row.querySelector('[data-velxio-slot="serial-actions"] button')
    || [...row.querySelectorAll('button')].find(b => /^(send|发送)$/i.test((b.textContent || '').trim()))
    || null;
}

// Send one command line to a board's console. Returns a refusal reason, or null.
export function sendSerialCommand(board, line) {
  if (!board) return '没有指定板子';
  if (!frames.has(board)) return `仿真器里没有 ${board} 这块板`;
  if (boardPhase(board) !== 'running') return `${board} 板还没在运行`;

  const doc = simDoc(board);
  if (!doc) return `${board} 的仿真窗口读不到`;

  // "There is a console box but it is greyed out" and "this window has no console box"
  // are different facts and the operator checks different things for each. The old order
  // looked for an enabled input first and then tested `disabled` on the result, so the
  // greyed-out case fell through to 「没找到」 -- a wrong answer about the wire.
  const cands = [...doc.querySelectorAll('input[type=text]')]
    .filter(i => !/搜索|search/i.test(i.placeholder || ''));
  const input = cands.find(i => !i.disabled);
  if (!input) return cands.length
    ? `${board} 的串行输入框是禁用的（这块板的仿真正没在跑：等它编译完并按过运行）`
    : `${board} 的串行控制台还没建起来（仿真窗口刚开，等它把固件跑起来再按）`;

  const row = input.parentElement;
  const sel = row && lineEndingSelect(row);
  const btn = row && sendButton(row);
  if (!btn) return `${board} 的串行发送按钮没找到`;

  if (sel && !NEWLINE.includes(sel.value)) {
    sel.value = 'nl';
    sel.dispatchEvent(new Event('change', { bubbles: true }));
  }
  setNativeValue(input, line);
  try {
    btn.click();
  } catch (e) {
    return `点发送失败：${e.message}`;
  }
  return null;
}

export function runningBoards() {
  return [...frames.keys()];
}

// Chrome-only, and enough here: every frame is same-origin inside this one tab, so
// the renderer's heap is the sum of the simulations. The number is printed in the
// prompt because "内存不够" without a measurement is a guess about someone else's
// machine.
export function memoryLoad() {
  const m = window.performance && window.performance.memory;
  if (!m || !m.jsHeapSizeLimit) return null;
  return {
    usedMB: Math.round(m.usedJSHeapSize / 1048576),
    limitMB: Math.round(m.jsHeapSizeLimit / 1048576),
    pct: m.usedJSHeapSize / m.jsHeapSizeLimit,
  };
}

let heapCeil = 0.82;
// The gate is a question about somebody else's machine: on this one five simulators
// cost 152 MB of a 4 GB budget, so the threshold can never be reached here and an
// untestable branch is a branch that is wrong. This setter exists so the refusal path
// can be driven for real in the browser instead of being described as working.
export function setMemoryCeil(pct) {
  heapCeil = pct > 0 && pct <= 1 ? pct : 0.82;
  return heapCeil;
}

// What stops a new simulator, and why. Returns null when it is fine to open one.
// Memory is the only gate: an instance that is already running keeps running until
// its own tab is clicked, and a fifth board never displaces a fourth.
export function memoryRefused() {
  const m = memoryLoad();
  if (m && m.pct > heapCeil) {
    return { why: `页面堆内存 ${(m.pct * 100).toFixed(0)}% 已过 ${(heapCeil * 100).toFixed(0)}% 线`, mem: m };
  }
  return null;
}

export function closeBoard(board) {
  const f = frames.get(board);
  if (!f) return;
  f.frame.remove();
  frames.delete(board);
  if (f.tab) f.tab.remove();
  setBoardUp(board, false);
  if (board === 'light') reapplyFeeders();
  if (active === board) {
    active = frames.keys().next().value || null;
    // With no instance left there is no board "在仿真", and the 开发板 menu line has
    // to say so rather than keep naming the one that was just closed.
    if (!active) { activePreset = null; st.preset = null; }
    showActive();
  }
  // The frame is gone, so its last frame of telemetry must not keep being read as
  // current by the drawer that used to own it.
  if (board === 'drone') telemetry.drone = null;
  if (board === 'water') telemetry.water = null;
  if (board === 'power') telemetry.power = null;
  if (board === 'fire') telemetry.fire = null;
  if (board === 'light') telemetry.light = null;
}

// Only these phases mean "something is happening": a failed or refused board has
// nothing left to wait for, and a bar that sweeps over it says the opposite.
const WORKING = { creating: 1, loading: 1, compiling: 1 };

function updateBusy() {
  if (!dock) return;
  dock.classList.toggle('busy', !!active && !!WORKING[boardPhase(active)]);
}

export function setBoardPhase(board, phase) {
  const f = frames.get(board);
  if (!f) return;
  const was = f.phase;
  f.phase = phase;
  f.t0 = phase === 'compiling' || phase === 'loading' ? Date.now() : f.t0;
  if (f.tab) f.tab.classList.toggle('esp-tab-busy', !!WORKING[phase]);
  if (f.tab) f.tab.classList.toggle('esp-tab-live', phase === 'running');
  // One write for two things that used to be decided in two places: the bar under
  // the tabs, and whether this board is allowed to move the ranch.
  setBoardUp(board, phase === 'running');
  if (board === active) updateBusy();
  // The gate just flipped, so whatever it holds has to be re-applied now rather
  // than on the next edge: a board that finishes its compile with the lamps
  // already commanded on would otherwise leave the yard dark until they changed.
  if (board === 'light' && was !== phase) reapplyFeeders();
}

export function boardStatusText(board) {
  const f = frames.get(board);
  if (!f) return starting.has(board)
    ? '正在启动 · 这块板的仿真实例已经在编译'
    : '未开启 · 打开这个抽屉就会自动开始';
  if (f.phase === 'refused') {
    const m = memoryLoad();
    return `内存不够：${f.error}${m ? `（已用 ${m.usedMB}/${m.limitMB} MB）` : ''} · 点上面亮着的页签关掉一块板再试`;
  }
  return boardProgress(board);
}

export function boardProgress(board) {
  const f = frames.get(board);
  if (!f) return null;
  const TEXT = {
    idle: '未开启', creating: '创建仿真实例…', loading: '载入板卡并写入固件…',
    compiling: '编译中（首次约 30-75 秒）…', running: '运行中', failed: '启动失败',
    stalled: '没在时限内上报（并发的仿真实例太多）',
    refused: '内存不够，未开启',
  };
  const label = TEXT[f.phase] || f.phase;
  // A stopped clock does not accumulate: 已用 N 秒 only belongs on a phase that is
  // still going.
  if (!WORKING[f.phase]) return label;
  return `${label} 已用 ${Math.round((Date.now() - f.t0) / 1000)} 秒`;
}

export function ensureDock() {
  if (dock) return dock;

  dock = el('div', 'esp-dock');
  const head = el('div', 'esp-dock-head');
  head.appendChild(el('span', 'esp-dock-title', 'ESP32 仿真'));
  const tabs = el('div', 'esp-dock-tabs');
  const toggle = el('button', 'esp-dock-btn', '收起');
  // The simulator is 660 px wide and the circuit on it is drawn at that scale: to
  // read a 12-wire harness you have to be able to make the window the screen.
  const maximize = el('button', 'esp-dock-btn', '最大化');
  head.appendChild(tabs);
  head.appendChild(maximize);
  head.appendChild(toggle);
  dock.appendChild(head);
  dock.maxBtn = maximize;
  maximize.addEventListener('click', () => setDockMax(!dock.classList.contains('max')));

  const load = el('div', 'esp-dock-load');
  load.appendChild(el('i'));
  dock.appendChild(load);
  dock.loadBar = load;

  const stack = el('div', 'esp-dock-stack');
  dock.appendChild(stack);
  dock.tabs = tabs;
  dock.stack = stack;

  dock.foldBtn = toggle;
  toggle.addEventListener('click', () => setDockOpen(dock.classList.contains('folded')));

  document.getElementById('app').appendChild(dock);
  return dock;
}

function showActive() {
  if (!dock) return;
  // The harness diagram, the picker label and the note all describe the board you
  // are looking at, not the last one that finished starting -- two boards compiling
  // at once used to leave the diagram showing whichever one wrote last.
  const p = PRESETS.find(x => x.id === active);
  if (p) { st.preset = p; st.selected = p.board; activePreset = p; }
  for (const [id, f] of frames) {
    f.frame.classList.toggle('esp-frame-hidden', id !== active);
    if (f.tab) f.tab.classList.toggle('sel', id === active);
  }
  updateBusy();
}

// Opening a board's drawer lands here. A board that is already running is only
// switched into view; a board that is not gets its own simulator, and if the page
// cannot afford one, that is said out loud instead of the tab quietly doing nothing.
export function openBoard(board) {
  const d = ensureDock();
  if (frames.has(board)) {
    active = board;
    showActive();
    return { ok: true, reused: true };
  }
  const stop = memoryRefused();
  if (stop) {
    const f = { frame: null, phase: 'refused', t0: Date.now(), error: stop.why };
    frames.set(board, f);
    addTab(board, f);
    active = board;
    showActive();
    return { ok: false, refused: stop };
  }
  const frame = el('iframe', 'esp-dock-frame');
  frame.src = SIM_SRC;
  frame.setAttribute('title', `Velxio simulator · ${board}`);
  d.stack.appendChild(frame);
  const f = { frame, phase: 'creating', t0: Date.now(), error: '' };
  frames.set(board, f);
  addTab(board, f);
  active = board;
  showActive();
  return { ok: true, reused: false };
}

function addTab(board, f) {
  const p = PRESETS.find(x => x.id === board);
  const t = el('button', 'esp-tab', p ? p.label.split(' ·')[0] : board);
  t.title = '点击查看这块板；再次点击关闭它的仿真实例';
  t.addEventListener('click', () => {
    if (boardPhase(board) === 'refused' || boardPhase(board) === 'idle') { openBoard(board); return; }
    if (active === board) { closeBoard(board); return; }
    active = board;
    showActive();
  });
  f.tab = t;
  dock.tabs.appendChild(t);
}

// The dock follows the 开发板 drawer: opening one expands the other, closing
// folds it back. Folding does not stop the simulation -- measured earlier, the
// LED keeps toggling while the frame is display:none.
export function setDockOpen(open) {
  if (!dock) return;
  dock.classList.toggle('folded', !open);
  dock.foldBtn.textContent = open ? '收起' : '展开';
}

// Full-screen the simulator without touching which boards are running: this is a
// window size, not a power switch, and nothing here reloads a frame.
export function setDockMax(on) {
  if (!dock) return;
  dock.classList.toggle('max', on);
  dock.maxBtn.textContent = on ? '还原' : '最大化';
  dock.maxBtn.setAttribute('aria-pressed', String(on));
}

/* ---------- the harness: what each pin is bolted to on the ranch ---------- */
// The simulator's canvas is the same blink example for every preset, so the wiring
// the panel shows cannot come from the canvas -- it comes from the board's own
// board.h, carried in as PRESETS[].wiring. The one part the simulator really renders
// is marked `sim`, because that is the only line here that can be checked against
// the picture instead of against the code.
const KIND_COLOR = { out: '#7dd3a0', in: '#8ab4ff', adc: '#f0c674', bus: '#b9a7e0' };
const KIND_NAME = { out: '输出', in: '输入', adc: '模拟', bus: '总线' };

function truthy(v) { return v === '1' || v === 'on' || v === 'true'; }

// One row's live state, resolved from whichever frame this board publishes. Returns
// null when there is no frame yet, which is drawn as an empty dot rather than as
// "off" -- the difference between "the valve is shut" and "nothing has told us".
function harnessState(entry) {
  const preset = st.preset;
  if (!preset || !entry.live) return null;
  const key = entry.live;
  // The lighting board's two relays are the only rows whose state this module
  // already tracks itself. The scene calls the second one `room`, the wiring table
  // calls it `house` after the firmware's PIN_RELAY_HOUSE, so the mapping is
  // explicit -- reading st['house'] here threw and took the whole drawer with it.
  const LEDGE = { street: 'street', house: 'room' };
  if (preset.id === 'light' && LEDGE[key]) {
    return st[LEDGE[key]].on ? { on: true } : { on: false };
  }
  const t = { light: telemetry.light, drone: telemetry.drone, water: telemetry.water,
              power: telemetry.power, fire: telemetry.fire, show: telemetry.show }[preset.id];
  if (!t) return null;
  const raw = t[key];
  if (raw === undefined || raw === '') return null;
  if (key === 'z0' || key === 'z1' || key === 'z2' || key === 'z3') {
    const z = String(t.z || '0000');
    const d = z[Number(key[1])] || '0';
    return { on: d === '1', warn: d !== '0', text: { '0': '正常', '1': '报警', '2': '断路', '3': '短路', '4': '模糊' }[d] || d };
  }
  const n = Number(raw);
  if (Number.isFinite(n) && !['pump', 'lit', 'brk', 'arm', 'siren', 'strb', 'barn', 'house', 'mcp', 'flow'].includes(key)) {
    return { on: n > 0, text: `${raw}${/^(va|vb|vc)$/.test(key) ? ' V' : /^i/.test(key) ? ' A' : ''}` };
  }
  return { on: truthy(raw) };
}

function buildHarness(preset) {
  const rows = (preset && preset.wiring) || [];
  const rowH = 26, w = 330;
  const h = Math.max(rowH, rows.length * rowH + 6);
  const svg = mk('svg', { viewBox: `0 0 ${w} ${h}`, preserveAspectRatio: 'xMidYMid meet',
                          class: 'esp-svg esp-svg-harness' });
  rows.forEach((e, i) => {
    const y = i * rowH + 14;
    svg.appendChild(mk('line', { x1: 4, y1: y, x2: 58, y2: y, stroke: KIND_COLOR[e.k] || '#7d8896',
                                 'stroke-width': 1.4, 'stroke-linecap': 'round' }));
    const pin = mk('text', { x: 62, y: y + 3.4, class: 'esp-h-pin' });
    pin.textContent = e.p;
    svg.appendChild(pin);
    svg.appendChild(mk('rect', { x: 118, y: y - 9, width: w - 126, height: 18, rx: 2,
                                 fill: '#101720', stroke: '#2a3644', 'stroke-width': 1 }));
    const dot = mk('circle', { cx: 127, cy: y, r: 3.2, fill: '#39434f', class: 'esp-h-dot' });
    // Indexed, not keyed by the telemetry field: most rows have no live field at
    // all, and matching those on an empty string hands every one of them the first
    // entry that also had none.
    dot.dataset.row = String(i);
    svg.appendChild(dot);
    const name = mk('text', { x: 136, y: y + 3.4, class: 'esp-h-name' });
    name.textContent = e.d;
    svg.appendChild(name);
    if (e.sim) {
      const tag = mk('text', { x: w - 10, y: y + 3.4, class: 'esp-h-sim', 'text-anchor': 'end' });
      tag.textContent = '仿真有';
      svg.appendChild(tag);
    }
  });
  return svg;
}

function syncHarness() {
  if (!panel) return;
  const key = st.preset ? st.preset.id : '';
  if (key !== panel.harnessKey) {
    panel.harnessKey = key;
    panel.harnessHost.textContent = '';
    if (st.preset) {
      panel.harnessHost.appendChild(el('div', 'esp-h-title',
        `${st.preset.label.split(' ·')[0]} · 引脚接线（取自 firmware/${key}/src/board.h）`));
      panel.harnessHost.appendChild(buildHarness(st.preset));
    }
  }
  if (!st.preset) return;
  [...panel.harnessHost.querySelectorAll('.esp-h-dot')].forEach(dot => {
    const entry = (st.preset.wiring || [])[Number(dot.dataset.row)];
    const s = entry ? harnessState(entry) : null;
    dot.setAttribute('fill', !s ? '#39434f' : s.warn ? '#ff8b6b' : s.on ? '#6ee7a0' : '#4a5563');
    const name = dot.nextElementSibling;
    if (name && name.classList.contains('esp-h-name') && entry) {
      name.textContent = s && s.text ? `${entry.d} · ${s.text}` : entry.d;
    }
  });
}

/* ---------- wiring diagram, drawn from the board element's own geometry ---------- */
function buildView(cfg) {
  const svg = mk('svg', {
    viewBox: `-86 -12 ${cfg.w + 200} ${cfg.h + 30}`,
    preserveAspectRatio: 'xMidYMid meet',
    class: 'esp-svg',
  });
  svg.appendChild(mk('image', { href: cfg.svgUrl, x: 0, y: 0, width: cfg.w, height: cfg.h }));

  const pads = new Map();
  for (const p of cfg.pins) {
    const key = `${p.x},${p.y}`;
    if (!pads.has(key)) {
      const left = p.x < cfg.w / 2;
      const node = { x: p.x, y: p.y, left, names: [] };
      node.dot = svg.appendChild(mk('circle', { cx: p.x, cy: p.y, r: 2.4, fill: '#0a0f15', stroke: '#7d8896', 'stroke-width': 0.7 }));
      node.label = svg.appendChild(mk('text', {
        x: left ? p.x - 5.5 : p.x + 5.5, y: p.y + 2.4, 'text-anchor': left ? 'end' : 'start',
        class: 'esp-pinlabel',
      }));
      pads.set(key, node);
    }
    pads.get(key).names.push(p.name);
  }
  for (const n of pads.values()) n.label.textContent = n.names.join('/');

  const byName = name => [...pads.values()].find(n => n.names.includes(name));
  const v = { pads, g4: byName('4'), g2: byName('2'), wires: [] };

  if (v.g4) {
    const y = v.g4.y;
    const wire = d => svg.appendChild(mk('path', { d, fill: 'none', stroke: '#3b4756', 'stroke-width': 1.6, 'stroke-linecap': 'round' }));
    v.wires.push(wire(`M${v.g4.x + 3} ${y} H162`), wire(`M202 ${y} H218`), wire(`M236 ${y} h14 v46 h-42`));
    v.res = svg.appendChild(mk('rect', { x: 162, y: y - 5, width: 40, height: 10, fill: '#131b24', stroke: '#7d8896', 'stroke-width': 1 }));
    label(svg, 'r-led', 182, y - 9, 'middle');
    label(svg, 'GPIO4', 150, y - 9, 'middle');
    v.glow = svg.appendChild(mk('circle', { cx: 227, cy: y, r: 13, fill: '#ff5a5a', opacity: 0 }));
    v.ledBody = svg.appendChild(mk('path', { d: `M218 ${y - 8} L218 ${y + 8} L236 ${y} Z`, fill: '#4a2224', stroke: '#c96a6a', 'stroke-width': 1 }));
    svg.appendChild(mk('path', { d: `M236 ${y - 8} L236 ${y + 8}`, stroke: '#c96a6a', 'stroke-width': 1.4, fill: 'none' }));
    for (const a of [[226, y - 9, 231, y - 16], [231, y - 6, 237, y - 12]]) {
      svg.appendChild(mk('path', { d: `M${a[0]} ${a[1]} L${a[2]} ${a[3]}`, stroke: '#5d6a79', 'stroke-width': 1, fill: 'none' }));
    }
    svg.appendChild(mk('path', {
      d: `M194 ${y + 46} h24 M198 ${y + 50} h16 M202 ${y + 54} h8`,
      stroke: '#7d8896', 'stroke-width': 1.4, fill: 'none', 'stroke-linecap': 'round',
    }));
    label(svg, 'GND', 214, y + 49, 'start');
  }
  if (v.g2) {
    v.onboard = svg.appendChild(mk('rect', { x: v.g2.x - 11, y: v.g2.y - 3.5, width: 7, height: 7, rx: 1.2, fill: '#16232e', stroke: '#41505f', 'stroke-width': 0.7 }));
    label(svg, '板载', v.g2.x - 14.5, v.g2.y + 2.4, 'end');
  }
  v.svg = svg;
  return v;
}

function label(svg, text, x, y, anchor) {
  const t = svg.appendChild(mk('text', { x, y, 'text-anchor': anchor, class: 'esp-netlabel' }));
  t.textContent = text;
  return t;
}

function paintView() {
  if (!view) return;
  const on = st.street.on;
  if (view.wires.length) {
    const c = on ? '#ffcf6b' : '#3b4756';
    view.wires.forEach(w => w.setAttribute('stroke', c));
    view.ledBody.setAttribute('fill', on ? '#ff6b6b' : '#4a2224');
    view.glow.setAttribute('opacity', on ? 0.32 : 0);
    view.g4.dot.setAttribute('stroke', on ? '#ffcf6b' : '#7d8896');
  }
  if (view.onboard) view.onboard.setAttribute('fill', st.room.on ? '#69c8ff' : '#16232e');
}

/* ---------- reading the simulated board ---------- */
function collectLeds(root, acc = []) {
  for (const n of root.querySelectorAll('*')) {
    if (n.tagName === 'WOKWI-LED') acc.push(n);
    if (n.shadowRoot) collectLeds(n.shadowRoot, acc);
  }
  return acc;
}

function ledLit(led) {
  if (typeof led.value === 'boolean') return led.value;
  const light = led.shadowRoot && led.shadowRoot.querySelector('.light');
  if (!light) return null;
  const cs = simWindow().getComputedStyle(light);
  if (cs.display === 'none') return false;
  const op = parseFloat(cs.opacity || '1');
  return Number.isFinite(op) ? op > 0.45 : true;
}

function serialState(doc) {
  const pres = doc.querySelectorAll('.simulator-panel pre');
  for (let i = pres.length - 1; i >= 0; i--) {
    const t = pres[i].textContent || '';
    const on = t.lastIndexOf('LED ON'), off = t.lastIndexOf('LED OFF');
    if (on < 0 && off < 0) continue;
    return on > off ? 'ON' : 'OFF';
  }
  return null;
}

function track(c, on, now) {
  if (c.on === on) return;
  c.on = on;
  if (c.last) {
    const dt = now - c.last;
    if (dt > 120) c.period = c.period ? c.period * 0.6 + dt * 0.4 : dt;
  }
  c.last = now;
  c.edges++;
}

function serialText(doc) {
  let t = '';
  for (const p of doc.querySelectorAll('.simulator-panel pre')) t += p.textContent || '';
  return t;
}

// Which board owns which frame line. Parsing every prefix out of every dock would
// let one simulator's DRONE line answer for another board's silence, and the whole
// point of five instances is that each drawer answers to its own.
const OWNS = { light: 'light', drone: 'drone', water: 'water', power: 'power', fire: 'fire',
  // The show airframe's frames are `SHOW,`, and the key is 'show' -- but the show
  // drawer, not this one, is what reads the upload ack out of them.
  show: 'show' };

function tick() {
  for (const [board, f] of frames) {
    if (!f.frame) continue;
    const doc = simDoc(board);
    if (!doc) continue;
    injectChrome(board);
    const text = serialText(doc);
    const key = OWNS[board];
    if (!key) continue;
    const line = lastTelemetryLine(text, key.toUpperCase());
    if (line) {
      // One count per distinct frame line, not per tick: this loop runs at 10 Hz and
      // the transcript keeps the last frame in it, so "a frame exists" would count
      // the same publish a dozen times. The line itself is the event.
      if (line !== f.lastLine) {
        const now = Date.now();
        if (f.lastAt && now > f.lastAt) {
          const dt = now - f.lastAt;
          if (dt < 5000) f.frameMs = f.frameMs ? f.frameMs * 0.7 + dt * 0.3 : dt;
        }
        f.lastAt = now;
        f.lastLine = line;
        f.frames = (f.frames || 0) + 1;
      }
      telemetry[key] = parseTelemetry(line, key.toUpperCase());
      if (f.phase !== 'running') setBoardPhase(board, 'running');
    }
  }
  // Before the canvas is looked at at all: these six readings describe the board,
  // and they have to keep saying "未开启" after its simulator is closed rather than
  // freeze on whatever the last canvas happened to show.
  renderWiring();
  renderStatus();
  const doc = simDoc('light') || simDoc();
  if (!doc) return;
  if (!panel) return;
  if (!st.boards.length) {
    const b = readBoards();
    if (b.length) {
      st.boards = b;
      renderPicker();
    }
  }
  const board = doc.querySelector('velxio-esp32');
  const cfg = board && board.config;
  const key = cfg ? `${cfg.svgUrl}|${cfg.w}|${cfg.h}|${cfg.pins.length}` : '';
  if (key && key !== st.cfgKey) {
    view = buildView(cfg);
    st.cfgKey = key;
    panel.svgHost.textContent = '';
    panel.svgHost.appendChild(view.svg);
  }
  if (!board) { view = null; st.cfgKey = ''; }
  syncHarness();

  const leds = board ? collectLeds(doc) : [];
  st.serial = serialState(doc);
  const states = leds.map(ledLit);
  const now = performance.now();
  const wasStreet = st.street.on, wasRoom = st.room.on;
  // A board that reports its decisions outranks a board whose pins we can only
  // peek at, so the LIGHT frame wins wherever the firmware emits one.
  const lit = telemetry.light;
  const streetLevel = lit ? lit.street === 'on' : states[0] === true;
  const roomLevel = lit ? lit.house === 'on'
    : (leds.length > 1 ? states[1] : st.serial === null ? states[0] : st.serial === 'ON');
  track(st.street, streetLevel, now);
  track(st.room, roomLevel, now);
  if (lit && (st.street.on !== wasStreet || st.room.on !== wasRoom)) driveFromBoard();

  paintView();
}

// The six readings of the dock itself. They used to be read off the simulator's
// canvas -- "实测周期" was the blink example's LED, "翻转" its edge count -- which
// reported the same two numbers for a fire panel as for a lighting panel. Everything
// here now comes from this board's own firmware and its own frame lines.
function renderStatus() {
  if (!panel) return;
  const p = st.preset;
  const f = frames.get(active);
  const phase = boardPhase(active);
  const key = active ? OWNS[active] : null;
  const t = key ? telemetry[key] : null;

  put(panel.status, !active ? '未开启'
    : phase === 'running' ? '运行中'
    : phase === 'failed' ? '启动失败'
    : phase === 'refused' ? '内存不够'
    : '启动中', null,
    phase === 'running' ? 'dr-live' : phase === 'failed' || phase === 'refused' ? 'dr-dead' : '');
  put(panel.firmware, p ? p.src.replace(/^.*\/build\//, '').replace(/\.ino$/, '') : '-', p ? '.ino' : null);
  put(panel.wires, p && p.wiring ? p.wiring.length : '-', p && p.wiring ? '路' : null);
  put(panel.period, f && f.frameMs ? Math.round(f.frameMs) : '-', f && f.frameMs ? 'ms' : null);
  put(panel.frames, f && f.frames ? f.frames : '-', f && f.frames ? '帧' : null);
  put(panel.signal, t && key ? key.toUpperCase() : '-', t && key ? '帧' : null, t && key ? 'dr-live' : '');

  panel.line.textContent = !p ? '还没有选中板卡：打开任意一个子系统抽屉就会启动它的板子'
    : `板卡 ${p.kit || p.board} · 固件 ${p.src} · 引脚定义取自 firmware/${p.src.replace(/^.*\/build\/|\.ino$/g, '')}/src/board.h` +
      (f && f.skipped && f.skipped.length ? ` · 画布上没有对应真实件：${f.skipped.length} 路（见 BOM）` : '');

  paintAck('light', panel.lampAck);
}

// The parts table: what this board's board.h actually wires, and what the board is
// reporting on that pin right now. The example canvas's three parts (esp32, r-led,
// led-ext) are not this board's inventory and are not listed here.
function renderWiring() {
  const p = st.preset;
  if (!panel) return;
  const key = active ? OWNS[active] : null;
  const t = key ? telemetry[key] : null;
  if (!p || !p.wiring) return;
  const dir = p.src.replace(/^.*\/build\//, '').replace(/\.ino$/, '');
  const rows = p.wiring.map(r => {
    let val = '—', src = `board.h`;
    if (r.live && t) {
      const v = t[r.live];
      val = v === undefined ? '本帧没有' : String(v);
      src = `board.h · 帧 ${r.live}=`;
    } else if (r.live) {
      val = '等待上报';
      src = `board.h · 帧 ${r.live}=`;
    }
    return [r.p, r.d, val, src];
  });
  const json = JSON.stringify([dir, rows]);
  if (json === st.partsKey) return;
  st.partsKey = json;
  panel.tbody.textContent = '';
  for (const [pin, desc, val, src] of rows) {
    const tr = panel.tbody.insertRow();
    tr.insertCell().textContent = pin;
    tr.insertCell().textContent = desc;
    const c = tr.insertCell();
    c.className = val === 'on' || val === '1' ? 'esp-hi' : '';
    c.textContent = val;
    tr.insertCell().textContent = src;
  }
}

/* ---------- driving the embedded editor ---------- */
function fire(node) {
  const r = node.getBoundingClientRect();
  const o = {
    bubbles: true, cancelable: true, button: 0, pointerId: 1, isPrimary: true, pointerType: 'mouse',
    clientX: r.x + r.width / 2, clientY: r.y + r.height / 2,
  };
  for (const t of ['pointerover', 'pointerdown', 'pointerup']) node.dispatchEvent(new PointerEvent(t, o));
  for (const t of ['mousedown', 'mouseup', 'click']) node.dispatchEvent(new MouseEvent(t, o));
}

const textOf = n => (n.textContent || '').trim().replace(/\s+/g, ' ');
const nameOf = n => (n.title || n.getAttribute('aria-label') || '').trim().replace(/\s+/g, ' ');

// A narrow viewport collapses the editor toolbar to icons, so the action name
// moves into `title`. Titles are matched as a prefix: "Add a board to compile"
// contains "compile" but is the Add button, not the Compile button.
// Every one of these takes the board it works on. They used to read `active`, which
// is whichever tab you last looked at: with two boards starting at the same time the
// second one's firmware was written into the first one's editor, and its Run press
// landed on whichever canvas happened to be in front.
function simButton(test, board) {
  const doc = simDoc(board);
  if (!doc) return undefined;
  return [...doc.querySelectorAll('button')].find(b => {
    const r = b.getBoundingClientRect();
    return r.width > 1 && r.height > 1 && test(textOf(b), nameOf(b));
  });
}

function injectChrome(board) {
  const doc = simDoc(board);
  if (!doc || !doc.head || doc.getElementById('ranch-hide-chrome')) return;
  const style = doc.createElement('style');
  style.id = 'ranch-hide-chrome';
  style.textContent = HIDE_CSS;
  doc.head.appendChild(style);
}

// The board list is read from the app's own picker cards, so the dropdown can
// never drift from what the simulator actually offers.
function readBoards(board) {
  const doc = simDoc(board);
  if (!doc) return [];
  return [...doc.querySelectorAll('.new-project-card')].map(c => ({
    name: (c.querySelector('.new-project-card-name') || {}).textContent?.trim() || '',
    desc: (c.querySelector('.new-project-card-desc') || {}).textContent?.trim() || '',
    node: c,
  })).filter(b => b.name && b.name !== 'Blank project');
}

// The picker's cards only exist while the dialog is mounted, and in this narrow
// layout the "new workspace" entry lives inside the hamburger menu that the
// panel hides -- so switching boards reloads the dock to get the boot picker
// back. A board swap restarts the simulation anyway.
async function reopenPicker(board) {
  // The frame of the board being started, not "the dock's" frame: with one dock
  // holding several simulators there is no single iframe to reload, and reloading
  // the wrong one leaves this board's canvas without a chip on it.
  const f = board && frames.get(board) ? frames.get(board) : null;
  if (!f || !f.frame) return;
  f.frame.src = SIM_SRC;
  for (let i = 0; i < 60; i++) {
    await sleep(300);
    const d = simDoc(board);
    if (d && d.querySelector('.new-project-card')) return;
  }
}

async function findBoardCard(name, board) {
  const doc = simDoc(board);
  if (!doc) return undefined;
  const match = () => readBoards(board).find(b => b.name === name);
  if (!match()) await reopenPicker(board);
  const card = match();
  return card && card.node;
}

// The editor is Monaco, reachable through the frame's own global; writing the
// preset firmware replaces the example sketch before the compile step.
async function writeFirmware(code, board) {
  const w = simWindow(board);
  const model = w && w.monaco && w.monaco.editor && w.monaco.editor.getModels()[0];
  if (!model) return '代码模型未就绪';
  model.setValue(code);
  return `固件已写入 ${code.split('\n').length} 行 / ${(code.length / 1024).toFixed(0)} KB`;
}

async function applyPreset(preset, board) {
  // The board's own circuit, drawn from its board.h, if the build made one. When it
  // loads, the example-card path below is skipped: clicking that card would replace
  // the circuit again with the blink example.
  const drawn = await loadCircuit(preset);
  let code = null;
  try {
    code = await presetCode(preset);
  } catch (e) {
    return `${drawn || '画布未就绪'} · ${e.message}`;
  }
  if (!drawn) {
    const loaded = await loadBoard(preset.board, board);
    if (!code) return `${loaded} · 使用例程自带代码`;
    await sleep(900);
    return `${loaded} · ${await writeFirmware(code, board)}`;
  }
  if (!code) return `${drawn} · 没有生成固件，先跑 node tools/bundle.mjs`;
  // The draft carries sketch.ino as well, but the text the editor was given at
  // restore time is not necessarily what the bundle holds a second later, so the
  // firmware is still written explicitly.
  for (let i = 0; i < 20; i++) {
    const w = simWindow(board);
    if (w && w.monaco && w.monaco.editor && w.monaco.editor.getModels().length) break;
    await sleep(300);
  }
  return `${drawn} · ${await writeFirmware(code, board)}`;
}

const canvasParts = doc => [...doc.querySelectorAll('*')].filter(n => /^(WOKWI|VELXIO)-/.test(n.tagName));

// Hand the editor a project of our own through the one channel it reads at mount:
// a same-origin draft in the frame's sessionStorage, then a reload. The project
// format is the app's own export format, so nothing here is invented -- and the
// part count is read back afterwards, because an unknown pinName or metadataId is
// dropped silently rather than reported.
async function loadCircuit(preset) {
  let project;
  try {
    const r = await fetch(`/firmware/build/circuits/${preset.id}.json`, { cache: 'no-store' });
    if (!r.ok) return null;
    project = await r.json();
  } catch {
    return null;
  }
  const w = simWindow(preset.id);
  if (!w || !project.components) return null;
  try {
    w.sessionStorage.setItem('velxio_ws_draft', JSON.stringify(project));
    w.sessionStorage.setItem('velxio_ws_restore', '1');
  } catch (e) {
    return `画布工程写入失败：${e.message}`;
  }
  w.location.reload();
  const want = project.components.length;
  for (let i = 0; i < 90; i++) {
    await sleep(300);
    const doc = simDoc(preset.id);
    if (!doc) continue;
    const got = canvasParts(doc).length;
    if (got >= want) {
      for (const c of [st.street, st.room]) Object.assign(c, { on: false, edges: 0, last: 0, period: 0 });
      st.cfgKey = '';
      st.partsKey = '';
      const f = frames.get(preset.id);
      if (f) f.skipped = project.$skipped || [];
      return `画布按 board.h 重画：${got} 个元件 / ${project.wires.length} 根线`;
    }
  }
  return `画布只放上了例程元件（要 ${want} 个）：这一版退回例程画布，电路没有改成真的`;
}

async function loadBoard(name, board) {
  const doc = simDoc(board);
  if (!doc) return '仿真窗口没打开';
  const card = await findBoardCard(name, board);
  if (!card) return `例程卡片里没有「${name}」`;
  fire(card);
  await sleep(2600);
  const parts = [...doc.querySelectorAll('*')].filter(n => /^(WOKWI|VELXIO)-/.test(n.tagName)).map(n => n.id);
  if (!parts.length) return '点了卡片但画布上没有元件';
  // A new board is a new circuit: the old blink period and edge counts would
  // otherwise keep being reported against it.
  for (const c of [st.street, st.room]) Object.assign(c, { on: false, edges: 0, last: 0, period: 0 });
  st.cfgKey = '';
  st.partsKey = '';
  return `已载入 ${parts.join(' + ')}`;
}

async function simStep(test, what, board) {
  const b = simButton(test, board);
  if (!b) return `找不到 ${what} 按钮`;
  fire(b);
  await sleep(1500);
  return `${what} 已按下`;
}

const compileStep = board => simStep((own, named) => WORDS.compile.test(own) || WORDS.compile.test(named), 'Compile', board);
const runStep = board => simStep((own, named) => WORDS.run.test(own) || WORDS.run.test(named), 'Run', board);
const stopStep = board => simStep((own, named) => WORDS.stop.test(own) || WORDS.stop.test(named), '停止', board);

// Starting a board: pick the board if the canvas is
// empty, compile, run -- the lights then follow the firmware without any
// further clicking.
// An ESP32 compile of these bundles measured 30-75 s in this container. The serial
// panel's <pre> only comes into the DOM once a simulation has actually started, so
// it is the outcome to wait for -- firing Compile and then Run 1.5 s later, which is
// what this used to do, presses Run while the compiler is still working and reports
// "started" either way.
async function waitSerial(board, timeoutMs) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const doc = simDoc(board);
    const pre = doc && doc.querySelector('.simulator-panel pre');
    if (pre && (pre.textContent || '').trim().length) return true;
    await sleep(1000);
  }
  return false;
}

// Boards start concurrently: the only thing that stops a new one is the memory gate
// in openBoard, and a board that is already running keeps running until its tab is
// clicked. `starting` replaces the old single `busy` flag plus queue, which serialised
// five independent simulators behind whichever compile happened to be first.
const starting = new Set();

// The note line lives in the 开发板 drawer's own markup, and it describes the board
// you are looking at -- so a background start does not write over it.
const say = (board, text) => { if (panel && board === active) panel.note.textContent = text; };

// Run compiles first when it has to -- the button's own label is 「运行 (如需则自动编译)」.
// Pressing Compile and then Run 1.5 s later was the bug: measured in the console, the
// compile job finished 3 s after it was queued and the Run press that had already
// landed was swallowed, so the simulation never started. One press, then the outcome.
// Waiting for the board to say so itself, rather than for "the serial panel has
// text in it": that panel keeps the output of whatever ran here before, so the old
// check reported success a second after the press -- while this board was still
// compiling -- and runPreset then stamped it 启动失败 two seconds before it started
// reporting. tick() is the only thing that declares a board running.
async function waitRunning(board, timeoutMs) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    if (boardPhase(board) === 'running') return true;
    await sleep(1000);
  }
  return false;
}

async function compileAndRun(board) {
  say(board, '编译并启动中…');
  say(board, await runStep(board));
  // Five QEMU instances on one backend starve each other: measured, three of five
  // reached their first frame and two sat at 483 and 357 characters of serial with
  // nothing more coming. So the wait grows with how many boards are starting at the
  // same moment, and a timeout says what was measured instead of calling it a failure.
  const budget = 120000 + 60000 * Math.max(0, starting.size - 1);
  const up = board ? await waitRunning(board, budget) : await waitSerial(board, budget);
  const chars = up ? 0 : ((serialText(simDoc(board)) || '').length);
  say(board, up
    ? (board ? '仿真运行中，本板已在上报' : '仿真运行中，串口有输出')
    : (board ? `按了运行，${Math.round(budget / 1000)} s 内没有等到 ${board.toUpperCase()} 帧（串口停在 ${chars} 字符）：并发太多时切到这一页会快一些`
             : '按了运行，但串口没有输出：打开「电路」页看报错'));
  return up;
}

// Another drawer asks for a board by preset id: put its firmware in its own
// simulator, compile it, run it. Five boards can be doing that at the same moment --
// each has its own iframe -- so nothing here waits for another board's compile. What
// stops a new one is the memory gate in openBoard, and nothing stops a running one
// except its own tab being clicked.
export async function runPreset(id) {
  const p = PRESETS.find(x => x.id === id);
  if (!p) return;
  if (starting.has(id)) return;
  const opened = openBoard(id);
  if (!opened.ok) {
    say(id, `内存不够，未开启这块板：${opened.refused.why}（已用 ${opened.refused.mem ? opened.refused.mem.usedMB : '?'} MB）`);
    return;
  }
  const already = frames.get(id).phase === 'running' && opened.reused;
  if (already) { showActive(); return; }
  starting.add(id);
  try {
    setBoardPhase(id, 'loading');
    say(id, `载入 ${p.label}…`);
    say(id, await applyPreset(p, id));
    renderPicker();
    setBoardPhase(id, 'compiling');
    const up = await compileAndRun(id);
    if (boardPhase(id) !== 'running') setBoardPhase(id, up ? 'failed' : 'stalled');
  } finally {
    starting.delete(id);
    showActive();
  }
}

/* ---------- the lighting board drives the two switches ---------- */
function driveFromBoard() {
  setStreetLights(st.street.on);
  setHouseLights(st.room.on);
}

// What the lighting board's `time=` verb asks for: year-month-dayThour:minute:second
// in this machine's local clock, which is the shape its sscanf accepts and checks.
function ranchClock() {
  const p = n => String(n).padStart(2, '0');
  const d = new Date();
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}` +
    `T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

/* ---------- board picker: a self-drawn listbox, not a native <select> ------ */
function closePickers(except) {
  for (const w of document.querySelectorAll('.esp-pick.open')) if (w !== except) w.classList.remove('open');
}
document.addEventListener('click', () => closePickers(null));

function renderPicker() {
  if (!panel) return;
  const host = panel.pickerHost;
  host.textContent = '';

  const wrap = el('div', 'esp-pick');
  const btn = el('button', 'esp-pick-btn', st.preset ? st.preset.label : st.selected);
  btn.setAttribute('aria-haspopup', 'listbox');
  btn.setAttribute('aria-expanded', 'false');
  btn.title = st.selected;
  const list = el('div', 'esp-pick-list');
  list.setAttribute('role', 'listbox');

  btn.addEventListener('click', e => {
    e.stopPropagation();
    const opening = !wrap.classList.contains('open');
    closePickers(wrap);
    wrap.classList.toggle('open', opening);
    btn.setAttribute('aria-expanded', String(opening));
  });

  const options = [
    ...PRESETS.map(p => ({ key: 'p:' + p.id, name: p.label, desc: '预设 · ' + p.board, preset: p })),
    ...(st.boards.length
      ? st.boards.map(b => ({ key: 'b:' + b.name, name: b.name, desc: b.desc }))
      : [{ key: 'b:' + st.selected, name: st.selected, desc: '读取板卡列表中…' }]),
  ];
  const current = st.preset ? 'p:' + st.preset.id : 'b:' + st.selected;
  for (const o0 of options) {
    const o = el('div', 'esp-pick-opt' + (o0.key === current ? ' sel' : ''));
    o.setAttribute('role', 'option');
    o.setAttribute('aria-selected', String(o0.key === current));
    o.appendChild(el('span', 'esp-pick-name', o0.name));
    if (o0.desc) o.appendChild(el('small', 'esp-pick-desc', o0.desc));
    o.addEventListener('click', async e => {
      e.stopPropagation();
      closePickers(null);
      if (o0.preset) {
        // A preset is not a board swap any more: it gets its own simulator instance,
        // which is the same thing opening that subsystem's drawer does, so the two
        // paths are one function.
        runPreset(o0.preset.id);
        return;
      }
      // A board picked out of the simulator's own list has no preset and no frame
      // line, so it loads into whichever instance is in front.
      panel.note.textContent = `载入 ${o0.name}…`;
      st.preset = null;
      st.selected = o0.name;
      activePreset = null;
      panel.note.textContent = await loadBoard(o0.name, active);
      renderPicker();
      await compileAndRun();
    });
    list.appendChild(o);
  }

  wrap.appendChild(btn);
  wrap.appendChild(list);
  host.appendChild(wrap);
}

/* ---------- panel DOM ---------- */
// The two lighting switches are markup inside the 配电 drawer's body. Their inline
// handler only moves the scene, so the wire has to be added the moment those nodes
// exist -- which is every time that drawer is opened, because opening a drawer
// rewrites its body.
export function wireLightingSwitches() {
  const swStreet = document.getElementById('swStreet');
  if (swStreet && !swStreet.dataset.wired) {
    swStreet.dataset.wired = '1';
    swStreet.addEventListener('click', () => cmd.lightStreet(streetLightOn));
  }
  const swHouse = document.getElementById('swHouse');
  if (swHouse && !swHouse.dataset.wired) {
    swHouse.dataset.wired = '1';
    swHouse.addEventListener('click', () => cmd.lightHouse(houseLightOn));
  }
  return !!swStreet;
}

export function mountEspPanel(host) {
  ensureDock();
  host.textContent = '';

  const grid = cardGrid();
  const status = cell(grid, '状态'), firmware = cell(grid, '固件'), wires = cell(grid, '板载接线');
  const period = cell(grid, '上报周期'), frames = cell(grid, '已收帧'), signal = cell(grid, '信号');
  host.appendChild(grid);
  const line = statusLine(host, '');

  // The two switches above ask the lighting board; what it did about the asking is
  // written here, in the board's own words from its next frame.
  const lampCtl = el('div', 'esp-ctl');
  lampCtl.appendChild(el('span', 'dr-lbl', '照明指令'));
  for (const [labelText, send] of [['光控自动', cmd.lightAuto], ['解除闭锁', cmd.lightClear], ['对时', null]]) {
    const b = el('button', 'esp-btn', labelText);
    b.addEventListener('click', () => (send ? send() : cmd.lightClock(ranchClock())));
    lampCtl.appendChild(b);
  }
  const lampAck = el('span', 'cmd-line');
  lampCtl.appendChild(lampAck);
  host.appendChild(lampCtl);
  const lampNote = el('small', 'esp-note',
    '对时：把这台电脑的时刻写给照明板的 RTC（time=年-月-日T时:分:秒），板子的钟在帧里以 t= 报回来');
  host.appendChild(lampNote);

  // The switches are the drawer's own markup and flip the scene on click; this adds
  // the wire behind the flip. They are wired from drawer.js, not from here: their two
  // nodes are born in the 配电 drawer's markup, and every drawer open rebuilds that
  // body, so a listener attached at board-panel mount time was attached to a node that
  // had already been replaced. The inline handler runs first, so the state read here is
  // the one the operator just asked for.
  const wrap = el('div', 'esp-panel');
  const ctl = el('div', 'esp-ctl');
  const pickerHost = el('div', 'esp-pickhost');
  ctl.appendChild(pickerHost);
  const note = el('small', 'esp-note');
  const mkBtn = (labelText, handler) => {
    const b = el('button', 'esp-btn', labelText);
    b.addEventListener('click', async () => {
      b.disabled = true;
      note.textContent = `${labelText}…`;
      try { note.textContent = await handler(active); } finally { b.disabled = false; }
    });
    ctl.appendChild(b);
    return b;
  };
  mkBtn('Compile', compileStep);
  mkBtn('Run', runStep);
  mkBtn('停止', stopStep);

  ctl.appendChild(note);
  wrap.appendChild(ctl);

  const svgHost = el('div', 'esp-svghost');
  // The canvas below is Velxio's own starter example for this board: an ESP32 DevKit,
  // a resistor and one LED. It is not the ranch's harness and this page cannot
  // rewire it -- the parts are placed and connected inside the editor's own state, by
  // dragging on its canvas -- so it gets labelled for what it is, and the real
  // per-board wiring is the diagram underneath it.
  const canvasCaption = el('div', 'esp-canvas-caption');
  canvasCaption.textContent = 'Velxio 例程画布（ESP32 + 一颗 LED）· 只用来跑固件；这台柜子的接线看下面那张';
  wrap.appendChild(canvasCaption);
  wrap.appendChild(svgHost);

  const harnessHost = el('div', 'esp-harness');
  wrap.appendChild(harnessHost);

  const table = el('table', 'esp-parts');
  const thead = table.createTHead().insertRow();
  for (const h of ['引脚', '接到什么', '本帧现值', '出处']) thead.insertCell().textContent = h;
  const tbody = table.createTBody();
  wrap.appendChild(table);

  host.appendChild(wrap);
  panel = { svgHost, harnessHost, harnessKey: '', tbody, status, firmware, wires,
            period, frames, signal, line, note, pickerHost, lampAck };
  renderPicker();

  // The drawer markup was just rebuilt, so push the board's lamp state through
  // once instead of waiting for the next edge on the serial line.
  if (telemetry.light) driveFromBoard();

  // The panel may have been rebuilt while the simulation kept running, so
  // redraw the cached diagram and take one reading immediately.
  if (view) {
    svgHost.appendChild(view.svg);
  }
  tick();
}

setInterval(tick, 100);

export function espPanelHost() {
  return document.getElementById(MOUNT_ID);
}
