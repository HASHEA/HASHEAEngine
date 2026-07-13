# Terrain Phase 4 Validation and Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the approved Terrain correctness, visual, performance, residency, edit-latency, and 30-minute stability requirements into reproducible dual-backend gates and close the feature only when every hard limit passes.

**Architecture:** RenderGate uses readiness-triggered captures for a deterministic Terrain scene; PerfGate uses the Phase 0 GPU timing snapshots and fixed 2560 × 1440 render target for Static, Brush, and LongRun workloads. Absolute limits live in a versioned Terrain contract separate from comparative baselines, while goldens and baselines remain bless-only artifacts requiring explicit user confirmation.

**Tech Stack:** C++17 telemetry, PowerShell, JSON, Vulkan/DX12 validation, SSIM/heatmaps, PerfGate schema v2, AIDevDoctor.

---

## Prerequisites and hard limits

- Phase 0 through Phase 3 are complete, reviewed, and green.
- Release Editor, VSync off, validation off for performance; validation on for correctness runs.
- Fixed output: 2560 × 1440. Reference class: RTX 4070 or RX 7800 XT.
- CPU frame P95 ≤ 3.33 ms; GPU frame P95 ≤ 3.33 ms.
- Terrain cull/LOD/submit CPU P95 ≤ 0.25 ms.
- `Terrain.GBuffer` GPU P95 ≤ 0.8 ms; `Terrain.Shadow` GPU P95 ≤ 0.6 ms.
- Terrain GPU resident memory ≤ 512 MiB.
- During `stroke_active`, CPU and GPU frame P95 are each ≤ 16.67 ms. Stroke-end-to-generation-ready P95 is ≤ 100 ms. The first complete present after every generation-ready signal starts `post_ready`, where CPU/GPU frame P95 must immediately return to ≤ 3.33 ms; no fixed recovery-frame allowance exists.
- LongRun collects one steady-clock memory sample per second for 30 minutes after readiness and a 60-second warmup. The SDD does not yet quantify acceptable allocator/driver noise, so Task 6 is an explicit approval checkpoint before any numeric leak rule is written or claimed as passed.
- Do not directly edit `tools/perf/perf_gate_baselines.json` or `tools/render/goldens/`; use bless commands only after explicit user confirmation.

## File map

- Create `tools/perf/terrain_perf_contract.json`: approved absolute limits and workload definitions, not a comparative baseline.
- Modify `scripts/RunPerfGate.ps1` and `scripts/TestRunPerfGate.ps1`: Terrain workloads, absolute checks, adapter evidence, memory slope and latency reports.
- Modify `project/src/engine/Function/Diagnostics/PerfGate.*`: named CPU spans, GPU residency series, readiness latency events.
- Modify `project/src/engine/Function/Render/TerrainRenderPass.*`: final named telemetry only; optimizations remain local and evidence-driven.
- Modify `project/src/editor/Services/TerrainEditorService.*`: deterministic Brush/LongRun automation driver and readiness events.
- Create `product/assets/scenes/TerrainPerf.scene.json` and `TerrainRenderGate.scene.json` plus their referenced Terrain/material assets.
- Modify `scripts/RunRenderGate.ps1` and its tests to register the Terrain scene.
- Update long-lived specs, `docs/CODEBASE_MAP.md`, and `docs/VERIFY.md` after measured behavior is final.

### Task 1: Add the absolute Terrain performance contract

**Files:**
- Create: `tools/perf/terrain_perf_contract.json`
- Create through guarded approval: `tools/perf/terrain_reference_adapters.json`
- Modify: `scripts/RunPerfGate.ps1`
- Modify: `scripts/TestRunPerfGate.ps1`

- [ ] **Step 1: Write RED self-tests for every hard limit**

