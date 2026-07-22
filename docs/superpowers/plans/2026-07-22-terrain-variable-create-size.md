# Terrain Variable Create Size Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Terrain authors enter independent 256–8192 m X/Z target extents, normalized to the nearest legal power of two with a 2048 × 2048 m default, while making the existing renderer safely allocate, index, publish, and draw every legal rectangular layout on Vulkan and DX12.

**Architecture:** Asset code owns the pure extent-normalization and layout-construction contract. Terrain Mode keeps shared text drafts for Create and Import and produces validated descriptors. Function Render derives one immutable `TerrainRenderLayoutInfo` per snapshot, uses it for CPU/HLSL addressing and resource sizing, and stages layout-changing snapshots in a complete candidate bundle before an atomic frame-boundary publication; the old published view remains drawable on every candidate failure.

**Tech Stack:** C++17, doctest, Editor `UIContext`, Function Asset/Render, HLSL compiled to DXIL/SPIR-V, Premake5/MSBuild, Vulkan/DX12 validation, RenderGate and PerfGate.

---

## Authoritative inputs and execution rules

- Approved SDD: `docs/sdd/SDD-2026-07-22-terrain-variable-create-size.md`.
- Long-term contracts: `docs/specs/features/terrain.md`, `docs/specs/modules/asset.md`, `docs/specs/modules/editor.md`, `docs/specs/modules/render.md`.
- Validation matrix: `docs/VERIFY.md`.
- Execute inline in `codex/terrain-variable-create-size`, task by task, using the `superpowers:executing-plans` skill.
- Preserve the historical meaning of `TerrainGridLayout{}` and `make_default_terrain_grid_layout()` as the explicit 8193²/full-pressure layout. Only authoring defaults become 2048 × 2048 m.
- Preserve existing first-load sparse/null streaming and same-layout legal null Component removal. Require a complete non-null Component table only for asset replacement and layout-changing replacement.
- Do not edit public Graphics/RHI or RenderGraph APIs. Do not bless render or performance baselines.
- Stage only files listed in the active task. Never stage or restore `project/thirdparty/tracy/tracy-profiler.exe`; never use `git add -A`.
- Before each commit, run `git diff --cached --check` and inspect `git diff --cached --stat` plus `git diff --cached`.

## File responsibility map

- `project/src/engine/Function/Asset/TerrainData.h/.cpp`: authoring extent constants, normalization, layout construction.
- `project/src/editor/Core/TerrainEditorSessionCore.h`: 2048 m create descriptor default.
- `project/src/editor/Panels/Terrain/TerrainModeState.h`: shared target drafts, strict parsing, defensive descriptor building.
- `project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp`: text inputs, deactivation/Enter normalization, derived cost labels and field errors.
- `project/src/engine/Function/Render/TerrainRenderAsset.h/.cpp`: dynamic layout derivation, resource sizing, candidate/published bundles, initial resident set and failure rollback.
- `project/src/engine/Function/Render/RenderAssetManager.h/.cpp`: preserve exact acceptance error and drive candidate finalization.
- `project/src/engine/Function/Render/TerrainRenderProxy.h/.cpp`, `RenderScene.h/.cpp`, `SceneRenderer.cpp`: consume one coherent published view and change bounds/TAA/shadow identity only at publication.
- `project/src/engine/Function/Render/TerrainRenderPass.cpp`: dynamic private constants and actual layout-dependent resource use.
- `project/src/engine/Shaders/Terrain/TerrainCommon.hlsli`, `TerrainSurface.hlsl`, `TerrainAtlasUpdate.hlsl`: dynamic row stride, boundaries, UV and last-Component ownership.
- `project/src/tests/Terrain/`, `project/src/tests/Editor/`: RED/GREEN layout, shader, rollback, scene, LOD and UI contracts.
- `docs/specs/`, `docs/CODEBASE_MAP.md`, `docs/VERIFY.md`, `docs/templates/TerrainEditorManualSignoff.md`: delivered behavior and verification handoff.

### Task 1: Add the canonical authoring extent contract

**Files:**
- Modify: `project/src/engine/Function/Asset/TerrainData.h`
- Modify: `project/src/engine/Function/Asset/TerrainData.cpp`
- Modify: `project/src/tests/Terrain/terrain_data_tests.cpp`

- [ ] **Step 1: Write normalization and independent-axis RED tests**

Add these cases to `terrain_data_tests.cpp`:

