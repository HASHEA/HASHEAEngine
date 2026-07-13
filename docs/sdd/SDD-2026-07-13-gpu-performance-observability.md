# SDD-2026-07-13-gpu-performance-observability: GPU 性能观测与 2K Release 基线（S2 / Phase 0）

## Status

Approved（2026-07-13，用户审阅通过；允许进入实施计划，仍禁止直接编辑或未确认即 bless 性能基线）

## Context

大世界植被的最终目标是 RTX 40xx/50xx 中高端显卡、2560×1440、完整渲染管线、300 FPS。300 FPS 只有 3.33 ms 整帧预算，必须先知道当前无植被 pipeline 的 CPU/GPU 构成，才能给 streaming、culling、GBuffer 和 shadow 分配可归因预算。

当前 PerfGate `Standard` profile 为 Debug，运行 Sandbox/Editor × Vulkan/DX12。引擎 JSON 只输出 CPU frame time、CPU begin/end/present 分解、内存和 draw/pass/dispatch 计数。`GpuProfilerRHI` 只把 Tracy zone 发往 Tracy，不提供可由 PerfGate 消费的 resolved timestamp sample；PerfGate 也没有固定 resolution/scene 的 profile contract。

当前 `tools/perf/perf_gate_baselines.json` 同时保存 profile 配置和 bless 水位，且该路径禁止直接编辑。新增 profile 不能依赖手改该文件。

## Goals

- 在 Vulkan/DX12 上提供 PerfGate 可消费、无同帧 CPU wait 的 GPU frame/pass timestamp telemetry。
- 新增固定 2560×1440、Release、Sandbox、双后端的 `VegetationFullPipeline` profile。
- 固定 benchmark scene、camera、scene config 和完整 pass 开关；报告实际 frame extent，拒绝静默使用错误分辨率。
- JSON 报告新增 GPU frame 与稳定 pass-group 的 avg/p50/p95/p99/min/max、sample coverage 和 invalid reason。
- 将可编辑 profile 定义移出 bless-only baseline 文件；baseline 仍只能经 `-BlessBaseline` 发布。
- 建立无植被 CPU/GPU baseline 的候选报告；只有用户确认硬件、驱动、场景和结果后才 bless。
- 保持 normal run 与 RenderGate 在 telemetry 关闭时零 query/readback 开销。

## Non-goals

- 本阶段不实现植被、GPU-driven culling、RenderGraph buffer 或 renderer 优化。
- 不以 Tracy capture 代替结构化 PerfGate 报告，也不移除现有 Tracy profiler。
- 不承诺 300 FPS 可达；只建立可归因证据。
- 不修改 `tools/perf/perf_gate_baselines.json` profile 或 baseline 内容；候选水位由后续 bless 命令写入。
- 不把 WARP/lavapipe 作为性能硬件；它们继续只做正确性 smoke。

## Current implementation

- Entry points:
  - CLI：`EntryPoint.h` 解析 `--perf-gate*`、`--scene` 等参数；没有固定 window extent 参数。
  - sampling：`Function/Diagnostics/PerfGate.*` 从 `RendererFrameStats` 收集 CPU 与 render counts。
  - script：`scripts/RunPerfGate.ps1` 从 `perf_gate_baselines.json.profiles` 读取配置并启动矩阵。
  - GPU profile：`Graphics/GpuProfilerRHI.*` + Vulkan/DX12 Tracy 实现；无结果查询 API。
- Modules: Graphics 双后端、Function/Render、Function/Diagnostics、EntryPoint、Sandbox、scripts/tools。
- Data flow: Renderer end-frame CPU stats → `PerfGateController::sample_after_frame` → telemetry JSON → PowerShell validation/trend report。
- Known constraints:
  - GPU query resolve 有多帧延迟，不能把“本次 CPU frame”与“本次 poll 到的 GPU frame”按调用顺序强配对。
  - Vulkan/DX12 timestamp frequency 与 query/readback 方式不同，但输出单位和有效性必须一致。
  - benchmark resolution 必须由启动参数控制并由 telemetry 回报，不能依赖本地 `imgui.ini` 或窗口历史状态。
  - baseline 文件受 hook 保护，只能 bless；profile 配置需要独立真源。

## Proposal

### GPU timing interface

保留 `IGpuProfilerContext` 作为 Tracy facade，新增只在 PerfGate 请求时启用的 `IGpuTimingTelemetry`，公共契约位于 `Graphics/GpuTimingTelemetryRHI.h/.cpp`。它使用固定容量 frame ring 和稳定 metric ID：