Build synthetic telemetry records where one metric exceeds its limit at a time. Assert `Test-TerrainAbsoluteContract` reports the exact metric and measured/limit values. Include missing GPU scope, wrong `render_output` extent, unknown adapter, Debug configuration, and incomplete 30-minute sample as failures. A different swapchain extent is allowed and remains separately reported. Dry-run assertions require every Static/Brush/LongRun executable and run directory to use Release with VSync/validation off and a 2560 × 1440 scene output.

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1`

Expected: FAIL because Terrain absolute contract handling does not exist.

- [ ] **Step 2: Add the versioned contract file**

Create exactly these machine limits:

```json
{
  "schema_version": 1,
  "scenario": "Terrain",
  "render_output": { "width": 2560, "height": 1440 },
  "limits": {
    "cpu_frame_p95_ms": 3.33,
    "gpu_frame_p95_ms": 3.33,
    "terrain_cpu_p95_ms": 0.25,
    "terrain_gbuffer_gpu_p95_ms": 0.8,
    "terrain_shadow_gpu_p95_ms": 0.6,
    "terrain_gpu_resident_mb": 512.0,
    "brush_active_cpu_frame_p95_ms": 16.67,
    "brush_active_gpu_frame_p95_ms": 16.67,
    "stroke_ready_p95_ms": 100.0,
    "post_ready_cpu_frame_p95_ms": 3.33,
    "post_ready_gpu_frame_p95_ms": 3.33
  },
  "workloads": {
    "Static": { "warmup_seconds": 30, "sample_seconds": 30 },
    "Brush": { "warmup_seconds": 30, "sample_seconds": 30 },
    "LongRun": { "warmup_seconds": 60, "sample_seconds": 1800 }
  }
}
```

- [ ] **Step 3: Implement strict contract evaluation**

Load this file only for `-Scenario Terrain`. Force `Configuration=Release`; reject an explicit Debug configuration. Treat absent/null/non-finite metrics as failure. Record PCI vendor/device ID, adapter name, and driver in summary JSON. Read exact approved vendor/device pairs from `tools/perf/terrain_reference_adapters.json`; an absent pair makes the run `UNQUALIFIED`. Do not infer qualification from a fuzzy marketing-name match and do not claim the hard gate passed.

Keep comparative baseline warnings separate from absolute failures. `-BlessBaseline` may update comparison data but can never change this contract file.

- [ ] **Step 4: Probe and explicitly approve the reference adapter identity**

Run: `RunPerfGate.bat -Profile Standard -Scenario Terrain -Configuration Release -ProbeReferenceAdapter`

Expected: the runner prints exact vendor/device IDs, adapter name, driver, and exits `UNQUALIFIED`. Present that evidence to the user and stop until they confirm the device belongs to the approved RTX 4070 / RX 7800 XT class. After confirmation, run the guarded `-ApproveReferenceAdapter` path; it adds exactly that vendor/device pair to `terrain_reference_adapters.json` and records the approval date without modifying performance baselines.

- [ ] **Step 5: Run self-tests and commit**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
git add tools/perf/terrain_perf_contract.json tools/perf/terrain_reference_adapters.json scripts/RunPerfGate.ps1 scripts/TestRunPerfGate.ps1
git diff --cached --check
git commit -m "test(terrain): define absolute performance contract"
```

Expected: self-test exits 0; commit contains only the listed files.

### Task 2: Emit complete Terrain CPU, GPU, residency, and latency telemetry

**Files:**
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Create: `project/src/tests/Terrain/terrain_perf_telemetry_tests.cpp`

- [ ] **Step 1: Write RED aggregation tests**

Feed two Terrain CPU spans, two same-name GPU scopes, resident-memory samples, and three stroke latency events into the controller. Assert per-frame CPU sums, same-hash GPU sums, peaks, P95 values, and chronological memory samples. Reject negative/non-finite durations and generation-ready events that do not match a recorded stroke end.

Run: `RunTests.bat Debug --test-case="Terrain perf telemetry*"`

Expected: FAIL because Terrain telemetry fields are absent.

- [ ] **Step 2: Instrument stable boundaries**