```cpp
TEST_CASE("Terrain authoring extent normalization clamps and snaps ties upward")
{
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(100u) == 256u);
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(300u) == 256u);
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(384u) == 512u);
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(500u) == 512u);
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(3000u) == 2048u);
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(3500u) == 4096u);
    CHECK(AshEngine::normalize_terrain_authoring_extent_meters(9000u) == 8192u);
}

TEST_CASE("Terrain authoring layouts derive independent axes at one meter spacing")
{
    const auto layout = AshEngine::make_terrain_authoring_grid_layout(2048u, 4096u);
    CHECK(layout.sample_count_x == 2049u);
    CHECK(layout.sample_count_z == 4097u);
    CHECK(layout.component_count_x == 8u);
    CHECK(layout.component_count_z == 16u);
    CHECK(layout.component_quad_count == 256u);
    CHECK(layout.sample_spacing_meters == doctest::Approx(1.0f));
    CHECK(AshEngine::is_valid_terrain_grid_layout(layout));
}
```

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
.\RunTests.bat Debug --test-case="*Terrain authoring*"
```

Expected: compile failure because the authoring constants/helpers do not exist.

- [ ] **Step 3: Implement the pure Asset helper**

Add the following public Asset contract without changing the existing full-layout helper:

```cpp
inline constexpr uint32_t k_terrain_authoring_extent_min_meters = 256u;
inline constexpr uint32_t k_terrain_authoring_extent_default_meters = 2048u;
inline constexpr uint32_t k_terrain_authoring_extent_max_meters = 8192u;

ASH_API auto normalize_terrain_authoring_extent_meters(
    uint32_t requested_extent_meters) noexcept -> uint32_t;
ASH_API auto make_terrain_authoring_grid_layout(
    uint32_t requested_extent_x_meters,
    uint32_t requested_extent_z_meters) noexcept -> TerrainGridLayout;
```

Clamp before snapping. Compare numeric distance to the adjacent powers of two and choose the upper value on equality. Construct layouts with spacing 1 m, 256 quads/Component, `component_count = extent/256`, and `sample_count = extent+1`.

- [ ] **Step 4: Run Debug and Release GREEN**

```powershell
.\RunTests.bat Debug --test-case="*Terrain authoring*"
.\RunTests.bat Release --test-case="*Terrain authoring*"
```

- [ ] **Step 5: Commit the Asset checkpoint**

```powershell
git add project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainData.cpp project/src/tests/Terrain/terrain_data_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add authoring extent layout helpers"
```

### Task 2: Share validated target-size drafts across Create and Import

**Files:**
- Modify: `project/src/editor/Core/TerrainEditorSessionCore.h`
- Modify: `project/src/editor/Panels/Terrain/TerrainModeState.h`
- Modify: `project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp`
- Modify: `project/src/tests/Editor/terrain_editor_contract_tests.cpp`

- [ ] **Step 1: Add descriptor, parsing and widget RED tests**

Add cases named:

- `Terrain create descriptor defaults to the 2048 meter authoring layout`
- `Terrain Mode target extent drafts normalize without losing invalid text`
- `Terrain Mode shares one target layout between create and import descriptors`
- `Terrain Mode target extent widgets normalize on Enter focus loss and submit`

The tests must prove: default 2049²/8²; `300/3500 -> 256/4096`; `384 -> 512`; `2048x` fails and remains visible; Create and Import both produce 2049 × 4097 samples / 8 × 16 Components; source dimensions such as 513 × 257 remain independent; source contracts contain both labels, `EnterReturnsTrue`, immediate `is_item_deactivated_after_edit()`, derived Samples/Components/1m text, and defensive submit-time normalization.

- [ ] **Step 2: Run focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="*Terrain create descriptor*"
.\RunTests.bat Debug --test-case="*Terrain Mode target extent*"
```

- [ ] **Step 3: Implement strict shared draft state**

Use text drafts so invalid input remains editable:

```cpp
std::string target_extent_x_meters_draft{ "2048" };
std::string target_extent_z_meters_draft{ "2048" };
std::string target_extent_x_error{};
std::string target_extent_z_error{};

static auto NormalizeTargetExtentDraft(
    std::string& draft,
    std::string& out_error) -> bool;
auto NormalizeTargetExtentDrafts() -> bool;
auto TryBuildTargetLayout(AshEngine::TerrainGridLayout& out_layout) const -> bool;
auto TryBuildCreateDesc(TerrainCreateAssetDesc& out_desc) const -> bool;
auto TryBuildImportDesc(AshEngine::TerrainHeightImportDesc& out_desc) const -> bool;
```

Parse a strict unsigned base-10 integer: reject empty strings, signs, whitespace/trailing characters and `uint32_t` overflow. Failed parsing leaves the draft unchanged and sets a field-local error. Successful parsing writes the canonical normalized decimal and clears the error. Change `TerrainCreateAssetDesc::layout` to the 2048 m authoring helper; retain `import_source_width/height` as independent source fields.

