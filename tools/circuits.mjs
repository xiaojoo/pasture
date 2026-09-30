// Generate one Velxio project per board: the real circuit that board.h describes,
// instead of the simulator's blink example that every preset used to share.
//
// Two rules this file is written against:
//
//   1. The GPIO numbers are never written here. Each row names a `#define` in its own
//      board.h and the number is read out of that file at build time, so a pin move in
//      the firmware moves the wire on the canvas -- and if the firmware stops defining
//      it, this build fails rather than drawing a hopeful picture.
//   2. Every part placed on the canvas is a real, buyable component that sits in that
//      signal chain on the ranch (`real` carries the market name, and it is also what
//      the BOM is built from). Where the simulator has no model for the part the
//      design actually uses -- the OV2640, the flight controller, a supervised loop
//      input module, an external watchdog -- **no part is placed**: the row is listed
//      under "画布上没有对应真实件" instead. A microphone is not a current transformer,
//      and a motor driver is not a water meter, no matter what the library happens
//      to have.
//
//   node tools/circuits.mjs            # write firmware/build/circuits/*.json + bom.md
//   node tools/circuits.mjs --check    # validate only, write nothing
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.dirname(fileURLToPath(import.meta.url)).replace(/[/\\]tools$/, '');
const FW = path.join(ROOT, 'firmware');
const OUT = path.join(FW, 'build', 'circuits');
const META = path.join(ROOT, 'reference', 'velxio-components.json');

// The two boards the simulator offers for this project, with the pin names its part
// carries. Both lists were read off a live canvas (`el.pinInfo.map(p => p.name)`),
// because an unknown pinName does not error -- the wire silently snaps somewhere else.
const PINS = {
  esp32: ['EN', 'VN', 'VP', '34', '35', '32', '33', '25', '26', '27', '14', '12', '13',
    'GND', 'VIN', '3V3', 'GND2', '15', '2', '4', 'RX2', '16', 'TX2', '17', '5', '18',
    '19', '21', 'RX0', 'TX0', '22', '23'],
  'esp32-s3': ['3V3.1', '3V3.2', 'RST', '4', '5', '6', '7', '15', '16', '17', '18', '8',
    '3', '46', '9', '10', '11', '12', '13', '14', '5V', 'GND.1', 'GND.2', 'TX', 'RX',
    '1', '2', '42', '41', '40', '39', '38', '37', '36', '35', '0', '45', '48', '47',
    '21', '20', '19', 'GND.3', 'GND.4'],
};

// The power rails, by the name each part's silkscreen uses. The S3 devkit labels its
// grounds GND.1-.4 and its 3V3 pins 3V3.1/.2, so a bare 'GND' is not a pin name there.
const RAIL = {
  esp32: { GND: 'GND', '3V3': '3V3', VIN: 'VIN' },
  'esp32-s3': { GND: 'GND.1', '3V3': '3V3.1', VIN: '5V' },
};

// GPIO -> the name that pin carries on the part's silkscreen.
const PIN_ALIAS = {
  esp32: { 36: 'VP', 39: 'VN', 1: 'TX0', 3: 'RX0' },
  'esp32-s3': { 43: 'TX', 44: 'RX' },
};