```cpp
begin_frame(command_buffer, frame_id);
begin_scope(command_buffer, metric_id);
end_scope(command_buffer, metric_id);
end_frame(command_buffer, frame_id);
commit_frame(frame_id);
abort_frame(frame_id, reason);
poll_completed_frame(GpuFrameTimingSample& out_sample); // Ready / Pending / Empty
get_info(GpuTimingTelemetryInfo& out_info);
```

`begin_frame`/`end_frame` 在 RenderDevice 主 command buffer 的 `begin_record` / `end_record` 之间录制 timestamp；`commit_frame` 只在该 command buffer 成功提交并已绑定后端 completion primitive 后调用，录制或提交前失败走 `abort_frame`。`poll_completed_frame` 只返回 fence/timeline 已完成的旧帧；用 `Ready / Pending / Empty` 区分可消费结果、仍在 GPU 飞行中的提交和无未决工作，没有结果时立即返回，不等待 GPU。sample 包含原始 `frame_id`、whole-frame duration、named scopes、valid flag 和 invalid reason。重复 metric、query overflow、backend unsupported 或 incomplete pair 都产生 invalid sample，不伪造 0 ms。`get_info` 返回 adapter、driver、timestamp frequency/period、valid bits、query capacity 和 timing scope metadata。

`GPU.Frame` 的 Phase 0 定义是 RenderDevice 主 command buffer 从首个 timestamp 到末个 timestamp 的执行时间：包含 scene、UI、present-copy/capture GPU 命令，不包含此前独立提交的 upload command buffer、CPU 提交/等待时间或 `vkQueuePresentKHR` / DXGI Present。报告把它标为 `main_command_buffer`，避免与端到端显示延迟混淆。

Vulkan 使用 timestamp query pool、`timestampPeriod` 和 frame completion；DX12 使用 timestamp query heap、queue frequency、resolve/readback buffer 和 fence。两端采用相同 ring 深度和 metric capacity。普通运行不创建 telemetry query resource。

### Metric contract

Phase 0 固定以下顶层 metric；名称进入 JSON schema，后续不得随意重命名：

- `GPU.Frame`
- `GPU.GBuffer`
- `GPU.AmbientOcclusion`
- `GPU.Shadows`
- `GPU.DeferredLighting`
- `GPU.EnvironmentAndSky`
- `GPU.Particles`
- `GPU.VolumetricLighting`
- `GPU.Bloom`
- `GPU.TemporalAA`
- `GPU.ToneMapAndOverlays`

metric 由 `SceneRenderer`/RenderGraph pass group 边界插入，而不是 PowerShell 按 pass 名猜测。单 graphics queue 下顶层 metric 不重叠；嵌套 Tracy zone 保持独立，不进入 PerfGate 汇总。

`GPU.Shadows` 只统计 shadow-map/depth generation；与逐灯光 deferred pass 交错的 shadow-mask evaluation 归入 `GPU.DeferredLighting`，避免为统计重排 RenderGraph。`VegetationFullPipeline` 固定为单个 timed scene view；若同帧出现多个 timed view 导致 metric 重复，Phase 0 明确产出 invalid sample，后续再设计 view 维度或聚合语义。

### Delayed sample collection

`RendererFrameStats` 不把 GPU duration 当成本帧同步字段，而是携带“本次 poll 到的 completed timing samples”。`PerfGateController` 以 sample 自带 frame ID 和采样窗口标记收集 GPU 数据。warmup 末尾前提交但之后才 resolve 的帧不计入；采样末尾允许有限 drain window，只 poll 已完成 frame，禁止 device idle。

报告必须包含：

- submitted/resolved/valid/invalid sample counts；
- GPU coverage ratio；
- 每个 metric 的 summary 与缺失率；
- backend timestamp frequency/period 和 query capacity；
- 实际 output width/height；
- GPU/adapter、driver、OS、configuration 与 validation 状态。

PerfGate 状态机固定为 `Warmup → Sampling → Draining → Complete`。采样窗口保存成功提交的 renderer frame ID 区间；`submitted` 是该区间内已 `commit_frame` 的数量，`resolved` 是 drain 结束前取回的数量，`valid` 是 sample 本身有效且 11 个 required metric 全部存在的数量，GPU coverage 为 `valid / submitted`。每个 required metric 另算 `present / submitted`；invalid/unresolved 不进入 percentile。drain 首版上限为 5 秒，窗口内提交全部离开 `Pending` 可提前完成；超时后剩余提交计为 unresolved，仍禁止 device idle。

