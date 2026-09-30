// The drone-show core, in JavaScript.
//
// This is a port of `firmware/lib/show_core.h` and nothing here is its own opinion:
// the constants, the shape parametrisations, the smoothstep, the lane checkerboard
// and the go/no-go bits all have to agree with the C++, because the ground station
// bakes a show with this file and each aircraft flies it with that one. The drift
// that would otherwise be invisible is checked by `.probe/cross.mjs` + `.probe/cross.sh`,
// which run both implementations over the same plans and diff the numbers.
//
// The rules the port carries over verbatim, and the reason each exists:
//  - shapes are sampled to exactly N points, deterministically, so a rehearsal and
//    the night produce the same cloud;
//  - which aircraft gets which point is its own step (greedy nearest matching),
//    because index-to-index makes stations cross the field through each other;
//  - transitions fly in stacked altitude lanes, because assignment alone still leaves
//    two stations meeting head-on in the middle of a move;
//  - a show that still pinches after that gets a repair pass -- destinations are
//    swapped, kept only when both aircraft involved end up with more room;
//  - separation is judged on the repaired, baked paths at 32 samples, not on the
//    outlines, and what is judged is exactly what the aircraft are told to fly.
export const SHOW_MAX_DRONES = 128;
export const SHOW_MAX_ACTS = 16;
const SAMPLES = 32;

export const SHAPES = [
  { id: 0, key: 'ring', name: '圆环' },
  { id: 1, key: 'grid', name: '方阵' },
  { id: 2, key: 'heart', name: '心形' },
  { id: 3, key: 'wave', name: '波浪' },
  { id: 4, key: 'column', name: '光柱' },
  { id: 5, key: 'arc', name: '拱形' },
  // Not a parametric shape: the points come from `act.cloud`, which the outline designer
  // traces out of a picture. It exists on this side of the system only -- show_core.h has
  // no shape 6, because the SHOWPLAN record carries shape *numbers* and has no way to
  // carry a point list yet. `planFormat()` refuses a plan that contains one, so the
  // upload button says so instead of sending a record the aircraft would reject.
  { id: 6, key: 'image', name: '自定义轮廓' },
];

export const REFUSAL = [
  { bit: 1, key: 'count', text: '机数不在 1-128 之间，或该形状放不下这个数量' },
  { bit: 2, key: 'acts', text: '节目为空，或某一段的时长/移动时间不是正数' },
  { bit: 4, key: 'sep', text: '有两条烘焙好的轨迹靠得比安全间距还近' },
  { bit: 8, key: 'fast', text: '有飞机需要超过它的最大速度（风也算进去了）' },
  { bit: 16, key: 'out', text: '有点或有一段轨迹出了地理围栏' },
  { bit: 32, key: 'alt', text: '有高度低于 10 m 或高于 120 m' },
  { bit: 64, key: 'batt', text: '电量不够跑完节目 + 返航 + 2 分钟余量' },
];

export function showSampleShape(shape, n, scale, alt, out) {
  if (n <= 0) return 0;
  const d = -alt;
  const count = Math.min(n, SHOW_MAX_DRONES);
  const made = [];
  switch (shape) {
    case 0: {
      const rings = count > 28 ? 3 : 1;
      for (let r = 0; r < rings && made.length < count; ++r) {
        const radius = scale * (rings > 1 ? (0.4 + 0.6 * r / (rings - 1)) : 1.0);
        const inRing = Math.floor((count - made.length) / (rings - r));
        const take = r === rings - 1 ? count - made.length : inRing;
        for (let i = 0; i < take; ++i) {
          const a = 2 * Math.PI * i / take;
          made.push({ n: radius * Math.sin(a), e: radius * Math.cos(a), d });
        }
      }
      break;
    }
    case 1: {
      let side = Math.floor(Math.sqrt(count) + 0.9999);
      if (side < 1) side = 1;
      for (let i = 0; i < count; ++i) {
        const row = Math.floor(i / side), col = i % side;
        const span = side - 1;
        made.push({
          n: span > 0 ? -scale + 2 * scale * col / span : 0,
          e: span > 0 ? -scale + 2 * scale * row / span : 0,
          d,
        });
      }
      break;
    }
    case 2: {
      for (let i = 0; i < count; ++i) {
        const t = 2 * Math.PI * i / count;
        const x = 16 * Math.pow(Math.sin(t), 3);
        const y = 13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t);
        made.push({ e: x * (scale / 17), n: y * (scale / 17), d });
      }
      break;
    }
    case 3: {
      for (let i = 0; i < count; ++i) {
        const u = count > 1 ? i / (count - 1) : 0.5;
        made.push({
          e: -scale + 2 * scale * u, n: 0,
          d: d - 0.06 * scale * Math.sin(6 * Math.PI * u),
        });
      }
      break;
    }
    case 4: {
      const cols = count / 4 > 1 ? Math.floor(count / 4) : 1;
      const per = Math.floor(count / cols);
      for (let c = 0; c < cols && made.length < count; ++c) {
        const u = cols > 1 ? c / (cols - 1) : 0.5;
        for (let i = 0; i < per && made.length < count; ++i) {
          const v = per > 1 ? i / (per - 1) : 0.5;
          made.push({
            e: -scale + 2 * scale * u, n: 0,
            d: d + 0.45 * scale * (2 * v - 1),
          });
        }
      }
      break;
    }
    case 5: {
      for (let i = 0; i < count; ++i) {
        const u = count > 1 ? i / (count - 1) : 0.5;
        const a = Math.PI * (0.15 + 0.7 * u);
        made.push({ e: scale * Math.cos(a), n: scale * Math.sin(a), d });
      }
      break;
    }
    default:
      return 0;
  }
  if (out) out.push(...made);
  return made.length;
}