- [ ] **Step 4: Add shared target-size UI and fail-closed intents**

Render `Target size X (m)` and `Target size Z (m)` with `input_text`. Read `is_item_deactivated_after_edit()` immediately after the matching item and normalize on Enter or deactivation. Display:

```text
Samples <x> x <z> | Components <x> x <z> | 1 m/sample
```

On Create/Import click, call `NormalizeTargetExtentDrafts()` again and only enqueue the intent when the matching `TryBuild*Desc` succeeds. Remove the fixed-8193 production-grid hint.

- [ ] **Step 5: Run focused Debug/Release tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="*Terrain create descriptor*"
.\RunTests.bat Debug --test-case="*Terrain Mode target extent*"
.\RunTests.bat Release --test-case="*Terrain create descriptor*"
.\RunTests.bat Release --test-case="*Terrain Mode target extent*"
```

- [ ] **Step 6: Commit the Editor checkpoint**

```powershell
git add project/src/editor/Core/TerrainEditorSessionCore.h project/src/editor/Panels/Terrain/TerrainModeState.h project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp project/src/tests/Editor/terrain_editor_contract_tests.cpp
git diff --cached --check
git commit -m "feat(editor): configure terrain create and import extents"
```

### Task 3: Derive dynamic render layout, indexing and resource sizes

**Files:**
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_asset_tests.cpp`
- Modify: `project/src/tests/Terrain/terrain_lod_tests.cpp`

- [ ] **Step 1: Add RED tests for min, rectangle, default and maximum layouts**

Add `Terrain render layout derives rectangular resource sizes` using 1×1, 1×32, 8×8, 8×16 and 32×32 Component layouts. Lock these oracles:

```cpp
CHECK(default_info.height_buffer_bytes == 8454400u);
CHECK(default_info.coarse_width == 257u);
CHECK(default_info.coarse_height == 257u);
CHECK(rect_info.height_buffer_bytes == 16908800u);
CHECK(rect_info.coarse_width == 257u);
CHECK(rect_info.coarse_height == 513u);
CHECK(max_info.height_buffer_bytes == 135270400u);
CHECK(max_info.coarse_width == 1025u);
CHECK(max_info.coarse_height == 1025u);
```

Add fail-closed cases for zero/33 Components, non-256 quads, non-finite or non-1m spacing, mismatched samples, incomplete Component table, wrong row-major coordinate and checked-arithmetic overflow. Preserve and rerun the existing same-layout null-removal tests unchanged. Add a rectangular LOD case proving `(7,15)` is valid and `(8,15)` is outside an 8×16 layout.

- [ ] **Step 2: Run focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="*Terrain render layout*"
.\RunTests.bat Debug --test-case="*Terrain*rectangular*LOD*"
```

- [ ] **Step 3: Introduce the single render-layout value seam**

Add:

```cpp
struct ASH_API TerrainRenderLayoutInfo
{
    TerrainGridLayout layout{};
    uint32_t component_count = 0u;
    uint32_t component_row_stride = 0u;
    uint64_t height_buffer_bytes = 0u;
    uint32_t coarse_width = 0u;
    uint32_t coarse_height = 0u;

    auto component_linear_index(TerrainComponentCoord coord) const -> size_t;
    auto contains(TerrainComponentCoord coord) const -> bool;
};

ASH_API auto derive_terrain_render_layout(
    const TerrainGridLayout& layout,
    TerrainRenderLayoutInfo& out_info,
    std::string* out_error = nullptr) -> bool;
```

Perform all multiplication/addition with checked 64-bit arithmetic, then checked narrowing for API dimensions. Use `coord.z * component_count_x + coord.x` everywhere. Replace the fixed actual-allocation constant with the derived byte count and create the coarse target at `(count_x*32+1, count_z*32+1)`. Keep the 1024 maximum mask capacity, 33025 words/Component, 256 atlas slots and 4144 atlas extent fixed.

- [ ] **Step 4: Validate snapshot shape without breaking residency removal**

Require `components.size() == component_count`. Every non-null entry must match its dense coordinate. Existing first-load sparse/null streaming remains legal; asset replacement and layout replacement require every entry non-null; same-layout accepted generations retain legal null removals and the existing `(content_generation, residency_revision)` ordering contract. Include actual samples/components/quads/spacing in rejection text.

- [ ] **Step 5: Run focused and full Terrain tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="*Terrain render layout*"
.\RunTests.bat Debug --test-case="*Terrain render asset*"
.\RunTests.bat Debug --test-case="*Terrain*LOD*"
.\RunTests.bat Release --test-case="*Terrain render layout*"
```