`VegetationFullPipeline` 硬要求总 GPU valid coverage 与每个 required metric coverage 均 ≥ 95%；不足、`GPU.Frame` 缺失、extent 在采样窗口内变化或 fixed-camera 契约失效均为 FAIL。`Standard` 保持 GPU telemetry opt-out，继续接受 schema v1/v2，不因缺少 GPU 字段破坏兼容。

### Profile configuration and benchmark launch

新增 `tools/perf/perf_gate_profiles.json` 作为新 profile 真源。脚本按 profile name 先查该文件；目标 profile 缺失时再回退旧 `baseline.profiles`，因此 Standard 可在不改受保护 baseline 文件的情况下保持兼容。新 profile 不写入 baseline 文件。

`VegetationFullPipeline` 初始配置：

| Field | Value |
| --- | --- |
| configuration | Release |
| target | Sandbox |
| backends | Vulkan, DX12 |
| extent | 2560×1440 |
| warmup/sample/timeout | 10s / 30s / 90s（首版沿用 Standard，实测后再调整） |
| scene | `product/assets/scenes/VegetationBaseline.scene.json`；无 VegetationComponent |
| validation | Off for performance run；另有 validation correctness run |
| vsync/frame cap | Off，并在 telemetry 中记录实际状态 |

EntryPoint/Application 增加 `--window-width=<pixels>` / `--window-height=<pixels>`；profile 同时传递 `--scene=product/assets/scenes/VegetationBaseline.scene.json` 与 2560×1440 extent，程序启动后回报实际 renderer extent。benchmark scene 显式开启 AO、方向光阴影、IBL/sky、粒子、体积光、Bloom、TAA 与 tone-map；禁止依赖 Engine.ini 的本地隐式默认。

PerfGate、extent、vsync、validation 与 GPU timing mode 必须在 `Application::initialize()` 前注入。脚本使用本次进程 override 固定 vsync off、performance validation off 和 GPU timing on；短正确性 run 单独用 Debug + validation on。override 优先级高于 `Engine.ini`，只作用于本进程且必须写入报告。benchmark/PerfGate 模式冻结 scene JSON 中的 primary camera，不运行 Sandbox free-camera 更新。仓库当前无 frame cap，报告显式记录 `frame_cap=off`。

### Telemetry and gate schema

engine telemetry 升级为 schema v2，并保持 v1 CPU 字段兼容。`RunPerfGate.ps1`/summary 增加 GPU Avg/P95、coverage、adapter/driver/extent 列与机器可读字段。

profile threshold 与 baseline 水位分开：profile 文件保存 coverage、extent 和绝对安全条件；历史水位继续由 `-BlessBaseline` 写入 `perf_gate_baselines.json`。首次 Phase 0 运行只产候选报告，不自动 bless。

profile 的 `required_gpu_metrics` 固定为本 SDD 的 11 个 metric；交互菜单不得把固定 Release profile 改成 Debug。runner 提供只用于诊断的 telemetry-off A/B override，同一 scene/extent/backend/Tracy 配置下比较 CPU overhead，但该 override 不能用于 bless。未来 bless schema 可保存各 required metric 的 avg/p95；缺少既有 baseline 时只报告 candidate，不以 `MISSING` 判失败。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Graphics common | backend-neutral timing sample/interface，独立于 Tracy result export | `Graphics/GpuTimingTelemetryRHI.h/.cpp`（新） |
| Graphics/Vulkan | query ring、timestamp resolve、non-blocking poll | `Graphics/Vulkan/VulkanGpuTimingTelemetry.h/.cpp`（新）+ context integration |
| Graphics/DX12 | query heap、resolve/readback ring、fence poll | `Graphics/DirectX12/DX12GpuTimingTelemetry.h/.cpp`（新）+ context integration |
| Function/Render | frame/pass-group scope、RenderGraph live-pass metric 与 delayed sample surface | `Renderer.*`、`RenderDevice.*`、`RenderGraphPass.*`、`RenderGraphBuilder.*`、`RenderGraphCompiler.*`、`RenderGraphExecutor.*`、`SceneRenderer.*` 与现有 pass call sites |
| Function/Diagnostics | JSON v2、GPU summaries/coverage/metadata | `Function/Diagnostics/PerfGate.*` |
| EntryPoint/Application | fixed extent CLI 与实际 extent 校验 | `EntryPoint.h`、`Function/Application*` |
| Sandbox/assets | fixed full-pipeline no-vegetation benchmark scene 与 fixed-camera mode | `project/src/sandbox/`、`product/assets/scenes/` |
| Tools | profile config 真源、GPU report/gate、菜单兼容、自测 | `tools/perf/perf_gate_profiles.json`、`scripts/RunPerfGate.ps1`、`scripts/RunPerfGateMenu.ps1`、tests/docs |

