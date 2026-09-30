// The choreography page: a screen of its own, because designing a show is not
// looking at the ranch. Shapes, the order they happen in, how long each is held and
// how long the aircraft get to move between them, and the go/no-go verdict the
// ground station computes before it will upload anything.
//
// Nothing here is decorative: every number on screen is either an input to
// `show/core.js` or an output of it, and the same core is what each aircraft runs
// (proved by .probe/cross.sh, which diffs the two implementations line by line).
import { SHAPES, showReview, showValidate, showDuration, refusalList, SHOW_MAX_DRONES } from '../show/core.js';
import { planFormat, planChecksum } from '../show/plan.js';
import { DEFAULT_PLAN } from '../show/defaults.js';
import { showLoad, showStart, showStop, showPreview, showState, showSetWind, showDispose } from '../show/fleet.js';
import { simWindow, telemetry, runPreset } from './esp-panel.js';
import { boardRunning } from '../state/boards.js';
import { showToast } from './toast.js';

let root = null;
let timer = 0;
const els = {};

let plan = JSON.parse(JSON.stringify(DEFAULT_PLAN));
let batt = 100;
// The verdict is recomputed whenever anything it reads has moved -- including the
// aircraft's own battery, which changes underneath the page all show long.
let dirty = true;
let lastSig = '';
let wind = 0;

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

function num(label, value, min, max, step, onInput) {
  const row = el('label', 'sp-field');
  row.appendChild(el('span', 'sp-k', label));
  const i = el('input', 'sp-input');
  i.type = 'number';
  i.min = min; i.max = max; i.step = step; i.value = value;
  i.addEventListener('input', () => { onInput(Number(i.value)); dirty = true; refresh(); });
  row.appendChild(i);
  return row;
}

// --- a self-drawn dropdown ----------------------------------------------------
// The native `<select>` popup belongs to the operating system: it paints in its own
// colours over this page, and it cannot show what the option actually means. Same
// reason every other control here is drawn in-house. One open at a time -- the trigger
// records what it wants first, then closes the others, or the click that opens one
// panel is the same click that a global guard would otherwise use to close it.
let openPick = null;
function closePick() {
  if (openPick) { openPick.classList.remove('open'); openPick = null; }
}
document.addEventListener('click', closePick);
// The panel scrolls; a list anchored inside it would be clipped by it, so the list is
// placed against the viewport and any scroll closes it rather than leaving it floating
// over the wrong row.
document.addEventListener('scroll', closePick, true);

function picker(options, start, onPick) {
  const wrap = el('div', 'sp-pick');
  const btn = el('button', 'sp-pick-btn');
  btn.type = 'button';
  const list = el('div', 'sp-pick-list');
  let value = start;
  const paint = () => {
    const now = options.find(o => o.id === value);
    btn.textContent = now ? now.name : '?';
    btn.title = now ? now.hint || now.name : '';
    list.textContent = '';
    for (const o of options) {
      const row = el('div', 'sp-pick-opt' + (o.id === value ? ' sel' : ''), o.name);
      row.addEventListener('click', e => {
        e.stopPropagation();
        value = o.id;
        closePick();
        paint();
        onPick(o.id);
      });
      list.appendChild(row);
    }
  };
  btn.addEventListener('click', e => {
    e.stopPropagation();
    const was = wrap.classList.contains('open');
    closePick();
    if (!was) {
      const r = btn.getBoundingClientRect();
      list.style.position = 'fixed';
      list.style.left = `${Math.round(r.left)}px`;
      list.style.top = `${Math.round(r.bottom + 4)}px`;
      list.style.minWidth = `${Math.round(r.width)}px`;
      wrap.classList.add('open');
      openPick = wrap;
    }
    paint();
  });
  wrap.appendChild(btn);
  wrap.appendChild(list);
  paint();
  return wrap;
}

function hex(n) {
  return `#${(n >>> 0).toString(16).padStart(6, '0')}`;
}

