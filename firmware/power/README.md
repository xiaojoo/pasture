# 牧场配电监测板 · ESP32 DevKit V1

装在水电共用柜里的那块板：它知道牧场正在用多少电、供入还是不是健康，以及水泵允不允许启动。
它看着三相电压/电流、一个漏电流互感器、柜温、主闸辅助触点，并握着水泵和场院照明两个接触器。

## 构建与烧录

```
pio run -d firmware/power                 # esp32-power
pio run -d firmware/power -t upload
pio device monitor --baud 115200          # 每 500 ms 一行 POWER,k=v
pio test -d firmware/power -e native
```

TTN 上行（本地图没有 broker 时）：`pio run -d firmware/power -e esp32-power-ttn`。

## 文件

| 文件 | 作用 |
|---|---|
| `src/board.h` | 引脚表 + ADC1 通道预算 + 四条 static_assert（无重复脚 / 不占 SPI flash / 输出不在只读脚 / 模拟输入全在 ADC1） |
| `src/hal.h` | 一套接口，两份实现：`hal_esp32.cpp` 读真实前端，`hal_sim.cpp` 读电网模型 |
| `src/meter.cpp` | 六路整流 RMS 前端 → 每相有效值、周期测频、能量积分、量程自检 |
| `src/feeders.cpp` | 唯一动接触器线圈的模块：许可/切除/跳闸三种权限，加上辅助触点回执 |
| `src/telemetry.cpp` | `POWER,k=v` 上行（串口 + MQTT `ranch/power/state`），命令走 `ranch/power/cmd` |
| `src/main.cpp` | 阈值装载、100 ms 控制环、跳闸人工复位闩、看门狗 |
| `../lib/power_meter.h` | 三相算术（平台无关，主机可测） |
| `../lib/power_quality.h` | 保护决策表：漏电 > 超温 > 频率 > 主闸 > 缺相 > 过载 > 不平衡 > 电压 |
| `test/test_sandbox.cpp` | 整机 + 电网模型的沙盒：70 条断言，含五种反证 |

## 已验证（带数字）

`bash .probe/power.sh`（WSL, g++ -std=c++17 -Wall -Werror）→ `70 checks, 0 failures`。
`bash .probe/cc.sh` → `373 checks, 0 failures`（其中 power_meter / power_quality 新增 72 条）。
`bash .probe/bundle.sh` → `bundle power: build=ok, run=0, frames=62999`（浏览器要粘的那份单文件被真编译真跑）。

| 场景 | 板子报的 | 电网模型的 | 结论 |
|---|---|---|---|
| 06:30 正常 | va 224.3 V, hz 50.01, kw 17.3 | 224.5 V, 50.01, 17.3 | 差 0.2 V（0.07%），来自 0.3 s 滤波 |
| 12 s 电能 | 0.0576 kWh | 0.0578 kWh | 差 0.35%，积分对得上 |
| B 相塌掉 | `act=SHED why=PHASE LOSS`，水泵 4 帧后断开 | 接触器确实开着，单相运行 1 次 | 缺相是减载不是跳闸，需求被 `held=1` 记住 |
| B 相恢复 | 10 s 内自己重启动 | `pump_starts` 从 1 变 2 | 减载会自恢复，不需要人 |
| 漏电到 150 mA | `act=TRIP why=LEAKAGE`，149 mA | 150 mA | 漏电动作不 debounce（已经是故障电流） |
| 漏电消失后 20 s | 仍然 `TRIP`，`lock=` 在数 | 电流已回 8 mA | 人工复位：`reclose` 之后水泵才回来 |
| ADS1115 不应答 | `ok=0 act=ALARM why=SENSOR FAULT` | 水泵持续带电，`pump_starts=1` | 测不到就只报警，不切已经在跑的负荷 |
| A 相电压采样线脱落（负荷仍在draw） | 电压保持 224.3 V 不写假 0，5 s 后 `SENSOR FAULT` | 该相确实还带电，水泵全程没停 | 与上面「B 相塌掉」是同一读数的两种真相，靠同相电流分辨；错了任一种要么白切水泵要么烧电机 |
| 全场同开（×5） | `load=93%` → `SHED` | kva 41.9/45 = 93.1% | 到减载线就切非关键回路，不跳闸 |
| 再往上（×8） | `load=130%` → `TRIP/OVERLOAD` | kva 61.1/45 = 135.8% | 差值是真因：电流前端在 103 A 被稳压管钳住 |
| 频率 47.2 Hz | `hz=47.20 act=TRIP why=FREQUENCY` | 50 Hz 电网 | 2 s 防抖后跳闸，300 s 锁到期自动合 |
| 本地开关 | 断开 GPIO0 许可 | 接触器立刻释放 | 手/自动开关优先于整个网络请求 |

