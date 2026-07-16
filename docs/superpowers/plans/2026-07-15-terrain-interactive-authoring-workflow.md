# Terrain Interactive Authoring Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one generic non-destructive Terrain edit layer support every sculpt/paint tool, update the Terrain during drag at an 80–100 ms wall-clock cadence with one Undo/Redo record, bind the Terrain panel directly from a selected Hierarchy entity, keep Flatten fixed to one stroke-wide target, and release viewport ownership when the Terrain editing context becomes inactive.

**Architecture:** Function Asset stores each height edit sample as an affine transform `T(H)=aH+b`; tools left-compose their dab so stroke order is preserved, and the v2 container migrates v1 Additive/Alpha data exactly in memory. `TerrainEditorService` owns one incremental stroke transaction with a resumable resampler, one stroke-wide target state, aggregate before/after patch, one in-flight complete-generation publication and delayed history recording. `TerrainModePanel` resolves a single selected entity through Scene/Selection/AssetDatabase services and reuses the existing `SelectAsset` intent instead of opening files itself. Viewport ownership is derived separately from Terrain panel visibility plus the exact current selection, so loss of editing context cancels the active stroke but never unloads the dirty authoring session.

**Tech Stack:** C++17, Function Asset Terrain data/brush/composition/container, Editor service/core/commands/panels through UIContext, doctest, Premake5/MSBuild, Vulkan/DX12 validation, RenderGate, PerfGate and user-performed manual verification.

---

## Authoritative inputs and prerequisites

- Approved SDD: `docs/sdd/SDD-2026-07-15-terrain-interactive-authoring-workflow.md`
- Required prerequisite: completed `2026-07-15-terrain-runtime-performance-loading.md` implementation and gates.
- Long-term specs: `docs/specs/features/terrain.md`, `docs/specs/modules/asset.md`, `docs/specs/modules/editor.md`
- Existing authoring plan/source contracts: `docs/superpowers/plans/2026-07-13-terrain-phase-3-editor-authoring.md`
- Tasks 1–7 were executed inline in the current worktree. The user selected the recommended subagent-driven path for the Task 8 amendment; implementation and review stay in the same isolated Terrain worktree, remain TDD-first, and use explicit path staging only.
- User/editor runtime files and Terrain assets remain unstaged. Container fixtures are created under test temp roots, never by rewriting user `.AshTerrain` files.

## File responsibility map

- `project/src/engine/Function/Asset/TerrainData.h/.cpp`: affine sparse height block and working-set validation.
- `project/src/engine/Function/Asset/TerrainComposition.h/.cpp`: layer-strength composition `lerp(H, aH+b, s)`.
- `project/src/engine/Function/Asset/TerrainBrush.h/.cpp`: resumable stroke sampling, affine dab composition and aggregate patches.
- `project/src/engine/Function/Asset/TerrainEditPatch.cpp`: affine logical-byte codec and atomic undo/redo.
- `project/src/engine/Function/Asset/TerrainLayerStack.h/.cpp`: generic layer metadata and stable layer creation/removal patches.
- `project/src/engine/Function/Asset/TerrainContainerFormat.h/.cpp`, `TerrainContainer.cpp`: v2 writer, v1/v2 reader and exact v1 migration.
- `project/src/engine/Function/Scene/SceneQuery.cpp`: CPU query composition over affine edit layers.
- `project/src/editor/Core/TerrainEditorSessionCore.h/.cpp`: deterministic mutation/composition/publication seams.
- `project/src/editor/Core/TerrainCommands.h/.cpp`: one stroke command with an optional auto-layer patch.
- `project/src/editor/Services/TerrainEditorService.h/.cpp`: live stroke transaction, fake clock seam, coalescing, rollback, delayed history.
- `project/src/editor/Panels/Terrain/TerrainModePanel.h/.cpp`: entity selection resolver and current-selection sync.
- `project/src/editor/Core/TerrainViewportInputRouter.h/.cpp`, `project/src/editor/Panels/ViewportPanelTerrainInteraction.h/.cpp`: fail-closed viewport authoring eligibility, active-stroke cancellation and input hand-back.
- `project/src/editor/Core/PanelDeps/ViewportPanelDeps.h`, `project/src/editor/Services/EditorSessionStateService.h/.cpp`, `project/src/editor/App/PanelBootstrapper.h/.cpp`, `project/src/editor/App/EditorApplicationImpl.cpp`: expose current panel-open and selection state to the viewport without unloading Terrain state.
- `project/src/editor/Panels/Terrain/TerrainModeState.h`, `TerrainModeWidgets.cpp`: terminology and generic-layer UI.
- `project/src/editor/App/PanelBootstrapper.cpp`: inject SceneService and SelectionService.
- `project/src/tests/Terrain/terrain_composition_tests.cpp`, `terrain_brush_tests.cpp`, `terrain_patch_tests.cpp`, `terrain_container_tests.cpp`, `terrain_query_tests.cpp`: affine/migration/patch/query contracts.
- `project/src/tests/Editor/terrain_editor_service_tests.cpp`, `terrain_editor_contract_tests.cpp`: live preview, single history, auto-layer and Hierarchy binding.
- `README.md`, `docs/specs/features/terrain.md`, `docs/specs/modules/asset.md`, `docs/specs/modules/editor.md`: final behavior and manual checklist.

