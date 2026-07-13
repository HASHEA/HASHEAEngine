# Terrain Phase 0 GPU Timing and Feasibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver non-blocking, machine-readable GPU frame/pass timing on Vulkan and DX12, prove it without Tracy, and measure the fixed 2560 × 1440 empty-Editor baseline before Terrain rendering work begins.

**Architecture:** A backend-neutral `GpuTimingRHI` facade owns stable result types while each backend records timestamp pairs into per-frame query slots and binds those slots to its real submission completion primitive. Completed data moves to an ordered CPU FIFO; PerfGate correlates snapshots by submitted frame index, aggregates same-name scopes per frame, and fails on missing, duplicate, overflowed, or backend-error samples.

**Tech Stack:** C++17, doctest, Vulkan timestamp query pools, D3D12 timestamp query heaps/readback, Premake5, MSBuild, PowerShell PerfGate, JSON telemetry.

---

## Prerequisites and fixed contracts

- Read `docs/sdd/SDD-2026-07-13-terrain-system.md`, especially **Machine-readable GPU timing**.
- Read `docs/specs/modules/graphics.md`, `docs/specs/modules/render.md`, `docs/specs/modules/tools.md`, and `docs/VERIFY.md`.
- Do not modify public resource, barrier, upload, queue, or submission interfaces.
- `Pending` is legal only from `try_collect`; every recording method returns `Success` or an immediate failure.
- One submitted frame index is queued and published exactly once. Query slots are reused only after complete materialization into the CPU FIFO or failure.
- Do not edit `project/src/tests/premake5.lua`; it already globs new `**.cpp` tests.
- Phase exit is blocked if the empty Release Editor exceeds CPU or GPU frame P95 3.33 ms. Open a separate performance SDD instead of weakening the Terrain target.

## File map

- Create `project/src/engine/Graphics/GpuTimingRHI.h/.cpp`: public result, snapshot, stable hash, install/get facade.
- Create `project/src/engine/Graphics/GpuTimingFrameTracker.h`: header-only backend-private slot/FIFO state machine shared by Vulkan and DX12, so Tests exercises the same code without exporting it from Engine.dll.
- Create `project/src/engine/Graphics/Vulkan/VulkanGpuTiming.h/.cpp`: Vulkan timestamp query implementation.
- Create `project/src/engine/Graphics/DirectX12/DX12GpuTiming.h/.cpp`: DX12 timestamp query implementation.
- Modify `project/src/engine/Graphics/Vulkan/VulkanContext.*` and `DirectX12/DX12Context.*`: lifetime and private submit-completion hook.
- Modify `project/src/engine/Function/Render/RenderDevice.*` and `Renderer.*`: frame/pass recording and submitted-frame identity.
- Modify `project/src/engine/Function/Diagnostics/PerfGate.*`, `Function/Application.*`, and `EntryPoint.h`: expected-set correlation, drain state, fixed output extent and report schema v2.
- Modify `project/src/editor/Editor.*`, `editor/Panels/ViewportPanel.*`, and `scripts/RunPerfGate.ps1`: fixed perf-only render extent and Empty scenario.
- Create `product/assets/scenes/TerrainPerfEmpty.scene.json`: deterministic empty benchmark scene.
- Create `product/assets/scenes/TerrainPerfEmpty.manifest.json`: canonical camera/environment/light/render-config hash shared with TerrainPerf.
- Create `tools/perf/terrain_feasibility_contract.json`: non-blessable 3.33 ms CPU/GPU empty-Editor limits.
- Create tests under `project/src/tests/Graphics/` and `project/src/tests/Function/`.

### Task 1: Add the backend-neutral timing contract

**Files:**
- Create: `project/src/engine/Graphics/GpuTimingRHI.h`
- Create: `project/src/engine/Graphics/GpuTimingRHI.cpp`
- Create: `project/src/tests/Graphics/gpu_timing_contract_tests.cpp`

