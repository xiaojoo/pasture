# 牧场照明控制板 · ESP32 DevKit V1

按**太阳**而不是按钟点开关：院子轮廓灯、主屋门廊、牛舍可调光高棚灯。黄昏是渐亮
不是瞬亮（牛群会被突然跳起的灯惊到，驱动器的冲击电流也不喜欢），深夜降到三成，
有人进牛舍再回到满亮。

## 构建与烧录

```
pio run -d firmware/lighting                   # esp32-lighting
pio run -d firmware/lighting -e esp32-lighting -t upload
node tools/bundle.mjs                          # firmware/build/lighting.ino
```

| 目标 | HAL 后端 | 说明 |
|---|---|---|
| `esp32-lighting` / `esp32-lighting-dali` | `hal_esp32.cpp` | 真机（0-10 V / DALI） |
| 浏览器 / 主机测试（`RANCH_SIM`） | `hal_sim.cpp` | 一套仿真天空与灯具电流 |

## 文件

```
platformio.ini
src/board.h        引脚表 + 编译期断言；调光频率/位宽、LDR 标定两点、电流互感器、
                   站点经纬度与时区（可被 NVS 覆盖）
src/hal.h          继电器、调光占空比、PIR、毫伏、驱动故障接点、时钟、NVS
src/lights.*       唯一动灯的模块：斜率限制、灯电流核对（ commanded 亮而不取流=灯坏）、
                   过流锁存、燃点小时数累计
src/telemetry.*    LIGHT 状态帧（带板子自己的分钟数）+ 事件行
src/main.*         参数、太阳时刻（每天算一次）、指令、任务表
test/test_sandbox.cpp  主机上跑整份应用 + 仿真天空
../lib/astro.h         纯逻辑：NOAA 级数日出日落、晨昏蒙影、极昼极夜标记
../lib/light_policy.h  纯逻辑：黄昏斜坡 → 深夜经济 → 运动提亮 → 手动限时 → 白天压制
```

## 已验证（带数字）

* 太阳时刻对着**年鉴**校，不是对着自己：**301 项** lib 单测里 `astro` 一节打印出
  实测值——春分升起 06:28 / 落下 18:33（白昼 726 分），夏至白昼 847 分、冬至 610 分
  （相差 3 h 57 m，这就是不能用固定 18:00 定时器的理由），70°N 在夏至/冬至都判为
  极昼/极夜而**不给时间**。写错公式的表现是这些数字跑出区间，而不是测试变绿。
* 主机沙箱 **17 项全绿**（`.probe/light.txt`）。固件只看得见光敏电阻的毫伏：
  - 正午起连跑 2 h：`max_duty=0`，`midday_lit=0`，天空 > 1000 lux，末帧 `street=off`。
  - 再跑到凌晨 01:00：轮廓灯在**板子自己的时钟 19:16** 亮起，正是它算出的民用昏影终
    （`set=19:16`），牛舍斜坡到过满亮（`max_duty=1.00`）；整晚"亮着的时候天空最亮"
    只有 **4 lux**，"亮灯且天还亮"的采样次数为 0。
  - 经济时段后有人走动：占空比从 0.34 抬回满亮（`mode` 字段跟着变成 MOTION）。
  - 反证一（灯坏）：注入驱动故障后末帧出现 `fault=` 与 `amp=0.00`——它报的是量到的
    电流，不是它以为的。
  - 反证二（纽扣电池没了）：`mode=NO CLOCK`、`clock=unset`，一次都不 schedule。
* bundle 用真 g++ 编译并跑过：`lighting.ino` 62.6 KB，75 000 帧。

## 这些数字不代表什么

同水利板：上面是仿真 HAL 的产物，不含 `hal_esp32.cpp`（WiFi、PubSubClient、DS3231
读时）的链接开销，真机数字以 `pio run -e esp32-lighting` 为准。

`LDR_MV_AT_DARK/DAYLIGHT` 与 `LUX_AT_DAYLIGHT` 是**标定值**：换光敏分压电阻后要重新
标定，否则“白天压制”会在错的光线上生效。`dayOfYear()` 用 (月-1)×31 的近似只出现在
仿真天空里（故意与 `lib/astro.h` 不同来源），板子上用的是逐日精确的那个。

## 接线要点

* 院子轮廓 `4`、主屋 `2` 是**继电器**输出；`2` 同时也是 dev kit 自带 LED 的脚，
  所以仿真图里那颗灯就是主屋这一路。
* 牛舍 `27` 走 0-10 V 驱动器，PWM 定在 **1 kHz**：LEDC 默认的几十 kHz 会让驱动器
  输出滤波啸叫，牛听得见。
* 电流互感器 `37`（ADC1）判“命令亮了却没电流”；驱动故障接点 `34` 是干接点，
  需要 `INPUT_PULLUP`（代码里已经是）。
* `34/35/36/39` 只能输入、且无内部上拉——PIR 用带源输出的模块（HC-SR501 即是）。
