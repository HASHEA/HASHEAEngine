# Terrain Phase 3 Editor Authoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver an Editor Terrain Mode that creates/imports, sculpts, paints, manages non-destructive layers, saves/reloads, resolves external conflicts, and provides one-command-per-stroke undo/redo without allowing Editor UI or viewport code to mutate Terrain assets directly.

**Architecture:** `TerrainEditorService` is the sole mutable authoring-session owner and calls Phase 1 asset/query APIs; panels and viewport code submit immutable user intents. `TerrainStrokeCommand` stores asset/layer/tile patches, viewport arbitration gives an active Terrain stroke priority over gizmo/selection while preserving camera controls, and brush previews are world-space `SceneOverlayLine` batches sent through the Function presentation facade.

**Tech Stack:** C++17, UIContext, EditorPanel/PanelManager, Inspector component-editor registry, EditorCommand/UndoRedoService, immutable Terrain snapshots, asynchronous asset jobs, SceneQuery Terrain APIs, ScenePresentation overlay facade, doctest, Premake5, PowerShell gates.

---

## Authoritative inputs and prerequisites

- Approved design: `docs/sdd/SDD-2026-07-13-terrain-system.md`
- Master sequence: `docs/superpowers/plans/2026-07-13-terrain-system-master.md`
- Required completed phases: Phase 1 asset/query core and Phase 2 Scene/render closure.
- Editor contract: `docs/specs/modules/editor.md`
- Scene/asset contracts: `docs/specs/modules/scene.md`, `docs/specs/modules/asset.md`

Phase 1/2 provide the exact contracts `TerrainAssetId`, 16-byte `TerrainLayerId`, `TerrainAssetSnapshot`, `TerrainEditPatch`, `TerrainBrushParameters`, `TerrainQueryStatus`, `query_height`, `query_normal`, `ray_cast_terrain`, `prefetch_query_region`, container/import/export functions, async AssetDatabase load, and Scene v6 `TerrainComponent`. This phase uses those names and shapes directly; it does not duplicate brush math, layer composition, container writing, or Terrain ray intersection inside Editor.

### 2026-07-14 implementation reconciliation

The completed Phase 1/2 code is the implementation source of truth where this pre-written plan used prospective names:

- `AssetDatabase` provides synchronous and asynchronous Terrain **load**, but container save/optimize and PNG/RAW/EXR import/export are synchronous Engine functions. `TerrainEditorService` therefore owns background jobs over immutable input copies and polls their futures on the Editor thread; panels never block and Engine APIs are not renamed merely to match the plan text.
- The actual composition boundary is `make_terrain_working_set` -> `compose_terrain_components` -> `publish_terrain_working_set`. Every editor mutation composes and publishes the complete current dirty-coordinate set before advertising a ready generation.
- Phase 3 coalesces mutations into one latest-generation composition request and executes that request from `TerrainEditorService::Update()` on the Editor thread. Capturing the mutable 8193 x 8193 working set for a background job would either race later strokes or copy roughly 128 MiB of Base height data per request; safe asynchronous composition therefore remains a Phase 4 COW/immutable-capture task. Phase 3 still verifies asset id, operation serial, source stroke sequence, content generation, and the exact complete dirty set before publication, and adds no whole-working-set copy beyond the already-approved Phase 1 snapshot publication boundary.
- The Phase 2 Scene adapter already converts a world-space Terrain ray and preserves the hit's terrain-local coordinate in `TerrainRayHit::local_sample` while returning world position/normal. The later Task 7 viewport adapter owns query-to-intent conversion and supplies that local sample plus the positive non-uniform metric; Task 4 freezes and forwards those values without duplicating transform math inside `TerrainEditorService`. `TerrainEditorIntent::world_position` remains reserved for the preview/viewport wiring in Tasks 7-8.
- The actual layer metadata currently has stable id, name, visibility, strength, blend mode, and sparse height/weight blocks, but no persisted lock bit and no public layer-stack mutation API. Task 5 must first add a Function-owned `TerrainLayerStack` API plus backward-compatible container metadata for the lock state; Editor commands call that API and do not mutate layer vectors themselves.
- `SceneOverlayLine`/`SceneOverlayBatchDesc` and `ScenePresentationSubsystem::submit_scene_overlay` are the existing overlay facade. Phase 3 reuses them without changing Graphics or RenderGraph.
- The execution mode is **inline task-by-task** in the current Terrain worktree. Each task keeps RED/GREEN evidence and a focused commit; no subagent execution is used, and nothing is pushed without an explicit user request.

Editor code may include Function headers but must not include `Graphics/`, Vulkan, DirectX 12, RenderGraph, or backend headers. `TerrainEditorService` owns every mutable session; `TerrainModePanel`, Inspector, and `ViewportPanel` may only call its intent/query methods.

## File responsibility map

- `project/src/editor/Services/TerrainEditorService.h/.cpp`: sole session owner, intent queue, stroke sequencing, async compose/save/load coordination, dirty content generation, conflict state, and immutable preview state.
- `project/src/editor/Core/TerrainEditorSessionCore.h/.cpp`: UI-free deterministic session state, intent reduction, content-generation/sequence arbitration, and preview values used by the service and tests.
- `project/src/editor/Core/TerrainCommands.h/.cpp`: stroke patch and layer-stack commands; no UI drawing or duplicate patch replay.
- `project/src/editor/Core/IEditorCommandExecutor.h`, `Services/UndoRedoService.h/.cpp`: explicit already-executed command recording used after an Engine brush transaction; normal commands keep execute-before-record behavior.
- `project/src/editor/Panels/Inspector/TerrainComponentEditor.h/.cpp`: TerrainComponent asset/visibility/shadow fields and enter-mode action.
- `project/src/editor/Panels/Inspector/InspectorPanelState.h`, `IInspectorComponentHost.h`, `InspectorPanelDrafts.cpp`, `InspectorComponentEditorRegistry.cpp`: Terrain draft plumbing only.
- `project/src/editor/Core/EntityCommands.h/.cpp`: add/edit/remove TerrainComponent command.
- `project/src/editor/Panels/Terrain/TerrainModeState.h`: selected tab/tool/layer and brush UI values.
- `project/src/editor/Panels/Terrain/TerrainModePanel.h/.cpp`: Manage/Sculpt/Paint/Layers UI and immutable intents.
- `project/src/editor/Panels/Terrain/TerrainModeWidgets.h/.cpp`: reusable brush/layer/status controls using UIContext.
- `project/src/editor/Services/TerrainBrushOverlayRenderer.h/.cpp`: convert immutable preview state to world-space overlay lines and submit them.
- `project/src/editor/Core/PanelDeps/ViewportPanelDeps.h`, `Panels/ViewportPanelInteraction.cpp`, `Panels/ViewportPanelCanvas.cpp`, `Panels/ViewportPanelToolbar.cpp`: Terrain input priority and presentation hookup.
- `project/src/editor/App/EditorApplicationImpl.h/.cpp`, `App/PanelBootstrapper.h/.cpp`: service lifecycle and panel dependency injection.
- `project/src/editor/App/SceneWorkflowCoordinator.cpp`, `App/EditorActionCoordinator.cpp`: close/reload/save ordering and history reset.
- `project/src/editor/Core/EditorIds.h`, `Shell/DockLayoutController.cpp`: stable panel id/title and default right-side dock.
- `project/src/tests/Editor/terrain_editor_service_tests.cpp`: real session/service state tests against Terrain production sources.
- `project/src/tests/Editor/terrain_commands_tests.cpp`: real stroke/layer command tests against a small Phase 1 Terrain asset.
- `project/src/tests/Editor/terrain_editor_contract_tests.cpp`: UI/bootstrap/architecture source-level assertions.
- `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`: Phase 1 authoring behavior exercised through Engine-linked public APIs.
- `project/src/tests/premake5.lua`: exact Terrain-only production source additions for Editor core tests.
- `product/assets/scenes/Terrain.scene.json`: Phase 2 deterministic Editor fixture.

