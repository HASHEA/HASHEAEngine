# SDD-2026-07-22-terrain-variable-create-size: 可配置 Terrain 创建与导入尺寸

## Status

Implemented — Automated verification passed; named human sign-off pending

用户于 2026-07-22 复核并批准本书面 SDD；后续实施必须遵守本文范围和分批验证合同。任何扩大公共 RHI/RenderGraph API、改变 Terrain schema 或放宽原子回滚语义的方案都需要先修订 SDD 并重新批准。

风险级别：**S2**。本变更同时修改 Editor authoring contract、Terrain render-resource sizing、CPU/HLSL 常量绑定以及 Vulkan/DX12 共用的渲染行为；必须在本 SDD 经用户书面复核批准后实施。本文批准前只允许提交设计文档，不修改生产代码。

截至 2026-07-26，Task 1–8 的实现和自动验证已完成：authoring normalization、共享 Create/Import target、动态矩形资源寻址、240-byte surface constants、完整 candidate replacement，以及 published snapshot/resources/bounds 的同代切换均已落地。首次 load 在 published view 建立前保持 Pending；同路径异步 load、validation、allocation、height、coarse 或 initial-atlas failure 保留旧 published view，实际路径变化才清除；Scene presentation 在 visible frame 构建前捕获 asset epoch，并分别发布 Terrain content-ready 与通用 capture-ready，plain smoke 不再能在异步 Terrain 仍 Pending 的 0-draw 帧提前成功。min/rect/default runtime fixture 由精确环境 token 生成到 ignored `Intermediate/generated-fixtures`，最大 case 继续使用显式 8193² TerrainGate；长期规格与具名人类 Vulkan/DX12 清单已同步。自动证据全部通过，但 `docs/templates/TerrainEditorManualSignoff.md` 的具名人类双后端交互签署尚未完成；按本文已批准的停止条件，本 SDD 在收到该签署前不标记 Done。

## Automated verification evidence — 2026-07-26

- fresh `generate_vs2022.bat` 成功；Editor、Sandbox 的 Debug/Release 定向构建均退出 0，Debug `AshImageDiff` 工具目标也成功生成。
- Debug 全量 `Tests.exe`：621 passed、2 skipped、30,823 assertions；`RunTests.bat Release` 构建与全量执行退出 0。新增回归先证明 plain smoke 在 `scene_packets_terrain_ready=0` 时保持 Running，之后才接入生产计数。
- `RunArchGate.bat` PASS（35 条既有 legacy warning，无新增）；最终 `AIDevDoctor.ps1 -Mode ValidatePlan` PASS，报告位于 `Intermediate/test-reports/ai-dev/20260726-065522/`。
- readiness：Editor default Vulkan/DX12，加 Sandbox min/rect/default/max Vulkan/DX12，共 10/10 退出 0。首个修复后 Vulkan default 由旧的 Terrain Pending、0 draw、第 4 帧误成功，变为 Pending → generation 1 recovered → 第 217 帧成功；最大 TerrainGate 在普通 Vulkan/DX12 分别到第 4739/4459 帧才放行。
- validation：通过 `cmd.exe /d /s /c` 保留 `--perf-gate-validation=on` 单 token，min/rect/default/max × Vulkan/DX12 共 8/8 退出 0。18 份精确 runtime log 均包含 Terrain recovered 与 readiness success；8 份 validation log 均记录 `validation=on`，没有 error/critical、VUID、GPU validation、device lost、fatal、assert、access violation 或 bad leak。
- resource byte oracle 由生产 sizing helper 的单测精确锁定：min 1×1 height 132,100 B、coarse 33×33 RGBA8 4,356 B；default 8×8 height 8,454,400 B、coarse 257×257 RGBA8 264,196 B；rect 8×16 height 16,908,800 B、coarse 257×513 RGBA8 527,364 B；max 32×32 height 135,270,400 B、coarse 1025×1025 RGBA8 4,202,500 B。固定高分辨率 atlas 仍为 4144×4144、256 slots。
- non-bless `RunRenderGate.bat` PASS，报告位于 `Intermediate/test-reports/render-gate/20260726-144643-892-68976-41fb0bcb/`：sandbox Vulkan/DX12 golden SSIM 0.996278/0.996177，跨后端 0.999747；particles 三项均为 1.0。
- `RunPerfGate.bat -Profile Standard` PASS、无 WARN，报告位于 `Intermediate/test-reports/perf-gate/20260726-144837-035-73756-e03d5938/`；未 bless baseline。
- 审计确认 `Engine.ini`、`EditorSettings.json`、`ViewportLayout.json` 恢复为运行前 SHA-256，`imgui.ini` 的本轮 Terrain window 持久化噪声已移除；4 张 render golden 与 `perf_gate_baselines.json` 的 SHA-256 均与运行前快照一致。
- 待办仅剩具名人类在 Vulkan/DX12 按 `docs/templates/TerrainEditorManualSignoff.md` 完成 Target Size、Create/Import、矩形最终画面和 reload failure retention 的真实 UI 签署；Agent 不代签。

