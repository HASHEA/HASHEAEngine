# GPU Performance Observability Implementation Plan

**Status:** Done（2026-07-14；Phase 0 candidate 与全部验证完成，未 bless baseline/golden）。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 在不实现植被的前提下，为 Vulkan/DX12 建立按需启用、无同帧 CPU wait 的结构化 GPU timing，并让 PerfGate 以固定 2560×1440 Release 全管线场景产出可归因的候选基线报告。

**Architecture:** Graphics 提供 backend-neutral timing contract，两个 context 各自复用既有三帧 completion slot 管理 query/readback；RenderDevice 负责主 command buffer frame scope，RenderGraph 通过显式 metric tag 生成 live-pass group scope，Renderer 非阻塞搬运已完成 sample，PerfGate 用 frame ID 状态机做 warmup/sampling/drain 和 schema v2 汇总。所有启动 override 在 `Application::initialize()` 前生效；profile 与 bless-only baseline 分离。

**Tech Stack:** C++17、Vulkan timestamp query、D3D12 timestamp query heap/readback、Premake5/MSBuild、doctest、PowerShell PerfGate、JSON scene/profile。

---

## Preconditions and scope guard

- 设计真源：`docs/sdd/SDD-2026-07-13-gpu-performance-observability.md`；core 与 Task 5 commit acknowledgement amendment 均为 Approved。
- 架构真源：`docs/adr/ADR-2026-07-13-gpu-driven-instance-runtime.md`。
- 本计划只做 Phase 0；不实现 Vegetation、RenderGraph buffer、GPU culling、HLOD 或 SpeedTree importer。
- 不直接编辑 `tools/perf/perf_gate_baselines.json`、`tools/render/goldens/`；本阶段不运行任何 bless。
- 保留用户现有 dirty 文件，尤其不修改 `product/assets/scenes/Sandbox.scene.json`、`product/config/editor/imgui.ini`、`.codex/`、`product/captures/` 和两份 AI engineering 文档。
- 新 `.cpp` 依赖 glob 进入工程，但现有 `.vcxproj` 不会自动刷新；第一次新增源码后必须运行 `generate_vs2022.bat`。
- 每个任务一个聚焦提交；若执行者未获提交授权，则跳过 `git commit`，但仍保持相同 diff 边界并记录建议提交信息。

## Stable contracts to preserve

```cpp
enum class GpuTimingPollResult : uint8_t { Ready, Pending, Empty };

class IGpuTimingTelemetry
{
public:
    virtual bool begin_frame(CommandBuffer* cmd, uint64_t frame_id) = 0;
    virtual bool begin_scope(CommandBuffer* cmd, GpuTimingMetric metric) = 0;
    virtual void end_scope(CommandBuffer* cmd, GpuTimingMetric metric) = 0;
    virtual void end_frame(CommandBuffer* cmd, uint64_t frame_id) = 0;
    virtual bool commit_frame(uint64_t frame_id) = 0;
    virtual void abort_frame(uint64_t frame_id, GpuTimingInvalidReason reason) = 0;
    virtual GpuTimingPollResult poll_completed_frame(GpuFrameTimingSample& out) = 0;
    virtual GpuTimingTelemetryInfo get_info() const = 0;
};
```

- `GPU.Frame` 是主 graphics command buffer 的 GPU workload，不包含 upload submission、CPU submit/present 或显示延迟。
- `commit_frame` 只在 backend 已确认 exact command buffer 进入实际提交且 completion primitive 建立后返回 `true`；`false` 不得留下可轮询 Pending slot，也不得计入 PerfGate `submitted`。若执行/完成状态无法证明，backend 必须 quarantine slot/resource 而非立即复用。
- ring depth = 3；metric count = 11；每帧 22 timestamps；backend query capacity = 66。
- metric duplicate、overflow、pair incomplete、未提交、device removed 均显式 invalid，不写 0 ms。
- `VegetationFullPipeline` 只接受单 timed scene view；多 view 造成 duplicate metric 时 sample invalid。
- `GPU.Shadows` 只含 shadow-map/depth generation；shadow-mask evaluation 归 `GPU.DeferredLighting`。
- GPU coverage = `valid / submitted`；各 required metric coverage = `present / submitted`；两者都必须 ≥ 0.95。
- drain 最多 5 秒，所有采样窗口 frame 离开 Pending 时可提前结束；禁止 `wait_idle`。

## Task 1: Add the common GPU timing contract and deterministic state tests

**Files:**

- Create: `project/src/engine/Graphics/GpuTimingTelemetryRHI.h`
- Create: `project/src/engine/Graphics/GpuTimingTelemetryRHI.cpp`
- Create: `project/src/tests/Function/gpu_timing_telemetry_tests.cpp`
- Modify: `project/src/engine/Graphics/GraphicsContext.h`

- [x] **Step 1: Write failing contract tests**

Add doctest cases for:

