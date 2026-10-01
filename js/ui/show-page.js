// The choreography page: a screen of its own, because designing a show is not
// looking at the ranch. Shapes, the order they happen in, how long each is held and
// how long the aircraft get to move between them, and the go/no-go verdict the
// ground station computes before it will upload anything.
//
// Nothing here is decorative: every number on screen is either an input to
// `show/core.js` or an output of it, and the same core is what each aircraft runs
// (proved by .probe/cross.sh, which diffs the two implementations line by line).
import { SHAPES, showReview, showValidate, showDuration, refusalList, SHOW_MAX_DRONES,
  showSampleShape, showAssign } from '../show/core.js';
import { planFormat, planChecksum } from '../show/plan.js';
import { DEFAULT_PLAN } from '../show/defaults.js';
import { showLoad, showStart, showStop, showPreview, showState, showSetWind,
  showFocus, showFocusState, showCraftInfo, showDisplayScale, showDisplayInfo,
  setShowDisplayScale, setShowDisplayAuto, showFrame } from '../show/fleet.js';

// Which airframe the close look is on. Kept out of the button's closure so the stepper
// and the dock's own refresh agree on one number.
let focusK = 0;

function paintFocus() {
  if (!els.focusBtn) return;
  // In the desk window there is no fleet to look at, so this reads what the stage said
  // it is doing. The first tick has not arrived yet, hence the empty fallback.
  const st = shownFocus();
  const on = st.focus >= 0;
  els.focusBtn.textContent = on ? '退出近看' : '近看机身';
  els.focusPrev.disabled = !on;
  els.focusNext.disabled = !on;
  els.focusLabel.textContent = on ? `第 ${st.focus + 1} / ${st.count} 架` : '';
}
import { simWindow, telemetry, runPreset } from './esp-panel.js';
import { boardRunning } from '../state/boards.js';
import { showToast } from './toast.js';
import { SOLO, DESK, showSyncGroup, onShowMessage, pushHello,
  pushPlan, pushCmd, pushState } from '../show/sync.js';

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

// --- which half of this file is running ---------------------------------------
// One document is the stage (the fleet, the boards, the simulators) and one is the desk
// (this form). The stage opens the desk as a separate window and then shows no desk at
// all, so the bottom of the picture is clear; the desk edits a plan it does not fly and
// sends it over as fast as it is typed. `SOLO` says which of the two this document is;
// `deskOut` on the stage says the desk is in the other window right now.
//
// `remote` is what the stage last said about itself: numbers this window cannot know,
// because the aircraft, the board and the show clock are all over there.
const remote = { st: null, focus: null, disp: null, craft: null, batt: NaN, running: false,
  upload: null };
let deskOut = false;
let soloWin = null;
let pushTimer = 0;

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

