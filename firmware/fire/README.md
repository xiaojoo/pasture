# 牧场消防监测板 · ESP32 DevKit V1

四回路探测器 + 手动报警按钮 + 水流开关 + 警铃 + 喷淋泵许可，加一条会自己喊的上行。
它是一张面板，不是一个小开发板：所有输入都要能被剪断、短路、腐蚀，输出要能被证明真的动了。

## 构建与烧录

```
pio run -d firmware/fire
pio run -d firmware/fire -t upload
pio device monitor --baud 115200      # 每 1 s 一行 FIRE,k=v；事件行是 RANCH,evt=
pio test -d firmware/fire -e native
```

## 回路怎么读

每个回路一个模拟输入：4k7 上拉到 3V3，末端装 EOL 电阻，探测器继电器闭合时把 1k 报警电阻接入。
于是四个可读的档位（`lib/fire_logic.h`）：

| 回路电阻 | ADC | 判定 |
|---|---|---|
| 0 Ω | ~0.00 V | SHORTED：压扁的线缆 / 螺丝压到线，不是火警 |
| 1 kΩ | ~0.58 V | ALARM |
| 4.7 kΩ | ~1.65 V | 正常 |
| 断路 | ~3.30 V | OPEN：剪线 / 防拆 |
| 带之间 | — | Unknown：腐蚀或过渡态，不猜边 |

档位边界存在 NVS（`b_shi/b_alo/b_ahi/b_nlo/b_nhi/b_olo`），改成 10k 上拉的站点必须重量并改写，
否则每条健康回路都会读成 dirty。状态要连续 3 个采样一致才被采纳（60 ms @ 20 ms 扫描）。

## 文件

| 文件 | 作用 |
|---|---|
| `src/board.h` | 引脚 + 六个回路全在 ADC1 的预算证明 + 四条 static_assert |
| `src/hal.h` | 一份接口两份实现；仿真侧能注入剪线/短路/腐蚀/继电器抖动/警铃死 |
| `src/panel.cpp` | 采回路（带去抖）、写继电器、累计输出通电时长 |
| `src/telemetry.cpp` | `FIRE,k=v` 状态帧 + 保持有效的拨号行 `ranch/fire/dial` |
| `src/main.cpp` | 阈值、20 ms 扫描 + 50 ms 决策 + 1 s 上报、按钮边沿 |
| `../lib/fire_logic.h` | 平台无关的判定：确认、闩锁、静音、自检、旁路 |
| `test/test_sandbox.cpp` | 整机 + 建筑模型沙盒：57 条断言，含六种反证 |

## 已验证（带数字）

`bash .probe/fire.sh` → `57 checks, 0 failures`
`bash .probe/cc.sh` → `407 checks, 0 failures`（fire_logic/fire 环路新增 34 条）
`bash .probe/bundle.sh` → `bundle fire: build=ok, run=0, frames=30000`

| 场景 | 板子的判定 | 建筑模型的 | 结论 |
|---|---|---|---|
| 正常夜 20:10 | `lvl=NORMAL z=0000` 无 why | 四条回路都在 EOL 档 | 25–35 帧/30 s，1 Hz 上报 |
| 牛舍单一探测器 | `lvl=ALARM why=SINGLE ZONE z=0100`，铃 + 闪灯 | 警铃继电器动作 1 次，许可触点是断的 | 一个探测器只让楼里听见，不让喷淋启动 |
| 同一状态再 20 s | `lvl=EMERGENCY` | `pump_permit_ms > 0` | 时间本身就是第二意见（每栋只有一个探头） |
| 牛舍 + 主屋 | `lvl=EMERGENCY` 立刻 | 许可立即闭合 | 两个探头不需要等时钟 |
| 手动报警按钮 | `lvl=EMERGENCY mcp=1` | — | 人砸的玻璃本身就是第二意见，且**不许静音** |
| 静音 | `sil=1` 铃停，`strb=1` 仍闪 | 事件 `evt=silence-refused` 只在被拒时出 | 静音只消声音，120 s 后自己再响 |
| 回路剪断 | `lvl=FAULT z=02xx` | 铃没响 | 断路是监管故障，不是火 |
| 回路短路 | `z=0003`，`lvl=FAULT` | 泵许可保持断开 | 压扁的线缆不该把配电房淹了 |
| 继电器抖动 | 状态一直 Unknown/Normal | 从未 `EMERGENCY`，铃 0 帧 | 连续 3 采样把它挡在门外 |
| 警铃线圈好、衔铁不动 | `bellfail=1 sup=1`，`lvl=ALARM` 保持 | 模型也报 1 次失败 | 报警不被降级成故障，故障另带一个字段 |
| 有烟时按 reset | 事件 `reset-refused`，帧 `why=RESET BLOCKED` | — | 带原因闩锁时复位被拒，不是闪一下 NORMAL |
| 烟散后再 reset | `evt=reset` → `lvl=NORMAL` 输出全掉 | `false_alarm_starts=1` | 这次没被证实的报警被记为误报 |
| 钥匙打 BYPASS | `arm=0` 输出全停，烟还在时 `lvl=ALARM` 保留 | — | 旁路掐的是输出，不是显示 |
| 8 小时长跑 | `bellfail=1` 帧数 0，`sup=1` 帧数 0，`sirenm=1` | 自检 8 次全部听见 | 每小时自检不误伤 |