```cpp
TEST_CASE("GPU timing metric names are stable")
{
    CHECK(gpu_timing_metric_name(GpuTimingMetric::Frame) == "GPU.Frame");
    CHECK(gpu_timing_metric_name(GpuTimingMetric::ToneMapAndOverlays) == "GPU.ToneMapAndOverlays");
    CHECK(gpu_timing_metric_count() == 11);
}

TEST_CASE("GPU timing frame state rejects duplicate and incomplete scopes");
TEST_CASE("GPU timing poll distinguishes pending from empty");
TEST_CASE("GPU timing timestamp wrap handles 36 and 64 valid bits");
TEST_CASE("GPU timing ticks convert to milliseconds with double precision");
```

Expected failure: headers/types/helpers do not exist.

- [x] **Step 2: Run the focused test and confirm RED**

Run:

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="GPU timing*"
```

Expected: build/test failure caused only by the missing timing contract.

- [x] **Step 3: Implement the smallest backend-neutral contract**

Use fixed-capacity types only:

```cpp
constexpr uint32_t k_gpu_timing_ring_depth = 3;
constexpr uint32_t k_gpu_timing_metric_count = 11;
constexpr uint32_t k_gpu_timing_queries_per_frame = 22;

struct GpuFrameTimingSample
{
    uint64_t frame_id = 0;
    bool valid = false;
    GpuTimingInvalidReason invalid_reason = GpuTimingInvalidReason::None;
    std::array<GpuMetricTiming, k_gpu_timing_metric_count> metrics{};
};
```

Include a common frame-state helper used by both backends for begin/end/duplicate/capacity validation. Do not allocate strings/vectors in begin/end/poll. `GpuTimingTelemetryInfo` owns cold-path adapter/driver strings and records backend, period/frequency, valid bits, capacity and `scope="main_command_buffer"`.

Add to `GraphicsContextInitConfig`:

```cpp
bool enableGpuTimingTelemetry = false;
```

Add a default-null virtual getter to `GraphicsContext`; disabled mode must not create a telemetry object.

- [x] **Step 4: Run tests and architecture gate**

```bat
RunTests.bat Debug --test-case="GPU timing*"
RunArchGate.bat
```

Expected: PASS; no Function/Graphics dependency reversal.

- [x] **Step 5: Commit the common contract**

```bat
git add project/src/engine/Graphics/GpuTimingTelemetryRHI.h project/src/engine/Graphics/GpuTimingTelemetryRHI.cpp project/src/engine/Graphics/GraphicsContext.h project/src/tests/Function/gpu_timing_telemetry_tests.cpp
git commit -m "feat(graphics): add gpu timing telemetry contract"
```

## Task 2: Move PerfGate launch configuration before RHI initialization

**Files:**

- Modify: `project/src/engine/EntryPoint.h`
- Modify: `project/src/engine/Function/Application.h`
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `project/src/tests/Function/application_automation_tests.cpp`

- [x] **Step 1: Write failing CLI and ordering tests**

Append tests to the existing EntryPoint test translation unit; do not include `EntryPoint.h` from a second test file.

Cover:

```text
--window-width=2560 --window-height=1440                  -> accepted
only one dimension / zero / negative / > uint16 max      -> fatal parse result
--perf-gate-gpu-timing=on|off                            -> explicit mode
--perf-gate-validation=on|off                            -> explicit override
--perf-gate-vsync=on|off                                 -> explicit override
--perf-gate-drain-seconds=5                              -> accepted finite positive
```

Add a source-order regression assertion that PerfGate/extent/scene/runtime overrides are injected before `application->initialize()`.

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="*PerfGate launch*"
```

Expected: new arguments/order are unsupported.

- [x] **Step 3: Implement strict parsing and pre-init injection**

Extend `PerfGateConfig` with GPU timing, drain, validation and vsync override fields. Add one paired extent parser with range checking; reject half-specified dimensions rather than falling back.

Reorder `main` to:

```text
parse rhi/perf/extent/scene/runtime options
→ create application
→ inject all pre-init overrides
→ initialize
→ inject post-init watchdog/frame-dump controls
→ start
```

`Application::configure_perf_gate()` now stores pending config before initialize. During initialize:

- apply width/height to `EngineInitConfig` before window creation;
- override runtime vsync before Window/Swapchain creation;
- override both backend validation configs before context init;
- set `GraphicsContextInitConfig.enableGpuTimingTelemetry` only when requested;
- after backend resolution, configure `PerfGateController` with the resolved backend;
- retain resolved vsync/validation/configuration metadata for the report.

Keep CLI overrides process-local; never rewrite `product/config/Engine.ini`.

- [x] **Step 4: Verify parser and startup behavior**

```bat
RunTests.bat Debug --test-case="*PerfGate launch*"
build_sandbox.bat Debug
run.bat sandbox vulkan Debug --smoke-test-seconds=30 --window-width=2560 --window-height=1440
```

Expected: tests/build/smoke PASS; log reports 2560×1440 and the requested vsync/validation values.

- [x] **Step 5: Commit launch contract**

```bat
git add project/src/engine/EntryPoint.h project/src/engine/Function/Application.h project/src/engine/Function/Application.cpp project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp project/src/tests/Function/application_automation_tests.cpp
git commit -m "feat(perf): configure gpu timing before rhi init"
```

