# 牧场水利控制板 · ESP32 DevKit V1

一块板上两路互相独立的水：**牛舍**按定时放水，**主屋**走自己的一路，任何一路的
故障都不能把另一路带走。板子同时看泵、看水箱、看漏水。

## 构建与烧录

```
pio run -d firmware/water                    # esp32-water
pio run -d firmware/water -e esp32-water -t upload
node tools/bundle.mjs                        # 浏览器用的 firmware/build/water.ino
```

| 目标 | HAL 后端 | 说明 |
|---|---|---|
| `esp32-water` / `esp32-water-rts` | `hal_esp32.cpp` | 真机（脉冲水表 / RS485 水表） |
| 浏览器 / 主机测试（`RANCH_SIM`） | `hal_sim.cpp` | 一套仿真管路：水井→泵→高位水箱→重力分两路 |

## 文件

```
platformio.ini
src/board.h        引脚表 + 编译期断言（不重复、不占 flash、模拟量必在 ADC1）；
                   阀电气参数（冲击 120 ms / 保持 55 %）、管路限值、定时默认值
src/hal.h          输出（线圈占空比、泵）、脉冲计数、两路模拟量、时钟、NVS
src/sense.*        脉冲→瞬时流量（2 s 窗口）→每周期与累计升数；压力/液位换算与越界判故障
src/valves.*       阀门与泵的时序：先建压再开阀、阀关后再延时停泵、卡阀锁存、硬禁泵
src/telemetry.*    WATER 状态帧（console / MQTT）+ 事件行（另一个 source，避免被当成状态）
src/main.*         参数、任务表、地面指令
test/test_sandbox.cpp  主机上跑整份应用 + 仿真管路
../lib/valve_logic.h   纯逻辑：冲击→保持→卡死→余水收尾
../lib/programme.h     纯逻辑：按本地时钟对齐的放水周期
../lib/water_safety.h  纯逻辑：漏水/超压/空转/水满/卡阀的优先级与禁泵连锁
```

## 已验证（带数字）

主机沙箱 **32 项全绿**（`.probe/water.txt`），仿真管路独立记账，固件只能透过脉冲、
毫伏和两个开关去推断：

* 一次完整工作日：两次定时放水（`runs=2`），阀开累计约 317 s，牛舍 37.6 L、主屋 324.9 L；
  固件自己算出的累计升数与管路计量的差 < 3 L（`barn`、`house` 两路分别核对）。
* 全程 `leaked=0`、`stuck=0`、`dry=0`，水箱从 68 % 涨到满并被浮球开关停泵
  （末帧 `why=TANK FULL`，`pump=0`）。
* 反证一（ borehole 干涸）：注入“失水”后 600 s 内出现 `why=DRY RUN` 共 27 帧，
  泵被**硬禁**、牛舍阀关闭；管路侧 `dry_run_events>0`。
* 反证二（漏水）：注入漏水后不再有新一轮放水（`barn_on=0`），主屋阀在 1.5 s 防抖内
  关闭（10 帧），末帧 `house=off`。
* 反证三（无时钟）：不给 RTC 跑满 2 h——一次都不放水，并且如实上报 `clock=unset`。

纯逻辑另有 **301 项** lib 单测（`../lib/tests.cpp`，含阀门时序、周期锚定、决策表优先级、
太阳时刻、调光策略），生成的 bundle 也用真 g++ 编译并跑过：`water.ino` 85.1 KB，60 269 帧。

## 这些数字不代表什么

上面是**仿真 HAL** 的产物，不含 `hal_esp32.cpp`，所以不含 WiFi/PubSubClient 的链接开销；
真机 flash/RAM 以 `pio run -e esp32-water` 自己的输出为准（那一步受网络限制，见
`firmware/README.md`）。`FLOW_PULSES_PER_LITRE=450` 是 YF-S201 的手册值，换水表必须换它。

## 接线要点

* 24 V 电磁阀经 MOSFET，`27`/`26`；泵经接触器驱动，`25`。三个都是**输出**，
  且都做了“先建压后开阀”的时序，直接并到同一路上会失去这个保护。
* 两路脉冲水表分别接 `4`、`13`。**只接一路**时请把没接那路的 `no_flow` 判据关掉
  （`no_flow` 参数调大），否则那一路会被判成卡阀。
* 压力 `36`、液位 `37` 必须在 ADC1 上：ADC2 与 WiFi 不能同时用。
* 漏水绳 `35`、水满浮球 `34` 是输入专用脚，**没有内部上拉**，必须用带源输出的模块
  或外接上拉。