// `name` is the field's autofill key. Chrome's Issues panel flags every form field that has
// neither an `id` nor a `name` (it counted 18 here, and 28 once the act rows grew), and the
// names have to be unique across the whole document -- which is why the act rows carry their
// own index rather than sharing one name per column.
function num(label, name, value, min, max, step, onInput) {
  const row = el('label', 'sp-field');
  row.appendChild(el('span', 'sp-k', label));
  const i = el('input', 'sp-input');
  i.type = 'number';
  i.name = name;
  i.min = min; i.max = max; i.step = step; i.value = value;
  // `commit()`, not just `dirty`: the left-hand numbers change the geometry -- 机数 changes
  // how many airframes the fleet holds and 最小间距 changes the lanes the paths are baked
  // with -- and the tick that watches `dirty` only re-runs the verdict. Measured: setting
  // 飞机数量 to 49 left 24 aircraft on screen and the bar still reading 「最紧的一对 3.36 m」
  // from the previous load, so the operator was shown an old field under new numbers.
  i.addEventListener('input', () => { onInput(Number(i.value)); commit(); });
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

// 加一幕 used to append one fixed recipe and hand back 不能飞: the show is built by
// appending, and whatever the next act happens to be almost never fits the altitudes and
// sizes already chosen. So the new act's shape, height and size are searched against the
// *same* go/no-go the panel shows, and the act is only added when some combination keeps
// the whole show flyable. The order of the candidates is the preference: nearest the
// existing lane first, so the operator gets the act they meant rather than the first one
// a search happened to find.
// How long the new act's 走位 has to be. The button used to copy the previous act's number
// (`move: last.move`), so adding a third act after one whose 走位 had been edited to 14
// produced another 14 -- a time that belongs to a different formation, and the one field
// that would not move. This is the same rule the go/no-go judges by: the smoothstep path
// peaks at 1.5x the average speed, and the leg is refused when `peak + wind > maxSpeed`,
// so the seconds written here are the seconds that check passes on.
function moveSeconds(p, cand, wind) {
  const n = p.drones;
  const prev = p.acts[p.acts.length - 1];
  const from = [], to = [];
  if (showSampleShape(prev.shape, n, prev.scale, prev.alt, from) !== n) return cand.move;
  if (showSampleShape(cand.shape, n, cand.scale, cand.alt, to) !== n) return cand.move;
  // `showAssign` writes into a caller-owned array (same contract as the C++ side, where it
  // is passed a stack buffer): called without one it threw, and the throw was swallowed by
  // the click handler -- the button looked pressed and nothing was ever added.
  const order = showAssign(from, to, n, new Array(n).fill(0));
  let far = 0;
  for (let k = 0; k < n; ++k) {
    const t = to[order[k]] ?? to[k];
    far = Math.max(far, Math.hypot(t.n - from[k].n, t.e - from[k].e, t.d - from[k].d));
  }
  const room = p.maxSpeed - wind;
  if (!(room > 0)) return 60;   // no move time passes at this wind; let the verdict say so
  return Math.min(60, Math.max(1, Math.ceil(1.5 * far / room)));
}

function flyableAct(p, battPct, wind) {
  const last = p.acts[p.acts.length - 1];
  // Shapes are tried from "a different figure than the act before" onwards: the old
  // button advanced the shape and kept the timing, and a show of five identical rings is
  // not a show. Flyability still decides, this only decides which flyable act is offered.
  const ids = SHAPES.filter(s => s.id !== 6).map(s => s.id);
  const from = (last.shape + 1) % ids.length;
  const shapes = ids.slice(from).concat(ids.slice(0, from));
  const alts = [last.alt, last.alt + 10, last.alt + 20, last.alt - 10, last.alt + 34, last.alt - 22]
    .filter(a => a >= 10 && a <= 120);
  const scales = [last.scale, last.scale * 1.4, last.scale * 0.7, last.scale * 1.9, 44]
    .filter(s => s >= 4 && s <= 90);
  let tries = 0, best = null;
  for (const shape of shapes) {
    for (const alt of alts) {
      for (const scale of scales) {
        const base = { shape, scale: Math.round(scale), alt: Math.round(alt),
                       hold: last.hold, move: last.move, colour: last.colour };
        const cand = { ...base, move: moveSeconds(p, base, wind) };
        const r = showReview({ ...p, acts: [...p.acts, cand] }, battPct, wind);
        ++tries;
        if (!r.why) return { cand, r, tries };
        if (!best || (best.clearance ?? -1) < (r.clearance ?? -1)) best = r.clearance;
      }
    }
  }
  return { cand: null, tries, best };
}

function addFlyableAct() {
  if (plan.acts.length >= 16) { showToast('一幕节目最多 16 段'); return; }
  const found = flyableAct(plan, effectiveBatt(), wind);
  if (!found.cand) {
    // Two different facts, and only one of them is this button's fault: a plan that was
    // already refused before anything was added would otherwise be blamed on the new act.
    const before = showReview(plan, effectiveBatt(), wind);
    if (before.why) {
      showToast(`没加：现在这一版本身就飞不了（${refusalList(before.why).map(r => r.text).join('；')}`
        + `）。先把已有各幕调开，再加下一幕。`);
      return;
    }
    showToast(`没加：试过 ${found.tries} 种形状/高度/尺寸组合，最宽的一种也只剩 `
      + `${(found.best ?? 0).toFixed(2)} m（要求 ${plan.separation.toFixed(2)} m）。`
      + `减小机数、放大尺寸或调小最小间距才加得进来。`);
    return;
  }
  plan.acts.push(found.cand);
  commit();
  showToast(`已加第 ${plan.acts.length} 幕：${SHAPES[found.cand.shape].name} · 尺寸 ${found.cand.scale} m · `
    + `高度 ${found.cand.alt} m · 走位 ${found.cand.move} s（按这一版最远那架机的路程算，不是照抄上一幕）· `
    + `可以飞（最紧两点 ${found.r.clearance.toFixed(2)} m，要求 ${plan.separation.toFixed(2)} m）`);
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
  colour.name = `act-${i + 1}-colour`;
  colour.value = hex(a.colour);
  // The colour picker fires `input` for every step of the drag; rebuilding this row under
  // it closes the picker mid-drag, so the live path keeps the row and the final one commits.
  colour.addEventListener('input', () => { a.colour = parseInt(colour.value.slice(1), 16); liveEdit(); });
  colour.addEventListener('change', () => { a.colour = parseInt(colour.value.slice(1), 16); commit(); });
  row.appendChild(colour);

  for (const [k, label, min, max, step] of [
    ['scale', '尺寸', 4, 90, 1], ['alt', '高度', 10, 120, 1],
    ['hold', '保持', 2, 120, 1], ['move', '走位', 1, 60, 1]]) {
    const f = el('label', 'sp-mini');
    f.appendChild(el('i', null, label));
    // `box`, not `i`: the row index is `i` and the name has to carry it, so shadowing it
    // here would silently produce `act-NaN-scale`.
    const box = el('input', 'sp-input sp-narrow');
    box.type = 'number'; box.name = `act-${i + 1}-${k}`;
    box.min = min; box.max = max; box.step = step; box.value = a[k];
    // Typing follows live (verdict, timeline, fleet), and the caret is only given up on
    // Enter or when the field is left -- that is when the row is rebuilt from the values.
    box.addEventListener('input', () => { a[k] = Number(box.value); liveEdit(); });
    box.addEventListener('change', () => { a[k] = Number(box.value); commit(); });
    f.appendChild(box);
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

// One edit, one owner of the aircraft. The stage rebuilds the fleet from the plan; the
// desk has no fleet, so it sends the plan instead and the stage's `showAdopt` rebuilds
// there. Either way the form repaints immediately -- 「改参数要实时生效」 is this line.
function commit() {
  dirty = true;
  if (DESK) pushPlan(plan, batt, wind); else showLoad(plan);
  refresh();
}

// Same, but without rebuilding the row that is being typed into.
function liveEdit() {
  dirty = true;
  if (DESK) pushPlan(plan, batt, wind); else showLoad(plan);
  refresh(true);
}

// The tab row carries the verdict even while the page is shut: the entry point says
// whether what is loaded would fly, so the operator does not open a screen to find out
// it cannot be used.
function syncMenu(why) {
  const m = document.getElementById('menuShow');
  if (m) m.textContent = `${plan.drones} 架 · ${plan.acts.length} 幕 · ${why ? '不能飞' : '可以飞'}`;
}

// --- popping the desk out ----------------------------------------------------
// The choreography is the screen you want on the second monitor: it is the one worked in
// for twenty minutes while the ranch itself goes on the projector. So the desk *moves*:
// `/?show=1&g=<stage id>` is this form with the ranch chrome hidden and no fleet of its own,
// and the bottom of the stage is left to the picture. The id is what tells the two documents
// they belong to the same show; without it (`/?show=1`, typed by hand) the window is a
// self-contained copy, which is what this URL was before there was a stage to talk to.
function openWindowed(url, name, w, h) {
  const left = Math.max(0, Math.round((screen.width - w) / 2));
  const top = Math.max(0, Math.round((screen.height - h) / 2));
  const win = window.open(url, name, `width=${w},height=${h},left=${left},top=${top}`);
  if (!win) showToast('浏览器拦住了这个窗口：允许弹出后再点一次');
  return win;
}

function openSoloWindow() {
  if (SOLO) return;
  // The group id rides in the URL so the two documents find each other and a third ranch
  // window on the same origin cannot join a show it was not opened for.
  const win = openWindowed(`/?show=1&g=${showSyncGroup()}`, 'ranchShow',
    screen.width, screen.height);
  if (!win) return;
  // The desk has moved: it is the whole other window now, and this one keeps only the
  // picture. Nothing is stopped -- the aircraft carry on flying while the form is carried
  // out to the second screen.
  soloWin = win;
  deskTakeover();
  // The desk asks who is running the show as soon as its document is up; this answers the
  // same way, in case the popup was already open and only gained focus -- a refocused
  // window never re-sent its hello, and would otherwise be showing a stale plan.
  pushHello();
}

function openDesignWindow() {
  // The designer starts at the fleet this plan has, so the outline it traces has one
  // point per aircraft and the two windows do not disagree about 机数.
  openWindowed(`/?show=adv&n=${plan.drones}`, 'ranchShowDesign',
    Math.min(1220, screen.width - 60), Math.min(880, screen.height - 60));
}

// How tall the desk is while it is open (0 when it is not), published so the fleet's framing
// can be slid clear of it. The framing does **not** shrink the show to fit the band left
// above the panel -- that is what made the formation small -- it moves the picture instead,
// and `showFrame()` centres that picture between the panel and the camera row in the header.
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
  // In the desk the board is on the other side of the wire, so whether it is up -- and what
  // the last upload did about it -- arrives as part of the stage's report. The gate reason
  // itself is computed here, because the plan, the battery and the wind the desk judges by
  // are the same numbers the stage just judged by.
  const up = remote.upload;
  const running = DESK ? remote.running : boardRunning('show');
  const busy = DESK ? !!(up && up.busy) : uploading;
  const carried = planFormat(plan) !== '';
  els.upload.disabled = busy || !carried || lastWhy !== 0 || !running;
  els.upload.title = !carried ? '这一版含图片轮廓幕：上传协议只带形状编号，点列还没接进 SHOWPLAN'
    : lastWhy !== 0 ? '节目没通过校验，不上传'
    : !running ? '先在开发板抽屉里把表演机跑起来'
    : DESK ? '把这一版节目交给主屏那一端，由它写进正在运行的表演机：串口的 show= 命令'
    : '把这一版节目写进正在运行的表演机：串口的 show= 命令';
  const gate = !carried ? '未上传：这一版含图片轮廓幕'
    : lastWhy !== 0 ? '未上传：节目没通过校验'
    : !running ? '未上传：表演机没运行'
    : DESK ? '可以上传：命令会送到主屏那一端执行' : '可以上传给表演机';
  // Until something has actually been sent, the line beside the button says why the
  // button is what it is. A status that was written once at build time starts lying the
  // moment the board comes up underneath it.
  if (DESK) {
    if (!up || (!up.started && !up.busy)) {
      els.uploadState.className = 'sp-note';
      els.uploadState.textContent = gate;
    } else {
      els.uploadState.className = up.cls;
      els.uploadState.textContent = up.text;
    }
    return;
  }
  if (!sentOnce && !uploading) noteUpload('sp-note', gate);
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

// The upload line is one record, not a DOM node. The stage owns the serial send box, but
// the desk is the screen that shows the result, and while the send is running the stage's
// own panel is detached -- writing into a node nobody can see and calling it a status is
// how a 「机上已收」 once sat beside a board that had never been sent anything.
let uploadNote = { text: '', cls: 'sp-note' };
function noteUpload(cls, text) {
  uploadNote = { text, cls };
  if (root && els.uploadState) {
    els.uploadState.className = cls;
    els.uploadState.textContent = text;
  }
}

async function uploadPlan() {
  if (uploading) return;
  const rec = planFormat(plan);
  if (!rec) { showToast('这一版节目写不成上传文本：太长，或者没有节目'); return; }
  if (!consoleSend(`show=${rec}`)) {
    noteUpload('sp-note bad', '找不到表演机串口的发送框：那一栏没开，或者模拟器换了界面');
    return;
  }
  const ck = planChecksum(rec);
  uploading = true;
  sentOnce = true;
  if (root) els.upload.disabled = true;
  noteUpload('sp-note', `已发 ${rec.length} 字节，等机上回帧（ck ${ck}）…`);
  const t0 = Date.now();
  while (Date.now() - t0 < 10000) {
    await new Promise(r => setTimeout(r, 250));
    const t = telemetry.show;
    if (t && Number(t.ck) === ck) {
      noteUpload('sp-note ok', `机上已收：${t.acts} 幕 · ck ${t.ck} · 第 ${t.id} 号站 · 相位 ${t.why || 'READY'}`);
      uploading = false;
      refresh();
      return;
    }
    noteUpload('sp-note', `已发 ${rec.length} 字节，等机上回帧（ck ${ck}）…`);
  }
  const t = telemetry.show;
  // Measured, not assumed: the monitor's send box empties when this fires, so the bytes
  // leave the page -- the emulator never hands them to the sketch's Serial, which is why
  // an aircraft that is otherwise talking is answering acts=0. A real airframe on USB-C,
  // or one on MQTT, takes the same line the sandbox test flies.
  const why = '：仿真器的串口只出不进，这一条要在真机（USB-C）或 MQTT 上才走得通';
  noteUpload('sp-note bad', t
    ? `机上没有认这一版：它报 acts=${t.acts} ck=${t.ck}，发出去的是 ck ${ck}${why}`
    : `发了 ${rec.length} 字节，10 秒内机上没回 SHOW 帧${why}`);
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
    const move = a.move;
    if (move) {
      const m = el('span', 'sp-seg-move', i === 0 ? '起飞到位' : '走位');
      m.style.left = `${acc / total * 100}%`;
      m.style.width = `${move / total * 100}%`;
      m.title = i === 0
        ? `起飞到位 ${acc.toFixed(0)}–${(acc + move).toFixed(0)} 秒：从机场上到编队高度再散开，`
          + '飞机自己也是这么算的（show.cpp 的第一幕从原点起飞）'
        : `走位 ${acc.toFixed(0)}–${(acc + move).toFixed(0)} 秒（${move} 秒飞完）`;
      seg.appendChild(m);
    }
    const held = el('span', 'sp-seg-hold');
    held.style.left = `${(acc + move) / total * 100}%`;
    held.style.width = `${a.hold / total * 100}%`;
    held.style.background = hex(a.colour);
    held.title = `${SHAPES.find(s => s.id === a.shape).name} · 保持 `
      + `${(acc + move).toFixed(0)}–${(acc + move + a.hold).toFixed(0)} 秒`;
    seg.appendChild(held);
    bar.appendChild(seg);
    acc += move + a.hold;
  });
  const head = el('span', 'sp-head');
  const st = shownState();
  head.style.left = `${(st.t / total) * 100}%`;
  bar.appendChild(head);
}

// What the pack on the aircraft actually holds, when the airframe is talking: a show is
// judged against the fuel in the machine, not against the number in the box. The box
// stays editable so a programme can be rehearsed with nothing on the pad, and whichever
// of the two is being used is named in the verdict line.
function effectiveBatt() {
  // The desk does not read the telemetry -- the stage does, and reports the number it is
  // judging with, so a pack that has drained shows up as the same 可以飞/不能飞 on both.
  if (DESK) return Number.isFinite(remote.batt) ? remote.batt : batt;
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
    `电量按${DESK ? '主屏那端在用的' : eb === batt ? '页面填的' : '机上上报的'} ${eb} % 判 · `
    + '判定随任何一项改动实时重算'));
  return { rev, why, list };
}