### Task 1: Replace Additive/Alpha height storage with affine samples

**Files:**
- Modify: `project/src/engine/Function/Asset/TerrainData.h`
- Modify: `project/src/engine/Function/Asset/TerrainData.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainComposition.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainLayerStack.h`
- Modify: `project/src/engine/Function/Asset/TerrainLayerStack.cpp`
- Modify: `project/src/engine/Function/Scene/SceneQuery.cpp`
- Modify: `project/src/tests/Terrain/terrain_composition_tests.cpp`
- Modify: `project/src/tests/Terrain/terrain_query_tests.cpp`

- [ ] **Step 1: Write the affine composition RED tests**

Replace the old mode-only oracle with exact cases for identity, strength, hidden layers and order:

```cpp
TEST_CASE("Terrain affine layers preserve tool order and strength")
{
    // Base H=10. Flatten target 20 at c=.5 gives T(H)=.5H+10.
    // Then Raise +3 left-composes to .5H+13 => 18 at full strength.
    AshEngine::TerrainSparseHeightBlock block =
        MakeAffineBlock(/*scale=*/0.5f, /*bias=*/13.0f);
    CHECK(ComposeOneHeight(10.0f, block, 1.0f) == doctest::Approx(18.0f));
    CHECK(ComposeOneHeight(10.0f, block, 0.5f) == doctest::Approx(14.0f));
    CHECK(ComposeOneHeight(10.0f, block, 0.0f) == doctest::Approx(10.0f));
}
```

Add a reversed-order case showing Raise then Flatten yields `.5H+11.5`, not `.5H+13`.

- [ ] **Step 2: Run focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain affine layers*"
```

Expected: compile failure because affine block fields do not exist.

- [ ] **Step 3: Change the canonical sparse height representation**

In `TerrainData.h` use:

```cpp
struct TerrainSparseHeightBlock
{
    TerrainComponentCoord owner{};
    TerrainSampleRect changed_rect{};
    std::vector<float> scales{};
    std::vector<float> biases{};
};

struct TerrainEditLayer
{
    TerrainLayerId id{};
    std::string name{};
    bool visible = true;
    bool locked = false;
    float strength = 1.0f;
    std::vector<TerrainSparseHeightBlock> height_blocks{};
    std::vector<TerrainSparseWeightBlock> weight_blocks{};
};
```

Remove public `TerrainHeightBlendMode`; keep any v1 blend decoding private to the container reader. Deep validation requires equal scale/bias counts, finite values, canonical rect ownership and removes identity `(1,0)` samples from persisted sparse data.

- [ ] **Step 4: Apply affine composition in runtime and CPU queries**

For each visible layer/sample:

```cpp
const double input = static_cast<double>(height);
const double transformed = static_cast<double>(scale) * input + bias;
height = static_cast<float>(
    input + (transformed - input) * static_cast<double>(layer.strength));
