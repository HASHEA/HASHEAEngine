# SDD-2026-07-15-terrain-interactive-authoring-workflow: Terrain 统一编辑层与直接交互

## Status

Validating（用户于 2026-07-15 批准本 SDD；production-size Base 深拷贝、远端 Component 跨代上传饥饿、编辑期查询/patch 热点、Flatten 跨批目标及面板/选择 tool ownership 均已修复；最终自动门禁已通过，等待 Vulkan/DX12 人工重签）

## Context

当前 Terrain authoring 已具备 Base Import、多编辑层、Raise/Lower/Smooth/Flatten/Noise/Paint/Erase、patch undo/redo、异步 composition/publication 与 UIContext-only Terrain Mode，但人工操作暴露了三个核心体验问题：

- `AddStrokeSample` 只积累 raw cursor samples，直到 `EndStroke` 才调用 `ApplyBrushStroke → ComposeComponents → PublishDirtyComponents`，所以拖动期间地形完全不变化，只有松开鼠标后刷新。
- Terrain Mode 只监听 Asset Browser 的单个 Terrain asset selection。Hierarchy 选中带 `TerrainComponent` 的实体不会解析其 `asset_path`，用户必须再去 Asset Browser 找资产、选择资产、打开面板。
- 当前每个 `TerrainEditLayer` 有全层唯一的 `Additive/Alpha` 高度模式；Raise/Lower/Noise 只接受 Additive，Smooth/Flatten 只接受 Alpha。用户必须理解实现细节并手动 Add Layer，才可能切换工具。材质 8 lane 和 Terrain edit-layer stack 又都在 UI 中称为 Layer，概念混乱。

笔刷算法本身不要求“必须有层”；层的价值是非破坏编辑、排序/隐藏/强度/锁定和资产重载。用户选择保留完整非破坏层栈，但采用方案 2：一个普通雕刻编辑层必须能容纳全部高度工具，默认流程不再要求理解 Additive/Alpha。该变更涉及 Terrain 数据模型、`.AshTerrain` 向后读取、brush/patch/composition、Editor selection/session 和 viewport input，风险级别为 S2。

本 SDD 在 runtime performance/loading SDD 完成后实施。实时预览会提高 composition/publication 频率，不能在现有 shadow/load CPU 放大问题尚未收敛时先行启用。

## Goals

- 用统一的逐 sample 仿射高度变换 `H_out = a × H_in + b` 替代用户可见的 Additive/Alpha 层类型；同一编辑层按 stroke 顺序支持 Raise、Lower、Noise、Smooth 和 Flatten。
- v1 Additive/Alpha `.AshTerrain` 必须精确迁移到新表示；加载后首次 composition 与旧算法在有限输入上逐 sample 等价。
- 材质固定 8 lane 在 UI/文档中统一称为“材质槽”；可排序的非破坏 stack 称为“地形编辑层”。Paint/Erase 仍写入选中的编辑层内的材质权重块。
- Hierarchy 单选带 TerrainComponent 的实体后，打开 Terrain Mode 即自动解析并绑定其 Terrain asset；不再要求重复选择 Asset Browser 项。
- layerless Terrain 第一次非空落笔时自动创建通用 `Edit Layer / 编辑层`；该层同时承载高度变换与材质权重，创建层和整次 stroke 原子合成一条 Undo/Redo。仅选择实体、打开面板、移动鼠标或空 stroke 不得把资产标为 dirty。
- 活动 stroke 每约 `80–100 ms` wall clock 增量应用新增 samples、合成并发布 dirty Components；不使用固定帧数节流。
- 从按下到松开的整个 stroke 严格只有一条 history record。Cancel、失焦、中途失败或 publication 失败必须回滚全部预览 mutation 并重新发布回滚结果。
- 正常 brush visual-feedback latency P95 `<= 150 ms`；单次 update 超过 work budget 时允许延后剩余 dirty work，但不得提交半个 generation 或产生额外 history。
- 保持现有外部修改、save/reload、history quarantine、scene-change preflight 和 immutable snapshot 原子性。

## Non-goals

