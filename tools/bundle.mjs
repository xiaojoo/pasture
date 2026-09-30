#!/usr/bin/env node
// Bundle a PlatformIO project into the single .ino the browser simulator
// compiles, so the firmware in the editor and the firmware that gets flashed
// onto a board are the same text.
//
// The order of `files` is the build order: every file's local #includes must
// appear earlier in the list. That is checked, not assumed, because a bundle
// with the order wrong fails to compile with an error that points at whichever
// header happened to come last.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const ROOT = path.resolve(import.meta.dirname, '..');

const PROJECTS = [
  {
    name: 'drone',
    dir: 'firmware/drone',
    // hal_esp32.cpp is left out on purpose: it is #if-guarded to nothing under
    // RANCH_SIM, and the only thing its presence in the bundle would prove is
    // that the browser can carry dead weight.
    files: [
      'src/board.h',
      'src/hal.h',
      'src/mavlink.h',
      'src/mavlink.cpp',
      'src/video.h',
      'src/video.cpp',
      '../lib/failsafe.h',
      '../lib/mission_planner.h',
      '../lib/frame_codec.h',
      '../lib/scheduler.h',
      'src/safety.h',
      'src/safety.cpp',
      'src/mission.h',
      'src/mission.cpp',
      'src/telemetry.h',
      'src/telemetry.cpp',
      'src/hal_sim.cpp',
      'src/main.cpp',
    ],
    defines: '#define RANCH_SIM 1\n',
    entry: 'void setup() { ranch::appSetup(); }\nvoid loop() { ranch::appLoop(); }\n',
    note: 'The simulated airframe in hal_sim.cpp stands in for the flight controller and the camera.',
  },
  {
    name: 'water',
    dir: 'firmware/water',
    files: [
      'src/board.h',
      'src/hal.h',
      '../lib/frame_codec.h',
      '../lib/programme.h',
      '../lib/scheduler.h',
      '../lib/valve_logic.h',
      '../lib/water_safety.h',
      'src/sense.h',
      'src/sense.cpp',
      'src/valves.h',
      'src/valves.cpp',
      'src/telemetry.h',
      'src/telemetry.cpp',
      'src/hal_sim.cpp',
      'src/main.cpp',
    ],
    defines: '#define RANCH_SIM 1\n',
    entry: 'void setup() { ranch::appSetup(); }\nvoid loop() { ranch::appLoop(); }\n',
    note: 'The simulated plumbing in hal_sim.cpp is what the valves and meters are wired to here.',
  },
  {
    name: 'lighting',
    dir: 'firmware/lighting',
    files: [
      'src/board.h',
      'src/hal.h',
      '../lib/frame_codec.h',
      '../lib/astro.h',
      '../lib/light_policy.h',
      '../lib/scheduler.h',
      'src/lights.h',
      'src/lights.cpp',
      'src/telemetry.h',
      'src/telemetry.cpp',
      'src/hal_sim.cpp',
      'src/main.cpp',
    ],
    defines: '#define RANCH_SIM 1\n',
    entry: 'void setup() { ranch::appSetup(); }\nvoid loop() { ranch::appLoop(); }\n',
    note: 'The sky in hal_sim.cpp is computed a different way from lib/astro.h on purpose.',
  },
  {
    name: 'power',
    dir: 'firmware/power',
    files: [
      'src/board.h',
      'src/hal.h',
      '../lib/frame_codec.h',
      '../lib/scheduler.h',
      '../lib/power_meter.h',
      '../lib/power_quality.h',
      'src/meter.h',
      'src/meter.cpp',
      'src/feeders.h',
      'src/feeders.cpp',
      'src/telemetry.h',
      'src/telemetry.cpp',
      'src/hal_sim.cpp',
      'src/main.cpp',
    ],
    defines: '#define RANCH_SIM 1\n',
    entry: 'void setup() { ranch::appSetup(); }\nvoid loop() { ranch::appLoop(); }\n',
    note: 'The simulated grid in hal_sim.cpp sags under the loads the board switches.',
  },
  {
    name: 'fire',
    dir: 'firmware/fire',
    files: [
      'src/board.h',
      'src/hal.h',
      '../lib/frame_codec.h',
      '../lib/fire_logic.h',
      '../lib/scheduler.h',
      'src/panel.h',
      'src/panel.cpp',
      'src/telemetry.h',
      'src/telemetry.cpp',
      'src/hal_sim.cpp',
      'src/main.cpp',
    ],
    defines: '#define RANCH_SIM 1\n',
    entry: 'void setup() { ranch::appSetup(); }\nvoid loop() { ranch::appLoop(); }\n',
    note: 'The detectors, bell and wiring faults in hal_sim.cpp are what the loops are wired to.',
  },
  {
    name: 'show',
    dir: 'firmware/show',
    // One aircraft of the show. hal_esp32.cpp is left out for the same reason it is
    // left out of the drone bundle: it is #if-guarded to nothing under RANCH_SIM.
    files: [
      'src/board.h',
      'src/hal.h',
      'src/mavlink.h',
      'src/mavlink.cpp',
      '../lib/frame_codec.h',
      '../lib/scheduler.h',
      '../lib/show_core.h',
      'src/rtk.h',
      'src/rtk.cpp',
      'src/plan.h',
      'src/plan.cpp',
      'src/show.h',
      'src/show.cpp',
      'src/telemetry.h',
      'src/telemetry.cpp',
      'src/hal_sim.cpp',
      'src/main.cpp',
    ],
    defines: '#define RANCH_SIM 1\n',
    entry: 'void setup() { ranch::appSetup(); }\nvoid loop() { ranch::appLoop(); }\n',
    note: 'The receiver, flight controller, airframe, strip and pack in hal_sim.cpp are what the programme is flown against.',
  },
];