function actRow(a, i) {
  const row = el('div', 'sp-act');

  // 自定义轮廓 is not offered here: an outline act has to arrive with its points, and
  // the only place that traces them is 高级. Picking it from an empty row would leave a
  //幕 with no cloud and a verdict the operator could not have caused.
  row.appendChild(picker(SHAPES.filter(s => s.id !== 6 || s.id === a.shape), a.shape,
    id => { a.shape = id; commit(); }));

  const colour = el('input', 'sp-colour');
  colour.type = 'color';
  colour.value = hex(a.colour);
  colour.addEventListener('input', () => { a.colour = parseInt(colour.value.slice(1), 16); commit(); });
  row.appendChild(colour);

  for (const [k, label, min, max, step] of [
    ['scale', '尺寸', 4, 90, 1], ['alt', '高度', 10, 120, 1],
    ['hold', '保持', 2, 120, 1], ['move', '走位', 1, 60, 1]]) {
    const f = el('label', 'sp-mini');
    f.appendChild(el('i', null, label));
    const i = el('input', 'sp-input sp-narrow');
    i.type = 'number'; i.min = min; i.max = max; i.step = step; i.value = a[k];
    i.addEventListener('input', () => { a[k] = Number(i.value); commit(); });
    f.appendChild(i);
    row.appendChild(f);
  }

  const ops = el('span', 'sp-ops');
  const mk = (t, fn) => {
    const b = el('button', 'sp-btn', t);
    b.title = t;
    b.addEventListener('click', fn);
    ops.appendChild(b);
  };
  mk('↑', () => { if (i > 0) { [plan.acts[i - 1], plan.acts[i]] = [plan.acts[i], plan.acts[i - 1]]; commit(); } });
  mk('↓', () => { if (i < plan.acts.length - 1) { [plan.acts[i + 1], plan.acts[i]] = [plan.acts[i], plan.acts[i + 1]]; commit(); } });
  mk('×', () => { if (plan.acts.length > 1) { plan.acts.splice(i, 1); commit(); } });
  row.appendChild(ops);

  row.appendChild(el('span', 'sp-idx', `${i + 1}`));
  return row;
}

function commit() {
  dirty = true;
  showLoad(plan);
  refresh();
}

// The tab row carries the verdict even while the page is shut: the entry point says
// whether what is loaded would fly, so the operator does not open a screen to find out
// it cannot be used.
function syncMenu(why) {
  const m = document.getElementById('menuShow');
  if (m) m.textContent = `${plan.drones} 架 · ${plan.acts.length} 幕 · ${why ? '不能飞' : '可以飞'}`;
}

// --- popping the page out ----------------------------------------------------
// The choreography is the screen you want on the second monitor: it is the one worked in
// for twenty minutes while the ranch itself goes on the projector. `?show=1` opens the
// same app with this dock out and the ranch chrome hidden -- the same modules and the
// same numbers, which is the whole reason it is a second window and not a second page.
function openWindowed(url, name, w, h) {
  const left = Math.max(0, Math.round((screen.width - w) / 2));
  const top = Math.max(0, Math.round((screen.height - h) / 2));
  const win = window.open(url, name, `width=${w},height=${h},left=${left},top=${top}`);
  if (!win) showToast('浏览器拦住了这个窗口：允许弹出后再点一次');
  return win;
}

function openSoloWindow() {
  openWindowed('/?show=1', 'ranchShow', screen.width, screen.height);
}

function openDesignWindow() {
  // The designer starts at the fleet this plan has, so the outline it traces has one
  // point per aircraft and the two windows do not disagree about 机数.
  openWindowed(`/?show=adv&n=${plan.drones}`, 'ranchShowDesign',
    Math.min(1220, screen.width - 60), Math.min(880, screen.height - 60));
}

// How far the camera row has to lift while this dock is open (see `.bottom` in
// show.css): the panel sits flush on the bottom edge, and the row it used to occupy
// moves above it instead of being buried under it.
function publishDockHeight() {
  const h = root ? Math.round(root.getBoundingClientRect().height) : 0;
  document.documentElement.style.setProperty('--show-dock', `${h}px`);
}

// --- the upload ---------------------------------------------------------------
// The console is the door: the simulator's serial monitor has one send box, and what
// goes through it lands on the board's UART0 RX -- the same wire a technician's USB-C
// cable is on, which is what plan.h promises the upload has to survive. The page types
// there and nowhere else. The aircraft answers on its own serial output, which the dock
// already turns into `telemetry.show`, so an upload is confirmed by the airframe's own
// `acts=`/`ck=` fields and never by this page hoping the keystroke went through.
function consoleBox() {
  const win = simWindow('show');
  if (!win || !win.document) return null;
  // The editor is served in Chinese and the box is the only text input with a
  // placeholder in its serial panel; if its label ever changes this returns null and
  // the button says so out loud rather than typing into a random field.
  return [...win.document.querySelectorAll('input[type=text][placeholder]')]
    .find(b => /发送/.test(b.placeholder)) || null;
}

