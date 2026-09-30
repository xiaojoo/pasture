# Ranch firmware

Three flashable firmware images plus the platform-neutral logic they share.

```
firmware/
├── lib/                      platform-neutral, no Arduino/ESP-IDF headers, host-testable
│   ├── mission_planner.h          waypoint mission state machine + geofence
│   ├── failsafe.h                 link / battery / geofence decision table
│   ├── valve_logic.h              solenoid sequencer: inrush, hold, stuck, trailing water
│   ├── programme.h                dispense cycles anchored to the wall clock
│   ├── water_safety.h             leak / over-pressure / dry-run / tank decisions
│   ├── astro.h                    sunrise, sunset, twilight, polar cases
│   ├── light_policy.h             dusk ramp, economy level, motion, timed override
│   ├── scheduler.h                rate scheduler with measured overrun
│   ├── frame_codec.h              bounded key=value frame writer and reader
│   └── tests.cpp                  host unit tests for all of the above
├── drone/                    ESP32-S3 companion computer (MAVLink + 图传 + 巡航 + 上云)
├── water/                    ESP32 water controller (valves, pump, metering, interlocks)
├── lighting/                 ESP32 lighting controller (astronomical clock, dimming, lamp current)
└── build/                    generated single-translation-unit bundles for the web simulator
```

## 验证到哪一步（数字，2026-09-29）

| 门禁 | 命令 | 结果 |
|---|---|---|
| 共享逻辑单测 | `.probe/cc.sh` | **301 项 0 失败**，含 `board.h` 引脚断言 |
| 无人机整机沙箱 | `.probe/sim.sh` | **34 项 0 失败**（含"安全开关置位"反证） |
| 水利整机沙箱 | `.probe/water.sh` | **32 项 0 失败**（含漏水、干涸、无时钟三个反证） |
| 照明整机沙箱 | `.probe/light.sh` | **17 项 0 失败**（含灯坏、无时钟两个反证） |
| 三个 bundle 编译并跑 | `.probe/bundle.sh` | drone/water/lighting 全 ok，帧数 31 199 / 60 269 / 75 000 |

**没验证到的**：`hal_esp32.cpp` 与 `*_task` 的 FreeRTOS/看门狗调用不在上面任何一条里——
它们被 `#if !defined(RANCH_SIM)` 排除在仿真与主机编译之外，而 `pio run -e esp32s3-drone`
目前下不动 esp32 core（GitHub release HEAD 超时）。所以**真机 flash/RAM 数字还没有**，
各板 README 里给的是仿真 HAL 产物的体积，不是出货固件的。


## Two build targets, one source of truth

| Target | Tool | What it proves |
|---|---|---|
| Real hardware | `platformio.io` (`pio run -e <env>`) or `arduino-cli compile` | The image you flash |
| Browser simulator | `node tools/bundle.mjs` → `build/<name>.ino` | The same state machines, driven by the same code, with the HAL swapped for a simulated back-end |

`tools/bundle.mjs` concatenates `lib/` + the board's `src/` into one translation unit,
defines `RANCH_SIM`, and replaces the hardware HAL translation unit with
`hal_sim.cpp`. The web panel fetches `build/<name>.ino` and writes it into the
editor, so a firmware change is visible in the simulator without duplicating code.

## Conventions

- Every module is `<name>.h` + `<name>.cpp`, includes its own header first, and
  never allocates on the hot path.
- All timing is monotonic (`millis()` wrapped into `uint32_t` deltas); no
  `delay()` outside of boot sequencing.
- Every task registers with the task watchdog and toggles the external
  watchdog pin through the main supervisor.
- Parameters live in NVS with a schema version; an incompatible version resets to
  defaults and raises a `STATUSTEXT`/log line rather than silently coercing.
- Failure policy is always explicit: a lost link, a bad CRC, an out-of-range
  parameter and a saturated sensor each have their own branch and their own
  reported reason.
- `-Wall -Werror` in every environment.

## Flashing

Each board directory has a README with the wiring table, the `pio run -t upload`
command for the exact part, the serial monitor settings, and an acceptance
checklist to run on the bench before the unit goes into the field.