const LOCAL = /^\s*#\s*include\s*"([^"]+)"/;
const problems = [];

function bundle(p) {
  const base = path.join(ROOT, p.dir);
  const order = p.files.map(f => path.basename(f));
  const parts = [];
  let bytes = 0;

  for (const rel of p.files) {
    const abs = path.resolve(base, rel);
    if (!fs.existsSync(abs)) { problems.push(`${p.name}: missing ${rel}`); continue; }
    const src = fs.readFileSync(abs, 'utf8');
    bytes += Buffer.byteLength(src);

    const out = [];
    for (const line of src.split('\n')) {
      if (/^\s*#\s*pragma\s+once\s*$/.test(line)) continue;   // one translation unit now
      const m = line.match(LOCAL);
      if (!m) { out.push(line.replace(/\r$/, '')); continue; }
      const want = path.basename(m[1]);
      const at = order.indexOf(want);
      if (at < 0) {
        problems.push(`${p.name}: ${rel} includes "${m[1]}" which is not in the file list`);
      } else if (at >= parts.length) {
        problems.push(`${p.name}: ${rel} includes ${want}, which appears later in the list`);
      }
      // Local includes are satisfied by concatenation, not by the compiler.
    }
    parts.push({ rel, text: out.join('\n').replace(/\n{3,}/g, '\n\n').trimEnd() });
  }

  const head = [
    '// GENERATED FILE - do not edit, edit the project and re-run:',
    '//   node tools/bundle.mjs',
    `// Source: ${p.dir} + firmware/lib  (${parts.length} files, ${(bytes / 1024).toFixed(1)} KB before bundling)`,
    `// ${p.note}`,
    '//',
    '// Build the same code for hardware with:  pio run -d ' + p.dir,
    '',
    p.defines,
  ].join('\n');

  const body = parts.map(x => `\n/* ==================== ${x.rel} ==================== */\n\n${x.text}\n`).join('');
  const tail = `\n/* ==================== simulator entry point ==================== */\n\n${p.entry}`;
  const all = head + body + tail;
  const sha = crypto.createHash('sha256').update(all).digest('hex').slice(0, 12);
  return { all, sha, bytes: Buffer.byteLength(all), files: parts.length };
}

if (problems.length) {
  console.error('bundle aborted:');
  for (const x of problems) console.error('  ' + x);
  process.exit(1);
}

fs.mkdirSync(path.join(ROOT, 'firmware/build'), { recursive: true });
for (const p of PROJECTS) {
  const r = bundle(p);
  const out = path.join(ROOT, 'firmware/build', `${p.name}.ino`);
  fs.writeFileSync(out, r.all, 'utf8');
  console.log(`${p.name}: ${r.files} files -> ${(r.bytes / 1024).toFixed(1)} KB  sha256:${r.sha}`);
}