When a real Editor-core test needs a production translation unit, append only the exact Terrain source path to `project/src/tests/premake5.lua`. Preserve the existing Editor include directory and the existing `EditorGizmoMath.cpp`/`EditorGizmoViewport.cpp` entries. Stage this mixed-owner file by hunk, inspect `git diff --cached -- project/src/tests/premake5.lua`, and never assume ownership of the whole file.

### Task 0: Link only UI-free Terrain Editor production sources into Tests

**Files:**
- Create: `project/src/editor/Core/TerrainEditorSessionCore.h`
- Create: `project/src/editor/Core/TerrainEditorSessionCore.cpp`
- Create: `project/src/tests/Editor/terrain_editor_service_tests.cpp`
- Modify by Terrain-only hunk: `project/src/tests/premake5.lua`

- [ ] **Step 1: Write the failing core smoke test**

```cpp
TEST_CASE("Terrain editor session core starts without mutable asset state")
{
    AshEditor::TerrainEditorSessionCore core{};
    CHECK(core.GetAssetId() == 0);
    CHECK(core.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Outside);
    CHECK_FALSE(core.HasActiveStroke());
}
```

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor session core*"
```

Expected: compile failure for `TerrainEditorSessionCore`.

- [ ] **Step 3: Add the minimal UI-free core**

```cpp
using AshEngine::TerrainAssetId;
using AshEngine::TerrainBrushParameters;
using AshEngine::TerrainBrushTool;
using AshEngine::TerrainLayerId;

enum class TerrainEditorMode : uint8_t { Manage, Sculpt, Paint, Layers };

struct TerrainEditorPreviewState
{
    AshEngine::TerrainQueryStatus query_status = AshEngine::TerrainQueryStatus::Outside;
    glm::vec3 center_ws{ 0.0f };
    glm::vec3 normal_ws{ 0.0f, 1.0f, 0.0f };
    float radius = 1.0f;
    bool layer_locked = false;
    bool stroke_active = false;
};

class TerrainEditorSessionCore final
{
public:
    TerrainAssetId GetAssetId() const { return _assetId; }
    const TerrainEditorPreviewState& GetPreviewState() const { return _preview; }
    bool HasActiveStroke() const { return _activeSequence != 0; }
private:
    TerrainAssetId _assetId = 0;
    uint64_t _activeSequence = 0;
    TerrainEditorPreviewState _preview{};
};
```

This core may depend on Function Terrain values but not `UIContext`, panels, ImGui, Graphics, or platform windows.

- [ ] **Step 4: Add one exact production source line to Tests**

Append this entry inside the existing `files` block without changing existing gizmo entries:

```lua
"%{wks.location}/project/src/editor/Core/TerrainEditorSessionCore.cpp",
```

- [ ] **Step 5: Run the focused test GREEN**

```powershell
.\generate_vs2022.bat
.\RunTests.bat Debug --test-case="Terrain editor session core*"
```

Expected: generation exits 0 and the smoke test passes.

- [ ] **Step 6: Stage only the Terrain hunk and commit**

```powershell
git add project/src/editor/Core/TerrainEditorSessionCore.h project/src/editor/Core/TerrainEditorSessionCore.cpp project/src/tests/Editor/terrain_editor_service_tests.cpp
git diff -- project/src/tests/premake5.lua
git add -p project/src/tests/premake5.lua
git diff --cached -- project/src/tests/premake5.lua
git diff --cached --check
git commit -m "test(editor): add terrain session core harness"
```

Expected: cached premake diff contains exactly one Terrain source line and preserves the Editor include plus both gizmo source lines.

### Task 1: Add TerrainComponent Inspector and undoable component command

**Files:**
- Create: `project/src/editor/Panels/Inspector/TerrainComponentEditor.h`
- Create: `project/src/editor/Panels/Inspector/TerrainComponentEditor.cpp`
- Create: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`
- Modify: `project/src/editor/Panels/Inspector/InspectorComponentEditorRegistry.cpp`
- Modify: `project/src/editor/Panels/Inspector/IInspectorComponentHost.h`
- Modify: `project/src/editor/Panels/Inspector/InspectorPanelState.h`
- Modify: `project/src/editor/Panels/Inspector/InspectorPanelDrafts.cpp`
- Modify: `project/src/editor/Panels/Inspector/InspectorPanelSupport.h`
- Modify: `project/src/editor/Panels/Inspector/InspectorPanelSupport.cpp`
- Modify: `project/src/editor/Core/EditorComponentComparison.h`
- Modify: `project/src/editor/Core/EditorComponentComparison.cpp`
- Modify: `project/src/editor/Core/EntityCommands.h`
- Modify: `project/src/editor/Core/EntityCommands.cpp`
- Modify: `project/src/editor/Core/SceneSnapshotComponentUtils.cpp`

- [ ] **Step 1: Write the failing Inspector/component source-contract test**

```cpp
static std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static size_t CountText(std::string_view text, std::string_view needle)
{
    size_t count = 0;
    for (size_t offset = 0; (offset = text.find(needle, offset)) != std::string_view::npos; offset += needle.size())
        ++count;
    return count;
}

TEST_CASE("Terrain Inspector routes component changes through an EditorCommand")
{
    const std::string registry = ReadText("project/src/editor/Panels/Inspector/InspectorComponentEditorRegistry.cpp");
    const std::string editor = ReadText("project/src/editor/Panels/Inspector/TerrainComponentEditor.cpp");
    const std::string commands = ReadText("project/src/editor/Core/EntityCommands.cpp");
    CHECK(registry.find("TerrainComponentEditor") != std::string::npos);
    CHECK(editor.find("CommitTerrainDraft") != std::string::npos);
    CHECK(commands.find("SetTerrainComponentCommand::Execute") != std::string::npos);
    CHECK(commands.find("SetTerrainComponentCommand::Undo") != std::string::npos);
}
```

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain Inspector*"
```

Expected: assertions fail because the Terrain Inspector and command do not exist.

- [ ] **Step 3: Add Terrain draft plumbing**

Add `TerrainDraft { SceneEntityId uEntityId; optional<TerrainComponent> optOriginalValue; optional<TerrainComponent> optCurrentValue; }` to `InspectorPanelState`. Add `SyncTerrainDraft`, `ResetTerrainDraftToLive`, `ResetTerrainDraftToDefaults`, and `CommitTerrainDraft` to `IInspectorComponentHost`. `SyncTerrainDraft` copies live data into both optionals when the entity id changes, and refreshes both only while current still equals original. Live reset copies the entity value into both optionals; default reset changes only current to `TerrainComponent{}`. Commit rejects invalid/equal state, executes `SetTerrainComponentCommand(entity_id, original, current)`, advances original to current only on success, and restores current from original on failure.

- [ ] **Step 4: Add SetTerrainComponentCommand**

```cpp
class SetTerrainComponentCommand final : public EditorCommand
{
public:
    SetTerrainComponentCommand(
        SceneEntityId entityId,
        std::optional<AshEngine::TerrainComponent> before,
        std::optional<AshEngine::TerrainComponent> after);
    const char* GetLabel() const override;
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    bool TryMerge(const EditorCommand& subsequent) override;
};
```

Reject empty/equal edits, select the Terrain entity after execute/undo, and use generic add/remove component facades. Snapshot copy/delete/restore must include Terrain.

- [ ] **Step 5: Implement the Inspector editor**

Draw asset path, visible, casts shadow, receives shadow, and eight override paths through `UIContext`. Positive scale stays in the existing Transform section; when a selected Terrain has rotation or non-positive scale, display an error and disable commit rather than correcting silently.

- [ ] **Step 6: Register the editor and run GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain Inspector*"
.\build_editor.bat Debug
```

