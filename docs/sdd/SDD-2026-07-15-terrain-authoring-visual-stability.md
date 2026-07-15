# SDD-2026-07-15-terrain-authoring-visual-stability: Terrain 编辑取景与表面稳定性

## Status

Validating (approved by user on 2026-07-15; CPU gates complete, GPU and manual sign-off pending)

## Context

Terrain Phase 3 人工 Vulkan 验证暴露了三类可复现问题：

- production-default Terrain 为 8192 m × 8192 m，但 Editor viewport camera 固定 `far = 2000 m`；Scene 的通用 world/subtree bounds 又只处理 Mesh，不包含 Terrain。缩放 Terrain 或把相机移远后，远裁剪面会切过平地和凸起，表现为水平硬切、远处薄片或悬浮块。
- `Terrain.scene.json` 引用的 `TerrainGate.AshTerrain` 是自动化 readiness/LOD fixture。中间的阶梯/坡道来自 fixture 的九个合成 Component；它们不是用户编辑内容。当前合成形状只存在于 composed cache，而 canonical Base/edit layer 仍是平地，编辑后重组 touched Component 会造成内容不一致。该 fixture 也不应被当作人工 authoring 示例。
- 平地与雕刻坡面出现规则暗纹。静态证据表明主因是方向光 3×3 PCF 对同一 reference depth 采样斜置 receiver，缺少 receiver-plane correction；outer cascade static cache identity 又未覆盖 Terrain 内容变化。现有 `ddx/ddy` normal 仅得到逐三角 flat normal，会进一步显出粗 LOD 曲面分面，但不能解释平地暗纹。复核 render path 后确认 GBuffer 与 shadow 当前均把主视图 `view/projection/camera_position/output_target` 交给 Terrain LOD，shadow atlas/tile 尺寸没有参与 LOD 选择；此前“shadow target 导致 caster/receiver LOD 不同”的假设被证据否定，不纳入实现。

这组修复跨越 Function Scene、Function Render、shader、Editor camera 与测试资产，且修改 Terrain shadow/camera 数据合同，风险级别为 S2。批准前只允许补充本 SDD 与只读证据，不修改生产代码或资产。

## Goals

- 保留现有“有限正数、允许非均匀”的 Terrain scale 合同；单位、均匀和非均匀正缩放下都能正确取景、聚焦、裁剪、查询和渲染。
- Editor 初始取景、`F` 聚焦和后续相机导航不再被固定 2000 m far plane 截断 production-size Terrain。
- 自动化 gate fixture 的 canonical 数据与 composed cache 一致，并与人工 Create Flat authoring 流程明确隔离。
- 消除平面方向光自阴影周期暗纹，并用回归合同锁定 Terrain shadow caster 与 GBuffer receiver 继续使用同一主视图 LOD 输入。
- Terrain snapshot generation、transform 或 `casts_shadow` 变化后，outer cascade static cache 必须失效；完全未变时才允许复用。
- 用全分辨率规范高度场的平滑 shading normal 替代逐三角 flat normal；在相邻 LOD 边界保持插值连续，减少雕刻曲面和远距粗 LOD 的分面感。
- Vulkan 与 DX12 画面、validation、性能及人工编辑反馈一致；不以 TAA、关闭阴影或粗暴增大常量 bias 掩盖缺陷。

## Non-goals

- 不禁止正 Terrain scale，不允许只在 Gizmo/UI 层禁用而保留运行时合同。
- 不支持 Terrain 旋转、零缩放或负缩放；这些仍按现有 Scene v6 合同 fail closed。
- 不修改 RHI、RenderGraph API、descriptor layout 或 Vulkan/DX12 后端实现。
- 不引入新的共享 Terrain draw-plan/cache 抽象；当前两条渲染路径已使用相同主视图输入，未证明需要改变所有权或生命周期。
- 不在本包实现实时 stroke 中间发布；一次拖动一条 undo history 的实时反馈另行设计，不能与本次视觉正确性修复混在同一提交。
- 不自动 bless RenderGate golden 或 PerfGate baseline。任何预期画面变化先由用户人工确认。
- 不把 `TerrainGate` 的合成坡道删除；它继续服务自动化 LOD/material/readiness 覆盖。

