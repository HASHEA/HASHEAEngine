# AshEngine Standard Perf Gate Design

Status: design approved for implementation planning.

Date: 2026-05-18

## 1. Purpose

AshEngine needs a repeatable pre-submit performance gate for performance-sensitive changes. The gate should catch hard correctness and lifecycle failures immediately, while collecting stable CPU frame-time and memory trend data before turning trend regressions into hard blockers.

The first implementation target is a standard local gate that runs in roughly 5 to 15 minutes and can later be reused by a Git hook or CI without duplicating logic.

## 2. Decisions

- Default gate strength: Standard.
- Implementation route: Engine-generated structured telemetry plus a PowerShell orchestration script.
- First-stage failure policy: report-only for performance trends, hard failure for red-line issues.
- Main performance benchmark: Sandbox standard Sponza scene.
- Editor participation: startup, shutdown, validation, leak, backend, and coarse memory health checks only.
- Trigger: manual script entry point, with optional hook or CI integration later.
- Versioned baseline config: `tools/perf/perf_gate_baselines.json`.
- Generated reports: `Intermediate/test-reports/perf-gate/<timestamp>/`.

## 3. Goals

- Run `Sandbox` and `Editor` on both Vulkan and DX12 through the normal startup and graceful shutdown paths.
- Record machine-readable per-run telemetry as JSON.
- Produce a human-readable summary report.
- Detect crashes, timeouts, backend mismatch, validation errors, debug-layer errors, and memory leaks as hard failures.
- Record CPU frame-time, FPS, draw/pass/dispatch counts, process memory, Engine heap memory, and backend memory where available.
- Keep Editor code untouched unless a later implementation task explicitly expands scope.
- Keep backend-specific details behind Engine or RHI abstractions.

## 4. Non-Goals For Phase 1

- Do not make FPS or frame-time trend changes hard failures.
- Do not require Tracy capture automation.
- Do not require RenderDoc, PIX, or GPU timestamp queries.
- Do not require DX12 allocator live/peak GPU memory if the current backend cannot expose it reliably.
- Do not add a mandatory `pre-commit` hook.

## 5. High-Level Architecture

The standard gate has two halves:

1. Engine telemetry path
   - A small PerfGate diagnostics layer is enabled only by explicit CLI parameters.
   - It samples per-frame runtime stats during a warmup and sample window.
   - It writes a structured JSON report before process exit.

2. Script orchestration path
   - `scripts/RunPerfGate.ps1 -Profile Standard` builds or verifies binaries, switches runtime backends, launches runs, enforces timeouts, gathers logs, and evaluates hard-failure rules.
   - It writes one per-run JSON file plus aggregate `summary.json` and `summary.md` files under `Intermediate/test-reports/perf-gate/<timestamp>/`.

Engine owns measurement. The script owns policy and presentation.

## 6. Runtime Matrix

Standard profile runs:

- `Sandbox + Vulkan`
- `Sandbox + DX12`
- `Editor + Vulkan`
- `Editor + DX12`

Sandbox is the only first-stage source for FPS and frame-time trend reporting. Editor runs still collect telemetry where available, but frame-time/FPS regressions in Editor are not gate failures in Phase 1.

## 7. CLI Contract

The proposed application arguments are:

```text
--perf-gate
--perf-gate-profile=Standard
--perf-gate-output=Intermediate/test-reports/perf-gate/<timestamp>/<target>-<backend>.json
--perf-gate-warmup-seconds=10
--perf-gate-sample-seconds=30
```

The existing smoke-test lifetime controls remain valid. The perf script should either pass `--smoke-test-seconds` large enough to cover warmup plus sample plus shutdown slack, or let PerfGate request exit after sampling completes.

## 8. Telemetry Schema

Each run writes a JSON document with schema versioning:

```json
{
  "schema_version": 1,
  "target": "Sandbox",
  "backend_requested": "Vulkan",
  "backend_actual": "Vulkan",
  "config": "Release",
  "scene": "Sponza.StandardScene",
  "profile": "Standard",
  "warmup_seconds": 10,
  "sample_seconds": 30,
  "frames_total": 160000,
  "frames_sampled": 120000,
  "cpu_frame_time_ms": {
    "avg": 0.25,
    "p50": 0.24,
    "p95": 0.32,
    "p99": 0.45,
    "min": 0.20,
    "max": 1.80
  },
  "fps": {
    "avg": 4000.0,
    "p05": 3125.0
  },
  "render_stats": {
    "draw_calls_avg": 620,
    "graphics_passes_avg": 6,
    "dispatches_avg": 0
  },
  "memory": {
    "process_working_set_peak_mb": 1800,
    "process_private_bytes_peak_mb": 1450,
    "engine_heap_current_mb": 280,
    "engine_heap_peak_mb": 320,
    "engine_heap_shutdown_live_bytes": 0,
    "gpu_allocator_supported": true,
    "gpu_allocator_current_mb": 820,
    "gpu_allocator_peak_mb": 900,
    "gpu_allocator_shutdown_live_bytes": 0
  },
  "errors": {
    "crashed": false,
    "timed_out": false,
    "validation_error_count": 0,
    "debug_layer_error_count": 0,
    "leak_error_count": 0,
    "backend_mismatch": false,
    "abnormal_exit": false
  }
}
```