- [ ] **Step 6: Commit the render-layout checkpoint**

```powershell
git add project/src/engine/Function/Render/TerrainRenderAsset.h project/src/engine/Function/Render/TerrainRenderAsset.cpp project/src/tests/Terrain/terrain_render_asset_tests.cpp project/src/tests/Terrain/terrain_lod_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): size render resources from asset layout"
```

### Task 4: Bind the dynamic layout identically in C++ and HLSL

**Files:**
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Shaders/Terrain/TerrainCommon.hlsli`
- Modify: `project/src/engine/Shaders/Terrain/TerrainSurface.hlsl`
- Modify: `project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl`
- Modify: `project/src/tests/Terrain/terrain_render_graph_tests.cpp`

- [ ] **Step 1: Add RED byte-layout and source-contract tests**

Add `Terrain surface constants bind rectangular layout for every pass` and `Terrain shaders contain no fixed whole-terrain stride or extent`. Lock a 240-byte surface constant block:

```cpp
struct TerrainSurfaceConstants
{
    glm::mat4 object_to_clip;
    glm::mat4 previous_object_to_clip;
    glm::mat4 object_to_world;
    glm::vec4 height_spacing_uv_scale;
    glm::uvec4 flags;
    glm::uvec4 layout;
};
static_assert(sizeof(TerrainSurfaceConstants) == 240u);
```

The test must assert HLSL contains the matching `uint4 AshTerrainLayout`, Atlas constants remain 32 bytes, and fixed whole-terrain expressions `coord.z * 32`, `min(..., 8192)`, and division by `8192.0` are absent from Terrain common code.

- [ ] **Step 2: Run focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*constants*layout*"
.\RunTests.bat Debug --test-case="*Terrain shaders*fixed*"
```

- [ ] **Step 3: Populate dynamic surface and atlas constants**

Set `layout = {component_count_x, component_count_z, sample_count_x, sample_count_z}` for GBuffer, shadow and LOD debug paths. Reuse two Atlas padding words for `component_count_x/z`, preserving 32 bytes. Use actual Component counts for final +X/+Z boundary ownership.

- [ ] **Step 4: Replace shader whole-terrain constants**

Make height row stride, global sample clamp, Terrain UV and coarse UV read `AshTerrainLayout`. Keep the per-Component coarse stride at 32 and the per-Component sample count at 257. In `SceneRenderer`, import/debug-report the coarse RenderTarget using its actual width and height rather than `k_terrain_coarse_weight_extent`.

- [ ] **Step 5: Compile every Terrain entry to DXIL and SPIR-V**

Run this exact PowerShell block; it covers the VS/PS variants that `TerrainRenderPass` creates plus Atlas CS, writes only to `Intermediate/`, and fails on the first compiler error:

```powershell
$dxc = (Resolve-Path 'project/thirdparty/dxc/bin/x64/dxc.exe').Path
$shaderRoot = (Resolve-Path 'project/src/engine/Shaders/Terrain').Path
$outRoot = Join-Path (Get-Location) 'Intermediate/shader-contract/terrain-variable-size'
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$variants = @(
    @{ File='TerrainSurface.hlsl'; Entry='VSMain'; Profile='vs_6_0'; Macro='TERRAIN_GBUFFER=1'; Name='gbuffer-vs' },
    @{ File='TerrainSurface.hlsl'; Entry='PSMain'; Profile='ps_6_0'; Macro='TERRAIN_GBUFFER=1'; Name='gbuffer-ps' },
    @{ File='TerrainSurface.hlsl'; Entry='VSMain'; Profile='vs_6_0'; Macro='TERRAIN_DEPTH_ONLY=1'; Name='depth-vs' },
    @{ File='TerrainSurface.hlsl'; Entry='PSMain'; Profile='ps_6_0'; Macro='TERRAIN_DEPTH_ONLY=1'; Name='depth-ps' },
    @{ File='TerrainSurface.hlsl'; Entry='VSMain'; Profile='vs_6_0'; Macro='TERRAIN_LOD_DEBUG=1'; Name='lod-vs' },
    @{ File='TerrainSurface.hlsl'; Entry='PSMain'; Profile='ps_6_0'; Macro='TERRAIN_LOD_DEBUG=1'; Name='lod-ps' },
    @{ File='TerrainAtlasUpdate.hlsl'; Entry='CSMain'; Profile='cs_6_0'; Macro=$null; Name='atlas-cs' }
)
foreach ($variant in $variants) {
    $source = Join-Path $shaderRoot $variant.File
    $common = @($source, '-E', $variant.Entry, '-T', $variant.Profile, '-I', $shaderRoot)
    if ($variant.Macro) { $common += @('-D', $variant.Macro) }
    & $dxc @common -Fo (Join-Path $outRoot ($variant.Name + '.dxil'))
    if ($LASTEXITCODE -ne 0) { throw "DXIL compile failed: $($variant.Name)" }
    & $dxc @common -spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout `
        -Fo (Join-Path $outRoot ($variant.Name + '.spv'))
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V compile failed: $($variant.Name)" }
}
```

- [ ] **Step 6: Run Terrain render tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*render*"
.\RunTests.bat Release --test-case="*Terrain*render*"
git add project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Shaders/Terrain/TerrainCommon.hlsli project/src/engine/Shaders/Terrain/TerrainSurface.hlsl project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl project/src/tests/Terrain/terrain_render_graph_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): bind dynamic layouts in surface shaders"
```