// Frame the fleet now and once more after the next render. The fit reads the camera's own
// matrices, and on the frame where the desk has just been built they are still the ones from
// before it existed: measured, `showFrame()` run during the build left the formation's centre
// at x -71 of a 1440 px window, and the same call one frame later put it at x 527.
// Only the window that owns the fleet has a camera worth framing -- the desk's own canvas
// is covered by the panel and holds no aircraft.
function reframeSoon() {
  if (DESK) return;
  showFrame();
  requestAnimationFrame(() => { showFrame(); paintFocus(); });
}

// --- the two ends of one show -------------------------------------------------
// Everything on screen that is not the plan: the clock, which aircraft the close look is
// on, how big the airframe is being drawn, whether the board is up, what the last upload
// did. The stage has these from the scene and the telemetry; the desk can only be told.
function stageState() {
  return {
    st: showState(),
    focus: showFocusState(),
    disp: showDisplayInfo(),
    craft: showCraftInfo(),
    batt: effectiveBatt(),
    running: boardRunning('show'),
    upload: { ...uploadNote, busy: uploading, started: sentOnce },
  };
}

function showPaintRemote(s) {
  if (!s) return;
  remote.st = s.st;
  remote.focus = s.focus;
  remote.disp = s.disp;
  remote.craft = s.craft;
  remote.batt = s.batt;
  remote.running = s.running;
  remote.upload = s.upload;
  if (root) refresh(true);
}