## Context

当前 Terrain Mode 的 Create Flat 和 Import Heightmap 都把目标布局固定为 `make_default_terrain_grid_layout()`：8193 × 8193 个 sample、8192 × 8192 m、32 × 32 个 Component。该布局保留了 8 km 能力，却让普通 authoring 默认承担完整地形的加载、内存和编辑成本；UI 也没有尺寸入口。

Terrain 资产和 CPU authoring API 已以 `TerrainGridLayout` 表示 X/Z 独立的 sample/component 数量，`.AshTerrain` 也已经持久化该布局。主要缺口位于渲染路径：Component 线性索引、packed height buffer、coarse weight target、atlas update shader 和 surface shader 仍假定每轴固定 32 个 Component。因此本变更不能只给 Editor 增加两个输入框，必须让既有渲染路径在批准上限内真正按资产布局工作。

用户已确认以下产品合同：X/Z 尺寸独立配置；每轴允许 256–8192 m 的 2 的幂；默认 2048 × 2048 m；固定 1 m/sample 和 256 × 256 quad/Component；Create Flat 与 Import Heightmap 共用目标尺寸；用户手填数值，在提交编辑时自动吸附到最近合法值，精确中点向上。

## Goals

- Terrain Mode 提供共享的 Target Size X / Z 数值输入，不使用预设下拉框。
- 每轴 authoring extent 为 `[256, 8192]` m 内的 2 的幂，X/Z 可不同，默认均为 2048 m。
- 固定 `sample_spacing_meters = 1`、`component_quad_count = 256`，由 extent 唯一推导 sample/component 数量。
- Create Flat 与所有高度图格式的 Import 共用同一目标布局；Import 的 source width/height 仍是独立源尺寸。
- renderer 接受合法的 1–32 × 1–32 Component 矩形布局，资源字节数和 coarse target 随实际布局缩放。
- 同一资产 reload/reimport 后布局变化时，GPU 资源以两阶段方式原子切换；失败保留旧的可渲染资源。
- 既有 8192 × 8192 m Terrain、`.AshTerrain`、Scene schema、TerrainGate 全尺寸压力 fixture 保持兼容。
- Vulkan 与 DX12 使用相同的 Function 层布局、常量和 shader 逻辑。

## Non-goals

- 不支持小于 256 m、大于 8192 m、非 2 的幂或非整数米的 authoring target。
- 不开放 sample spacing、Component quad 数量或每 Component 分辨率配置。
- 不在本变更中增加 Terrain resize-in-place、已有资产重采样命令或 Scene Inspector 尺寸编辑。
- 不改变 height/weight 编码、8 个材质槽、编辑层、笔刷、LOD 算法、Terrain transform 或资产 schema。
- 不把整个 Terrain 的全部权重常驻进高分辨率 atlas；既有 256 槽 LRU 策略保持不变。
- 不新增 RHI/RenderGraph 公共 API，不增加第三方依赖，不刷新 golden/perf baseline。
- 不把 UI 自动化当作人工交互签署；Agent 只执行自动门禁并移交人工清单。

