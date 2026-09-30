// The programme as it crosses the radio: a mirror of `planFormat()` in
// `firmware/show/src/plan.cpp`, byte for byte.
//
// This is the third copy of a show in the system and the only one that is allowed to
// be a copy: the ground station edits the plan (`show/core.js`), the aircraft rebuilds
// its own lane from the record (`plan.cpp`), and the wire between them is this string.
// A formatter that drifts by one digit produces a programme the parser refuses, or --
// worse -- one it accepts with a different number in it. So `.probe/cross.sh` prints
// both implementations' output for the same plans and the two files are diffed line by
// line, and `firmware/show/test/test_sandbox.cpp` parses the string this file emits and
// formats it back, which is the round trip.
//
//   SHOWPLAN,<drones>,<separation>,<max speed>,<geofence>,<rtl alt>,<act count>
//     ;A,<shape>,<scale>,<alt>,<hold s>,<move s>,<colour>     once per act
export const SHOW_PLAN_TAG = 'SHOWPLAN';
export const SHOW_PLAN_MAX_TEXT = 1024;    // the longest upload the board takes

const num = v => Number(v).toFixed(2);
const hex6 = v => (v & 0xFFFFFF).toString(16).padStart(6, '0');

// '' when the plan cannot be represented, matching the C++ contract of "either the
// whole programme is there or nothing is" -- a truncated upload is an aircraft that
// flies half a show.
export function planFormat(plan) {
  const acts = plan.acts || [];
  if (!(plan.drones > 0) || acts.length === 0 || acts.length > 16) return '';
  let s = `${SHOW_PLAN_TAG},${plan.drones},${num(plan.separation)},${num(plan.maxSpeed)},`
    + `${num(plan.geofence)},${num(plan.rtlAlt)},${acts.length}`;
  for (const a of acts) {
    // Shape 6 is an outline traced from a picture, and this record carries shape
    // *numbers* -- there is no field for a point list yet. Refusing the whole plan here
    // is what keeps the upload button from sending `;A,6,...`, which the aircraft's
    // parser would correctly call a corrupt record.
    if (a.shape === 6) return '';
    s += `;A,${a.shape},${num(a.scale)},${num(a.alt)},${num(a.hold)},${num(a.move)},${hex6(a.colour)}`;
  }
  return s.length >= SHOW_PLAN_MAX_TEXT ? '' : s;
}

// The ack number for a record, over its exact bytes -- the same rolling sum
// `planChecksum()` in plan.cpp computes, and the same number the aircraft publishes in
// the `ck=` field of its own frame. The page compares the two, so an upload is only
// confirmed by the airframe's own words. `.probe/cross.sh` prints both sides' numbers
// for the same records: a change on one side is a failed diff, not a silent mismatch.
export function planChecksum(text) {
  let ck = 0;
  for (let i = 0; i < text.length; ++i) ck = (ck * 31 + (text.charCodeAt(i) & 0xFF)) & 0xFFFF;
  return ck;
}
