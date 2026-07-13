# SDD-2026-07-13-world-scale-gpu-vegetation: 大世界 GPU-driven 植被系统（S3 总体）

## Status

Approved（2026-07-13，用户审阅通过；任何阶段实现仍须遵守对应阶段依赖与验证门禁）

## Context

目标是在 AshEngine 中加入面向大世界的 GPU grass 与 GPU tree：Editor 在地形表面使用植被笔刷；运行时按分块流送，GPU 完成实例剔除、LOD/HLOD、压实和 indirect draw；SpeedTree 作为树木内容来源；最终性能目标为 RTX 40xx/50xx 中高端显卡上 2560×1440、完整渲染管线、300 FPS。

该目标同时影响性能观测、RenderGraph、Function/RHI 间接绘制契约、场景数据、资产管线、Editor、shader、阴影与 temporal 状态，属于 S3。禁止从总体 SDD 直接跳到大范围实现；每个阶段必须另有标准 SDD、独立批准和可归因验证。

已确认的产品边界：

- 首版笔刷只面向地形表面；地形系统仍在开发，植被只依赖稳定表面采样契约。
- 笔刷写入分块密度/物种权重和确定性 seed，不在 scene JSON 保存单株 transform。
- 世界总实例数不设硬上限；运行时通过显存、IO、上传和可见距离预算控制 resident set。
- 首版动态行为为全局方向风、强度和阵风；不做角色压草、局部风源、砍伐、燃烧或物理交互。
- `.AshVegetation` 是稳定资产；SpeedTree 是可插拔离线来源，runtime 不依赖其 SDK。
- 植被专用领域能力与通用 GPU-driven runtime 分离，保证未来普通静态网格直接接入。

## Goals

- 提供地形表面植被笔刷、Undo/Redo、稀疏密度瓦片和脏分块增量烘焙。
- 提供原生物种、作者层与运行时 chunk 资产契约。
- 支持无世界总量上限的分块流送，以及有界 resident GPU instance pages。
- GPU 执行 page/instance 剔除、LOD 分类、稳定压实和 indexed indirect draw。
- 支持 GPU grass、GPU tree、全局风、Masked/双面材质、GBuffer、阴影和 TAA motion vector。
- 草使用 mesh/card/cluster/fade 层次；树使用 mesh LOD、单树 impostor 和 chunk HLOD。
- 建立可被未来全 GPU-driven static-mesh renderer 复用的 page/prototype/view/draw-group contract。
- 建立 Release 2K 完整管线 CPU/GPU 可归因门禁，最终评估 300 FPS。

## Non-goals

- 在本项目内开发或替代 SpeedTree 建模器。
- 首版运行时读取 `.st`，或在未批准第三方依赖前引入 SpeedTree SDK。
- 首版角色/载具交互、破坏、砍伐、燃烧、导航、碰撞或网络同步。
- 首版把所有普通静态网格迁移到 GPU-driven runtime。
- 依赖 bindless、mesh shader、ray tracing、indirect-count 或 RTX 50 系独占能力保证正确性。
- 在未测得 Release GPU 基线前承诺植被单项毫秒预算或 bless 性能水位。

## Current implementation

- Entry points:
  - Scene → render：`ScenePresentationSubsystem` 构建不可变 `VisibleRenderFrame`，`SceneRenderer` 组织 per-view RenderGraph。
  - static mesh：`RenderScene::build_visible_render_frame` 运行 CPU frustum culling；`SceneRenderer::render_static_meshes_to_pass` 按 mesh/section/program 合批并上传三帧 ring instance vertex buffer。
  - indirect：RHI 已有 draw/draw-indexed/dispatch indirect；Function `GraphicsDrawDesc` 只有单条 non-indexed indirect。
  - assets：模型支持 FBX/OBJ/glTF/GLB，纹理支持常见源格式及 DDS/KTX2；Material V2 支持 Surface、Masked、two-sided 和 world-position offset。
  - PerfGate：Standard 为 Debug、Sandbox/Editor × Vulkan/DX12，只输出 CPU frame time；baseline 为空。
