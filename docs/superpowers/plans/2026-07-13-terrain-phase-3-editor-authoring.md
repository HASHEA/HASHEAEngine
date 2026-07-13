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

Phase 1/2 provide the exact contracts `TerrainAssetId`, 16-byte `TerrainLayerId`, `TerrainAssetSnapshot`, `TerrainEditPatch`, `TerrainBrushParameters`, `TerrainQueryStatus`, `query_height`, `query_normal`, `ray_cast_terrain`, `prefetch_query_region`, async save/load/import/export handles, and Scene v6 `TerrainComponent`. This phase uses those names and shapes directly; it does not duplicate brush math, layer composition, container writing, or Terrain ray intersection inside Editor.

Editor code may include Function headers but must not include `Graphics/`, Vulkan, DirectX 12, RenderGraph, or backend headers. `TerrainEditorService` owns every mutable session; `TerrainModePanel`, Inspector, and `ViewportPanel` may only call its intent/query methods.

## File responsibility map

- `project/src/editor/Services/TerrainEditorService.h/.cpp`: sole session owner, intent queue, stroke sequencing, async compose/save/load coordination, dirty content generation, conflict state, and immutable preview state.
- `project/src/editor/Core/TerrainEditorSessionCore.h/.cpp`: UI-free deterministic session state, intent reduction, content-generation/sequence arbitration, and preview values used by the service and tests.
- `project/src/editor/Core/TerrainCommands.h/.cpp`: stroke patch and layer-stack commands; no UI drawing.
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
- Modify: `project/src/editor/Core/EditorContext.h`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify by Terrain-only hunk: `project/src/tests/premake5.lua`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Add Engine-linked stroke patch RED tests**

```cpp
TEST_CASE("Terrain stroke patch restores exact bytes and ignores empty changes")
{
    AshEditor::TerrainEditorSessionCore core{};
    AshEngine::TerrainWorkingSet pristine = TerrainTests::MakeSmallTerrainWorkingSet();
    AshEngine::TerrainWorkingSet edited = pristine;
    const auto before = TerrainTests::SnapshotBlockBytes(pristine, { 0, 0 });
    const std::vector<AshEngine::TerrainEditPatch> patches = TerrainTests::MakeRaisePatches(edited, { 8.0f, 0.5f });
    CHECK_FALSE(patches.empty());
    REQUIRE(core.Open(std::move(pristine)));
    CHECK(core.ApplyPatches(patches, AshEditor::TerrainPatchDirection::Forward));
    CHECK(core.ApplyPatches(patches, AshEditor::TerrainPatchDirection::Backward));
    CHECK(TerrainTests::SnapshotBlockBytes(*core.GetWorkingSet(), { 0, 0 }) == before);
    CHECK(TerrainTests::MakeNoChangePatches(edited).empty());
}
```

- [ ] **Step 2: Run the patch test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke patch*"
```

Expected: patch direction, exact restore, or empty-patch assertion fails.

- [ ] **Step 3: Complete the Phase 1 patch behavior**

Use Phase 1 `TerrainEditPatch` exactly: asset id, 16-byte layer id, component `owner.x/owner.z`, `Height`/`Weight` domain, changed rectangle, deterministic raw/RLE before bytes, and after bytes. Validate the domain-specific byte stride before mutation. One stroke returns `std::vector<TerrainEditPatch>` because it can cross component boundaries; the Editor command owns the stroke sequence. Add this UI-free Editor replay contract to `TerrainEditorSessionCore.h`:

```cpp
enum class TerrainPatchDirection : uint8_t { Forward, Backward };
bool ApplyPatches(
    const std::vector<AshEngine::TerrainEditPatch>& patches,
    TerrainPatchDirection direction,
    std::string* out_error = nullptr);