- 不删除非破坏编辑层，不把所有 brush 直接烘焙进 Base Import。
- 不把 Terrain edit layer 变成逐 stroke 命令日志并在每次 composition 重放全部历史；持久化真源仍是 canonical sparse blocks。
- 不实现 GPU brush、compute composition、多人协同或跨进程实时编辑。
- 不把材质 8 lane 改成无限层、virtual texture 或材质 graph。
- 不在单纯选择 layerless asset 时自动 mutation；默认层只在第一次可提交的非空 stroke 内创建。
- 不允许 silent session replacement。当前 session dirty、conflict、file operation、pending composition 或 history quarantine 时，Hierarchy 选择另一 Terrain 必须 fail closed 并显示可操作诊断。
- 不用“每 N 帧刷新”替代 wall-clock/work-budget；低 FPS 与高 FPS 下交互合同必须一致。
- 不在本包修改 Graphics/RHI、Terrain shadow、streaming 或 performance baseline。

## Current implementation

- Entry points:
  - `TerrainModePanel::BindEventBus` 只处理 `EditorSelectionKind::Asset`，通过 AssetDatabaseService 提交 `SelectAsset` intent。
  - `ViewportPanelInteraction` 在 primary Scene viewport 生成 `BeginStroke / AddStrokeSample / EndStroke / CancelStroke` intent。
  - `TerrainEditorService::AddStrokeSample` 只 `push_back(raw_samples)`；`EndStroke` 一次性 apply、创建 patches、ScheduleComposition 并提交 history command。
  - `TerrainComposition` 逐层读取 `height_blend_mode`：Additive 执行 `H + value × coverage × strength`，Alpha 执行 `lerp(H, value, coverage × strength)`。
  - `.AshTerrain` v1 的 EditHeight block 存储 `values + coverage`，layer metadata 存储 Additive/Alpha。
- Modules:
  - Function Asset 定义 TerrainEditLayer、brush、patch、composition 和 container。
  - Editor Core/Service 维护 session、active stroke、pending composition、history 与 external-file state。
  - TerrainModePanel/ViewportPanel 只通过 UIContext 和 intents 交互。
- Known constraints:
  - 一次非空 mutation 只推进一次 content generation；完整 dirty set 必须原子 compose/publish。
  - stroke 在首次 mutation 前冻结 Base 和选中层及以下可见层，确定性 resampling 不依赖 frame index。
  - active stroke、pending composition、file operation 和 external conflict 有严格互斥/rollback 合同。

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Terrain layer data model | `TerrainSparseHeightBlock` 改为逐 sample `scale(a) + bias(b)` 仿射字段；`TerrainEditLayer` 移除用户语义上的 height blend mode，保留稳定 ID/name/visible/locked/strength | `Function/Asset/TerrainData.*`、`TerrainLayerStack.*` |
| Composition | 每层计算 `transformed = a × input + b`，再按 layer strength 从 input 插值到 transformed；finite/range guard fail closed | `Function/Asset/TerrainComposition.*` |
| Brush | 全部高度工具更新当前 layer affine transform：增量工具组合 bias，目标工具按 coverage 左复合现有 transform；保持 deterministic stroke resampling/seed | `Function/Asset/TerrainBrush.*` |
| Patch / history | height patch 保存 before/after affine sample；支持 incremental preview patch 聚合、逆序 rollback、最终单 command；默认层 creation 与 stroke 合并为 compound Terrain command | `Function/Asset/TerrainEditPatch.*`、Editor Terrain commands/core/service、UndoRedo tests |
| Container migration | `.AshTerrain` writer 升级为 v2；reader 支持 v1/v2。v1 Additive/Alpha blocks 精确转为 affine blocks，成功 load 不立即改写磁盘，下一次 Save/Optimize 写 v2 | `Function/Asset/TerrainContainerFormat.*`、`TerrainContainer.*`、tests |
| Incremental preview | ActiveStroke 保存 resampler continuation、未处理 raw samples、累计 forward/inverse patch、dirty set 与 last-preview wall time；到期只处理新增 segment，compose/publish 完整当前 generation | `editor/Services/TerrainEditorService.*`、`Core/TerrainEditorSessionCore.*` |
| Hierarchy auto-bind | TerrainModePanel deps 增加 SceneService/SelectionService 或窄的 Editor entity resolver；处理单个 Entity selection，读取 TerrainComponent path，经 AssetDatabaseService 解析为稳定 asset id 后复用 `SelectAsset` intent | `editor/App/PanelBootstrapper.*`、panel deps、`Panels/Terrain/TerrainModePanel.*`、services |
| Default workflow/UI | layerless session 显示可用笔刷；第一次非空 Begin/Add transaction 创建通用 `Edit Layer / 编辑层`。隐藏 Additive/Alpha 选择，文案区分 Edit Layers 与 Material Slots | Terrain Mode views/state、UIContext draw helpers、editor spec/tests |
| Specs / verification | 回写统一层数学、v1 migration、实时预览、direct binding 与人工中文 checklist | `docs/specs/features/terrain.md`、`docs/specs/modules/editor.md`、`docs/VERIFY.md`（若矩阵需补充） |

