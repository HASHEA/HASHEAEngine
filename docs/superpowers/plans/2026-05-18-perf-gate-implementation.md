# Standard Perf Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Phase 1 standard performance gate: Engine telemetry JSON plus a PowerShell gate runner that reports CPU frame-time, memory, backend, validation, and leak health for Sandbox and Editor.

**Architecture:** Engine code owns measurement through a disabled-by-default PerfGate diagnostics controller. Base and RHI expose small snapshot APIs for memory telemetry. `scripts/RunPerfGate.ps1` owns orchestration, backend switching, timeout handling, log scanning, and PASS/WARN/FAIL summary generation.

**Tech Stack:** C++17, existing AshEngine `Application` lifecycle, `RendererFrameStats`, `nlohmann::json`, Windows process memory APIs, Vulkan VMA tracking, PowerShell 5+.

---

## Scope

This plan implements Phase 1 only:

- PerfGate CLI parsing.
- Per-run telemetry JSON.
- CPU frame-time/FPS/render-stat sampling.
- OS process memory and Engine heap memory sampling.
- Vulkan VMA current/peak/shutdown live bytes where leak tracking is enabled.
- DX12 GPU allocator stats marked unsupported for Phase 1.
- Standard profile runner script and baseline/threshold skeleton.
- Documentation updates.

This plan does not implement GPU timestamp queries, Tracy capture export, baseline bless/update commands, Git hooks, or CI integration.

## File Structure

- `project/src/engine/Base/hmemory.h`
  - Add Engine heap statistics structures and read-only snapshot accessors.
- `project/src/engine/Base/hmemory.cpp`
  - Maintain current/peak allocation counters and expose shutdown live bytes without changing allocator ownership.
- `project/src/engine/Base/ProcessMemoryDiagnostics.h`
  - New lightweight process memory snapshot API.
- `project/src/engine/Base/ProcessMemoryDiagnostics.cpp`
  - Windows implementation using process memory counters; non-Windows returns unsupported zeroes.
- `project/src/engine/Base/EngineSelfTests.cpp`
  - Add self-tests for heap stats, process snapshot defaults, RHI memory defaults, PerfGate config parsing, and percentile summaries.
- `project/src/engine/Graphics/GraphicsContext.h`
  - Add backend-neutral `RenderMemoryStats` and default `get_render_memory_stats()` API.
- `project/src/engine/Graphics/Vulkan/VulkanContext.h`
  - Store render memory statistics beside existing VMA tracking.
- `project/src/engine/Graphics/Vulkan/VulkanContext.cpp`
  - Update Vulkan memory stats in VMA track/untrack and cache shutdown-live bytes before allocator destruction.
- `project/src/engine/Graphics/DirectX12/DX12Context.h`
  - Override memory stats as unsupported, keeping the contract explicit.
- `project/src/engine/Function/Diagnostics/PerfGate.h`
  - New Engine-facing PerfGate config, summary, run report, and controller API.
- `project/src/engine/Function/Diagnostics/PerfGate.cpp`
  - CLI parsing, warmup/sample tracking, percentile calculation, memory sampling, JSON writing.
- `project/src/engine/Function/Application.h`
  - Add `configure_perf_gate()` and a `PerfGateController` member without exposing backend details.
- `project/src/engine/Function/Application.cpp`
  - Integrate PerfGate lifecycle into startup, per-frame sampling, graceful exit, and shutdown report writing.
- `project/src/engine/EntryPoint.h`
  - Parse PerfGate CLI arguments and pass config into `Application`.
- `project/src/engine/premake5.lua`
  - Link `psapi` on Windows for process memory counters.
- `scripts/RunPerfGate.ps1`
  - New standard gate entry point.
- `tools/perf/perf_gate_baselines.json`
  - New versioned profile, caps, and warn thresholds.
- `README.md`
  - Add the script entry and plan link.
- `docs/EngineDeveloperGuide.md`
  - Document PerfGate CLI, report paths, metric semantics, and Phase 1 limitations.

---

### Task 1: Engine Heap Memory Statistics

**Files:**
- Modify: `project/src/engine/Base/hmemory.h`
- Modify: `project/src/engine/Base/hmemory.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write the failing heap stats self-test**

Add this helper test in the anonymous namespace of `project/src/engine/Base/EngineSelfTests.cpp`, near the other allocator tests:

```cpp
auto test_memory_service_reports_heap_statistics() -> bool
{
    HeapMemoryStats before = MemoryService::instance()->get_heap_stats();
    void* allocation = Ash_Alloc(nullptr, 128, 16);
    HeapMemoryStats during = MemoryService::instance()->get_heap_stats();
    Ash_Free(nullptr, allocation);
    HeapMemoryStats after = MemoryService::instance()->get_heap_stats();

    if (!allocation)
    {
        return report_self_test_failure("MemoryService heap stats", "allocation failed");
    }
    if (during.current_allocated_bytes <= before.current_allocated_bytes)
    {
        return report_self_test_failure("MemoryService heap stats", "current bytes did not increase after allocation");
    }
    if (during.peak_allocated_bytes < during.current_allocated_bytes)
    {
        return report_self_test_failure("MemoryService heap stats", "peak bytes were lower than current bytes");
    }
    if (during.live_allocation_count <= before.live_allocation_count)
    {
        return report_self_test_failure("MemoryService heap stats", "live allocation count did not increase");
    }
    return (after.current_allocated_bytes == before.current_allocated_bytes) ||
        report_self_test_failure("MemoryService heap stats", "current bytes did not return to the original value");
}
```

Add it to `run_engine_base_self_tests()` after `test_typed_allocation_respects_alignment()`:

```cpp
all_passed = test_memory_service_reports_heap_statistics() && all_passed;
```

- [ ] **Step 2: Run self-test to verify it fails to compile**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: compile or run fails because `HeapMemoryStats` and `MemoryService::get_heap_stats()` do not exist.

- [ ] **Step 3: Add heap statistics declarations**

In `project/src/engine/Base/hmemory.h`, add this struct after `MemoryStatistics`:

```cpp
struct HeapMemoryStats
{
    size_t current_allocated_bytes = 0;
    size_t peak_allocated_bytes = 0;
    uint64_t live_allocation_count = 0;
    uint64_t peak_allocation_count = 0;
};
```

In `HeapAllocator`, add:

```cpp
auto get_stats() const -> HeapMemoryStats;
```

Change the mutex member to mutable and add counters:

```cpp
mutable std::mutex m_mutex{};
void* m_pTlsfHandle = nullptr;
void* m_pMemory = nullptr;
size_t m_szAllocatedSize = 0;
size_t m_szPeakAllocatedSize = 0;
size_t m_szMaxSize = 0;
uint64_t m_liveAllocationCount = 0;
uint64_t m_peakAllocationCount = 0;
```

In `MemoryService`, add:

```cpp
auto get_heap_stats() const -> HeapMemoryStats
{
    return m_heapAllocator.get_stats();
}
```

- [ ] **Step 4: Implement heap statistics maintenance**

In `project/src/engine/Base/hmemory.cpp`, add this method near the other `HeapAllocator` methods:

```cpp
auto HeapAllocator::get_stats() const -> HeapMemoryStats
{
    std::lock_guard<std::mutex> lock(m_mutex);
    HeapMemoryStats stats{};
    stats.current_allocated_bytes = m_szAllocatedSize;
    stats.peak_allocated_bytes = m_szPeakAllocatedSize;
    stats.live_allocation_count = m_liveAllocationCount;
    stats.peak_allocation_count = m_peakAllocationCount;
    return stats;
}
```

After every successful allocation where `m_szAllocatedSize += actualSize;`, add:

```cpp
++m_liveAllocationCount;
m_szPeakAllocatedSize = std::max(m_szPeakAllocatedSize, m_szAllocatedSize);
m_peakAllocationCount = std::max(m_peakAllocationCount, m_liveAllocationCount);
```

After every successful deallocation where `m_szAllocatedSize` is reduced, add:

```cpp
if (m_liveAllocationCount > 0)
{
    --m_liveAllocationCount;
}
```

In `HeapAllocator::shutdown()`, reset the new counters after destroying/freeing memory:

```cpp
m_szPeakAllocatedSize = 0;
m_liveAllocationCount = 0;
m_peakAllocationCount = 0;
```

Add `#include <algorithm>` near the top of `hmemory.cpp`.