- Modules: Base、Graphics、Function/Render、Function/Scene、Function/Asset、Editor、Sandbox、tools/perf。
- Data flow: Scene mesh entity → `StaticMeshPrimitiveProxy` → CPU visibility → `VisibleStaticMeshDraw` → CPU batch/instance buffer → direct draw。
- Known constraints:
  - RenderGraph 只管理 texture；buffer access enum 已存在部分状态但没有 buffer handle、依赖、lifetime 或执行 barrier。
  - `IGpuProfilerContext` 是 Tracy facade，不向 PerfGate 返回 resolved timestamp samples。
  - 当前最新本地 Debug PerfGate 只观测到 Sandbox CPU avg 约 7.82 ms（Vulkan）/8.57 ms（DX12）；该数据不是 Release/GPU 基线，不能判定 3.33 ms 整帧目标是否可达。
  - `MeshVertex` 没有树木 trunk/branch/leaf deformation 所需辅助数据。
  - 场景当前没有 Vegetation 组件，schema version 为 5（正在审阅的粒子 S2 可能先推进到 v6，植被阶段必须以落地时实际 schema 为准）。

## Proposal

总体架构遵循 [ADR-2026-07-13-gpu-driven-instance-runtime](../adr/ADR-2026-07-13-gpu-driven-instance-runtime.md)。

### Authoring and asset contract

| Asset | Purpose | Runtime editable |
| --- | --- | --- |
| `.AshVegetation` | 物种：mesh LOD、材质、bounds、deformation、shadow、impostor/HLOD 配置 | No |
| `.AshVegetationLayer` | 作者数据：物种 palette、稀疏 density/weight tiles、seed、散布过滤 | Editor only |
| `.AshVegetationChunk` | 烘焙产物：schema/hash/surface revision、压缩实例页、bounds、HLOD | No |

Scene 只由 `VegetationComponent` 引用 layer 资产。笔刷 stroke 以压缩 tile patch 形成一个 Undo/Redo command；异步 baker 对脏 tile 的关联 chunk 做 generation-checked 增量提交。相同 layer ID、chunk/cell 坐标、species ID、seed 和 surface revision 必须产生相同排序与字节输出。

地形通过 Function 层的批量 surface-sampling contract 返回稳定 surface ID/revision、位置、法线和坡度。无 provider 时 Editor 可编辑 palette，但禁止落笔并显示明确原因。

### Runtime streaming and large-world coordinates

CPU 以整数 world-partition coordinate 和 page-local 量化位置保存大世界数据；提交 GPU 时转为 camera-relative float。地形与植被必须共用该坐标契约。

streamer 按相机、预取距离、滞回和 HLOD 层级计算 desired chunks。IO worker 只读取/校验/解压 CPU 数据；render thread 按上传预算写入 GPU page pool。page handle 为 `{slot, generation}`，逐出必须等待引用该 slot 的 GPU frame 完成。预算不足时保留近景，优先切换远景 HLOD；禁止随机覆盖有效页。

### GPU visibility and draw flow

```text
resident pages
  → page bounds coarse cull
  → instance frustum + distance + previous-frame HZB
  → screen-error LOD selection with hysteresis
  → deterministic count/prefix/scatter by draw group
  → visible instance indices + indexed indirect args
  → GPU-driven Depth/GBuffer/Shadow draws
```

HZB 按 view key 隔离，来自上一帧 depth。resize、camera cut、content epoch 或大幅 view 跳转后关闭 occlusion 一帧，只保留保守 frustum/distance culling。

`firstInstance` 恒为 0；draw constants 传 visible-list base 和 Prototype ID。草、树和未来普通静态网格共享 runtime 输出格式。

### Rendering, wind and HLOD

- 新增 `SurfaceGPUDriven` Material V2 family；继续写现有 DeferredHQ GBuffer，复用 AO、lighting、IBL、Bloom、TAA 和 tone-map。
- 植被 mesh 使用可选压缩 deformation vertex stream，不膨胀普通 `MeshVertex`。
- shader 用 current/previous view 和 wind snapshot 计算两帧变形位置，输出正确 motion vector。
- LOD 过渡使用 stable spatial dither，禁止 frame-random dither。
- grass：cluster mesh → reduced card → chunk aggregate card → fade。
- tree：mesh LOD → per-tree multi-view impostor → chunk cluster HLOD。
- grass 只在近距离投影；near tree/grass 可动态风动投影，outer cascade 使用简化或稳定 silhouette；HLOD shadow 可关闭或缓存。

