# 五块板的元件清单（画布上的每一件都要能在市场上买到）

| 板子 | board.h 里的名字 | GPIO | 画布上的件 | 现场真实器件（市场型号） |
|---|---|---|---|---|
| light | PIN_RELAY_STREET | 4 | relay | HF115F-003 5V 继电器模块（10 A 触点，场院周界接触器线圈侧） |
| light | PIN_RELAY_HOUSE | 2 | relay | HF115F-003 5V 继电器模块（门廊 / 厨房回路） |
| light | PIN_DIM_BARN | 27 | opamp-lm358 | LM358 输出级 + 0-10 V 调光接口（牛舍高棚驱动器控制端） |
| light | PIN_PIR | 35 | pir-motion-sensor | HC-SR501 人体红外模块（牛舍，高电平有效） |
| light | PIN_LDR | 36 | photoresistor-sensor | GL5528 光敏电阻 + 分压（屋檐光照） |
| light | PIN_BALLAST | 39 | resistor-330 | SCT-013-030 电流互感器 + 33Ω 采样电阻（灯头母线） |
| light | PIN_FAULT | 34 | pushbutton | 驱动器故障干接点（开 = 正常） |
| light | PIN_RTC_SDA | 21 | ds3231 | DS3231MZ + 24C32（时刻表在掉电后仍然成立） |
| light | PIN_WDT_FEED | 18 | —（画布上没画） | SPV05-MG / TPS3823 外部看门狗（件库无对应真实件） |
| water | PIN_VALVE_BARN | 27 | mosfet-irf540 | IRF540N + 1N4007 续流（牛舍 24 V 电磁阀，RY8201-02NC） |
| water | PIN_VALVE_HOUSE | 26 | mosfet-irf540 | IRF540N + 1N4007 续流（主屋 24 V 电磁阀） |
| water | PIN_PUMP | 25 | relay | HF115F-003 继电器驱动增压泵接触器（CJX2-0910 线圈侧） |
| water | PIN_FLOW_BARN | 4 | ic-74hc14 | YF-S201 水表（7.5 Hz/L）+ 10k 上拉 + 74HC14 施密特整形 |
| water | PIN_FLOW_HOUSE | 13 | ic-74hc14 | YF-S201 水表（主屋支路）+ 上拉 + 74HC14 整形 |
| water | PIN_PRESSURE | 36 | opamp-lm358 | MPX-V500 / EPM-100 压力变送器 0.5-4.5 V，LM358 跟随后进 ADC |
| water | PIN_LEVEL | 39 | tilt-switch | 电阻式浮子液位（投入式液位计 SV-300 同位） |
| water | PIN_TANK_HIGH | 34 | pushbutton | 水箱上限位干接点（满停泵，不进溢流管） |
| water | PIN_LED_STATUS | 2 | led | 绿色 3mm 指示灯 |
| water | PIN_RTC_SDA | 21 | ds3231 | DS3231MZ + 24C32（掉电后定时计划仍然成立） |
| water | PIN_LEAK | 35 | —（画布上没画） | 绳式漏水传感器（SV-300 / TT105 类，件库无对应真实件） |
| water | PIN_WDT_FEED | 18 | —（画布上没画） | SPV05-MG / TPS3823 外部看门狗（件库无对应真实件） |
| power | PIN_VA | 36 | opto-pc817 | ZMPT101B 电压互感器模块（A 相，内部 PC817 光耦隔离） |
| power | PIN_VB | 39 | opto-pc817 | ZMPT101B 电压互感器模块（B 相） |
| power | PIN_VC | 32 | opto-pc817 | ZMPT101B 电压互感器模块（C 相） |
| power | PIN_IA | 33 | resistor-330 | SCT-013-030 电流互感器 + 33Ω 采样（A 相，15 mV/A） |
| power | PIN_IB | 34 | resistor-330 | SCT-013-030 + 采样（B 相） |
| power | PIN_IC | 35 | resistor-330 | SCT-013-030 + 采样（C 相） |
| power | PIN_ZC_A | 23 | opamp-lm358 | ZMPT101B 板载 LM393 过零比较输出 |
| power | PIN_BRK_AUX | 18 | pushbutton | 总闸辅助触点（干接点到地） |
| power | PIN_PUMP_FB | 19 | pushbutton | 接触器辅助触点（CJX2-0910 的 NO 侧，合闸证明） |
| power | PIN_PUMP | 25 | relay | HF115F-003 继电器 → 水泵接触器线圈 |
| power | PIN_LIGHTS | 26 | relay | HF115F-003 继电器 → 场院照明馈线接触器线圈 |
| power | PIN_IN_PUMP | 13 | slide-switch | 柜门本地许可开关（水泵馈线，高电平有效 + 下拉） |
| power | PIN_IN_LIGHTS | 27 | slide-switch | 柜门本地许可开关（照明馈线） |
| power | PIN_LED_STATUS | 2 | led | 绿色 3mm 指示灯 |
| power | PIN_WDT_FEED | 4 | —（画布上没画） | SPV05-MG / TPS3823 外部看门狗（件库无对应真实件） |
| fire | PIN_Z_HOUSE | 39 | resistor-4k7 | 4k7 终端电阻（EOL）+ 主屋回路 JTY-GD-3200 探测器 |
| fire | PIN_Z_BARN | 36 | resistor-4k7 | 4k7 EOL + 牛舍回路探测器 |
| fire | PIN_Z_STORE | 32 | resistor-4k7 | 4k7 EOL + 仓库 / 饲料间回路 |
| fire | PIN_Z_POWER | 33 | resistor-4k7 | 4k7 EOL + 配电房回路（火最先来的地方） |
| fire | PIN_MCP | 34 | pushbutton | J-SAP-M-51 破玻璃手动报警按钮 |
| fire | PIN_FLOW | 35 | tilt-switch | WFS-1 湿式喷淋水流开关 |
| fire | PIN_SIREN | 25 | relay | HF115F-003 继电器 → 室内声光报警器 |
| fire | PIN_STROBE | 27 | relay | HF115F-003 继电器 → 室外频闪警灯 |
| fire | PIN_PUMP | 14 | relay | HF115F-003 继电器 → 喷淋泵控制柜许可干接点 |
| fire | PIN_SIREN_FB | 26 | pushbutton | 铃臂继电器辅助触点（证明铃真的动了，不是只给了电） |
| fire | PIN_KEY_ARM | 5 | slide-switch | 面板钥匙开关 ARM / BYPASS |
| fire | PIN_BTN_SILENCE | 13 | pushbutton | 面板静音按钮（瞬时到地） |
| fire | PIN_BTN_TEST | 18 | pushbutton | 警铃自检按钮 |
| fire | PIN_LED_STATUS | 2 | led | 绿色 3mm 运行指示灯 |
| fire | PIN_WDT_FEED | 4 | —（画布上没画） | SPV05-MG / TPS3823 外部看门狗（件库无对应真实件） |
| drone | PIN_SAFE_OUT | 38 | bjt-2n2222 | 2N2222 + 1N4148 驱动飞控 RCPIN 安全开关（高 = SAFE，锁死电机） |
| drone | PIN_ARM_IN | 39 | slide-switch | 钥匙 ARM 开关（低 = 闭合：放到起飞按钮旁，先合上它再按起飞） |
| drone | PIN_BATT_ADC | 1 | resistor-10k | 10k/33k 分压（4.3:1，4S 电池组电压） |
| drone | PIN_CURR_ADC | 2 | cap-100n | ACS712-20A 电流模块（载荷母线，100 mV/A）+ 100n 滤波 |
| drone | PIN_BUZZER | 42 | buzzer | 有源蜂鸣器（失联 / 低电） |
| drone | PIN_LED_STATUS | 41 | led | 绿色 3mm 状态灯 |
| drone | PIN_SD_CS | 21 | microsd-card | TF / microSD 卡座（硬件 SPI，飞行日志） |
| drone | PIN_FC_TX | 17 | —（画布上没画） | 飞控本体（Pixhawk 3X / ArduPilot，UART2 57600 MAVLink）——件库无对应真实件 |
| drone | PIN_DBG_TX | 43 | —（画布上没画） | 第二路调试串口（43/44 = TX/RX），无外设 |
| drone | PIN_WDT_FEED | 40 | —（画布上没画） | SPV05-MG / TPS3823 外部看门狗（件库无对应真实件） |
| drone | CAM_PIN_XCLK | 3 | —（画布上没画） | OV2640 摄像头模组（14 根 DVP 线，件库无对应真实件） |
| show | PIN_LED_DATA | 8 | neopixel | WS2812B ×2：臂灯 + 腹部信标（每颗满白 160 mA，BEC 预算写在 board.h） |
| show | PIN_SAFE_IN | 39 | slide-switch | SAFE 钥匙开关（低 = 插入，电机锁死） |
| show | PIN_BATT_ADC | 1 | resistor-10k | 10k/33k 分压（4.3:1，4S 电池组电压） |
| show | PIN_RTK_TX | 16 | gps-neo6m | NEO-M9N RTK 模块（件库里只有 NEO-6M：同一族 UBX 协议，RTK 定解画不出来） |
| show | PIN_BUZZER | 41 | buzzer | 有源蜂鸣器（失联 / 低电才响，节目自己收尾不响） |
| show | PIN_LED_STATUS | 42 | led | 绿色 3mm 台架灯：一秒一闪 = 等节目，两闪 = 在飞，不亮 = SAFE 拔了 |
| show | PIN_RTK_RX | 15 | —（画布上没画） | RTK 模块回程 TX→RX 与 TIMEPULSE 秒脉冲（真实模块的第 4 脚）——画布上的 NEO-6M 件没有这两个脚位 |
| show | PIN_RTK_PPS | 2 | —（画布上没画） | 同上：把全机群对齐到同一个 GNSS 秒的那根线 |
| show | PIN_FC_TX | 17 | —（画布上没画） | 飞控本体（Pixhawk 3X / ArduPilot，UART 57600 MAVLink）——件库无对应真实件 |
| show | PIN_WDT_FEED | 40 | —（画布上没画） | SGM823 / TPS3813 外部看门狗（件库无对应真实件） |
| show | PIN_DBG_TX | 43 | —（画布上没画） | USB-CDC 控制台（43/44 = TX/RX）：地面站的「上传节目」就是往这根串口敲一行 show=SHOWPLAN,…，画布上没有"插过来的一根 USB 线"可画 |