### API / contract changes

1. **Affine edit-layer math**
   - 每个 canonical height sample 的 layer transform 为 `T(H) = aH + b`，`a`、`b` 必须有限；identity 是 `a=1, b=0`，identity sample 不持久化。
   - 可见 layer strength `s` 的合成为 `H_next = lerp(H, T(H), s)`；隐藏层等价 `s=0`。strength 继续使用现有有限编辑合同。
   - Additive dab 的局部操作 `D(H)=H+d` 左复合当前 transform：`a'=a, b'=b+d`。
   - Smooth/Flatten 的目标操作 `D(H)=(1-c)H+c×target` 左复合当前 transform：`a'=(1-c)a, b'=(1-c)b+c×target`。因此同一层内 Flatten 后 Raise、Raise 后 Smooth 都严格保留 stroke 顺序。
   - Noise 是确定性 signed additive dab；Lower 是负 additive dab。所有组合在 double 中计算并在 float representable/finite guard 后提交。

2. **v1 → v2 exact migration**
   - v1 Additive sample `value=v, coverage=c` 转为 `a=1, b=v×c`；层 strength 保留，故 `H+s×v×c` 精确保持。
   - v1 Alpha sample转为 `a=1-c, b=c×v`；层 strength 保留，故 `lerp(H,v,c×s)` 精确保持。
   - v1 非有限、coverage 越界、重复 owner/rect 或非法 blend mode 继续 fail closed；不得猜测修复。
   - file header 写 v2；reader 保留 v1，未知更高版本拒绝。加载 v1 只在内存迁移，Save/Optimize 才原子写 v2。
   - migration oracle 覆盖 `s=0/0.5/1`、`c=0/0.5/1`、正负 additive、层排序/隐藏及边界值。

3. **Default layer transaction**
   - layerless asset 的 hover/preview 允许 Ready，但不 mutation。
   - 第一次 stroke 只有在出现首个有效、会改变 canonical value 的 dab 时才创建稳定 ID 的 `Edit Layer / 编辑层`；empty/cancelled/no-op stroke 不留下 layer。
   - layer creation patch 与累计 stroke patch 属于一个 compound command；Undo 同时移除 stroke 内容和自动层，Redo 恢复同一稳定 layer ID 与内容。
   - asset 已有选中可编辑层时不创建额外层；locked layer 明确拒绝 stroke，不静默切换。

4. **Incremental preview transaction**
   - Begin 冻结 asset/layer/config/metric 和 lower-stack source，建立一个 active transaction；期间 mode/tool/layer/asset 切换继续被拒绝。
   - resampler 保存 segment continuation，只处理上次 preview 后的新路径；不得每 80–100 ms 重放整条 raw path。
   - Flatten 的目标平面只在整笔首个有效 resampled dab 上从冻结 through-selected source 采样一次，并作为 active-stroke 状态传给后续所有 preview batch；禁止按 batch 的 `samples.front()` 重新采样。one-shot 与任意合法 incremental 分批必须得到相同 canonical affine block。
   - wall-clock deadline 默认 80 ms，允许 80–100 ms cadence；到期且无 pending publication 时 apply 新 segment。刷新条件不是 frame count。
   - 每次 preview mutation 产生新的 content generation，并以完整 dirty set 原子 compose/publish；同一 transaction 的 patch 在内存中按 sample identity 合并为“stroke 前值 → 当前值”。
   - history 在 End 前不入栈。End 等最后 composition/publication 成功后只提交一条 command；Undo/Redo 各自只推进一次对应 mutation generation。
   - Cancel、viewport 失焦、service shutdown、scene preflight、apply/compose/publish/history push 任一失败都按逆 patch 回滚到 Begin 前状态并发布回滚 generation。无法证明完整回滚时进入现有 history quarantine/read-only fail-closed 状态。