function consoleSend(text) {
  const win = simWindow('show');
  const box = consoleBox();
  if (!win || !box) return false;
  const set = Object.getOwnPropertyDescriptor(win.HTMLInputElement.prototype, 'value').set;
  box.focus();
  set.call(box, text);
  box.dispatchEvent(new win.Event('input', { bubbles: true }));
  for (const kind of ['keydown', 'keyup']) {
    box.dispatchEvent(new win.KeyboardEvent(kind, {
      key: 'Enter', code: 'Enter', keyCode: 13, which: 13, bubbles: true,
    }));
  }
  return true;
}

let uploading = false;
let lastWhy = 0;
let sentOnce = false;

// The gate is re-read on every look, not once at build time: the aircraft can come up
// or drop out while the page is open, and a button that stayed grey after its board
// started -- or stayed live after it stopped -- is a button that lies about the one
// thing it is for.
function gateUpload() {
  if (!els.upload) return;
  const running = boardRunning('show');
  const carried = planFormat(plan) !== '';
  els.upload.disabled = uploading || !carried || lastWhy !== 0 || !running;
  els.upload.title = !carried ? '这一版含图片轮廓幕：上传协议只带形状编号，点列还没接进 SHOWPLAN'
    : lastWhy !== 0 ? '节目没通过校验，不上传'
    : !running ? '先在开发板抽屉里把表演机跑起来'
    : '把这一版节目写进正在运行的表演机：串口的 show= 命令';
  // Until something has actually been sent, the line beside the button says why the
  // button is what it is. A status that was written once at build time starts lying the
  // moment the board comes up underneath it.
  if (!sentOnce && !uploading && els.uploadState) {
    els.uploadState.className = 'sp-note';
    els.uploadState.textContent = !carried ? '未上传：这一版含图片轮廓幕'
      : lastWhy !== 0 ? '未上传：节目没通过校验'
      : !running ? '未上传：表演机没运行' : '可以上传给表演机';
  }
}

// The outline designer writes its trace back into the plan that opened it. The point
// list is decimated to the fleet's own size if the two disagree -- an even walk along a
// curve sampled by arc length keeps the shape, and it keeps 机数 the one number the rest
// of the form is showing.
window.addEventListener('message', e => {
  if (e.origin !== location.origin || !e.data || e.data.type !== 'show-outline') return;
  const given = Array.isArray(e.data.points) ? e.data.points : [];
  if (given.length < 2) return;
  const want = plan.drones;
  const points = given.length === want ? given
    : Array.from({ length: want }, (_, i) => given[Math.floor(i * given.length / want)]);
  const last = plan.acts[plan.acts.length - 1];
  if (plan.acts.length >= 16) { showToast('一幕节目最多 16 段'); return; }
  plan.acts.push({ shape: 6, scale: e.data.scale || 18, alt: e.data.alt || last.alt,
                   hold: last.hold, move: last.move, colour: last.colour, cloud: points });
  if (root) commit(); else { dirty = true; syncMenu(showReview(plan, effectiveBatt(), wind).why); }
  showToast(`已加入一幕：${points.length} 个点描出的轮廓`);
});

async function uploadPlan() {
  if (uploading) return;
  const rec = planFormat(plan);
  if (!rec) { showToast('这一版节目写不成上传文本：太长，或者没有节目'); return; }
  if (!consoleSend(`show=${rec}`)) {
    els.uploadState.textContent = '找不到表演机串口的发送框：那一栏没开，或者模拟器换了界面';
    els.uploadState.className = 'sp-note bad';
    return;
  }
  const ck = planChecksum(rec);
  uploading = true;
  sentOnce = true;
  els.upload.disabled = true;
  els.uploadState.className = 'sp-note';
  const t0 = Date.now();
  while (Date.now() - t0 < 10000) {
    await new Promise(r => setTimeout(r, 250));
    const t = telemetry.show;
    els.uploadState.textContent = `已发 ${rec.length} 字节，等机上回帧（ck ${ck}）…`;
    if (t && Number(t.ck) === ck) {
      els.uploadState.textContent = `机上已收：${t.acts} 幕 · ck ${t.ck} · 第 ${t.id} 号站 · 相位 ${t.why || 'READY'}`;
      els.uploadState.className = 'sp-note ok';
      uploading = false;
      refresh();
      return;
    }
  }
  const t = telemetry.show;
  // Measured, not assumed: the monitor's send box empties when this fires, so the bytes
  // leave the page -- the emulator never hands them to the sketch's Serial, which is why
  // an aircraft that is otherwise talking is answering acts=0. A real airframe on USB-C,
  // or one on MQTT, takes the same line the sandbox test flies.
  const why = '：仿真器的串口只出不进，这一条要在真机（USB-C）或 MQTT 上才走得通';
  els.uploadState.textContent = t
    ? `机上没有认这一版：它报 acts=${t.acts} ck=${t.ck}，发出去的是 ck ${ck}${why}`
    : `发了 ${rec.length} 字节，10 秒内机上没回 SHOW 帧${why}`;
  els.uploadState.className = 'sp-note bad';
  uploading = false;
  refresh();
}

