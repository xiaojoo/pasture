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
| 共享逻辑单测 | `.probe/cc.sh` | **556 项 0 失败**（2026-10-01 重跑），含 `board.h` 引脚断言 |
| 无人机整机沙箱 | `.probe/sim.sh` | **88 项 0 失败**（2026-10-01；含"安全开关置位"反证、9 项摇杆判据、4 项指令回执判据、22 项"取消自动巡航"判据） |
| 水利整机沙箱 | `.probe/water.sh` | **41 项 0 失败**（含漏水、干涸、无时钟三个反证 + 下行门与定值读回） |
| 照明整机沙箱 | `.probe/light.sh` | **20 项 0 失败**（含灯坏、无时钟两个反证 + 下行门与拒因） |
| 配电整机沙箱 | `.probe/power.sh` | **76 项 0 失败**（含下行门、九个定值读回、超范围不退值） |
| 消防整机沙箱 | `.probe/fire.sh` | **63 项 0 失败**（含下行门与计时读回） |
| 两份编排核心逐行比对 | `.probe/cross.sh` + `node .probe/cross.mjs` | C++/JS 输出 `diff` 为空（2026-10-01，含 6 个形状 × 7 种机数的采样、夹点与 `ck=`） |
| 六个 bundle 编译并跑 | `.probe/bundle.sh` | 全 `build=ok run=0`：drone 31 199 / water 60 364 / lighting 75 004 / power 62 999 / fire 30 000 / show 31 199 帧 |

**心形这一天改了一次采样**：等参数步长在尖点和凹口处把两架机挤到 0.59 m（规则 2 m），
所以"24 架跑三幕会被拒"其实是**一个幕自己的内部间距**，不是过渡夹点。改成等弧长
（720 步表，两份实现同一顺序、同一算式）后同一幕 3.52 m，默认三幕直接过；
`n=49 / 100` 仍然会被拒——固定尺寸的轮廓放不下那么多机，这是真的该拒。

**圆环超过 28 架时间距不均**（2026-10-01 修，`.probe/shapegaps.mjs` 量的最近邻）：分了 3 圈却
**每圈放同样架数**，于是内圈挤、外圈疏。49 架 / 尺寸 18 m 量到内圈 **2.81 m**、外圈 **6.07 m**
（max/min **2.16**），36 架 1.45、64 架 2.39、100 架 2.43 —— 这就是「间隙不对」：一圈有一圈的
节距才叫圆环。改成**按周长分架数**（第 j 圈权重 j）并把圈数由架数定
（`k = round((sqrt(1+4n/π)-1)/2)`，n≤28 仍是单圈不动），同一组数出来的间距变成
49 架 **4.51 / 4.68（1.04）**、36 架 1.04、64 架 1.04、100 架 1.12。两份实现同一算式，
`.probe/cross.sh` + `node .probe/cross.mjs` 的 diff 仍然为空（425 行对 425 行）。

**改了机数，画面不跟着改**（同一天修）：左边那排数字（机数 / 最小间距 / 最大速度 / 围栏 /
返航高度 / 电量 / 风）的 `input` 只置了 `dirty` 再 `refresh()`，而 `refresh()→judge()`
**只重算判定、不 `showLoad()`** —— 量到把机数填成 49 之后，场景里仍是 **24 架**，
底部那条「最紧的一对 3.36 m」也是上一版加载留下的数。改成走 `commit()`，实测
24 / 36 / 49 / 100 四档画面上分别是 24 / 36 / 49 / 100 架，交叉对全为 **0**
（机身画宽 ÷ 最近邻 = 0.40 / 0.33 / 0.27 / 0.21，放大上限由最紧一对夹着）。