5. **Preview work budget and coalescing**
   - 每个 Editor update 只允许一个 active Terrain composition/publication；新的 cursor samples 在 pending 时继续累积，不启动重叠 worker或覆盖 pending identity。
   - 单次 preview 以 wall-clock/dirty-Component budget 限制；超预算部分留到下一次 update，不能发布只包含 dirty set 子集的 snapshot。
   - latency 记录从 sample 被 service 接收到对应 generation 成为 published snapshot；正常负载 P95 `<=150 ms`。超过门槛记录 telemetry/warning，不靠降低 history 原子性追赶。

6. **Hierarchy direct binding**
   - 只接受一个有效 Entity selection，且 entity 有合法 TerrainComponent/path。多选、无 Terrain、非法 path 不替换当前 authoring session。
   - path 通过 AssetDatabaseService canonical resolve 成稳定 Terrain asset id，再提交现有 `SelectAsset` intent；Panel 不直接打开容器或修改 service internals。
   - 同 asset selection 为 no-op，保留 pending load、drafts、active tab 和 diagnostics。
   - 选择另一 Terrain 时沿用现有 session replacement contract：dirty/conflict/file operation/pending composition/quarantine 阻止切换并显示原因；绝不 silent discard。
   - Terrain Mode 打开时会同步检查当前 primary selection，解决“先选 entity、后开 panel”没有新 event 的情况。
   - authoring session 的加载/dirty 状态与 viewport tool ownership 分离。视口只有在 Terrain Mode 面板打开，且当前恰好单选当前 Terrain Entity 或当前 Terrain asset 时，才允许 Sculpt/Paint 接管 LMB、W/E/R 和 brush overlay。
   - 面板关闭、空选、多选、选择其他 asset/entity 或 Terrain asset identity 失配时，保留已加载 working set、dirty/history、tab/config 和 diagnostics，但立即清除 preview 并归还 gizmo/selection 输入；若当时有 active stroke，必须按既有 Cancel 合同回滚整笔后再失活。

7. **Terminology and UI**
   - `Edit Layers / 地形编辑层`：非破坏 stack，可排序/隐藏/锁定/强度。
   - `Material Slots / 材质槽`：固定 8 个 surface lanes；Paint/Erase 选择槽位，不把它们显示成可排序 edit layer。
   - 普通 Sculpt/Paint 页面不显示 Additive/Alpha；旧 v1 layer migration 后统一显示为普通编辑层。

### Backend impact

- authoring、affine composition、migration、patch 和 selection 都在 Function Asset/Editor CPU 层；Vulkan/DX12 消费同一 immutable composed Component snapshot。
- 实时 publication 会更频繁触发 TerrainRenderAsset dirty Component upload；依赖 runtime SDD 的有界 upload/prepared draw 后再启用。
- 不增加 shader binding 或 RHI API。两后端仍必须验证连续 preview generation、atlas update、shadow cache invalidation 和 clean shutdown。

### Performance