## Baseline at approval

- Entry points:
  - `TerrainModeWidgets.cpp` 的 Create/Import UI 只收集路径、高度范围、flat height 和源图参数。
  - `TerrainModeState::BuildCreateDesc` 与 `BuildImportDesc` 都无条件写入 `make_default_terrain_grid_layout()`。
  - `TerrainCreateAssetDesc` 和 `TerrainHeightImportDesc::target_layout` 已能携带 `TerrainGridLayout`。
- Asset/CPU:
  - `TerrainGridLayout` 已包含 X/Z sample/component 数量、Component quad 数量和 spacing。
  - 容器、import、compose、query 与 LOD 大多按 layout 工作；合法关系为 `sample_count = component_count × component_quad_count + 1`。
- Render:
  - `TerrainRenderAsset` 只接受 8193/32 布局，并以 32 为 Component row stride。
  - packed height buffer 以最大 1024 个 Component 分配；coarse weight target 固定为 1025 × 1025。
  - `TerrainCommon.hlsli`、`TerrainAtlasUpdate.hlsl` 和 Terrain surface root constants 假定每轴 32 个 Component。
  - 高分辨率 weight atlas 是 16 × 16 个驻留槽，共 256 槽；它是可见/活跃 Component 的 LRU cache，不等于全地形 Component 表。
  - same-layout snapshot table 允许用 null Component 表达既有 residency removal；layout replacement 不能复用该增量形状，必须从完整替换快照构造候选。
- Known constraints:
  - renderer 的批准上限仍为每轴 32 个 Component。
  - Component 内部仍固定 257 × 257 sample，packed words/Component、共享 index grids 和 9 级 LOD 可保持不变。
  - root inline constant capacity 当前足以增加一个 16-byte layout vector，但 C++/HLSL byte layout 必须由契约测试锁定。

## Proposal

### Canonical layout contract

共享 helper 从 authoring extent 推导 `TerrainGridLayout`：

```text
extent_x_m, extent_z_m ∈ {256, 512, 1024, 2048, 4096, 8192}
sample_spacing_meters = 1
component_quad_count = 256
component_count_x = extent_x_m / 256
component_count_z = extent_z_m / 256
sample_count_x = extent_x_m + 1
sample_count_z = extent_z_m + 1
```

因此最小布局为 257 × 257 sample / 1 × 1 Component；默认布局为 2049 × 2049 / 8 × 8；最大布局仍为 8193 × 8193 / 32 × 32。矩形布局合法，例如 2048 × 4096 m 对应 2049 × 4097 sample 和 8 × 16 Component。

Editor authoring 默认值改为 2048 × 2048 m。既有 `TerrainGridLayout{}` 和历史 full-size helper 不静默改义；需要最大压力布局的 TerrainGate、容器兼容测试和既有 fixture 改用或继续使用显式 8192 m/full-size 构造。新增/命名清晰的 authoring layout helper 是 UI 和 descriptor 默认的单一真源，避免把“authoring 默认”与“支持上限”再次混为一谈。

公共资产验证仍接受满足现有 layout 不变量的受支持数据；render contract 限定 `component_quad_count == 256`、spacing 为 finite positive、每轴 Component 为 1–32。Editor 本次只生成上述 2 的幂、1 m spacing 子集。

### Numeric input and normalization

Create 与 Import 区域共用 `target_extent_x_m` / `target_extent_z_m` 两个 draft，不复制两套状态。UI 显示普通可编辑数值，不提供下拉预设。