**一幕节目把机身放到 ×16、互相穿过去**（2026-10-01 修）：`showWorstPair` 只量"相邻两幕之间的
过渡"，一幕节目没有相邻幕，它返回 `1e30` → `tightest` 被当成 0 → `autoScale()` 在**量不出来**
的时候返回的是 `AUTO_MAX`。49 架 / 站间距 4.51 m 的一圈于是画成每架 **10.73 m**，
**217 对**机身互相穿过（3 m 小圈那一版是 1176 对）—— 这就是「间隙不对」的第二条根因。
三处一起改：新增 `stationGap()` 量**每一幕自己的站间距**（不再依赖有没有第二幕）；
"量不出来"退回 **1×** 而不是 16×；± 旋钮的上限改成 `tightest / 翼展`（一架机身刚好等于
隔壁那道空隙）。量到：一幕 49 架 ×16 → **×3.7**、交叉 217 → **0**；± 问 2/4/8/16/99
一律给 **1.1**（那一版的上限）。反证只破一边：把 station 那一项去掉 → 一幕回到 ×16、
交叉 217，连 ± 的上限也一起塌回 16（`ceiling=16`）。

**编排台：先改成右侧竖栏，又按他的要求改回底部 —— 但取景不再让高度**（2026-10-02）。
他先点名「这个界面也不对，太小了」，量出来底栏占 900 px 高的 **52%**（468 px），而旧 `showFrame()`
把整场**按台子上方那条带**取景，24 架只投影成 **251×73 px**。中途真做成过一版右侧竖栏
（同一版 744×264 px），他看完说还是要回底部、但不许占主屏高度。回到底部后有两台相机搬法都试过：

- 按带取景（旧）：**251×73 px**；
- 相机整体往下搬 16 m 把画面抬上去：宽度回来了，**pxH 从 264 塌到 95** —— 相机变成从下往上看，
  平放的圆环被看成侧视，队形反而更不像。

最后用**镜头平移** `camera.setViewOffset(W, h, 0, 290, W, h)`：相机姿态一点不动，只把视锥窗口往下搬。
量到**开台 694×301 px、关台 694×301 px（比值 1.00）**，机群整团在 y 76..306、台面顶在 423，
也就是台子只让画面挪位置、不让它缩小。判据 `.probe/dockview.mjs`（6 项 0 失败，含"开台不被压小"
和"整团在台子上方"两条能各自变红的数）。

**量具自己搞错的一条（值得记）**：`.probe/frame.mjs` 把 NDC 的 y 用 `(y+1)/2` 映射成屏幕 y
（应当是 `(1-y)/2`），于是把机群 y 76..306 报成 **594..824**，"机群藏在台子后面"是这个镜像
造成的假故障——我据此差点又去改产品代码。修完两台仪器对同一帧读数一致（x 448..1007、y 76..306）。

**开台那一刻取景是错的**：`showFrame()` 在搭建过程中调用时，相机矩阵还是上一帧的——量到机群
中心落在 **x −71**（画面外），0.9 s、2.9 s 后仍是 −71，事后再调一次才落到 **x 527**。改成建完台
再取景并补一帧（`reframeSoon()`，`requestAnimationFrame` 里再调一次）。

**关掉编排台不再清空机群**：原来 `toggleShowPage(false)` 会 `showDispose()`，实测场景里
traverse 命中 **0** 架 —— 关台正是为了看机群，却把要看的东西删掉了。改成停钟 + 重新取景，
量到关台后仍是 24 架、694×301 px、居中整窗（y 314..544）。

**台子里的控件不许跑到栏外**：整块面板自己滚的时候，主按钮那排被推到可视区外——实测
起飞 / 停止 / 上传节目 / 近看机身 / 对准机群 落在 **y 900..1049**（面板只到 900，还得滚 165 px
才够得着）。改成面板不滚、表单列自己滚、脚排钉住；窄栏那一版还撞出过 ↑↓× 出界和标签折成
一字一行，一并留了 `white-space:nowrap` 与 `flex-wrap`。量到脚排在栏内、越界控件 **0** 个。
量具：`.probe/dockview.mjs`（开台/关台取景 + 越界）、`.probe/frame.mjs`（同帧两法对照）、
`.probe/oneact.mjs`（放大与交叉 + 反证）、`.probe/deskfoot.mjs`（脚排可达性）。

