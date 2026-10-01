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

* 主机：`firmware/lib` 556 项检查、沙箱 61 项检查全绿（2026-10-01）。沙箱在虚拟时间里飞完整条
  9 航点航线（机时 192 s），8 个相机航点各触发一次快门，仿真 FC 回执与规划器的
  计数一致，最后 0.3 m 内落地。
* 摇杆下行（2026-10-01 新增）：控制台的 `rc=fwd,side,climb,yaw` → `RC_CHANNELS_OVERRIDE` →
  仿真 FC 只在 LOITER 下认它。沙箱量到：地面推杆被拒且原因进了帧（`rcwhy=NOT ARMED`），
  空中 0.9 右杆按住 1.5 s 机位**沿机头右方法向移动 6.72 m、沿机头方向 0.00 m**，
  帧里 `ovr=1`、`mode=HOLD`；松杆 `ovr=0`；`rc=0,6` 判为 `BAD AXES` 而不是读成 60。
  反证只破一边：把 `fwd` 接回 roll 通道，只有那两条轴向判据变红（61 项 2 失败），其余不动。

* 取消自动巡航（2026-10-01 新增，`hold` / `resume` 一对）：`hold` 之后板子**停在 LOITER 不回
  自动**——看门狗原来只看规划器的 phase，而"取消"是停下 phase 机而不是倒带它，所以松杆的下一拍
  就又被拽回 AUTO；现在 `hold_requested` 排在看门狗第一行。取消期间**规划器不按时钟走**：
  dwell 倒数、快门计数、航点游标全都冻住（不然飞机在头顶原地转，仪表盘已经把这条航线飞完了），
  只有 `left=` 还跟着飞机。`resume` 把没飞完的接着飞；地面按 `hold`、没取消就按 `resume`
  都是**拒**（`ack=no:hold` / `no:resume`），不会被说成做了。帧里 `mode=LOITER` 就是这个状态。
  **`patrol` 在空中就等于 `resume`**（2026-10-01 加）：页面上只剩一颗「自动巡检」，第三下也是它，
  而 `patrol` 原来只置 `launch_requested`，`tryLaunch()` 见 `launched` 就返回——于是板子回
  `ack=ok:patrol` 却继续被 `hold_requested` 拽在 LOITER 里不动，这正是"板子说做了其实没做"。
  现在 `missionOnCommand` 里空中那一支先把闩锁清掉再进自动；在地面仍然照旧走原来的发射路径
  （`mission.count` 没建立时不能拿 `missionStartAuto()` 的 false 去回一个 `no:patrol`）。
  代价要写清楚：`missionStartAuto()` 把 `plan.target` 归零，所以**接回自动是从第一个航点重飞整条线**，
  `resume` 也是——两个动词在这一点上没有区别，仪表盘上会看到 `wp` 从 2 跳回 1。
  沙箱 22 项判据，三条反证各自只破一边：删掉看门狗那一行 → 只有"FC 真在 LOITER"两条红；
  删掉规划器冻结 → 只有"航点游标没往前走"一条红；删掉 `patrol` 那一支的清闩锁 →
  只有"取消后按 patrol 把计划接回去了"一条红（末帧停在 `mode=LOITER … ack=ok:patrol`）。
  **两条一开始不会红的判据是怎么不成立的**（值得记）：一是**在地面段取消**——那时飞机不在航点上，
  dwell 根本不会到期，冻不冻结都一样；二是**基准取晚了**——取消后再等 16 s 才记下 `target`，
  而这架机的 dwell 只有 3–6 s，等于拿烧完的计划跟烧完的计划比。改成"一到航点上就取消、
  取消的那一刻取基准、再按板子自己的 `up=` 走满 60 s"才会红。
* 五个 bundle：`node tools/bundle.mjs` 后 `.probe/bundle.sh` 全部 `build=ok run=0`
  （drone 31 199 帧，最后一帧带 `ovr=0`）。

**浏览器那一跳已经量到了**（2026-10-01 第二次试，用无头 Chrome + CDP 真鼠标事件，
见 `firmware/README.md` 的“浏览器这一腿量到了什么”和 `.probe/buttons.mjs`）：页面不再
被系统判成后台标签，`document.hidden=false`、视口 1440×900、500 ms 内 rAF 67 帧，QEMU
跑起来了，无人机板 3.0 s 进运行。实测：

* 抽屉里 起飞 / **自动巡检**（一颗两态）/ 返航 / 降落，帧里依次回
  `ack=ok:takeoff` … `ack=ok:land`，**201–420 ms**，回执那行写「无人机板已执行 takeoff」。
  这个 `ack=` 字段是这一轮才给无人机板补上的——另外四块板早就有。
* 「自动巡检」一颗两态的整条链（2026-10-01，`.probe/patrol2.mjs`，**18 项 0 失败**，清 profile 重跑）：
  地上不亮、按一下 `ok:patrol` 起飞；板子报出 `mode=CLIMB` 后点亮（面板 120 ms 一拍，两轮分别量到
  **407 ms** 和 **1 ms**——差多少取决于轮询落在哪一拍上，别当常数用）；
  爬到 `agl≈8.8 m` 再按 → `ok:hold`、`mode=LOITER`；**停 20 秒**：`wp` 2→2、`shots` 0→0，
  画面里的飞机 x/z 到小数点后两位都没动（`[5.26,-21.49]` → `[5.26,-21.49]`，只有 y 从 7.73 追到 9.23，
  那是场景在追板子报的高度，不是飞机在飞）；按住「↑ 前进」→ `mode=HOLD`、`ovr=1`，机位真的动了；
  松手 → 回 `LOITER`、`ovr=0`，**不弹回航线**；第三按 → `ok:patrol`、`mode=CLIMB`
  （`wp` 从 2 跳回 1，见上面那条代价）。
  按钮的亮/灭读的是**帧里的 `mode`**，不是场景镜像：镜像挂在 board-link 的 200 ms 节拍上，
  第一版就是被它坑的——帧已经 `CLIMB`、按钮还当着没亮，于是第二下又发了一遍 `patrol`，
  整条链往后错一拍。