Expected: source-contract case passes; Editor links all new Inspector methods.

- [ ] **Step 7: Commit the Inspector closure**

```powershell
git add project/src/editor/Panels/Inspector/TerrainComponentEditor.h project/src/editor/Panels/Inspector/TerrainComponentEditor.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp project/src/editor/Panels/Inspector/InspectorComponentEditorRegistry.cpp project/src/editor/Panels/Inspector/IInspectorComponentHost.h project/src/editor/Panels/Inspector/InspectorPanelState.h project/src/editor/Panels/Inspector/InspectorPanelDrafts.cpp project/src/editor/Panels/Inspector/InspectorPanelSupport.h project/src/editor/Panels/Inspector/InspectorPanelSupport.cpp project/src/editor/Core/EditorComponentComparison.h project/src/editor/Core/EditorComponentComparison.cpp project/src/editor/Core/EntityCommands.h project/src/editor/Core/EntityCommands.cpp project/src/editor/Core/SceneSnapshotComponentUtils.cpp
git diff --cached --check
git commit -m "feat(editor): add terrain component inspector"
```

Expected: no Render/Graphics file staged.

### Task 2: Create TerrainEditorService as the only mutable owner

**Files:**
- Create: `project/src/editor/Services/TerrainEditorService.h`
- Create: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.h`
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_service_tests.cpp`
- Modify: `project/src/editor/App/EditorApplicationImpl.h`
- Modify: `project/src/editor/App/EditorApplicationImpl.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Write the intent ownership RED test**

```cpp
TEST_CASE("Terrain editor core accepts one selected asset and immutable intents")
{
    AshEditor::TerrainEditorSessionCore core{};
    AshEditor::TerrainEditorIntent select{};
    select.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
    select.asset_id = 17;
    CHECK(core.Reduce(select));
    CHECK(core.GetAssetId() == 17);
    CHECK(select.asset_id == 17);
}
```

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor core accepts*"
```

Expected: compile failure for `TerrainEditorIntent` or `Reduce`.

- [ ] **Step 3: Define immutable intents in the UI-free core**

```cpp
enum class TerrainLayerActionKind : uint8_t
{
    Add,
    Delete,
    Duplicate,
    Rename,
    Move,
    SetVisible,
    SetLocked,
    SetOpacity
};

struct TerrainLayerActionIntent
{
    TerrainLayerActionKind kind = TerrainLayerActionKind::Add;
    TerrainLayerId layer_id{};
    AshEngine::TerrainHeightBlendMode blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
    std::string name{};
    uint32_t destination_index = 0;
    float opacity = 1.0f;
    bool flag_value = false;
};

struct TerrainEditorIntent
{
    enum class Kind : uint8_t { SelectAsset, BeginStroke, AddStrokeSample, EndStroke, CancelStroke, LayerAction, Save, Reload, KeepLocal, SaveAs, Optimize, Import, Export };
    Kind kind = Kind::SelectAsset;
    TerrainAssetId asset_id = 0;
    TerrainLayerId layer_id{};
    uint64_t sequence = 0;
    glm::vec3 world_position{ 0.0f };
    AshEngine::TerrainStrokeSample stroke_sample{};
    AshEngine::TerrainBrushMetric brush_metric{};
    TerrainBrushParameters brush{};
    TerrainLayerActionIntent layer_action{};
    std::filesystem::path asset_path{};
    std::optional<AshEngine::TerrainHeightImportDesc> import_desc{};
    std::optional<AshEngine::TerrainHeightExportDesc> export_desc{};
};

```

Implement `TerrainEditorSessionCore::Reduce` as a deterministic state transition; it copies values from the const intent and never retains a pointer to panel state.

Add `bool Open(AshEngine::TerrainWorkingSet working_set)` and `const AshEngine::TerrainWorkingSet* GetWorkingSet() const`. `Open` takes the mutable working set by value and rejects asset id zero or invalid layout; no mutable working-set reference leaves the core.

- [ ] **Step 4: Define the service public surface**

```cpp
class TerrainEditorService final
{
public:
    bool Initialize(AshEngine::AssetDatabase& assets, IEditorCommandExecutor& commands);
    void Shutdown();
    void Update();
    bool SubmitIntent(const TerrainEditorIntent& intent);
    const TerrainEditorPreviewState& GetPreviewState() const;
    bool HasDirtyAssets() const;
    bool HasBlockingOperation() const;
};
```

The core owns selected ids plus sequence/content-generation reduction; the service owns the editable Phase 1 session and task handles. `TerrainEditorService` contains one core by value; panels never construct another production core or retain mutable Terrain layer/tile pointers.

- [ ] **Step 5: Wire lifecycle into EditorApplicationImpl**

Construct the service with other Editor services, initialize after AssetDatabase, call `Update` before panel update, call `Shutdown` before AssetDatabase/UndoRedo teardown, and store it as `std::unique_ptr<TerrainEditorService>`.

- [ ] **Step 6: Run contract/build GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor core accepts*"
.\build_editor.bat Debug
```

Expected: test and Editor build pass; `rg -n "Graphics/|Vulkan|DirectX12|RenderGraph" project/src/editor/Services/TerrainEditorService.*` returns no match.

- [ ] **Step 7: Commit service ownership**

```powershell
git add project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/editor/Core/TerrainEditorSessionCore.h project/src/editor/Core/TerrainEditorSessionCore.cpp project/src/tests/Editor/terrain_editor_service_tests.cpp project/src/editor/App/EditorApplicationImpl.h project/src/editor/App/EditorApplicationImpl.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): own terrain authoring sessions"
```

Expected: one lifecycle/ownership commit.

### Task 3: Add one-command-per-stroke undo and redo

**Files:**
- Create: `project/src/editor/Core/TerrainCommands.h`
- Create: `project/src/editor/Core/TerrainCommands.cpp`
- Create: `project/src/tests/Editor/terrain_commands_tests.cpp`
- Modify: `project/src/editor/Core/IEditorCommandExecutor.h`
- Modify: `project/src/editor/Core/EditorContext.h`
- Modify: `project/src/editor/Services/UndoRedoService.h`
- Modify: `project/src/editor/Services/UndoRedoService.cpp`
- Modify: `project/src/editor/App/EditorApplicationImpl.h`
- Modify: `project/src/editor/App/EditorApplicationImpl.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify by Terrain-only hunk: `project/src/tests/premake5.lua`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Add Engine-linked command replay RED tests**

```cpp
TEST_CASE("Terrain stroke command replays the Engine patch API by stable identity")
{
    AshEngine::TerrainWorkingSet pristine = TerrainTests::MakeSmallTerrainWorkingSet();
    AshEngine::TerrainWorkingSet edited = pristine;
    const auto before = TerrainTests::SnapshotBlockBytes(pristine, { 0, 0 });
    const std::vector<AshEngine::TerrainEditPatch> patches = TerrainTests::ApplyRaiseStroke(edited);
    CHECK_FALSE(patches.empty());
    TerrainEditorServiceHarness harness = MakeTerrainEditorServiceHarness(std::move(pristine));
    AshEditor::EditorContext context{};
    context.pTerrainEditorService = &harness.service;
    AshEditor::TerrainStrokeCommand command(
        harness.asset_id, harness.layer_id, 3, patches);
    CHECK(command.Execute(context));
    CHECK(harness.SnapshotBlock(0, 0) != before);
    CHECK(command.Undo(context));
    CHECK(harness.SnapshotBlock(0, 0) == before);
}
```

