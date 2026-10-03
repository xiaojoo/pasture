// The plan of the place, in metres, in one file.
//
// Everything the ranch is made of used to be placed by eye-balling a coordinate in whichever
// module happened to draw it, which is why the road, the fence and the herd never quite agreed
// on where the yard was. This file is the agreement: one grid, one carriageway width, and every
// other dimension expressed as a multiple of it.
//
// THE GRID. 横平竖直，只有两种东西：一条南北大道（axis 'z'）和三条东西横道（axis 'x'）。
// 没有弧线、没有斜穿：原来那条从横道拐进院子的 CatmullRom 弧已经删了，因为它让"农场路网"
// 变成了"随便踩出来的小径"。所有路都是矩形带，交点处按 Y 阶梯分层，谁高谁低写死在这里。
//
// THE LINES. 住宅和厂区各自排成一条直线，而且和大门那条线（z = FENCE_Z，东西向）平行：
// 大门在 z=58 横着，住宅线在 z=-82 横着，厂区线在 z=32 横着。原来房子贴着屋后树行两侧
// 一岸三户、水塔配电农机各占一角，转一圈看就是"到处都是"。
//
// The multiples are measured, not chosen. The reference is an aerial of a fazenda laid out on a
// mirror-symmetric spine, and against that track the house facade measured 4.5 carriageway
// widths at the reference's own 6.5 m road. The render road is now 9 m wide because at 6.5 m the
// avenue read as a footpath from the overview, so HOUSE_W is written as the measured 29.25 m
// rather than as a multiple of TRACK_W -- widening the road must not silently inflate the house.
//
// Coordinates: +Z is toward the visitor, so the gate is at large +Z and the rear avenue runs to
// -Z. Screen-right from the gate is +X.
export const TRACK_W = 9.0;          // 车行道（原 6.5，从总览看像人行小径）
export const SHOULDER_W = 2.6;       // 两侧磨光的草带
export const GATE_OPEN = TRACK_W * 1.3;
export const GATE_Z = 52;
export const FENCE_Z = 58;           // the boundary line the gate stands in
export const HOUSE_W = 29.25;        // 实测：参考图 295px 墙 / 65px 路 × TRACK_W(参考)
export const HOUSE_D = 11;
export const HOUSE_WALL_H = 5.2;
export const FORK_Z = 17;
// 主屋从轴的中段挪到大道的尽头：他标的 1 号（主屋）要放到 2 号位（大道跑完那头）。
// 量出来的：牌 2 的圆心离两条路缘线拟合出的轴线 0.3 px（就在轴上），而土带的最后一行
// 在 v=145，牌心在 v=110 —— 差的那 35 px 正好是一栋 7 m 高的房子在 230 m 外的像高，
// 也就是说他指的是"房子坐在那儿之后，屋身盖住大道尽头"，不是"路再往前"。
export const HOUSE_Z = -104;
// 主屋朝门前伸多少：台基 + 门廊 + 檐口，从建成之后的世界包围盒量回来是 10.05 m（屋心到最南缘，
// 不是 HOUSE_D/2 —— 那只是墙）。大道和屋后按树行都收在这个数外面，否则路尾钻进屋檐底下、
// 树干从屋顶里长出来（上一版量出来路屋重叠 32 m²）。
// 屋心从 -100 挪到 -104 是因为住宅排（后檐在 -88.9）：-100 那一版量回来主屋前檐离住宅排后檐
// 只有 1.05 m，两栋的屋檐在 res-3/res-4 那 4 m 宽的重叠段上几乎贴住。
export const HOUSE_FRONT = 10.6;
// 门前那个圆形前庭（r=TRACK_W*1.45，原来抬在 Y.plaza 上）和牌坊底下的压土圈这一轮一起删了：
// 他要"这两块地面圆形去掉，保持和别的一样"。所以这里不再有 PLAZA_* —— 大道从牌坊一直是一条
// 等宽的车辙带，两侧的路肩也是。
export const ALLEE_FRONT = [16, 46];
// 树行走到主屋门前为止：按树在 x=±ALLEE_X，房子宽 29.25，树再往北就长在屋里了。
// 树行走到主屋门前为止，而且要从檐口外退 2 m 以上：按树 scale=1 的冠量回来半径 1.6~2.0 m，
// 上一版只退 0.6，探针量到最北那两颗（x=±7.4, z=-92.8）的冠压进主屋包围盒 1.15 m。
// 退到 +3.4 之后树在 z=-90.0，离主屋前檐（-93.95）3.95 m，而且整行都落在屋后那条干管
// （z=-91）的南边 —— 树行和管子不再相交。
export const ALLEE_REAR = [HOUSE_Z + HOUSE_FRONT + 3.4, -9];
export const PLATEAU_R = 130;

