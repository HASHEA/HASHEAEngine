# Terrain Runtime Performance and Loading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove Terrain shadow CPU amplification, bound first-load memory/work, share fallback material resources, and replace per-frame rebuild error spam with a typed Ready/Pending/Failed contract.

**Architecture:** `SceneRenderer` creates one immutable, frame-bounded Terrain prepared draw from the primary view and passes it to GBuffer and every shadow callback. `TerrainRenderAsset` keeps immutable component references, packs/uploads a bounded number of heights per render update, generates weights only for requested atlas residency, and borrows renderer-owned fallback arrays. `RenderScene` polls one shared asynchronous Terrain load and reports a typed result to `ScenePresentationSubsystem`, which keeps non-Terrain scene content valid while readiness remains fail-closed.

**Tech Stack:** C++17, Function Asset/Render/ScenePresentation, immutable Terrain snapshots, RenderGraph consumers without API changes, doctest, Premake5/MSBuild, Vulkan/DX12 validation, RenderGate and PerfGate.

---

## Authoritative inputs and execution order

- Approved SDD: `docs/sdd/SDD-2026-07-15-terrain-runtime-performance-loading.md`
- Long-term specs: `docs/specs/features/terrain.md`, `docs/specs/modules/asset.md`, `docs/specs/modules/rendering.md`
- Validation matrix: `docs/VERIFY.md`
- This plan lands before `2026-07-15-terrain-interactive-authoring-workflow.md` because live preview increases publication frequency.
- Execution is inline in `codex/terrain-system-design`; no subagent is requested. Every task uses focused RED/GREEN and a selective commit.
- Never stage the existing dirty Editor settings, `docs/verification/`, user scene, or user Terrain assets. Never edit or bless render/perf baselines.

## File responsibility map

- `project/src/engine/Function/Render/TerrainRenderPass.h/.cpp`: immutable prepared draw, one instance-buffer update, GBuffer/shadow consumers, atlas weight payload generation.
- `project/src/engine/Function/Render/SceneRenderer.cpp`: construct the prepared draw before graph lambdas and capture it in GBuffer/directional/sunlight passes.
- `project/src/engine/Function/Render/TerrainRenderAsset.h/.cpp`: bounded component upload queue, lazy weight staging, readiness diagnostics, shared fallback references.
- `project/src/engine/Function/Render/RenderAssetManager.h/.cpp`: renderer-owned fallback Terrain arrays and repeated pending-finalization scheduling.
- `project/src/engine/Function/Render/RenderScene.h/.cpp`: per-path async Terrain resolve state and typed rebuild result.
- `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`: separate general scene validity from Terrain readiness and log only state transitions.
- `project/src/engine/Function/Render/ScenePresentationReadiness.h/.cpp`: preserve fail-closed automation status for Pending/Failed Terrain.
- `project/src/tests/Terrain/terrain_render_graph_tests.cpp`: prepared draw integration and one-preparation source/runtime contracts.
- `project/src/tests/Terrain/terrain_render_asset_tests.cpp`: bounded bytes, lazy weights, fallback sharing and generation cancellation.
- `project/src/tests/Terrain/terrain_render_scene_tests.cpp`: async resolve and non-Terrain scene validity.
- `project/src/tests/Terrain/terrain_readiness_tests.cpp`: typed readiness and transition behavior.
- `README.md`, `docs/specs/features/terrain.md`, `docs/specs/modules/asset.md`, `docs/specs/modules/rendering.md`: delivered behavior, diagnostics and validation evidence.

### Task 1: Prepare Terrain draw data once per primary view and frame