Measure CPU cull + LOD selection + instance submission under canonical `Terrain.CpuSubmit`. Keep existing GPU hashes `Terrain.GBuffer` and `Terrain.Shadow`. Export Terrain-owned height, weight atlas, coarse weights, material arrays, instance buffers, and staging allocation resident bytes without counting unrelated renderer allocations.

At stroke mouse-up, record asset ID, stroke sequence, and steady-clock timestamp. When the same content generation reaches compose + upload completion readiness, emit one latency sample. A failed or superseded generation records a failure, not a shorter replacement latency. Tag every frame as `stroke_active`, `stroke_waiting_ready`, `post_ready`, or `static`; missing/unknown tags fail the Brush gate. The first complete present after readiness enters `post_ready` immediately.

- [ ] **Step 3: Add report schema fields**

Under PerfGate schema v2 emit `terrain.cpu_submit_ms`, `terrain.gpu_resident_bytes`, `terrain.stroke_ready_ms`, per-state CPU/GPU frame summaries, and `memory_series` with elapsed seconds, process private bytes, and Terrain GPU bytes. Keep raw memory series only for LongRun; other workloads emit summaries.

- [ ] **Step 4: Run tests and commit**

```powershell
RunTests.bat Debug --test-case="Terrain perf telemetry*"
RunTests.bat Debug
git add project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp project/src/engine/Function/Render/TerrainRenderPass.h project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_perf_telemetry_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): report performance telemetry"
```

Expected: focused and full Debug suites pass.

### Task 3: Build deterministic Static, Brush, and LongRun workloads

**Files:**
- Create: `product/assets/scenes/TerrainPerf.scene.json`
- Create: `product/assets/terrain/TerrainPerf.AshTerrain`
- Create: `product/assets/terrain/TerrainPerf.manifest.json`
- Create: `product/assets/materials/TerrainPerf/` assets
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.h`
- Modify: `project/src/engine/Function/Diagnostics/PerfGate.cpp`
- Modify: `scripts/RunPerfGate.ps1`
- Modify: `scripts/TestRunPerfGate.ps1`
- Create: `project/src/tests/Terrain/terrain_perf_workload_tests.cpp`

- [ ] **Step 1: Write RED workload-state tests**

Assert all workloads wait for readiness before warmup. Static never mutates Terrain. Brush replays one canonical 64 m radius world-space path with fixed seed/spacing, classifies active/waiting/post-ready frames, and waits for the generation-ready plus first-complete-present signal before scheduling the next stroke. LongRun cycles the same bounded set of edits and undos, so logical Terrain content and history memory remain bounded.

Run: `RunTests.bat Debug --test-case="Terrain perf workload*"`

Expected: FAIL because the workload controller is absent.

- [ ] **Step 2: Create the reference Terrain asset and scene**

Generate through the approved Terrain import/save APIs: 8193² samples, eight material layers loaded, typical view containing 1–2 blended layers, a marked pressure region containing four blended layers, one directional light with shadows, and no other meshes/particles/water/vegetation. Store the scene SHA-256, Terrain container SHA-256, each material asset SHA-256, Terrain generation, dimensions, height mapping, and canonical camera/environment/light/render-config hash in `product/assets/terrain/TerrainPerf.manifest.json`; gate startup validates every field and requires the canonical hash to equal `product/assets/scenes/TerrainPerfEmpty.manifest.json` before readiness.

- [ ] **Step 3: Implement signal-driven workload control**

Add `--terrain-perf-workload=Static|Brush|LongRun`. For every Terrain workload, pass the Phase 0 required-scope set `{ Terrain.GBuffer, Terrain.Shadow }`; missing either scope in any sampled frame is an absolute failure, while Empty continues to require no named pass. Drive transitions with Terrain readiness, submitted generation, upload completion, and present completion. Use wall-clock values only for warmup/sample duration and hard timeout; never use a frame count or sleep as a success condition.

- [ ] **Step 4: Extend runner self-test and dry-run**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -Profile Standard -Scenario Terrain -Configuration Release -DryRun
```

