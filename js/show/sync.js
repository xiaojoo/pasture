// One show, two documents.
//
// The 独立窗口 is the choreography desk pulled out of the bottom of the stage so the stage
// can be nothing but the fleet. That only works if both documents are looking at the *same*
// programme -- two copies of the same page each holding their own plan is two shows, and
// the second one would also start its own simulator, whose serial console steals the
// downlink from the first.
//
// So: the desk pushes the whole plan on every edit, the stage pushes back the few numbers
// the desk displays but cannot know on its own (is the board up, what does the aircraft's
// battery say, where is the show clock, what did the upload do), and the buttons that need
// the stage -- 起飞 / 停止 / 上传节目 / 近看机身 / 对准机群 -- travel as commands.
//
// The channel is a BroadcastChannel named with a group id that the stage puts into the
// popup's URL, so a third ranch window on the same origin cannot join a show it was not
// opened for. Messages carry the sender's own id and are dropped on the floor if it is the
// sender's own echo.
const query = new URLSearchParams(location.search);
export const SOLO = query.get('show') === '1';
const self = Math.random().toString(36).slice(2);
// A stage names its channel after itself and stamps that into the popup's URL; a desk is
// only ever told the id through that URL. Hand-typed /?show=1 has no id and lands on the
// 'main' name, where no stage is -- which is what keeps it a self-contained copy.
const group = query.get('g') || (SOLO ? 'main' : self);
// A popped-out window only drives another screen when it was opened *from* one, which is
// what the group id in its URL means.
export const DESK = SOLO && !!query.get('g');

let ch = null;
try { ch = new BroadcastChannel(`ranch-show-${group}`); } catch { ch = null; }

const handlers = {};

// The id the stage stamps into the window it opens. A stage uses its own random id rather
// than a fixed name, so two ranch windows on the same origin never take over each other's
// show -- and a second popup with the same window name just focuses the first one, so the
// pair stays one stage and one desk.
export function showSyncGroup() { return group; }

export function onShowMessage(t, fn) { handlers[t] = fn; }

function send(t, body) {
  if (!ch) return false;
  ch.postMessage({ t, from: self, ...body });
  return true;
}

if (ch) {
  ch.onmessage = e => {
    const m = e.data;
    if (!m || m.from === self || !handlers[m.t]) return;
    handlers[m.t](m);
  };
}

// `batt` and `wind` travel with the plan because they are inputs to the same go/no-go: the
// desk judges the programme with the numbers it is showing, and the stage has to judge it
// with the same ones or the two windows disagree about 可以飞.
export const pushPlan = (plan, batt, wind) => send('plan', { plan, batt, wind });
export const pushCmd = (what, v) => send('cmd', { what, v });
export const pushState = state => send('state', { state });
export const pushHello = () => send('hello', { role: SOLO ? 'desk' : 'stage' });