// The stage takes the plan the desk is editing. `showLoad` rebuilds the aircraft from
// scratch, which is what makes the formation move under the keystroke.
function showAdopt(m) {
  if (!m || !m.plan || !Array.isArray(m.plan.acts)) return;
  plan = m.plan;
  if (Number.isFinite(m.batt)) batt = m.batt;
  if (Number.isFinite(m.wind)) { wind = m.wind; showSetWind(wind); }
  if (DESK) { refresh(); return; }
  showLoad(plan);
  // The gate the upload button is judged by has to follow plans that arrive with no form
  // on screen, or the stage would answer 可以上传 about a programme it never re-checked.
  lastWhy = showValidate(plan, effectiveBatt(), wind);
  syncMenu(showReview(plan, effectiveBatt(), wind).why);
  reframeSoon();
}

// The buttons that touch the aircraft. In the stage they run here; in the desk they
// travel, because the simulators, the serial console and the scene are all on that side.
function stageAct(what, v) {
  if (what === 'start') showStart();
  else if (what === 'stop') showStop();
  else if (what === 'upload') uploadPlan();
  else if (what === 'preview') showPreview(Number(v) || 0);
  else if (what === 'focus') showFocus(Number(v));
  else if (what === 'frame') showFrame();
  else if (what === 'scale') setShowDisplayScale(Math.max(1, showDisplayScale() + Number(v)));
  else if (what === 'autoScale') setShowDisplayAuto(true);
  else if (what === 'bringBack') { deskReturn(); return; }
  else return;
  if (root) { paintFocus(); timeline(); }
}