Expected: self-test passes; dry-run lists Static, Brush, and LongRun for Vulkan and DX12 with the same scene hash and 2560 × 1440 `render_output`.

- [ ] **Step 5: Commit workloads**

Stage only the exact files below, inspect the binary Terrain asset size/hash in the cached diff, and commit:

```powershell
git add product/assets/scenes/TerrainPerf.scene.json product/assets/terrain/TerrainPerf.AshTerrain product/assets/terrain/TerrainPerf.manifest.json product/assets/materials/TerrainPerf project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/engine/Function/Diagnostics/PerfGate.h project/src/engine/Function/Diagnostics/PerfGate.cpp scripts/RunPerfGate.ps1 scripts/TestRunPerfGate.ps1 project/src/tests/Terrain/terrain_perf_workload_tests.cpp
git diff --cached --check
git commit -m "test(terrain): add deterministic performance workloads"
```

### Task 4: Add the Terrain RenderGate scene without blessing

**Files:**
- Create: `product/assets/scenes/TerrainRenderGate.scene.json`
- Create: `product/assets/terrain/TerrainRenderGate.AshTerrain`
- Create: `product/assets/terrain/TerrainRenderGate.manifest.json`
- Create: `product/assets/materials/TerrainRenderGate/` assets
- Modify: `scripts/RunRenderGate.ps1`
- Create: `scripts/TestRunRenderGate.ps1`
- Modify: `RunRenderGate.bat`

- [ ] **Step 1: Write the RED registration/readiness test**

