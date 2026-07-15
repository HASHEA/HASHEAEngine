# RHI Self-Test and Readiness Composition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing CI command complete raw RHI, constant-buffer, RenderGraph indirect, and readiness validation in one process without treating a successful one-shot self-test as an early readiness failure.

**Architecture:** Keep the RenderGraph indirect self-test one-shot when no readiness automation is active. When readiness is active, a passing self-test yields control back to the existing normal render/present/readiness loop; a failing self-test remains fail-closed and terminal. The existing CI command is the persistent integration regression, so no single-call production abstraction or source-text unit test is added.

**Tech Stack:** C++17, doctest/engine runtime integration, Premake5 + MSBuild, GitHub Actions Windows runner, Vulkan/DX12.

---

### Task 1: Capture the existing CI lifecycle failure as RED

**Files:**
- Inspect: `.github/workflows/ci.yml:93-119`
- Inspect: `project/src/engine/Function/Application.cpp:537-549`
- Evidence: `product/logs/*.logfile`

- [ ] **Step 1: Wait for the shared CPU/GPU window**

Do not start build, Sandbox, validation, RenderGate, or PerfGate while another worktree owns the shared window. Fresh-precheck effective roots before running the reproduction.

- [ ] **Step 2: Build the exact CI target**

Run:

```bat
build_sandbox.bat Release
```

Expected: exit 0 and `product/bin64/Release-windows-x86_64/Sandbox.exe` is fresh.

- [ ] **Step 3: Run the current DX12 combined command**

Run:

```bat
run.bat sandbox dx12 Release --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

Expected RED: raw indirect, constant buffer, and RenderGraph indirect log PASS; process exits 1 with `readiness automation terminated before reaching a final outcome`. If it does not reproduce, stop and inspect the exact executable/source SHA before changing production code.

### Task 2: Make successful self-test completion compose with readiness

**Files:**
- Modify: `project/src/engine/Function/Application.cpp:539-549`

- [ ] **Step 1: Apply the minimal lifecycle condition**

Replace the unconditional exit after the RenderGraph indirect self-test with failure-terminal and no-automation one-shot behavior:

```cpp
renderGraphIndirectSelfTestCompleted = true;
const bool graphSelfTestPassed = renderer && run_render_graph_indirect_self_test(*renderer);
framePresentCompleted = graphSelfTestPassed;
if (!graphSelfTestPassed)
{
	runtimeFailureDetected.store(true, std::memory_order_release);
	request_exit();
}
else if (!automationEnabled)
{
	request_exit();
}
```

Do not add a helper or public API. On pass + readiness, the current iteration may be observed as non-ready; the next iteration enters the existing normal render/present path and readiness controller.

- [ ] **Step 2: Rebuild Sandbox Release**

Run:

```bat
build_sandbox.bat Release
```

Expected: exit 0.

- [ ] **Step 3: Re-run the DX12 command as GREEN**

Run:

```bat
run.bat sandbox dx12 Release --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

Expected GREEN: all three self-tests PASS, readiness succeeds on a later normal frame, Sandbox reports `clean_exit=yes`, process exits 0, and fresh logs contain no generic error/critical, validation error, device lost, access violation, fatal, or assert.

- [ ] **Step 4: Verify the one-shot contract remains bounded**

Run:

```bat
run.bat sandbox dx12 Release --rhi-selftest-indirect --rhi-selftest-constant-buffer --run-for-frames=1
```

Expected: all requested self-tests PASS and process exits 0 without requiring readiness success.

### Task 3: Record the corrected contract

**Files:**
- Modify: `.github/workflows/ci.yml:93-119`
- Modify: `docs/specs/modules/application.md`
- Modify: `docs/sdd/SDD-2026-07-15-rhi-selftest-readiness-composition.md`

- [ ] **Step 1: Update the CI comments without changing commands**

State that `--rhi-selftest-indirect` now covers raw RHI plus the Function RenderGraph full-chain self-test, and that a passing self-test composes with readiness in the same process. Keep both DX12 and Vulkan commands unchanged so they remain the regression oracle.

- [ ] **Step 2: Update the Application command-line contract**

Document these exact outcomes under `--rhi-selftest-indirect`:

```text
failure -> runtime failure and immediate non-zero shutdown
success + readiness enabled -> continue normal frames until readiness completes
success + readiness disabled -> bounded one-shot exit
```

- [ ] **Step 3: Mark the Mini SDD Done**

Change its status from `Approved` to `Done` and record the RED/GREEN commands plus final verification results. Do not claim a backend or gate passed until fresh evidence exists.

### Task 4: Run the required regression matrix

**Files:**
- Verify only; do not edit baselines or configuration persistently.

- [ ] **Step 1: Run the Vulkan combined command**

Run:

```bat
run.bat sandbox vulkan Release --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

Expected: same four-stage PASS contract as DX12, exit 0, `clean_exit=yes`, reject patterns 0.

- [ ] **Step 2: Run focused and full CPU tests**

Run:

```bat
RunTests.bat Debug --test-case=*indirect self-test*
RunTests.bat Debug
RunArchGate.bat
```

Expected: all exit 0; ArchGate introduces no new violations.

- [ ] **Step 3: Run the Application readiness matrix**

Run:

```bat
run.bat all Debug --smoke-test-seconds=120
```

Expected: Editor/Sandbox × Vulkan/DX12 all exit 0; Sandbox clean exits; fresh reject patterns 0; runtime configuration files restored byte-for-byte.

- [ ] **Step 4: Run the rendering regression gate**

Run:

```bat
RunRenderGate.bat
```

Expected: PASS without `-BlessGolden`; no golden changes.

- [ ] **Step 5: Run static/tooling checks**

Run:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check
```

Expected: both exit 0.

### Task 5: Selectively commit and update PR #9

**Files:**
- Stage only the Mini SDD, implementation plan, `Application.cpp`, CI workflow comment, and Application spec.
- Exclude: `project/thirdparty/tracy/tracy-csvexport.exe`
- Exclude: `project/thirdparty/tracy/tracy-profiler.exe`

- [ ] **Step 1: Audit the staged paths**

Run:

```bat
git diff --cached --name-status
git diff --cached --check
```

Expected: only the approved lifecycle-fix paths; no Tracy binaries, baselines, goldens, or runtime configuration.

- [ ] **Step 2: Commit the verified fix**

Run:

```bat
git commit -m "fix(application): compose self-test with readiness"
```

Expected: one focused implementation commit after all gates pass.

- [ ] **Step 3: Push and audit PR head**

Run:

```bat
git push origin codex/gpu-driven-foundation
```

Expected: local HEAD, remote branch SHA, and PR #9 head SHA match; PR remains OPEN and non-draft.