- 用户输入期间保留原始文本/数值，不在每个按键后跳动。
- Enter 或控件失焦时，先解析整数，再 clamp 到 `[256, 8192]`，最后按数值距离吸附到最近的 2 的幂；距离完全相等时选较大的值。
- 点击 Create Flat 或 Import 时再次执行同一 normalization，防止未触发失焦或程序化 intent 绕过。
- 无法解析为整数的 draft 保持可编辑但阻止操作，并显示明确错误；不得沿用旧值悄悄提交。
- 规范化示例：`100→256`、`300→256`、`384→512`（精确中点向上）、`500→512`、`3000→2048`、`3500→4096`、`9000→8192`。
- UI 在输入下方只读显示 `Samples X × Z`、`Components X × Z`、`1 m/sample`，让实际成本在创建前可见。

normalization 与 layout construction 放在无 UI 依赖的可测试 helper 中；Widgets 只负责 draft 生命周期、错误展示和 intent 触发。

### Create and import data flow

Create Flat 以共享目标 layout 构造 `TerrainCreateAssetDesc`。Import Heightmap 以同一 layout 写入 `TerrainHeightImportDesc::target_layout`；source width/height 继续表示文件原始维度，不与目标尺寸联动。

- Reject：源尺寸必须精确等于目标 sample count。
- Crop：沿用既有确定性中心裁剪规则，并以目标 sample count 为裁剪结果。
- Catmull-Rom：沿用既有确定性重采样规则，把完整源图重采样到目标 sample count。

PNG、RAW R16、RAW R32F、EXR 使用同一 target layout；格式、byte order、flip、EXR channel 和高度映射语义不变。Create/Import 的异步作业在 dispatch 前捕获已经规范化且完整验证的 layout；运行期间继续遵守取消、内存预算、staged-file 和原子发布合同。

### Dynamic render resources

`TerrainRenderAsset` 从活动 snapshot layout 得到 immutable render-layout descriptor，并把所有全地形寻址统一绑定到该 descriptor：

- Component linear index 使用 `coord.z * component_count_x + coord.x`。
- Component table、upload/readiness 计数和 dirty lookup 使用实际 `component_count_x × component_count_z`。
- packed height buffer 分配：`actual_component_count × height_words_per_component × 4 bytes`。
- coarse weight target 尺寸：

```text
coarse_width  = component_count_x × 32 + 1
coarse_height = component_count_z × 32 + 1
```

  其中 32 是每个 256-quad Component 按 8 sample 步长下采样得到的 coarse cell 数；末 Component 继续拥有 +X/+Z 最终边界。
- SceneRenderer/RenderGraph import 使用 render target 的实际 width/height，不复写 1025 常量。
- CPU 的最大容量常量可保留用于 bounded array/validation，但错误消息、循环和有效索引不得再假定 1024 个实际 Component。

新增纯值 `validate/derive render layout` seam，一次 checked arithmetic 后输出 Component count、dense row stride、height bytes 和 coarse width/height；RenderAsset、RenderPass、错误报告和 tests 共用该结果，禁止各处重复推导。所有 snapshot 的 table size 必须精确等于 `component_count_x × component_count_z`，每个非空项必须是 row-major、坐标匹配的 257² Component；缺项/错序 fail closed。同一 layout 下，凡按现有 `(content_generation, residency_revision)` 排序规则被接受的更新，都继续允许合法 null removal，不要求 residency revision 单独递增。asset replacement 或 layout-changing candidate 必须来自完整非空替换快照，null/incomplete replacement fail closed，不能把旧布局 residency state 搬到新 dense index 空间。

高分辨率 weight atlas 保持固定 256 槽、既有 slot extent 和 LRU；较小 Terrain 只会使用更少槽，最大 Terrain 仍按可见/活跃集换入。共享 index grids、每 Component 上传 payload、weight staging bytes 和 LOD level 数量也保持固定。

### C++ / HLSL binding contract

Terrain surface root constants 新增一个 16-byte layout vector，至少携带 `component_count_x`、`component_count_z`、`sample_count_x`、`sample_count_z`。C++ `TerrainSurfaceConstants` 与 HLSL `AshRootConstants` 必须逐字段、逐偏移一致，并保持在 inline constant capacity 内。