The numbers above are illustrative structure examples, not current project baselines. Real baseline values must be generated on the target machine by the perf gate tooling.

## 9. Metric Semantics

`cpu_frame_time_ms` is the CPU-side frame time currently represented by `RendererFrameStats`. It measures CPU frame orchestration and submit work. It is not GPU execution time.

- `avg`: average sampled frame time.
- `p50`: median sampled frame time.
- `p95`: 95th percentile sampled frame time, useful for regular stutter.
- `p99`: 99th percentile sampled frame time, useful for long-tail stalls.
- `min`: fastest sampled frame.
- `max`: slowest sampled frame. This is diagnostic only and should not be a Phase 1 hard threshold.

`fps.avg` is derived from average frame time. `fps.p05` is the 5th percentile FPS and approximately corresponds to `1000 / cpu_frame_time_ms.p95`.

Future GPU timestamp work should add a separate `gpu_frame_time_ms` section instead of reusing CPU fields.

## 10. Sampling Model

Standard profile should use:

- Warmup: 10 seconds.
- Sample: 30 seconds.
- Shutdown slack: script-enforced timeout margin.

Warmup frames are excluded from trend metrics. This reduces noise from shader compilation, asset loading, initial pipeline creation, and first-frame allocation.

The Engine telemetry layer should collect raw sampled frame times in memory, then compute percentiles at the end of the run. The report should include the number of total frames and sampled frames so obviously invalid runs are easy to spot.

## 11. Memory Telemetry

Phase 1 memory sources:

- OS process memory
  - Working set peak.
  - Private bytes peak.
- Engine heap memory
  - Current allocated bytes.
  - Peak allocated bytes.
  - Shutdown live bytes.
- Vulkan GPU memory
  - Current and peak bytes where available.
  - Shutdown live bytes from VMA leak tracking.
- DX12 GPU memory
  - May return `gpu_allocator_supported=false` until a reliable backend allocator query exists.

The Engine heap should expose a read-only statistics snapshot from Base memory infrastructure. This is useful outside PerfGate and should not depend on the PerfGate module.

## 12. Red-Line Failures

Any of these should make the perf gate fail:

- Process non-zero exit code.
- Crash.
- Script timeout.
- Missing telemetry JSON.
- Malformed telemetry JSON.
- Requested backend differs from actual backend.
- Vulkan validation error.
- DX12 debug-layer error or corruption.
- Fatal RHI, present, shader compile, or resource lifecycle error in logs.
- Engine heap shutdown live bytes is non-zero.
- Vulkan VMA shutdown live allocation is non-zero.
- Process private bytes or working set exceeds the absolute cap configured for the profile.

These failures are correctness, lifecycle, or reliability issues. They should block a performance-sensitive submission even before trend baselines are stable.

## 13. Trend Reporting

Phase 1 reports these as `WARN`, not `FAIL`:

- `cpu_frame_time_ms.avg` change against baseline.
- `cpu_frame_time_ms.p95` and `p99` change against baseline.
- `fps.avg` and `fps.p05` change against baseline.
- Draw call, graphics pass, and dispatch count changes.
- Process memory peak changes.
- Engine heap peak changes.
- GPU allocator peak changes where supported.

Suggested first warn-only thresholds for Standard:

- Sandbox average CPU frame time: +10 percent.
- Sandbox p95 CPU frame time: +15 percent.
- Sandbox p99 CPU frame time: +25 percent.
- Sandbox process private bytes peak: +15 percent.
- Sandbox Engine heap peak: +15 percent.
- Draw calls or pass count: +10 percent unless intentionally changed.

These values should live in `tools/perf/perf_gate_baselines.json` and can be tuned after collecting real reports.

## 14. Baseline File

The baseline file is versioned and should contain:

- Profile definitions.
- Absolute memory caps.
- Warn-only relative thresholds.
- Optional per-target and per-backend baseline values.
- Machine metadata for generated baselines.

Example shape:

```json
{
  "schema_version": 1,
  "profiles": {
    "Standard": {
      "warmup_seconds": 10,
      "sample_seconds": 30,
      "timeout_seconds": 90,
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

`baselines` may stay empty in Phase 1, but the profile and threshold file itself should be added with the first runnable gate so the policy is versioned from the start.

## 15. Script Responsibilities

`scripts/RunPerfGate.ps1` should:

- Validate repository root and output directories.
- Optionally regenerate/build, or fail with a clear message if binaries are missing.
- Preserve and restore `product/config/Engine.ini`.
- Run the Standard matrix.
- Write stdout/stderr and log snapshots under the run report directory.
- Enforce per-run timeout.
- Parse telemetry JSON.
- Scan new log output for validation, debug-layer, fatal, and leak signatures.
- Generate:
  - `summary.json`
  - `summary.md`
  - one JSON file per run
  - one log bundle per run
- Exit non-zero on red-line failure.

The script should be the only policy entry point. Future Git hook and CI integrations should call the same script.

## 16. Engine Module Plan

Proposed modules:

- `project/src/engine/Function/Diagnostics/PerfGate.h`
- `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- `project/src/engine/Base/MemoryDiagnostics.h` or a small extension to `hmemory.*`
- RHI-facing memory snapshot structure in an Engine-visible abstraction, not in Editor code.

Responsibilities:

- Parse PerfGate CLI args.
- Track warmup and sample windows.
- Sample `RendererFrameStats`.
- Sample process and Engine heap memory.
- Request backend memory stats through a generic interface.
- Write JSON at shutdown or when sampling completes.

This path must include Tracy instrumentation only where it touches hot paths or new sampling code that runs per frame. PerfGate must remain disabled by default and should impose no measurable overhead when not enabled.

## 17. Backend Memory Interface

Function or Graphics should expose a backend-neutral structure:

```cpp
struct RenderMemoryStats
{
    bool supported = false;
    uint64_t gpu_allocator_current_bytes = 0;
    uint64_t gpu_allocator_peak_bytes = 0;
    uint64_t gpu_allocator_shutdown_live_bytes = 0;
};
```

Vulkan should populate this from VMA or existing VMA leak tracking. DX12 can initially report `supported=false` until D3D12MA or a reliable DXGI budget path is added.

No Vulkan or DX12 concrete types should leak into Editor, Sandbox, or general application code.

## 18. Report Summary

`summary.md` should be optimized for quick review:

- Overall status: PASS, WARN, or FAIL.
- Matrix table with target, backend, exit status, backend match, validation count, leak count, CPU frame-time summary, and memory peaks.
- Red-line failures first.
- Warn-only trend changes second.
- Per-run artifact paths.

`summary.json` should contain the same information for automation.

## 19. Phase Plan

### Phase 1: Runnable Standard Gate

- Add PerfGate CLI and telemetry JSON.
- Add CPU frame-time, FPS, draw/pass/dispatch sampling.
- Add OS process memory and Engine heap memory snapshots.
- Add Vulkan VMA shutdown live allocation to telemetry.
- Mark DX12 GPU allocator stats unsupported if needed.
- Add `scripts/RunPerfGate.ps1`.
- Add the initial `tools/perf/perf_gate_baselines.json` profile and threshold skeleton, with empty measured baselines if needed.
- Generate per-run and aggregate reports.
- Fail on red-line rules.
- Update `README.md` and `docs/EngineDeveloperGuide.md`.

### Phase 2: Baseline Stability

- Add baseline update/bless command.
- Populate stable per-machine or per-reference baselines after enough reports have been collected.
- Record CPU, GPU, driver, OS, config, and backend metadata.
- Turn trend deltas into reliable `WARN` annotations.
- Add Light and Heavy profiles if useful.

### Phase 3: GPU And Deep Profiling

- Add RHI timestamp query support.
- Add separate `gpu_frame_time_ms` metrics.
- Improve DX12 GPU allocator telemetry.
- Optionally export Tracy captures or CSV.
- Add multi-scene benchmarking.
- Add optional pre-push or CI invocation.

## 20. Risks And Mitigations

- CPU frame time can be much lower than GPU frame time. Mitigation: name the field `cpu_frame_time_ms` and reserve `gpu_frame_time_ms` for timestamp queries.
- Very high FPS values amplify timing noise. Mitigation: use warmup, percentiles, and warn-only trend reporting in Phase 1.
- Editor frame-time data is noisy. Mitigation: do not use Editor FPS as a hard performance gate initially.
- Baselines are machine-dependent. Mitigation: record machine metadata before promoting trend warnings to hard failures.
- Log scanning can be brittle. Mitigation: prefer structured telemetry for Engine-generated errors where practical, and keep regex scanning as a supplement.
- PerfGate sampling must not perturb performance. Mitigation: keep it disabled by default and use lightweight aggregation.

## 21. Approval State

The design choices in this document were reviewed and approved during brainstorming:

- Standard gate as the default.
- Report-only trend policy with red-line hard failures.
- Sandbox as the primary performance benchmark.
- Manual script entry point.
- Versioned baseline config plus generated reports under `Intermediate/test-reports/perf-gate/`.
- Engine telemetry plus script policy implementation route.