function act(what, v) {
  if (!DESK) { stageAct(what, v); return; }
  if (!pushCmd(what, v)) { showToast('这一扇窗口连不上主屏：主屏那一栏没开'); return; }
  // The stage answers on its next tick and the result comes back as state; say which
  // machine the button actually pressed rather than pretending the picture is here.
  if (what !== 'preview' && what !== 'scale') showToast('已送到主屏那一端执行');
}

// The desk has moved out of the bottom of this window and lives in the popup. Nothing is
// stopped and nothing is reloaded: the aircraft keep flying and the plan is already the
// one they are wearing -- only the form changes hands.
function deskTakeover() {
  if (SOLO) return;
  deskOut = true;
  if (root) {
    root.remove();
    root = null;
    if (timer) { clearInterval(timer); timer = 0; }
    publishDockHeight();   // the camera row drops back down, the picture owns the bottom
  }
  reframeSoon();
  if (!pushTimer) pushTimer = setInterval(() => {
    // The popup can be closed from its own title bar, with no word back to this document.
    // Then the form belongs here again, and the bottom of the picture is not the place to
    // leave it empty.
    if (soloWin && soloWin.closed) { deskReturn(); return; }
    pushState(stageState());
  }, 250);
  // Hand the current programme over so the desk starts from what is flying, not from the
  // factory default its own document booted with.
  pushPlan(plan, batt, wind);
  pushState(stageState());
}

function deskReturnQuiet() {
  deskOut = false;
  if (pushTimer) { clearInterval(pushTimer); pushTimer = 0; }
  soloWin = null;
}

// And back: the form returns to the bottom of this window. Called when the desk window is
// closed, and when the 编队表演 tab is clicked while the desk is out.
function deskReturn() {
  if (soloWin && !soloWin.closed) soloWin.close();
  deskReturnQuiet();
  toggleShowPage(true);
}