## Task 3: Implement Vulkan timestamp telemetry on recycled frame slots

**Files:**

- Create: `project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.h`
- Create: `project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.cpp`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.cpp`
- Modify: `project/src/tests/Function/gpu_timing_telemetry_tests.cpp`

- [x] **Step 1: Add failing Vulkan-independent edge tests**

Use the common frame-state/conversion seam to cover:

```text
timestampValidBits == 0                         -> Unsupported
36-bit wrap                                     -> correct masked delta
64-bit delta                                    -> no 1ULL << 64 undefined shift
availability == 0                               -> Pending, not zero duration
ring slot still Pending                         -> no overwrite
aborted/unsubmitted frame                       -> no valid sample
```

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="GPU timing Vulkan*"
```

- [x] **Step 3: Implement the Vulkan backend**

Create one context-owned `VkQueryPool` with 66 timestamp slots, segmented by the existing three backend frame slots. Persist:

- `VkPhysicalDeviceLimits::timestampPeriod` as `double` nanoseconds/tick;
- main queue family `timestampValidBits`;
- driver name/info from `VkPhysicalDeviceDriverProperties` when available;
- adapter/vendor/device metadata already available in physical-device properties.

Recording contract:

```text
slot was waited/recycled
→ resolve old slot with VK_QUERY_RESULT_64_BIT | WITH_AVAILABILITY_BIT (never WAIT_BIT)
→ vkCmdResetQueryPool for current slot as first timing command
→ write frame/group timestamps
→ end frame timestamps
→ queue submit succeeds
→ commit frame against that slot/completion value
```

Hook `resolve_recycled_slot(currentFrame)` after the existing timeline wait or fence wait succeeds and before allocator/pool reuse. `poll_completed_frame()` only pops a fixed CPU completed queue and never calls a wait function.

Do not reuse Tracy query storage and do not change Tracy enablement. Disabled mode creates no *new structured-telemetry* query pool.

- [x] **Step 4: Add static no-wait checks and build**

```powershell
rg -n "VK_QUERY_RESULT_WAIT_BIT|vkDeviceWaitIdle|wait_for_frame_completion" project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.*
```

Expected: no matches.

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="GPU timing*"
build_sandbox.bat Debug
```

Expected: PASS.

- [x] **Step 5: Commit Vulkan implementation**

```bat
git add project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.h project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.cpp project/src/engine/Graphics/Vulkan/VulkanContext.h project/src/engine/Graphics/Vulkan/VulkanContext.cpp project/src/tests/Function/gpu_timing_telemetry_tests.cpp
git commit -m "feat(vulkan): add nonblocking gpu timing telemetry"
```

## Task 4: Implement DX12 timestamp telemetry with per-slot readback

**Files:**

- Create: `project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.h`
- Create: `project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.h`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Fence.h`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Fence.cpp`
- Modify: `project/src/tests/Function/gpu_timing_telemetry_tests.cpp`

- [x] **Step 1: Add failing DX12-independent edge tests**

Cover frequency failure/zero, 64-bit tick conversion, fence Pending, `UINT64_MAX` device removed, exact command-list submit, readback offset/capacity and skipped submit.

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="GPU timing DX12*"
```

- [x] **Step 3: Make frame-fence signal observable**

Change `DX12Fence::signal` to report HRESULT success and the signaled target value. Update all callers explicitly; do not silently convert a failed signal into Pending forever.

- [x] **Step 4: Implement the DX12 backend**

Create one 66-entry `D3D12_QUERY_HEAP_TYPE_TIMESTAMP` heap and one persistently mapped, segmented readback resource. Query heap has no reset command: issue only `EndQuery`. At `end_frame`, resolve only the contiguous range actually issued, after the last timestamp and before command-list close.

Use the direct graphics queue `GetTimestampFrequency()` once during enabled initialization. `DX12Context::submit()` must verify the exact native command list entered `ExecuteCommandLists`, then bind the timing slot to the successfully signaled existing frame fence. Slot harvest happens after the existing begin-frame fence wait, before allocator reuse.

Do not reuse the staging pool, descriptor heaps or Tracy private heap/fence.

- [x] **Step 5: Prove poll is nonblocking and build both configurations**

```powershell
rg -n "wait_idle|WaitForSingleObject|\.wait\(" project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.*
```

Expected: no matches.

```bat
RunTests.bat Debug --test-case="GPU timing*"
build_sandbox.bat Debug
build_sandbox.bat Release
```

- [x] **Step 6: Commit DX12 implementation**

```bat
git add project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.h project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.cpp project/src/engine/Graphics/DirectX12/DX12Context.h project/src/engine/Graphics/DirectX12/DX12Context.cpp project/src/engine/Graphics/DirectX12/DX12Fence.h project/src/engine/Graphics/DirectX12/DX12Fence.cpp project/src/tests/Function/gpu_timing_telemetry_tests.cpp
git commit -m "feat(dx12): add nonblocking gpu timing telemetry"
```