Atlas update constants 使用现有 padding 空间携带 `component_count_x/z`，保持当前 32-byte 总尺寸。`TerrainAtlasUpdate.hlsl` 用动态末 Component 坐标判断最终边界；coarse pixel 的每 Component stride 仍为 32。

`TerrainCommon.hlsli` 的 height word linear index、global sample clamp、world/weight UV 计算都读取动态 layout，禁止保留 `coord.z * 32`、`sample <= 8192` 或 `/8192` 等全地形魔数。GBuffer、shadow、LOD debug 共用同一 surface shader contract，因此两后端和全部 Terrain pass 同步生效。

本变更只扩展已有 Terrain 私有 root constants，不修改公共 RHI、descriptor set/root signature API 或 RenderGraph API。

### Layout-changing reload and rollback

同一 asset path 在 reload/reimport 后可能从一种合法 layout 变为另一种。布局变化不能在旧资源上原地改尺寸，也不能先销毁旧资源。候选/发布身份为 `asset_id + content_generation + residency_revision`：同 asset ID 才按 generation/revision 单调比较；asset ID 变化按完整 replacement 处理，禁止拿新资产重置后的 generation 与旧资产直接排序。

一次 pass 只取得一个 coherent published view，内含 snapshot、layout derivation、height/staging buffers、两张高分辨率 weight atlas、coarse target、slot metadata、upload queues 和 published identity；禁止通过多个 getter 分别读取而在 swap 中混合代际。fallback material arrays 等真正 immutable 且与 layout 无关的资源可以共享。发布流程为：

1. 验证新 snapshot 和 layout，计算所有尺寸并执行 overflow/budget 检查。
2. 构造独立 candidate resource set（height/staging buffers、两张 weight atlas、coarse target、slot metadata 和 queues），完成全量 height 与全量 coarse upload；候选不得写入 active atlas。
3. 冻结确定性的 initial high-resolution resident set：取 latest published frame 已 resident 或已 required 的 Component，过滤到候选布局中的非空坐标，沿用既有 LRU/稳定坐标优先级去重并截断到最多 256 项。候选为该集合分配稳定 slot metadata，并在自己的两张 atlas 中完成全部 upload/dispatch。没有 published view 的首次加载沿用现有首次可见集准备流程；published 合法交集为空时，空集合本身就是无画质降级的初始集合。
4. candidate 的全量 height/coarse 与 initial resident set 全部 ready 后，才在 frame boundary 原子替换 active snapshot + render layout + resource set。任一 initial atlas upload/dispatch 失败都丢弃 candidate 并继续显示旧 view。最大地形仍只有 256 个高分辨率 LRU 槽；swap 后因相机移动/可见集变化产生的新请求沿用既有 LRU 和 coarse fallback。自动化 readiness 继续等待当前可见集的 atlas/upload activity 收敛，不能把“资源 bundle 已切换”冒充最终 capture ready。
5. 旧资源进入既有 deferred destruction/lifetime 路径。
6. 任一分配、upload、validation 或 publication 失败时丢弃 candidate，active 旧 snapshot/resources 继续可见；readiness/error 暴露“candidate failed but published view remains usable”，不能把它误报成无资源终态，也不能发布半新半旧状态。

RenderScene/Terrain proxy 在候选期间继续使用 published snapshot 的 bounds、draw data 和 resource identity。只有 frame-boundary swap 才同时推进 visible-frame Terrain generation、TAA temporal signature 和 directional-shadow cache identity；候选到达不能提前让新 bounds 裁剪旧资源，也不能提前失效后在真正 swap 时漏掉失效。没有任何 published view 的首次加载失败才允许按现有 fail-closed 规则移除/拒绝 proxy。

同一 layout 下按现有 `(content_generation, residency_revision)` 排序规则被接受的更新继续走增量上传与合法 null removal 路径。实现需要故障注入证明 candidate height/staging/atlas/coarse 创建、height/coarse upload 和 initial resident atlas dispatch 失败都不会破坏旧 published view或形成混代；swap 后因新相机需求触发的普通 LRU upload 失败保持 coarse/fallback 且 readiness 不成功。