// The pins each part actually carries, read off a live canvas (`el.pinInfo` on the
// placed element) rather than off the component list -- which only gives a pin *count*.
// This exists because an unknown `pinName` does not fail: the editor quietly draws the
// wire to somewhere else, so a status LED wired to pin "1" (it has A and C) or a GPS
// wired to "1" (it has VCC/RX/TX/GND) looks connected on the canvas and carries nothing.
// A row that names a pin outside this table is a build error, not a silent wire.
//
// The table is complete for every part any row below places, and `checkWires` walks the
// finished wire list against it, so a part missing here is a build error too -- half a
// table is worse than none, because the rows it does not cover are exactly the ones no
// one has looked at.
const PART_PINS = {
  led: ['A', 'C'],
  buzzer: ['1', '2'],
  'slide-switch': ['1', '2', '3'],
  pushbutton: ['1.l', '2.l', '1.r', '2.r'],
  'gps-neo6m': ['VCC', 'RX', 'TX', 'GND'],
  neopixel: ['VDD', 'DOUT', 'VSS', 'DIN'],
  relay: ['COIL+', 'COIL-', 'NO', 'COM', 'NC'],
  'ds3231': ['GND', 'VCC', 'SDA', 'SCL'],
  'photoresistor-sensor': ['VCC', 'GND', 'DO', 'AO'],
  'pir-motion-sensor': ['VCC', 'OUT', 'GND'],
  // The three resistor ids below are the same placed part (wokwi-resistor, measured).
  resistor: ['1', '2'],
  'resistor-10k': ['1', '2'],
  'resistor-330': ['1', '2'],
  'resistor-4k7': ['1', '2'],
  // The analog and power parts. `1` is not a pin on any of these, which is what the
  // first version of this file got wrong: it defaulted every unrouted signal to "1".
  'opamp-lm358': ['IN+', 'IN-', 'OUT'],
  'mosfet-irf540': ['D', 'G', 'S'],
  'bjt-2n2222': ['C', 'B', 'E'],
  'ic-74hc14': ['1A', '1Y', '2A', '2Y', '3A', '3Y', 'GND', '4Y', '4A', '5Y', '5A', '6Y', '6A', 'VCC'],
  'tilt-switch': ['GND', 'VCC', 'OUT'],
  'cap-100n': ['1', '2'],
  // The card's data pins are DI/DO, not MOSI/MISO: the silkscreen names are the pin
  // names, and a wire to "MOSI" is drawn nowhere.
  'microsd-card': ['CD', 'DO', 'GND', 'SCK', 'VCC', 'DI', 'CS'],
  mpu6050: ['INT', 'AD0', 'XCL', 'XDA', 'SDA', 'SCL', 'GND', 'VCC'],
  'opto-pc817': ['AN', 'CAT', 'COL', 'EMIT'],
};

// A relay on this canvas is the module's *input* side, and the library models the bare
// winding: placed with its defaults it is a 5 V / 70 Ω coil, which asks 71 mA of a pin
// whose absolute maximum is 40 mA, and which a 3.3 V pin is at the edge of pulling in
// (the editor's own check calls anything under 60 % of the rating a relay that will
// never close). The modules on these boards have a driver on them, and what that driver
// presents to the pin is a logic-level coil; that is what is placed here, with the same
// values Velxio's own GPIO-driven relay examples use.
const COIL = { coil_voltage: 3.3, coil_resistance: 1000 };

const PART = { 'esp32': 'esp32-devkit-v1', 'esp32-s3': 'esp32-devkit-v1' };