## Current implementation

- Entry points:
  - `EditorViewportCameraService::SeedCameraFromSceneContent` / `FocusEntity` / `RefreshCameraOverride`
  - `get_entity_world_bounds` / `get_entity_subtree_world_bounds`
  - `TerrainRenderPass::render_surface` / `render_shadow`
  - `SunLightShadowPass::resolve_cascade_cache_mode`
  - `DirectionalShadowMask.hlsl` 与 `TerrainSurface.hlsl`
- Modules:
  - Function Scene 负责 world-space bounds/query。
  - Editor camera 消费 Scene bounds 并发布 camera override。
  - TerrainRenderPass 每次 draw 独立运行 Terrain LOD；`make_shadow_view_context` 保留主视图 `output_target`，shadow frame 只替换 `view_projection`，因此 Terrain LOD 仍由主视图尺寸、`view`、`projection` 与 `camera_position` 决定。
  - SunLightShadowPass 用 `frame.static_scene_revision + light VP` 缓存 outer cascades。
- Data flow:
  - Scene Terrain revision 与 primitive/transform/light revision 分离。
  - ScenePresentation 构造 `VisibleRenderFrame` 时只把 primitive revision 作为 `static_scene_revision`。
  - brush publication 替换 immutable Terrain snapshot；render proxy 消费新 generation。
- Known constraints:
  - production default 是 8193² samples / 8192 m extent。
  - Editor orbit 最远允许 20000 m，当前 far plane 只有 2000 m。
  - Terrain 只允许 axis-aligned positive scale；world-space brush/query 已依赖该能力。
  - shadow PCF radius 默认 1，当前 9 个 taps 共用一个未经 receiver-plane 修正的 reference depth。

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Function Scene / Asset | 增加 Terrain world bounds 计算；完整 resident snapshot 使用 Component root min/max，缺失 Component 时回退到 height mapping 的保守范围；`get_entity_world_bounds` 与 mixed subtree 同时支持 Mesh/Terrain | `Function/Scene/SceneQuery.*`、必要时 `Function/Asset/Terrain*` 公共 helper |
| Editor camera | 保存最近一次有效 scene/focus bounds；根据当前 view-space bounds 动态计算 near/far，初始取景、`F` 聚焦、dolly/orbit/pan 后都刷新；禁止硬编码“8192/12000 m”特例 | `editor/Services/EditorViewportCameraService.*` |
| Terrain fixture | gate 中合成坡道写入 canonical Base 或 edit layer，再由正式 composition/writer 生成同一 composed cache；fixture test 以独立全域 oracle 断言 canonical、cooked height/weight/min-max 与加载结果。固定 `lod_errors` 是唯一 gate-only metadata 例外，另由精确合同锁定。人工 checklist 只从 Create Flat 开始，不再启动 gate scene 供 authoring | `product/assets/terrain/TerrainGate.AshTerrain`、`tests/Terrain/terrain_readiness_tests.cpp`、相关 fixture writer/test 文档 |
| Directional shadow shader | 从 shadow projection 的屏幕导数求 receiver-plane depth gradient；每个 PCF tap 按 UV offset 修正 comparison depth，退化导数回退到既有 constant bias | `engine/Shaders/Shadow/DirectionalShadowMask.hlsl`、shadow shader contract tests |
| Terrain LOD regression | 不改变现有 draw-plan 所有权；补 source/behavior regression，锁定 shadow context 不得用 atlas/级联 tile 尺寸替换 Terrain 的主视图 LOD 输入 | `Function/Render/TerrainRenderPass.*`、`SceneRenderer.*`、tests |
| Shadow cache identity | 新增明确的 static shadow caster revision：绑定 scene runtime/content epoch，并逐项覆盖实际 Static/Stationary mesh caster 的 transform、mesh GPU publication、DepthOnly material publication 与 `casts_shadow=true` Terrain 的 snapshot/render-publication/transform；精确 caster 集合已覆盖增删，故不再混入全体 primitive revision，也不让全局 transform revision、Movable mesh 或不投影 Terrain 造成无关失效 | `Function/Render/RenderScene.*`、`MaterialRenderProxy.*`、`TerrainRenderAsset.*`、`SunLightShadowPass.*`、tests |
| Terrain surface normal | vertex 阶段从已有 global height buffer 读取全分辨率规范梯度；细 LOD 边遇粗邻居时按粗边两个端点的规范梯度插值。该 shading field 有意与 morph 解耦；顶点只输出未归一化 world normal，pixel 阶段插值后归一化，`ddx/ddy` 只作退化 fallback | `engine/Shaders/Terrain/TerrainCommon.hlsli`、`TerrainSurface.hlsl`、shader tests |
| Specs / verification | 回写当前 scale、camera bounds、shadow cache/LOD 与人工 fixture 边界；记录人工签署证据 | `docs/specs/features/terrain.md`、相关 module spec、`docs/VERIFY.md`（若流程变化）、`docs/verification/terrain/` |