- [ ] **Step 5: Run self-test to verify it passes**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code `0`; no `MemoryService heap stats` failure.

- [ ] **Step 6: Commit heap statistics**

```powershell
git add -- project/src/engine/Base/hmemory.h project/src/engine/Base/hmemory.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add engine heap memory statistics"
```

---

### Task 2: Process Memory Diagnostics

**Files:**
- Create: `project/src/engine/Base/ProcessMemoryDiagnostics.h`
- Create: `project/src/engine/Base/ProcessMemoryDiagnostics.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `project/src/engine/premake5.lua`

- [ ] **Step 1: Write the failing process memory self-test**

Add this include to `EngineSelfTests.cpp`:

```cpp
#include "ProcessMemoryDiagnostics.h"
```

Add this test in the anonymous namespace:

```cpp
auto test_process_memory_snapshot_is_available() -> bool
{
    const ProcessMemorySnapshot snapshot = get_current_process_memory_snapshot();
#if defined(ASH_WINDOWS)
    if (!snapshot.supported)
    {
        return report_self_test_failure("Process memory snapshot", "Windows process memory snapshot reported unsupported");
    }
    if (snapshot.working_set_bytes == 0 || snapshot.private_bytes == 0)
    {
        return report_self_test_failure("Process memory snapshot", "Windows process memory counters were zero");
    }
#else
    if (snapshot.supported)
    {
        return report_self_test_failure("Process memory snapshot", "non-Windows process memory snapshot unexpectedly reported supported");
    }
#endif
    return true;
}
```

Add it to `run_engine_base_self_tests()` after the heap stats test:

```cpp
all_passed = test_process_memory_snapshot_is_available() && all_passed;
```

- [ ] **Step 2: Run self-test to verify it fails to compile**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: compile fails because `ProcessMemoryDiagnostics.h` does not exist.

- [ ] **Step 3: Add process memory diagnostics header**

Create `project/src/engine/Base/ProcessMemoryDiagnostics.h`:

```cpp
#pragma once

#include "hcore.h"
#include <cstdint>

namespace AshEngine
{
    struct ProcessMemorySnapshot
    {
        bool supported = false;
        uint64_t working_set_bytes = 0;
        uint64_t private_bytes = 0;
        uint64_t pagefile_bytes = 0;
    };

    auto ASH_API get_current_process_memory_snapshot() -> ProcessMemorySnapshot;
}
```

- [ ] **Step 4: Add process memory diagnostics implementation**

Create `project/src/engine/Base/ProcessMemoryDiagnostics.cpp`:

```cpp
#include "ProcessMemoryDiagnostics.h"

#if defined(ASH_WINDOWS)
#include <Windows.h>
#include <Psapi.h>
#endif

namespace AshEngine
{
    auto get_current_process_memory_snapshot() -> ProcessMemorySnapshot
    {
        ProcessMemorySnapshot snapshot{};
#if defined(ASH_WINDOWS)
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)))
        {
            snapshot.supported = true;
            snapshot.working_set_bytes = static_cast<uint64_t>(counters.WorkingSetSize);
            snapshot.private_bytes = static_cast<uint64_t>(counters.PrivateUsage);
            snapshot.pagefile_bytes = static_cast<uint64_t>(counters.PagefileUsage);
        }
#endif
        return snapshot;
    }
}
```

- [ ] **Step 5: Link psapi on Windows**

In `project/src/engine/premake5.lua`, add `"psapi"` to both Windows Debug and Release `links` blocks for the Engine project:

```lua
links
{
    "tracy",
    "dbghelp",
    "psapi",
    "d3d12",
    "dxgi",
    "dxguid",
    "D3D12MA",
}
```

- [ ] **Step 6: Run self-test to verify it passes**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code `0`; no `Process memory snapshot` failure.

- [ ] **Step 7: Commit process memory diagnostics**

```powershell
git add -- project/src/engine/Base/ProcessMemoryDiagnostics.h project/src/engine/Base/ProcessMemoryDiagnostics.cpp project/src/engine/Base/EngineSelfTests.cpp project/src/engine/premake5.lua
git commit -m "Add process memory diagnostics"
```

---

### Task 3: Backend-Neutral Render Memory Stats

**Files:**
- Modify: `project/src/engine/Graphics/GraphicsContext.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write the failing RHI memory default self-test**

Add this test in `EngineSelfTests.cpp` near other RHI value-semantics tests:

```cpp
auto test_render_memory_stats_default_to_unsupported() -> bool
{
    RHI::RenderMemoryStats stats{};
    const bool ok =
        !stats.supported &&
        stats.gpu_allocator_current_bytes == 0 &&
        stats.gpu_allocator_peak_bytes == 0 &&
        stats.gpu_allocator_shutdown_live_bytes == 0;
    return ok ||
        report_self_test_failure("RenderMemoryStats defaults", "default stats were not unsupported zeroes");
}
```

Add it to `run_engine_base_self_tests()` after `test_ash_barrier_copy_move_is_safe()`:

```cpp
all_passed = test_render_memory_stats_default_to_unsupported() && all_passed;
```

- [ ] **Step 2: Run self-test to verify it fails to compile**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: compile fails because `RHI::RenderMemoryStats` does not exist.

- [ ] **Step 3: Add the neutral stats contract**

In `project/src/engine/Graphics/GraphicsContext.h`, add this struct before `class GraphicsContext`:

```cpp
struct RenderMemoryStats
{
    bool supported = false;
    uint64_t gpu_allocator_current_bytes = 0;
    uint64_t gpu_allocator_peak_bytes = 0;
    uint64_t gpu_allocator_shutdown_live_bytes = 0;
};
```

Add this default virtual method inside `GraphicsContext`:

```cpp
virtual auto get_render_memory_stats() const -> RenderMemoryStats
{
    return {};
}
```

Ensure `GraphicsContext.h` includes `<cstdint>` if it does not already do so.

- [ ] **Step 4: Add Vulkan stats storage and override**

In `project/src/engine/Graphics/Vulkan/VulkanContext.h`, add this public override:

```cpp
auto get_render_memory_stats() const -> RenderMemoryStats override;
```

Add this private member near `vmaTrackedAllocations`:

```cpp
mutable RenderMemoryStats renderMemoryStats{};
```

In `project/src/engine/Graphics/Vulkan/VulkanContext.cpp`, implement:

```cpp
auto VulkanContext::get_render_memory_stats() const -> RenderMemoryStats
{
    std::lock_guard<std::mutex> lock(vmaTrackedAllocationsMutex);
    return renderMemoryStats;
}
```

In `_create_vulkan_memory_allocator()`, after `vmaCreateAllocator` succeeds, initialize:

```cpp
renderMemoryStats.supported = ASH_ENABLE_VMA_LEAK_TRACKING != 0;
renderMemoryStats.gpu_allocator_current_bytes = 0;
renderMemoryStats.gpu_allocator_peak_bytes = 0;
renderMemoryStats.gpu_allocator_shutdown_live_bytes = 0;
```

In `_track_vma_allocation()`, after inserting into `vmaTrackedAllocations`, update:

```cpp
renderMemoryStats.supported = true;
renderMemoryStats.gpu_allocator_current_bytes += info.size;
renderMemoryStats.gpu_allocator_peak_bytes = std::max(
    renderMemoryStats.gpu_allocator_peak_bytes,
    renderMemoryStats.gpu_allocator_current_bytes);
```

In `_untrack_vma_allocation()`, before erasing the iterator, update:

```cpp
const uint64_t size = iter->second.size;
renderMemoryStats.gpu_allocator_current_bytes =
    size <= renderMemoryStats.gpu_allocator_current_bytes ?
    renderMemoryStats.gpu_allocator_current_bytes - size :
    0;
```

In `_dump_vma_leaks()`, before logging whether the map is empty, compute shutdown live bytes:

```cpp
uint64_t liveBytes = 0;
for (const auto& [allocationHandle, info] : vmaTrackedAllocations)
{
    (void)allocationHandle;
    liveBytes += info.size;
}
renderMemoryStats.gpu_allocator_shutdown_live_bytes = liveBytes;
```

Add `#include <algorithm>` to `VulkanContext.cpp` if not already included.

- [ ] **Step 5: Mark DX12 GPU allocator stats unsupported in Phase 1**

In `project/src/engine/Graphics/DirectX12/DX12Context.h`, add this explicit override:

```cpp
auto get_render_memory_stats() const -> RenderMemoryStats override
{
    return {};
}
```

- [ ] **Step 6: Run self-test and build both backends**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code `0`.

Then run:

```powershell
.\build_sandbox.bat Debug x64
.\build_editor.bat Debug x64
```

Expected: both builds exit `0`.

- [ ] **Step 7: Commit render memory stats**

```powershell
git add -- project/src/engine/Graphics/GraphicsContext.h project/src/engine/Graphics/Vulkan/VulkanContext.h project/src/engine/Graphics/Vulkan/VulkanContext.cpp project/src/engine/Graphics/DirectX12/DX12Context.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Expose render memory statistics"
```

---

### Task 4: PerfGate Core Helpers And Self-Tests

**Files:**
- Create: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Create: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing tests for config parsing and percentiles**

Add this include to `EngineSelfTests.cpp`:

```cpp
#include "Function/Diagnostics/PerfGate.h"
```

Add these tests in the anonymous namespace:

```cpp
auto test_perf_gate_config_parser_defaults_to_disabled() -> bool
{
    char arg0[] = "Sandbox.exe";
    char* argv[] = { arg0 };
    const PerfGateConfig config = parse_perf_gate_config(1, argv);
    return (!config.enabled) ||
        report_self_test_failure("PerfGate config disabled default", "parser enabled PerfGate without --perf-gate");
}

auto test_perf_gate_config_parser_reads_arguments() -> bool
{
    char arg0[] = "Sandbox.exe";
    char arg1[] = "--perf-gate";
    char arg2[] = "--perf-gate-profile=Standard";
    char arg3[] = "--perf-gate-output=Intermediate/test-reports/perf-gate/test/run.json";
    char arg4[] = "--perf-gate-warmup-seconds=1.5";
    char arg5[] = "--perf-gate-sample-seconds=2.5";
    char arg6[] = "--perf-gate-target=Sandbox";
    char* argv[] = {
        arg0,
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6
    };
    const PerfGateConfig config = parse_perf_gate_config(7, argv);
    const bool ok =
        config.enabled &&
        config.profile == "Standard" &&
        config.target_name == "Sandbox" &&
        config.output_path == "Intermediate/test-reports/perf-gate/test/run.json" &&
        config.warmup_seconds == 1.5 &&
        config.sample_seconds == 2.5;
    return ok ||
        report_self_test_failure("PerfGate config parser", "parser did not preserve perf-gate arguments");
}

auto test_perf_gate_frame_summary_percentiles_are_stable() -> bool
{
    std::vector<double> samples = { 0.40, 0.10, 0.20, 0.30 };
    const PerfGateFrameTimeSummary summary = summarize_perf_gate_frame_times(samples);
    const bool ok =
        summary.sample_count == 4 &&
        summary.min_ms == 0.10 &&
        summary.max_ms == 0.40 &&
        summary.avg_ms > 0.249 &&
        summary.avg_ms < 0.251 &&
        summary.p50_ms == 0.20 &&
        summary.p95_ms == 0.40 &&
        summary.p99_ms == 0.40;
    return ok ||
        report_self_test_failure("PerfGate frame summary", "percentiles or averages were not stable");
}
```