## Task 5: Integrate frame recording, submit/abort and nonblocking sample transport

**Files:**

- Modify: `project/src/engine/Graphics/GpuTimingTelemetryRHI.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.h`
- Modify: `project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.cpp`
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
- Modify: `project/src/engine/Function/Render/Renderer.h`
- Modify: `project/src/engine/Function/Render/Renderer.cpp`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.cpp`
- Modify: `project/src/tests/Function/gpu_timing_telemetry_tests.cpp`
- Modify: `project/src/tests/Function/render_graph_gpu_metric_tests.cpp`

- [x] **Step 1: Add failing lifecycle tests with a fake telemetry object**

Test the shared recording coordinator for:

```text
Retryable acquire                           -> no timing frame begun
begin_record success                        -> begin_frame once with canonical renderer frame ID
end_record path                             -> end_frame before command close
successful exact submit                     -> commit once
recording or exact submit definitely skipped -> false, no Pending, slot safely reusable
execution/completion state unprovable        -> false, no pollable Pending, slot quarantined
quarantined slot                             -> later begin rejected until proof or shutdown
completed queue may return up to ring depth  -> fixed-capacity transport, no vector allocation
```

The RED suite must distinguish safe abort from uncertain execution. It must mechanically prove that an unprovable submission cannot make the physical query/readback slot reusable, even though it is excluded from the pollable Pending queue and from PerfGate `submitted`.

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="GPU timing lifecycle*"
```

- [x] **Step 3: Record `GPU.Frame` in the main command buffer**

In `RenderDevice::begin_frame()`, start timing only after swapchain acquire succeeds and `begin_record()` returns true. Use `RenderDevice::Impl::frame_index` as the canonical successful render frame ID; never use Vulkan/DX12 backend slot counters.

In `RenderDevice::end_frame()`, close `GPU.Frame` and record backend resolve before `end_record()`. All early exits between begin/end must call `abort_frame`.

Backend submission records a binding only after the exact command buffer enters the executed batch and its completion primitive is established. `RenderDevice` calls `commit_frame` after backend `end_frame`; the returned boolean is the sole submitted acknowledgement surfaced to `RendererFrameStats`. A failed acknowledgement must not create Pending work: safely unsubmitted recordings are aborted, while an unprovable execution/completion state quarantines the slot/resource.

- [x] **Step 4: Drain completed samples into fixed Renderer stats**

Extend `RendererFrameStats` with:

```cpp
uint64_t render_frame_id = 0;
bool gpu_timing_frame_submitted = false;
std::array<RHI::GpuFrameTimingSample, RHI::k_gpu_timing_ring_depth> completed_gpu_samples{};
uint32_t completed_gpu_sample_count = 0;
```

At renderer begin/end timing completion, poll until `Ready` is exhausted; stop immediately on `Pending` or `Empty`. Copy samples and the exact commit acknowledgement into `m_last_completed_frame_stats` before `Application` calls `PerfGateController::sample_after_frame()`. Task 7 records a submitted frame ID only when `gpu_timing_frame_submitted` is true.

- [x] **Step 5: Run focused tests and dual-backend smoke**

```bat
RunTests.bat Debug --test-case="GPU timing*"
run.bat sandbox vulkan Debug --run-for-seconds=20 --perf-gate --perf-gate-gpu-timing=on --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=5 --perf-gate-drain-seconds=5
run.bat sandbox dx12 Debug --run-for-seconds=20 --perf-gate --perf-gate-gpu-timing=on --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=5 --perf-gate-drain-seconds=5
```

Expected: no hang; samples show delayed frame IDs; no validation/debug-layer errors.

- [x] **Step 6: Commit frame lifecycle integration**

```bat
git add project/src/engine/Graphics/GpuTimingTelemetryRHI.h project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.h project/src/engine/Graphics/Vulkan/VulkanGpuTimingTelemetry.cpp project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.h project/src/engine/Graphics/DirectX12/DX12GpuTimingTelemetry.cpp project/src/engine/Function/Render/RenderDevice.h project/src/engine/Function/Render/RenderDevice.cpp project/src/engine/Function/Render/Renderer.h project/src/engine/Function/Render/Renderer.cpp project/src/engine/Graphics/Vulkan/VulkanContext.cpp project/src/engine/Graphics/DirectX12/DX12Context.cpp project/src/tests/Function/gpu_timing_telemetry_tests.cpp project/src/tests/Function/render_graph_gpu_metric_tests.cpp
git commit -m "feat(render): collect delayed gpu frame timings"
```

## Task 6: Make GPU timing groups explicit in RenderGraph

**Files:**