- [ ] **Step 2: Add an already-executed history RED test**

Start the harness in the brush-produced `edited` state, create a `TerrainStrokeCommand` from its patches, and call `IEditorCommandExecutor::RecordExecutedCommand`. Assert recording does not call `Execute` and therefore does not double-apply the stroke. Then assert Undo restores exact before bytes and Redo restores exact after bytes. Add an empty-patch case that records no history entry.

- [ ] **Step 3: Run the command tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke command*"
.\RunTests.bat Debug --test-case="Terrain already-executed command*"
```

Expected: command/history contracts fail because the already-executed path does not exist.

- [ ] **Step 4: Add a precise already-executed command path**

Extend `IEditorCommandExecutor` with:

```cpp
enum class EditorCommandRecordResult : uint8_t
{
    Recorded = 0,
    RolledBack,
    RollbackFailed
};

virtual EditorCommandRecordResult RecordExecutedCommand(
    std::unique_ptr<EditorCommand> upCommand) = 0;
```

Add `UndoRedoService::RecordExecuted(std::unique_ptr<EditorCommand>, EditorContext&)`. It never calls `EditorCommand::Execute`; it pre-reserves the standalone history storage, then clears redo, records one normal history state, applies post-execute selection, and publishes the same history/dirty notifications as a successful `Execute`. An already-executed command cannot enter an open transaction: delayed commit/cancel has no synchronous tri-state channel through which the mutation owner could prove compensation, so this path immediately calls the command's atomic `Undo`. Any storage/selection failure does the same; return `RolledBack` only when compensation succeeds and `RollbackFailed` otherwise. `EditorApplicationImpl::RecordExecutedCommand` forwards the tri-state result through this method with its owned context. Normal `ExecuteCommand` semantics remain unchanged, and every test double implementing the executor must opt into the new path explicitly.

- [ ] **Step 5: Define TerrainStrokeCommand over Engine replay**

```cpp
class TerrainStrokeCommand final : public EditorCommand
{
public:
    TerrainStrokeCommand(TerrainAssetId assetId, TerrainLayerId layerId, uint64_t sequence, std::vector<AshEngine::TerrainEditPatch> patches);
    const char* GetLabel() const override;
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
};
```

`Execute` calls `TerrainEditorService::ApplyStrokePatches(patches, AshEngine::TerrainEditPatchDirection::Redo)` and `Undo` calls the same service with `Undo`. The service delegates byte validation and atomic mutation to Phase 1 `apply_terrain_edit_patches`; Editor defines no second patch direction, codec, or replay implementation. Commands locate the authoring session by asset/layer identity, selection remains unchanged, and the command never stores an entire 8193 x 8193 image.

Add `TerrainEditorService* pTerrainEditorService` to `EditorContext`; `EditorApplicationImpl::WireServices` fills it. `TerrainStrokeCommand` fails without this pointer instead of falling back to direct asset mutation.

- [ ] **Step 6: Add production sources to the real Editor tests**

Append only the exact Terrain production lines required by the real command/service test to the existing Tests `files` block:

```lua
"%{wks.location}/project/src/editor/Core/TerrainCommands.cpp",
"%{wks.location}/project/src/editor/Services/TerrainEditorService.cpp",
```

- [ ] **Step 7: Make stroke completion emit exactly one recorded command**

On BeginStroke, allocate one sequence, reserve both forward and compensation generations, and freeze parameters/metric. AddStrokeSample only appends the raw terrain-local sample; it never pre-resamples or invokes a kernel. EndStroke calls Phase 1 `apply_terrain_brush_stroke` exactly once against the service-owned working set, constructs one already-executed `TerrainStrokeCommand`, installs the resulting full-dirty composition request, and then records exactly that command. An empty patch submits none. CancelStroke clears the raw path and submits none because no mutation occurred. Command-construction failure replays the retained patches with Engine Undo; history-recording failure uses `RecordExecuted`'s required Undo, whose higher-generation request replaces the forward request. A `RolledBack` result is accepted only when the higher rollback generation and matching pending dirty set are observable. `RollbackFailed`, an exception, or a malformed rollback result discards pending publication and quarantines the session until reload/asset replacement. Both rollback paths invalidate stale compose work, preserve monotonic generation, and never publish untracked logical edits.

- [ ] **Step 8: Add source-contract assertions and run GREEN**

```cpp
CHECK(CountText(serviceSource, "RecordExecutedCommand(") == 1);
CHECK(commandSource.find("TerrainEditPatchDirection::Undo") != std::string::npos);
CHECK(commandSource.find("TerrainEditPatchDirection::Redo") != std::string::npos);
CHECK(commandSource.find("decode_terrain_rle") == std::string::npos);
```

Run:

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke command*"
.\RunTests.bat Debug --test-case="Terrain already-executed command*"
.\build_editor.bat Debug
```

Expected: recording leaves applied bytes unchanged, Undo/Redo are byte-exact and generation-monotonic, and Editor links the command/executor changes.

- [ ] **Step 9: Stage the mixed premake hunk and commit stroke history**

```powershell
git add project/src/editor/Core/TerrainCommands.h project/src/editor/Core/TerrainCommands.cpp project/src/tests/Editor/terrain_commands_tests.cpp project/src/editor/Core/IEditorCommandExecutor.h project/src/editor/Core/EditorContext.h project/src/editor/Services/UndoRedoService.h project/src/editor/Services/UndoRedoService.cpp project/src/editor/App/EditorApplicationImpl.h project/src/editor/App/EditorApplicationImpl.cpp project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git add -p project/src/tests/premake5.lua
git diff --cached -- project/src/tests/premake5.lua
git diff --cached --check
git commit -m "feat(editor): add undoable terrain strokes"
```

Expected: no scene-wide image copy and no Editor-side patch decoder; cached premake diff adds exactly the required Terrain production sources and retains the pre-existing gizmo lines.

### Task 4: Route raw viewport strokes and publish generations in order

**Files:**
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_service_tests.cpp`

- [ ] **Step 1: Write raw-path routing RED tests**

```cpp
TEST_CASE("Terrain editor forwards one raw path to one Engine brush transaction")
{
    TerrainEditorServiceHarness harness = MakeTerrainEditorServiceHarness();
    REQUIRE(harness.BeginStroke(MakeReadyHit(), MakeBrushMetric(2.0f, 0.5f)));
    REQUIRE(harness.AddRawSample({ { 1.0f, 2.0f }, 0.5f }));
    REQUIRE(harness.AddRawSample({ { 3.0f, 4.0f }, 1.0f }));
    REQUIRE(harness.EndStroke());
    CHECK(harness.engine_brush_call_count == 1);
    CHECK(harness.recorded_command_count == 1);
}
```

Add Cancel/empty-stroke cases, invalid non-uniform scale/metric cases, incompatible tool/layer failure, and Pending/Outside/Failed query-state rejection. Phase 1 already owns frame-density and all kernel-value tests; do not duplicate their implementation in Editor.

- [ ] **Step 2: Add ordered compose/publication RED tests**

Start two successful mutations and complete their compose jobs out of order. Assert the service discards stale generations, publishes only the exact current full dirty set, clears dirty only after successful `publish_terrain_working_set`, and leaves dirty/generation/session bytes intact on compose or publication failure. Undo during a pending compose invalidates that job and schedules the Undo generation.

- [ ] **Step 3: Run the focused tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor forwards one raw path*"
.\RunTests.bat Debug --test-case="Terrain editor publishes generations*"
```