### Task 5: Stage layout changes in a complete candidate resource bundle

**Files:**
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderAsset.cpp`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.h`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_asset_tests.cpp`

- [ ] **Step 1: Add RED tests for candidate isolation and deterministic preload**

Add CPU seams that inject failure at candidate height buffer creation, staging creation, atlas 0/1 creation, coarse target creation, the Nth height upload, coarse dispatch and initial resident atlas dispatch. For each failure assert the old published snapshot identity, layout, five GPU resource pointers (height, staging, two atlases and coarse), bounds-facing identity and readiness remain usable. Add success cases for 8×8→8×16 and 32×32→8×8, plus asset-ID replacement whose generation restarts. Add deterministic initial-set coverage: published resident/required coordinates are filtered into the candidate layout, de-duplicated, stable LRU/coordinate ordered and capped at 256.

Add `Terrain sparse first load remains legal without a published view` and `Terrain sparse first load failure leaves no publication` so the transition classifier cannot accidentally apply complete-replacement rules to initial streaming. The classifier must have three explicit branches: no published view → sparse first load; same asset/layout → incremental generation; different asset ID or layout → complete replacement.

Add `Terrain published runtime owns post-swap LRU and same-layout queues` and assert that after candidate publication: a camera-driven residency request changes only the new view's slot/queue state; a same-layout null removal advances only that runtime state; a frame retaining the old view observes its old slots/readiness; and `TerrainRenderAsset` has no parallel global upload/readiness collections outside the view/candidate runtime owners.

- [ ] **Step 2: Run rollback tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="*Terrain candidate*"
.\RunTests.bat Debug --test-case="*Terrain layout replacement*"
```

- [ ] **Step 3: Introduce immutable published/candidate bundles**

Separate mutable candidate work from the immutable published handle so every generation-owned queue/readiness value has an explicit owner:

```cpp
struct TerrainRenderResourceSet
{
    std::shared_ptr<StorageBuffer> packed_height_buffer{};
    std::shared_ptr<StorageBuffer> dirty_weight_staging_buffer{};
    std::array<std::shared_ptr<RenderTarget>, 2> weight_atlases{};
    std::shared_ptr<RenderTarget> coarse_weight_target{};
};

struct TerrainRenderRuntimeState
{
    TerrainRenderResourceSet resources{};
    std::array<TerrainAtlasSlotMetadata, k_terrain_weight_atlas_slot_count> atlas_slots{};
    std::vector<TerrainGpuComponentUpload> pending_component_uploads{};
    std::vector<TerrainGpuComponentUpload> pending_weight_updates{};
    std::vector<TerrainComponentCoord> pending_implicit_weight_resets{};
    std::vector<TerrainComponentCoord> pending_component_removals{};
    TerrainRenderAssetState readiness_state{};
};

struct TerrainRenderCandidateState
{
    std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
    TerrainRenderLayoutInfo layout{};
    std::shared_ptr<TerrainRenderRuntimeState> runtime{};
    std::vector<TerrainComponentCoord> initial_resident_set{};
    std::string error{};
};

struct TerrainPublishedRenderView
{
    std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
    TerrainRenderLayoutInfo layout{};
    std::shared_ptr<TerrainRenderRuntimeState> runtime{};
    TerrainShadowCasterIdentity identity{};
    uint64_t publication_epoch = 0u;
};
```

Hold `std::shared_ptr<const TerrainPublishedRenderView> m_published_view` and `std::unique_ptr<TerrainRenderCandidateState> m_candidate_state`. Getters and render-pass preparation must acquire one published view, then snapshot that view's runtime under the existing asset lock; they never mix independent asset-level getters across a swap. A candidate owns a complete runtime state. Failed candidates discard it. Successful publication transfers that same runtime owner into the immutable published-view handle; post-swap camera LRU, same-layout uploads/removals, completion counters and readiness continue through `published_view->runtime`. No parallel asset-level slots, queues or readiness state may remain outside `m_published_view`/`m_candidate_state`.