**「＋ 新增一幕」照抄上一幕的走位**（2026-10-02 修）：`flyableAct()` 搜的是形状/高度/尺寸，
时间两栏写死 `hold: last.hold, move: last.move` —— 上一幕把走位改成 14，加进来的那一幕就是 14，
而这个数属于另一副队形。改成 `moveSeconds()` 按**这一版最远那架机的路程**算，用的就是判定速度
那条规则的同一个式子（smoothstep 峰值是均速的 1.5 倍，`1.5 × 距离 / (maxSpeed − wind)`），
向上取整夹在 1–60。量到：上一幕走位 14 → 新幕 **11**（心形 / 尺寸 25 / 高度 40），判定仍「可以飞」，
一次点击 **23 ms**（180 个候选，每个多一次采样 + 分配）。反证只破一边：改回 `move: last.move`
→ 只有「走位不是照抄上一幕」红，量回 14。

**输入框打一个字光标就没了**（同一天修）：`actRow` 里数字框的 `input` 走 `commit()`，而
`commit()→refresh()` 会 `els.acts.textContent=''` 把**这一行连你正在敲的那个框一起重建**，
光标随之丢掉，第二个数字打进空气。拆成两条路：`input` → `liveEdit()`（判定 / 时间轴 / 机群跟着走，
但**不重建行**），`change`（回车或离开输入框）→ `commit()` 交出行；颜色选择器同理（拖动时
`input` 连发，重建会把取色器关掉）。量到 `hadFocus=true, afterInput=true`（打字后光标还在）、
`afterCommit=false`（回车才交出来）。反证只破一边：`input` 改回 `commit()` → 只有
「光标在打字后还在」红，量到 `afterInput=false`。

**修这两条时撞出来的第三条**：`showAssign(from, to, n, order)` 的 `order` 是**调用方持有的数组**
（和 C++ 侧传栈缓冲同一契约），按三参数调它 → `order[i] = at` 抛异常 → 异常在 click 处理器里
**被浏览器吞掉**：按钮看着按了、`ms=1`、幕数不变、吐司也没有。教训：**驱动按钮做判据时要在页面上
挂 `window.addEventListener('error')` 并把计数读回来**，否则"没反应"会被当成"走错了分支"来查。
`.probe/actedit.mjs`（7 项 0 失败）现在带 `pageErrors` 这一项。回归：`addact / timeline /
focustoggle / frame / sideview / mess` 全 pass，`cc.sh` 556/0、`show.sh` 129/0、`cross` diff 空
（425 行）、54 个页面模块 `--check` 0 失败。

**「独立窗口」改成把编排台搬过去，不再多开一份 app**（2026-10-02）。他要的是：点独立窗口之后
底部那块台子**没了**，改参数**实时生效**。原来那颗钮做的是 `/?show=1` —— 同一套页面再开一份，
自己拿一份 `plan`、自己起一片机群，而且台子一开就 `runPreset('show')`，于是**第二块仿真板**
跟着起来抢同一条下行串口（README 前面量过：两块板都开着时，往一栏串口敲的一行被另一块答）。

现在分两个角色：**主屏**（机群、板子、仿真器）和**编排台**（这一张表单）。接线的是一根
`BroadcastChannel`（`js/show/sync.js`）：频道名用主屏自己生成的随机 id，主屏把它盖进弹窗 URL
的 `g=`；台子每次改动就把整份 `plan + 电量 + 风` 推过去，主屏每次上报自己够不着的那几个数
（节目钟、近看第几架、机身画多大、板子开着没、上传回了什么话），起飞 / 停止 / 上传 / 近看 /
对准 / ± / 拖动预览 都当作命令走。**只有带 `g=` 的才是接了主屏的台子**；手敲 `/?show=1` 仍是
过去那份自给自足的窗口（自己起板、自己片机群），这条留着手测老行为。

撞到的第一条是**频道名和 id 对不上**：`showSyncGroup()` 返回一个值、`BroadcastChannel` 名字用
另一个值，两边都"注册成功"、一条消息也不通 —— 症状是台子上写「机身显示：还没连上主屏」而
改什么都没反应。教训：**跨文档那条链路要先量"通不通"，再量"对不对"**，判据里必须有一条
"两边的同一个数相等"，不然接线没接上时表单本身照样自洽。

量具 `.probe/sololink.mjs`（**29 项 0 失败**，两扇真窗口，自己开两条 CDP 连接，因为整条主张
活在两扇之间）：

