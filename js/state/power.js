// The switchboard board's state as the scene sees it.
//
// Deliberately a leaf: nothing here imports another state module, because the
// lighting and water modules read *this* one to find out whether their load is
// still being fed. Two boards reporting the same contactor is not a contradiction
// on a real cabinet -- the water board reports what it asked for and the power
// board reports what is actually energised -- so both stay visible below.
export const powerGrid = {
  volts: [0, 0, 0],
  amps: [0, 0, 0],
  kw: 0,
  kva: 0,
  loadPct: 0,
  hz: 0,
  pf: 0,
  rcdMa: 0,
  tempC: 0,
  kwhToday: 0,
  kwhTotal: 0,

  // The two feeders this board switches. True until a board says otherwise, so
  // the scene behaves as it did before the cabinet was wired in.
  pumpFed: true,
  yardFed: true,
  breakerOn: true,

  // What the protection layer decided, and how long it intends to keep deciding it.
  act: '',
  why: '',
  shedSec: 0,
  lockSec: 0,
  held: false,
  sensorFault: false,
  phaseLoss: false,
  coil: '',
  link: 'down',
  uptime: 0,
  lastSeen: 0,
};

const num = (v, fallback = 0) => {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
};

// A POWER frame from the board's serial line. Field names are the firmware's, and
// `pump`/`lit` there are confirmed contactor states, not coil commands.
export function applyPowerFrame(t) {
  if (!t) return false;
  powerGrid.volts = [num(t.va), num(t.vb), num(t.vc)];
  powerGrid.amps = [num(t.ia), num(t.ib), num(t.ic)];
  powerGrid.kw = num(t.kw);
  powerGrid.kva = num(t.kva);
  powerGrid.pf = num(t.pf);
  powerGrid.loadPct = num(t.load);
  powerGrid.hz = num(t.hz);
  powerGrid.rcdMa = num(t.rcd);
  powerGrid.tempC = num(t.temp);
  powerGrid.kwhToday = num(t.kwhd);
  powerGrid.kwhTotal = num(t.kwh);
  powerGrid.pumpFed = t.pump === '1';
  powerGrid.yardFed = t.lit === '1';
  powerGrid.breakerOn = t.brk === '1';
  powerGrid.act = t.act || '';
  powerGrid.why = t.why || '';
  powerGrid.shedSec = num(t.shed);
  powerGrid.lockSec = num(t.lock);
  powerGrid.held = t.held === '1';
  powerGrid.sensorFault = t.ok === '0';
  powerGrid.phaseLoss = t.phl === '1';
  powerGrid.coil = t.coil || '';
  powerGrid.link = t.link || 'down';
  powerGrid.uptime = num(t.up);
  powerGrid.lastSeen = Date.now();
  return true;
}

export function powerStale() {
  // A board that stopped reporting is not a board reporting zeros: the simulator
  // holds one program, so loading another board's firmware silences this one and its
  // last frame would otherwise stay on screen as if it were current.
  return !powerGrid.lastSeen || Date.now() - powerGrid.lastSeen > 3000;
}

export function powerFaultText() {
  if (powerStale()) return '';
  if (powerGrid.sensorFault) return '配电板测不到量程，只报警不切负荷';
  if (powerGrid.act === 'TRIP') return `总闸跳闸：${powerGrid.why || '未报原因'}`;
  if (powerGrid.act === 'SHED') return `减载中：${powerGrid.why || '过载'}，水泵回路已切除`;
  if (powerGrid.act === 'ALARM') return `${powerGrid.why || '越限'}`;
  return '';
}