### API / contract changes

- 新增双后端 GPU timing telemetry 接口和 `GpuFrameTimingSample`。
- `RendererFrameStats` 增加 resolved GPU sample collection，不改变 CPU frame-time 语义。
- PerfGate telemetry schema v1 → v2，保留现有字段。
- Perf profile 从 baseline 内联配置迁移到独立 profile 真源；baseline bless contract 不变。
- 新增 window extent CLI；命令行只覆盖本次进程，不改 `Engine.ini` 或 editor layout。
- PerfGate launch override 在初始化前固定 vsync/validation/GPU timing；优先级高于 `Engine.ini`，实际解析值写入报告。

### Backend impact

- 两后端都必须产生相同 metric names、毫秒单位、coverage 与 invalid semantics。
- GPU timing 只在 PerfGate 开启；初始化或 capability 失败使 Vegetation profile FAIL，但普通运行继续。
- validation run 与 performance run 分离，报告必须记录 validation 开关，禁止混合比较。

### Performance

- telemetry disabled：不创建新增 telemetry query heap/pool，不录 telemetry timestamp，不 poll telemetry readback；既有 Tracy 配置保持不变并在 A/B 两组中一致。
- telemetry enabled：每 metric 两个 timestamp；固定容量，禁止热路径分配和 device idle。
- 使用 A/B 运行比较 telemetry off/on 的 CPU frame time；观测开销必须记录，若影响 300 FPS 判断则在报告中校正为“instrumented”而不伪装原始性能。
- 本阶段只生成候选 baseline。用户确认机器、驱动、场景和报告后，才允许执行 `RunPerfGate.bat -Profile VegetationFullPipeline -BlessBaseline`。

## Verification plan

| Verification | Coverage | Command / evidence |
| --- | --- | --- |
| Unit | percentile、delayed frame association、coverage、JSON v1/v2、profile loading | `RunTests.bat Debug` |
| Tool self-test | missing/invalid GPU metrics、extent mismatch、fallback profile、bless routing | `scripts/TestRunPerfGate.ps1` |
| Architecture | dependency direction | `RunArchGate.bat` |
| Build | 双后端 timing implementation | `build_editor.bat Debug`、`build_sandbox.bat Debug`、`build_sandbox.bat Release` |
| Render regression | instrumentation off 零行为变化 | `RunRenderGate.bat` |
| Performance profile | Release 2K full pipeline 双后端 | `RunPerfGate.bat -Profile VegetationFullPipeline` |
| Validation | query reset/resolve/fence lifetime | Vulkan validation 与 DX12 debug/GPU validation 下各跑短 profile smoke |
| Manual report audit | adapter/driver/extent/coverage/metric sums | 检查 `summary.md/json` 与原始 telemetry；不 bless |

## Task breakdown

1. 先加纯数据 summary/schema/profile-loader 单测，不接后端。
2. 增加 Graphics timing interface 与 fake backend tests。
3. Vulkan query ring + non-blocking resolve，自测通过。
4. DX12 query/readback ring + fence poll，自测通过。
5. Renderer/SceneRenderer metric integration，双后端短 run 有有效 sample。
6. PerfGate JSON v2、profile config、extent CLI 与固定场景。
7. 完整验证，输出候选报告供用户确认；本阶段实现提交不得包含 baseline bless。

## Risks

| Risk | Mitigation |
| --- | --- |
| 同帧 readback 让指标本身改变性能 | frame ring + fence/timeline non-blocking poll；禁止 wait_idle |
| CPU/GPU frame 错配 | sample 自带提交 frame ID；PerfGate 按 ID/窗口收集，不按 poll 顺序猜测 |
| Tracy 与 telemetry 重复 query 开销 | 两套职责分离；PerfGate run 可关闭 Tracy capture，只保留结构化 telemetry |
| pass metric 求和与 whole frame 不一致 | 固定不重叠顶层 scopes；报告 uncovered GPU time，不强迫求和相等 |
| 实际窗口不是 2K | CLI override + engine 回报 extent + script 硬校验 |
| profile 变更触碰受保护 baseline | 新建 profile 真源；baseline 只由 bless 写入 |
| backend timestamp 暂不可用 | profile FAIL 并报告 reason；普通运行不受影响 |

## Open questions

- 固定 benchmark 机器的具体 GPU SKU、driver 与 OS build 在首次候选报告中记录，由用户确认后成为比较环境；不写死到源代码。
- 各 pass-group 的毫秒硬预算在 Phase 0 baseline 产生后确定，属于 Phase 1+ SDD 输入。