| 量什么 | 量到 |
| --- | --- |
| 搬走之后主屏底部 | `.show-page` 命中 false，`--show-dock` **477px → 0px**，机群仍是 **24 架**（没被顺手清掉） |
| 编排台在独立窗口 | 占满 **1440×900**；自己没有机群（`craft=0`）、**0 个仿真 iframe、boardUp 全 false** |
| 打一个字生效 | 机数 24 → 49，主屏 `showState().craft` 跟上：**118 ms**；只改一格尺寸 18→60，主屏队形半径 **25.5 m → 60 m：131 ms** |
| 按钮送过去 | 近看机身 focus **−1 → 0**；`+` **×2.0 → ×3.0**（台子那格把主屏的数读回来）；起飞后主屏钟 **0.62 → 2.64 s**，台子的节目钟格读到 **3.0 / 50 s**；停止 false；拖动预览 **t → 5.0 s**；对准机群 focus **0 → −1**；恢复默认节目 → 主屏回 **24 架 / 50 s** |
| 判定跨窗口一致 | 改完是「不能飞」时按起飞，主屏 `flying=false`（没有"这边绿、那边动"） |
| 两条回来的路 | 台子的「收回底部」：独立窗口关掉、dock 回 **477px**；左边「编队表演」那一格：同样关掉弹窗、台子回底部 |
| 反证（只破一边） | 把 URL 里的 `g=` 拿掉 → 那扇窗口自己 **24 → 49 架**、主屏仍是 **24 架**，且它仍然自己起一块仿真板（`show=true`，和主屏那块同时开着） |
| 两个窗口 | `pageErrors` 都是空 |

顺带量到一条**不属于本功能**的旧缺陷，先报数没动它：仿真台那块 iframe 压在左栏上面 —— 台子
矩形 **[17, 453, 658, 430]**，「编队表演」那一格 **[29, 615, 237, 54]**，`elementFromPoint` 在
格子中心给出 `IFRAME.esp-dock-frame`，也就是**仿真台没收起时那一格点不动**（量具先把仿真台
「收起」再点，收起后 `elementFromPoint` 命中的就是那一格自己）。

还有一条是量具自己的现实：**无头浏览器只有一块屏**，两扇窗口不能都在前台，而节目钟和仿真编译
都是吃帧的 —— 第一轮量到 `flying=true` 而 `t` 一直是 **0**，`Page.bringToFront` 之后钟才走。
两台显示器都可见的真实部署不受这条影响，但判据里每次都把 `document.visibilityState` 回读出来，
免得下一次又把"没帧"当成"没接线"。

本轮没改固件，`cc.sh / show.sh / cross` 未重跑；页面模块 `--check` 现在 **55 个 0 失败**，
`addact / actedit / dockview / focustoggle / timeline / oneact / deskfoot / mess` 回归全 pass。

**镜头那一行菜单搬到顶栏**（2026-10-02，他点名「这个菜单改到顶部」）。`.bottom` 从
`bottom:22px` 改成 `top:20px`，仍水平居中 —— 顶栏 82 px 高、brand 在 **y 20..62**，行是
**y 20..64**，同一顶线。行宽 333 px，brand 右边界 **276**，状态栏左边界 = **宽 − 431**，
所以居中不碰两边的最小宽度是 **1195 px**（1100 那一档实测两边压住 48 px，一颗状态栏药丸
`elementFromPoint` 已经不是它自己）。于是 `@media(max-width:1240px)` 让行退回底部，并保留
原来"让开编排台"的那条抬升；窄屏要它待在顶栏就得先动 brand / 状态栏，那是另一件事。

搬走顺带修掉一条旧的：底部那行原来在 1440×900 会被仿真台压住。反证只破一边 —— 同一把尺子，
把行按回底部 → **⌖ 总览 / 🏠 主屋 两颗 4/4 个点落在 `IFRAME.esp-dock-frame` 上**；挪回顶栏
→ **0 颗**。量具 `.probe/menutop.mjs`：**7 档宽度（1440 / 1280 / 1240 / 1100 / 900 / 650 /
420）× 开台不开台全过** —— 行不进顶栏内容、行颗颗可点、行不挡别的控件、开台不压小画面
（pxH 开台不开台同一个数）。