- [ ] **Step 4: Build and publish candidates atomically**

For a layout/asset replacement: validate a complete snapshot; allocate every candidate resource; upload all heights and coarse weights; preload the frozen deterministic initial resident set into the candidate atlases; then enqueue one frame-boundary view swap. Candidate work must never write the published runtime's atlas or queues. Failed work destroys only the candidate runtime. Successful swap transfers candidate runtime ownership into the new view; the prior view and runtime remain alive for frames already holding them and follow deferred destruction. Subsequent camera-driven LRU requests and same-layout generations are enqueued only through the current published runtime, may use coarse fallback, and keep readiness Pending until visible atlas/upload work converges.

- [ ] **Step 5: Preserve same-layout incremental behavior**

Keep current pointer-equal pending upload carry-forward, partial upload coalescing, implicit reset and null removal behavior for same-layout snapshots. Only layout/asset replacement takes the complete-candidate path.

- [ ] **Step 6: Run focused tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="*Terrain candidate*"
.\RunTests.bat Debug --test-case="*Terrain layout replacement*"
.\RunTests.bat Debug --test-case="*Terrain render asset*"
.\RunTests.bat Release --test-case="*Terrain candidate*"
git add project/src/engine/Function/Render/TerrainRenderAsset.h project/src/engine/Function/Render/TerrainRenderAsset.cpp project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/engine/Function/Render/RenderAssetManager.h project/src/engine/Function/Render/RenderAssetManager.cpp project/src/tests/Terrain/terrain_render_asset_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): publish layout changes atomically"
```

### Task 6: Keep Scene bounds, temporal identity and errors coherent with publication

**Files:**
- Modify: `project/src/engine/Function/Render/TerrainRenderProxy.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderProxy.cpp`
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.h`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_scene_tests.cpp`

- [ ] **Step 1: Add RED tests for coherent scene publication**

Add tests proving a rejected candidate keeps the prior proxy and bounds; a pending candidate does not change culling, TAA signature or shadow identity; successful frame-boundary publication changes snapshot/resources/bounds/TAA/shadow identity together; first-load failure with no published bundle remains fail closed. Require errors to contain the asset path, received samples/components/quads/spacing and the precise candidate stage/reason.

- [ ] **Step 2: Run focused tests and verify RED**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*published*view*"
.\RunTests.bat Debug --test-case="*Terrain*candidate*error*"
```

- [ ] **Step 3: Wire one coherent published view through Scene**

Expose one immutable published-view handle from `TerrainRenderAsset`; let `TerrainRenderProxy` compute bounds from that view only. During a candidate build, `RenderScene` retains the current proxy. Advance visible-frame generation, TAA temporal signature and directional-shadow caster identity from the bundle identity only when the frame-boundary swap becomes observable.

- [ ] **Step 4: Preserve exact acceptance diagnostics**

Change `RenderAssetManager::request_terrain_asset`/status wiring to retain `accept_snapshot` detail. `RenderScene` must not replace it with a generic message or clear all Terrain proxies when a usable published view remains. Distinguish `candidate failed; published view retained` from `first publication failed; no drawable Terrain`.

- [ ] **Step 5: Run focused and full scene tests GREEN and commit**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*published*view*"
.\RunTests.bat Debug --test-case="*Terrain*candidate*error*"
.\RunTests.bat Debug --test-case="*Terrain*scene*"
.\RunTests.bat Release --test-case="*Terrain*scene*"
git add project/src/engine/Function/Render/TerrainRenderProxy.h project/src/engine/Function/Render/TerrainRenderProxy.cpp project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Function/Render/RenderAssetManager.h project/src/engine/Function/Render/RenderAssetManager.cpp project/src/engine/Function/Render/SceneRenderer.cpp project/src/tests/Terrain/terrain_render_scene_tests.cpp
git diff --cached --check
git commit -m "fix(terrain): keep scene state coherent during layout reload"
```

### Task 7: Update fixtures, specifications and verification routing

**Files:**
- Modify: `project/src/tests/Terrain/terrain_import_tests.cpp`
- Modify: `project/src/tests/Terrain/terrain_readiness_tests.cpp`
- Modify: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/specs/modules/editor.md`
- Modify: `docs/specs/modules/render.md`
- Modify: `docs/CODEBASE_MAP.md`
- Modify: `docs/VERIFY.md`
- Modify: `docs/templates/TerrainEditorManualSignoff.md`
- Modify: `docs/sdd/SDD-2026-07-22-terrain-variable-create-size.md`