Require the default scene list to contain `terrain`, both backend captures, its exact scene/manifest paths, readiness-based capture arguments, SSIM heatmap output, timeout-without-PNG failure, missing-golden failure, cross-backend diff, and refusal to bless without `-BlessGolden`. Assert no fixed-frame option appears in the generated command. Keep `TestRenderGateGoldenPublisher.ps1` unchanged as the separate transactional publisher test.

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunRenderGate.ps1`

Expected: FAIL because the terrain scene is unregistered.

- [ ] **Step 2: Create the visual contract scene**

Use deterministic slopes, plateaus, a component boundary through the camera view, two adjacent LOD levels, 1/2/4-layer blend regions, one directional shadow, and a camera that exposes geomorph and weight-gutter seams. Record the scene, Terrain, and material SHA-256 values plus Terrain generation in `product/assets/terrain/TerrainRenderGate.manifest.json`. Validate the manifest and asset through normal load/readiness; do not use a special renderer path.

- [ ] **Step 3: Register signal-based capture**

Add `terrain` to RenderGate's default scene table. Capture only after asset load, compose, height/atlas upload completion, scene submission, and present completion all match the current generation. Retain the existing wall-clock timeout and cross-backend SSIM comparison. Add `-ValidationEvidence`: it uses Debug validation/debug layers, requires both backends to publish a readiness capture, scans VUID/D3D12 query/resource/lifetime diagnostics, and does not use golden success as a substitute for clean validation.

- [ ] **Step 4: Run temporary captures, not bless**

Run: `RunRenderGate.bat -Scenes terrain`

Expected on first introduction: both backend PNGs and the cross-backend diff/heatmap are produced; the gate reports missing goldens. Inspect the images for seams, LOD pops, shadow mismatch, material bleed, and backend differences.

- [ ] **Step 5: Commit the scene and runner only**

```powershell
git add product/assets/scenes/TerrainRenderGate.scene.json product/assets/terrain/TerrainRenderGate.AshTerrain product/assets/terrain/TerrainRenderGate.manifest.json product/assets/materials/TerrainRenderGate scripts/RunRenderGate.ps1 scripts/TestRunRenderGate.ps1 RunRenderGate.bat
git diff --cached --check
git commit -m "test(render): add terrain visual gate scene"
```

Expected: no files under `tools/render/goldens/` are staged.

### Task 5: Profile and fix only measured performance failures

**Files:**
- Modify only the Terrain files identified by Phase 4 telemetry.
- Test the same component under `project/src/tests/Terrain/`.

- [ ] **Step 1: Run Static and Brush before changing code**

```powershell
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload Static -Configuration Release
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload Brush -Configuration Release
```

Expected: reports identify the exact failing metric, backend, adapter, pass, and P95. If both pass, make no optimization change and continue to Task 6.

- [ ] **Step 2: For each failure, add a regression test or reproducible counter assertion**

Examples of accepted evidence are excessive visible-component count, atlas upload bytes, instance rebuild count, material sample count, or compose job fan-out. The failing test must assert the observed bound before implementation changes.

- [ ] **Step 3: Apply one focused optimization and rerun its test**

Permitted areas are quadtree culling/LOD reuse, instance-buffer reuse, atlas residency/upload batching, shader zero-weight/top-four pruning, and asynchronous composition scheduling already approved by the SDD. Do not add GPU culling, bindless, sparse textures, mesh shaders, or a new RHI API.

- [ ] **Step 4: Re-run both workloads and commit one root cause at a time**

```powershell
RunTests.bat Debug --test-case="Terrain*"
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload Static -Configuration Release
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload Brush -Configuration Release
git diff --cached --check
git commit -m "perf(terrain): reduce measured terrain frame cost"
```

Expected: all absolute limits pass on both reference backends. If more than one independent bottleneck is fixed, repeat Steps 2–4 and commit each root cause separately.

### Task 6: Run the 30-minute stability gate

**Files:**
- Modify: `scripts/RunPerfGate.ps1`
- Modify: `scripts/TestRunPerfGate.ps1`

- [ ] **Step 1: Add RED cadence and completeness tests**

Verify a steady-clock sampler emits at most one point per elapsed second without sleeping, requires 1800 chronological samples per backend, rejects duplicate timestamps and gaps larger than five seconds, and requires complete first/final five-minute windows of 300 samples each.

- [ ] **Step 2: Implement raw characterization and observe GREEN**

Implement the 1 Hz sampler and report raw process-private/Terrain-GPU series, Theil-Sen slope, first/final five-minute median and P95, missing-sample count, and duration. Characterization mode writes summary JSON `status = "NEEDS_APPROVAL"` and exits with numeric code 3; it must not label memory stability PASS, WARN, or return 0.

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1`

Expected: PASS.

- [ ] **Step 3: Run one dual-backend characterization and obtain the missing acceptance approval**

Run:

```powershell
& .\RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload LongRun -Configuration Release -CharacterizeMemory
if ($LASTEXITCODE -ne 3) { throw "Expected NEEDS_APPROVAL exit code 3, got $LASTEXITCODE" }
```

Expected: both 30-minute samples complete, summary status is `NEEDS_APPROVAL`, and the script exits 3. The execution harness treats only this characterization step's code 3 as an intentional pause; ordinary gates still treat every nonzero exit as failure. Present the raw series, slopes, first/final windows, and a concrete proposed process/GPU noise tolerance to the user. Stop until the user explicitly approves the numeric rule; then amend the SDD and `terrain_perf_contract.json` with exactly that approved rule before writing PASS/FAIL analysis.

- [ ] **Step 4: Write RED stable/noisy/growing tests against the approved rule**

Construct deterministic stable, bounded-noise, and monotonic-growth series using the approved numbers. Stable and bounded-noise series pass; monotonic growth and any exceeded window/slope limit fail with a machine-readable reason.

- [ ] **Step 5: Implement the approved rule and run LongRun**

Run: `RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload LongRun -Configuration Release`

Expected: both 30-minute samples complete, every approved memory rule passes, no crash/device loss/validation error occurs, and the process exits normally after the completion signal.

- [ ] **Step 6: Commit approved analysis changes**