- Create: `project/src/tests/Function/render_graph_gpu_metric_tests.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphPass.h`
- Modify: `project/src/engine/Function/Render/RenderGraphBuilder.h`
- Modify: `project/src/engine/Function/Render/RenderGraphBuilder.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphCompiler.cpp`
- Modify: `project/src/engine/Function/Render/RenderGraphExecutor.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`
- Modify: `project/src/engine/Function/Render/BloomPass.cpp`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Modify: `project/src/engine/Function/Render/DirectionalLightShadowPass.cpp`
- Modify: `project/src/engine/Function/Render/EnvironmentLightingPass.cpp`
- Modify: `project/src/engine/Function/Render/ParticleSystemPass.cpp`
- Modify: `project/src/engine/Function/Render/PostProcessToneMapPass.cpp`
- Modify: `project/src/engine/Function/Render/RenderDebugView.cpp`
- Modify: `project/src/engine/Function/Render/SkyBackgroundPass.cpp`
- Modify: `project/src/engine/Function/Render/SunLightShadowPass.cpp`
- Modify: `project/src/engine/Function/Render/TemporalAAPass.cpp`
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`

- [x] **Step 1: Write failing RenderGraph metadata/cache tests**

Add tests proving:

```text
every added pass has an explicit metric tag
metric tag participates in topology/cache hash and equality
culled passes do not create timing transitions
adjacent live passes with the same metric produce one group scope
metric change or end-of-graph closes the active scope
disabled telemetry makes zero begin/end calls
```

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="RenderGraph GPU metric*"
```

- [x] **Step 3: Add explicit pass metric to the API**

Add `RHI::GpuTimingMetric timing_metric` to `RenderGraphPassNode`. Make it a required argument to both `add_raster_pass` and `add_compute_pass`; do not provide a default that lets new passes silently omit classification. Use `Untracked` only after an explicit call-site decision.

Include the metric in compiler topology hash/cache equality.

- [x] **Step 4: Emit scopes around compiled live pass groups**

In `RenderGraphExecutor`, transition scopes from the compiled live-pass sequence, not around graph-building calls. End an active raster pass before changing metric. Always close a group on execute failure.

Assign all current call sites:

| Metric | Pass ownership |
| --- | --- |
| GBuffer | SceneRenderer depth/gbuffer/material raster work |
| AmbientOcclusion | AmbientOcclusionPass |
| Shadows | DirectionalLightShadowPass, SunLightShadowPass depth generation |
| DeferredLighting | DeferredLightingPass, shadow-mask evaluation, direct light accumulation |
| EnvironmentAndSky | EnvironmentLightingPass, SkyBackgroundPass |
| Particles | ParticleSystemPass |
| VolumetricLighting | VolumetricLightingPass |
| Bloom | BloomPass |
| TemporalAA | TemporalAAPass |
| ToneMapAndOverlays | PostProcessToneMapPass, RenderDebugView, overlay/composite passes |

- [x] **Step 5: Run tests, arch gate and RenderGate**

```bat
RunTests.bat Debug --test-case="RenderGraph GPU metric*"
RunArchGate.bat
RunRenderGate.bat
```

Expected: all PASS; telemetry is off in RenderGate and images remain unchanged. Do not bless.

- [x] **Step 6: Commit RenderGraph metric integration**

```bat
git add project/src/engine/Function/Render/RenderGraphPass.h project/src/engine/Function/Render/RenderGraphBuilder.h project/src/engine/Function/Render/RenderGraphBuilder.cpp project/src/engine/Function/Render/RenderGraphCompiler.cpp project/src/engine/Function/Render/RenderGraphExecutor.cpp project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/AmbientOcclusionPass.cpp project/src/engine/Function/Render/BloomPass.cpp project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Function/Render/DirectionalLightShadowPass.cpp project/src/engine/Function/Render/EnvironmentLightingPass.cpp project/src/engine/Function/Render/ParticleSystemPass.cpp project/src/engine/Function/Render/PostProcessToneMapPass.cpp project/src/engine/Function/Render/RenderDebugView.cpp project/src/engine/Function/Render/SkyBackgroundPass.cpp project/src/engine/Function/Render/SunLightShadowPass.cpp project/src/engine/Function/Render/TemporalAAPass.cpp project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/tests/Function/render_graph_gpu_metric_tests.cpp
git commit -m "feat(rendergraph): annotate gpu timing metric groups"
```

Before committing, inspect `git diff --cached --name-only` and ensure every staged path belongs to this task.

## Task 7: Add PerfGate GPU summaries, drain state and schema v2

**Files:**

- Create: `project/src/tests/Function/perf_gate_tests.cpp`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/engine/Function/Render/Renderer.h`

- [x] **Step 1: Write failing pure-data and state-machine tests**

Cover:

```text
CPU percentile behavior remains unchanged
Warmup frame resolved during Sampling is excluded by frame ID
Sampling frame resolved during Draining is included
valid/submitted and per-metric present/submitted coverage
invalid samples excluded from percentile but counted by reason
all pending resolved -> drain completes early
5-second drain timeout -> unresolved count and Complete
extent change inside sample window -> invalid run
schema v2 retains every schema v1 CPU field
11 metric summaries expose avg/p50/p95/p99/min/max
```

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="PerfGate GPU*"
```

- [x] **Step 3: Implement the controller state machine**

Use explicit `Warmup`, `Sampling`, `Draining`, `Complete`. Record submitted renderer frame IDs, accept resolved samples by their own IDs, and request exit only in Complete. Do not infer submitted/resolved counts from poll order.