**Files:**
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_graph_tests.cpp`

- [ ] **Step 1: Add a failing structural and CPU-contract test**

Add a test named `Terrain SceneRenderer prepares one immutable draw for GBuffer and shadows`. It must assert that `SceneRenderer.cpp` calls `prepare_draw(` once before graph pass declarations, captures `terrain_prepared_draw`, and that neither `render_gbuffer` nor `render_shadow` contains `build_terrain_lod_batches` or `ensure_instance_buffer`.

```cpp
TEST_CASE("Terrain SceneRenderer prepares one immutable draw for GBuffer and shadows")
{
    const std::string renderer = ReadSource("project/src/engine/Function/Render/SceneRenderer.cpp");
    const std::string terrain = ReadSource("project/src/engine/Function/Render/TerrainRenderPass.cpp");
    CHECK(CountText(renderer, "m_terrain_render_pass.prepare_draw(") == 1u);
    CHECK(renderer.find("terrain_prepared_draw") != std::string::npos);
    const size_t consumer_begin = terrain.find("bool TerrainRenderPass::render_prepared_surface");
    REQUIRE(consumer_begin != std::string::npos);
    const std::string_view consumers(terrain.data() + consumer_begin, terrain.size() - consumer_begin);
    CHECK(consumers.find("build_terrain_lod_batches") == std::string_view::npos);
    CHECK(consumers.find("ensure_instance_buffer") == std::string_view::npos);
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
.\RunTests.bat Debug --test-case="Terrain SceneRenderer prepares one immutable draw*"
```

Expected: FAIL because `prepare_draw` and `render_prepared_surface` do not exist.

- [ ] **Step 3: Introduce the frame-bounded prepared draw contract**

Add these Function-internal values in `TerrainRenderPass.h`; they have two real consumers (GBuffer and shadow) and do not alter Graphics/RHI APIs.

```cpp
struct TerrainPreparedDraw
{
    std::shared_ptr<const TerrainAssetSnapshot> asset_snapshot{};
    std::shared_ptr<TerrainRenderAsset> render_asset{};
    glm::mat4 world_transform{ 1.0f };
    TerrainLodResult lod{};
    std::vector<uint32_t> batch_offsets{};
    std::shared_ptr<StorageBuffer> instance_buffer{};
    uint64_t render_frame_index = 0u;
    bool casts_shadow = false;

    bool is_drawable() const;
};

using TerrainPreparedDrawPtr = std::shared_ptr<const TerrainPreparedDraw>;
```

Expose `prepare_draw(frame, primary_view_context, render_frame_index)` and change `render_gbuffer`/`render_shadow` to accept `const TerrainPreparedDrawPtr&`. Move `make_lod_view`, `build_terrain_lod_batches`, atlas-slot lookup, packing, batch offsets and `ensure_instance_buffer` into `prepare_draw`. Keep object-to-clip constants in the consumer so each shadow cascade still uses its own light view-projection.

- [ ] **Step 4: Wire the single producer into SceneRenderer**

Immediately after `prepare_graph`, construct once:

```cpp
const TerrainPreparedDrawPtr terrain_prepared_draw =
    m_terrain_render_pass.prepare_draw(frame, view_context, render_frame_index);
```

Capture that shared immutable object in the GBuffer lambda and the composite shadow callback. `ShadowCasterMobilityFilter::DynamicOnly` must still return without drawing Terrain; `casts_shadow=false` must not submit shadow draws.

- [ ] **Step 5: Run focused Terrain render tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*render*"
.\RunTests.bat Debug --test-case="*Terrain*shadow*"
```

Expected: all selected tests pass; source contract shows one producer and no consumer-side preparation.

- [ ] **Step 6: Commit the prepared-draw change**

```powershell
git add project/src/engine/Function/Render/TerrainRenderPass.h project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/engine/Function/Render/SceneRenderer.cpp project/src/tests/Terrain/terrain_render_graph_tests.cpp
git diff --cached --check
git commit -m "perf(terrain): reuse prepared draws across shadow passes"
```

### Task 2: Bound height packing/upload and generate weight payloads lazily

**Files:**
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.cpp`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_asset_tests.cpp`

- [ ] **Step 1: Write RED tests for pending-memory and empty-weight behavior**

Add tests that accept the canonical 1024-component snapshot and assert: pending entries retain component pointers rather than 1024 packed vectors; only one transient packed height payload exists at a time; empty component weights require zero packed weight bytes; a stale generation cannot publish after replacement. A fake work-budget clock proves that processing stops because the elapsed wall-clock budget was consumed, not because a fixed component/frame count was reached.

```cpp
CHECK(asset.pending_component_upload_count() == 1024u);
CHECK(asset.pending_cpu_payload_bytes() <=
    AshEngine::k_terrain_render_height_words_per_component * sizeof(uint32_t));
CHECK(asset.pending_weight_payload_bytes() == 0u);
```

- [ ] **Step 2: Run the focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain render asset bounds pending CPU payload*"
.\RunTests.bat Debug --test-case="Terrain render asset leaves empty weights implicit*"
```

Expected: compile failure because diagnostics do not exist, or assertions expose the eager ~646 MiB payload.

- [ ] **Step 3: Split height and weight packing helpers**

Replace the all-domain helper with explicit functions while preserving the old wrapper for existing callers/tests until migration is complete:

```cpp
ASH_API bool build_terrain_component_height_words(
    const TerrainComponentSnapshot& component,
    const TerrainHeightMapping& mapping,
    std::vector<uint32_t>& out_words,
    std::string* out_error = nullptr);

ASH_API bool build_terrain_component_weight_rgba8(
    const TerrainComponentSnapshot& component,
    std::array<std::vector<uint8_t>, 2>& out_weights,
    std::string* out_error = nullptr);
```

The empty-weight fast path returns success with empty vectors and semantically means lane 0 weight 255.

- [ ] **Step 4: Store immutable component references and consume a bounded chunk**

Change pending entries to:

```cpp
struct TerrainGpuComponentUpload
{
    TerrainComponentCoord coord{};
    uint64_t content_generation = 0u;
    std::shared_ptr<const TerrainComponentSnapshot> component{};
};
```

`accept_snapshot` validates shape/coord only and queues shared pointers. `finalize_gpu_resources` packs one component into a reusable transient vector, uploads it, releases/reuses that vector, and continues while both a byte-work budget and a `std::chrono::steady_clock` deadline remain. The default deadline is a small render-update wall-clock budget; the loop has no fixed component/frame count. It removes only successfully uploaded entries, keeps readiness Pending while work remains, and publishes only after uploads and removals are empty. Generation replacement drops old entries before they can write. Tests inject a deterministic clock/budget helper so RED/GREEN does not depend on machine speed.

- [ ] **Step 5: Pack weights only when an atlas slot needs an update**

In the atlas update path, locate the immutable component for the selected coord, call `build_terrain_component_weight_rgba8`, write the existing single `TerrainWeightUpload` staging buffer, and release both temporary vectors after pass data capture. Empty weights upload the constant lane-0 pattern without a persistent per-component vector.

- [ ] **Step 6: Run focused render-asset and graph tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain render asset*"
.\RunTests.bat Debug --test-case="Terrain atlas*"
```

Expected: generation/readiness tests pass, pending bytes stay bounded, and atlas barrier tests remain unchanged.

- [ ] **Step 7: Commit bounded/lazy uploads**

```powershell
git add project/src/engine/Function/Render/TerrainRenderAsset.h project/src/engine/Function/Render/TerrainRenderAsset.cpp project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/tests/Terrain/terrain_render_asset_tests.cpp
git diff --cached --check
git commit -m "perf(terrain): bound height uploads and defer weights"
```

### Task 3: Share fallback Terrain material arrays per RenderAssetManager

**Files:**
- Modify: `project/src/engine/Function/Render/RenderAssetManager.h`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_asset_tests.cpp`

- [ ] **Step 1: Write a failing ownership test**

Create two Terrain render assets through one manager and require all three fallback array pointers to be identical, while a second manager owns a different set. After `shutdown`, weak references must expire.

- [ ] **Step 2: Verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain fallback material arrays are manager owned*"
```

Expected: FAIL because each `TerrainRenderAsset` currently calls `create_fallback_material_array` three times.

- [ ] **Step 3: Add the manager-owned resource value**

```cpp
struct TerrainFallbackMaterialArrays
{
    std::array<std::shared_ptr<RenderTarget>, 3> arrays{};
    bool is_valid() const;
};
```

Add `request_terrain_fallback_material_arrays()` to `RenderAssetManager`, cache one shared instance on the manager, and reset it before renderer/material shutdown. Move the existing array creation helper into the manager implementation.

- [ ] **Step 4: Inject shared fallback arrays into TerrainRenderAsset**

`request_terrain_asset` obtains the manager resource and passes it to `TerrainRenderAsset::set_fallback_material_arrays`. `finalize_gpu_resources` fails closed if the shared resource cannot be created; it no longer allocates arrays itself.

- [ ] **Step 5: Run focused tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="Terrain fallback material arrays*"
.\RunTests.bat Debug --test-case="Terrain render asset manager*"
git add project/src/engine/Function/Render/RenderAssetManager.h project/src/engine/Function/Render/RenderAssetManager.cpp project/src/engine/Function/Render/TerrainRenderAsset.h project/src/engine/Function/Render/TerrainRenderAsset.cpp project/src/tests/Terrain/terrain_render_asset_tests.cpp
git diff --cached --check
git commit -m "perf(terrain): share fallback material arrays"
```

### Task 4: Replace synchronous Terrain rebuild and repeated errors with typed resolve state

**Files:**
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`
- Modify: `project/src/engine/Function/Render/ScenePresentationReadiness.h`
- Modify: `project/src/engine/Function/Render/ScenePresentationReadiness.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_scene_tests.cpp`
- Modify: `project/src/tests/Terrain/terrain_readiness_tests.cpp`

- [ ] **Step 1: Write RED tests for Pending, Failed and transition logging**

Test contracts:

```cpp
CHECK(result.status == TerrainSceneResolveStatus::Pending);
CHECK(result.asset_path == "terrain/Test.AshTerrain");
CHECK(result.diagnostic.empty());
CHECK(scene_non_terrain_content_remains_valid);
```

Add source/runtime assertions that unchanged Pending produces no error, the first Failed transition logs one path+reason, repeated Failed logs zero new errors, and Ready recovery clears the failure without recreating the shared request.

- [ ] **Step 2: Run focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain RenderScene async resolve*"
.\RunTests.bat Debug --test-case="Terrain presentation logs state transitions*"
```

Expected: FAIL because rebuild is synchronous and returns only `bool`.

- [ ] **Step 3: Define the typed result and persistent request state**

In `RenderScene.h` add:

```cpp
enum class TerrainSceneResolveStatus : uint8_t { Ready, Pending, Failed };

struct TerrainSceneResolveResult
{
    TerrainSceneResolveStatus status = TerrainSceneResolveStatus::Ready;
    std::string asset_path{};
    std::string diagnostic{};
    uint64_t content_generation = 0u;
};
```

`RenderScene` stores one `{canonical_path, shared_future, catalog/revision identity}` request for the current Terrain entity. `rebuild_terrains_from_scene` starts `load_terrain_by_path_async` once, polls with zero timeout, returns Pending without `get()`, requests the render asset when the future is ready, and returns Failed with the canonical path plus AssetDatabase/render-asset diagnostic on stable failure.

- [ ] **Step 4: Separate Scene validity from Terrain resolve state**

Extend `ScenePresentationSubsystem::SceneState` with the last Terrain result and a transition identity. General primitives/lights/environment/particles continue to build when Terrain is Pending/Failed. Visible frame creation includes the last valid Terrain proxy only when its generation remains valid; Terrain readiness remains Pending/Failed and automation cannot capture success.

- [ ] **Step 5: Log transitions once**

Pending may emit one debug/info transition, Failed emits exactly one error containing binding, scene, canonical path and diagnostic, and Ready recovery may emit one info. Equality of status/path/generation/diagnostic suppresses duplicates.

- [ ] **Step 6: Run focused and full CPU tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*resolve*"
.\RunTests.bat Debug --test-case="*Terrain*readiness*"
.\RunTests.bat Debug
.\RunTests.bat Release
```

Expected: all tests pass; Pending never produces the prior per-frame error string.

- [ ] **Step 7: Commit typed resolve behavior**

```powershell
git add project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Function/Render/ScenePresentationSubsystem.cpp project/src/engine/Function/Render/ScenePresentationReadiness.h project/src/engine/Function/Render/ScenePresentationReadiness.cpp project/src/tests/Terrain/terrain_render_scene_tests.cpp project/src/tests/Terrain/terrain_readiness_tests.cpp
git diff --cached --check
git commit -m "fix(terrain): make scene loading pending-aware"
```

### Task 5: Update specs and run the complete runtime gate

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/specs/modules/rendering.md`
- Modify: `docs/sdd/SDD-2026-07-15-terrain-runtime-performance-loading.md`

- [ ] **Step 1: Record delivered contracts before final gates**

Document one prepared draw per frame/view, bounded height work, implicit empty weights, manager-owned fallbacks, typed resolve/log transitions, and the exact readiness signal. Mark the SDD `Implementing`; do not mark `Done` before all evidence passes.

- [ ] **Step 2: Run CPU/build/architecture gates**

```powershell
.\generate_vs2022.bat
.\RunTests.bat Debug
.\RunTests.bat Release
.\build_editor.bat Debug
.\build_editor.bat Release
.\build_sandbox.bat Debug
.\build_sandbox.bat Release
.\RunArchGate.bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

Expected: every command exits 0; no new architecture legacy entry.

- [ ] **Step 3: Coordinate and run GPU gates strictly serially**

After both other sessions explicitly release the CPU/GPU window, snapshot Engine.ini, EditorSettings, ViewportLayout and imgui bytes, then run:

```powershell
.\run.bat all Debug --smoke-test-seconds=120
.\RunRenderGate.bat
.\RunPerfGate.bat -Profile Standard
```

Run the Terrain scene once with Vulkan validation and once with DX12 debug layer/GPU validation. Any validation error, RenderGate FAIL, PerfGate FAIL, permanent Pending, config mismatch or leaked root process stops the gate. Do not bless.

- [ ] **Step 4: Run the same-scene Terrain A/B**

Use temporary scene copies under `Intermediate/diagnostics/terrain-runtime-final/<timestamp>` for no Terrain, Terrain shadow off and Terrain shadow on. Require for each backend:

```text
shadow_on CPU avg <= 1.5 * shadow_off CPU avg
Terrain wall-time delta <= 5 seconds
Terrain private peak delta <= 768 MiB
prepared draw count == 1 per rendered frame/view
```

Fresh logs must contain no generic error/critical, validation, device-lost, assert or repeated Terrain rebuild failure.

- [ ] **Step 5: Restore configuration, mark the SDD Done and commit docs**

Byte-restore all four config files, verify their pre-run hashes, verify effective CPU/GPU roots are zero, append report paths/results to the SDD, set Status `Done`, then:

```powershell
git add README.md docs/specs/features/terrain.md docs/specs/modules/asset.md docs/specs/modules/rendering.md docs/sdd/SDD-2026-07-15-terrain-runtime-performance-loading.md
git diff --cached --check
git commit -m "docs(terrain): record runtime performance closure"
```

## Plan self-review

- Spec coverage: prepared draw, shadow semantics, bounded heights, lazy weights, shared fallback arrays, typed resolve/logging, readiness, dual-backend gates and quantitative A/B all map to Tasks 1–5.
- Scope boundary: no Graphics/RHI/RenderGraph API, shadow quality, baseline or full world-streaming changes are included.
- Type consistency: `TerrainPreparedDrawPtr`, `TerrainSceneResolveStatus`, `TerrainSceneResolveResult`, component-pointer pending entries and manager-owned fallback arrays are introduced before use.
- Placeholder scan: no TBD/TODO/“add tests later” steps remain; every task has a RED command, implementation contract, GREEN command and selective commit.