function timeline() {
  const total = showDuration(plan) || 1;
  const bar = els.timeline;
  bar.textContent = '';
  let acc = 0;
  plan.acts.forEach((a, i) => {
    const seg = el('div', 'sp-seg');
    const move = i === 0 ? 0 : a.move;
    if (move) {
      const m = el('span', 'sp-seg-move', '走位');
      m.style.width = `${move / total * 100}%`;
      seg.appendChild(m);
    }
    const held = el('span', 'sp-seg-hold');
    held.style.width = `${a.hold / total * 100}%`;
    held.style.background = hex(a.colour);
    held.title = `${SHAPES.find(s => s.id === a.shape).name} · 保持 ${a.hold}s`;
    seg.appendChild(held);
    bar.appendChild(seg);
    acc += move + a.hold;
  });
  const head = el('span', 'sp-head');
  const st = showState();
  head.style.left = `${(st.t / total) * 100}%`;
  bar.appendChild(head);
}

// What the pack on the aircraft actually holds, when the airframe is talking: a show is
// judged against the fuel in the machine, not against the number in the box. The box
// stays editable so a programme can be rehearsed with nothing on the pad, and whichever
// of the two is being used is named in the verdict line.
function effectiveBatt() {
  const t = telemetry.show;
  const v = t && boardRunning('show') ? Number(t.batt) : NaN;
  return Number.isFinite(v) ? v : batt;
}

// Everything the go/no-go table reads. The timer re-runs the judgement on this, so the
// verdict cannot go stale when the aircraft's own state moves underneath the page.
function signature() {
  return [plan.drones, plan.separation, plan.maxSpeed, plan.geofence, effectiveBatt(), wind,
    plan.acts.map(a => `${a.shape}/${a.scale}/${a.alt}/${a.hold}/${a.move}`).join(';')].join('|');
}

function judge() {
  const eb = effectiveBatt();
  const rev = showReview(plan, eb, wind);
  const why = rev.why;
  const list = refusalList(why);
  lastWhy = why;
  lastSig = signature();
  dirty = false;
  syncMenu(why);
  gateUpload();

  els.verdict.textContent = list.length ? '不能飞' : '可以飞';
  els.verdict.className = `sp-verdict ${list.length ? 'bad' : 'ok'}`;
  els.reasons.textContent = '';
  if (!list.length) {
    els.reasons.appendChild(el('li', null,
      `${plan.drones} 架 · ${plan.acts.length} 幕 · 全场 ${showDuration(plan).toFixed(0)} 秒 · `
      + `最小间距按 ${plan.separation} m 判，走位按 ${plan.maxSpeed} m/s 和 ${wind} m/s 风判`));
    // The two numbers the repair pass is judged on: that it ran at all, and how much
    // room the tightest pair of aircraft ended up with.
    els.reasons.appendChild(el('li', null,
      rev.swaps > 0
        ? `走位消解换过 ${rev.swaps} 次目的地，全场最紧的一对实距 ${rev.clearance.toFixed(2)} m（规则 ${plan.separation} m）`
        : `走位无需消解，全场最紧的一对实距 ${rev.clearance.toFixed(2)} m（规则 ${plan.separation} m）`));
  }
  for (const r of list) els.reasons.appendChild(el('li', 'bad', r.text));
  els.reasons.appendChild(el('li', null,
    `电量按${eb === batt ? '页面填的' : '机上上报的'} ${eb} % 判 · 判定随任何一项改动实时重算`));
  return { rev, why, list };
}