At drain timeout, classify remaining IDs as unresolved without waiting for the device. A GPU timing profile fails if total or any required metric coverage is below the profile threshold.

- [x] **Step 4: Write schema v2 while preserving v1 CPU fields**

Add:

```json
{
  "schema_version": 2,
  "runtime": {
    "configuration": "Release",
    "extent": { "width": 2560, "height": 1440, "stable": true },
    "vsync": false,
    "frame_cap": "off",
    "validation": false
  },
  "gpu": {
    "scope": "main_command_buffer",
    "submitted": 0,
    "resolved": 0,
    "valid": 0,
    "invalid": 0,
    "coverage": 0.0,
    "invalid_reasons": {},
    "backend_info": {},
    "metrics": {}
  }
}
```

Record adapter/driver/OS/backend/timestamp metadata and required-metric coverage. Do not serialize a missing/invalid duration as `0.0`.

- [x] **Step 5: Run tests and short schema runs**

```bat
RunTests.bat Debug --test-case="PerfGate*"
run.bat sandbox vulkan Debug --run-for-seconds=20 --perf-gate --perf-gate-gpu-timing=on --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=5 --perf-gate-drain-seconds=5 --perf-gate-output=Intermediate/test-reports/perf-gate/dev-vulkan.json
run.bat sandbox dx12 Debug --run-for-seconds=20 --perf-gate --perf-gate-gpu-timing=on --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=5 --perf-gate-drain-seconds=5 --perf-gate-output=Intermediate/test-reports/perf-gate/dev-dx12.json
```

Expected: schema 2, nonzero submitted/resolved counts, frame IDs delayed but correctly associated.

- [x] **Step 6: Commit PerfGate schema/state**

```bat
git add project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp project/src/engine/Function/Application.cpp project/src/engine/Function/Render/Renderer.h project/src/tests/Function/perf_gate_tests.cpp
git commit -m "feat(perf): report gpu timing schema v2"
```

## Task 8: Add the fixed full-pipeline benchmark scene and camera mode

**Files:**

- Create: `product/assets/scenes/VegetationBaseline.scene.json`
- Create: `project/src/tests/Scene/vegetation_baseline_scene_tests.cpp`
- Modify: `project/src/sandbox/App/SandboxApplication.h`
- Modify: `project/src/sandbox/App/SandboxApplication.cpp`
- Modify: `project/src/sandbox/App/SandboxStandardScene.h`
- Modify: `project/src/sandbox/App/SandboxStandardScene.cpp`
- Modify: `project/src/sandbox/App/SandboxFreeCameraController.cpp` only if the existing application-level skip cannot keep the camera fixed

- [x] **Step 1: Write a failing scene contract test**

Load the new scene through existing scene serialization and assert:

```text
schema version 5
exactly one primary camera with fixed transform
at least one renderable mesh and every referenced asset exists
active environment/IBL/sky
shadow-casting directional sunlight
at least one emitting particle component
AO, directional shadows, Bloom, volumetric lighting, TAA and tone map enabled
no VegetationComponent / no vegetation instance data
```

- [x] **Step 2: Confirm RED**

```bat
RunTests.bat Debug --test-case="Vegetation baseline scene*"
```

- [x] **Step 3: Author an independent scene asset**

Create the file from committed assets and schema examples. Do not copy or normalize the dirty working-tree `Sandbox.scene.json`. Use existing Sponza/environment/particle references and make every full-pipeline switch explicit in JSON.

- [x] **Step 4: Freeze camera only in benchmark/PerfGate mode**

Pass a fixed-camera flag from `SandboxApplication` to the standard scene/controller. In that mode, load the primary camera transform from scene JSON and skip free-camera updates; normal Sandbox interaction remains unchanged. Report the fixed-camera mode in PerfGate runtime metadata.

- [x] **Step 5: Verify scene load and both backends**

```bat
RunTests.bat Debug --test-case="Vegetation baseline scene*"
run.bat sandbox vulkan Debug --scene=product/assets/scenes/VegetationBaseline.scene.json --smoke-test-seconds=120
run.bat sandbox dx12 Debug --scene=product/assets/scenes/VegetationBaseline.scene.json --smoke-test-seconds=120
```

Expected: readiness PASS; logs contain no missing asset/validation errors.

- [x] **Step 6: Commit scene contract**

```bat
git add product/assets/scenes/VegetationBaseline.scene.json project/src/tests/Scene/vegetation_baseline_scene_tests.cpp project/src/sandbox/App/SandboxApplication.h project/src/sandbox/App/SandboxApplication.cpp project/src/sandbox/App/SandboxStandardScene.h project/src/sandbox/App/SandboxStandardScene.cpp
git commit -m "feat(sandbox): add fixed full-pipeline perf scene"
```

Add `SandboxFreeCameraController.cpp` only if it was actually changed.

## Task 9: Separate profile configuration and upgrade the PowerShell gate

**Files:**