// The desk's own close button: fold this window away and put the form back on the bottom
// of the stage. The message goes first so the stage reopens before this document dies.
function showDeskClose() {
  if (DESK) pushCmd('bringBack');
  window.close();
}

onShowMessage('hello', m => {
  // A desk announcing itself: this window yields the form and becomes the picture.
  if (m.role === 'desk') { if (!SOLO) deskTakeover(); return; }
  // A stage asking for the current programme (its popup was already open and only
  // refocused, so it never saw the last keystroke).
  if (DESK) pushPlan(plan, batt, wind);
});
onShowMessage('plan', m => { if (DESK || !SOLO) showAdopt(m); });
onShowMessage('state', m => { if (DESK) showPaintRemote(m.state); });
onShowMessage('cmd', m => { if (!SOLO) stageAct(m.what, m.v); });
// The desk cannot see the stage, so it asks as soon as it is up; the stage answers by
// taking over, which pushes the plan back.
if (DESK) pushHello();

// What the 状态 cell says. `parked` is the fleet standing on the first formation for editing
// -- nobody has asked it to fly, so that is still 待命, and the label names what is on screen
// rather than claiming a pause that never ran.
const showStatusText = st => st.flying ? st.phase
  : st.parked ? '待命 · 第一幕队形' : (st.t > 0 ? '暂停' : '待命');

// The live numbers, read from whichever document owns the aircraft. In the desk there is no
// fleet to ask, so these come from the stage's 250 ms report; until the first one lands the
// status cell says so instead of showing a clock that never ran.
function shownState() {
  if (!DESK) return showState();
  return remote.st ?? { flying: false, parked: false, t: 0, total: showDuration(plan),
    act: 0, actCount: plan.acts.length, craft: 0 };
}
function shownFocus() {
  return DESK ? (remote.focus ?? { focus: -1, count: 0 }) : showFocusState();
}

const STAT_KEYS = ['机数', '第几幕', '状态', '节目钟', '电量', '风'];

function statVals() {
  const st = shownState();
  return [
    `${st.craft} / ${SHOW_MAX_DRONES}`,
    `${st.act} / ${st.actCount}`,
    DESK && !remote.st ? '还没连上主屏' : showStatusText(st),
    `${st.t.toFixed(1)} / ${st.total.toFixed(0)} s`,
    `${effectiveBatt()} %`,
    `${wind} m/s`,
  ];
}

// `keepRows` is the keystroke path. Rebuilding the act rows replaces the very input the
// caret is sitting in, so typing one digit in 走位 threw the focus away and the next digit
// went nowhere -- the rows are rebuilt on commit (Enter or blur), which is when the number
// is final. The verdict and the fleet still follow every keystroke.
function refresh(keepRows) {
  if (!root || !root.isConnected) return;
  judge();

  if (!keepRows) {
    els.acts.textContent = '';
    plan.acts.forEach((a, i) => els.acts.appendChild(actRow(a, i)));
  }

  paintFocus();          // the close-look row says which aircraft it is on
  // A new plan has a new tightest pair, so the auto size and the number beside it are
  // recomputed here rather than left at whatever the last load showed.
  if (els.paintScale) els.paintScale();
  els.stats.textContent = '';
  const vals = statVals();
  STAT_KEYS.forEach((k, i) => {
    const c = el('div', 'sp-stat');
    c.appendChild(el('i', null, k));
    c.appendChild(el('b', null, vals[i]));
    els.stats.appendChild(c);
  });
  els.scrub.max = showDuration(plan).toFixed(1);
  timeline();
  publishDockHeight();
}

