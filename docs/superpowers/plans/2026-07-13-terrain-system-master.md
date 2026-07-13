# Terrain System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved 8 km × 8 km editor-authorable Terrain system, including the approved minimal GPU timing RHI telemetry contract, while preserving dual-backend correctness and the 1440p/300 FPS acceptance target.

**Architecture:** Phase 0 first establishes non-blocking Vulkan/DX12 GPU timestamp snapshots and proves the empty Editor baseline. One `TerrainComponent` then references one versioned `.AshTerrain` asset; sparse CPU edit layers publish immutable component snapshots, while rendering keeps packed R16 height data in a StorageBuffer, writes resident weight tiles through compute into a physical atlas, and submits shared-grid Component LODs as instanced batches.

**Tech Stack:** C++17, doctest, JSON scene schema, versioned binary asset container, WIC, TinyEXR v3.2.0, HLSL/DXC, RenderGraph, Vulkan, DirectX 12, Premake5, PowerShell gates.

---

## Authoritative inputs

- Approved SDD: `docs/sdd/SDD-2026-07-13-terrain-system.md`
- Repository map: `docs/CODEBASE_MAP.md`
- Validation matrix: `docs/VERIFY.md`
- Module contracts: `docs/specs/modules/{scene,asset,render,graphics,editor,tools}.md`

The SDD wins over this plan if wording differs. The only approved new public Graphics contract is fixed-capacity, asynchronous `GpuTimingRHI`; stop and revise the SDD before adding physics collision, splines, vegetation, World Partition, RVT, Nanite, runtime editing, arbitrary rotation, texture-region upload, or any other public Graphics/RHI interface.

## Phase plans

Execute these plans in order. Each phase ends in a clean, independently reviewed commit series.

1. `docs/superpowers/plans/2026-07-13-terrain-phase-0-feasibility.md`
2. `docs/superpowers/plans/2026-07-13-terrain-phase-1-asset-core.md`
3. `docs/superpowers/plans/2026-07-13-terrain-phase-2-rendering.md`
4. `docs/superpowers/plans/2026-07-13-terrain-phase-3-editor-authoring.md`
5. `docs/superpowers/plans/2026-07-13-terrain-phase-4-validation-performance.md`

## Global invariants

- Keep `Base ← Graphics ← Function ← Editor/Sandbox`; Editor code uses `UIContext` and Function public interfaces only.
- Use TDD for every pure-logic or bug-fix step: failing test, observed RED, minimal implementation, observed GREEN.
- Do not use `git add -A`; stage only files listed by the active task and inspect `git diff --cached`.
- Preserve `firstInstance == 0`; every LOD batch starts its instance buffer at element zero.
- Do not use Vulkan sparse binding, bindless descriptors, mesh shaders, or backend-specific Terrain behavior.
- Do not add texture-region upload to `Graphics/CommandBuffer.h`; dirty texture updates use StorageBuffer upload plus compute UAV writes.
- GPU timing never waits for the device. It publishes each submitted frame index once, aggregates same-name scopes per frame, and treats missing/duplicate/overflow/error data as a PerfGate failure.
- Readiness uses task/upload/present completion signals. Frame counts and sleeps are failure timeouts only, never success criteria.
- Do not edit perf baselines or render goldens directly. Use bless commands only after explicit user confirmation.
- Keep Terrain work isolated from unrelated dirty files and concurrent Editor work.

## Phase gate workflow

### Task 1: Establish the execution branch state

**Files:**
- Read: `docs/sdd/SDD-2026-07-13-terrain-system.md`
- Read: active phase plan

- [ ] **Step 1: Confirm the approved SDD and clean index**

Run:

```powershell
git branch --show-current
git status --short
Select-String -LiteralPath docs/sdd/SDD-2026-07-13-terrain-system.md -Pattern '^Approved$'
```

Expected: branch `codex/terrain-system-design`, no staged/untracked implementation files, one `Approved` match.

- [ ] **Step 2: Record the phase start SHA**

Run:

```powershell
$phaseStartFile = git rev-parse --git-path terrain-phase-start.sha
git rev-parse HEAD | Set-Content -LiteralPath $phaseStartFile -Encoding ascii
Get-Content -LiteralPath $phaseStartFile
```

Expected: one 40-character SHA stored in the worktree's private Git metadata, not in the tracked worktree.

### Task 2: Execute one phase only

**Files:**
- Read: the selected phase plan from the ordered list above

- [ ] **Step 1: Complete every checkbox in the active phase plan**

Use the exact tests and commit boundaries in that plan. Do not begin the next phase while the active phase has a failing gate or unresolved P0–P2 review item.

- [ ] **Step 2: Run the phase exit gate**

Run the exit-gate commands listed at the end of the active phase plan.

Expected: every required command exits 0; validation logs contain no API, synchronization, lifetime, or leak errors.

- [ ] **Step 3: Review the phase diff**

Run:

```powershell
$phaseStartSha = Get-Content -LiteralPath (git rev-parse --git-path terrain-phase-start.sha)
git diff --check "$phaseStartSha..HEAD"
git diff --name-only "$phaseStartSha..HEAD"
git show --stat --oneline HEAD
git status --short
```

Expected: no whitespace errors, only active-phase files in the whole phase series, clean worktree.

### Task 3: Verify Phase 4 closed the complete feature

**Files:**
- Read: `docs/specs/features/terrain.md`
- Read: `docs/specs/modules/{scene,asset,render,graphics,editor,tools}.md`
- Read: `docs/CODEBASE_MAP.md`
- Read: `docs/VERIFY.md`
- Read: `docs/sdd/SDD-2026-07-13-terrain-system.md`

- [ ] **Step 1: Verify all five phase gates are complete**

Run:

```powershell
git log --oneline --extended-regexp --grep='gpu timing|terrain' --decorate
```

Expected: commits for GPU timing/feasibility, asset core, rendering, editor authoring, and final validation/performance.

- [ ] **Step 2: Verify long-lived specs and final measurements are already closed**

Run:

```powershell
Select-String -LiteralPath docs/sdd/SDD-2026-07-13-terrain-system.md -Pattern '^Done$'
Select-String -LiteralPath docs/specs/features/terrain.md -Pattern 'AshTerrain|TerrainComponent|TerrainQueryStatus|Terrain.GBuffer|Terrain.Shadow'
```

Expected: one `Done` status and matches for every implemented long-lived contract. Phase 4 owns these documentation edits and its documentation commit; do not edit or commit them again here.

- [ ] **Step 3: Verify final evidence freshness and clean state**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\AIDevDoctor.ps1 -Mode ValidatePlan -IncludePerfGate
git status --short
git log -1 --format='%H %s'
```

Expected: AIDevDoctor exits 0 with fresh PerfGate evidence, worktree is clean, and the last commit is Phase 4's documentation closure. No additional commit is created by this master verification task.