```

Reject non-finite intermediate/final values before modifying output. Update `SceneQuery.cpp` to use the same helper or formula so render composition, height query, normal query and ray cast agree.

- [ ] **Step 5: Run composition/query tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain affine layers*"
.\RunTests.bat Debug --test-case="*Terrain*query*"
```

Expected: affine order, hidden/strength and CPU query oracles pass.

- [ ] **Step 6: Commit the affine model**

```powershell
git add project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainData.cpp project/src/engine/Function/Asset/TerrainComposition.cpp project/src/engine/Function/Asset/TerrainLayerStack.h project/src/engine/Function/Asset/TerrainLayerStack.cpp project/src/engine/Function/Scene/SceneQuery.cpp project/src/tests/Terrain/terrain_composition_tests.cpp project/src/tests/Terrain/terrain_query_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): unify sculpt layers with affine edits"
```

### Task 2: Make every brush tool compose into the same layer and preserve affine patches

**Files:**
- Modify: `project/src/engine/Function/Asset/TerrainBrush.h`
- Modify: `project/src/engine/Function/Asset/TerrainBrush.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainEditPatch.cpp`
- Modify: `project/src/tests/Terrain/terrain_brush_tests.cpp`
- Modify: `project/src/tests/Terrain/terrain_patch_tests.cpp`

- [ ] **Step 1: Write RED tool-order and patch tests**

Use one layer for Raise, Smooth, Flatten, Noise and Lower; compare the resulting `(a,b)` with direct function composition. Add patch Undo/Redo byte equality for scale+bias blocks.

```cpp
CHECK(block.scales[index] == doctest::Approx((1.0f - coverage) * old_scale));
CHECK(block.biases[index] == doctest::Approx(
    (1.0f - coverage) * old_bias + coverage * target));
```

- [ ] **Step 2: Verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain brush composes all sculpt tools*"
.\RunTests.bat Debug --test-case="Terrain affine patch*"
```

Expected: current compatibility checks reject tools based on layer mode.

- [ ] **Step 3: Implement left-composed dabs**

For additive `d`, update `b += d`. For Smooth/Flatten with effective coverage `c` and frozen target `t`, update:

```cpp
scale = (1.0f - c) * scale;
bias = (1.0f - c) * bias + c * t;
```

Noise remains deterministic signed additive; Lower negates the Raise delta. Paint/Erase continue to update weight values/coverage in the same generic edit layer. Remove all layer/tool compatibility rejection based on Additive/Alpha.

- [ ] **Step 4: Change patch logical bytes to scale+bias**

The height patch encoder/decoder serializes two finite float planes in row-major order. Undo requires exact current bytes equal to `after_bytes`; Redo requires exact current bytes equal to `before_bytes`; malformed codec/rect/count rejects the whole batch before mutation.

- [ ] **Step 5: Add aggregate-patch support for incremental preview**

Add:

```cpp
ASH_API bool merge_terrain_edit_patches(
    const std::vector<TerrainEditPatch>& next,
    std::vector<TerrainEditPatch>& in_out_aggregate,
    std::string* out_error = nullptr);
```

For each asset/layer/domain/owner union rect, preserve the first observed before value and latest after value; reject sequence/identity mismatches atomically. This function is used by live preview and tested independently here.

- [ ] **Step 6: Run brush/patch tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="Terrain brush*"
.\RunTests.bat Debug --test-case="Terrain patch*"
git add project/src/engine/Function/Asset/TerrainBrush.h project/src/engine/Function/Asset/TerrainBrush.cpp project/src/engine/Function/Asset/TerrainEditPatch.cpp project/src/tests/Terrain/terrain_brush_tests.cpp project/src/tests/Terrain/terrain_patch_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): compose every brush in generic layers"
```

### Task 3: Add exact v1-to-v2 container migration

**Files:**
- Modify: `project/src/engine/Function/Asset/TerrainContainerFormat.h`
- Modify: `project/src/engine/Function/Asset/TerrainContainerFormat.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainContainer.cpp`
- Modify: `project/src/tests/Terrain/terrain_container_tests.cpp`

- [ ] **Step 1: Add RED migration fixtures**

Write minimal v1 bytes through a test-only fixture builder for Additive and Alpha blocks. Load them and assert:

```cpp
CHECK(additive.scales[i] == 1.0f);
CHECK(additive.biases[i] == doctest::Approx(value[i] * coverage[i]));
CHECK(alpha.scales[i] == doctest::Approx(1.0f - coverage[i]));
CHECK(alpha.biases[i] == doctest::Approx(coverage[i] * value[i]));
```

Compare old-formula and affine composition at strength `0`, `.5`, `1`. Require load to leave source bytes untouched; Save/Optimize emits v2 and round-trips identically.

- [ ] **Step 2: Verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain container migrates v1 height layers*"
```

Expected: FAIL because the reader accepts only exact version 1 and the writer has no v2 format.

- [ ] **Step 3: Define supported versions and private v1 metadata**

```cpp
namespace TerrainContainerFormat
{
    inline constexpr uint32_t k_legacy_version = 1u;
    inline constexpr uint32_t k_version = 2u;
    bool is_supported_version(uint32_t version);
}
```

The v2 height block writes scale+bias and omits blend mode from editable layer metadata. The v1 reader retains a private `LegacyTerrainHeightBlendMode` and converts in memory. Unknown versions, invalid blend values, non-finite values and coverage outside `[0,1]` fail closed.

- [ ] **Step 4: Preserve atomic save/recovery contracts**

Incremental save and Optimize must use the current v2 writer, checked source revision, commit lease and existing descriptor recovery path. Merely loading v1 must not change write time, revision, catalog or source bytes.

- [ ] **Step 5: Run complete container tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="Terrain container*"
git add project/src/engine/Function/Asset/TerrainContainerFormat.h project/src/engine/Function/Asset/TerrainContainerFormat.cpp project/src/engine/Function/Asset/TerrainContainer.cpp project/src/tests/Terrain/terrain_container_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): migrate edit layers to container v2"
```

### Task 4: Add a resumable stroke sampler and 80–100 ms preview transaction

**Files:**
- Modify: `project/src/engine/Function/Asset/TerrainBrush.h`
- Modify: `project/src/engine/Function/Asset/TerrainBrush.cpp`
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.h`
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/tests/Terrain/terrain_brush_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_service_tests.cpp`

- [ ] **Step 1: Write RED tests using a fake steady clock**

Cover: no publication before 80 ms; one complete publication at 80–100 ms; only new path segments are applied; samples coalesce while one composition is pending; End flushes the tail; the whole drag records one command; Cancel/publication failure restores Begin bytes and records no command.

```cpp
clock.Advance(79ms); service.Update(); CHECK(publisher.calls == 0u);
clock.Advance(1ms);  service.Update(); CHECK(publisher.calls == 1u);
CHECK(history.recorded_count == 0u);
SubmitEndStroke(service); service.UpdateUntilIdle();
CHECK(history.recorded_count == 1u);
```

- [ ] **Step 2: Verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor previews an active stroke by wall clock*"
.\RunTests.bat Debug --test-case="Terrain editor rolls back every live preview*"
```

Expected: FAIL because `AddStrokeSample` only appends and `EndStroke` owns the first mutation.

- [ ] **Step 3: Introduce a resumable resampler**

Add:

```cpp
struct TerrainStrokeResamplerState
{
    std::optional<TerrainStrokeSample> previous_input{};
    std::optional<TerrainStrokeSample> previous_output{};
    double distance_to_next_sample_meters = 0.0;
};

ASH_API bool append_resampled_terrain_stroke(
    TerrainStrokeResamplerState& state,
    const TerrainBrushMetric& metric,
    float spacing_meters,
    const std::vector<TerrainStrokeSample>& new_input,
    std::vector<TerrainStrokeSample>& out_new_samples,
    std::string* out_error = nullptr);