export function halfTrack() {
  return TRACK_W / 2;
}

/* =========================================================
   路网 (axis-aligned)
   axis:'z' -> 一条南北带，横向位置 at、纵向从 from 到 to；axis:'x' 反之。
   大道一条通带从牌坊跑到主屋门前（收在 HOUSE_Z + HOUSE_FRONT，不是屋心 —— 按墙线收会钻进
   屋檐底下，量出来重叠 32 m²）。横道只从建筑**旁边**过。
   这一轮重排成三条：一条南北大道 + 两条东西横道。删掉的两条都不再伺候任何东西 ——
   · plant（z=32，-60..90）：原本是厂区四栋 + 仓库的门前路；那些建筑搬进大院和东头给水排之后，
     它两头都不接东西，量出来 150 m 长、服务 0 栋建筑、白占 7 盏路灯。它让出的带子并回大院。
   · mid / west 上一轮已经删了（牛舍门口便道、西牧场进路）。
   cattle 是全场主横道：西头从牲口大院东墙的门里穿进去，铺到院西栅栏外 10 m，整条走完
   牛舍院和牲口圈之间那 16 m 空当；东头到泵房门口（x=116）。
   路口只和大道相遇：大道高一层（Y.spine）。两条 Y.cross=0.056 的道压在一起就是共面闪烁
   —— 上一版 west 从住宅横道的中心线起头，量出来 51.8 m² 共面。
========================================================= */
export const ROADS = [
  { id: 'avenue', axis: 'z', at: 0, from: 64, to: HOUSE_Z + HOUSE_FRONT, w: TRACK_W, y: 0.084 },
  { id: 'cattle', axis: 'x', at: -22, from: -100, to: 116, w: TRACK_W * 0.8, y: 0.056 },
  { id: 'residence', axis: 'x', at: -70, from: -78, to: 78, w: TRACK_W * 0.8, y: 0.056 },
];


/* 树行/管线/电线杆的走廊：都贴着大道的路肩外沿，左右对称。 */
export const ALLEE_X = halfTrack() + 2.9;        // 屋后按树行
export const PIPE_X = halfTrack() + SHOULDER_W * 0.5;   // 水干管（路肩下）
// 11.4 m：原来这条路肩外排的是院墙和门楼，杆子让开墙外一臂站在 11.4；墙撤了，杆线
// 留在原地不动 —— 它本来就站在住宅横道和院地之间那条空当里。
export const POLE_X = halfTrack() + SHOULDER_W + 4.3;

/* =========================================================
   分区
   1 住宅：z = RES_LINE_Z 一条横线，六户，正面朝南（朝着住宅横道、也就朝着大门方向）
   2 牲口大院：全场就这一片栅栏，在西边那片空地上（他这一轮点的名：原来那三片 —— 牛栏、
     西牧场、羊圈 —— 全部圈进 8 号位那一个大围栏，里面再分成几个小围栏，牛舍进圈）。
     外围一圈 + 里面几条分隔，东墙开一个车行门，cattle 横道从门里插进来。
     东边那条是河（水面最近处量回来 r=172，在 x≈163、z≈-55 那一段），牲口不进那一边 ——
     上一轮"牛圈挨着河就是往水源里拉粪"这条还作数。
   3 给水那一排（水塔/配电/水泵）：搬到大院对面、东头贴圆缘朝河那一侧（他点的 1→2）。
   4 农机房：大院东墙外，和牛舍、仓库排在同一条线上（他点的 3→4），不进圈 ——
     他上一轮的规矩"农机房这边不要栅栏"还留着。
========================================================= */
export const RES_LINE_Z = -82;
// 给水那一排的进深：plateau 里面地形精确 0（js/core/ground.js:175 —— r≤130 就是 0，没有
// "差不多"），所以这一排能往东走到哪儿只看远角离 130 还剩多少。三栋排在 z=-34 一条线上、
// 门朝南朝着 cattle 横道；泵房是这三栋里最靠东的，它的远角量回来 (115.8, -40.1) = r 122.6。
// 他标的 2 号位落点按拟合出来的机位反投回地面是 r=325（河对岸的山根）—— 130 之外就没有
// 平地了，所以这一排只能贴在圆缘里侧，离河最近 57 m。
export const WORKS_Z = -34;
// 大院外框。西北角 (-110,-60) 到轴心 125.3：再往西或再往北 2 m 就翻出 plateau 的圆缘。
// 北界这一轮从 26 推到 34 —— 那是删掉的 plant 横道让出来的带子，圈地因此多出 8 m。
export const YARD = { x0: -110, x1: -50, z0: -60, z1: 34 };
// 里面分成几片。这一轮他点掉两道栅栏（截图里标红的）：牛舍院南边那道 z=-30，和放牛与羊圈
// 之间那道双栅栏 z=8/12。于是牛舍院和中间那条车道合成一个院子 —— 车道从东墙的门里进来，
// 一路铺到院西栅栏外 10 m，正面正对牛舍和仓库的门；放牛和羊圈也合成一片（羊站的那块踩实
// 土面还在，只是不再有栅栏围着它）。
// 还留着的栅栏收在 z=-14 而不是 -16：灯杆落在路缘外 2.2 m = -16.2，再让 0.2 m 就正好站在
// 栅栏横杆里头。
// 土面这一轮全撤：他要"这块也弄成绿地"，羊圈那块踩实的土面也变成草地，所以四片一样，
// floor 这个字段没了（见 js/world/road.js 末尾那段说明）。
export const PADDOCKS = [
  { id: 'steadings', x0: -110, x1: -50, z0: -60, z1: -14 },  // 牛舍 + 仓库 + 穿过的车道
  { id: 'cattle', x0: -110, x1: -81, z0: -14, z1: 34 },      // 牛栏
  { id: 'grazing', x0: -79, x1: -50, z0: -14, z1: 34 },      // 散吃的那片 + 羊圈（中间那道撤了）
  { id: 'sheep', x0: -79, x1: -50, z0: 12, z1: 34 },         // 羊和鸡站的那一块
];
export const ZONES = { residence: { z: RES_LINE_Z, xs: [-66, -43, -20, 20, 43, 66] } };