### API / contract changes

1. **Terrain bounds**
   - `get_entity_world_bounds` 对 Mesh 与 Terrain 都可返回有效 `SceneWorldBounds`；不支持的 entity 仍返回 false。
   - Terrain local X/Z 范围来自 layout 与 sample spacing；Y 范围优先聚合 resident Component root min/max。snapshot 未完整 resident 时使用 height mapping 的保守范围，不得低估。
   - 完整 world matrix 的有限正缩放参与八角变换；父级缩放必须反映在 subtree bounds。

2. **Editor clip range**
   - near/far 由当前 camera view 中目标 bounds 的最小/最大正向深度和安全余量求得；camera 位于 bounds 内时 near 保持安全下限。
   - far 至少覆盖目标 bounds，且随 orbit/dolly/scale 或 Scene bounds revision 更新；不把固定 frame 数当作刷新条件。
   - 动态 far 只影响 Editor override，不改变 Scene camera 的序列化字段。

3. **Terrain LOD regression**
   - shadow atlas extent、cascade tile resolution 和 pass order不得替换 Terrain LOD 使用的主视图 `output_target/view/projection/camera_position`。
   - 本包不新增跨 pass 或跨帧共享计划；若未来要缓存 LOD 结果，必须另行证明两个真实调用点、生命周期与多 viewport key。

4. **TerrainGate canonical boundary**
   - canonical Base、唯一固定 ID 的 weight edit layer、cooked height/weight 与 min-max 必须由独立解析 oracle 和正式 recomposition 双重验证；设计区外 owned weight 必须保持默认 Layer 0。
   - 固定 `lod_errors` 不属于 authoring canonical layer composition。它只用于 TerrainGate 强制覆盖九个 LOD，是经批准的唯一自动化 metadata 例外；测试对全部 1024 个 Component 精确验证，普通 Create Flat/Import/Save 不采用该例外。
   - TerrainGate 不是人工编辑模板；人工验收必须创建新的 flat Terrain。

5. **Shadow cache**
   - static cache entry 同时比较 light/cascade VP 与 static caster revision。
   - revision 绑定 `scene_runtime_id + scene_content_epoch`，防止 pass 实例跨 Scene 内容世代复用旧 tile；mesh 只遍历实际 Static/Stationary shadow draw，并覆盖 world transform、section 与 mesh GPU publication identity，Movable-only transform 不令 outer cache 失效。
   - 仅 `casts_shadow=true` 的 Terrain 进入 revision；其 snapshot generation、asset identity/residency replacement、render asset accepted/published 状态或 world transform 任一变化都令相关 outer cascade 进入 `StaticRefresh`。render asset identity 必须在一次 mutex 持有期内取 immutable snapshot，禁止拼接多个 getter 的跨时刻状态；`casts_shadow=false` Terrain 的内容与 transform 变化不得影响 cache。
   - planner 只分配 tile 和选择 `StaticRefresh`，不得提前提交 cache identity；只有 refresh execute callback 的 tile clear 与 `StaticOnly` draw 都成功录入 CPU command recording 后才提交 revision 与 light VP。
   - 当前安全边界只证明 CPU recording 接受了两组 draw；现有 RenderGraph callback 不暴露 pass-end、queue submit 或 GPU completion 结果。本包记录该限制，不扩 RenderGraph API。
   - 未变化才允许 `StaticCached`；near cascade 继续每帧更新。

