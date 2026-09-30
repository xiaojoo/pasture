# 牧场巡检无人机 · ESP32-S3 伴飞计算机

这块板子不是飞控。飞控（ArduPilot 一类的 FC）负责 1 kHz 的姿态与位置闭环，
这份固件是它的**伴飞计算机**：读 MAVLink、上传航线、触发相机、跑 MK 图传、
按失联/电量/地理围栏表做安全监督，并把状态推给牧场后台。

## 构建与烧录

```
pio run -d firmware/drone                     # esp32s3-drone
pio run -d firmware/drone -e esp32s3-drone -t upload
pio device monitor --baud 115200              # USB-CDC
node tools/bundle.mjs                         # 生成浏览器用的 firmware/build/drone.ino
```

两个构建目标共用同一套源码：

| 目标 | HAL 后端 | 相机 | MQTT | 用途 |
|---|---|---|---|---|
| `esp32s3-drone` / `esp32-drone-wroom` | `hal_esp32.cpp` | esp32-cam MJPEG | PubSubClient | 真机 |
| 浏览器 / 主机测试（`RANCH_SIM`） | `hal_sim.cpp` | 计数器 | 关闭 | 牧场 3D 场景镜像 |

## 文件

```
platformio.ini     两个 esp32 环境 + native 主机测试环境
src/board.h        引脚表、飞行包线、任务优先级；底部 static_assert 证明引脚不重复、
                   不占用 flash/octal-PSRAM/USB-JTAG，且两路模拟量都在 ADC1
src/hal.h          硬件抽象（两个实现）
src/mavlink.*      手写 MAVLink v2：分帧、X.25 CRC + CRC_EXTRA、按尺寸降序的字段表
src/mission.*      航线上传、AUTO 交接、地理围栏、拍照触发
src/safety.*       解锁前检查、失联表、外部看门狗
src/telemetry.*    key=value 状态帧 → 串口 / MQTT / SD 三个出口，同一份字节
src/video.*        OV2640 + esp_http_server 的 MJPEG 图传，含按帧率自适应降质
src/main.*         参数(NVS)、任务表、自动起飞时刻
test/test_sandbox.cpp  主机上跑整份应用（含仿真机体）的验收测试
```

## 已验证（带数字）

* 主机：`firmware/lib` 104 项检查、沙箱 34 项检查全绿。沙箱在虚拟时间里飞完整条
  9 航点航线（机时 192 s），8 个相机航点各触发一次快门，仿真 FC 回执与规划器的
  计数一致，最后 0.3 m 内落地。
* 反证：同一份二进制、安全开关置位时——不解锁、不离开地面、`safetyCanArm` 报
  `SAFE SWITCH`。门禁不会两边都绿。
* 真编译器：`firmware/build/drone.ino`（126 231 字节 / 3 728 行）在 Arduino-ESP32
  工具链下 **编译通过**，产物 `velxio-sketch.bin` 239 424 字节，
  `merged_flash.bin` 304 960 字节；分段 `flash.text` 126 216 / `flash.rodata` 51 148 /
  `iram0.text+vectors` 46 127 / `dram0.data+bss` 29 084 字节。
* 浏览器：Run 之后串口出 950 帧 `DRONE,…`，状态机走完
  GROUND→CLIMB→TRANSIT→DWELL→…→RTL→DESCEND→LANDED，`shots=8`、`batt=92`，
  场景按遥测镜像。

## 这些数字不代表什么

上面的 flash/RAM 是**仿真 HAL** 的体积：`hal_esp32.cpp` 被 `#if !defined(RANCH_SIM)`
排除在 bundle 之外，所以这份统计不含 esp_camera、WiFi、PubSubClient、SD 的链接开销。
真机版本通常大几百 KB 并多占 DRAM/PSRAM——买板前请按 `pio run -e esp32s3-drone`
自己的输出为准，那一步目前被网络卡住（见 firmware/README.md 的“未完成”）。

`FC_MODE_AUTO=3 / RTL=6 / LOITER=5 / LAND=9` 是 ArduCopter 的 custom_mode，属于 **FC
固件的属性**，不是这份代码的。首次联调请对照你飞控的 Mode 页；填错的表现是
“模式始终没切过去”，交接序列会一直重试并在 `arm=` 字段里告诉你卡在哪一步。

## 联调顺序

1. 只接 USB，看 `RANCH,evt=boot,rst=…` 与 `DRONE,…,safe=1`。
2. 接 FC 的 telemetry 1（57600，17/18）。看 `RANCH,evt=health,rxcrc=0`——rxcrc 非 0
   说明接线反了或波特率不对，不是代码问题。
3. 拨/arm 开关：`safe` 翻成 0，`arm=WAIT FIX → SET MODE → ARM → TAKEOFF → AUTO`。
   卡在某一步就停在那一步重试，不会跳步。
4. 首次带相机：`RANCH,evt=health` 里 `nologue` 不涨说明 SD 挂上了。