/* 栅栏一条一条列，不是"每个圈画一个矩形"：外围四条边和里面的分隔线有共用端点，按圈画就会
   在同一条线上立两根柱子。gateAt 是这门在这条线上的分数位置，gateW 是门宽（跟着那条路）。
   全场只有一个车行门：cattle 横道穿过东墙（x=-50）那一口，位置 = (-22 + 60) / 94 = 0.404。
   院内的几道门开在车道两侧，让车能拐进牛舍院和牲口圈；羊圈没有车行门 —— 它夹在南边，
   没有路经过，羊是从隔壁放牛那片赶进去的。 */
const LANE_GATE = { gateAt: (-22 - YARD.z0) / (YARD.z1 - YARD.z0), gateW: TRACK_W * 0.8 };
export const FENCE_LINES = [
  { axis: 'x', at: YARD.z0, from: YARD.x0, to: YARD.x1 },
  { axis: 'x', at: YARD.z1, from: YARD.x0, to: YARD.x1 },
  { axis: 'z', at: YARD.x0, from: YARD.z0, to: YARD.z1 },
  { axis: 'z', at: YARD.x1, from: YARD.z0, to: YARD.z1, ...LANE_GATE },
  { axis: 'x', at: -14, from: YARD.x0, to: YARD.x1, gates: [0.25, 0.75] },   // 牲口圈北边
  { axis: 'z', at: -81, from: -14, to: YARD.z1 },                             // 牛栏 ↔ 放牛/羊圈
];
export const GATE_W = TRACK_W * 0.8;

// 每栋建筑站在哪，只在这里写一次：建筑模块读它定位，pipes.js 读同一组数收尾。
// 以前是两套字面量，所以牛舍一挪，水管就留在墙里两米。
export const SITES = {
  house:     { x: 0, z: HOUSE_Z, ry: 0 },
  // 牛舍和仓库都在院里，正面朝 +z —— 朝着庄园大门那个方向，也正好朝着穿进院来的那条车道。
  // 上一版它们立着排（ry=π/2，门朝东），这一轮按他说的转过来，于是各自的长宽对调：
  // 牛舍横过来 24.8 宽 × 17.6 深，仓库 21 × 17.8，并排排在院子的北半条。
  barn:      { x: -95, z: -45, ry: 0 },
  warehouse: { x: -66, z: -45, ry: 0 },
  // 农机房：大院东墙外，和牛舍、仓库在同一条线上（他这一轮的 3→4）。它不进圈。
  machine:   { x: -32, z: -45, ry: 0 },
  // 给水那一排：东头贴圆缘、朝河那一侧，门朝南朝着 cattle 横道。三栋的远角都验过在 r=130
  // 的圆内（泵房 122.6）。间距不是等分：配电 14.2 m 宽、水泵 11.7、水塔 9.1，按各自体量排开
  // 之后塔↔配电 5.4、配电↔泵 4.1。
  tower:     { x: 76, z: WORKS_Z, ry: -Math.PI / 2 },
  power:     { x: 93, z: WORKS_Z, ry: -Math.PI / 2 },
  pump:      { x: 110, z: WORKS_Z, ry: -Math.PI / 2 },
};