```

The existing whole-stroke resampler constructs an empty state and calls this function, ensuring the incremental and batch paths share one oracle.

- [ ] **Step 4: Extend ActiveStroke into one transaction**

Store resampler state, unprocessed raw samples, aggregate patches, cumulative dirty components, Begin snapshot/generation, preview deadline, ending/cancel flags and optional auto-layer patch. Inject `std::function<std::chrono::steady_clock::time_point()> _now` with a production default; tests replace it through an `ASH_TESTS` setter.

- [ ] **Step 5: Apply and publish only at the wall-clock boundary**

`AddStrokeSample` validates and appends only. `Update` calls `TryAdvanceActiveStrokePreview` when `now >= next_preview_time` and no composition is in flight. It resamples only new input, applies only new dabs, merges patches, schedules the complete dirty set and advances the deadline by 80 ms from the actual dispatch time. Pending publication coalesces input; it never launches overlapping composition.

- [ ] **Step 6: Delay history until the final publication succeeds**

`EndStroke` marks the transaction ending and flushes remaining input. `CompletePendingComposition` records exactly one aggregate `TerrainStrokeCommand` only after the final generation publishes. Cancel, focus loss, scene preflight, apply/compose/publish/record failure replays aggregate Undo, republishes the complete rollback generation and either returns Ready or enters existing quarantine if rollback cannot be proven.

- [ ] **Step 7: Run focused live-preview tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke sampling*"
.\RunTests.bat Debug --test-case="Terrain editor*stroke*"
git add project/src/engine/Function/Asset/TerrainBrush.h project/src/engine/Function/Asset/TerrainBrush.cpp project/src/editor/Core/TerrainEditorSessionCore.h project/src/editor/Core/TerrainEditorSessionCore.cpp project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_brush_tests.cpp project/src/tests/Editor/terrain_editor_service_tests.cpp
git diff --cached --check
git commit -m "feat(editor): preview terrain strokes during drag"
```

### Task 5: Auto-create one generic edit layer inside the first stroke command

**Files:**
- Modify: `project/src/editor/Core/TerrainCommands.h`
- Modify: `project/src/editor/Core/TerrainCommands.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_service_tests.cpp`

- [ ] **Step 1: Write RED compound-history tests**

For a layerless asset: hover/open/empty/cancel leaves zero layers and clean generation; the first non-empty Raise or Paint creates one layer named `Edit Layer`; history delta is one; Undo removes both content and the layer; Redo restores the same layer ID and content. Locked existing layers reject without auto-switching.

- [ ] **Step 2: Verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor auto-creates one edit layer*"
```

Expected: FAIL because BeginStroke requires a selected compatible layer.

- [ ] **Step 3: Extend TerrainStrokeCommand with an optional layer patch**

```cpp
TerrainStrokeCommand(
    TerrainAssetId asset_id,
    TerrainLayerId layer_id,
    uint64_t sequence,
    std::vector<TerrainEditPatch> patches,
    std::optional<TerrainLayerStackPatch> auto_layer_patch,
    TerrainLayerId selected_before,
    TerrainLayerId selected_after);
```

Redo applies the layer insertion before stroke patches; Undo replays stroke patches before removing the layer. Both paths preserve exact stable ID/selection and fail atomically through the service replay seams.

- [ ] **Step 4: Create the layer only on the first effective dab**

Begin/hover do not mutate. When the first resampled dab proves non-empty and the working set has no layer, apply an Add layer stack edit named `Edit Layer`, save its patch in the ActiveStroke transaction, select its stable ID, and then apply the dab. If the dab is no-op or any later stage cancels, roll back the insertion.

- [ ] **Step 5: Run compound tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor auto-creates one edit layer*"
.\RunTests.bat Debug --test-case="Terrain editor*history*"
git add project/src/editor/Core/TerrainCommands.h project/src/editor/Core/TerrainCommands.cpp project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Editor/terrain_editor_service_tests.cpp
git diff --cached --check
git commit -m "feat(editor): create terrain edit layers on first stroke"
```

### Task 6: Bind Hierarchy-selected Terrain entities and simplify terminology

**Files:**
- Modify: `project/src/editor/Panels/Terrain/TerrainModePanel.h`
- Modify: `project/src/editor/Panels/Terrain/TerrainModePanel.cpp`
- Modify: `project/src/editor/Panels/Terrain/TerrainModeState.h`
- Modify: `project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp`
- Modify: `project/src/editor/App/PanelBootstrapper.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Write RED selection and UI-contract tests**

Assert the panel constructor receives `SceneService*` and `SelectionService*`; a single Entity with a valid TerrainComponent resolves its `asset_path` through `AssetDatabaseService` and submits `SelectAsset`; multi-select/non-Terrain/invalid path do not replace the session; opening the panel after selection performs the same sync. Source contract rejects direct container load and Graphics/backend includes.

- [ ] **Step 2: Verify RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain mode binds a selected Terrain entity*"
.\RunTests.bat Debug --test-case="Terrain mode distinguishes edit layers and material slots*"
```