export function toggleShowPage(force) {
  const open = force !== undefined ? force : !root;
  // The tab is still the way back: clicking 编队表演 while the desk is in its own window
  // shuts that window and puts the form on the bottom of this one again.
  if (open && deskOut) { deskReturn(); return; }
  if (!open) {
    if (root) { root.remove(); root = null; }
    if (timer) { clearInterval(timer); timer = 0; }
    publishDockHeight();      // 0: the canvas is the whole window again
    // Closing the desk leaves the fleet where the plan put it. It used to dispose every
    // airframe -- the reasoning was that a show with no control surface on screen is a dead
    // affordance -- but the desk is a column beside the canvas now, so closing it is how the
    // operator gets an unobstructed look, and emptying the sky at that moment removed
    // exactly the thing he closed the panel to see.
    showStop();
    reframeSoon();
    return;
  }
  if (root) return;

  // Opening the show is opening its board, the same way every other subsystem row
  // behaves: the airframe is what the upload button and the verdict answer to, and a
  // choreography screen whose board was never started is a screen that cannot do the one
  // thing it is for.
  // The one document that must not run things is the attached desk: it drives the stage's
  // aircraft, and a second `runPreset('show')` there would open a second simulator whose
  // serial console steals the downlink from the first (measured in firmware/README.md: with
  // two boards open, a line typed into one console is answered by the other). A hand-typed
  // /?show=1 has no stage to talk to, so it keeps its own board and its own fleet.
  if (!DESK) {
    if (!boardRunning('show')) runPreset('show');
    // The desk must not build a second fleet either: `showLoad` would park a copy of the
    // show in the desk's own scene, under the panel, where nobody can see it and nothing
    // can stop it.
    showLoad(plan);
  }

  root = el('div', 'show-page');
  const head = el('div', 'sp-head-bar');
  head.appendChild(el('h2', null, '✦ 编队表演编排'));
  const close = el('button', 'sp-btn sp-close', SOLO ? (DESK ? '收回底部' : '关闭') : '关闭');
  // From the desk: fold it away and hand the screen back to the stage. From the stage:
  // call the desk back over the bottom of the picture.
  close.addEventListener('click', () => { if (SOLO) showDeskClose(); else toggleShowPage(false); });
  const tools = el('div', 'sp-tools');
  if (!SOLO) {
    const solo = el('button', 'sp-btn sp-solo', '独立窗口');
    solo.title = '把编排台搬成单独一个窗口、占满那块屏；这一屏底部就不再显示编排台，'
      + '只留机群画面。在这扇独立窗口里改的任何参数，主屏那端的机群立刻跟着动。';
    solo.addEventListener('click', openSoloWindow);
    tools.appendChild(solo);
  }
  tools.appendChild(close);
  head.appendChild(tools);
  root.appendChild(head);

  const cols = el('div', 'sp-cols');

  const left = el('div', 'sp-col');
  left.appendChild(el('div', 'sp-col-title', '机群与场地'));
  left.appendChild(num('飞机数量', 'drones', plan.drones, 1, SHOW_MAX_DRONES, 1, v => { plan.drones = Math.round(v); }));
  left.appendChild(num('最小间距 (m)', 'separation', plan.separation, 1, 8, 0.5, v => { plan.separation = v; }));
  left.appendChild(num('最大速度 (m/s)', 'max-speed', plan.maxSpeed, 1, 20, 0.5, v => { plan.maxSpeed = v; }));
  left.appendChild(num('围栏半径 (m)', 'geofence', plan.geofence, 20, 400, 5, v => { plan.geofence = v; }));
  left.appendChild(num('返航高度 (m)', 'rtl-alt', plan.rtlAlt, 10, 120, 1, v => { plan.rtlAlt = v; }));
  left.appendChild(num('当前电量 (%)', 'battery', batt, 5, 100, 5, v => { batt = v; }));
  left.appendChild(num('风 (m/s)', 'wind', wind, 0, 12, 0.5, v => { wind = v; showSetWind(v); }));
  left.appendChild(el('small', 'sp-note',
    '数量、间距、速度、围栏、电量、风全部进 go/no-go 判定：改了任何一项，右侧立刻重判一次。'));
  cols.appendChild(left);

  const mid = el('div', 'sp-col');
  mid.appendChild(el('div', 'sp-col-title', '节目单（从上到下依次执行）'));
  els.acts = el('div', 'sp-acts');
  mid.appendChild(els.acts);
  const add = el('button', 'sp-btn sp-add', '＋ 新增一幕（自动调到能飞）');
  add.title = '找一个形状、高度和尺寸，让加上这一幕之后整场仍然通过校验；找不到就不加，并说清差多少';
  add.addEventListener('click', addFlyableAct);
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
    const why = showValidate(plan, effectiveBatt(), wind);
    if (why !== 0) { showToast('节目未通过校验，不能起飞'); return; }
    act('start');
  });
  const stop = el('button', 'sp-btn', '停止');
  stop.addEventListener('click', () => act('stop'));
  els.upload = el('button', 'sp-btn', '上传节目');
  els.upload.addEventListener('click', () => act('upload'));
  els.uploadState = el('span', 'sp-note',
    boardRunning('show') ? '还没上传' : '未上传：表演机没运行');
  els.scrub = el('input', 'sp-scrub');
  els.scrub.type = 'range';
  els.scrub.name = 'show-scrub';
  els.scrub.min = 0;
  els.scrub.step = 0.1;
  els.scrub.addEventListener('input', () => act('preview', Number(els.scrub.value)));
  foot.appendChild(play);
  foot.appendChild(stop);
  foot.appendChild(els.upload);
  foot.appendChild(els.uploadState);
  // The scene's orbit floor is three airframes away, so "look at the drone" has to be
  // a mode and not a scroll wheel: this pins the camera to one station and follows it
  // while it flies, and the steppers walk the formation one aircraft at a time.
  els.focusBtn = el('button', 'sp-btn', '近看机身');
  els.focusBtn.addEventListener('click', () => {
    const st = shownFocus();
    // Entering has to clamp *into* the fleet: `focusK` is left at -1 by an exit, and
    // Math.min(-1, count-1) is -1, so the third click asked for station -1 and the
    // button looked dead after one enter/exit pair.
    focusK = st.focus >= 0 ? -1 : Math.max(0, Math.min(focusK, st.count - 1));
    act('focus', focusK);
    paintFocus();
  });
  const stepFocus = d => {
    const st = shownFocus();
    if (st.focus < 0 || !st.count) return;
    focusK = Math.max(0, Math.min(st.count - 1, st.focus + d));
    act('focus', focusK);
    paintFocus();
  };
  els.focusPrev = el('button', 'sp-btn', '‹');
  els.focusNext = el('button', 'sp-btn', '›');
  els.focusPrev.addEventListener('click', () => stepFocus(-1));
  els.focusNext.addEventListener('click', () => stepFocus(1));
  els.focusLabel = el('span', 'sp-k', '');

  // How big the airframe is drawn. A 0.72 m model 170 m away is five pixels -- measured,
  // not guessed -- so the show view enlarges it, and the enlargement is a number on the
  // bar with the real span beside it, so nobody reads the picture as the aircraft.
  els.scaleDown = el('button', 'sp-btn', '−');
  els.scaleUp = el('button', 'sp-btn', '+');
  els.scaleLabel = el('span', 'sp-k', '');
  const paintScale = () => {
    // How far the aircraft is drawn is a property of the fleet, and in the desk the fleet
    // is on the other screen: this is the stage's own reading of it.
    const info = DESK ? remote.craft : showCraftInfo();
    const d = DESK ? remote.disp : showDisplayInfo();
    if (!d) { els.scaleLabel.textContent = '机身显示：还没连上主屏'; return; }
    els.scaleLabel.textContent = `机身显示 ×${d.scale.toFixed(1)}`
      + (d.auto ? (d.tightest ? `（自动：最近两架机隔 ${d.tightest.toFixed(1)} m）`
        : '（自动：只有一架机，按真实比例画）') : `（手动：自动是 ×${d.suggested.toFixed(1)}）`)
      // The ± knob stops where an airframe becomes as wide as the gap beside it, and that
      // ceiling is said out loud rather than left to feel like a dead button.
      + ` · 上限 ×${d.ceiling.toFixed(1)}（再大机身就交叉）`
      + (info ? ` · 真实翼展 ${info.wing.toFixed(2)} m` : '');
  };
  const bumpScale = d => { act('scale', d); if (!DESK) paintScale(); };
  els.scaleDown.addEventListener('click', () => bumpScale(-1));
  els.scaleUp.addEventListener('click', () => bumpScale(1));
  // The label is the way back: pressing ± makes the size a manual choice, and the only
  // thing an operator who has just watched the field turn into spaghetti wants is 自动.
  els.scaleLabel.classList.add('sp-scale-label');
  els.scaleLabel.title = '点一下回到自动（按这一版最近的两架机定大小）';
  els.scaleLabel.addEventListener('click', () => {
    act('autoScale');
    if (!DESK) paintScale();
  });
  els.scaleDown.title = '把机身画小一点（1× 就是真实比例）';
  els.scaleUp.title = '把机身画大一点';
  els.paintScale = paintScale;
  paintScale();
  foot.appendChild(els.focusBtn);
  foot.appendChild(els.focusPrev);
  foot.appendChild(els.focusLabel);
  foot.appendChild(els.focusNext);
  foot.appendChild(els.scaleDown);
  foot.appendChild(els.scaleLabel);
  foot.appendChild(els.scaleUp);
  // The way back when the operator has orbited into the barn: the show flies 96 m north
  // of the ranch, so "where is my formation" is a question this page has to be able to
  // answer with one click.
  els.frameBtn = el('button', 'sp-btn', '对准机群');
  els.frameBtn.title = '把镜头拉回整场编队：按投影盒取景，并让开底部这块编排台';
  els.frameBtn.addEventListener('click', () => { act('frame'); paintFocus(); });
  foot.appendChild(els.frameBtn);
  foot.appendChild(el('span', 'sp-k', '拖动预览'));
  foot.appendChild(els.scrub);
  foot.appendChild(el('small', 'sp-note', '走位中的飞机会暗一档：那是节目钟说它还没到位。'));
  root.appendChild(foot);

  document.getElementById('app').appendChild(root);
  refresh();
  // Frame the fleet now that the desk's width is known: opening this page used to leave
  // the camera wherever the ranch had last been pointed, and the formation the operator
  // came to look at was off the edge of the screen.
  reframeSoon();

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
    const st = shownState();
    els.scrub.value = st.t.toFixed(1);
    timeline();
    const kids = els.stats.children;
    const vals = statVals();
    for (let i = 0; i < kids.length && i < vals.length; ++i) kids[i].lastChild.textContent = vals[i];
  }, 200);
}

// The row is on screen before this page has ever been opened, so it gets one real
// judgement at load instead of a placeholder that never changes.
syncMenu(showReview(plan, batt, wind).why);