- [ ] **Step 1: Write the failing facade and hash tests**

Add doctest cases that verify `gpu_timing_install/get`, deterministic FNV-1a names, and distinct canonical Terrain names:

```cpp
TEST_CASE("GPU timing facade installs and clears one context")
{
    FakeGpuTimingContext fake;
    RHI::gpu_timing_install(&fake);
    CHECK(RHI::gpu_timing_get() == &fake);
    RHI::gpu_timing_install(nullptr);
    CHECK(RHI::gpu_timing_get() == nullptr);
}

TEST_CASE("GPU timing names use stable FNV-1a hashes")
{
    CHECK(RHI::gpu_timing_name_hash("Terrain.GBuffer") == 0xb740cda0611fe57bull);
    CHECK(RHI::gpu_timing_name_hash("Terrain.Shadow") != RHI::gpu_timing_name_hash("Terrain.GBuffer"));
}
```

- [ ] **Step 2: Run the focused tests and observe RED**

Run: `RunTests.bat Debug --test-case="GPU timing*"`

Expected: compile failure because `Graphics/GpuTimingRHI.h` and its symbols do not exist.

- [ ] **Step 3: Implement the exact public contract**

Define the SDD-approved `GpuTimingResult`, `GpuTimingScopeHandle`, `GpuTimingScopeSample`, `GpuTimingFrameSnapshot`, `IGpuTimingContext`, `gpu_timing_install/get`, and this constexpr hash. Mark only `gpu_timing_install` and `gpu_timing_get` with `ASH_API` so the Tests executable can verify the Engine-owned facade; the backend-private tracker remains header-only and unexported.

```cpp
constexpr auto gpu_timing_name_hash(const char* name) -> uint64_t
{
    uint64_t hash = 14695981039346656037ull;
    while (name && *name)
    {
        hash ^= static_cast<uint8_t>(*name++);
        hash *= 1099511628211ull;
    }
    return hash;
}
```

Use `std::array<GpuTimingScopeSample, 128>` in the snapshot. Keep the installed pointer in the `.cpp`; do not add Tracy includes or `TRACY_ENABLE` guards.

- [ ] **Step 4: Run the focused tests and observe GREEN**

Run: `RunTests.bat Debug --test-case="GPU timing*"`

Expected: all GPU timing facade/hash cases pass.

- [ ] **Step 5: Commit the contract**

```powershell
git add project/src/engine/Graphics/GpuTimingRHI.h project/src/engine/Graphics/GpuTimingRHI.cpp project/src/tests/Graphics/gpu_timing_contract_tests.cpp
git diff --cached --check
git commit -m "feat(graphics): add readable gpu timing contract"
```

### Task 2: Implement and test the slot/FIFO lifecycle

**Files:**
- Create: `project/src/engine/Graphics/GpuTimingFrameTracker.h`
- Create: `project/src/tests/Graphics/gpu_timing_frame_tracker_tests.cpp`

- [ ] **Step 1: Write lifecycle RED tests**

Cover query-slot `Idle -> Recording -> Submitted -> Completed -> Materialized -> Idle`, FIFO `Queued -> Published`, stale handles, duplicate frame indices, slot exhaustion, and FIFO exhaustion. The core assertion is:

```cpp
CHECK(tracker.begin_recording(17, slot) == RHI::GpuTimingResult::Success);
CHECK(tracker.mark_submitted(slot, 91) == RHI::GpuTimingResult::Success);
CHECK(tracker.begin_recording(18, second_slot) == RHI::GpuTimingResult::Success);
CHECK(second_slot != slot);
CHECK(tracker.mark_completed(slot, make_snapshot(17, 2.5)) == RHI::GpuTimingResult::Success);
CHECK(tracker.begin_recording(19, reused_slot) == RHI::GpuTimingResult::Success);
CHECK(reused_slot == slot);
CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
CHECK(snapshot.submitted_frame_index == 17);
CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Pending);
```

