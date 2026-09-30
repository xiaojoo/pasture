import { houseLightOn, streetLightOn } from '../state/time.js';
import { water, waterSystem } from '../world/water-tower.js';
import { powerGrid } from '../state/power.js';
/* =========================================================
   DRAWER DATA
========================================================= */


export const drawer =
document.getElementById(
    "drawer"
);

export const drawerTitle =
document.getElementById(
    "drawerTitle"
);

export const drawerSubtitle =
document.getElementById(
    "drawerSubtitle"
);

export const drawerBody =
document.getElementById(
    "drawerBody"
);


export const drawerData = {

board:{

title:"🎛 开发板",

subtitle:"ESP32 DEV BOARD",

html:`

<div id="espHost"></div>

`

},


drone:{

title:"🛸 无人机巡检",

subtitle:"DRONE INSPECTION",

html:`

<div id="droneHost"></div>

`

},


power:{

title:"⚡ 电路系统",

subtitle:"POWER MANAGEMENT",

html:`

<div class="section">

<div class="section-label">
实时电力
</div>

<div class="data-grid">

<div class="data-card">
<div class="data-label">总功率</div>
<div class="data-value" id="pwKw">
-- <small>kW</small>
</div>
</div>

<div class="data-card">
<div class="data-label">今日用电</div>
<div class="data-value" id="pwDay">
-- <small>kWh</small>
</div>
</div>

<div class="data-card">
<div class="data-label">A 相电压</div>
<div class="data-value" id="pwVolt">
-- <small>V</small>
</div>
</div>

<div class="data-card">
<div class="data-label">负载率</div>
<div class="data-value" id="pwLoad">
-- <small>%</small>
</div>
</div>

</div>
</div>


<div class="section">

<div class="section-label">
配电监测板
</div>

<div id="powerHost"></div>

</div>


<div class="section">

<div class="section-label">
照明控制
</div>

<div class="device">

<div class="device-icon">💡</div>

<div class="device-info">

<div class="device-name">
道路照明
</div>

<div class="device-desc">
16 盏路灯 · 夜间自动控制
</div>

</div>

<div
id="swStreet"
class="switch ${streetLightOn ? "on":""}"
onclick="toggleStreetLights()">
</div>

</div>


<div class="device">

<div class="device-icon">🏠</div>

<div class="device-info">

<div class="device-name">
建筑照明
</div>

<div class="device-desc">
主屋 / 牛舍 / 仓库
</div>

</div>

<div
id="swHouse"
class="switch ${houseLightOn ? "on":""}"
onclick="toggleHouseLights()">
</div>

</div>

</div>


<div class="section">

<div class="section-label">
配电设备
</div>

<div class="device">

<div class="device-icon">⚡</div>

<div class="device-info">

<div class="device-name">
1号主配电柜
</div>

<div class="device-desc">
牧场主配电中心
</div>

</div>

<div class="device-status" id="pwCabinet">
正常
</div>

</div>


<div class="device">

<div class="device-icon">◉</div>

<div class="device-info">

<div class="device-name">
主线路
</div>

<div class="device-desc" id="pwFeederDesc">
负载 --% · 温度 ${powerGrid.tempC ? powerGrid.tempC.toFixed(0) : 34}°C
</div>

</div>

<div class="device-status" id="pwFeeder">
正常
</div>

</div>

</div>

`

},


water:{

title:"💧 水利系统",

subtitle:"WATER MANAGEMENT",

html:`

<div class="section">

<div class="section-label">
水利实时状态
</div>

<div class="data-grid">

<div class="data-card">
<div class="data-label">当前水压</div>
<div class="data-value">
${waterSystem.pressure}
<small>MPa</small>
</div>
</div>

<div class="data-card">
<div class="data-label">水塔水位</div>
<div class="data-value">
${waterSystem.level}
<small>%</small>
</div>
</div>

<div class="data-card">
<div class="data-label">今日用水</div>
<div class="data-value">
18.6 <small>m³</small>
</div>
</div>

<div class="data-card">
<div class="data-label">管网状态</div>
<div class="data-value green">
正常
</div>
</div>

</div>
</div>


<div class="section">

<div class="section-label">
水泵控制
</div>

<div class="device">

<div class="device-icon">
💧
</div>

<div class="device-info">

<div class="device-name">
1号供水泵
</div>

<div class="device-desc">
当前频率 42 Hz
</div>

</div>

<div
class="switch ${waterSystem.pumpOn ? "on":""}"
onclick="
waterSystem.pumpOn=!waterSystem.pumpOn;
this.classList.toggle('on');
updateWaterVisual();
">
</div>

</div>


<div class="device">

<div class="device-icon">
◉
</div>

<div class="device-info">

<div class="device-name">
东区供水阀门
</div>

<div class="device-desc">
自动调节
</div>

</div>

<div
class="switch ${waterSystem.valveOn ? "on":""}"
onclick="
waterSystem.valveOn=!waterSystem.valveOn;
this.classList.toggle('on');
updateWaterVisual();
">
</div>

</div>

</div>


<div class="section">

<div class="section-label">
管网监测
</div>

<div class="device">

<div class="device-icon">
≋
</div>

<div class="device-info">

<div class="device-name">
东区管网
</div>

<div class="device-desc">
压力 0.42 MPa
</div>

</div>

<div class="device-status">
正常
</div>

</div>


<div class="device">

<div class="device-icon">
≋
</div>

<div class="device-info">

<div class="device-name">
西区管网
</div>

<div class="device-desc">
压力 0.39 MPa
</div>

</div>

<div class="device-status">
正常
</div>

</div>

</div>


<div id="waterHost"></div>

`

},


fire:{

title:"🔥 消防系统",

subtitle:"FIRE SAFETY",

html:`

<div class="section">

<div class="section-label">
消防实时状态
</div>

<div class="data-grid">

<div class="data-card">
<div class="data-label">回路健康</div>
<div class="data-value" id="fwHealth">
- <small>/4 回路</small>
</div>
</div>

<div class="data-card">
<div class="data-label">当前报警</div>
<div class="data-value" id="fwAlarms">
- <small>路</small>
</div>
</div>

<div class="data-card">
<div class="data-label">喷淋泵许可</div>
<div class="data-value" id="fwPump">
-
</div>
</div>

<div class="data-card">
<div class="data-label">消防水池</div>
<div class="data-value" id="fwTank">
${waterSystem.level} <small>%</small>
</div>
</div>

</div>
</div>


<div class="section">

<div class="section-label">
消防监测板
</div>

<div id="fireHost"></div>

</div>


<div class="section">

<div class="section-label">
消防设备
</div>

<div class="device">

<div class="device-icon">
🔥
</div>

<div class="device-info">

<div class="device-name">
消防泵
</div>

<div class="device-desc">
自动待机
</div>

</div>

<div class="device-status" id="fwPumpRow">
正常
</div>

</div>


<div class="device">

<div class="device-icon">
🚿
</div>

<div class="device-info">

<div class="device-name">
自动喷淋
</div>

<div class="device-desc" id="fwSprinklerDesc">
全园区覆盖
</div>

</div>

<div class="device-status" id="fwSprinkler">
未接管
</div>

</div>


<div class="device">

<div class="device-icon">
◉
</div>

<div class="device-info">

<div class="device-name">
烟雾探测器
</div>

<div class="device-desc">
24 个探测器在线
</div>

</div>

<div class="device-status" id="fwLoopRow">
正常
</div>

</div>


<div class="device">

<div class="device-icon">
🚨
</div>

<div class="device-info">

<div class="device-name">
消防报警
</div>

<div class="device-desc">
当前没有报警事件
</div>

</div>

<div class="device-status" id="fwAlarmRow">
正常
</div>

</div>

</div>

`

}

};