- Create: `tools/perf/perf_gate_profiles.json`
- Create: `scripts/PerfGateProfileConfig.ps1`
- Modify: `scripts/RunPerfGate.ps1`
- Modify: `scripts/RunPerfGateMenu.ps1`
- Modify: `scripts/TestRunPerfGate.ps1`
- Modify: `RunPerfGate.bat`

- [x] **Step 1: Add failing script self-tests**

Extend `-SelfTest` to cover:

```text
new profile file wins over baseline.profiles
missing new profile falls back to baseline.profiles
unknown profile fails
locked Release profile rejects -Configuration Debug and menu override
runner propagates --rhi, --scene, extent, vsync, validation, timing and drain args
only declared targets/backends build and run
schema v1 remains accepted for Standard
schema v2 GPU coverage/required metric/extent failures are enforced
missing baseline produces candidate/MISSING but not automatic FAIL
telemetry-off A/B cannot be used with -BlessBaseline
bless writer changes only baselines.*, never profile definitions
```

- [x] **Step 2: Confirm RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
```

- [x] **Step 3: Add shared profile loading**

`PerfGateProfileConfig.ps1` is the single loader used by both runner and menu. It first reads `tools/perf/perf_gate_profiles.json`, then falls back per profile to `perf_gate_baselines.json.profiles`.

Define:

```json
{
  "schema_version": 1,
  "profiles": {
    "VegetationFullPipeline": {
      "configuration": "Release",
      "configuration_locked": true,
      "targets": [{ "name": "Sandbox", "backends": ["Vulkan", "DX12"] }],
      "scene": "product/assets/scenes/VegetationBaseline.scene.json",
      "window_width": 2560,
      "window_height": 1440,
      "warmup_seconds": 10,
      "sample_seconds": 30,
      "drain_seconds": 5,
      "timeout_seconds": 90,
      "gpu_timing": "required",
      "min_gpu_coverage": 0.95,
      "required_gpu_metrics": [
        "GPU.Frame", "GPU.GBuffer", "GPU.AmbientOcclusion", "GPU.Shadows",
        "GPU.DeferredLighting", "GPU.EnvironmentAndSky", "GPU.Particles",
        "GPU.VolumetricLighting", "GPU.Bloom", "GPU.TemporalAA",
        "GPU.ToneMapAndOverlays"
      ],
      "vsync": false,
      "validation": false,
      "frame_cap": "off",
      "fixed_camera": true
    }
  }
}
```

- [x] **Step 4: Stop mutating Engine.ini for backend selection**

Pass `--rhi=vulkan|dx12` and process-local runtime overrides directly. Preserve/restore logic only where still needed for legacy Standard; prefer converting Standard to `--rhi` in the same focused script change if self-tests prove identical behavior.

Build only the profile-declared target. Vegetation profile must run exactly two records: Release Sandbox Vulkan and Release Sandbox DX12.

- [x] **Step 5: Upgrade report and candidate/baseline handling**

Summary JSON/Markdown add GPU Avg/P95, coverage, adapter/driver, actual extent, validation and vsync columns. When no baseline exists, mark `baseline_status=MISSING`, `baseline_blessed=false` and retain the candidate report. Future bless writes CPU and required GPU metric avg/p95 only after explicit user authorization.

Add a diagnostic `-TelemetryMode Off` A/B override; forbid it with `-BlessBaseline` and label its report non-comparable for GPU baseline.

- [x] **Step 6: Make all script tests pass**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile VegetationFullPipeline -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGateMenu.ps1 -Preview
```

Expected: PASS; dry-run shows two Release Sandbox commands with fixed args; preview uses the shared loader.

- [x] **Step 7: Commit tooling/profile config**

```bat
git add tools/perf/perf_gate_profiles.json scripts/PerfGateProfileConfig.ps1 scripts/RunPerfGate.ps1 scripts/RunPerfGateMenu.ps1 scripts/TestRunPerfGate.ps1 RunPerfGate.bat
git commit -m "feat(perf): add fixed 2k gpu timing profile"
```

Verify `tools/perf/perf_gate_baselines.json` is not staged and has no diff.

## Task 10: Run validation, produce the candidate report and update long-term docs

**Files:**

- Modify: `docs/specs/modules/application.md`
- Modify: `docs/specs/modules/graphics.md`
- Modify: `docs/specs/modules/render.md`
- Modify: `docs/specs/modules/render-graph.md`
- Modify: `docs/specs/modules/sandbox.md`
- Modify: `docs/specs/modules/tools.md`
- Modify: `docs/PerfGateUsageGuide.md`
- Modify: `docs/CODEBASE_MAP.md`
- Modify: `docs/VERIFY.md`
- Modify: `docs/sdd/SDD-2026-07-13-gpu-performance-observability.md`
- Modify: `docs/sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md`
- Modify: `docs/plans/README.md`

- [x] **Step 1: Run the full automated verification matrix**

```bat
generate_vs2022.bat
RunTests.bat Debug
RunArchGate.bat
build_editor.bat Debug
build_sandbox.bat Debug
build_sandbox.bat Release
RunRenderGate.bat
RunPerfGate.bat -Profile Standard
```