### Error handling

UI 输入错误直接关联 X/Z 字段。AssetManager 不得吞掉 render acceptance 错误；资产/renderer 拒绝必须传递到 Scene/readiness，并包含 asset path、收到的 `samples/components/quads/spacing` 布局及精确原因，例如超出 32 Component 上限、sample/component 关系不成立、资源尺寸溢出或 candidate upload 失败。

非法、超限、table size 不完整、replacement null/错序或 C++/HLSL 不一致均 fail closed；same-layout residency removal 的合法 null 不得被误拒绝。禁止退回 8192 m、截断 Component、只画部分地形或静默改 spacing。异步失败保留既有目标文件、当前 authoring session 和旧可渲染资源。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Terrain asset contract | 增加 authoring extent 常量/helper，区分 2048 m authoring 默认与 8192 m 上限/full fixture | `Function/Asset/TerrainData.*`、相关 tests |
| Editor state/core | 共享 X/Z target draft、规范化、Create/Import descriptor 和错误状态 | `editor/Panels/Terrain/TerrainModeState.*`、`TerrainEditorSessionCore.*` |
| Editor UI | 手填尺寸、失焦/Enter commit、derived layout 展示、操作前防御性规范化 | `editor/Panels/Terrain/TerrainModeWidgets.cpp`、UI-focused tests |
| Render asset | 动态 layout validation/indexing/buffer/coarse target、两阶段资源切换 | `Function/Render/TerrainRenderAsset.*`、`RenderAssetManager.*`（仅必要接线） |
| Render scene/proxy | coherent published snapshot/bounds、candidate failure 分类、swap 时 temporal/shadow identity | `Function/Render/RenderScene.*`、`TerrainRenderProxy.*`、`SceneRenderer.cpp` |
| Render pass/scene | 动态 Terrain private constants 和实际 target extent import | `Function/Render/TerrainRenderPass.*`、`SceneRenderer.cpp` |
| Shaders | 动态 Component/sample 边界、索引、coarse ownership 和 UV | `Shaders/Terrain/TerrainCommon.hlsli`、`TerrainAtlasUpdate.hlsl`、`TerrainSurface.hlsl` |
| Tests/fixtures | normalization、矩形布局、资源 sizing、rollback、双后端 fixtures；保留最大 TerrainGate | `project/src/tests/Editor/`、`project/src/tests/Terrain/`、`project/src/tests/Function/`、测试资产 |
| Docs | 回写 Terrain/Editor/Render/Asset spec、导航、VERIFY 和人工模板 | `docs/specs/`、`docs/CODEBASE_MAP.md`、`docs/VERIFY.md`、`docs/templates/` |

### API / contract changes

- 新增无 UI 依赖的 extent normalization/layout-construction helper；输入/输出使用整数米，失败显式返回错误。
- `TerrainCreateAssetDesc` 与 Terrain Mode 的 Import draft/`BuildImportDesc` 默认目标改为 2048 × 2048 m layout；底层 `TerrainGridLayout{}` 和显式传入 layout 的调用不变。
- `TerrainGridLayout` 的序列化字段和 `.AshTerrain`/Scene schema 不变。
- Terrain 私有 C++/HLSL constant layout 增加动态 layout 字段并由 static/source contract tests 锁定。
- 不新增或修改公共 RHI/RenderGraph API。

### Backend impact

Vulkan/DX12 都消费同一 HLSL 源码和 Function 常量。后端资源 API 已支持按尺寸创建 buffer/render target；实现只传入动态合法尺寸。必须分别验证 SPIR-V/DXIL 编译、validation/debug layer、矩形 target extent、边界 owner 和 layout reload。任何单后端专用修正都不允许泄漏到 Editor/Function 公共 contract。

### Performance