Add them to `run_engine_base_self_tests()` after `test_render_memory_stats_default_to_unsupported()`:

```cpp
all_passed = test_perf_gate_config_parser_defaults_to_disabled() && all_passed;
all_passed = test_perf_gate_config_parser_reads_arguments() && all_passed;
all_passed = test_perf_gate_frame_summary_percentiles_are_stable() && all_passed;
```

- [ ] **Step 2: Run self-test to verify it fails to compile**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: compile fails because `Function/Diagnostics/PerfGate.h` does not exist.

- [ ] **Step 3: Add PerfGate public header**

Create `project/src/engine/Function/Diagnostics/PerfGate.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Base/hmemory.h"
#include "Base/ProcessMemoryDiagnostics.h"
#include "Function/Render/Renderer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RHIBackend.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
    struct PerfGateConfig
    {
        bool enabled = false;
        std::string profile = "Standard";
        std::string output_path{};
        std::string target_name{};
        double warmup_seconds = 10.0;
        double sample_seconds = 30.0;
    };

    struct PerfGateFrameTimeSummary
    {
        uint64_t sample_count = 0;
        double avg_ms = 0.0;
        double p50_ms = 0.0;
        double p95_ms = 0.0;
        double p99_ms = 0.0;
        double min_ms = 0.0;
        double max_ms = 0.0;
    };

    struct PerfGateRenderStatsSummary
    {
        double draw_calls_avg = 0.0;
        double graphics_passes_avg = 0.0;
        double dispatches_avg = 0.0;
    };

    struct PerfGateMemorySummary
    {
        ProcessMemorySnapshot process_peak{};
        HeapMemoryStats engine_heap_peak{};
        HeapMemoryStats engine_heap_shutdown{};
        RHI::RenderMemoryStats render_memory{};
    };

    auto ASH_API parse_perf_gate_config(int argc, char* argv[]) -> PerfGateConfig;
    auto ASH_API summarize_perf_gate_frame_times(std::vector<double> samples) -> PerfGateFrameTimeSummary;

    class ASH_API PerfGateController
    {
    public:
        auto configure(const PerfGateConfig& config, const char* target_name, RHI::Backend backend) -> void;
        auto is_enabled() const -> bool;
        auto begin() -> void;
        auto sample_after_frame(const RendererFrameStats& frame_stats) -> void;
        auto should_request_exit() const -> bool;
        auto capture_render_memory_stats(const RHI::RenderMemoryStats& stats) -> void;
        auto capture_shutdown_heap_stats(const HeapMemoryStats& stats) -> void;
        auto write_report(bool abnormal_exit) -> bool;

    private:
        auto sample_memory() -> void;
        auto elapsed_seconds() const -> double;

    private:
        PerfGateConfig m_config{};
        std::string m_target_name{};
        RHI::Backend m_backend = RHI::Backend::Default;
        std::chrono::steady_clock::time_point m_start_time{};
        bool m_started = false;
        bool m_report_written = false;
        uint64_t m_frames_total = 0;
        uint64_t m_frames_sampled = 0;
        std::vector<double> m_frame_time_samples_ms{};
        uint64_t m_draw_call_sum = 0;
        uint64_t m_graphics_pass_sum = 0;
        uint64_t m_dispatch_sum = 0;
        PerfGateMemorySummary m_memory{};
    };
}
```

- [ ] **Step 4: Implement config parsing and frame summary**

Create `project/src/engine/Function/Diagnostics/PerfGate.cpp` with:

```cpp
#include "PerfGate.h"

#include "Base/hlog.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>

namespace AshEngine
{
    namespace
    {
        using json = nlohmann::json;

        auto parse_double_arg(const std::string& value, double fallback) -> double
        {
            char* end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            return end != value.c_str() && parsed > 0.0 ? parsed : fallback;
        }

        auto starts_with(const std::string& value, const char* prefix) -> bool
        {
            return value.rfind(prefix, 0) == 0;
        }

        auto perf_gate_backend_name(RHI::Backend backend) -> const char*
        {
            switch (backend)
            {
            case RHI::Backend::Vulkan:
                return "Vulkan";
            case RHI::Backend::DirectX12:
                return "DX12";
            case RHI::Backend::Default:
            default:
                return "Default";
            }
        }

        auto percentile_from_sorted(const std::vector<double>& sorted, double percentile) -> double
        {
            if (sorted.empty())
            {
                return 0.0;
            }
            const double clamped = std::max(0.0, std::min(1.0, percentile));
            const size_t index = static_cast<size_t>(std::ceil(clamped * static_cast<double>(sorted.size())) - 1.0);
            return sorted[std::min(index, sorted.size() - 1u)];
        }
    }

    auto parse_perf_gate_config(int argc, char* argv[]) -> PerfGateConfig
    {
        PerfGateConfig config{};
        for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
            if (argument == "--perf-gate")
            {
                config.enabled = true;
                continue;
            }
            if (starts_with(argument, "--perf-gate-profile="))
            {
                config.profile = argument.substr(std::char_traits<char>::length("--perf-gate-profile="));
                continue;
            }
            if (starts_with(argument, "--perf-gate-output="))
            {
                config.output_path = argument.substr(std::char_traits<char>::length("--perf-gate-output="));
                continue;
            }
            if (starts_with(argument, "--perf-gate-target="))
            {
                config.target_name = argument.substr(std::char_traits<char>::length("--perf-gate-target="));
                continue;
            }
            if (starts_with(argument, "--perf-gate-warmup-seconds="))
            {
                config.warmup_seconds = parse_double_arg(
                    argument.substr(std::char_traits<char>::length("--perf-gate-warmup-seconds=")),
                    config.warmup_seconds);
                continue;
            }
            if (starts_with(argument, "--perf-gate-sample-seconds="))
            {
                config.sample_seconds = parse_double_arg(
                    argument.substr(std::char_traits<char>::length("--perf-gate-sample-seconds=")),
                    config.sample_seconds);
                continue;
            }
        }
        return config;
    }

    auto summarize_perf_gate_frame_times(std::vector<double> samples) -> PerfGateFrameTimeSummary
    {
        PerfGateFrameTimeSummary summary{};
        if (samples.empty())
        {
            return summary;
        }
        std::sort(samples.begin(), samples.end());
        double sum = 0.0;
        for (double sample : samples)
        {
            sum += sample;
        }
        summary.sample_count = static_cast<uint64_t>(samples.size());
        summary.avg_ms = sum / static_cast<double>(samples.size());
        summary.p50_ms = percentile_from_sorted(samples, 0.50);
        summary.p95_ms = percentile_from_sorted(samples, 0.95);
        summary.p99_ms = percentile_from_sorted(samples, 0.99);
        summary.min_ms = samples.front();
        summary.max_ms = samples.back();
        return summary;
    }
}
```