Add a separate test proving a slot cannot be reused before completion/materialization, and one proving FIFO overflow returns `CapacityExceeded` without replacing the oldest snapshot.

- [ ] **Step 2: Run the tracker tests and observe RED**

Run: `RunTests.bat Debug --test-case="GPU timing tracker*"`

Expected: compile failure because `GpuTimingFrameTracker` does not exist.

- [ ] **Step 3: Implement the deterministic tracker**

Implement the complete tracker inline in `GpuTimingFrameTracker.h`; do not export it with `ASH_API`. Use a fixed array of four frame slots and an eight-entry ring FIFO. Each slot stores state, generation, submitted frame index, backend completion value, and query count. Validate the generation in every handle. `mark_completed` first checks FIFO capacity, then copies the entire snapshot into the FIFO and releases the query slot; `try_publish` returns and removes only the oldest FIFO item. FIFO overflow sets a sticky `CapacityExceeded`, preserves all queued snapshots, and moves the completed slot to `Failed` before releasing it.

Do not allocate in `begin_recording`, `mark_submitted`, `mark_completed`, or `try_publish`.

- [ ] **Step 4: Run tracker and full unit tests**

Run:

```powershell
RunTests.bat Debug --test-case="GPU timing tracker*"
RunTests.bat Debug
```

Expected: focused tests and the full Debug suite pass.

- [ ] **Step 5: Commit the tracker**

```powershell
git add project/src/engine/Graphics/GpuTimingFrameTracker.h project/src/tests/Graphics/gpu_timing_frame_tracker_tests.cpp
git diff --cached --check
git commit -m "feat(graphics): track gpu timing frame lifecycle"
```

### Task 3: Implement Vulkan timestamp collection