/* 建成之后从屋心到檐口的实测距离（朝 +z / +x 为正）。模块里写的 W/D 只是墙，屋檐、台阶、
   滑到边上的库门、水塔的腿都在它外面，所以水管收口得读这个数，不能在两处各猜一遍 ——
   上一版 pipes.js 里那串 standOff 就是按旧体量猜的，量到最深的一根在配电里穿堂 14.25 m。
   牛舍、仓库、农机房这一轮都转成 ry=0（正面朝大门），接的都是南檐：干管沿 cattle 横道的
   北肩走（z=-27），在它们南边。再强调一次上一版那个错：把东山墙的 x 偏移喂给了一个 z 端点，
   管子从 z=-27 一路扎到 z=-89.7，在牛舍里 24.8 m。换朝向必须换轴、并且重量。
   量的时候注意：**是屋心到檐口，不是包围盒半高**。牛舍的盒子绕着屋心不对称（门廊+台阶往前
   多伸 0.6 m），拿 (z1-z0)/2 当偏移会短 0.4 m，管口就扎进屋里 0.25 m（这一轮就这么回）。 */
export const EAVE = {
  tower:     { z: 4.50 },
  power:     { z: 6.00 },
  pump:      { z: 6.10 },
  machine:   { z: 9.10 },    // ry=0：正面（+z）那道檐
  barn:      { z: 9.41 },    // ry=0：南檐到屋心 —— 量回来 z1=-35.59、屋心 -45
  warehouse: { z: 9.80 },    // ry=0：南檐，量回来 z1=-35.20
  res:       { z: -6.90 },   // 住宅排的后檐（朝主屋那一面）
};


/* 车行门要回路网验一遍：FENCE_LINES 上点名的那道门，必须真的有一条路横过它 —— 那条路的方向
   和线垂直、它的中心线正对着门、它的两端盖住这道门所在的墙。对不上的话 gateRoad 就是 null，
   那等于在栅栏中间开了个车到不了的洞（上一版 ZONES.plant.gate 就是这么个 null）。
   院里那几道 gates 是赶畜/人行的小门，不验。 */
for (const f of FENCE_LINES) {
  if (f.gateAt === undefined) continue;
  const p = f.from + (f.to - f.from) * f.gateAt;
  const road = ROADS.find(r => r.axis !== f.axis && Math.abs(r.at - p) < 0.05
    && Math.min(r.from, r.to) <= f.at && Math.max(r.from, r.to) >= f.at);
  f.gateRoad = road ? road.id : null;
}

export const LANES = ROADS.filter(r => r.axis === 'x').map(r => ({
  id: r.id, z: r.at, w: r.w,
  xFrom: Math.min(r.from, r.to), xTo: Math.max(r.from, r.to),
}));


/* =========================================================
   住宅 (THE RESIDENTIAL LINE)
   六户排在 z = -82 一条线上，正面都朝 +Z：沿街直接开门开窗，前面没有院墙（原来那一排
   青砖围墙 + 门楼整体撤掉了）。户距 23 m，中间那格 40 m 是让给大道的。
========================================================= */
export const PLOT_FRONT = 11.5;
export const PLOT_DEPTH = 7.4;

export const PLOTS = ZONES.residence.xs.map((x, i) => ({
  id: `res-${i + 1}`,
  x,
  z: RES_LINE_Z,
  ry: 0,                                               // 正面朝 +Z：朝着横道和大门
  bays: i === 2 || i === 3 ? 5 : 3,                    // 中间两户是大家族，五开间
}));

/* =========================================================
   GROUND LAYERS
   Surfaces share the plateau, and inside PLATEAU_R the terrain is exactly 0 -- so the only
   thing keeping them apart is the gap chosen here. That gap used to be 2-5 mm, under what the
   depth buffer can resolve at the overview distance: on a 24-bit buffer with near = 0.1 the
   resolvable step at 114 m is 7.75 mm, so the plaza and the cross drive traded pixels on every
   frame the camera turned. That is the shimmer, not the damping tail.

   Two coupled knobs fix it: near went 0.1 -> 0.5 (dz = z^2 / (near * 2^24)), and every pair that
   overlaps in plan is >= 14 mm apart -- 14 mm resolves out to 343 m, past maxDistance = 320.
   polygonOffset is gone with them: a per-material guess at the same problem that shimmers on its
   own at grazing angles.
========================================================= */
export const Y = {
  terrain:  0,       // the plateau itself
  shoulder: 0.014,   // 路肩磨草带：最宽、最低
  floor:    0.028,   // 建筑自己的地面（主屋、六户院）
  cross:    0.056,   // 东西横道
  spine:    0.084,   // 南北大道：谁都不让
};