```

Decode each raw/RLE rectangle into temporary bytes first, validate every asset/layer/owner/size against the core-owned working set, then apply the full vector in stable `owner.z/owner.x` order. A validation failure changes no working-set bytes. Backward then forward replay must be byte-exact and content-generation-monotonic.

- [ ] **Step 4: Define TerrainStrokeCommand**

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

`Execute`/`Undo` locate the authoring session by asset/layer identity; selection remains unchanged. The command never stores an entire 8193 x 8193 image.

Add `TerrainEditorService* pTerrainEditorService` to `EditorContext`; `EditorApplicationImpl::WireServices` fills it. `TerrainStrokeCommand` fails without this pointer instead of falling back to direct asset mutation.

- [ ] **Step 5: Add the real Editor command test and production sources**

```cpp
TEST_CASE("TerrainStrokeCommand executes and undoes one patch by stable ids")
{
    TerrainEditorServiceHarness harness = MakeTerrainEditorServiceHarness();
    AshEditor::EditorContext context{};
    context.pTerrainEditorService = &harness.service;
    const auto before = harness.SnapshotBlock(0, 0);
    AshEditor::TerrainStrokeCommand command(harness.asset_id, harness.layer_id, 3, harness.raise_patches);
    CHECK(command.Execute(context));
    CHECK(harness.SnapshotBlock(0, 0) != before);
    CHECK(command.Undo(context));
    CHECK(harness.SnapshotBlock(0, 0) == before);
}
```

Append only these Terrain lines to the existing Tests `files` block:

```lua
"%{wks.location}/project/src/editor/Core/TerrainCommands.cpp",
"%{wks.location}/project/src/editor/Services/TerrainEditorService.cpp",
```

- [ ] **Step 6: Make stroke lifecycle emit exactly one command**

On BeginStroke allocate one sequence and lazy-capture first-touch before blocks. AddStrokeSample mutates only the service-owned working session. EndStroke finalizes one patch and submits one `TerrainStrokeCommand`; an empty patch submits none. CancelStroke applies captured before bytes and submits none.

- [ ] **Step 7: Add source-contract assertions and run GREEN**

```cpp
CHECK(CountText(serviceSource, "ExecuteCommand(std::make_unique<TerrainStrokeCommand>") == 1);
CHECK(commandSource.find("TerrainPatchDirection::Backward") != std::string::npos);
CHECK(commandSource.find("TerrainPatchDirection::Forward") != std::string::npos);
```

Run:

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke patch*"
.\RunTests.bat Debug --test-case="Terrain stroke command*"
.\build_editor.bat Debug
```

Expected: patch and source-contract tests pass; Editor links command implementations.

- [ ] **Step 8: Stage the mixed premake hunk and commit stroke history**

```powershell
git add project/src/editor/Core/TerrainCommands.h project/src/editor/Core/TerrainCommands.cpp project/src/tests/Editor/terrain_commands_tests.cpp project/src/editor/Core/EditorContext.h project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git add -p project/src/tests/premake5.lua
git diff --cached -- project/src/tests/premake5.lua
git diff --cached --check
git commit -m "feat(editor): add undoable terrain strokes"
```

Expected: no scene-wide image copy; cached premake diff adds exactly the two Terrain production sources and retains the pre-existing gizmo lines.

### Task 4: Implement deterministic viewport stroke sampling and six tools

**Files:**
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`

- [ ] **Step 1: Write frame-rate-independent sampling RED tests**

```cpp
TEST_CASE("Terrain stroke sampling is invariant to input frame subdivision")
{
    const std::vector<glm::vec3> coarse{ {0,0,0}, {10,0,0} };
    const std::vector<glm::vec3> fine{ {0,0,0}, {2,0,0}, {4,0,0}, {6,0,0}, {8,0,0}, {10,0,0} };
    CHECK(AshEngine::resample_terrain_stroke(coarse, 1.0f) == AshEngine::resample_terrain_stroke(fine, 1.0f));
}
```

Add tests for Raise, Lower, Smooth with halo, Flatten first-hit target, deterministic Noise seed, Paint normalization, and Erase fallback to layer 0.

- [ ] **Step 2: Run the focused tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke sampling*"
.\RunTests.bat Debug --test-case="Terrain brush tool*"
```

Expected: missing sampling or tool semantics fail.

- [ ] **Step 3: Complete pure brush kernels in Function**

Implement fixed world-distance resampling and the approved six authoring tools: Raise/Lower share one additive kernel; Smooth reads halo; Flatten captures first Ready hit; Noise hashes world sample plus stroke seed; Paint/Erase normalize to an exact integer sum of 255.

- [ ] **Step 4: Route service samples to the pure kernels**