- [ ] **Step 5: Implement PerfGateController**

Extend `PerfGate.cpp` with:

```cpp
auto PerfGateController::configure(const PerfGateConfig& config, const char* target_name, RHI::Backend backend) -> void
{
    m_config = config;
    m_target_name = !m_config.target_name.empty() ? m_config.target_name : (target_name ? target_name : "");
    m_backend = backend;
}

auto PerfGateController::is_enabled() const -> bool
{
    return m_config.enabled;
}

auto PerfGateController::begin() -> void
{
    if (!m_config.enabled)
    {
        return;
    }
    m_started = true;
    m_start_time = std::chrono::steady_clock::now();
}

auto PerfGateController::elapsed_seconds() const -> double
{
    if (!m_started)
    {
        return 0.0;
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start_time).count();
}

auto PerfGateController::sample_memory() -> void
{
    const ProcessMemorySnapshot process = get_current_process_memory_snapshot();
    if (process.supported)
    {
        m_memory.process_peak.supported = true;
        m_memory.process_peak.working_set_bytes = std::max(m_memory.process_peak.working_set_bytes, process.working_set_bytes);
        m_memory.process_peak.private_bytes = std::max(m_memory.process_peak.private_bytes, process.private_bytes);
        m_memory.process_peak.pagefile_bytes = std::max(m_memory.process_peak.pagefile_bytes, process.pagefile_bytes);
    }

    const HeapMemoryStats heap = MemoryService::instance()->get_heap_stats();
    m_memory.engine_heap_peak.current_allocated_bytes = std::max(
        m_memory.engine_heap_peak.current_allocated_bytes,
        heap.current_allocated_bytes);
    m_memory.engine_heap_peak.peak_allocated_bytes = std::max(
        m_memory.engine_heap_peak.peak_allocated_bytes,
        heap.peak_allocated_bytes);
    m_memory.engine_heap_peak.live_allocation_count = std::max(
        m_memory.engine_heap_peak.live_allocation_count,
        heap.live_allocation_count);
    m_memory.engine_heap_peak.peak_allocation_count = std::max(
        m_memory.engine_heap_peak.peak_allocation_count,
        heap.peak_allocation_count);
}

auto PerfGateController::sample_after_frame(const RendererFrameStats& frame_stats) -> void
{
    if (!m_config.enabled || !m_started || m_report_written)
    {
        return;
    }

    ++m_frames_total;
    sample_memory();

    const double elapsed = elapsed_seconds();
    if (elapsed < m_config.warmup_seconds)
    {
        return;
    }
    if (elapsed > m_config.warmup_seconds + m_config.sample_seconds)
    {
        return;
    }

    ++m_frames_sampled;
    m_frame_time_samples_ms.push_back(frame_stats.cpu_frame_time_ms);
    m_draw_call_sum += frame_stats.draw_call_count;
    m_graphics_pass_sum += frame_stats.graphics_pass_count;
    m_dispatch_sum += frame_stats.compute_dispatch_count;
}

auto PerfGateController::should_request_exit() const -> bool
{
    return m_config.enabled &&
        m_started &&
        elapsed_seconds() >= (m_config.warmup_seconds + m_config.sample_seconds);
}

auto PerfGateController::capture_render_memory_stats(const RHI::RenderMemoryStats& stats) -> void
{
    m_memory.render_memory = stats;
}

auto PerfGateController::capture_shutdown_heap_stats(const HeapMemoryStats& stats) -> void
{
    m_memory.engine_heap_shutdown = stats;
}
```

- [ ] **Step 6: Implement JSON report writing**

Add to `PerfGate.cpp`:

```cpp
auto PerfGateController::write_report(bool abnormal_exit) -> bool
{
    if (!m_config.enabled || m_report_written)
    {
        return true;
    }
    m_report_written = true;

    if (m_config.output_path.empty())
    {
        HLogError("PerfGate: output path is empty.");
        return false;
    }

    const PerfGateFrameTimeSummary frame_summary = summarize_perf_gate_frame_times(m_frame_time_samples_ms);
    const double sampled_count = frame_summary.sample_count > 0 ? static_cast<double>(frame_summary.sample_count) : 1.0;

    json report{};
    report["schema_version"] = 1;
    report["target"] = m_target_name;
    report["backend_actual"] = perf_gate_backend_name(m_backend);
    report["profile"] = m_config.profile;
    report["warmup_seconds"] = m_config.warmup_seconds;
    report["sample_seconds"] = m_config.sample_seconds;
    report["frames_total"] = m_frames_total;
    report["frames_sampled"] = m_frames_sampled;

    report["cpu_frame_time_ms"] = {
        { "avg", frame_summary.avg_ms },
        { "p50", frame_summary.p50_ms },
        { "p95", frame_summary.p95_ms },
        { "p99", frame_summary.p99_ms },
        { "min", frame_summary.min_ms },
        { "max", frame_summary.max_ms }
    };
    report["fps"] = {
        { "avg", frame_summary.avg_ms > 0.0 ? 1000.0 / frame_summary.avg_ms : 0.0 },
        { "p05", frame_summary.p95_ms > 0.0 ? 1000.0 / frame_summary.p95_ms : 0.0 }
    };
    report["render_stats"] = {
        { "draw_calls_avg", static_cast<double>(m_draw_call_sum) / sampled_count },
        { "graphics_passes_avg", static_cast<double>(m_graphics_pass_sum) / sampled_count },
        { "dispatches_avg", static_cast<double>(m_dispatch_sum) / sampled_count }
    };
    report["memory"] = {
        { "process_working_set_peak_mb", static_cast<double>(m_memory.process_peak.working_set_bytes) / (1024.0 * 1024.0) },
        { "process_private_bytes_peak_mb", static_cast<double>(m_memory.process_peak.private_bytes) / (1024.0 * 1024.0) },
        { "engine_heap_peak_mb", static_cast<double>(m_memory.engine_heap_peak.peak_allocated_bytes) / (1024.0 * 1024.0) },
        { "engine_heap_shutdown_live_bytes", m_memory.engine_heap_shutdown.current_allocated_bytes },
        { "gpu_allocator_supported", m_memory.render_memory.supported },
        { "gpu_allocator_current_mb", static_cast<double>(m_memory.render_memory.gpu_allocator_current_bytes) / (1024.0 * 1024.0) },
        { "gpu_allocator_peak_mb", static_cast<double>(m_memory.render_memory.gpu_allocator_peak_bytes) / (1024.0 * 1024.0) },
        { "gpu_allocator_shutdown_live_bytes", m_memory.render_memory.gpu_allocator_shutdown_live_bytes }
    };
    report["errors"] = {
        { "abnormal_exit", abnormal_exit },
        { "backend_mismatch", false },
        { "crashed", false },
        { "timed_out", false }
    };

    std::filesystem::path output_path = m_config.output_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream output(output_path, std::ios::trunc);
    if (!output.is_open())
    {
        HLogError("PerfGate: failed to open report '{}'.", output_path.string());
        return false;
    }
    output << report.dump(2);
    output << '\n';
    return true;
}
```