6. **PCF receiver-plane correction**
   - comparison depth 对每个 tap 使用同一 receiver plane 上对应 UV 的预测深度，再应用小的 constant bias。
   - gradient 非有限、UV Jacobian 退化或超出安全界时 fail soft 到 constant-bias path；不得产生 NaN/Inf。
   - 不通过全局大幅提高 `depth_bias` / `normal_bias` 解决，以避免 peter-panning。

7. **Smooth normal**
   - shading normal 取全分辨率规范高度场的中心差分（Terrain 全局外边界使用单边差分），不随当前 LOD stride 或 morph factor 改变。
   - 同 LOD 接缝两侧读取同一 global sample。细 LOD 边遇粗邻居时，细边顶点按粗邻居边上两个实际顶点的规范梯度插值；双粗邻角点必须退化为共同端点，保证相邻 raster 插值一致。
   - 顶点阶段用 object-to-world 线性部分变换两条 tangent 并取叉积，但不得先单位化；pixel 阶段对插值后的 world normal 安全单位化并统一朝向。这样在有限正非均匀缩放下仍保持边界的线性插值相等。
   - 该合同是视觉平滑场，不声称逐像素等于 geomorph 三角形几何法线；法线插值退化时才回退 `ddx/ddy` 几何法线。
   - 材质 tangent-space normal 继续叠加在该几何 basis 上；不新增 shader resource binding。

### Backend impact

- Vulkan/DX12 共用 HLSL、Function draw plan 与 cache identity；不增加后端专用分支。
- shader 编译结果必须在两后端反射出相同既有绑定；本方案不改 descriptor/root constant layout。
- Debug Vulkan validation 与 DX12 debug layer/GPU validation 都必须运行；任一 validation/debug-layer error 视为失败。

### Performance

- receiver-plane gradient 每像素计算一次，每 tap 只增加常数级 dot/add；不得增加 PCF tap 数。
- smooth normal 在 vertex 阶段增加已有 height buffer 的有限采样，不在 pixel 阶段做多次高度读取。
- 不调整 300 FPS、Terrain shadow/GBuffer 或 Standard PerfGate 阈值。任何 PerfGate FAIL 阻断提交；WARN 必须分析并由用户裁定。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| Focused RED/GREEN | Terrain bounds、camera clip、主视图 LOD 输入回归、cache invalidation、shader source/数学边界 | `RunTests.bat Debug --test-case="*Terrain*"` 加新增精确 filters |
| 全量单测 | Debug/Release 行为与 legacy bridge | `RunTests.bat Debug`；`RunTests.bat Release` |
| 构建 | Editor/Sandbox + shader 双配置 | `build_editor.bat Debug/Release`；`build_sandbox.bat Debug/Release` |
| 架构/计划 | Function/Editor 边界与 SDD/规格同步 | `RunArchGate.bat`；`AIDevDoctor.ps1 -Mode ValidatePlan` |
| Readiness | Editor/Sandbox × Vulkan/DX12、信号驱动退出 | `run.bat all Debug --smoke-test-seconds=120` |
| Validation | Terrain 场景、双后端 shadow/LOD/cache | Vulkan validation 与 DX12 debug layer/GPU validation 的定向 Terrain smoke |
| 渲染回归 | 默认 golden、跨后端；不 bless | `RunRenderGate.bat` |
| 性能 | 绝对与比较门槛；不 bless | `RunPerfGate.bat -Profile Standard` |
| 专项图像 | clean flat、固定斜坡、雕刻曲面；Final/GBuffer normal/shadow mask；PCF 0/1/2、各 cascade/transition | readiness-driven 非 bless capture + image diff/区域统计 |
| 人工签署 | Vulkan/DX12 中缩放、远距、Create Flat、雕刻、Undo/Redo 与无纹路 | 由用户按中文 Terrain manual checklist 亲自操作并签署；AI 不代签 |

## Task breakdown