Clamp radius to `[1, 2048]` meters, validate current layer kind, prefetch the brush region, reject BeginStroke unless query state is Ready, and enqueue samples with the current stroke sequence. Do not apply one sample per UI frame.

- [ ] **Step 5: Enforce ordered async publication**

Each compose job carries asset `content_generation` and stroke sequence. Publish only the next completed sequence; keep later completions queued. Discard results older than the current in-memory `content_generation` without clearing dirty state.

- [ ] **Step 6: Run sampling/tools GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain stroke sampling*"
.\RunTests.bat Debug --test-case="Terrain brush tool*"
```

Expected: coarse/fine inputs are byte-identical and all tool semantics pass.

- [ ] **Step 7: Commit tools and sequencing**

```powershell
git add project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp
git diff --cached --check
git commit -m "feat(editor): add deterministic terrain brushes"
```

Expected: one tools/sequencing commit.

### Task 5: Add undoable non-destructive layer management

**Files:**
- Modify: `project/src/editor/Core/TerrainCommands.h`
- Modify: `project/src/editor/Core/TerrainCommands.cpp`
- Modify: `project/src/editor/Services/TerrainEditorService.h`
- Modify: `project/src/editor/Services/TerrainEditorService.cpp`
- Modify: `project/src/tests/Terrain/terrain_authoring_session_tests.cpp`

- [ ] **Step 1: Write layer-stack RED tests**

```cpp
TEST_CASE("Terrain layer commands preserve ids order occupancy and undo")
{
    AshEditor::TerrainEditorSessionCore core = MakeSmallTerrainEditorCore();
    const TerrainLayerId first = core.AddLayer("First", AshEngine::TerrainHeightBlendMode::Additive);
    const TerrainLayerId second = core.AddLayer("Second", AshEngine::TerrainHeightBlendMode::Alpha);
    const auto before = core.CaptureLayerStack();
    CHECK(core.MoveLayer(second, 0));
    CHECK(core.SetLayerVisible(first, false));
    CHECK(core.SetLayerOpacity(first, 0.25f));
    CHECK(core.RestoreLayerStack(before));
    CHECK(core.CaptureLayerStack() == before);
}
```

Add delete/restore, duplicate-new-UUID, rename, lock, and affected-occupancy dirty-union cases.

- [ ] **Step 2: Run layer tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain layer commands*"
```

Expected: at least one command/restore/dirty-union assertion fails.

- [ ] **Step 3: Complete Function layer-stack mutations**

Make add/delete/duplicate/rename/reorder/visibility/opacity/lock return reversible metadata patches. Recomposition dirties only the occupancy union of affected layers; deleting a layer retains its sparse blocks in the undo patch.

Add these UI-free core methods with stable-id lookup and no index persistence:

```cpp
TerrainLayerId AddLayer(std::string name, AshEngine::TerrainHeightBlendMode mode);
bool MoveLayer(TerrainLayerId layer_id, uint32_t destination_index);
bool SetLayerVisible(TerrainLayerId layer_id, bool visible);
bool SetLayerOpacity(TerrainLayerId layer_id, float opacity);
TerrainLayerStackSnapshot CaptureLayerStack() const;
bool RestoreLayerStack(const TerrainLayerStackSnapshot& snapshot);
```

- [ ] **Step 4: Add Editor layer commands**

Define `TerrainLayerCommand` with explicit operation enum and before/after metadata patch. Execute/Undo use asset id and stable layer ids; no command depends on current selection or list index.

- [ ] **Step 5: Route LayerAction intents through command execution**

`TerrainEditorService::SubmitIntent(LayerAction)` validates the selected asset, creates exactly one layer command, and updates selected layer id after successful execution. Locked layers reject stroke begin and expose `layer_locked=true` in preview state.

- [ ] **Step 6: Run layer tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain layer commands*"
.\build_editor.bat Debug
```

Expected: all metadata/dirty/undo assertions pass and Editor builds.

- [ ] **Step 7: Commit layer management**

```powershell
git add project/src/editor/Core/TerrainCommands.h project/src/editor/Core/TerrainCommands.cpp project/src/editor/Services/TerrainEditorService.h project/src/editor/Services/TerrainEditorService.cpp project/src/tests/Terrain/terrain_authoring_session_tests.cpp
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