// `dataPin` is the pin on the part the signal lands on. It is only optional for the
// two-terminal passives, whose pins really are 1 and 2: anywhere else the "1" default is
// a pin that does not exist, and `checkWires` below refuses to write the project.
// `vcc` powers the part, `tie`/`tiePin` is the far end of a two-terminal part,
// `scl`/`extra` are the second and further bus lines, and `props` is what the canvas
// part is configured as. `notDrawn` rows carry the market part that has no simulator
// model.
const SPEC = {
  light: {
    kind: 'esp32', dir: 'lighting', bundle: 'lighting.ino',
    rows: [
      { id: 'k1', define: 'PIN_RELAY_STREET', part: 'relay', at: [330, 120],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 5V 继电器模块（10 A 触点，场院周界接触器线圈侧）' },
      { id: 'k2', define: 'PIN_RELAY_HOUSE', part: 'relay', at: [330, 250],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 5V 继电器模块（门廊 / 厨房回路）' },
      { id: 'u1', define: 'PIN_DIM_BARN', part: 'opamp-lm358', at: [330, 380],
        dataPin: 'IN+', tie: 'GND', tiePin: 'IN-',
        real: 'LM358 输出级 + 0-10 V 调光接口（牛舍高棚驱动器控制端）' },
      { id: 'p1', define: 'PIN_PIR', part: 'pir-motion-sensor', at: [560, 120],
        dataPin: 'OUT', vcc: ['VIN', 'GND'],
        real: 'HC-SR501 人体红外模块（牛舍，高电平有效）' },
      { id: 'r1', define: 'PIN_LDR', part: 'photoresistor-sensor', at: [560, 250],
        dataPin: 'AO', vcc: ['3V3', 'GND'],
        real: 'GL5528 光敏电阻 + 分压（屋檐光照）' },
      { id: 't1', define: 'PIN_BALLAST', part: 'resistor-330', tie: 'GND', at: [560, 380],
        real: 'SCT-013-030 电流互感器 + 33Ω 采样电阻（灯头母线）' },
      { id: 'f1', define: 'PIN_FAULT', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [790, 120],
        real: '驱动器故障干接点（开 = 正常）' },
      { id: 'x1', define: 'PIN_RTC_SDA', part: 'ds3231', at: [790, 380], dataPin: 'SDA', vcc: ['3V3', 'GND'], scl: 'PIN_RTC_SCL',
        real: 'DS3231MZ + 24C32（时刻表在掉电后仍然成立）' },
      { define: 'PIN_WDT_FEED', notDrawn: 'SPV05-MG / TPS3823 外部看门狗（件库无对应真实件）' },
    ],
  },
  water: {
    kind: 'esp32', dir: 'water', bundle: 'water.ino',
    rows: [
      { id: 'q1', define: 'PIN_VALVE_BARN', part: 'mosfet-irf540', at: [330, 120],
        dataPin: 'G', tie: 'GND', tiePin: 'S',
        real: 'IRF540N + 1N4007 续流（牛舍 24 V 电磁阀，RY8201-02NC）' },
      { id: 'q2', define: 'PIN_VALVE_HOUSE', part: 'mosfet-irf540', at: [330, 250],
        dataPin: 'G', tie: 'GND', tiePin: 'S',
        real: 'IRF540N + 1N4007 续流（主屋 24 V 电磁阀）' },
      { id: 'k1', define: 'PIN_PUMP', part: 'relay', at: [330, 380],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 继电器驱动增压泵接触器（CJX2-0910 线圈侧）' },
      { id: 's1', define: 'PIN_FLOW_BARN', part: 'ic-74hc14', dataPin: '1A', vcc: ['3V3', 'GND'], at: [560, 120],
        real: 'YF-S201 水表（7.5 Hz/L）+ 10k 上拉 + 74HC14 施密特整形' },
      { id: 's2', define: 'PIN_FLOW_HOUSE', part: 'ic-74hc14', dataPin: '1A', vcc: ['3V3', 'GND'], at: [560, 250],
        real: 'YF-S201 水表（主屋支路）+ 上拉 + 74HC14 整形' },
      { id: 's3', define: 'PIN_PRESSURE', part: 'opamp-lm358', at: [560, 380],
        dataPin: 'IN+', tie: 'GND', tiePin: 'IN-',
        real: 'MPX-V500 / EPM-100 压力变送器 0.5-4.5 V，LM358 跟随后进 ADC' },
      { id: 's4', define: 'PIN_LEVEL', part: 'tilt-switch', at: [790, 120],
        dataPin: 'OUT', vcc: ['3V3', 'GND'],
        real: '电阻式浮子液位（投入式液位计 SV-300 同位）' },
      { id: 's5', define: 'PIN_TANK_HIGH', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [790, 380],
        real: '水箱上限位干接点（满停泵，不进溢流管）' },
      { id: 'l1', define: 'PIN_LED_STATUS', part: 'led', dataPin: 'A', tie: 'GND', tiePin: 'C', series: 'resistor-330', at: [1010, 120], real: '绿色 3mm 指示灯' },
      { id: 'x1', define: 'PIN_RTC_SDA', part: 'ds3231', at: [1010, 380], dataPin: 'SDA', vcc: ['3V3', 'GND'], scl: 'PIN_RTC_SCL',
        real: 'DS3231MZ + 24C32（掉电后定时计划仍然成立）' },
      { define: 'PIN_LEAK', notDrawn: '绳式漏水传感器（SV-300 / TT105 类，件库无对应真实件）' },
      { define: 'PIN_WDT_FEED', notDrawn: 'SPV05-MG / TPS3823 外部看门狗（件库无对应真实件）' },
    ],
  },
  power: {
    kind: 'esp32', dir: 'power', bundle: 'power.ino',
    rows: [
      // The ZMPT101B is a voltage transformer with its own divider, not an optocoupler;
      // the PC817 stands in for the module's isolation stage only. What the canvas can
      // honestly show is the isolated output side landing on the ADC pin with the
      // emitter grounded -- the LED side (AN/CAT) stays unwired, because the AC front
      // end that drives it is not a part this library has.
      { id: 'a1', define: 'PIN_VA', part: 'opto-pc817', at: [330, 120],
        dataPin: 'COL', tie: 'GND', tiePin: 'EMIT',
        real: 'ZMPT101B 电压互感器模块（A 相，内部 PC817 光耦隔离）' },
      { id: 'a2', define: 'PIN_VB', part: 'opto-pc817', at: [330, 250],
        dataPin: 'COL', tie: 'GND', tiePin: 'EMIT',
        real: 'ZMPT101B 电压互感器模块（B 相）' },
      { id: 'a3', define: 'PIN_VC', part: 'opto-pc817', at: [330, 380],
        dataPin: 'COL', tie: 'GND', tiePin: 'EMIT',
        real: 'ZMPT101B 电压互感器模块（C 相）' },
      { id: 'c1', define: 'PIN_IA', part: 'resistor-330', tie: 'GND', at: [560, 120],
        real: 'SCT-013-030 电流互感器 + 33Ω 采样（A 相，15 mV/A）' },
      { id: 'c2', define: 'PIN_IB', part: 'resistor-330', tie: 'GND', at: [560, 250], real: 'SCT-013-030 + 采样（B 相）' },
      { id: 'c3', define: 'PIN_IC', part: 'resistor-330', tie: 'GND', at: [560, 380], real: 'SCT-013-030 + 采样（C 相）' },
      { id: 'z1', define: 'PIN_ZC_A', part: 'opamp-lm358', at: [790, 120],
        dataPin: 'IN+', tie: 'GND', tiePin: 'IN-',
        real: 'ZMPT101B 板载 LM393 过零比较输出' },
      { id: 'b1', define: 'PIN_BRK_AUX', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [790, 250],
        real: '总闸辅助触点（干接点到地）' },
      { id: 'f1', define: 'PIN_PUMP_FB', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [790, 380],
        real: '接触器辅助触点（CJX2-0910 的 NO 侧，合闸证明）' },
      { id: 'k1', define: 'PIN_PUMP', part: 'relay', at: [1010, 120],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 继电器 → 水泵接触器线圈' },
      { id: 'k2', define: 'PIN_LIGHTS', part: 'relay', at: [1010, 250],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 继电器 → 场院照明馈线接触器线圈' },
      { id: 'i1', define: 'PIN_IN_PUMP', part: 'slide-switch', dataPin: '2', tie: 'GND', tiePin: '1', at: [1240, 120],
        real: '柜门本地许可开关（水泵馈线，高电平有效 + 下拉）' },
      { id: 'i2', define: 'PIN_IN_LIGHTS', part: 'slide-switch', dataPin: '2', tie: 'GND', tiePin: '1', at: [1240, 250],
        real: '柜门本地许可开关（照明馈线）' },
      { id: 'l1', define: 'PIN_LED_STATUS', part: 'led', dataPin: 'A', tie: 'GND', tiePin: 'C', series: 'resistor-330', at: [1240, 380], real: '绿色 3mm 指示灯' },
      { define: 'PIN_WDT_FEED', notDrawn: 'SPV05-MG / TPS3823 外部看门狗（件库无对应真实件）' },
    ],
  },
  fire: {
    kind: 'esp32', dir: 'fire', bundle: 'fire.ino',
    rows: [
      { id: 'l1', define: 'PIN_Z_HOUSE', part: 'resistor-4k7', tie: 'GND', at: [330, 120],
        real: '4k7 终端电阻（EOL）+ 主屋回路 JTY-GD-3200 探测器' },
      { id: 'l2', define: 'PIN_Z_BARN', part: 'resistor-4k7', tie: 'GND', at: [330, 250], real: '4k7 EOL + 牛舍回路探测器' },
      { id: 'l3', define: 'PIN_Z_STORE', part: 'resistor-4k7', tie: 'GND', at: [330, 380], real: '4k7 EOL + 仓库 / 饲料间回路' },
      { id: 'l4', define: 'PIN_Z_POWER', part: 'resistor-4k7', tie: 'GND', at: [560, 120], real: '4k7 EOL + 配电房回路（火最先来的地方）' },
      { id: 'm1', define: 'PIN_MCP', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [560, 250],
        real: 'J-SAP-M-51 破玻璃手动报警按钮' },
      { id: 'f1', define: 'PIN_FLOW', part: 'tilt-switch', at: [560, 380],
        dataPin: 'OUT', vcc: ['3V3', 'GND'],
        real: 'WFS-1 湿式喷淋水流开关' },
      { id: 'r1', define: 'PIN_SIREN', part: 'relay', at: [790, 120],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 继电器 → 室内声光报警器' },
      { id: 'r2', define: 'PIN_STROBE', part: 'relay', at: [790, 250],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 继电器 → 室外频闪警灯' },
      { id: 'r3', define: 'PIN_PUMP', part: 'relay', at: [790, 380],
        dataPin: 'COIL+', tie: 'GND', tiePin: 'COIL-', props: COIL,
        real: 'HF115F-003 继电器 → 喷淋泵控制柜许可干接点' },
      { id: 'b1', define: 'PIN_SIREN_FB', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [1010, 120],
        real: '铃臂继电器辅助触点（证明铃真的动了，不是只给了电）' },
      { id: 'k1', define: 'PIN_KEY_ARM', part: 'slide-switch', dataPin: '2', tie: 'GND', tiePin: '1', at: [1010, 250],
        real: '面板钥匙开关 ARM / BYPASS' },
      { id: 's1', define: 'PIN_BTN_SILENCE', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [1010, 380], real: '面板静音按钮（瞬时到地）' },
      { id: 's2', define: 'PIN_BTN_TEST', part: 'pushbutton', dataPin: '1.l', tie: 'GND', tiePin: '2.l', at: [1240, 120], real: '警铃自检按钮' },
      { id: 'l9', define: 'PIN_LED_STATUS', part: 'led', dataPin: 'A', tie: 'GND', tiePin: 'C', series: 'resistor-330', at: [1240, 380], real: '绿色 3mm 运行指示灯' },
      { define: 'PIN_WDT_FEED', notDrawn: 'SPV05-MG / TPS3823 外部看门狗（件库无对应真实件）' },
    ],
  },
  drone: {
    kind: 'esp32-s3', dir: 'drone', bundle: 'drone.ino',
    rows: [
      { id: 'g1', define: 'PIN_SAFE_OUT', part: 'bjt-2n2222', at: [330, 120],
        dataPin: 'B', tie: 'GND', tiePin: 'E',
        real: '2N2222 + 1N4148 驱动飞控 RCPIN 安全开关（高 = SAFE，锁死电机）' },
      { id: 'a1', define: 'PIN_ARM_IN', part: 'slide-switch', dataPin: '2', tie: 'GND', tiePin: '1', at: [1010, 120], real: '钥匙 ARM 开关（低 = 闭合：放到起飞按钮旁，先合上它再按起飞）' },
      { id: 'b1', define: 'PIN_BATT_ADC', part: 'resistor-10k', tie: 'GND', at: [330, 380],
        real: '10k/33k 分压（4.3:1，4S 电池组电压）' },
      { id: 'c1', define: 'PIN_CURR_ADC', part: 'cap-100n', tie: 'GND', at: [560, 120],
        real: 'ACS712-20A 电流模块（载荷母线，100 mV/A）+ 100n 滤波' },
      { id: 'd2', define: 'PIN_BUZZER', part: 'buzzer', tie: 'GND', at: [560, 250], real: '有源蜂鸣器（失联 / 低电）' },
      { id: 'l1', define: 'PIN_LED_STATUS', part: 'led', dataPin: 'A', tie: 'GND', tiePin: 'C', series: 'resistor-330', at: [560, 380], real: '绿色 3mm 状态灯' },
      { id: 's1', define: 'PIN_SD_CS', part: 'microsd-card', at: [790, 120],
        dataPin: 'CS', vcc: ['3V3', 'GND'],
        // The card's silkscreen pins: DI is the card's input (MOSI), DO its output (MISO).
        extra: { SCK: 'PIN_SD_SCK', DI: 'PIN_SD_MOSI', DO: 'PIN_SD_MISO' },
        real: 'TF / microSD 卡座（硬件 SPI，飞行日志）' },
      { id: 'i1', define: 'PIN_IMU_INT', part: 'mpu6050', at: [790, 250], optional: true,
        dataPin: 'INT', vcc: ['3V3', 'GND'],
        real: 'MPU-6050 六轴（备份姿态源）' },
      { id: 'g2', define: 'PIN_GNSS_TX', part: 'gps-neo6m', dataPin: 'RX', vcc: ['VIN', 'GND'], at: [790, 380], optional: true,
        real: 'NEO-6M GPS 模块（航线航点记录）' },
      { define: 'PIN_FC_TX', notDrawn: '飞控本体（Pixhawk 3X / ArduPilot，UART2 57600 MAVLink）——件库无对应真实件' },
      { define: 'PIN_DBG_TX', notDrawn: '第二路调试串口（43/44 = TX/RX），无外设' },
      { define: 'PIN_WDT_FEED', notDrawn: 'SPV05-MG / TPS3823 外部看门狗（件库无对应真实件）' },
      { define: 'CAM_PIN_XCLK', notDrawn: 'OV2640 摄像头模组（14 根 DVP 线，件库无对应真实件）' },
    ],
  },
  show: {
    kind: 'esp32-s3', dir: 'show', bundle: 'show.ino',
    rows: [
      { id: 'n1', define: 'PIN_LED_DATA', part: 'neopixel', dataPin: 'DIN', vcc: ['VIN', 'GND'], vccPins: ['VDD', 'VSS'], at: [330, 120],
        real: 'WS2812B ×2：臂灯 + 腹部信标（每颗满白 160 mA，BEC 预算写在 board.h）' },
      { id: 'a1', define: 'PIN_SAFE_IN', part: 'slide-switch', dataPin: '2', tie: 'GND', tiePin: '1', at: [330, 250],
        real: 'SAFE 钥匙开关（低 = 插入，电机锁死）' },
      { id: 'b1', define: 'PIN_BATT_ADC', part: 'resistor-10k', tie: 'GND', at: [330, 380],
        real: '10k/33k 分压（4.3:1，4S 电池组电压）' },
      { id: 'g2', define: 'PIN_RTK_TX', part: 'gps-neo6m', dataPin: 'RX', vcc: ['VIN', 'GND'], at: [790, 380],
        real: 'NEO-M9N RTK 模块（件库里只有 NEO-6M：同一族 UBX 协议，RTK 定解画不出来）' },
      { id: 'd2', define: 'PIN_BUZZER', part: 'buzzer', tie: 'GND', at: [560, 250],
        real: '有源蜂鸣器（失联 / 低电才响，节目自己收尾不响）' },
      { id: 'l1', define: 'PIN_LED_STATUS', part: 'led', dataPin: 'A', tie: 'GND', tiePin: 'C', series: 'resistor-330', at: [560, 380],
        real: '绿色 3mm 台架灯：一秒一闪 = 等节目，两闪 = 在飞，不亮 = SAFE 拔了' },
      { define: 'PIN_RTK_RX', notDrawn: 'RTK 模块回程 TX→RX 与 TIMEPULSE 秒脉冲（真实模块的第 4 脚）——画布上的 NEO-6M 件没有这两个脚位' },
      { define: 'PIN_RTK_PPS', notDrawn: '同上：把全机群对齐到同一个 GNSS 秒的那根线' },
      { define: 'PIN_FC_TX', notDrawn: '飞控本体（Pixhawk 3X / ArduPilot，UART 57600 MAVLink）——件库无对应真实件' },
      { define: 'PIN_WDT_FEED', notDrawn: 'SGM823 / TPS3813 外部看门狗（件库无对应真实件）' },
      { define: 'PIN_DBG_TX', notDrawn: 'USB-CDC 控制台（43/44 = TX/RX）：地面站的「上传节目」就是往这根串口敲一行 show=SHOWPLAN,…，画布上没有"插过来的一根 USB 线"可画' },
    ],
  },
};

function readDefines(dir) {
  const file = path.join(FW, dir, 'src', 'board.h');
  const text = fs.readFileSync(file, 'utf8');
  const out = new Map();
  for (const m of text.matchAll(/#define\s+((?:PIN|CAM_PIN)_\w+)\s+(-?\d+)/g)) out.set(m[1], Number(m[2]));
  return { map: out, file };
}

// The part dictionary the simulator itself serves (curl http://localhost:3080/
// components-metadata.json, vendored into reference/ so this build is offline and
// reproducible). A metadataId that is not in here does not fail loudly -- the editor
// just drops the part -- so it is checked before anything is drawn.
const KNOWN_PARTS = new Set(
  (fs.existsSync(META) ? JSON.parse(fs.readFileSync(META, 'utf8')).components : []).map(c => c.id));

// The last gate before anything is written: every wire end has to land on a pin the
// part really carries. This is deliberately done on the assembled wire list rather
// than row by row, because the bug it exists to catch was a row-by-row check that only
// looked at the pins a row *mentioned* -- so a wire the row never mentioned (the coil
// of a relay, a second data pin) went out unchecked and the editor answered it with
// "Pin 1 not found on component k2" on every load.
function checkWires(board, parts, wires, names) {
  const pins = new Map();
  for (const p of parts) {
    // The board's own pins come from PINS, measured off the placed board element; every
    // other part's come from PART_PINS, measured off the placed part.
    const known = p.id === 'esp32' ? names : PART_PINS[p.metadataId];
    if (known) pins.set(p.id, known);
    else problems.push(`${board}: ${p.id}（${p.metadataId}）的引脚没有量过，先把它量进 PART_PINS 再画`);
  }
  for (const w of wires) {
    for (const side of ['start', 'end']) {
      const ref = w[side];
      const known = pins.get(ref.componentId);
      if (!known) {
        problems.push(`${board}: ${w.id} 连到了不存在的元件 ${ref.componentId}`);
      } else if (!known.includes(ref.pinName)) {
        problems.push(`${board}: ${w.id} 连到 ${ref.componentId} 的引脚 ${ref.pinName}，` +
          `${ref.componentId} 实际只有 ${known.join('/')}`);
      }
    }
  }
}

const problems = [];
const notes = [];
const bom = [];
const projects = {};

for (const [board, spec] of Object.entries(SPEC)) {
  const { map: defs, file } = readDefines(spec.dir);
  const names = PINS[spec.kind];
  const alias = PIN_ALIAS[spec.kind];
  const parts = [{ id: 'esp32', metadataId: PART[spec.kind], x: 60, y: 60, properties: {} }];
  const wires = [];
  let n = 0;
  const skipped = [];

  for (const row of spec.rows) {
    const gpio = defs.get(row.define);
    if (gpio === undefined) {
      if (row.optional || row.notDrawn) continue;
      problems.push(`${board}: ${path.relative(ROOT, file)} 里没有 ${row.define}`);
      continue;
    }
    if (row.notDrawn) {
      skipped.push(`${row.define}=${gpio}：${row.notDrawn}`);
      bom.push([board, row.define, gpio, '—（画布上没画）', row.notDrawn]);
      continue;
    }
    const pinName = String(alias[gpio] ?? gpio);
    if (!names.includes(pinName)) {
      problems.push(`${board}: ${row.define}=${gpio}（引脚名 ${pinName}）不在 ${spec.kind} 的排针上，画布不连这一路`);
      continue;
    }
    if (!KNOWN_PARTS.has(row.part)) {
      problems.push(`${board}: 仿真件库里没有 ${row.part}（${row.define}），这一路不画`);
      skipped.push(`${row.define}=${gpio}：件库无 ${row.part}`);
      continue;
    }
    parts.push({ id: row.id, metadataId: row.part, x: row.at[0], y: row.at[1], properties: row.props || {} });
    wires.push({
      id: `w-${row.id}-${n++}`,
      start: { componentId: 'esp32', pinName },
      // Which of the part's own pins the signal lands on. There is no default: a part
      // whose signal pin is not named on the row would land on "1" whatever it has, and
      // an unknown pinName does not error -- the editor draws the wire to nowhere and
      // reports "Pin 1 not found" in the console on every load. `checkWires` at the end
      // of this file refuses to write a project whose wires do not land on real pins.
      end: { componentId: row.id, pinName: row.dataPin || '1' },
      waypoints: [], color: '#ff5555',
    });
    if (row.series) {
      // An LED straight across a GPIO and ground is a dead LED: the simulator says so in
      // amps. The resistor goes between the pin and the anode, so the canvas shows the
      // part the board.h harness actually has in that line. The resistor's own two pins
      // are 1 and 2; the far end still lands on the row's dataPin, which names a pin on
      // the part being fed.
      const rid = `${row.id}r`;
      parts.push({ id: rid, metadataId: row.series, x: row.at[0] + 115, y: row.at[1] + 95, properties: {} });
      wires[wires.length - 1].end = { componentId: rid, pinName: '1' };
      wires.push({ id: `w-${rid}-out`, start: { componentId: rid, pinName: '2' },
        end: { componentId: row.id, pinName: row.dataPin || '1' }, waypoints: [], color: '#ff5555' });
    }
    if (row.scl) {
      const s = defs.get(row.scl);
      const sName = s === undefined ? null : String(alias[s] ?? s);
      if (sName && names.includes(sName)) {
        wires.push({ id: `w-${row.id}-scl`, start: { componentId: 'esp32', pinName: sName },
          end: { componentId: row.id, pinName: 'SCL' }, waypoints: [], color: '#33b1f0' });
      } else {
        problems.push(`${board}: ${row.scl} 不在 ${spec.kind} 排针上，${row.id} 的时钟线没连`);
      }
    }
    for (const [pin, def] of Object.entries(row.extra || {})) {
      const g = defs.get(def);
      const nm = g === undefined ? null : String(alias[g] ?? g);
      if (nm && names.includes(nm)) {
        wires.push({ id: `w-${row.id}-${pin}`, start: { componentId: 'esp32', pinName: nm },
          end: { componentId: row.id, pinName: pin }, waypoints: [], color: '#f0a531' });
      } else if (g !== undefined) {
        problems.push(`${board}: ${def}=${g} 不在 ${spec.kind} 排针上，${row.id} 的 ${pin} 没连`);
      }
    }
    const tie = row.tie && (RAIL[spec.kind][row.tie] || row.tie);
    if (tie && !names.includes(tie)) {
      problems.push(`${board}: ${row.id} 的 ${row.tie} 端在 ${spec.kind} 上没有这个引脚名，这一端没连`);
    }
    if (row.tie && names.includes(tie)) {
      // The far end of a two-terminal passive: a burden or EOL resistor with one leg
      // in the air carries nothing, and the simulator's own circuit check says so.
      wires.push({ id: `w-${row.id}-tie`, start: { componentId: row.id, pinName: row.tiePin || '2' },
        end: { componentId: 'esp32', pinName: tie }, waypoints: [], color: '#000000' });
    }
    const vcc = row.vcc && [RAIL[spec.kind][row.vcc[0]] || row.vcc[0], RAIL[spec.kind][row.vcc[1]] || row.vcc[1]];
    const [vccPin, gndPin] = row.vccPins || ['VCC', 'GND'];
    if (vcc && names.includes(vcc[0]) && names.includes(vcc[1])) {
      wires.push({ id: `w-${row.id}-vcc`, start: { componentId: 'esp32', pinName: vcc[0] },
        end: { componentId: row.id, pinName: vccPin }, waypoints: [], color: '#e53935' });
      wires.push({ id: `w-${row.id}-gnd`, start: { componentId: 'esp32', pinName: vcc[1] },
        end: { componentId: row.id, pinName: gndPin }, waypoints: [], color: '#000000' });
    }
    bom.push([board, row.define, gpio, row.part, row.real]);
  }

  checkWires(board, parts, wires, names);

  projects[board] = {
    format: 'velxio-project', version: 1,
    boards: [{ id: 'esp32', boardKind: spec.kind, x: 60, y: 60, activeFileGroupId: 'group-esp32', languageMode: 'arduino' }],
    fileGroups: {},
    components: parts, wires, activeBoardId: 'esp32',
    // Not part of the project format: what the canvas could not draw, so the panel can
    // say so instead of showing a quieter circuit than the code.
    $kind: spec.kind,
    $skipped: skipped,
  };
  if (skipped.length) notes.push(`${board}: ${skipped.length} 路在画布上没有对应真实件（已列进 BOM，未画）`);
}

if (problems.length) {
  console.log('circuits: 问题如下');
  for (const p of problems) console.log('  ! ' + p);
  console.log(`circuits: ${problems.length} 条硬错误，没有写出任何工程（画布上的线必须落在真实引脚上）`);
  process.exit(1);
}
for (const n of notes) console.log('  · ' + n);
if (process.argv.includes('--check')) {
  console.log(`circuits --check: ${Object.keys(projects).length} 块板可生成，${notes.length} 条提示，0 条硬错误`);
  process.exit(0);
}

fs.mkdirSync(OUT, { recursive: true });
for (const [board, project] of Object.entries(projects)) {
  const bundle = fs.readFileSync(path.join(FW, 'build', SPEC[board].bundle), 'utf8');
  project.fileGroups['group-esp32'] = [{ name: 'sketch.ino', content: bundle }];
  const f = path.join(OUT, `${board}.json`);
  fs.writeFileSync(f, JSON.stringify(project));
  console.log(`circuits ${board.padEnd(7)} 元件 ${String(project.components.length).padStart(2)}  连线 ${String(project.wires.length).padStart(2)}  ` +
    `${(fs.statSync(f).size / 1024).toFixed(1)} KB  ${path.relative(ROOT, f)}`);
}
fs.writeFileSync(path.join(OUT, 'bom.md'),
  '# 五块板的元件清单（画布上的每一件都要能在市场上买到）\n\n' +
  '| 板子 | board.h 里的名字 | GPIO | 画布上的件 | 现场真实器件（市场型号） |\n|---|---|---|---|---|\n' +
  bom.map(r => `| ${r[0]} | ${r[1]} | ${r[2]} | ${r[3]} | ${r[4]} |`).join('\n') + '\n');
console.log(`circuits: BOM ${bom.length} 行 -> firmware/build/circuits/bom.md，${notes.length} 条提示，0 条硬错误`);