Expected: routing or ordered-publication assertions fail.

- [ ] **Step 4: Capture a valid world metric at BeginStroke**

Consume the Ready Scene adapter hit's preserved `TerrainRayHit::local_sample` through the Task 7 query-to-intent adapter; do not repeat the Phase 2 axis-aligned transform inside the authoring service. Preserve both positive non-uniform scale axes as `TerrainBrushMetric::world_meters_per_terrain_meter`; never collapse them to one scalar. Task 4 freezes the caller-provided brush parameters and metric for the stroke, and rejects BeginStroke unless the selected asset/layer/query state and metric are valid. Task 7 supplies real viewport hits; Task 4 tests the service boundary with explicit terrain-local samples.

- [ ] **Step 5: Forward raw samples without pre-resampling**

Store raw `TerrainStrokeSample` points and pressure in arrival order. Do not clamp invalid brush values, call `resample_terrain_stroke`, apply one dab per UI frame, or mutate on AddStrokeSample. On EndStroke call `apply_terrain_brush_stroke(working_set, frozen_params, frozen_metric, raw_path, ...)` exactly once; Engine remains the second-line validator for stale/scripted intents.

- [ ] **Step 6: Enforce ordered deferred publication**

Each deferred request captures asset id, stroke sequence, operation serial, content generation, and the exact full dirty coordinate set for that generation. A newer mutation replaces the older request before `Update()` performs composition. Publish only when all captured identity fields still equal the current session; any stale request is discarded without clearing dirty state. Publication success uses the Phase 1 atomic API, updates the immutable preview snapshot, and only then advertises completion/readiness. True background completion ordering is deferred to Phase 4 because Phase 3 has no immutable/COW working-set capture.

- [ ] **Step 7: Run routing/publication GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain editor forwards one raw path*"
.\RunTests.bat Debug --test-case="Terrain editor publishes generations*"
.\RunTests.bat Debug --test-case="Terrain brush*"
```

Expected: Editor invokes one Engine brush transaction per completed stroke, stale jobs never publish, and the authoritative Phase 1 brush suite remains green.

- [ ] **Step 8: Commit routing and sequencing**

```powershell
git add project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp project/src/tests/Editor/terrain_editor_service_tests.cpp
git diff --cached --check
git commit -m "feat(editor): route terrain stroke transactions"
```

Expected: one routing/sequencing commit with no brush kernel or patch codec duplicated in Editor.

### Task 5: Add undoable non-destructive layer management

**Files:**
- Create: `project/src/engine/Function/Asset/TerrainLayerStack.h`
- Create: `project/src/engine/Function/Asset/TerrainLayerStack.cpp`
- Create: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainData.h`
- Modify: `project/src/engine/Function/Asset/TerrainContainerFormat.h`
- Modify: `project/src/engine/Function/Asset/TerrainContainer.cpp`
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.h`
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.cpp`
- Modify: `project/src/editor/Core/TerrainCommands.h`
- Modify: `project/src/editor/Core/TerrainCommands.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/tests/Terrain/terrain_container_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_commands_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_service_tests.cpp`
- Modify: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/editor.md`
- Modify: `docs/superpowers/plans/2026-07-13-terrain-phase-3-editor-authoring.md`

- [x] **Step 1: Write layer-stack RED tests**

```cpp
TEST_CASE("Terrain layer commands preserve ids order occupancy and undo")
{
	TerrainWorkingSet working_set = MakeSmallTerrainWorkingSet();
	TerrainLayerStackPatch patch{};
	REQUIRE(apply_terrain_layer_stack_edit(working_set, edit, patch, dirty, &error));
	CHECK(patch.has_change());
	REQUIRE(apply_terrain_layer_stack_patch(
		working_set, patch, TerrainEditPatchDirection::Undo, dirty, &error));
}
```

Add delete/restore, duplicate-new-ID, rename, move, visibility, opacity, lock, idempotent `has_change()==false`, affected-occupancy dirty-union, stale replay, malformed patch and atomic failure cases. Editor tests cover direct command replay, stable selection, lock rejection, and history rollback/quarantine.

- [x] **Step 2: Run layer tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain layer commands*"
```

Observed: build failed on missing `TerrainLayerStack.h`, `TerrainLayerCommand`, and selected-layer API before production implementation.

- [x] **Step 3: Complete Function layer-stack mutations and persistence**

Make add/delete/duplicate/rename/reorder/visibility/opacity/lock return reversible patches. Recomposition dirties only the occupancy union of affected layers; deleting and duplicating retain only the affected layer's sparse blocks in the patch.

Implement those operations in `TerrainLayerStack.*`, including stable 16-byte id allocation and duplicate-id rejection. Add `locked` to `TerrainEditLayer` and persist it through a backward-compatible container metadata revision: existing version-1 assets load with `locked=false`, newly saved metadata round-trips the bit, and unsupported future versions still fail closed. Add container tests for old-unlocked input and new locked round-trip before Editor wiring.

Use these Function APIs with stable-id lookup and no index persistence:

```cpp
bool apply_terrain_layer_stack_edit(
	TerrainWorkingSet&, const TerrainLayerStackEdit&, TerrainLayerStackPatch&,
	std::vector<TerrainComponentCoord>&, std::string*);
bool apply_terrain_layer_stack_patch(
	TerrainWorkingSet&, const TerrainLayerStackPatch&, TerrainEditPatchDirection,
	std::vector<TerrainComponentCoord>&, std::string*);
```

This reversible patch contract supersedes the early `CaptureLayerStack`/`RestoreLayerStack` sketch. Topology patches retain one affected layer; metadata and order patches retain no sparse blocks, so history never deep-copies the whole 8193² layer stack. A successful idempotent edit returns `patch.has_change()==false` and must not enter history.

- [x] **Step 4: Add Editor layer commands**

Define `TerrainLayerCommand` around one validated Function patch plus stable selected-before/selected-after IDs. Execute/Undo replay by asset id and sequence; no command depends on a vector index or guesses selection from an insertion slot.

- [x] **Step 5: Route LayerAction intents through command execution**

`TerrainEditorService::SubmitIntent(LayerAction)` validates the selected asset, performs exactly one Function mutation, schedules its generation, records exactly one already-executed layer command, and updates selected layer id from the stable target order. Empty patches produce no history or publication. `SelectLayer` is a non-history stable-ID intent; BeginStroke requires intent, brush, and current selection IDs to match. Locked layers reject stroke begin and expose `layer_locked=true` in preview state. Record failure uses the same verified generation/selection/pending rollback and quarantine contract as stroke.