### Failure and readiness contract

chunk 状态为 `Absent → Requested → CPUReady → UploadPending → Resident → EvictPending`，任一加载/校验/上传失败进入 `Failed`。Editor 保留 last-known-good chunk 并标 Stale/Failed；自动化/正式 benchmark 的 required chunk 失败必须使 readiness 失败。

GPU pool overflow 不覆盖 resident data；记录 requested/resident/visible/culled/LOD/HLOD/overflow 统计。材质或必需纹理失败可以显示 fallback 便于诊断，但仍保留 asset failure，禁止把 fallback 当 readiness success。

### Module changes

| Module | Change | Planned area |
| --- | --- | --- |
| Graphics | GPU timestamp telemetry（Phase 0）；复用既有 indexed indirect | `Graphics/` 双后端 |
| Function/RenderGraph | buffer resource、dependency/lifetime/barrier、transient storage buffer | `Function/Render/RenderGraph*` |
| Function/Render | GPU-driven page/prototype/runtime、HZB、grass/tree pass、SurfaceGPUDriven family | `Function/Render/`、`Shaders/` |
| Function/Scene | VegetationComponent、world partition/surface contract、extraction/version | `Function/Scene/` |
| Function/Asset | species/layer/chunk load/cook、SpeedTree-export adapter boundary | `Function/Asset/` |
| Editor | brush panel、tile patch undo、bake/status/debug overlay | `project/src/editor/` via `UIContext` |
| Sandbox/tools | fixed benchmark scenes、GPU metrics、Vegetation Release profile | `project/src/sandbox/`、`scripts/`、`tools/perf/` |

### API / contract changes

- 新增 RenderGraph storage buffer public surface；Phase 1 SDD 定义精确 API。
- `GraphicsDrawDesc` 增加显式 indirect kind，并补 indexed indirect / drawCount / stride；既有粒子调用迁移到 explicit non-indexed。
- 新增 versioned GPU-driven page/prototype/view/draw-group layout；C++/HLSL 以 static assert 和 self-test 对齐。
- 新增 `.AshVegetation*` schema 与 Scene `VegetationComponent`；scene schema 版本以实施时主线为基数递增。
- 新增地形 surface sampling 与 world-partition coordinate contract；需与地形 SDD 双向链接。
- Material V2 新增 `SurfaceGPUDriven` family 与 optional deformation binding；不新增材质 Domain。

### Backend impact

- Vulkan/DX12 必须等价实现 GPU timestamps、buffer barriers 和 indexed indirect Function path。
- HLSL 由现有 DXC 双后端编译，不允许后端宏把植被语义泄漏到 Function/Scene。
- plain timestamps/indirect 为目标硬件基础能力；不满足 benchmark timing 的设备只使 Vegetation Perf profile 不可运行，不影响普通启动。
- WARP/lavapipe 继续承担正确性 smoke；300 FPS 只在记录了 GPU/driver/OS 的 RTX 4070-class 或更高固定机器评估。

### Performance

最终目标是 2560×1440、完整 pipeline、300 FPS，即 3.33 ms/frame。该目标是整机目标，不是植被单项承诺。

Phase 0 先建立无植被 Release CPU/GPU baseline。此后每阶段按 baseline 剩余预算设置独立 CPU streaming、GPU cull、GBuffer、shadow 和 total frame gate；未测 baseline 前不得写死毫秒阈值。世界总实例数不设上限，resident pages、upload bytes/frame、IO bandwidth 和 HLOD distances 由 profile budget 控制。

## Phased plan