- affine composition 与现有 value+coverage 同为每 sample 常数级运算，不增加 block 渐近复杂度；canonical block仍按 owner Component 稀疏存储。
- incremental preview 只处理新增 resampled dabs和受影响 Components；禁止 O(total stroke history) 重放。
- production-size Base Import 在 authoring session 内不可变；`TerrainWorkingSet`、打开时的 source snapshot 与所有 preview publication 必须共享同一 Base R16 allocation。笔刷只修改稀疏 edit layers，禁止为每次预览复制 8193² 高度数组。
- visual feedback latency P95 `<=150 ms`；一次拖动 history count delta 必须为 1。
- Standard PerfGate threshold 不调整。实时笔刷是人工/定向 workload，不用 Standard 平均值掩盖交互 hitch。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| Function focused | affine composition/left-composition、v1 exact migration、patch merge/rollback、deterministic brush | `RunTests.bat Debug --test-case="*Terrain*"` 精确 filters |
| Editor focused | entity auto-bind、panel/selection authoring eligibility、失活中途整笔 cancel、layerless first stroke、single history、80ms wall-clock、跨 batch 固定 Flatten target、coalescing、cancel/failure rollback、dirty/conflict block | Terrain editor/service/contract doctest filters |
| 全量单测 | Debug/Release、legacy bridge、内存释放 | `RunTests.bat Debug`；`RunTests.bat Release` |
| 构建 | Editor/Sandbox + container consumers | `build_editor.bat Debug/Release`；`build_sandbox.bat Debug/Release` |
| 架构/计划 | UIContext-only、层级依赖、SDD/spec | `RunArchGate.bat`；`AIDevDoctor.ps1 -Mode ValidatePlan` |
| Readiness | 频繁 Terrain publication 后双后端完整就绪 | `run.bat all Debug --smoke-test-seconds=120` |
| Validation | 连续 dirty upload/atlas/shadow-cache generation | Vulkan validation + DX12 debug layer/GPU validation 定向 Terrain run |
| 渲染回归 | composed snapshot/overlay/双后端 | `RunRenderGate.bat`（non-bless） |
| 性能 | 通用回归与 runtime SDD Terrain A/B | `RunPerfGate.bat -Profile Standard` + non-bless Terrain diagnostic |
| 人工签署 | Hierarchy 直达、首次落笔、实时反馈、全部 7 工具、单条 Undo/Redo、Layers 高级操作 | 用户按中文 checklist 在 Vulkan/DX12 亲自操作；AI 不代签 |

## Task breakdown

1. 写 affine math、v1 migration 和 tool-order RED；实现 Function Asset data/composition/brush，不接 UI。
2. 升级 container v2 writer + v1/v2 reader，补原子 Save/Optimize、recovery/revision 和 golden-free round-trip tests。
3. 扩展 height patch 为 affine before/after，验证普通 command undo/redo 与 layer reorder/strength/visibility。
4. 实现 active-stroke incremental transaction、wall-clock cadence、patch aggregation、完整 rollback；先用 fake clock/publication seam 完成 RED/GREEN。
5. 实现 layerless first-stroke compound command；空/no-op/cancel 不留 layer，非空 stroke history delta=1。
6. 为 TerrainModePanel 注入窄 entity resolver/Scene+Selection deps，完成先选后开、开后换选、dirty/conflict/multi-select 合同。
7. 收敛 UI 文案与默认 tab/selection，移除普通流程的 Additive/Alpha 控件，区分地形编辑层和材质槽。
8. 完成全量 CPU/build/ArchGate/AIDevDoctor 后协调 GPU，串行双后端 readiness/validation/RenderGate/PerfGate；最后由用户人工签署。
9. 回写 Terrain/editor spec、本 SDD validation record；v1 migration、实时 preview 或直接编辑任一未签署不得标记 Done。

## Validation record