**Files:**
- Create: `project/src/engine/Graphics/Vulkan/VulkanGpuTiming.h`
- Create: `project/src/engine/Graphics/Vulkan/VulkanGpuTiming.cpp`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.cpp`
- Create: `project/src/tests/Graphics/vulkan_gpu_timing_math_tests.cpp`

- [ ] **Step 1: Write RED tests for valid-bit masking and wraparound**

Define `vulkan_timestamp_delta` as a `constexpr` helper in `VulkanGpuTiming.h`, with no SDK object or exported symbol, and verify:

```cpp
CHECK(vulkan_timestamp_delta(0x00fffff0ull, 0x00000010ull, 24) == 0x20ull);
CHECK(vulkan_timestamp_delta(100ull, 340ull, 64) == 240ull);
CHECK(vulkan_timestamp_delta(0ull, 1ull, 0) == 0ull);
```

Run: `RunTests.bat Debug --test-case="Vulkan GPU timing*"`

Expected: compile failure because the Vulkan timing helper is absent.

- [ ] **Step 2: Implement the Vulkan context**

At initialization, read the selected graphics queue family's `timestampValidBits` and `VkPhysicalDeviceLimits::timestampPeriod`. Return `Unsupported` when valid bits are zero or the period is non-positive. Allocate one query pool region per tracker slot for `2 + 2 * 128` timestamps.

Record reset and begin/end timestamps in the active main command buffer. Use synchronization2 `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`; when the synchronization2 function is unavailable, use semantically equivalent top/bottom pipeline timestamp commands. Mask and subtract timestamps modulo `2^timestampValidBits` before multiplying by `timestampPeriod / 1'000'000.0`.

`VulkanContext::submit` only queues command buffers, so it must not mark timing as submitted. In `VulkanContext::end_frame`, immediately after the real `vkQueueSubmit`/`vkQueueSubmit2` succeeds, bind the timing slot to that frame's exact fence or timeline value. Queue-submit failure maps to `RecordFailed`. After completion, call `vkGetQueryPoolResults` without `VK_QUERY_RESULT_WAIT_BIT`, include availability values, and map unavailable/failed results to `ResolveFailed`. When frame close observes that the main command buffer was not submitted, cancel its recording slot back to `Idle` without producing a snapshot.

Add a source-contract test proving the private hook occurs after the `vkQueueSubmit` success branch in `end_frame`, not inside the enqueue-only `submit` method, and that it receives the frame completion value.

- [ ] **Step 3: Run focused tests and build both engine consumers**

```powershell
RunTests.bat Debug --test-case="Vulkan GPU timing*"
build_editor.bat Debug
build_sandbox.bat Debug
```

Expected: tests pass and both builds exit 0.

- [ ] **Step 4: Run ordinary Vulkan backend smoke**

Run: `run.bat editor vulkan Debug --smoke-test-seconds=120`

Expected: exit 0 and ordinary Vulkan validation logs are clean. Timing has not yet been wired into RenderDevice, so this step is backend sanity only and is not timing-validation evidence.

- [ ] **Step 5: Commit Vulkan support**

```powershell
git add project/src/engine/Graphics/Vulkan/VulkanGpuTiming.h project/src/engine/Graphics/Vulkan/VulkanGpuTiming.cpp project/src/engine/Graphics/Vulkan/VulkanContext.h project/src/engine/Graphics/Vulkan/VulkanContext.cpp project/src/tests/Graphics/vulkan_gpu_timing_math_tests.cpp
git diff --cached --check
git commit -m "feat(vulkan): collect nonblocking gpu timestamps"
```

### Task 4: Implement DX12 timestamp collection

**Files:**
- Create: `project/src/engine/Graphics/DirectX12/DX12GpuTiming.h`
- Create: `project/src/engine/Graphics/DirectX12/DX12GpuTiming.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Fence.h`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Fence.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.h`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.cpp`
- Create: `project/src/tests/Graphics/dx12_gpu_timing_math_tests.cpp`

- [ ] **Step 1: Write RED tests for frequency conversion and failure**

```cpp
CHECK(dx12_timestamp_ms(1000ull, 4000ull, 1'000'000ull) == doctest::Approx(3.0));
CHECK(dx12_validate_timestamp_frequency(0ull) == RHI::GpuTimingResult::QueueFrequencyInvalid);
CHECK(dx12_validate_timestamp_frequency(27'000'000ull) == RHI::GpuTimingResult::Success);
```

Run: `RunTests.bat Debug --test-case="DX12 GPU timing*"`

Expected: compile failure because the helpers do not exist.

- [ ] **Step 2: Implement the DX12 context**

Create a timestamp `ID3D12QueryHeap` and one persistently mapped readback resource with a non-overlapping range per tracker slot. Read `ID3D12CommandQueue::GetTimestampFrequency` once and map zero/failure to `QueueFrequencyInvalid`.

Define frequency conversion as a `constexpr` helper in `DX12GpuTiming.h`. Add backend-private `DX12Fence::signal_checked(ID3D12CommandQueue*, uint64_t& out_value) -> HRESULT`; it increments/reserves the value, calls `ID3D12CommandQueue::Signal`, and returns both HRESULT and the exact value. Keep the existing RHI `signal` override unchanged by making it call the checked helper and log failure. Record `EndQuery` for frame and scope pairs, then `ResolveQueryData` before the main command list closes. `ExecuteCommandLists` itself returns void, so in `DX12Context::submit` the checked fence signal is the trackable completion contract: only `SUCCEEDED(signal_checked)` may bind the timing slot to `out_value`. Map a removed device to `DeviceLost` and other signal failure to `RecordFailed`; never leave the slot to time out as `Pending`. Materialize only when `GetCompletedValue()` reaches the bound value; invalid readback maps to `ResolveFailed`. When frame close observes no main command-list submission, cancel the recording slot without resolving or queueing a snapshot.

Add a failure-injection/source-contract test proving a failed checked signal returns `DeviceLost`/`RecordFailed`, never calls `mark_submitted`, and a successful signal passes the exact returned value. Verify the timing slot is never marked submitted before the checked signal succeeds.

- [ ] **Step 3: Run focused tests, build, and ordinary DX12 smoke**

```powershell
RunTests.bat Debug --test-case="DX12 GPU timing*"
build_editor.bat Debug
build_sandbox.bat Debug
run.bat editor dx12 Debug --smoke-test-seconds=120
```

Expected: all commands exit 0 and the ordinary backend smoke is clean. Timing has not yet been wired into RenderDevice, so this step is build/backend sanity only and is not timing-validation evidence.

- [ ] **Step 4: Commit DX12 support**

```powershell
git add project/src/engine/Graphics/DirectX12/DX12GpuTiming.h project/src/engine/Graphics/DirectX12/DX12GpuTiming.cpp project/src/engine/Graphics/DirectX12/DX12Fence.h project/src/engine/Graphics/DirectX12/DX12Fence.cpp project/src/engine/Graphics/DirectX12/DX12Context.h project/src/engine/Graphics/DirectX12/DX12Context.cpp project/src/tests/Graphics/dx12_gpu_timing_math_tests.cpp
git diff --cached --check
git commit -m "feat(dx12): collect nonblocking gpu timestamps"
```

### Task 5: Record frames/passes and make PerfGate consume them

**Files:**
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
- Modify: `project/src/engine/Function/Render/Renderer.h`
- Modify: `project/src/engine/Function/Render/Renderer.cpp`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `project/src/engine/Function/Application.cpp`
- Create: `project/src/tests/Function/perf_gate_gpu_timing_tests.cpp`

- [ ] **Step 1: Write RED tests for pending, immediate failure, aggregation, and identity**

Use a fake `IGpuTimingContext` to assert:

```cpp
controller.expect_submitted_frame(41, cpu_stats);
fake.queue_pending();
CHECK_FALSE(controller.has_failed());
fake.queue_snapshot(make_snapshot(41, 2.8, {{ terrain_gbuffer, 0.3 }, { terrain_gbuffer, 0.4 }, { terrain_shadow, 0.5 }}));
controller.drain_gpu_timing(fake);
CHECK(controller.gpu_frame_samples().back() == doctest::Approx(2.8));
CHECK(controller.scope_samples(terrain_gbuffer).back() == doctest::Approx(0.7));
```

Configure `required_scope_hashes` per case. An Empty controller with `{}` must accept a frame snapshot with zero scopes. A Terrain controller with `{ terrain_gbuffer, terrain_shadow }` must reject a frame missing either hash and must sum repeated hashes per frame before P95. Separate cases must reject duplicate frame 41, unexpected frame 99, hash collision, `ResolveFailed`, and outstanding expected frames at deadline (`DrainTimeout`). Add one case proving an aborted renderer frame is never inserted into the expected set and the following successfully submitted frame is accepted.

- [ ] **Step 2: Run the focused tests and observe RED**

Run: `RunTests.bat Debug --test-case="PerfGate GPU timing*"`

Expected: compile failure because the expected-set/drain API is missing.

- [ ] **Step 3: Add RenderDevice recording without altering submit APIs**

After the main command buffer begins recording, call `begin_frame` with `RenderDevice::Impl::frame_index`. Before it ends recording, call `end_frame`. Around every existing render pass, hash `m_impl->current_pass_name` and record one GPU scope alongside the existing Tracy zone. If any recording call is not `Success`, mark the renderer frame invalid and propagate a fatal PerfGate error; never submit an unterminated timing frame as valid telemetry.

Add `submitted_frame_index` and `gpu_timing_record_result` to `RendererFrameStats`, preserving the last successfully submitted frame identity.

- [ ] **Step 4: Implement PerfGate report schema v2**

Add `required_scope_hashes` with canonical names to `PerfGateConfig`. `Scenario Empty` supplies an empty set; Phase 4 `Scenario Terrain` supplies exactly `Terrain.GBuffer` and `Terrain.Shadow`. Maintain an expected `std::unordered_set<uint64_t>`, a seen set, GPU frame samples, and per-hash per-frame sums. Drain `try_collect` until `Pending`; any other non-`Success` is immediate failure. Emit:

```json
{
  "schema_version": 2,
  "gpu_timing": {
    "status": "complete",
    "error": "Success",
    "expected_frames": 9000,
    "received_frames": 9000,
    "frame_time_ms": { "p50": 2.1, "p95": 2.9, "p99": 3.1 },
    "passes": {
      "Terrain.GBuffer": { "stable_name_hash": "b740cda0611fe57b", "p95": 0.7 },
      "Terrain.Shadow": { "stable_name_hash": "55c2b22f128624a4", "p95": 0.5 }
    }
  }
}
```

Register canonical names before sampling and reject two names with the same hash. Check required scopes independently for every accepted frame; never treat an optional scope observed in another frame as satisfying a missing required scope. `should_request_exit` becomes true only after the wall-clock sample window has ended and the expected set is empty, or immediately after failure. Continue rendering during drain.

- [ ] **Step 5: Run focused and full tests**

```powershell
RunTests.bat Debug --test-case="PerfGate GPU timing*"
RunTests.bat Debug
RunArchGate.bat
```

Expected: all commands exit 0.

- [ ] **Step 6: Commit recording and PerfGate consumption**

```powershell
git add project/src/engine/Function/Render/RenderDevice.h project/src/engine/Function/Render/RenderDevice.cpp project/src/engine/Function/Render/Renderer.h project/src/engine/Function/Render/Renderer.cpp project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp project/src/engine/Function/Application.cpp project/src/tests/Function/perf_gate_gpu_timing_tests.cpp
git diff --cached --check
git commit -m "feat(perf): gate completed gpu timing samples"
```

### Task 6: Add an explicit no-Tracy build proof

**Files:**
- Modify: `premake5.lua`
- Modify: `project/src/engine/premake5.lua`
- Modify: `scripts/RunPerfGate.ps1`
- Modify: `scripts/TestRunPerfGate.ps1`

- [ ] **Step 1: Write the PowerShell self-test RED assertion**

Extend `RunPerfGate.ps1 -SelfTest` to require `-NoTracy` command construction to contain `premake5.exe --no-tracy vs2022`, an Editor clean build, two backend runs, standard regeneration, and a second clean build. It must reject `-NoTracy -SkipBuild` because that could reuse Tracy-enabled objects.

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1`