Expected: all PASS. PerfGate WARN requires a written judgment; FAIL blocks completion. RenderGate must not bless.

- [x] **Step 2: Run backend validation correctness checks**

Run short Debug checks; `--perf-gate-validation=on` means Vulkan validation + synchronization validation or DX12 debug layer + GPU validation for this process:

```bat
run.bat sandbox vulkan Debug --scene=product/assets/scenes/VegetationBaseline.scene.json --window-width=2560 --window-height=1440 --run-for-seconds=20 --perf-gate --perf-gate-gpu-timing=on --perf-gate-validation=on --perf-gate-vsync=off --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=5 --perf-gate-drain-seconds=5 --perf-gate-output=Intermediate/test-reports/perf-gate/validation-vulkan.json
run.bat sandbox dx12 Debug --scene=product/assets/scenes/VegetationBaseline.scene.json --window-width=2560 --window-height=1440 --run-for-seconds=20 --perf-gate --perf-gate-gpu-timing=on --perf-gate-validation=on --perf-gate-vsync=off --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=5 --perf-gate-drain-seconds=5 --perf-gate-output=Intermediate/test-reports/perf-gate/validation-dx12.json
```

Inspect `product/logs` for query reset, availability, resolve, fence, device-removal and lifetime errors.

Expected: zero validation/debug-layer errors. Performance numbers from these runs are correctness-only and never baseline candidates.

- [x] **Step 3: Run the fixed Release candidate profile**

```bat
RunPerfGate.bat -Profile VegetationFullPipeline
```

Expected:

- exactly Sandbox Release × Vulkan/DX12;
- actual extent remains 2560×1440;
- GPU total and all required metric coverage ≥ 0.95;
- adapter/driver/OS/configuration/vsync/validation/fixed-camera metadata complete;
- missing baseline is reported as candidate, not silently blessed;
- output lands only under `Intermediate/test-reports/perf-gate/`.

- [x] **Step 4: Measure telemetry overhead A/B**

```bat
RunPerfGate.bat -Profile VegetationFullPipeline -TelemetryMode Off
```

Compare CPU avg/p95 against the instrumented run with the same backend/scene/extent/Tracy state. Record the delta in the candidate summary; do not subtract it from reported GPU values and do not bless either run.

- [x] **Step 5: Prove protected baselines did not change**

```bat
git diff --exit-code -- tools/perf/perf_gate_baselines.json tools/render/goldens
```

Expected: exit code 0.

- [x] **Step 6: Update long-term documentation from measured behavior**

Document:

- Graphics timing lifecycle, backend completion semantics and disabled-mode cost;
- RenderGraph explicit metric tag/cache behavior;
- Application CLI precedence and fixed benchmark launch contract;
- PerfGate schema v2, profile routing, coverage/drain and candidate/bless workflow;
- fixed scene/camera constraints and actual candidate hardware/driver metadata;
- all commands/results, including any justified WARN.

Set Phase 0 SDD to Done only after every required command passes and the candidate report exists. Update the master SDD Phase 0 row to Done; do not advance Phase 1 automatically. Move this plan entry from Active to Archived.

- [x] **Step 7: Validate the plan-derived change set**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check -- docs project/src scripts tools/perf product/assets/scenes RunPerfGate.bat
```

When global `git diff --check` reports pre-existing trailing whitespace in user-owned dirty files, scope the evidence to the files touched by this plan and report the unrelated findings without editing them.

- [x] **Step 8: Commit docs and hand off the candidate report**

```bat
git add docs/specs/modules/application.md docs/specs/modules/graphics.md docs/specs/modules/render.md docs/specs/modules/render-graph.md docs/specs/modules/sandbox.md docs/specs/modules/tools.md docs/PerfGateUsageGuide.md docs/CODEBASE_MAP.md docs/VERIFY.md docs/sdd/SDD-2026-07-13-gpu-performance-observability.md docs/sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md docs/plans/README.md
git commit -m "docs(perf): record gpu timing phase zero results"
```

Do not commit `Intermediate/` reports. Hand the report path and key measurements to the user for baseline review. Only a later explicit approval may run:

```bat
RunPerfGate.bat -Profile VegetationFullPipeline -BlessBaseline
```

That bless is outside this implementation plan.

## Final acceptance checklist

- [x] Telemetry disabled adds no structured timing resource, timestamp command or readback poll.
- [x] Vulkan and DX12 use identical metric names, units, validity and coverage semantics.
- [x] No telemetry poll path waits for GPU/device idle.
- [x] Retryable acquire, failed recording, skipped submit and device removal cannot generate a false valid sample.
- [x] RenderGraph metric tags participate in cache identity and only live passes produce scopes.
- [x] `VegetationFullPipeline` is fixed Release, 2560×1440, full pipeline, fixed camera, vsync/frame cap off.
- [x] `Standard` stays compatible and does not require GPU telemetry.
- [x] Candidate report exists for both hardware backends; no performance/render baseline changed.
- [x] All verification commands and validation-log audits are recorded in docs.