- [ ] **Step 7: Run self-test to verify it passes**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code `0`; no PerfGate self-test failures.

- [ ] **Step 8: Commit PerfGate core helpers**

```powershell
git add -- project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add perf gate telemetry helpers"
```

---

### Task 5: Application And EntryPoint Integration

**Files:**
- Modify: `project/src/engine/Function/Application.h`
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/engine/EntryPoint.h`

- [ ] **Step 1: Add Application PerfGate API and member**

In `project/src/engine/Function/Application.h`, add:

```cpp
#include "Function/Diagnostics/PerfGate.h"
```

Add this public method near `set_max_run_seconds()`:

```cpp
auto configure_perf_gate(const PerfGateConfig& config) -> void;
```

Add this protected member near `debugDrawService`:

```cpp
PerfGateController perfGateController{};
```

- [ ] **Step 2: Parse PerfGate config in EntryPoint**

In `project/src/engine/EntryPoint.h`, after the existing includes, add:

```cpp
#include "Function/Diagnostics/PerfGate.h"
```

In `main()`, after smoke test parsing and before `application->start();`, add:

```cpp
const AshEngine::PerfGateConfig perfGateConfig = AshEngine::parse_perf_gate_config(argc, argv);
if (perfGateConfig.enabled)
{
    application->configure_perf_gate(perfGateConfig);
}
```

- [ ] **Step 3: Implement Application PerfGate lifecycle**

In `Application.cpp`, implement:

```cpp
auto Application::configure_perf_gate(const PerfGateConfig& config) -> void
{
    perfGateController.configure(config, initConfig.title, activeBackend);
}
```

In `Application::start()`, after `_on_startup();`, add:

```cpp
perfGateController.begin();
```

In the frame loop, after `_present_frame();`, add:

```cpp
if (renderer && perfGateController.is_enabled())
{
    perfGateController.sample_after_frame(renderer->get_frame_stats());
    if (perfGateController.should_request_exit())
    {
        HLogInfo("PerfGate sample window complete; requesting application exit.");
        request_exit();
    }
}
```

In `_shutdown_runtime()`, capture render memory stats after backend shutdown and before context destroy:

```cpp
if (graphicsContext)
{
    graphicsContext->shutdown();
    if (perfGateController.is_enabled())
    {
        perfGateController.capture_render_memory_stats(graphicsContext->get_render_memory_stats());
    }
    graphicsContext->destroy();
    graphicsContext = nullptr;
}
```

Before `MemoryService::instance()->shutdown();`, add:

```cpp
if (perfGateController.is_enabled())
{
    perfGateController.capture_shutdown_heap_stats(MemoryService::instance()->get_heap_stats());
    perfGateController.write_report(false);
}
```

Keep the existing shutdown order otherwise intact.

- [ ] **Step 4: Build Sandbox and Editor**

Run:

```powershell
.\build_sandbox.bat Debug x64
.\build_editor.bat Debug x64
```

Expected: both commands exit `0`.

- [ ] **Step 5: Run a short PerfGate smoke for Sandbox**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --perf-gate --perf-gate-profile=Standard --perf-gate-output=Intermediate/test-reports/perf-gate/manual/sandbox-dx12.json --perf-gate-warmup-seconds=1 --perf-gate-sample-seconds=2 --smoke-test-seconds=5
```

Expected: exit code `0` and file `Intermediate/test-reports/perf-gate/manual/sandbox-dx12.json` exists with `schema_version`, `frames_sampled`, `cpu_frame_time_ms`, and `memory` fields.

- [ ] **Step 6: Commit Application integration**

```powershell
git add -- project/src/engine/Function/Application.h project/src/engine/Function/Application.cpp project/src/engine/EntryPoint.h
git commit -m "Integrate perf gate runtime telemetry"
```

---

### Task 6: Baseline Profile Skeleton

**Files:**
- Create: `tools/perf/perf_gate_baselines.json`

- [ ] **Step 1: Add baseline profile JSON**

Create `tools/perf/perf_gate_baselines.json`:

```json
{
  "schema_version": 1,
  "profiles": {
    "Standard": {
      "configuration": "Debug",
      "warmup_seconds": 10,
      "sample_seconds": 30,
      "timeout_seconds": 90,
      "targets": [
        {
          "target": "Sandbox",
          "backends": [ "Vulkan", "DX12" ],
          "trend_source": true
        },
        {
          "target": "Editor",
          "backends": [ "Vulkan", "DX12" ],
          "trend_source": false
        }
      ],
      "absolute_caps": {
        "sandbox_private_bytes_mb": 4096,
        "editor_private_bytes_mb": 6144
      },
      "warn_thresholds": {
        "cpu_frame_time_avg_percent": 10,
        "cpu_frame_time_p95_percent": 15,
        "cpu_frame_time_p99_percent": 25,
        "private_bytes_peak_percent": 15,
        "engine_heap_peak_percent": 15,
        "draw_call_count_percent": 10
      }
    }
  },
  "baselines": {}
}
```