Expected: FAIL because `-NoTracy` is not recognized.

- [ ] **Step 2: Add the Premake option and conditional definitions**

At workspace scope add:

```lua
newoption {
    trigger = "no-tracy",
    description = "Build Engine without TRACY_ENABLE/TRACY_ON_DEMAND for GPU timing validation"
}
```

Conditionally omit the Tracy project include in root `premake5.lua`, the Tracy include path, both Tracy definitions, and every `tracy` link in `project/src/engine/premake5.lua` when `--no-tracy` is set. Preserve `ASH_DEBUG`, validation, and all other configuration definitions. The resulting Engine binary must neither compile nor link Tracy.

- [ ] **Step 3: Implement safe no-Tracy orchestration**

`RunPerfGate.ps1 -NoTracy -Scenario Empty` must execute this order in the isolated worktree:

```powershell
./premake5.exe --no-tracy vs2022
./scripts/InvokeMSBuild.ps1 -MSBuildPath $msbuild -SolutionPath ./AshEngine.sln -Target Clean -Configuration Release -Platform x64
./build_editor.bat Release x64
Invoke-PerfGateRun -Target Editor -Backend Vulkan -Scenario Empty -Configuration Release
Invoke-PerfGateRun -Target Editor -Backend DX12 -Scenario Empty -Configuration Release
./premake5.exe vs2022
./scripts/InvokeMSBuild.ps1 -MSBuildPath $msbuild -SolutionPath ./AshEngine.sln -Target Clean -Configuration Release -Platform x64
./build_editor.bat Release x64
```