```powershell
git add docs/sdd/SDD-2026-07-13-terrain-system.md tools/perf/terrain_perf_contract.json scripts/RunPerfGate.ps1 scripts/TestRunPerfGate.ps1
git diff --cached --check
git commit -m "test(terrain): gate long-run memory growth"
```

### Task 7: Obtain visual approval and bless Terrain goldens

**Files:**
- Create through bless only: `tools/render/goldens/terrain/vulkan.png`
- Create through bless only: `tools/render/goldens/terrain/dx12.png`

- [ ] **Step 1: Present both captures and heatmap to the user**

Stop and request explicit confirmation that Vulkan and DX12 Terrain images are visually correct. Include absolute image paths and measured cross-backend SSIM. Do not run bless before approval.

- [ ] **Step 2: Bless only the Terrain scene after approval**

Run: `RunRenderGate.bat -Scenes terrain -BlessGolden`

Expected: the publisher updates only the two Terrain golden files through the guarded bless path.

- [ ] **Step 3: Re-run the complete render gate**

Run: `RunRenderGate.bat`

Expected: sandbox, particles, and terrain golden comparisons plus all cross-backend comparisons pass.

- [ ] **Step 4: Commit the approved goldens**

```powershell
git add tools/render/goldens/terrain/vulkan.png tools/render/goldens/terrain/dx12.png
git diff --cached --name-only
git commit -m "test(render): bless approved terrain goldens"
```

Expected: exactly two files are committed.

### Task 8: Run the complete exit matrix and close documentation

**Files:**
- Create/Modify: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/scene.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/specs/modules/render.md`
- Modify: `docs/specs/modules/graphics.md`
- Modify: `docs/specs/modules/editor.md`
- Modify: `docs/specs/modules/tools.md`
- Modify: `docs/CODEBASE_MAP.md`
- Modify: `docs/VERIFY.md`
- Modify: `docs/sdd/SDD-2026-07-13-terrain-system.md`

- [ ] **Step 1: Run full builds, unit, architecture, smoke, render, and performance gates**

```powershell
generate_vs2022.bat
RunTests.bat Debug
RunTests.bat Release
RunArchGate.bat
build_editor.bat Debug
build_editor.bat Release
build_sandbox.bat Debug
build_sandbox.bat Release
run.bat all Debug --smoke-test-seconds=120
RunRenderGate.bat -Scenes terrain -Configuration Debug -ValidationEvidence
RunRenderGate.bat
RunPerfGate.bat -Profile Standard
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Release -NoTracy
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload Static -Configuration Release
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload Brush -Configuration Release
RunPerfGate.bat -Profile Standard -Scenario Terrain -Workload LongRun -Configuration Release
```

Expected: every command exits 0; the validation-evidence run produces both Terrain readiness captures with no Vulkan/DX12 diagnostic; the fresh no-Tracy run passes after all Engine/Render/Editor changes and restores a clean standard Release build before Terrain workloads; no PerfGate WARN is left unexplained; all validation logs are clean.

- [ ] **Step 2: Update only implemented long-lived behavior**

Record schema v6, `.AshTerrain` v1, layer/brush/query contracts, rendering/readiness, Editor workflow, GPU timing, gate commands, actual CPU/GPU/memory/latency results, and known non-goals. Change SDD status from `Approved` to `Done` and link the final report directories.

- [ ] **Step 3: Run documentation and plan audit**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan -IncludePerfGate
git diff --check
```

Expected: AIDevDoctor exits 0 and diff check is clean.

- [ ] **Step 4: Commit closure documentation**

```powershell
git add docs/specs/features/terrain.md docs/specs/modules/scene.md docs/specs/modules/asset.md docs/specs/modules/render.md docs/specs/modules/graphics.md docs/specs/modules/editor.md docs/specs/modules/tools.md docs/CODEBASE_MAP.md docs/VERIFY.md docs/sdd/SDD-2026-07-13-terrain-system.md
git diff --cached --check
git commit -m "docs(terrain): record completed terrain system"
```

Expected: one documentation-only commit and clean worktree.