* 那颗按钮的"按下态"填色去掉了，状态改由 pad 上方**两句常驻的话**说（2026-10-01，
  `.probe/drlines.mjs` 8 项 0 失败）：`自动巡检` 在 `aria-pressed=true / false / 推杆中` 三种情况下
  computed 值与「起飞」那颗逐字相同（`bg rgba(110,231,160,.14)`、`border rgba(110,231,160,.38)`、
  `color rgb(234,247,239)`）；同一把尺子在「FPV 画中画」的按下态上量得到差别
  （`color rgb(110,231,160)`），所以"看不出来"不是尺子瞎。两句分别是「板子现在的相 / 已取消…」和
  「摇杆：…ovr=1…」，**按住方向键时前一句不再被后一句盖掉**（原来是一个槽位挑一句，
  一推杆就把"自动已取消"那句话冲没了）。两条反证各自只破一边：去掉 CSS 里那个
  `:not(.dr-toggle)` → 只有"按下态和起飞同色"红（`color` 变成 `rgb(110,231,160)`）；
  把 `sortie` 在 `ovr=1` 时清空 → 只有"取消说明在推杆时还在"红（量到空串）。
* 同一架机用**手机通道**走一遍（`POST /api/cmd`，页面那两颗按钮不碰）：`drone.hover` → `LOITER`；
  `drone.axes` 连发 8 条 `{fwd:0.8,…}`（260 ms 一条，模拟按住）→ `HOLD`、`ovr=1`；
  `drone.patrol` → `CLIMB`。一条 `axes` 不算推杆：板子 500 ms 收不到就交还通道，所以判据按"连发"量。
  量到过一帧把场景镜像打回 `ovr=null` 的（帧自己写着 `ovr=1`），是"这一帧没带这个键就当作没说"
  的老写法，已改成只在带键时更新——`applyBoardCommand`。
* 按住「↑ 前进」「上升」两个方向键：帧里 `ovr=1`、`mode=HOLD`，抽屉那行
  「摇杆：无人机板在照它飞（DRONE 帧 ovr=1）」；松开回 `ovr=0`。
* 方向键那一行**不写 `ack=`**（`rc` 每 150 ms 一行，会把按钮的回执冲掉），判据在
  `sim.sh` 里：发一条 `rc` 之后帧里仍然是上一条按钮的 `ack=no:nonesuch=1`。

* 反证：同一份二进制、安全开关置位时——不解锁、不离开地面、`safetyCanArm` 报
  `SAFE SWITCH`。门禁不会两边都绿。
* 真编译器：`firmware/build/drone.ino`（**126 231 字节 / 3 728 行那一次**）在 Arduino-ESP32
  工具链下编译通过，产物 `velxio-sketch.bin` 239 424 字节，`merged_flash.bin` 304 960 字节；
  分段 `flash.text` 126 216 / `flash.rodata` 51 148 / `iram0.text+vectors` 46 127 /
  `dram0.data+bss` 29 084 字节。**这些体积是加 `ack=` 之前量的**；现在源文件重生成后是
  **151 921 字节 / 4 226 行**（`.probe/bundle.sh` 这一轮 `build=ok run=0`，31 199 帧），
  那一组 flash/RAM 数要重跑工具链才有，别再当现值引用。
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

`RC_CHANNELS_OVERRIDE` 用 id 70、38 字节载荷、crc_extra **124**。这三个数是从 **ArduPilot
自己的生成方言**里读出来的（`pymavlink 2.4.50`，`dialects/v20/common.py` 里
`RC_CHANNELS_OVERRIDE.crc_extra = 124`，`native_format = "<HHHHHHHHBBHHHHHHHHHH"`），
字段表也和 2021、2024 两份 `common.xml` 一致（`chan1_raw…chan18_raw`，早先那个 `t` 字段已删）。
写在这里是因为我自己按记忆复现 mavgen 算 crc 的那把尺子，在 16 个已知常量上**全部失配**
（它给 id 70 算出 137）——所以这类常数只能从飞控用的方言里读，不能推。
仍然要留一句：真 FC 的方言版本由飞控固件那次构建决定，接真机前用它的 `mavlink.py`/方言再核一次；
仿真 FC 和固件读同一张表，这类错在门禁里量不出来（`firmware/README.md` 另记了这张表已知的 7 处漂移）。

**摇杆这条链在浏览器里量过了**（见上面“浏览器那一跳已经量到了”）：真 rAF、真 QEMU、
真按住方向键，帧里 `ovr=1` → 松杆 `ovr=0`。还剩一条**没量**：这条链的时序是在无头 Chrome
的软件渲染里量的，编译与推杆的耗时都会比一台前台开着的真机器慢；判据本身（谁在飞、
拒因、松杆停）不依赖速度。

## 联调顺序

1. 只接 USB，看 `RANCH,evt=boot,rst=…` 与 `DRONE,…,safe=1`。
2. 接 FC 的 telemetry 1（57600，17/18）。看 `RANCH,evt=health,rxcrc=0`——rxcrc 非 0
   说明接线反了或波特率不对，不是代码问题。
3. 拨/arm 开关：`safe` 翻成 0，`arm=WAIT FIX → SET MODE → ARM → TAKEOFF → AUTO`。
   卡在某一步就停在那一步重试，不会跳步。
4. 首次带相机：`RANCH,evt=health` 里 `nologue` 不涨说明 SD 挂上了。