Put the standard regeneration/clean build in `finally` so failure cannot leave a no-Tracy solution or object set behind.

- [ ] **Step 4: Run the script self-test and fresh generation**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
generate_vs2022.bat
```

Expected: both commands exit 0.

- [ ] **Step 5: Commit the no-Tracy path**

```powershell
git add premake5.lua project/src/engine/premake5.lua scripts/RunPerfGate.ps1 scripts/TestRunPerfGate.ps1
git diff --cached --check
git commit -m "test(perf): verify gpu timing without tracy"
```

### Task 7: Create the fixed 1440p Empty Editor harness

**Files:**
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `project/src/engine/EntryPoint.h`
- Modify: `project/src/engine/Function/Application.h`
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/editor/Editor.h`
- Modify: `project/src/editor/Editor.cpp`
- Modify: `project/src/editor/Panels/ViewportPanel.h`
- Modify: `project/src/editor/Panels/ViewportPanel.cpp`
- Create: `product/assets/scenes/TerrainPerfEmpty.scene.json`
- Create: `product/assets/scenes/TerrainPerfEmpty.manifest.json`
- Create: `tools/perf/terrain_feasibility_contract.json`
- Modify: `scripts/RunPerfGate.ps1`
- Modify: `scripts/TestRunPerfGate.ps1`
- Create: `project/src/tests/Function/perf_gate_scenario_tests.cpp`