## 这些数字不代表什么

- **`hal_esp32.cpp` 没有被任何东西编译过**（esp32 平台包本机取不下来）。上面全是仿真 HAL 的数字；
  真机的 flash/RAM、比较器输入门槛、11 dB 衰减在带顶端和底端的非线性都还没量。
  上板第一件事是 `pio run` 过不过，然后是下面清单。
- **短路和报警在同一档位上是分不开的**（两线回路 + 单极性分压）。真面板靠极性敏感 EOL 区分，
  这块板只把 0 Ω 报成 `short` 档，靠"这个回路的探头没有地址上报"人来判断。别把它当隔离故障的能力。
- **确认时间 20 s 是设计选择，不是规范**。要按当地规范改 `confirm`（5–300 s 可写 NVS）。
- **自检只证明衔铁动了**，不证明声压够：铃被拆了、被泡沫包住，这块板听不见。
- **上行断了自己会重连**，但拨号靠 broker 的 retained 消息；没有 broker 的现场要把 `esp32-fire-ttn` 打开。

## 接线要点

| 功能 | GPIO | 备注 |
|---|---|---|
| 主屋 / 牛舍回路 | 37 / 36 | ADC1_CH1 / CH0 |
| 仓库 / 配电房回路 | 32 / 33 | ADC1_CH4 / CH5，配电房是火最先来的地方 |
| 手动报警按钮 / 水流开关 | 34 / 35 | ADC1_CH6 / CH7，只读脚，正好当输入 |
| 警铃 / 频闪 / 泵许可 | 25 / 27 / 14 | 继电器线圈 |
| 衔铁辅助触点 | 26 | 干接点进 GND，内部上拉 |
| 钥匙 / 静音 / 测试 | 5 / 13 / 18 | 干接点进 GND；钥匙断开 = BYPASS（安全侧读数） |
| DS3231 | I2C 21/22 @0x68 | 日志要有时间，掉电后还能对上 |
| 看门狗 / 状态灯 | 4 / 2 | 面板任务优先级最高（PRIO_PANEL 18） |

上电验收（每次装完都要走）：

1. 拔掉任一回路末端 EOL → 该路读 `2`（open），铃不响，`evt` 里有 dial。
2. 该回路两端短接 → 读 `3`（short），且**泵许可不能闭合**。
3. 用 1 kΩ 替 EOL → 3 s 后 `ALARM`，`z` 对应位是 `1`，铃响。
4. 保持 1 kΩ 20 s → 变 `EMERGENCY` 且许可闭合；`pfb=1`。
5. 拿掉 1 kΩ 并按 reset → `lvl=NORMAL`，输出全掉，日志里有一条 `evt=reset`。
6. 报警状态下按 reset → 应看到 `evt=reset-refused`，闩锁不清。
7. 报警状态下按静音 → 铃停、闪灯不停、120 s 后自动再响；砸玻璃时按静音应被拒。
8. 警铃供电断开后自检 → `bellfail=1`、`sup=1`，且恢复后不会自己清（要 reset）。
9. 钥匙打 BYPASS → 所有输出停，烟还在时 `lvl` 仍是 ALARM（旁路掐输出不掐显示）。
10. 断掉 WiFi 24 h 再恢复 → 期间事件应能从 retained 的 dial 主题读到。