## 这些数字不代表什么

- **`hal_esp32.cpp` 没有被任何东西编译过。** esp32 平台包在本机还下不下来（GitHub release HEAD 超时），
  所以上面全是「仿真 HAL」的数字：真机的 flash/RAM、LEDC/ISR 时序、ADS1115 寄存器实测还没有。
  上板第一件事是 `pio run` 能不能过，然后才是下面的验收清单。
- **功率因数不是测出来的。** 前端是整流的，只给每相 RMS 大小，不给相位，所以 `pf=` 是站点参数
  （`SITE_POWER_FACTOR`，默认 0.92）。只有 kW/kWh 带这个假设；每条保护规则用的是电压、负载百分数、
  频率、漏电流、温度，都不需要它。要计费用的功率因数，买一只 Modbus 电表，不要相信这块板。
- **`load=130%` 那次是前端饱和的读数**，不是板子算错：3.3 V 量程下 15 mV/A 到 103 A 就到顶。
  保护仍然正确动作，但 100 A 以上的电流读不出来——这是选分流电阻时要重算的一件事。
- **`brownouts=1`** 意味着电机确实在两相上跑了大约 2 s（防抖时间）。如果一次都不能容忍，
  就把 `debounce_s` 调小或者给接触器加一条独立的缺相互锁。

## 接线要点

| 功能 | GPIO | 备注 |
|---|---|---|
| A/B/C 相电压 | 36 / 37 / 32 | ZMPT101 整流前端，230 V 呈现约 920 mV |
| A/B/C 相电流 | 33 / 34 / 35 | 15 mV/A，虚地 1650 mV，反并联硅钳位 |
| 温度 + 漏电流 | I2C 21/22 → ADS1115 0x48 | ADC1 六个脚已被六路 RMS 用光，这是第二块 ADC 存在的原因 |
| A 相过零 | 23 | 比较器方波，两个沿一个周期；频率只从这里来 |
| 主闸辅助 / 接触器辅助 | 18 / 19 | 干接点进 GND，内部上拉，闭合读低 |
| 手/自动许可 | 0 / 39 | 高有效 + 内部下拉。方向是启动安全决定的：GPIO0 复位时为低会进下载模式 |
| 水泵 / 场院照明接触器 | 25 / 26 | 线圈 |
| 外部看门狗 / 状态灯 | 4 / 2 | 看门狗是窗口型，控制环 100 ms 喂 |

上电验收（每次装完都要走）：

1. 断开 A 相采样线 → 应报 `ok=0` 而不是缺相（电流还在，判定是采样掉了）。
2. 合上 B 相负荷到 20% 再断开 B 相采样 → 同样只报警。
3. 用可调电源把 A 相降到 190 V 保持 3 s → `UNDERVOLTAGE` 报警，且只在 B/C 仍正常时才是单相问题。
4. 漏电测试按钮（或 mA 源注入互感器）到 100 mA → 立刻 `TRIP/LEAKAGE`，故障撤掉后仍不复位，
   直到 `reclose`。
5. 主闸分闸 → `BREAKER OPEN`，合闸后自动跟随，不需要人工复位。
6. 断开 ADS1115 的 SDA → `SENSOR FAULT`，此时两个接触器都不许动。
7. 手/自动开关打到自动、`mosquitto_pub -t ranch/power/cmd -m 'pump=1'`，然后停掉发送方 →
   5 s 内水泵接触器释放（请求是有租约的，不会活过发出它的那台机器）。