- [x] **Step 6: Run layer tests and Task 5 gates GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain layer commands*"
.\RunTests.bat Debug --test-case="Terrain layer command*"
.\RunTests.bat Debug --test-case="Terrain editor *layer*"
.\RunTests.bat Debug --test-case="Terrain container *layer*"
.\RunTests.bat Debug
.\RunTests.bat Release
.\RunArchGate.bat
.\generate_vs2022.bat
.\build_editor.bat Debug
.\build_sandbox.bat Debug
.\build_editor.bat Release
.\build_sandbox.bat Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
.\run.bat all Debug --smoke-test-seconds=120
```

Result: Function/editor layer filters pass, including exact selection and rollback quarantine contracts. Full Debug/Release tests pass (287/287, 21068 assertions), ArchGate and AIDevDoctor pass, fresh generation plus Editor/Sandbox Debug/Release builds pass, and all four readiness combinations exit successfully from the scene-ready signal with clean Sandbox shutdown and zero fresh-log rejection hits. Runtime configuration bytes are restored exactly after the matrix.

- [x] **Step 7: Commit layer management**

```powershell
git add project/src/engine/Function/Asset/TerrainLayerStack.h project/src/engine/Function/Asset/TerrainLayerStack.cpp project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainContainerFormat.h project/src/engine/Function/Asset/TerrainContainer.cpp
git add project/src/editor/Core/TerrainEditorSessionCore.h project/src/editor/Core/TerrainEditorSessionCore.cpp project/src/editor/Core/TerrainCommands.h project/src/editor/Core/TerrainCommands.cpp project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp
git add project/src/tests/Terrain/terrain_authoring_session_tests.cpp project/src/tests/Terrain/terrain_container_tests.cpp project/src/tests/Editor/terrain_commands_tests.cpp project/src/tests/Editor/terrain_editor_service_tests.cpp docs/specs/features/terrain.md docs/specs/modules/editor.md docs/superpowers/plans/2026-07-13-terrain-phase-3-editor-authoring.md
git diff --cached --check
git commit -m "feat(editor): manage terrain edit layers"
```

Expected: stable ids, not vector positions, identify commands.

### Task 6: Build Terrain Mode panel and UIContext widgets

**Files:**
- Create: `project/src/editor/Panels/Terrain/TerrainModeState.h`
- Create: `project/src/editor/Panels/Terrain/TerrainModeWidgets.h`
- Create: `project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp`
- Create: `project/src/editor/Panels/Terrain/TerrainModePanel.h`
- Create: `project/src/editor/Panels/Terrain/TerrainModePanel.cpp`
- Modify: `project/src/editor/Core/EditorIds.h`
- Modify: `project/src/editor/App/PanelBootstrapper.h`
- Modify: `project/src/editor/App/PanelBootstrapper.cpp`
- Modify: `project/src/editor/App/EditorApplicationImpl.cpp`
- Modify: `project/src/editor/Shell/DockLayoutController.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Write panel/bootstrap RED contracts**

```cpp
TEST_CASE("Terrain Mode is a UIContext panel backed by TerrainEditorService")
{
    const std::string panel = ReadText("project/src/editor/Panels/Terrain/TerrainModePanel.cpp");
    const std::string bootstrap = ReadText("project/src/editor/App/PanelBootstrapper.cpp");
    CHECK(panel.find("begin_tab_bar") != std::string::npos);
    CHECK(panel.find("SubmitIntent") != std::string::npos);
    CHECK(panel.find("ImGui::") == std::string::npos);
    CHECK(bootstrap.find("CreatePanel<TerrainModePanel>") != std::string::npos);
}
```

- [ ] **Step 2: Run focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain Mode*"
```

Expected: panel/bootstrap assertions fail.

- [ ] **Step 3: Add stable panel ids and dependency injection**

Add `EditorPanelIds::TerrainMode = "terrain_mode"` and `EditorWindowTitles::TerrainMode = "Terrain"`. Pass `TerrainEditorService*` through `PanelBootstrapContext`, create one panel, bind its event bus, set it closed by default, and dock its title on the Inspector node.

- [ ] **Step 4: Implement Manage tab**

Draw flat create, PNG/RAW/EXR import, final/base/layer export, Save, Save As, Reload, Optimize, progress, `content_generation`, `residency_revision`, and error state. RAW fields include format/endian/axis; EXR includes channel; size mismatch requires explicit crop or Catmull-Rom resample selection.

- [ ] **Step 5: Implement Sculpt and Paint tabs**

Sculpt offers Raise, Lower, Smooth, Flatten, Noise. Paint offers Paint, Erase, and exactly eight material layers. Common controls expose radius, strength, falloff, spacing, and deterministic seed; every control changes `TerrainModeState` then submits an immutable selection/config intent.

- [ ] **Step 6: Implement Layers tab**

Draw add/delete/duplicate/rename/reorder/hide/lock/opacity controls keyed by stable layer id. Buttons submit LayerAction intents and never mutate a `TerrainAssetSnapshot` directly.

- [ ] **Step 7: Run panel contract/build GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain Mode*"
.\build_editor.bat Debug
```

Expected: test passes, Editor links panel/widgets, and `rg -n "ImGui::|Graphics/|Vulkan|DirectX12" project/src/editor/Panels/Terrain` returns no match.

- [ ] **Step 8: Commit Terrain Mode UI**

```powershell
git add project/src/editor/Panels/Terrain/TerrainModeState.h project/src/editor/Panels/Terrain/TerrainModeWidgets.h project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp project/src/editor/Panels/Terrain/TerrainModePanel.h project/src/editor/Panels/Terrain/TerrainModePanel.cpp project/src/editor/Core/EditorIds.h project/src/editor/App/PanelBootstrapper.h project/src/editor/App/PanelBootstrapper.cpp project/src/editor/App/EditorApplicationImpl.cpp project/src/editor/Shell/DockLayoutController.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): add terrain authoring mode"
```

Expected: one UI-only intent submission commit.

### Task 7: Add viewport input arbitration

**Files:**
- Create: `project/src/editor/Core/TerrainViewportInputRouter.h`
- Create: `project/src/editor/Core/TerrainViewportInputRouter.cpp`
- Create: `project/src/tests/Editor/terrain_viewport_interaction_tests.cpp`
- Modify: `project/src/editor/Core/PanelDeps/ViewportPanelDeps.h`
- Modify: `project/src/editor/App/PanelBootstrapper.cpp`
- Modify: `project/src/editor/Panels/ViewportPanelInteraction.h`
- Modify: `project/src/editor/Panels/ViewportPanelInteraction.cpp`
- Modify: `project/src/editor/Panels/ViewportPanelInteractionSupport.cpp`
- Modify: `project/src/editor/Panels/ViewportPanelToolbar.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`
- Modify by Terrain-only hunk: `project/src/tests/premake5.lua`

- [ ] **Step 1: Write the real arbitration RED test**

```cpp
TEST_CASE("Terrain viewport router consumes only authoring mouse-left")
{
    AshEditor::TerrainViewportRouteInput input{};
    input.primary_scene_viewport = true;
    input.mode = AshEditor::TerrainEditorMode::Sculpt;
    input.query_status = AshEngine::TerrainQueryStatus::Ready;
    input.left_pressed = true;
    CHECK(AshEditor::route_terrain_viewport_input(input).consume_mouse_left);
    input.alt = true;
    CHECK_FALSE(AshEditor::route_terrain_viewport_input(input).consume_mouse_left);
    input.alt = false;
    input.right_down = true;
    CHECK_FALSE(AshEditor::route_terrain_viewport_input(input).consume_mouse_left);
}
```