export function showAssign(from, to, n, order) {
  const taken = new Array(n).fill(false);
  for (let i = 0; i < n; ++i) {
    let best = Infinity, at = -1;
    for (let j = 0; j < n; ++j) {
      if (taken[j]) continue;
      const dn = to[j].n - from[i].n, de = to[j].e - from[i].e, dd = to[j].d - from[i].d;
      const d2 = dn * dn + de * de + dd * dd;
      if (d2 < best) { best = d2; at = j; }
    }
    if (at < 0) at = i;
    order[i] = at;
    taken[at] = true;
  }
  return order;
}

export function showPath(a, b, u) {
  const t = Math.max(0, Math.min(1, u));
  const s = t * t * (3 - 2 * t);
  return { n: a.n + (b.n - a.n) * s, e: a.e + (b.e - a.e) * s, d: a.d + (b.d - a.d) * s };
}

export function showLane(dest, separation) {
  const lanes = 4;
  const cx = Math.floor(dest.n / separation);
  const cz = Math.floor(dest.e / separation);
  const k = ((cx + cz) % lanes + lanes) % lanes;
  return (k - (lanes - 1) * 0.5) * separation * 2;
}

export function showPathLane(a, b, u, lane) {
  const p = showPath(a, b, u);
  p.d -= lane * Math.sin(Math.PI * u);
  return p;
}

export function showReturnSlot(plan, i) {
  const side = Math.floor(Math.sqrt(plan.drones > 0 ? plan.drones : 1) + 0.9999);
  const step = 8;
  return {
    n: (i % side - (side - 1) * 0.5) * step,
    e: (Math.floor(i / side) - (side - 1) * 0.5) * step,
    d: -plan.rtlAlt,
  };
}

export function showDuration(plan) {
  let t = 0;
  for (let i = 0; i < plan.acts.length; ++i) t += plan.acts[i].move + plan.acts[i].hold;
  return t;
}

// --- deconfliction -------------------------------------------------------------
// Greedy assignment plus altitude lanes still leaves a pair of stations meeting
// head-on inside a transition. The repair pass is the same one the C++ runs: swap a
// pair's destinations and keep the swap only if the show gets less tight. A swap does
// not change any formation -- each act stays a permutation of the same cloud -- so the
// only thing that moves is who flies where.
export function showPairClearance(aFrom, aTo, bFrom, bTo, aLane, bLane) {
  let best = 1e30;
  for (let s = 0; s < SAMPLES; ++s) {
    const u = s / (SAMPLES - 1);
    const pa = showPathLane(aFrom, aTo, u, aLane);
    const pb = showPathLane(bFrom, bTo, u, bLane);
    const d = Math.sqrt((pa.n - pb.n) ** 2 + (pa.e - pb.e) ** 2 + (pa.d - pb.d) ** 2);
    if (d < best) best = d;
  }
  return best;
}