取景跟着改了一条：`showFrame()` 现在把画面居中在**"行底 .. 台子顶"**这条带子里（带子的上沿
读的是那一行的渲染盒，不是样式表），1440×900 开台时机群从 **y 61..363** 变成 **y 93..395**，
投影仍是 **694×301 px**（按窗口定尺、只挪位置那条规矩没动）。踩到的坑值得记：
`getComputedStyle(row).top` 对 `position:absolute` 且 `top:auto` 的元素**返回用后值（"734px"）
而不是 `auto`**，靠它判"在不在顶栏"会在 1240 那一档把底部那行当成天花板，画面被推到 **y
656..922**（出画）。改成量渲染盒 `rowBox.top <= 40` 才对。

两条**报数没动**的旧缺陷：① 仿真台（`left:16 bottom:16`，660×446，z-index 25）在 1440×900
不开编排台的时候也盖住左栏 6 颗子系统按钮里的 **5 颗**（`IFRAME.esp-dock-frame 4/4`），
上面那条"编队表演那一格点不动"是同一件事；② 窗口矮而编排台高时可画的带子装不下画面 ——
1100×800 台子 579 px / 带子 221 px / 画面 267 px，900×700 是 510 / 190 / 232，650×900 是
710 / 190 / 202，于是机群上下都出画（`top −22`）。这是"按窗口定尺、只挪位置"的既有代价，
要治得先决定牺牲尺寸还是牺牲台子高度。



**没验证到的**：`hal_esp32.cpp` 与 `*_task` 的 FreeRTOS/看门狗调用不在上面任何一条里——
它们被 `#if !defined(RANCH_SIM)` 排除在仿真与主机编译之外，而 `pio run -e esp32s3-drone`
目前下不动 esp32 core（GitHub release HEAD 超时）。所以**真机 flash/RAM 数字还没有**，
各板 README 里给的是仿真 HAL 产物的体积，不是出货固件的。

**手抄的 MAVLink 表与 ArduPilot 现用方言有 7 处对不上**（2026-10-01 用 `pymavlink 2.4.50`
生成方言逐条比过，见 `drone/README.md`）：`COMMAND_LONG` id 17 vs 76、`MISSION_COUNT` 49 vs 44、
`RADIO_STATUS` 185 vs 109，以及 `COMMAND_ACK / MISSION_ITEM_INT / MISSION_REQUEST_INT /
BATTERY_STATUS` 的 crc_extra。仿真 FC 与固件读同一张表，所以上面每一条门禁都量不出这个错——
**接真 FC 之前整表要重核**，别把"沙箱里 CRC 全过"当成协议对齐。



## 五块板都有的下行门，和它的两个回执字段

浏览器里的仿真没有 MQTT。所以每块板把**串口的另一端**当地面站：`consoleCommandPump()`
读 `Serial`，按 `
` 切成一行，交给和 broker 同一个 `onGroundCommand`——页面能敲进去的
动词和硬件能收到的动词因此不可能各说一套。真机上这条门也在（USB-CDC），但正式的下行是
MQTT `ranch/<板>/cmd`。

每块板的帧里因此多了两个字段：

* `ack=ok:<动词>[=<值>]` / `ack=no:...` —— 这块板对最后一行指令的判定，保留 6 秒。
  没有它，"板子做了" 和 "页面自己画了画" 在界面上长得一样。
* `lim=名字数值/名字数值/...`（water / power / fire）—— 这块板**正在存着**的定值与计时。
  名字和数值绑在同一段里，因为一张靠位置对齐的表可以被下一个人悄悄重排。

页面侧对应关系收在 `ranch/js/ui/cmd.js` 的一张表里：抽屉的按钮和手机的指令表都从那里发，
所以"这条线在固件里叫什么"只有一个答案。

## 浏览器这一腿量到了什么（2026-10-01，真渲染）

上面所有门禁都是主机侧的 C++。浏览器里那一半以前只能"没量过就交底"，现在用无头 Chrome
（`--headless=new`，CDP 真鼠标事件，见 `.probe/cdp.mjs`）跑通了：**一次开一块板**，
把抽屉里每颗按钮都按一遍，读板子下一帧的 `ack=`：