默认 2048 × 2048 m 的 Component 数从 1024 降至 64。packed height buffer、Component upload/compose/readiness 工作和 coarse target 应按实际布局下降；高分辨率 atlas 固定成本不变。实现不得为了简化仍按最大 1024 Component 分配 height buffer/coarse target，也不得修改 LOD/画质或 PerfGate 阈值。

布局切换期间允许 active + candidate 两套 layout-dependent resources 的有界暂时峰值，这是原子回滚的正确性成本；所有尺寸先做 checked arithmetic/budget 检查，分配失败保留 active view。不得通过原地覆写 active atlas 来降低峰值。资源字节 oracle至少锁定：8×8 height 8,454,400 B、coarse 257×257 RGBA8 = 264,196 B；8×16 height 16,908,800 B、coarse 257×513 RGBA8 = 527,364 B；32×32 height 135,270,400 B、coarse 1025×1025 RGBA8 = 4,202,500 B。

最大 8192 × 8192 m 仍需保持既有性能水位和 1 GiB import 预算合同。矩形/min/default/max 运行证据要记录实际 GPU resource byte count，证明较小布局确实减少 layout-dependent 资源，而不是仅改变 draw bounds。

## Verification plan

| 验证 | 覆盖 | 命令/证据 |
| --- | --- | --- |
| TDD RED/GREEN | clamp、最近 2 的幂、tie-up、X/Z 独立、默认值、descriptor 推导 | focused doctest filters |
| Asset/import focused | 256×8192、2048×4096、2048×2048、8192×8192；Reject/Crop/Catmull-Rom 共用 target | `RunTests.bat Debug/Release --test-case="*Terrain*"` 对应 focused filters |
| Render CPU seams | 动态 row stride、精确 table size、replacement 非空与 same-layout null removal、height bytes、coarse extent、root constant offsets、边界 owner | Terrain/Function focused tests，含 injected allocation/upload failures |
| Fail-closed | 非法/超限/不完整/溢出 layout 与 candidate rollback；错误含路径和布局 | focused tests |
| Full tests | Editor/Function/Terrain 全部回归 | `RunTests.bat Debug`、`RunTests.bat Release` |
| Project/build | 工程接线、双配置、双目标 | fresh `generate_vs2022.bat`；Editor/Sandbox Debug+Release build |
| Architecture/docs | 依赖方向、SDD/spec/计划一致 | `RunArchGate.bat`；`AIDevDoctor.ps1 -Mode ValidatePlan` |
| Shader compilation | Terrain surface/atlas update 的 DXIL 与 SPIR-V entry | 既有 shader compile gate/targeted compiler commands |
| Readiness | min、矩形、默认、最大 fixture 的 compose/upload/atlas/scene submit | 四组合 `run.bat all Debug --smoke-test-seconds=120`，并对 target fixtures 做双后端定向运行 |
| Validation | Vulkan validation、DX12 debug+GPU validation；布局切换无 lifetime/bounds 错误 | min/rect/default/max 双后端 short validation，fresh logs |
| Render regression | GBuffer/shadow/material/LOD/cross-backend 不变 | `RunRenderGate.bat`，non-bless；Terrain 定向 capture 仅作证据 |
| Performance | 默认布局资源下降，最大布局不退化 | `RunPerfGate.bat -Profile Standard`；记录四布局资源字节与 TerrainGate 最大压力证据 |
| Recovery audit | 配置、日志、子进程、golden/perf baseline 无污染 | 前后 SHA-256、fresh session logs、effective roots、baseline zero diff |
| Human signoff | 手填/失焦/Enter/tie-up 体验、derived labels、Create/Import 对话与最终画面 | 人类在 Vulkan/DX12 亲自操作；Agent 只移交中文清单，不代签 |

任何 validation/debug-layer error、RenderGate FAIL、PerfGate FAIL 或未批准 WARN 都按 stop-rule 处理；不 bless golden/perf baseline。

## Task breakdown