// The clearance of the tightest other aircraft against one station, over every
// transition. Recomputing only this instead of the whole show's worst pair is what
// keeps the repair pass usable at 128 aircraft.
export function showStationWorst(pos, actCount, n, k, separation) {
  let worst = 1e30;
  for (let i = 0; i + 1 < actCount; ++i) {
    for (let o = 0; o < n; ++o) {
      if (o === k) continue;
      const d = showPairClearance(pos[i][k], pos[i + 1][k], pos[i][o], pos[i + 1][o],
                                  showLane(pos[i + 1][k], separation),
                                  showLane(pos[i + 1][o], separation));
      if (d < worst) worst = d;
    }
  }
  return worst;
}

// The show's single tightest pair, with where it is. The pair is named with the same
// one-millimetre dead band as the C++: candidates that differ by less than that are one
// problem, and the earlier one in station order gets the name, so both implementations
// point at the same pair instead of at whatever their arithmetic precision prefers. The
// clearance returned is still the true minimum.
export function showWorstPair(pos, actCount, n, separation) {
  const TIE_BAND = 0.001;
  let worst = 1e30, atAct = -1, a = -1, b = -1;
  for (let i = 0; i + 1 < actCount; ++i) {
    for (let x = 0; x < n; ++x) {
      for (let y = x + 1; y < n; ++y) {
        const d = showPairClearance(pos[i][x], pos[i + 1][x], pos[i][y], pos[i + 1][y],
                                    showLane(pos[i + 1][x], separation),
                                    showLane(pos[i + 1][y], separation));
        if (d < worst - TIE_BAND) { worst = d; atAct = i; a = x; b = y; }
        else if (d < worst) { worst = d; }
      }
    }
  }
  return { clearance: worst, atAct, a, b };
}

// Repair the compiled show in place; returns how many swaps it took. Stops when the
// show is clear, when a pass cannot improve it, or after maxPasses.
export function showDeconflict(pos, actCount, n, separation, maxPasses) {
  // No swap can get under the spacing inside a formation itself: an act is a
  // permutation of the same cloud whoever flies it, so its tightest pair is a property
  // of the shape and the count, not of the routing. When that floor is already under
  // the rule the show is refused for density whatever the repair does, and running the
  // pass anyway burns the whole budget on a plan that was never going to fly.
  let staticWorst = 1e30;
  for (let i = 0; i < actCount; ++i) {
    for (let a = 0; a < n; ++a) {
      for (let b = a + 1; b < n; ++b) {
        const dn = pos[i][a].n - pos[i][b].n, de = pos[i][a].e - pos[i][b].e, dd = pos[i][a].d - pos[i][b].d;
        const d = Math.sqrt(dn * dn + de * de + dd * dd);
        if (d < staticWorst) staticWorst = d;
      }
    }
  }
  if (staticWorst < separation) return 0;

  let swaps = 0;
  for (let pass = 0; pass < maxPasses; ++pass) {
    const w = showWorstPair(pos, actCount, n, separation);
    if (w.atAct < 0 || w.clearance >= separation) break;
    const { atAct: i, a, b } = w;
    const worst = w.clearance;

    const beforeB = showStationWorst(pos, actCount, n, b, separation);
    let repaired = false;
    // Try handing b's destination to someone else -- station a first (the direct
    // swap), then everyone. Kept only if the tightest thing about *both* aircraft
    // involved gets better, otherwise the repair just moves the collision somewhere
    // else and the next pass undoes it.
    for (let c = 0; c < n && !repaired; ++c) {
      if (c === b) continue;
      const beforeC = showStationWorst(pos, actCount, n, c, separation);
      const tmp = pos[i + 1][b];
      pos[i + 1][b] = pos[i + 1][c];
      pos[i + 1][c] = tmp;
      const afterB = showStationWorst(pos, actCount, n, b, separation);
      const afterC = showStationWorst(pos, actCount, n, c, separation);
      if (afterB > worst + 0.001 && afterC > worst + 0.001
          && afterB > beforeB - 0.001 && afterC >= beforeC - 0.001) {
        ++swaps;
        repaired = true;
        break;
      }
      pos[i + 1][c] = pos[i + 1][b];
      pos[i + 1][b] = tmp;
    }
    if (!repaired) break;
  }
  return swaps;
}