| 板 | 按的钮 | 板子回话 | 用时 |
|---|---|---|---|
| 照明 | 光控自动 / 解除闭锁 / 对时 | `ok:auto` / `ok:clear` / `ok:time=2026-10-01T03:59:35` | 0.6–1.1 s |
| 照明 | 道路照明、舍内照明两个开关（在配电抽屉里） | `ok:street=0` `ok:street=1` `ok:house=0` `ok:house=1` | 0.5–0.8 s |
| 照明 | 不认识的动词 | `no:nonesuch=1` | 0.6 s |
| 水利 | 牛舍定时 / 主屋常开 / 立即放水 / 周期 / 时长 | `ok:stop` `ok:house=1` `ok:barn=next` `ok:period=5400` `ok:run=360` | 0.2–0.6 s |
| 配电 | 7 个动作 + 欠压定值 | `ok:pump=1/0` `ok:lit=1` `ok:reclose` `ok:stop` `ok:resume` `ok:uv=0.87` | 0.2–0.6 s |
| 消防 | 静音 / 复位 / 试验（都要二次确认）+ 确认时长 | `ok:silence` `ok:reset` `ok:test` `ok:confirm=25` | 0.4–0.6 s |
| 无人机 | **自动巡检一颗按钮两态**：地上按=起飞并飞航线，空中再按=取消，第三按=接回自动 | `ok:patrol` → `ok:hold`（`mode=LOITER`）→ `ok:patrol`（`mode=CLIMB`）；返航/降落另算 | 0.2–0.4 s 到帧 |
| 无人机 | 取消之后按住方方键 | 帧里 `ovr=1`、`mode=HOLD`，松开 `ovr=0` 并**停在 LOITER**（不弹回航线）；取消 20 秒后 `wp`、`shots` 一格没动 | 每 150 ms 一行 |
| 无人机 | 手机走 `/api/cmd`：`hover` / `axes` 连发 / `patrol` | 同一架机：`LOITER` → `HOLD`+`ovr=1` → `CLIMB`，页面上那两颗按钮不必碰 | 250 ms 一轮询 |

**同时开两块以上仿真时，串口下行不认板子**：实测把 `street=1` 打进照明板自己的控制台，
照明板的记录一个字符都没长（570 → 570），水利板的记录里却出现了这一行并回话
`no:street=1`；点页签把照明板切到前台也改不回来。这是 Velxio 那层的性质，不是固件的——
固件按自己那一路 UART 收，收到什么答什么。页面现在会把它说出来：如果**别的板**在我发出
这一条之后开始回同一个动词，回执那行写「照明板没收到这条 auto：是水利板接走的」，
而不是含糊成「6 秒内没回话」。判据与文案在 `js/ui/cmd.js`（`sent.seen` 快照 + `sent.stolen`）。

**首次打开页面时，全站所有按钮都报「板子没理我」**（2026-10-01 修）：`js/init.js` 第 29 行
`core/animation.js` 的顶层就同步调了一次 `animate()`，而 `animate()` 第一帧走到
`show/fleet.js` 的 `showUpdate()`，那里 `showDuration(plan)` 没防 `plan === null`——没有存过
编排节目的人就是 null。抛出的位置在 `requestAnimationFrame` 之后一行，所以循环还在转但永远
走不到 `render`，更糟的是 `init.js` 从那一刻起没跑完：**排在它后面的
`state/board-link.js`（下行通道的安装处）和 `state/phone-link.js` 一行都没执行**。
量具上就是 `typeof window.ranchSend === "undefined"`，界面上就是每颗按钮「板子 8 秒内没有认
「takeoff」：板子还在 GROUND」。修法是那一行加 `plan &&`；判据改成两条能各自变红的数：
`typeof window.ranchSend`，和一次真按「自动巡检」拿到 `ack=ok:patrol`。
清 profile（新 `--user-data-dir`）复现与复测都跑过：修前 `ranchSend=undefined` 且
`import('/js/init.js')` 抛 `Cannot read properties of null (reading 'acts')`，栈顶三帧是
`showDuration ← showUpdate ← animate`；修后 `ranchSend=function`，无人机板 4 535 ms 进运行，`.probe/patrol2.mjs` 18 项 0 失败。

所以给人用的口径是：**要看下行，就只留一块仿真实例**（其余的在开发板抽屉里点掉页签）。
上行（帧、遥测、五块板同时在跑）不受这条限制。

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