Expected: FAIL because the panel handles only `EditorSelectionKind::Asset` and still exposes blend-mode wording.

- [ ] **Step 3: Inject and use Scene/Selection services**

Add two non-owning dependencies to `TerrainModePanel`. Implement `TryBindSelection(const EditorSelection&, const std::vector<EditorSelection>&)`:

```cpp
if (selections.size() != 1u || selection.eKind != EditorSelectionKind::Entity)
    return false;
const AshEngine::Entity entity = _pSceneService->FindEntity(selection.uId);
if (!entity.is_valid() || !entity.has_terrain_component())
    return false;
const std::string& path = entity.get_terrain_component().asset_path;
const AshEngine::AssetInfo* asset = _pAssetDatabaseService->FindByPath(path);
if (!asset || asset->type != AshEngine::AssetType::Terrain)
    return false;
return SubmitSelectAsset(asset->id);
```

Reuse the current selection event for live changes and call the same helper from the first open `OnGui` using `SelectionService::GetSelection/GetSelections`. Existing service replacement gates surface dirty/conflict/pending diagnostics; the panel never discards state.

- [ ] **Step 4: Update bootstrap and UI terminology**

Pass `refContext.pSceneService` and `refContext.pSelectionService` from `PanelBootstrapper`. Remove ordinary Additive/Alpha controls. Use Chinese/English labels `地形编辑层 / Edit Layers` for the stack and `材质槽 / Material Slots` for fixed lanes. A layerless ready session keeps Sculpt/Paint controls enabled and explains that the first effective dab creates `Edit Layer`.

- [ ] **Step 5: Run Editor contract tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="Terrain mode*"
git add project/src/editor/Panels/Terrain/TerrainModePanel.h project/src/editor/Panels/Terrain/TerrainModePanel.cpp project/src/editor/Panels/Terrain/TerrainModeState.h project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp project/src/editor/App/PanelBootstrapper.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): open terrain authoring from hierarchy"
```

### Task 7: Update specs and run automated plus human verification

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/specs/modules/editor.md`
- Modify: `docs/sdd/SDD-2026-07-15-terrain-interactive-authoring-workflow.md`

- [ ] **Step 1: Document the delivered data and interaction contracts**

Record affine math, v1/v2 compatibility, generic edit-layer/material-slot terminology, 80 ms wall-clock preview, one-history transaction, automatic layer creation, Hierarchy binding and fail-closed replacement. Mark SDD `Implementing` until every gate and user signature passes.

- [ ] **Step 2: Run all CPU/build gates**

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

Expected: all exit 0; no new architecture legacy or validation-plan gap.

- [ ] **Step 3: Coordinate and run dual-backend automated gates**

After explicit CPU/GPU window release and four-config byte snapshots:

```powershell
.\run.bat all Debug --smoke-test-seconds=120
.\RunRenderGate.bat
.\RunPerfGate.bat -Profile Standard
```

Run a bounded Terrain editing/publication sequence under Vulkan validation and DX12 debug layer/GPU validation. Stop on error/critical, validation, device lost, assert, readiness failure, RenderGate FAIL or PerfGate FAIL. Do not bless.

- [ ] **Step 4: Require user-performed manual signature**

Launch one visible Vulkan Editor, then one visible DX12 Editor only after the user completes the first. Provide a Chinese checklist and require the user—not the agent—to sign each item:

```text
1. Hierarchy 选择 Terrain → 打开地形面板即可落笔。
2. 无编辑层资产第一次有效落笔自动出现“编辑层”，未落笔不变脏。
3. Raise/Lower/Smooth/Flatten/Noise/Paint/Erase 拖动时约 80–100 ms 可见更新。
4. 每次完整拖动只撤销一次、重做一次；取消/失焦无残留。
5. 编辑层排序/隐藏/锁定/强度正常；材质槽固定 8 个且概念不混淆。
6. 保存、重载 v2 正常；载入 v1 后画面不变且仅保存时升级。
```