function refresh() {
  if (!root || !root.isConnected) return;
  const { rev, why, list } = judge();

  els.acts.textContent = '';
  plan.acts.forEach((a, i) => els.acts.appendChild(actRow(a, i)));

  const st = showState();
  els.stats.textContent = '';
  for (const [k, v] of [
    ['机数', `${st.craft} / ${SHOW_MAX_DRONES}`],
    ['第几幕', `${st.act} / ${st.actCount}`],
    ['状态', st.flying ? st.phase : (st.t > 0 ? '暂停' : '待命')],
    ['节目钟', `${st.t.toFixed(1)} / ${st.total.toFixed(0)} s`],
    ['电量', `${effectiveBatt()} %`],
    ['风', `${wind} m/s`],
  ]) {
    const c = el('div', 'sp-stat');
    c.appendChild(el('i', null, k));
    c.appendChild(el('b', null, v));
    els.stats.appendChild(c);
  }
  els.scrub.max = showDuration(plan).toFixed(1);
  timeline();
  publishDockHeight();
}

export function toggleShowPage(force) {
  const open = force !== undefined ? force : !root;
  if (!open) {
    if (root) { root.remove(); root = null; }
    if (timer) { clearInterval(timer); timer = 0; }
    document.documentElement.style.setProperty('--show-dock', '0px');
    showDispose();
    return;
  }
  if (root) return;

  // Opening the show is opening its board, the same way every other subsystem row
  // behaves: the airframe is what the upload button and the verdict answer to, and a
  // choreography screen whose board was never started is a screen that cannot do the one
  // thing it is for.
  if (!boardRunning('show')) runPreset('show');
  showLoad(plan);

  root = el('div', 'show-page');
  const head = el('div', 'sp-head-bar');
  head.appendChild(el('h2', null, '✦ 编队表演编排'));
  const close = el('button', 'sp-btn sp-close', '关闭');
  close.addEventListener('click', () => toggleShowPage(false));
  const solo = el('button', 'sp-btn sp-solo', '独立窗口');
  solo.title = '把这个编排台单独开成一个浏览器窗口、占满那块屏；这台留给看机群';
  solo.addEventListener('click', openSoloWindow);
  const tools = el('div', 'sp-tools');
  tools.appendChild(solo);
  tools.appendChild(close);
  head.appendChild(tools);
  root.appendChild(head);

  const cols = el('div', 'sp-cols');

  const left = el('div', 'sp-col');
  left.appendChild(el('div', 'sp-col-title', '机群与场地'));
  left.appendChild(num('飞机数量', plan.drones, 1, SHOW_MAX_DRONES, 1, v => { plan.drones = Math.round(v); }));
  left.appendChild(num('最小间距 (m)', plan.separation, 1, 8, 0.5, v => { plan.separation = v; }));
  left.appendChild(num('最大速度 (m/s)', plan.maxSpeed, 1, 20, 0.5, v => { plan.maxSpeed = v; }));
  left.appendChild(num('围栏半径 (m)', plan.geofence, 20, 400, 5, v => { plan.geofence = v; }));
  left.appendChild(num('返航高度 (m)', plan.rtlAlt, 10, 120, 1, v => { plan.rtlAlt = v; }));
  left.appendChild(num('当前电量 (%)', batt, 5, 100, 5, v => { batt = v; }));
  left.appendChild(num('风 (m/s)', wind, 0, 12, 0.5, v => { wind = v; showSetWind(v); }));
  left.appendChild(el('small', 'sp-note',
    '数量、间距、速度、围栏、电量、风全部进 go/no-go 判定：改了任何一项，右侧立刻重判一次。'));
  cols.appendChild(left);

  const mid = el('div', 'sp-col');
  mid.appendChild(el('div', 'sp-col-title', '节目单（从上到下依次执行）'));
  els.acts = el('div', 'sp-acts');
  mid.appendChild(els.acts);
  const add = el('button', 'sp-btn sp-add', '＋ 加一幕');
  add.addEventListener('click', () => {
    if (plan.acts.length >= 16) { showToast('一幕节目最多 16 段'); return; }
    const last = plan.acts[plan.acts.length - 1];
    // A new act inherits the one before it -- 走位 included. The show is built by
    // appending, and a fixed 14 s made every act after the first move at a different
    // speed from the one it followed, so the timeline read as a jump nobody asked for.
    plan.acts.push({ shape: (last.shape + 1) % SHAPES.length, scale: 18, alt: last.alt,
                     hold: last.hold, move: last.move, colour: last.colour });
    commit();
  });
  const adv = el('button', 'sp-btn sp-adv', '高级');
  adv.title = '开一个独立窗口：上传图片，按图片轮廓在 3D 里摆节目';
  adv.addEventListener('click', openDesignWindow);
  const addRow = el('div', 'sp-addrow');
  addRow.appendChild(add);
  addRow.appendChild(adv);
  mid.appendChild(addRow);
  const reset = el('button', 'sp-btn', '恢复默认节目');
  reset.addEventListener('click', () => { plan = JSON.parse(JSON.stringify(DEFAULT_PLAN)); batt = 100; wind = 0; commit(); });
  mid.appendChild(reset);
  cols.appendChild(mid);

  const right = el('div', 'sp-col');
  els.verdict = el('div', 'sp-verdict ok', '');
  right.appendChild(els.verdict);
  els.reasons = el('ul', 'sp-reasons');
  right.appendChild(els.reasons);
  right.appendChild(el('div', 'sp-col-title', '实时状态'));
  els.stats = el('div', 'sp-stats');
  right.appendChild(els.stats);
  cols.appendChild(right);
  root.appendChild(cols);

  els.timeline = el('div', 'sp-timeline');
  els.timeline.id = 'showTimeline';
  root.appendChild(els.timeline);

  const foot = el('div', 'sp-foot');
  const play = el('button', 'sp-btn sp-go', '起飞');
  play.addEventListener('click', () => {
    const why = showValidate(plan, batt, wind);
    if (why !== 0) { showToast('节目未通过校验，不能起飞'); return; }
    showStart();
  });
  const stop = el('button', 'sp-btn', '停止');
  stop.addEventListener('click', () => showStop());
  els.upload = el('button', 'sp-btn', '上传节目');
  els.upload.addEventListener('click', uploadPlan);
  els.uploadState = el('span', 'sp-note',
    boardRunning('show') ? '还没上传' : '未上传：表演机没运行');
  els.scrub = el('input', 'sp-scrub');
  els.scrub.type = 'range';
  els.scrub.min = 0;
  els.scrub.step = 0.1;
  els.scrub.addEventListener('input', () => showPreview(Number(els.scrub.value)));
  foot.appendChild(play);
  foot.appendChild(stop);
  foot.appendChild(els.upload);
  foot.appendChild(els.uploadState);
  foot.appendChild(el('span', 'sp-k', '拖动预览'));
  foot.appendChild(els.scrub);
  foot.appendChild(el('small', 'sp-note', '走位中的飞机会暗一档：那是节目钟说它还没到位。'));
  root.appendChild(foot);

  document.getElementById('app').appendChild(root);
  refresh();
  // The render loop owns the show clock; this interval only reads it. Two owners of one
  // clock made the show run at double speed while the page was open. Closing the page
  // does end the show -- measured, t 28.2 -> 0 with the fleet disposed -- because this
  // drawer is the only place 停止 lives, and a fleet performing with no control surface
  // on screen is the same dead affordance pointed the other way.
  if (!timer) timer = setInterval(() => {
    gateUpload();
    // The verdict follows the aircraft, not just the form: the pack drains while the
    // show flies, and a 「可以飞」 that was true at the moment the page was opened is
    // the most dangerous kind of stale number on this screen.
    if (dirty || signature() !== lastSig) judge();
    const st = showState();
    els.scrub.value = st.t.toFixed(1);
    timeline();
    const s = els.stats;
    if (s.firstChild) {
      const kids = s.children;
      const vals = [`${st.craft} / ${SHOW_MAX_DRONES}`, `${st.act} / ${st.actCount}`,
        st.flying ? st.phase : (st.t > 0 ? '暂停' : '待命'),
        `${st.t.toFixed(1)} / ${st.total.toFixed(0)} s`, `${effectiveBatt()} %`, `${wind} m/s`];
      for (let i = 0; i < kids.length && i < vals.length; ++i) kids[i].lastChild.textContent = vals[i];
    }
  }, 200);
}

// The row is on screen before this page has ever been opened, so it gets one real
// judgement at load instead of a placeholder that never changes.
syncMenu(showReview(plan, batt, wind).why);