// Sample, assign, repair: the station-ordered positions of every act, plus the number
// of swaps the repair pass needed. This is what both the go/no-go decision and the
// aircraft read from, so a show is judged on exactly the paths it will fly.
export function showCompilePlan(plan) {
  const n = plan.drones;
  const acts = plan.acts;
  if (n <= 0 || n > SHOW_MAX_DRONES || acts.length === 0 || acts.length > SHOW_MAX_ACTS) return null;
  const pos = [];
  for (let i = 0; i < acts.length; ++i) {
    const a = acts[i];
    const cloud = [];
    if (a.shape === 6) {
      // An outline act brings its own points; one per aircraft or the plan is not
      // flyable and says so by refusing to compile.
      if (!a.cloud || a.cloud.length !== n) return null;
      for (const p of a.cloud) cloud.push({ n: p.n, e: p.e, d: p.d });
    } else if (showSampleShape(a.shape, n, a.scale, a.alt, cloud) !== n) return null;
    pos.push(cloud);
    if (i > 0) {
      const order = new Array(n);
      showAssign(pos[i - 1], pos[i], n, order);
      pos[i] = order.map(k => pos[i][k]);
    }
  }
  const swaps = showDeconflict(pos, acts.length, n, plan.separation, 4 * n);
  return { pos, swaps };
}

// Everything that has to be true before an upload is worth pressing, plus the two
// numbers the verdict was made of: how many destination swaps the repair pass needed,
// and what the tightest pair in the show ended up at. One compile covers both, because
// what is judged has to be what is flown -- and judging it twice would be two answers.
export function showReview(plan, battPct, wind) {
  let why = 0;
  const n = plan.drones;
  if (n <= 0 || n > SHOW_MAX_DRONES) why |= 1;
  if (plan.acts.length === 0 || plan.acts.length > SHOW_MAX_ACTS) why |= 2;
  for (const a of plan.acts) {
    if (!(a.hold > 0) || !(a.move > 0) || !(a.scale > 0)) why |= 2;
    if (a.alt < 10 || a.alt > 120) why |= 32;
  }
  if (why) return { why, swaps: 0, clearance: null };

  const c = showCompilePlan(plan);
  if (!c) return { why: 1, swaps: 0, clearance: null };
  const pos = c.pos;
  const actCount = pos.length;

  for (let i = 0; i < actCount; ++i) {
    for (let s = 0; s < SAMPLES; ++s) {
      for (let k = 0; k < n; ++k) {
        let p = pos[i][k];
        if (i + 1 < actCount) {
          p = showPathLane(pos[i][k], pos[i + 1][k], s / (SAMPLES - 1),
                           showLane(pos[i + 1][k], plan.separation));
        }
        if (Math.sqrt(p.n * p.n + p.e * p.e) > plan.geofence) why |= 16;
        if (-p.d < 5 || -p.d > 120) why |= 32;
      }
    }
  }

  for (let i = 0; i + 1 < actCount; ++i) {
    const dt = plan.acts[i + 1].move;
    for (let k = 0; k < n; ++k) {
      const dn = pos[i + 1][k].n - pos[i][k].n;
      const de = pos[i + 1][k].e - pos[i][k].e;
      const dd = pos[i + 1][k].d - pos[i][k].d;
      const v = 1.5 * Math.sqrt(dn * dn + de * de + dd * dd) / dt;
      if (v + wind > plan.maxSpeed) why |= 8;
    }
  }

  // Separation inside each formation: a shape that is too dense for the count is
  // unsafe while it is simply sitting there, and no swap can fix that -- the cloud is
  // the same set of points whoever flies it.
  for (let i = 0; i < actCount; ++i) {
    for (let a = 0; a < n; ++a) {
      for (let b = a + 1; b < n; ++b) {
        const dn = pos[i][a].n - pos[i][b].n, de = pos[i][a].e - pos[i][b].e, dd = pos[i][a].d - pos[i][b].d;
        if (Math.sqrt(dn * dn + de * de + dd * dd) < plan.separation) why |= 4;
      }
    }
  }

  // Separation on the repaired paths. The tightest pair is the number the editor puts
  // next to the verdict, because "it passes" says less than "it passes with 2.4 m to
  // spare on a 2 m rule".
  const w = showWorstPair(pos, actCount, n, plan.separation);
  if (w.clearance < plan.separation) why |= 4;

  const need = showDuration(plan) + 90 + 120;
  if (battPct * (22 * 60) < need * 100) why |= 64;
  return { why, swaps: c.swaps, clearance: w.clearance };
}

// The go/no-go answer on its own, same shape as the C++ `showValidate`.
export function showValidate(plan, battPct, wind) {
  return showReview(plan, battPct, wind).why;
}

export function refusalList(why) {
  return REFUSAL.filter(r => (why & r.bit) !== 0);
}