- [ ] **Step 2: Validate JSON parsing**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Content -Raw 'tools/perf/perf_gate_baselines.json' | ConvertFrom-Json | Out-Null"
```

Expected: exit code `0`.

- [ ] **Step 3: Commit baseline skeleton**

```powershell
git add -- tools/perf/perf_gate_baselines.json
git commit -m "Add perf gate baseline profile"
```

---

### Task 7: Perf Gate Runner Script

**Files:**
- Create: `scripts/RunPerfGate.ps1`

- [ ] **Step 1: Add script skeleton with dry-run support**

Create `scripts/RunPerfGate.ps1`:

```powershell
param(
    [string]$Profile = "Standard",
    [string]$Configuration = "",
    [switch]$SkipBuild,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    $path = Resolve-Path (Join-Path $PSScriptRoot "..")
    if (!(Test-Path (Join-Path $path "AshEngine.sln"))) {
        throw "Could not resolve AshEngine repository root from $PSScriptRoot"
    }
    return $path.Path
}

function Set-EngineBackend {
    param([string]$ConfigPath, [string]$Backend)
    $lines = Get-Content -LiteralPath $ConfigPath
    $output = New-Object System.Collections.Generic.List[string]
    $inRhi = $false
    $updated = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            $inRhi = ($matches[1].Trim().ToLowerInvariant() -eq "rhi")
            $output.Add($line)
            continue
        }
        if ($inRhi -and $line -match '^\s*Backend\s*=') {
            $output.Add("Backend=$Backend")
            $updated = $true
            continue
        }
        $output.Add($line)
    }
    if (!$updated) {
        $output.Add("")
        $output.Add("[RHI]")
        $output.Add("Backend=$Backend")
    }
    Set-Content -LiteralPath $ConfigPath -Value $output -Encoding UTF8
}

function New-RunRecord {
    param([string]$Target, [string]$Backend, [string]$Executable, [string]$TelemetryPath)
    [PSCustomObject]@{
        target = $Target
        backend = $Backend
        executable = $Executable
        telemetry = $TelemetryPath
        exit_code = $null
        status = "NOT_RUN"
        failures = @()
        warnings = @()
    }
}