| Phase | Scope | Exit gate |
| --- | --- | --- |
| 0 | GPU timestamp telemetry、Release 2K full-pipeline profile、空基线 | 双后端报告有效，extent=2560×1440，CPU/GPU sample coverage 达标，无同步 readback stall |
| 1 | RenderGraph buffer、Function indexed indirect、通用 page/prototype contract | headless compiler tests + 双后端 compute→buffer→indexed indirect self-test，默认场景无回归 |
| 2 | `.AshVegetation*`、surface contract、brush、deterministic bake | 无 terrain provider 时安全禁用；test provider 下 paint/erase/undo/reload/cook byte-stable |
| 3 | streaming + GPU grass | 分块移动/逐出稳定，grass GBuffer/wind/near shadow 双后端 RenderGate 通过 |
| 4 | GPU tree + SpeedTree-export adapter + impostor | mesh LOD/impostor/deformation/motion vector/shadow 通过视觉与 validation 门禁 |
| 5 | previous-frame HZB + chunk HLOD + 性能收敛 | long-run memory plateau、跨后端一致、按 Phase 0 基线评估 300 FPS 与各子预算 |

Phase 0/1 的 Review SDD：

- [SDD-2026-07-13-gpu-performance-observability](SDD-2026-07-13-gpu-performance-observability.md)
- [SDD-2026-07-13-gpu-driven-foundation](SDD-2026-07-13-gpu-driven-foundation.md)

## Verification plan

| Verification | Coverage | Command / evidence |
| --- | --- | --- |
| Docs | 文档格式/路径 | `git diff --check` |
| Unit | schema、determinism、allocator、LOD、graph compiler | `RunTests.bat Debug` |
| Architecture | dependency direction | `RunArchGate.bat` |
| Build | Editor/Sandbox Debug+Release | `build_editor.bat Debug`、`build_sandbox.bat Debug`、对应 Release |
| Render | grass/tree/HLOD golden + backend diff | `RunRenderGate.bat`；预期变化经用户确认后才 bless |
| Performance | Standard 回归 + Vegetation Release profile | `RunPerfGate.bat -Profile Standard`；Phase 0 新 profile |
| Validation | buffer/barrier/lifetime/indirect | Vulkan validation 与 DX12 debug/GPU validation 各跑 benchmark smoke |
| Soak | streaming、fence、memory plateau | 固定相机路径长时循环，报告 resident/evicted/upload/overflow 与 GPU memory |
| Editor | brush workflow | paint/erase → undo/redo → save/reload → dirty bake/failure recovery |

## Task breakdown

1. 批准并完成 Phase 0，建立可信的 Release CPU/GPU baseline。
2. 根据 Phase 0 结果批准并完成 Phase 1，冻结可执行的 GPU-driven foundation contract。
3. 与地形开发者对齐 world-partition 和 surface revision，再编写/批准 Phase 2 SDD。
4. GPU grass、GPU tree、HZB/HLOD 分别写阶段 SDD，不把多阶段实现混入一个 diff。
5. 每阶段关闭时将最终行为回写 `docs/specs/`；总体 SDD 在全部阶段完成后标 Done。

## Risks

| Risk | Mitigation |
| --- | --- |
| 300 FPS 在完整现有 pipeline 上无剩余预算 | Phase 0 先测无植被 GPU/CPU 基线；把整机优化与植被增量分别归因 |
| RenderGraph buffer 扩展破坏 barrier/lifetime | 独立 Phase 1；headless compiler + 双后端 validation + indirect self-test |
| 地形接口并行开发发生类型冲突 | Phase 2 前冻结共享 world-partition/surface contract；植被不 include terrain internal |
| SpeedTree 未授权或导出语义变化 | 原生 asset 稳定；FBX/glTF 可先工作；专用 importer 延后且仅离线 |
| 风动导致阴影/TAA 成本和伪影 | current/previous deformation；near dynamic/outer simplified shadow；分项 GPU timing |
| resident pool overflow 或 use-after-free | budgeted page pool、slot generation、fence-delayed reuse、long-run soak |
| 双后端 compaction 顺序导致 golden 波动 | count/prefix/stable scatter；固定 seed/step/camera；跨后端 RenderGate |
| 通用底座为单一植被过度设计 | Phase 1 只实现 graph buffer、indexed indirect 与最小 page/prototype contract；HZB/streaming/shader 后移到真实消费者阶段 |

## Open questions

- 无总体设计阻塞项。精确 GPU/driver、最终场景内容和各阶段毫秒阈值由 Phase 0 报告确定并写入后续阶段 SDD，不在本 SDD 中猜测。