- [ ] **Step 5: Restore, record evidence, mark Done and commit docs**

Restore all four config files byte-for-byte, verify effective roots zero and fresh logs clean, append automated report paths plus the user’s Vulkan/DX12 signature to the SDD, set Status `Done`, then:

```powershell
git add README.md docs/specs/features/terrain.md docs/specs/modules/asset.md docs/specs/modules/editor.md docs/sdd/SDD-2026-07-15-terrain-interactive-authoring-workflow.md
git diff --cached --check
git commit -m "docs(terrain): record interactive authoring closure"
```

### Task 8: Keep Flatten stroke-global and deactivate tools with the Terrain context

**Files:**
- Modify: `project/src/engine/Function/Asset/TerrainBrush.h`
- Modify: `project/src/engine/Function/Asset/TerrainBrush.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/editor/Core/TerrainViewportInputRouter.h`
- Modify: `project/src/editor/Core/TerrainViewportInputRouter.cpp`
- Modify: `project/src/editor/Core/PanelDeps/ViewportPanelDeps.h`
- Modify: `project/src/editor/Panels/ViewportPanelTerrainInteraction.h`
- Modify: `project/src/editor/Panels/ViewportPanelTerrainInteraction.cpp`
- Modify: `project/src/editor/Panels/ViewportPanelInteraction.cpp`
- Modify: `project/src/editor/App/PanelBootstrapper.h`
- Modify: `project/src/editor/App/PanelBootstrapper.cpp`
- Modify: `project/src/editor/App/EditorApplicationImpl.cpp`
- Modify: `project/src/tests/Terrain/terrain_brush_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_viewport_interaction_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_service_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`
- Modify: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/editor.md`

- [ ] **Step 1: Write the Flatten cross-batch RED**

Add a test with frozen heights `H(P0)=10` and `H(P1)=30`, hard radius `0.25`, strength `1`, and spacing larger than the segment. Express the wished-for stroke state explicitly:

```cpp
AshEngine::TerrainBrushStrokeTargetState target_state{};
REQUIRE(AshEngine::apply_resampled_terrain_brush_dabs(
    working_set, flatten, metric, frozen_layers, target_state,
    { TerrainStrokeSample{ { 1.0f, 1.0f }, 1.0f } }, patches, dirty));
REQUIRE(AshEngine::apply_resampled_terrain_brush_dabs(
    working_set, flatten, metric, frozen_layers, target_state,
    { TerrainStrokeSample{ { 3.0f, 1.0f }, 1.0f } }, patches, dirty));
REQUIRE(patches.size() == 1u);
const auto second_after = DecodePatchBytes(patches.front(), true);
CHECK(ReadFloatLe(second_after, 0u) == doctest::Approx(0.0f));
CHECK(ReadFloatLe(second_after, 1u) == doctest::Approx(10.0f));
```

Run:

```powershell
.\RunTests.bat Debug --test-case="Terrain Flatten keeps one target across preview batches"
```

Expected RED: compile failure because `TerrainBrushStrokeTargetState` and the stateful overload do not exist. This proves the API cannot currently preserve a stroke-global target.

- [ ] **Step 2: Write the viewport-context RED**

Extend `TerrainViewportRouteInput` with a fail-closed `authoring_context_active` field and add two cases:

```cpp
input.mode = AshEditor::TerrainEditorMode::Sculpt;
input.authoring_context_active = false;
input.left_pressed = true;
input.left_down = true;
auto route = AshEditor::route_terrain_viewport_input(input);
CHECK(route.send_gizmo);
CHECK_FALSE(route.consume_mouse_left);
CHECK_FALSE(route.begin_stroke);