- 2026-07-16 首次人工 Vulkan 签署确认单笔 history 与中途取消有效，但实时雕刻仍表现为松手后才更新，编辑模式约 10 FPS，松手后还需等待数秒。该会话启用了 GPU-assisted/core validation，因此帧率不能作为正常模式基线；但 publication 延迟属于真实缺陷。
- 根因是 `make_terrain_working_set` 在打开 authoring 时复制整张 Base 高度图，`publish_terrain_working_set` 又在每个约 80 ms 的 preview generation 同步复制一次。production 8193² R16 为 67,125,249 samples，单次复制约 128 MiB；该主线程复制足以阻塞后续 preview dispatch并造成大幅内存带宽/峰值压力。
- RED/GREEN 以 Base pointer identity 锁定结构合同：working set 与 source snapshot 共享 Base，dirty publication 后新 snapshot 仍共享相同 Base；mutable edit-layer stack 和 dirty Component snapshot 继续保持独立/不可变发布。
- 修复后的自动证据：Debug/Release full tests exit 0（Debug 491/491、25,987 assertions）；Editor/Sandbox Debug/Release build PASS；ArchGate PASS（35 条既有 legacy WARN）；AIDevDoctor PASS；Terrain scene 四组合 Debug readiness exit 0；non-bless RenderGate `20260716-122708-552-69308-316ad9bb` PASS；Standard PerfGate `20260716-122813-2933376-2158fb09` PASS。20 份 fresh runtime logs 拒绝词为 0，四份运行配置逐字节恢复，未 bless baseline/golden。
- 上述自动证据不代替人工实时交互签署。最终 Done 仍要求用户在正常非 validation Vulkan/DX12 Editor 中确认拖动期间约 80–100 ms 可见更新、松手无数秒 stall、完整拖动一条 history，并记录正常模式帧率。
- 2026-07-16 正常非 validation Vulkan 人工复测确认：编辑模式约 60 FPS，靠近 Terrain 原点时可在拖动中看到预览，`Ctrl+Z` 正确撤销整笔；远离原点的 Component 仍只在松手后刷新。根因不是 brush 坐标或 layer transaction，而是上一 preview generation 尚为 `Pending` 时，`TerrainRenderAsset::accept_snapshot` 把 pointer-equal 的全部 1024 个 resident Component 重新按 row-major 排队；每代只在 4 MiB / 2 ms budget 内从 `(0,0)` 开始消费，连续约 80 ms generation 因而反复让近端先完成、远端饥饿。
- 跨代 coalescing 修复改为只携带真正未完成的 height/weight/reset/removal，并重绑新 generation；已完成且 pointer-equal 的 height 直接复用，resident weight slot 原地重绑，新变化尾插。RED 证明部分完成后旧逻辑会重新产生 3 个 height work 且丢失 pending reset；GREEN 为 14/14 render-asset tests、151/151 assertions。失败恢复仍保留全量重建。
- 编辑模式剩余 CPU 热点是每帧 brush cursor 的精确 Terrain ray cast：旧路径会对全部 XZ 候选先分配并深验 min/max 层级。现保留所有候选的常数级 shape fail-closed 校验，按 XZ entry 下界前到后遍历，仅对可能优于已确认最近命中的候选执行深验；far min/max 损坏可在安全 early-out 后跳过，far shape 损坏仍立即 `Failed` 且不写输出。拖动期还删除了 service 在每次增量合并前对持续增长 aggregate patch 的外层重复深拷贝；merge API 自身继续用局部副本提供强异常保证，late-failure 回归精确验证解码后的重叠 source 不连续时 aggregate 全字段不变。focused ray tests 为 4/4、31/31，focused merge test 为 1/1、55/55；最终帧率与远端可见 cadence 仍需人工重测。
- 远端修复后的最终自动证据：Debug/Release full tests 均为 496/496 PASS、1 skipped、26,066 assertions；Editor/Sandbox Debug/Release build PASS；ArchGate PASS（35 条既有 legacy WARN）；AIDevDoctor PASS。四组合 Debug readiness exit 0；non-bless RenderGate `20260716-152459-779-58936-495b311c` PASS；Standard PerfGate `20260716-152553-5984733-eb4acc47` PASS；Release Empty PerfGate `20260716-152906-0383313-a583cec9` PASS。28 份 fresh runtime logs 拒绝词为 0，四份运行配置逐字节恢复，performance baseline SHA-256 保持 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214`，未 bless baseline/golden。上述结果仍不代替远端实时笔刷与正常模式帧率的人工签署。
- 远端实时笔刷人工通过后，用户继续发现 Flatten 在多个 80 ms preview batch 间改变目标，以及关闭 Terrain 面板或改选其他对象后仍可刷旧 Terrain。根因分别是 Engine 每批从 `samples.front()` 重采目标、以及 viewport ownership 仅检查 Sculpt/Paint mode。修复后 active stroke 持有一次采样的 `TerrainBrushStrokeTargetState`；面板/单选/资产 identity 成为 query、overlay、LMB、Gizmo 与 W/E/R 的共同 fail-closed predicate，失活只取消当前笔画并保留 dirty authoring session。
- 新增 RED/GREEN 覆盖 one-shot/增量 Flatten canonical 等价、首批与连续零压力 target anchor、全零压力不创建层且 generation/clean identity 不变、empty/failure/非 Flatten 状态边界、panel/selection/asset identity 真值表、press latch、失活中的整笔取消和 dirty session 保留。最新 Flatten focused Debug/Release 为 2/2 cases、25/25 assertions，Editor focused Debug/Release 为 65/65 cases、1,604/1,604 assertions，viewport/context focused Release 为 13/13 cases、215/215 assertions；修复合入后的 Debug full 为 505/505 cases、26,222/26,222 assertions；Editor Debug/Release build PASS；ArchGate PASS（35 条既有 legacy WARN）；task-scoped `git diff --check` PASS。两项修复分别完成规格与代码质量独立审查，最新 P1 复审同样为 P0/P1/P2=0 CLEAN。该证据尚不代替统一 full Release、readiness、双后端 gate 与人工 Vulkan/DX12 重签。
- 最终 staged review 发现并关闭两个 P2：长期 spec 把增量重采样 API 写成不存在的旧名，现已统一为 `append_resampled_terrain_stroke`；production selection composition 原先缺少可执行 seam，现由 UI-free `TerrainViewportInputRouter` 按 `kind + id` 判定单选 identity，并通过 Entity/Asset 双 resolver 表执行真实分派，label/path 不参与 identity。生命周期测试直接用该 seam 的返回值驱动 begin → context-loss cancel → held reopen 不重入 → release latch → reselect/new press；RED 为缺少 resolver seam 的编译失败，GREEN 后 Debug/Release focused 与 production source contract 均通过。
- Flatten/tool-eligibility 精确最终字节自动矩阵（2026-07-16）：Debug/Release full tests 均为 506/506 cases PASS、1 skipped（Release 26,281/26,281 assertions）；Editor/Sandbox Debug+Release build PASS；ArchGate PASS（35 条既有 legacy WARN）；AIDevDoctor PASS。task-scoped staged diff 检查独立执行，明确排除用户所有的 `EditorSettings.json`/`imgui.ini` 既有行尾空白。四组合 Debug readiness exit 0；non-bless RenderGate `20260716-180801-861-60820-14bab628` PASS（sandbox Vulkan/DX12/cross SSIM `0.996278/0.996177/0.999747`，particles 三项均为 `1`）；Standard PerfGate `20260716-180903-8285548-5069795c` 四组合 PASS、无 warning/failure；Release Empty PerfGate `20260716-181234-2381280-397eb972` 双后端 PASS，Vulkan CPU/GPU P95 `0.8798/0.7950 ms`、DX12 `0.9398/0.8318 ms`，均低于不可 bless 的 `3.33 ms` 硬上限。28 份 fresh Engine/Application 日志与 25 份门禁/构建日志合计 53 份，拒绝词与真实 leak 命中均为 0，runtime generic error 为 0（构建日志中的 22 个 `error` 文本均为 MSBuild 固定的 structured-output 说明）。四份运行配置逐字节恢复为 Engine `FF5E59...AE645`、EditorSettings `F9D4BD...19F6D`、ViewportLayout `91757A...9D43`、imgui `4C0499...FE44`；performance baseline SHA-256 保持 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214`，effective roots 为 0，未 bless baseline/golden。最终 Done 仍要求用户在同一最终提交上完成 Vulkan/DX12 人工重签。