- [ ] **Step 2: Run focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain viewport router*"
```

Expected: compile failure for `TerrainViewportRouteInput`.

- [ ] **Step 3: Implement the UI-free router and link it to Tests**

```cpp
struct TerrainViewportRouteResult
{
    bool send_camera = true;
    bool send_terrain = false;
    bool send_gizmo = true;
    bool send_selection = true;
    bool consume_mouse_left = false;
};
```

Sculpt/Paint on the primary Scene viewport sends Terrain only when no Alt/RMB/MMB camera gesture is active. A Ready left press/down/release consumes selection and gizmo; Manage/Layers/Game/aux routes preserve existing behavior. Append only this source line to Tests:

```lua
"%{wks.location}/project/src/editor/Core/TerrainViewportInputRouter.cpp",
```

- [ ] **Step 4: Pass TerrainEditorService through ViewportPanelDeps**

Add `TerrainEditorService* pTerrainEditorService` and populate it in `MakeViewportPanelDeps`. Game/aux viewports receive the pointer but do not author Terrain.

- [ ] **Step 5: Reuse the existing viewport ray builder**

Extract the local `TryBuildSceneInteractionRay` wrapper so Terrain interaction calls `EditorViewportCameraService::TryBuildViewportRay`; do not reproduce inverse projection math.

- [ ] **Step 6: Insert Terrain arbitration after camera update**

Only the primary Scene viewport with active Sculpt/Paint mode may call `UpdateTerrainInteraction`. Alt+LMB, RMB, MMB, mouse wheel, and camera focus remain camera-owned. A Ready, unmodified LMB stroke consumes mouse-left; Pending/Outside/Failed does not begin a stroke and cancels selection start for that press only when Terrain mode explicitly owns the tool.

- [ ] **Step 7: Skip gizmo/selection on a consumed stroke**

Pass `terrainInteraction.bConsumesMouseLeft || gizmoInteraction.bConsumesMouseLeft` into selection. Do not invoke gizmo update while a Terrain stroke is active. Disable W/E/R gizmo shortcuts in active Sculpt/Paint mode; keep them in Manage/Layers.

- [ ] **Step 8: Run router, ordering contract, and build GREEN**

Also retain the source-order assertion `camera < terrain < gizmo < selection` in `terrain_editor_contract_tests.cpp` so the real router and the panel wiring are both covered.

```powershell
.\RunTests.bat Debug --test-case="Terrain viewport router*"
.\RunTests.bat Debug --test-case="Terrain viewport interaction*"
.\build_editor.bat Debug
```

Expected: call order/consumption assertions pass and Editor builds.

- [ ] **Step 9: Stage the Terrain premake hunk and commit viewport arbitration**

```powershell
git add project/src/editor/Core/TerrainViewportInputRouter.h project/src/editor/Core/TerrainViewportInputRouter.cpp project/src/tests/Editor/terrain_viewport_interaction_tests.cpp project/src/editor/Core/PanelDeps/ViewportPanelDeps.h project/src/editor/App/PanelBootstrapper.cpp project/src/editor/Panels/ViewportPanelInteraction.h project/src/editor/Panels/ViewportPanelInteraction.cpp project/src/editor/Panels/ViewportPanelInteractionSupport.cpp project/src/editor/Panels/ViewportPanelToolbar.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git add -p project/src/tests/premake5.lua
git diff --cached -- project/src/tests/premake5.lua
git diff --cached --check
git commit -m "feat(editor): route terrain viewport input"
```

Expected: no Graphics include or duplicated ray math; cached premake diff adds only the Terrain viewport router and preserves all earlier Terrain/gizmo lines.

### Task 8: Add world-space brush preview with SceneOverlayLine

**Files:**
- Create: `project/src/editor/Services/TerrainBrushOverlayRenderer.h`
- Create: `project/src/editor/Services/TerrainBrushOverlayRenderer.cpp`
- Modify: `project/src/editor/Panels/ViewportPanelCanvas.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Write overlay RED contract**

```cpp
TEST_CASE("Terrain brush preview uses the ScenePresentation overlay facade")
{
    const std::string overlay = ReadText("project/src/editor/Services/TerrainBrushOverlayRenderer.cpp");
    CHECK(overlay.find("SceneOverlayLine") != std::string::npos);
    CHECK(overlay.find("submit_scene_overlay") != std::string::npos);
    CHECK(overlay.find("draw_window_line") == std::string::npos);
    CHECK(overlay.find("Graphics/") == std::string::npos);
}
```

- [ ] **Step 2: Run focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain brush preview*"
```

Expected: overlay file/calls are absent.

- [ ] **Step 3: Implement deterministic ring geometry**

Generate 64 segments in the tangent plane formed from preview normal, center each point with a `query_height` Ready result, and use `SceneOverlayDepthMode::DepthTestNoWrite`. Colors are Ready green, Pending amber, and locked/Failed red; Outside emits no lines.

- [ ] **Step 4: Submit through the viewport binding facade**

`TerrainBrushOverlayRenderer::Submit` takes `TerrainEditorPreviewState` plus `SceneViewBindingHandle`, builds a local vector of `SceneOverlayLine`, and calls `submit_scene_overlay`. Invalid binding or empty line list returns false without retaining pointers.

- [ ] **Step 5: Hook Scene viewport decoration**

In `ViewportPanelCanvas::DrawDecorations`, after helper overlays and before 2D box-selection decoration, submit the Terrain brush overlay for the primary Scene viewport. `ScenePresentationSubsystem` owns the per-binding copy and clears it after consumption.

- [ ] **Step 6: Run overlay contract/build GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain brush preview*"
.\build_editor.bat Debug
```

Expected: facade assertions pass and Editor links the overlay renderer.

- [ ] **Step 7: Commit brush overlay**

```powershell
git add project/src/editor/Services/TerrainBrushOverlayRenderer.h project/src/editor/Services/TerrainBrushOverlayRenderer.cpp project/src/editor/Panels/ViewportPanelCanvas.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): preview terrain brushes in scene"
```

Expected: overlay uses Function presentation types only.

### Task 9: Implement async save generations and Ctrl+S ordering

**Files:**
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/editor/App/EditorActionCoordinator.cpp`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Write save-generation RED tests**

```cpp
TEST_CASE("Terrain save completion clears only the captured content generation")
{
    AshEditor::TerrainEditorSessionCore core = MakeSmallTerrainEditorCore();
    REQUIRE(core.AddLayer("Before Save", AshEngine::TerrainHeightBlendMode::Additive).is_valid());
    const uint64_t saving = core.BeginSaveContentGeneration();
    REQUIRE(core.AddLayer("After Save", AshEngine::TerrainHeightBlendMode::Additive).is_valid());
    CHECK(core.CompleteSaveContentGeneration(saving, true));
    CHECK(core.IsDirty());
    CHECK(core.GetContentGeneration() > saving);
}
```

Add a failed-save case proving dirty state and undo data remain.

- [ ] **Step 2: Run save tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain save completion*"
```

Expected: generation/dirty assertion fails.

- [ ] **Step 3: Complete Phase 1 save generation behavior**

Capture the latest completed compose `content_generation` at save start. Successful completion marks only that content generation persisted; edits after capture remain dirty. Failure retains working data, dirty state, and reversible patches.

Expose the exact core bookkeeping methods used by the service and tests:

```cpp
uint64_t BeginSaveContentGeneration();
bool CompleteSaveContentGeneration(uint64_t saved_content_generation, bool succeeded);
uint64_t GetContentGeneration() const;
bool IsDirty() const;
```

- [ ] **Step 4: Add service save intents and progress**

`Save`, `SaveAs`, and `Optimize` start non-blocking Phase 1 operations. `Update` polls completion and publishes progress/error state. Exit/reload may expose Wait/Cancel/Discard choices, but ordinary `Ctrl+S` never blocks the UI thread.

- [ ] **Step 5: Order Ctrl+S Terrain assets before Scene**

In `EditorActionCoordinator`, gather dirty Terrain assets referenced by the active Scene, request their saves, and defer Scene save until all captured Terrain saves succeed. A Terrain failure leaves the Scene dirty and displays the exact asset error.

- [ ] **Step 6: Add source-contract assertion and run GREEN**

Assert `SaveDirtyReferencedTerrains` appears before the existing Scene save call. Run:

```powershell
.\RunTests.bat Debug --test-case="Terrain save completion*"
.\RunTests.bat Debug --test-case="Terrain Ctrl+S*"
.\build_editor.bat Debug
```

Expected: content-generation tests and ordering contract pass.

- [ ] **Step 7: Commit save ordering**

```powershell
git add project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/editor/App/EditorActionCoordinator.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): save terrain assets transactionally"
```

Expected: scene and asset dirty states remain distinct.