- [ ] **Step 1: Lock Create/Import and compatibility fixtures**

Add target-layout tests for 256×8192, 2048×4096, 2048×2048 and 8192×8192 across Reject/Crop/Catmull-Rom. Assert the existing TerrainGate full-pressure fixture in `terrain_readiness_tests.cpp` explicitly constructs 8193²/32² and does not depend on the new authoring default. Keep `.AshTerrain` and Scene schema byte/version expectations unchanged.

Extend the existing environment-gated fixture generator in `terrain_readiness_tests.cpp` with exact token `ASHENGINE_TERRAIN_LAYOUT_FIXTURE_GENERATOR=GENERATE_LAYOUT_MATRIX_V1`. It writes only these ignored artifacts and refuses to overwrite an existing directory:

```text
Intermediate/generated-fixtures/terrain-layouts/min/TerrainLayout.AshTerrain
Intermediate/generated-fixtures/terrain-layouts/min/Terrain.scene.json
Intermediate/generated-fixtures/terrain-layouts/rect/TerrainLayout.AshTerrain
Intermediate/generated-fixtures/terrain-layouts/rect/Terrain.scene.json
Intermediate/generated-fixtures/terrain-layouts/default/TerrainLayout.AshTerrain
Intermediate/generated-fixtures/terrain-layouts/default/Terrain.scene.json
```

The layouts are respectively 256×256, 2048×4096 and 2048×2048 m. The max runtime case continues to use `product/assets/scenes/Terrain.scene.json` and `TerrainGate.AshTerrain`; no generated fixture or binary asset is committed.

- [ ] **Step 2: Run import and TerrainGate CPU tests**

```powershell
.\RunTests.bat Debug --test-case="*Terrain*import*"
.\RunTests.bat Debug --test-case="*TerrainGate*"
.\RunTests.bat Release --test-case="*Terrain*import*"
```

- [ ] **Step 3: Rewrite long-term contracts to the delivered behavior**

Document: authoring default 2048 m; independent 256–8192 m axes normalized to powers of two; 1 m spacing; 256 quads/Component; fixed 256-slot high-res atlas; dynamic height/coarse resources; complete candidate replacement and atomic rollback; unchanged schema and full-size pressure fixture. Remove statements claiming runtime resources are always 8193²/32²/1025² or surface constants are always 224 bytes.

- [ ] **Step 4: Update the Chinese human checklist**

Require a human, on Vulkan and DX12, to type `300`, `384`, `3500`, invalid text and independent X/Z values; verify normalization timing, derived labels, Create/Import shared target, a rectangular render, reload rollback diagnostics and final image. State explicitly that agents may prepare the scene and logs but may not operate or sign the UI steps.