## Risks

| Risk | Mitigation |
| --- | --- |
| affine 组合数学或 stroke 顺序错误 | 用函数级 composition oracle和随机有限样本验证左复合；覆盖 Raise→Flatten 与 Flatten→Raise |
| v1 资产迁移改变画面 | 逐 sample exact oracle；加载 v1 后 old/new composition 对比；不自动写盘 |
| 频繁 preview 产生多条 history | End 前 history count 不变的机械断言；只提交 aggregate command |
| Cancel/失败留下部分 Terrain | before/after aggregate inverse + 完整 dirty publish；无法证明时 quarantine，不伪装成功 |
| 80ms 更新造成主线程 hitch | 只处理新增 samples、单 composition in-flight、wall-clock/dirty budget；runtime SDD 先落地 |
| 自动层让 mere selection 变 dirty | layer 只在首个非空 mutation 内创建；hover/open/no-op/cancel tests |
| Hierarchy 换选丢失 dirty session | 复用 SelectAsset/session replacement gate；Panel 不直接 Close/Open working set |
| 两套“层”仍让用户困惑 | UI/文档统一使用“地形编辑层”和“材质槽”，移除 Additive/Alpha 普通控件 |

## Open questions

- 无。用户已选择方案 2：一个普通编辑层支持全部雕刻工具；保留完整非破坏 stack，但普通工作流自动绑定和按需创建默认层。