1. **证据与 RED**：固化 flat/ramp/sculpt shadow mask、camera far-cut 与 gate recomposition 失败用例。
2. **Fixture integrity**：让 gate canonical 数据与 composed cache 一致；人工验证改为 Create Flat。
3. **Bounds/camera**：实现 Terrain bounds 与动态 clip，完成 unit/uniform/nonuniform/parent scale tests。
4. **PCF correction**：先消除 clean flat 的周期暗纹，保持绑定不变并验证 PCF radius 0/1/2。
5. **LOD regression/cache**：锁定 shadow 不替换主视图 Terrain LOD 输入；Terrain 编辑、transform 与 casts-shadow 改变都刷新 outer cache。
6. **Smooth normal**：替换 flat derivative normal，验证 morph 0/0.5/1 不改变 shading field、同 LOD seam、邻接 LOD 端点插值、双粗邻角点、全局边界与非均匀 scale。
7. **全量门禁**：按验证矩阵串行完成 CPU、双后端、RenderGate、PerfGate；恢复四配置并审计 fresh logs/root processes。
8. **人工验收**：用户完成 Vulkan/DX12 中文表格；失败继续修复，未签署不得标记 Done。

每个步骤单独提交或保持可独立审查的 commit 边界；fixture、camera、shadow 和 normal 不混成一个不可回滚 diff。

## Risks

| Risk | Mitigation |
| --- | --- |
| 动态 far 过大降低深度精度 | 使用现有 reverse-Z 与按 bounds 自适应范围；不采用固定 12000/20000 m；加入近处小物体深度回归 |
| receiver-plane correction 在级联边界退化 | 严格 finite/Jacobian guard，退化回既有 constant bias；覆盖 cascade transition |
| smooth shading normal 与 geomorph 三角形不完全一致 | 明确把二者解耦作为已批准视觉合同；粗邻边只按实际 coarse 顶点的未归一化 world normal 插值，并以独立 CPU oracle 覆盖 seam/corner/boundary/morph/scale |
| 未来改动误把 shadow atlas 当作 Terrain LOD viewport | source/behavior regression 锁定主视图输入；本包不引入新的跨 pass 状态 |
| cache revision 漏掉资产内容更新 | identity 直接绑定 accepted immutable snapshot generation/identity，并覆盖 RenderScene rebuild/transform/casts-shadow |
| gate fixture 预期画面变化 | 不 bless；先保留自动化合同，专项 capture 由用户确认后才决定 golden |
| GPU 成本影响 300 FPS | vertex 而非 pixel height sampling；不增加 PCF taps；Standard PerfGate 与 Terrain 性能合同不放宽 |

## Validation record

- Focused RED/GREEN 已分别覆盖 Terrain world bounds / Editor clip、静态 caster cache / receiver-plane PCF、TerrainGate canonical/cooked 全域 oracle，以及跨 LOD canonical shading normal；camera、shadow、fixture、normal 四个边界的独立只读复核均为 `P0/P1/P2 = 0, CLEAN`。
- Fresh 全量 `RunTests.bat Debug` 通过 `435/435` test cases、`25161/25161` assertions；Release `Tests.exe` 通过 `435/435` test cases、`25168/25168` assertions，二者均报告 `All Memory Free`。
- `build_editor.bat Debug/Release` 与 `build_sandbox.bat Debug/Release` 均成功；Terrain HLSL 的 GBuffer/depth/LOD permutations 已分别用 DXIL 与 SPIR-V 编译通过。
- `RunArchGate.bat` PASS，仅保留 `35` 条既有 legacy warning；`AIDevDoctor.ps1 -Mode ValidatePlan` exit `0`。
- 双后端 readiness、validation、专项图像 A/B、non-bless RenderGate、Standard PerfGate 与用户人工操作签署尚待在最新集成提交上执行；完成前本 SDD 不标记 Done，也不 bless golden/baseline。

## Approved decisions

- 用户批准继续保留有限正数（含非均匀）Terrain scale，并修复 bounds/camera，不破坏性限制为 `scale=(1,1,1)`。
- 用户批准 `TerrainGate` 仅作为自动化 fixture；人工验收统一从 Editor `Create Flat` 开始。
- 用户批准方案 A：全分辨率规范 shading gradient、粗邻边端点插值、VS 未归一化而 PS 归一化；它有意与 geomorph 几何解耦，不新增 binding/payload。
- 用户批准固定 `lod_errors` 作为 TerrainGate 唯一 gate-only metadata 例外；canonical height/weight/min-max 仍必须与正式 composition 一致。