- [ ] **Step 1: Write RED parser and extent tests**

Parse `--perf-gate-scenario=Empty --perf-gate-width=2560 --perf-gate-height=1440` into `PerfGateConfig`. Reject zero, negative, or dimensions above 8192. Add a Viewport extent policy test proving normal Editor uses panel size while the actual scene-output render target is exactly 2560 × 1440. Assert `RendererFrameStats.frame_width/height` continues to report the independent swapchain/back-buffer extent and is never overwritten with the scene-output size. Add runner self-tests where CPU P95 3.34 or GPU P95 3.34 makes Empty feasibility exit 1 even when a comparative baseline is missing or blessed. Add `-TimingValidation`, which requires Debug, turns on the existing backend validation/debug layers, waits for readiness plus at least one complete timing snapshot, and exits by that signal rather than a fixed frame count.

Run: `RunTests.bat Debug --test-case="PerfGate scenario*"`

Expected: FAIL because scenario and extent fields do not exist.

- [ ] **Step 2: Store PerfGate input before initialization and bind the resolved backend afterward**

Parse and store `PerfGateConfig` in `EntryPoint.h` before `application->initialize()` so Editor can read the fixed extent during initialization, but do not configure `PerfGateController` yet. Add `Application::set_perf_gate_config` to store the value and protected `report_perf_gate_render_output_extent(width, height)` for the Editor viewport service to call after allocating its real scene-output target. After RHI initialization resolves `activeBackend`, call `PerfGateController::configure(stored_config, title, activeBackend)`. Add a test that a pre-init Vulkan/DX12 override is visible to Editor while report `backend_actual` is the resolved backend, never `Default`. The override changes only the scene render target extent; the ImGui image may scale to the panel and window/swapchain creation stays independent.

- [ ] **Step 3: Add the deterministic empty scene and runner scenario**