- [ ] **Step 5: Validate docs and commit**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check
git add project/src/tests/Terrain/terrain_import_tests.cpp project/src/tests/Terrain/terrain_readiness_tests.cpp docs/specs/features/terrain.md docs/specs/modules/asset.md docs/specs/modules/editor.md docs/specs/modules/render.md docs/CODEBASE_MAP.md docs/VERIFY.md docs/templates/TerrainEditorManualSignoff.md docs/sdd/SDD-2026-07-22-terrain-variable-create-size.md
git diff --cached --check
git commit -m "docs(terrain): specify configurable authoring extents"
```

### Task 8: Run the full matrix and close the SDD

**Files:**
- Modify after successful evidence: `docs/sdd/SDD-2026-07-22-terrain-variable-create-size.md`
- Modify after successful evidence: applicable Terrain verification evidence under the repository's existing docs routing

- [ ] **Step 1: Fresh-generate and run CPU gates**

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

- [ ] **Step 2: Generate isolated layout fixtures**

```powershell
$fixtureRoot = 'Intermediate/generated-fixtures/terrain-layouts'
if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
$env:ASHENGINE_TERRAIN_LAYOUT_FIXTURE_GENERATOR = 'GENERATE_LAYOUT_MATRIX_V1'
try {
    .\RunTests.bat Debug --test-case="Terrain layout runtime fixture generator emits isolated assets"
    if ($LASTEXITCODE -ne 0) { throw 'Terrain layout fixture generation failed.' }
}
finally {
    Remove-Item Env:ASHENGINE_TERRAIN_LAYOUT_FIXTURE_GENERATOR -ErrorAction SilentlyContinue
}
```

- [ ] **Step 3: Coordinate one exclusive GPU window and run readiness**

After fresh process/config preflight, run the default four-combination matrix and then both Sandbox backends for the other layouts:

```powershell
.\run.bat editor vulkan Debug --scene=Intermediate/generated-fixtures/terrain-layouts/default/Terrain.scene.json --smoke-test-seconds=120
.\run.bat editor dx12 Debug --scene=Intermediate/generated-fixtures/terrain-layouts/default/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox vulkan Debug --scene=Intermediate/generated-fixtures/terrain-layouts/default/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox dx12 Debug --scene=Intermediate/generated-fixtures/terrain-layouts/default/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox vulkan Debug --scene=Intermediate/generated-fixtures/terrain-layouts/min/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox dx12 Debug --scene=Intermediate/generated-fixtures/terrain-layouts/min/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox vulkan Debug --scene=Intermediate/generated-fixtures/terrain-layouts/rect/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox dx12 Debug --scene=Intermediate/generated-fixtures/terrain-layouts/rect/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox vulkan Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox dx12 Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
```

Every command must exit 0 only after readiness convergence and clean exit. The logs/resource diagnostics must report the expected height/coarse byte oracles for min, rectangle, default and max.

- [ ] **Step 4: Run every layout under Vulkan and DX12 validation**

`--perf-gate-validation=on` is the existing process-local override: Vulkan enables validation+synchronization validation; DX12 enables the debug layer+GPU validation. It avoids editing `Engine.ini`.

```powershell
$scenes = @(
    'Intermediate/generated-fixtures/terrain-layouts/min/Terrain.scene.json',
    'Intermediate/generated-fixtures/terrain-layouts/rect/Terrain.scene.json',
    'Intermediate/generated-fixtures/terrain-layouts/default/Terrain.scene.json',
    'product/assets/scenes/Terrain.scene.json'
)
foreach ($scene in $scenes) {
    .\run.bat sandbox vulkan Debug "--scene=$scene" --smoke-test-seconds=120 --perf-gate-validation=on
    if ($LASTEXITCODE -ne 0) { throw "Vulkan validation failed: $scene" }
    .\run.bat sandbox dx12 Debug "--scene=$scene" --smoke-test-seconds=120 --perf-gate-validation=on
    if ($LASTEXITCODE -ne 0) { throw "DX12 validation failed: $scene" }
}
```

Require readiness convergence after visible atlas work, clean exit, and no generic error/critical, validation error, VUID, GPU-based validation error, device-lost, fatal, assert, access-violation or bad-leak rejects in the exact fresh log pairs.

- [ ] **Step 5: Run non-bless rendering and performance gates**

```powershell
.\RunRenderGate.bat
.\RunPerfGate.bat -Profile Standard
```

Stop on RenderGate FAIL, PerfGate FAIL or an unapproved WARN. Do not bless. Verify the 2048² default reduces dynamic height/coarse allocation while the explicit 8192² TerrainGate stays within its approved memory/performance contract.

- [ ] **Step 6: Restore and audit the environment**

Restore Engine.ini, EditorSettings, ViewportLayout and imgui files from byte snapshots; verify render/perf baseline hashes and zero diffs; require no effective Editor/Sandbox/AshImageDiff/gate/build roots; inspect only fresh session logs.

- [ ] **Step 7: Mark the SDD Done only after automatic evidence**

Record exact commands/results, resource byte counts, reports and any human-signoff status. Change SDD Status from Approved to Done only when all automatic required gates pass. Human UI items may be recorded as pending without being agent-signed.

- [ ] **Step 8: Commit the verification checkpoint**

```powershell
git add docs/sdd/SDD-2026-07-22-terrain-variable-create-size.md
git diff --cached --check
git commit -m "test(terrain): verify configurable terrain layouts"
```

## Completion conditions

- A newly opened Terrain Mode shows 2048 × 2048 m without changing legacy full-layout constructors or stored assets.
- X/Z drafts accept arbitrary text while editing, normalize valid integers to the nearest legal power of two on commit, tie upward, and fail closed on invalid text.
- Create and Import submit the same normalized target layout; import source dimensions remain independent.
- Every legal 1–32 × 1–32 rectangular layout derives exact CPU indices, height bytes and coarse dimensions without whole-terrain 32/8192 shader constants.
- Layout-changing reload never exposes a mixed snapshot/resource/bounds generation and never destroys a usable published view on candidate failure.
- Same-layout null removal, fixed 256-slot atlas LRU, 9 shared LOD grids, `.AshTerrain`/Scene schemas and the explicit 8193² TerrainGate fixture remain compatible.
- Debug/Release tests and builds, architecture/docs gates, DXIL/SPIR-V compilation, dual-backend readiness/validation, non-bless RenderGate and Standard PerfGate all satisfy `docs/VERIFY.md`.
- The worktree contains no staged or committed Tracy LFS noise, user runtime settings, generated logs or baseline changes.