1. **Layout helper + Editor RED/GREEN**：锁定 normalization、authoring default、共享 draft 和 Create/Import descriptor；验收为 focused tests 全绿，尚不改 renderer。
2. **Render indexing/sizing RED/GREEN**：让 CPU seams 接受 1–32 的矩形 layout，动态分配 height/coarse 资源；验收为 min/rect/default/max sizing 和越界失败测试全绿。
3. **Shader binding RED/GREEN**：扩展 Terrain 私有 constants，去除全地形 32/8192 魔数，编译 DXIL/SPIR-V；验收为 byte-layout/source contracts 和 targeted shader compile 全绿。
4. **Atomic layout switch**：引入 coherent published/candidate resource view、identity ordering、确定性 initial resident set 和故障注入；验收为各分配/height/coarse/initial-atlas upload 失败保留旧 snapshot/resources/bounds，成功只在 frame boundary 同步切换 draw、culling、TAA 与 shadow identity，后续可见集 LRU 收敛后 readiness 才成功。
5. **Integration**：fresh generate、双配置 tests/build、ArchGate/AIDevDoctor、四布局双后端 readiness/validation。
6. **Gates/docs**：non-bless RenderGate、Standard PerfGate、资源字节证据、配置恢复；回写长期 specs、将本 SDD 标记 Done，并移交人工清单。

每个 checkpoint 只提交本步骤拥有的文件，cached diff 必须排除并发会话和 LFS 噪声；禁止 `git add -A`。

## Risks

| Risk | Mitigation |
| --- | --- |
| 只改 UI，renderer 仍按 32×32 寻址导致越界/错图 | 动态 layout descriptor 作为 CPU/HLSL 单一真源，矩形和最小布局契约测试 |
| coarse target 边界 owner 在非 32 Component 时少一行/列 | 公式 `count×32+1` 与动态 last-component 判断，四角/边界 GPU capture |
| 误把高分辨率 atlas 按全地形缩小/放大 | SDD 明确保留固定 256 槽 LRU，仅 coarse target/height buffer 动态化 |
| root constants C++/HLSL 偏移不一致 | `sizeof/offsetof` static assertions、source contract tests、双编译器验证 |
| 同路径 layout reload 先销毁旧资源造成黑帧或 UAF | candidate resource set、frame-boundary atomic swap、deferred destruction、故障注入 |
| 候选 snapshot 的新 bounds 提前裁剪仍在显示的旧资源 | RenderScene/Proxy 只消费 coherent published view；bounds 与 GPU resources 同帧切代 |
| asset replacement 的 generation 重置被旧 generation 拒绝 | 以 asset ID 区分 replacement，同 ID 才比较 content/residency revision |
| 把合法 same-layout null removal 与非法 replacement 缺项混为一谈 | table size 始终精确；同 layout 下按现有 generation/revision 排序被接受的更新保留 null removal；layout replacement 强制完整非空快照 |
| 最大地形 1024 Component 无法全部装进 256 atlas 槽 | candidate 全量准备 coarse，只预载 published resident/required 集的确定性最多 256 项；initial atlas 失败禁止 swap，后续按既有 LRU 填充 |
| 较小默认意外削弱 8 km 压力覆盖 | TerrainGate 和 full-size tests 显式保持 8193²，不依赖 authoring 默认 |
| 用户输入跳动或未失焦时提交旧值 | edit 中保留 draft；deactivation normalize；操作前再次 normalize；派生标签显示真实目标 |
| 既有资产被迁移或重写 | schema 不变、load 按资产 layout、无 resize-in-place，兼容测试锁定原 bytes/布局 |
| 最大布局性能/内存回归 | 最大 fixture、1 GiB import 合同、Standard PerfGate 和资源字节审计，不改阈值/基线 |

## Open questions

无。产品选择、数值范围、吸附规则、默认值、X/Z 独立、Create/Import 共享目标以及验证边界均已在 2026-07-22 的任务讨论中确认；本书面 SDD 已获用户批准，实施按对应 plan 的检查点执行。