input.stroke_active = true;
route = AshEditor::route_terrain_viewport_input(input);
CHECK(route.cancel_stroke);
CHECK(route.consume_mouse_left);
CHECK_FALSE(route.send_gizmo);
```

Add source/integration contracts requiring `ViewportPanelTerrainInteraction` to read `EditorSessionStateService::IsPanelOpen(EditorPanelIds::TerrainMode, false)`, accept exactly one matching Entity or Terrain asset selection, and preserve the loaded `TerrainEditorService` working set on mismatch.

Run:

```powershell
.\RunTests.bat Debug --test-case="Terrain viewport router deactivates with its editing context"
.\RunTests.bat Debug --test-case="Terrain viewport authoring requires an open panel and matching selection"
```

Expected RED: the route input has no context flag and viewport dependencies do not expose session panel state.

- [ ] **Step 3: Implement one atomic Flatten target state**

In `TerrainBrush.h` add:

```cpp
struct TerrainBrushStrokeTargetState
{
    std::optional<float> flatten_height{};
};
```

Pass it by non-const reference to `apply_resampled_terrain_brush_dabs`. For Flatten, copy the optional into a local candidate, sample frozen through-selected height at the first non-empty batch only when the candidate is empty, validate it is finite, and commit the candidate back to the state only after the brush mutation succeeds. `apply_terrain_brush_stroke` creates one local target state. `TerrainEditorService::ActiveStroke` stores the state and passes the same instance to every 80 ms batch. Non-Flatten tools do not read or change it.

- [ ] **Step 4: Derive viewport ownership without unloading the session**

Add `EditorSessionStateService* pSessionStateService` to `ViewportPanelDeps` and `PanelBootstrapContext`, wire it from `EditorApplicationImpl`, and use the cached panel/selection state in `ViewportPanelTerrainInteraction`:

```cpp
const bool panelOpen = deps.pSessionStateService &&
    deps.pSessionStateService->IsPanelOpen(EditorPanelIds::TerrainMode, false);
const bool matchingSelection = ResolveExactlyOneSelectedTerrainAsset(deps) ==
    deps.pTerrainEditorService->GetSelectedAssetId();
const bool contextActive = panelOpen && matchingSelection;
```

Only `contextActive && (Sculpt || Paint)` may query Terrain, show the brush overlay, consume LMB or disable W/E/R. Feed `contextActive` to the router; when it becomes false during an active stroke, reuse `CancelStroke`, clear the preview, and retain the mouse latch until physical release. Do not call `SelectAsset(0)`, close the working set, clear dirty/history, reset the active tab, or discard drafts.

- [ ] **Step 5: Run focused GREEN and regression tests**

```powershell
.\RunTests.bat Debug --test-case="Terrain Flatten keeps one target across preview batches"
.\RunTests.bat Debug --test-case="Terrain viewport*"
.\RunTests.bat Debug --test-case="Terrain editor*stroke*"
.\RunTests.bat Debug --test-case="Terrain mode*"
```

Expected: fixed target matches one-shot Flatten; panel close and selection mismatch return gizmo/selection input, active mismatch cancels one whole stroke, and the authoring session remains loaded and dirty.

- [ ] **Step 6: Update specs and repeat closure gates**

Record the stroke-global Flatten target and the panel/selection ownership predicate in the Terrain and Editor specs. Then run Debug/Release full tests, Editor/Sandbox Debug/Release builds, ArchGate, AIDevDoctor, four-combination readiness, non-bless RenderGate and Standard PerfGate. Restore the four runtime configs byte-for-byte, scan only fresh logs, and require the user to repeat Vulkan then DX12 manual checks for Flatten, panel close, non-Terrain selection, active-stroke cancellation and the already-passing near/far live preview.

## Plan self-review

- Spec coverage: affine generic layers, exact v1 migration, Paint/Erase coexistence, wall-clock incremental preview, single history, rollback/quarantine, auto-layer, Hierarchy binding, terminology, dual-backend and human verification map to Tasks 1–7; stroke-global Flatten and panel/selection tool deactivation map to Task 8.
- Scope boundary: no GPU brush, Graphics/RHI API, infinite material layers, collaboration, baseline change or frame-count throttle is included.
- Type consistency: affine `scales/biases`, resumable sampler, aggregate patches, optional auto-layer patch, `TerrainBrushStrokeTargetState`, authoring-context route flag and session-state panel dependency are introduced before use.
- Placeholder scan: every implementation task includes explicit RED, implementation contract, GREEN and selective commit; no TBD/TODO or deferred error-handling placeholders remain.