### Task 10: Implement reload and external-modification conflict flow

**Files:**
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/editor/Panels/Terrain/TerrainModePanel.cpp`
- Modify: `project/src/editor/App/SceneWorkflowCoordinator.cpp`
- Modify: `project/src/editor/Services/UndoRedoService.h`
- Modify: `project/src/editor/Services/UndoRedoService.cpp`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Write conflict/reload RED tests**

```cpp
TEST_CASE("Terrain external modification never discards dirty local state silently")
{
    AshEditor::TerrainEditorSessionCore core = MakeSmallTerrainEditorCore();
    REQUIRE(core.AddLayer("Local Edit", AshEngine::TerrainHeightBlendMode::Additive).is_valid());
    CHECK(core.NotifyExternalContentGeneration(12) == AshEditor::TerrainExternalChangeResult::Conflict);
    CHECK(core.IsDirty());
    CHECK(core.ResolveConflict(AshEditor::TerrainConflictChoice::KeepLocal));
    CHECK(core.IsDirty());
}
```

Add clean auto-reload, ReloadDiscard, SaveAs, failed reload, and stale command-history cases.

- [ ] **Step 2: Run conflict tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain external modification*"
```

Expected: missing conflict choice or dirty-preservation assertion fails.

- [ ] **Step 3: Complete Phase 1 conflict state behavior**

Clean sessions accept background reload and publish an immutable new snapshot at a frame boundary. Dirty sessions enter Conflict and keep memory unchanged until explicit ReloadDiscard, KeepLocal, or SaveAs. Failed/CRC-recovery assets remain read-only and report the exact content generation/block error.

Add the exact Editor-side state transition values:

```cpp
enum class TerrainExternalChangeResult : uint8_t { ReloadQueued, Conflict, IgnoredStale, Failed };
enum class TerrainConflictChoice : uint8_t { ReloadDiscard, KeepLocal, SaveAs };
TerrainExternalChangeResult NotifyExternalContentGeneration(uint64_t disk_content_generation);
bool ResolveConflict(TerrainConflictChoice choice);
```

- [ ] **Step 4: Add selective Terrain history removal**

Extend `EditorCommand` with an optional `GetAffectedTerrainAssetId()` returning zero by default, and add `UndoRedoService::RemoveCommandsForTerrainAsset(TerrainAssetId)`. Successful reload removes only entries for that asset and recomputes saved/current history state; unrelated entity commands remain.

- [ ] **Step 5: Add Terrain Mode conflict dialog**

Use `UIContext::begin_popup_modal` to display disk/local content generations and Reload/Discard, Keep Local, Save As actions. Failed assets show Repair and Save As but disable overwrite Save and all stroke controls.

- [ ] **Step 6: Reset sessions on Scene workflow changes**

During New/Open/Reload Scene, ask TerrainEditorService to finish/cancel active strokes, resolve blocking save choices, clear selected Terrain, then let existing Selection/UndoRedo reset proceed. Never leave a command pointing at a destroyed session.

- [ ] **Step 7: Run conflict/history tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain external modification*"
.\RunTests.bat Debug --test-case="Terrain reload history*"
.\build_editor.bat Debug
```

Expected: explicit conflict choices, selective history clearing, failed-state read-only behavior, and Editor build pass.

- [ ] **Step 8: Commit reload/conflict behavior**

```powershell
git add project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/editor/Panels/Terrain/TerrainModePanel.cpp project/src/editor/App/SceneWorkflowCoordinator.cpp project/src/editor/Services/UndoRedoService.h project/src/editor/Services/UndoRedoService.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): resolve terrain reload conflicts"
```

Expected: unrelated undo history survives Terrain-only reload.

### Task 11: Run Phase 3 exit gates and manual authoring workflow

**Files:**
- Verify: all Phase 3 files above
- Verify fixture: `product/assets/scenes/Terrain.scene.json`
- Do not modify: `tools/render/goldens/`
- Do not modify: `tools/perf/perf_gate_baselines.json`

- [ ] **Step 1: Run complete tests in Debug and Release**

```powershell
.\RunTests.bat Debug
.\RunTests.bat Release
```

Expected: both exit 0, including Terrain authoring and Editor source-contract cases.

- [ ] **Step 2: Run fresh Editor/Sandbox builds**

```powershell
.\generate_vs2022.bat
.\build_editor.bat Debug
.\build_editor.bat Release
.\build_sandbox.bat Debug
.\build_sandbox.bat Release
```

Expected: all exit 0; no unresolved Terrain command/service symbol.

- [ ] **Step 3: Run architecture and plan gates**

```powershell
.\RunArchGate.bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\AIDevDoctor.ps1 -Mode ValidatePlan
rg -n "#include .*Graphics/|#include .*Vulkan|#include .*DirectX12|ImGui::" project/src/editor/Panels/Terrain project/src/editor/Services/TerrainEditorService.* project/src/editor/Services/TerrainBrushOverlayRenderer.*
```

Expected: gates exit 0 and `rg` prints no forbidden dependency.

- [ ] **Step 4: Run Editor readiness on both backends**

```powershell
.\run.bat editor vulkan Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
.\run.bat editor dx12 Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
```

Expected: both exit 0 after Terrain asset/render/present readiness; no fixed-frame success rule.

- [ ] **Step 5: Execute the manual Terrain Mode checklist**

On both backends: create a flat 8193 terrain; import PNG, RAW, and EXR; run Raise, Lower, Smooth, Flatten, Noise, Paint, and Erase; add/duplicate/reorder/hide/lock/opacity-change layers; undo/redo each operation; save/close/reload; trigger clean external reload and dirty conflict choices; export and reimport each supported format.

Expected: one drag equals one history entry, stroke path is stable at different viewport frame rates, locked/Pending/Failed states cannot edit, and reload/save errors preserve local work.

- [ ] **Step 6: Inspect overlay and input behavior**

Expected: Ready/Pending/locked brush rings use their defined colors and follow the Terrain surface; active stroke blocks gizmo/selection; Alt+LMB, RMB, MMB, wheel, and focus camera controls remain responsive; Game viewport never authors Terrain.

- [ ] **Step 7: Run existing render/performance regression gates**

```powershell
.\RunRenderGate.bat
.\RunPerfGate.bat -Profile Standard
```

Expected: both pass; no golden or baseline is blessed in Phase 3.

- [ ] **Step 8: Inspect Phase 3 diff and status**

```powershell
git diff --check
git status --short
git log --oneline --grep="terrain" -12
```

Expected: no whitespace errors, no unrelated Editor changes, no direct Graphics include, and every Phase 3 commit is focused.

## Phase 3 completion criteria

- `TerrainEditorService` is the only mutable authoring-session owner.
- Inspector edits TerrainComponent through `SetTerrainComponentCommand` and snapshot paths include Terrain.
- Terrain Mode exposes Manage, Sculpt, Paint, and Layers entirely through UIContext.
- Raise/Lower, Smooth, Flatten, Noise, Paint, and Erase use deterministic Phase 1 kernels.
- Mouse-down through mouse-up creates one reversible patch command; empty/cancelled strokes create none.
- Layer operations use stable ids, are undoable, and dirty only affected sparse occupancy.
- Ctrl+S saves dirty referenced Terrain content generations before Scene; later edits remain dirty.
- External changes never discard dirty local state silently; successful reload removes only affected Terrain history.
- Terrain viewport strokes take mouse-left priority while camera controls remain available.
- Brush preview uses world-space `SceneOverlayLine` through ScenePresentation and never accesses Graphics.
- Debug/Release builds, tests, ArchGate, both-backend Editor readiness, RenderGate, Standard PerfGate, and the manual checklist pass.