Create a schema-v5 scene containing the canonical camera, environment, and one shadow-casting directional light later reused by TerrainPerf, but no Terrain, mesh, or particle entities. `TerrainPerfEmpty.manifest.json` records the canonical camera/environment/light/render-config JSON and its SHA-256; the Terrain workload must match this hash. Create `tools/perf/terrain_feasibility_contract.json` with immutable CPU/GPU P95 limits of 3.33 ms. PerfGate schema v2 reports `render_output.width/height` from the allocated Editor scene output and `swapchain.width/height` from `RendererFrameStats` as separate evidence. `RunPerfGate.ps1 -Scenario Empty` runs only Release Editor, injects its scene path plus 2560/1440 arguments, disables VSync, waits for readiness before warmup, verifies `render_output` is 2560 × 1440 without requiring the swapchain to match, and applies the non-blessable feasibility contract before comparative baseline checks. The measured GPU frame contains the scene-output passes because they are recorded on the timed main graphics command buffer; add a source-contract assertion for that ordering. `-TimingValidation` uses the same scene in Debug but checks timing completeness and validation logs instead of performance limits.

- [ ] **Step 4: Run focused tests and dry-run inspection**

```powershell
RunTests.bat Debug --test-case="PerfGate scenario*"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard -Scenario Empty -DryRun
```

Expected: tests pass; dry-run shows one Editor run per backend with the fixed scene and extent arguments.

- [ ] **Step 5: Commit the harness**

```powershell
git add project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp project/src/engine/EntryPoint.h project/src/engine/Function/Application.h project/src/engine/Function/Application.cpp project/src/editor/Editor.h project/src/editor/Editor.cpp project/src/editor/Panels/ViewportPanel.h project/src/editor/Panels/ViewportPanel.cpp product/assets/scenes/TerrainPerfEmpty.scene.json product/assets/scenes/TerrainPerfEmpty.manifest.json tools/perf/terrain_feasibility_contract.json scripts/RunPerfGate.ps1 scripts/TestRunPerfGate.ps1 project/src/tests/Function/perf_gate_scenario_tests.cpp
git diff --cached --check
git commit -m "feat(perf): add fixed 1440p empty editor scenario"
```

### Task 8: Prove feasibility and close Phase 0

**Files:**
- Modify: `docs/specs/modules/graphics.md`
- Modify: `docs/specs/modules/render.md`
- Modify: `docs/specs/modules/tools.md`
- Modify: `docs/VERIFY.md`

- [ ] **Step 1: Run fresh unit/build/architecture gates**

```powershell
generate_vs2022.bat
RunTests.bat Debug
RunTests.bat Release
RunArchGate.bat
build_editor.bat Debug
build_editor.bat Release
build_sandbox.bat Debug
build_sandbox.bat Release
```

Expected: every command exits 0.

- [ ] **Step 2: Run double-backend validation and no-Tracy evidence**

```powershell
run.bat editor vulkan Debug --smoke-test-seconds=120
run.bat editor dx12 Debug --smoke-test-seconds=120
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Debug -TimingValidation
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Release -NoTracy
```

Expected: every command exits 0; the Debug timing-validation run produces at least one completed snapshot per backend and logs no timestamp/query/fence/readback error; both Release no-Tracy reports have schema v2, complete GPU samples, and `render_output` 2560 × 1440.

- [ ] **Step 3: Run the standard PerfGate and inspect the hard baseline**

Run: `RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Release`

Expected: Vulkan and DX12 CPU frame P95 ≤ 3.33 ms and GPU frame P95 ≤ 3.33 ms. If either backend fails, stop Phase 0, do not implement Terrain, and write a separate performance-remediation SDD.

- [ ] **Step 4: Update long-lived contracts with measured behavior**

Document the public timing snapshot/result semantics, backend-private slot lifecycle, PerfGate schema v2, no-Tracy command, fixed extent, and actual empty baseline numbers. Do not copy aspirational Terrain implementation into module specs.

- [ ] **Step 5: Run final document/tool checks and commit**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan -IncludePerfGate
git add docs/specs/modules/graphics.md docs/specs/modules/render.md docs/specs/modules/tools.md docs/VERIFY.md
git diff --cached --check
git commit -m "docs(perf): record gpu timing and empty baseline"
```

Expected: self-test and AIDevDoctor exit 0; the documentation commit contains only the four listed files.