$repoRoot = Get-RepoRoot
$baselinePath = Join-Path $repoRoot "tools/perf/perf_gate_baselines.json"
$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
$profileConfig = $baseline.profiles.$Profile
if ($null -eq $profileConfig) {
    throw "Unknown perf gate profile '$Profile'."
}
if ([string]::IsNullOrWhiteSpace($Configuration)) {
    $Configuration = $profileConfig.configuration
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/perf-gate/$timestamp"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

$engineConfig = Join-Path $repoRoot "product/config/Engine.ini"
$engineConfigBackup = Join-Path $reportRoot "Engine.ini.backup"
Copy-Item -LiteralPath $engineConfig -Destination $engineConfigBackup -Force

$records = @()
try {
    foreach ($target in $profileConfig.targets) {
        foreach ($backend in $target.backends) {
            $exeName = if ($target.target -eq "Editor") { "Editor.exe" } else { "Sandbox.exe" }
            $runDir = Join-Path $repoRoot "product/bin64/$Configuration-windows-x86_64"
            $exePath = Join-Path $runDir $exeName
            $telemetryPath = Join-Path $reportRoot ("{0}-{1}.json" -f $target.target, $backend)
            $record = New-RunRecord $target.target $backend $exePath $telemetryPath
            $records += $record

            if (!(Test-Path $exePath)) {
                $record.status = "FAIL"
                $record.failures += "Missing executable: $exePath"
                continue
            }

            if ($DryRun) {
                $record.status = "DRY_RUN"
                continue
            }

            Set-EngineBackend -ConfigPath $engineConfig -Backend $backend
            $arguments = @(
                "--perf-gate",
                "--perf-gate-profile=$Profile",
                "--perf-gate-output=$telemetryPath",
                "--perf-gate-target=$($target.target)",
                "--perf-gate-warmup-seconds=$($profileConfig.warmup_seconds)",
                "--perf-gate-sample-seconds=$($profileConfig.sample_seconds)",
                "--smoke-test-seconds=$([double]$profileConfig.warmup_seconds + [double]$profileConfig.sample_seconds + 10.0)"
            )
            Push-Location $runDir
            try {
                & $exePath @arguments *> (Join-Path $reportRoot ("{0}-{1}.log" -f $target.target, $backend))
                $record.exit_code = $LASTEXITCODE
            }
            finally {
                Pop-Location
            }
            if ($record.exit_code -ne 0) {
                $record.status = "FAIL"
                $record.failures += "Process exited with code $($record.exit_code)"
                continue
            }
            if (!(Test-Path $telemetryPath)) {
                $record.status = "FAIL"
                $record.failures += "Missing telemetry JSON: $telemetryPath"
                continue
            }
            $telemetry = Get-Content -Raw -LiteralPath $telemetryPath | ConvertFrom-Json
            if ($telemetry.backend_actual -ne $backend) {
                $record.status = "FAIL"
                $record.failures += "Backend mismatch: requested $backend, actual $($telemetry.backend_actual)"
                continue
            }
            if ($telemetry.memory.engine_heap_shutdown_live_bytes -ne 0) {
                $record.status = "FAIL"
                $record.failures += "Engine heap live bytes at shutdown: $($telemetry.memory.engine_heap_shutdown_live_bytes)"
                continue
            }
            if ($telemetry.memory.gpu_allocator_supported -and $telemetry.memory.gpu_allocator_shutdown_live_bytes -ne 0) {
                $record.status = "FAIL"
                $record.failures += "GPU allocator live bytes at shutdown: $($telemetry.memory.gpu_allocator_shutdown_live_bytes)"
                continue
            }
            $record.status = "PASS"
        }
    }
}
finally {
    Copy-Item -LiteralPath $engineConfigBackup -Destination $engineConfig -Force
}

$overall = if ($records | Where-Object { $_.status -eq "FAIL" }) { "FAIL" } elseif ($records | Where-Object { $_.status -eq "DRY_RUN" }) { "DRY_RUN" } else { "PASS" }
$summary = [PSCustomObject]@{
    schema_version = 1
    profile = $Profile
    configuration = $Configuration
    status = $overall
    report_root = $reportRoot
    runs = $records
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportRoot "summary.json") -Encoding UTF8

$markdown = @()
$markdown += "# AshEngine Perf Gate Summary"
$markdown += ""
$markdown += "Status: $overall"
$markdown += ""
$markdown += "| Target | Backend | Status | Failures |"
$markdown += "| --- | --- | --- | --- |"
foreach ($record in $records) {
    $failureText = ($record.failures -join "; ")
    $markdown += "| $($record.target) | $($record.backend) | $($record.status) | $failureText |"
}
$markdown | Set-Content -LiteralPath (Join-Path $reportRoot "summary.md") -Encoding UTF8

Write-Host "Perf gate report: $reportRoot"
if ($overall -eq "FAIL") {
    exit 1
}
exit 0
```

- [ ] **Step 2: Run dry-run**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard -DryRun -SkipBuild
```

Expected: exit code `0`; a new `Intermediate/test-reports/perf-gate/<timestamp>/summary.json` exists and run records have `DRY_RUN`.

- [ ] **Step 3: Run a short real gate by temporarily reducing profile duration**

Edit `tools/perf/perf_gate_baselines.json` only for the local test run:

```json
"warmup_seconds": 1,
"sample_seconds": 2,
"timeout_seconds": 20
```

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard -SkipBuild
```

Expected: exit code `0`; `summary.md` lists Sandbox and Editor on Vulkan and DX12. Restore the profile to 10/30/90 before committing.

- [ ] **Step 4: Commit script**

```powershell
git add -- scripts/RunPerfGate.ps1 tools/perf/perf_gate_baselines.json
git commit -m "Add standard perf gate runner"
```

---

### Task 8: Documentation Updates

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`

- [ ] **Step 1: Update README validation section**

In `README.md`, under "验证与调试", add:

````markdown
标准性能门禁入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard
```

报告输出到 `Intermediate/test-reports/perf-gate/<timestamp>/`。Phase 1 中，崩溃、超时、backend 错配、validation/debug-layer 错误、Engine heap 或 Vulkan VMA shutdown live bytes 会失败；CPU frame time、FPS、draw/pass/dispatch 和内存峰值作为趋势报告，不作为硬失败。
````

In README's document list, add:

```markdown
- 性能门禁实现计划：[`docs/superpowers/plans/2026-05-18-perf-gate-implementation.md`](docs/superpowers/plans/2026-05-18-perf-gate-implementation.md)
```

- [ ] **Step 2: Update EngineDeveloperGuide diagnostics section**

In `docs/EngineDeveloperGuide.md`, under section `12. 日志、验证、调试与泄露定位`, add a subsection:

```markdown
### 12.8 Standard Perf Gate

标准性能门禁通过 `scripts/RunPerfGate.ps1 -Profile Standard` 运行。脚本会按 `tools/perf/perf_gate_baselines.json` 中的 Standard profile 切换 backend，运行 Sandbox 与 Editor，并把每次运行的 telemetry JSON、日志和汇总报告写入 `Intermediate/test-reports/perf-gate/<timestamp>/`。

Phase 1 telemetry 由 Engine 侧 PerfGate controller 生成，字段包括 CPU frame time、FPS、draw/pass/dispatch 数量、进程 working set/private bytes、Engine heap current/peak/shutdown live bytes，以及 backend memory stats。`cpu_frame_time_ms` 只表示 CPU 侧 frame orchestration/submit 时间，不代表 GPU 执行时间；未来 GPU timestamp query 应写入独立的 `gpu_frame_time_ms` 字段。

Phase 1 的硬失败包括非 0 退出、超时、telemetry 缺失或损坏、backend mismatch、validation/debug-layer error、Engine heap shutdown live bytes 非 0、Vulkan VMA shutdown live bytes 非 0，以及 profile 配置的绝对内存上限。FPS、frame-time percentile、draw/pass/dispatch 和内存峰值只做 WARN 趋势报告。
```

- [ ] **Step 3: Validate documentation links**

Run:

```powershell
Select-String -Path README.md -Pattern "RunPerfGate|性能门禁实现计划"
Select-String -Path docs/EngineDeveloperGuide.md -Pattern "Standard Perf Gate|cpu_frame_time_ms"
```

Expected: both commands print matching lines.

- [ ] **Step 4: Commit documentation**

```powershell
git add -- README.md docs/EngineDeveloperGuide.md docs/superpowers/plans/2026-05-18-perf-gate-implementation.md
git commit -m "Document standard perf gate workflow"
```

---

### Task 9: Final Verification

**Files:**
- Verify all changed files from Tasks 1-8.

- [ ] **Step 1: Regenerate solution**

Run:

```powershell
.\generate_vs2022.bat
```

Expected: exit code `0`.

- [ ] **Step 2: Build Debug targets**

Run:

```powershell
.\build_sandbox.bat Debug x64
.\build_editor.bat Debug x64
```

Expected: both commands exit `0`.

- [ ] **Step 3: Run Engine self-tests**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code `0`.

- [ ] **Step 4: Run standard perf gate**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard -SkipBuild
```

Expected: exit code `0`; latest `Intermediate/test-reports/perf-gate/<timestamp>/summary.json` has `status` equal to `PASS` or `WARN`, not `FAIL`.

- [ ] **Step 5: Inspect generated telemetry**

Run:

```powershell
$latest = Get-ChildItem Intermediate/test-reports/perf-gate -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Get-ChildItem $latest.FullName -Filter *.json
Get-Content -Raw (Join-Path $latest.FullName "summary.md")
```

Expected: per-run JSON files exist for Sandbox and Editor on both backends; `summary.md` includes the matrix table.

- [ ] **Step 6: Check working tree**

Run:

```powershell
git status --short
```

Expected: no unstaged tracked changes from the perf gate implementation. Existing unrelated untracked files may remain if they predated this work.

---

## Execution Notes

- Use `Debug` as the Phase 1 default profile configuration so validation/debug-layer and VMA leak tracking participate in the gate. A future Release performance lane can be added after Phase 1 without changing the telemetry schema.
- Do not modify `project/src/editor` for Phase 1. Editor participates only by running through the shared Engine entry point.
- Keep PerfGate disabled unless `--perf-gate` is present.
- Keep JSON output under `Intermediate/test-reports/perf-gate/`.
- Do not compare Debug and Release baselines against each other.
