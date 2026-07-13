# Terrain Phase 2 Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the Scene v6 to dual-backend rendering closure for an 8193 x 8193 Terrain, including immutable render snapshots, packed-height storage, weight-atlas compute updates, component LOD batches, directional shadows, deferred GBuffer output, and signal-driven readiness.

**Architecture:** `TerrainComponent` references a Phase 1 `TerrainAssetSnapshot`; `ScenePresentationSubsystem` copies it into `RenderTerrainProxy` and then `VisibleTerrainFrame`. `TerrainRenderAsset` owns persistent GPU resources, while `TerrainRenderPass` registers the two atlas textures with RenderGraph, declares only texture UAV/SRV dependencies, and draws shared indexed grids inside the existing GBuffer clear pass and directional-shadow callbacks.

**Tech Stack:** C++17, doctest, JSON scene schema v6, HLSL/DXC, Function RenderDevice/Renderer, RenderGraph textures, packed R16 StorageBuffer data, RGBA8 UAV atlases, Vulkan, DirectX 12, Premake5, PowerShell gates.

---

## Authoritative inputs and prerequisites

- Approved design: `docs/sdd/SDD-2026-07-13-terrain-system.md`
- Master sequence: `docs/superpowers/plans/2026-07-13-terrain-system-master.md`
- Repository map: `docs/CODEBASE_MAP.md`
- Verification matrix: `docs/VERIFY.md`
- Required completed phases: Phase 0 GPU timing/baseline and Phase 1 asset/query core.

Phase 1 must provide these exact public Function-layer values before this plan starts:

```cpp
using TerrainAssetId = uint64_t;

struct TerrainLayerId
{
    std::array<uint8_t, 16> bytes{};
};

struct TerrainDirtyComponentPayload
{
    TerrainComponentCoord coord{};
    uint64_t content_generation = 0;
    std::shared_ptr<const TerrainComponentSnapshot> component{};
};

struct TerrainAssetSnapshot
{
    TerrainAssetId asset_id = 0;
    std::filesystem::path source_path{};
    TerrainGridLayout layout{};
    TerrainHeightMapping height_mapping{};
    std::array<TerrainMaterialLayerDesc, k_terrain_material_layer_count> material_layers{};
    uint64_t content_generation = 0;
    uint64_t residency_revision = 0;
    bool failed = false;
    std::string failure_detail{};
    std::shared_ptr<const std::vector<uint16_t>> base_heights{};
    std::shared_ptr<const std::vector<TerrainEditLayer>> edit_layers{};
    std::vector<std::shared_ptr<const TerrainComponentSnapshot>> components{};
};
```

`TerrainDirtyComponentPayload` carries the immutable CPU component snapshot, not GPU-packed bytes. `TerrainRenderAsset.cpp` derives a private `TerrainGpuComponentUpload { TerrainComponentCoord coord; uint64_t content_generation; vector<uint32_t> packed_height_words; array<vector<uint8_t>, 2> weight_rgba8; }` and never exposes that private representation through Asset or Scene headers. Every component coordinate uses `coord.x` and `coord.z`.

These are the locked Phase 1 shapes; Phase 2 consumes them directly and does not create alternate aliases or payload names. Do not add physics, runtime editing, sparse binding, bindless descriptors, mesh shaders, GPU culling, texture-region upload, or another public RHI resource interface.

RenderGraph currently tracks textures, not buffers. This plan declares the weight atlas as `ComputeUAV` followed by `GraphicsSRV`; height/staging StorageBuffer transitions continue through existing program-binding barrier collection.

Preserve the repository's current global deferred order: GBuffer, AO, directional shadow work, deferred lighting, environment, particles, and post process. The SDD Terrain ordering requires dirty uploads/atlas writes before Terrain samples those resources; it does not authorize reordering unrelated existing passes. Terrain GBuffer therefore joins the existing GBuffer clear pass, and Terrain shadow geometry joins the later existing directional-shadow callbacks.

## File responsibility map

- `project/src/engine/Function/Scene/SceneComponents.h`: serialized `TerrainComponent` value only.
- `project/src/engine/Function/Scene/Scene.h/.cpp`: component facade, schema v6 JSON, extraction, validation, and independent terrain version.
- `project/src/engine/Function/Render/RenderDevice.h/.cpp`: Function-only 2D texture-array upload descriptor and creation wrapper.
- `project/src/engine/Function/Render/Renderer.h/.cpp`: thin forwarding wrapper; no backend branching.
- `project/src/engine/Function/Render/TerrainRenderAsset.h/.cpp`: content-generation-keyed GPU resources, immutable-component diffing, private packed uploads, residency state, and readiness.
- `project/src/engine/Function/Render/TerrainRenderProxy.h/.cpp`: immutable Scene-to-Render asset/transform/world-bounds proxy.
- `project/src/engine/Function/Render/TerrainLod.h/.cpp`: pure quadtree culling, projected-error LOD selection, adjacency repair, and nine draw batches.
- `project/src/engine/Function/Render/TerrainRenderPass.h/.cpp`: atlas compute, shared-grid resources, shader programs, shadow/GBuffer draws, debug resources, and timing scopes.
- `project/src/engine/Shaders/Terrain/TerrainCommon.hlsli`: packed height, normal, morph, edge alignment, weight and top-four helpers.
- `project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl`: dirty weight StorageBuffer to RGBA8 atlas/coarse-map compute writes.
- `project/src/engine/Shaders/Terrain/TerrainSurface.hlsl`: GBuffer and depth/shadow VS/PS variants.
- `project/src/engine/Function/Render/RenderAssetManager.h/.cpp`: request/finalize cache and readiness epoch integration.
- `project/src/engine/Function/Render/RenderScene.h/.cpp`: `VisibleTerrainFrame` and visible Terrain snapshot construction.
- `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`: independent Terrain revision synchronization.
- `project/src/engine/Function/Render/SceneRenderer.h/.cpp`: existing GBuffer/shadow integration, temporal invalidation, debug view, and capture-ready conjunction.
- `project/src/tests/Scene/terrain_component_tests.cpp`: Scene v6 component and migration tests.
- `project/src/tests/Terrain/terrain_texture_array_tests.cpp`: Function descriptor validation tests.
- `project/src/tests/Terrain/terrain_render_asset_tests.cpp`: content-generation/readiness/residency state-machine tests.
- `project/src/tests/Terrain/terrain_render_scene_tests.cpp`: proxy and immutable frame tests.
- `project/src/tests/Terrain/terrain_lod_tests.cpp`: culling, adjacency, batches, and `firstInstance` tests.
- `project/src/tests/Terrain/terrain_render_graph_tests.cpp`: dirty/no-dirty graph texture access and pass-order tests.
- `product/assets/scenes/Terrain.scene.json` and `product/assets/terrain/TerrainGate.AshTerrain`: deterministic integration fixture.

Do not modify `project/src/tests/premake5.lua`; the Tests project already includes `project/src/tests/**.cpp`.

### Task 1: Add Scene v6 TerrainComponent

**Files:**
- Create: `project/src/tests/Scene/terrain_component_tests.cpp`
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
- Modify: `project/src/engine/Function/Scene/Scene.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Function/Scene/SceneQuery.h`
- Modify: `project/src/engine/Function/Scene/SceneQuery.cpp`

- [ ] **Step 1: Write the failing descriptor and facade test**

```cpp
TEST_CASE("TerrainComponent descriptor and facade expose the v6 contract")
{
    AshEngine::Scene scene = AshEngine::Scene::create("Terrain Component Test");
    AshEngine::Entity entity = scene.create_entity("Terrain");
    CHECK(AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Terrain));
    AshEngine::TerrainComponent terrain{};
    terrain.asset_path = "terrain/TerrainGate.AshTerrain";
    terrain.visible = true;
    terrain.casts_shadow = true;
    terrain.receives_shadow = true;
    CHECK(entity.set_terrain_component(terrain));
    CHECK(entity.has_terrain_component());
    CHECK(entity.get_terrain_component().asset_path == terrain.asset_path);
    CHECK(scene.get_render_terrain_version() > 0);
}
```

- [ ] **Step 2: Run the focused test and observe RED**

Run:

```powershell
.\RunTests.bat Debug --test-case="TerrainComponent descriptor and facade expose the v6 contract"
```

Expected: compile failure naming `SceneComponentType::Terrain`, `TerrainComponent`, or the missing entity facade.

- [ ] **Step 3: Add the serialized component value and extraction descriptor**

Add these exact public shapes, preserving existing enum values:

```cpp
enum class SceneComponentType : uint8_t
{
    Name = 0,
    Transform,
    Camera,
    Light,
    Mesh,
    Environment,
    Particle,
    Terrain
};

struct TerrainComponent
{
    std::string asset_path{};
    bool visible = true;
    bool casts_shadow = true;
    bool receives_shadow = true;
    std::array<std::string, 8> material_layer_overrides{};
};

struct SceneTerrainExtractionDesc
{
    EntityId entity_id = 0;
    TerrainComponent terrain{};
    glm::mat4 world_transform{ 1.0f };
};
```

In `Scene.h`, add `has/get/add/set/remove_terrain_component`, `extract_terrain_entities`, and `get_render_terrain_version` beside the Particle equivalents.

- [ ] **Step 4: Implement validation and independent revision updates**

In `Scene.cpp`, route every generic component switch through `Terrain`. Reject empty `asset_path`, non-zero rotation, non-finite scale, zero scale, and negative scale in `set_terrain_component`/load. On Terrain value changes set `render_terrain_version = change_version`; on transform changes retain existing transform revision and do not rebuild Terrain asset data.

- [ ] **Step 5: Add schema v6 save/load and v5 migration tests**

```cpp
TEST_CASE("Scene v6 preserves Terrain and v5 scenes migrate without Terrain")
{
    const std::filesystem::path path = tests_temp_dir() / "terrain_component_roundtrip.scene.json";
    AshEngine::Scene source = AshEngine::Scene::create("Terrain Save");
    AshEngine::Entity entity = source.create_entity("Terrain");
    CHECK(entity.add_terrain_component({ "terrain/TerrainGate.AshTerrain", true, true, true, {} }));
    std::string error{};
    REQUIRE(source.save_to_file(path, &error));
    nlohmann::json saved = ReadJson(path);
    CHECK(saved.at("version") == 6u);
    AshEngine::Scene loaded = AshEngine::Scene::load_from_file(path, &error);
    REQUIRE_MESSAGE(loaded.is_valid(), error);
    CHECK(loaded.get_entities_with_component(AshEngine::SceneComponentType::Terrain).size() == 1);
    nlohmann::json legacy = saved;
    legacy["version"] = 5u;
    legacy["entities"][0].erase("terrain");
    WriteJson(path, legacy);
    AshEngine::Scene legacy_loaded = AshEngine::Scene::load_from_file(path, &error);
    REQUIRE_MESSAGE(legacy_loaded.is_valid(), error);
    CHECK(legacy_loaded.get_entities_with_component(AshEngine::SceneComponentType::Terrain).empty());
}
```

- [ ] **Step 6: Add world-space Scene Terrain query adapters**

Expose overloads that keep Phase 1 snapshot-local names intact:

```cpp
ASH_API auto query_height(
    const Scene& scene,
    AssetDatabase& assets,
    EntityId terrain_entity,
    const glm::vec3& world_position,
    float& out_world_height) -> TerrainQueryStatus;
ASH_API auto query_normal(
    const Scene& scene,
    AssetDatabase& assets,
    EntityId terrain_entity,
    const glm::vec3& world_position,
    glm::vec3& out_world_normal) -> TerrainQueryStatus;
ASH_API auto ray_cast_terrain(
    const Scene& scene,
    AssetDatabase& assets,
    const TerrainRay& world_ray,
    float max_distance,
    EntityId& out_terrain_entity,
    TerrainRayHit& out_world_hit) -> TerrainQueryStatus;
```

Resolve the Terrain asset through AssetDatabase, transform world inputs by the entity's translation/positive scale, call the Phase 1 snapshot-local overload, and transform Ready outputs back to world space. Preserve `Pending`, `Outside`, and `Failed`; only write output values on `Ready`.

- [ ] **Step 7: Add world-query transform tests**

Create a flat small Terrain at translation `(10, 20, 30)` and scale `(2, 3, 2)`. Assert a world height query returns `20 + local_height * 3`, normal uses inverse-transpose scale and normalization, ray hit includes the Terrain entity, Outside remains Outside, and Pending does not overwrite caller outputs.

- [ ] **Step 8: Run Scene tests GREEN**

Run:

```powershell
.\RunTests.bat Debug --test-case="*TerrainComponent*"
.\RunTests.bat Debug --test-case="*Terrain world query*"
```

Expected: all TerrainComponent and world-query cases pass; malformed Terrain transforms/assets leave the prior component unchanged.

- [ ] **Step 9: Commit the Scene contract**

```powershell
git add project/src/tests/Scene/terrain_component_tests.cpp project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.h project/src/engine/Function/Scene/Scene.cpp project/src/engine/Function/Scene/SceneQuery.h project/src/engine/Function/Scene/SceneQuery.cpp
git diff --cached --check
git commit -m "feat(scene): add terrain component schema v6"
```

Expected: one focused Scene commit; no Render or Editor files staged.

### Task 2: Add the Function-only 2D texture-array wrapper

**Files:**
- Create: `project/src/tests/Terrain/terrain_texture_array_tests.cpp`
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
- Modify: `project/src/engine/Function/Render/Renderer.h`
- Modify: `project/src/engine/Function/Render/Renderer.cpp`

- [ ] **Step 1: Write descriptor validation RED tests**

```cpp
TEST_CASE("Texture2DArray upload descriptor validates every layer and mip")
{
    AshEngine::Texture2DArrayUploadDesc desc{};
    desc.width = 1024;
    desc.height = 1024;
    desc.array_layer_count = 8;
    desc.mip_level_count = 1;
    desc.format = AshEngine::RenderTextureFormat::RGBA8_UNORM;
    std::array<uint8_t, 4> pixel{};
    AshEngine::TextureSubresourceUploadDesc subresource{ 0, 0, pixel.data(), 4096, 4096 * 1024 };
    desc.subresources = &subresource;
    desc.subresource_count = 1;
    std::string error{};
    CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
    CHECK(error == "subresource_count must equal mip_level_count * array_layer_count.");
}
```

Add cases for zero layers, out-of-range layer/mip, duplicate `(layer,mip)`, null data, short row pitch, and a complete 8-layer descriptor.

- [ ] **Step 2: Run the focused test and observe RED**

Run:

```powershell
.\RunTests.bat Debug --test-case="Texture2DArray*"
```

Expected: compile failure for `Texture2DArrayUploadDesc` and its validator.

- [ ] **Step 3: Add the Function descriptor and validator**

```cpp
struct Texture2DArrayUploadDesc
{
    uint16_t width = 1;
    uint16_t height = 1;
    RenderTextureFormat format = RenderTextureFormat::RGBA8_UNORM;
    uint16_t array_layer_count = 1;
    uint8_t mip_level_count = 1;
    const TextureSubresourceUploadDesc* subresources = nullptr;
    uint32_t subresource_count = 0;
    const char* name = nullptr;
};

ASH_API bool validate_texture_2d_array_upload_desc(
    const Texture2DArrayUploadDesc& desc,
    std::string* out_error = nullptr);
```

The validator must require exactly one unique subresource for every layer/mip when data is supplied and use `calculate_render_texture_tight_row_pitch` for pitch checks.

- [ ] **Step 4: Implement packing and creation without changing Graphics**

Add `RenderDevice::create_texture_2d_array` and `Renderer::create_texture_2d_array`. In `RenderDevice.cpp`, pack subresources in layer-major then mip-major order, set `RHI::TextureCreation::type = RHI::Ash_Texture2D`, `array_layer_count = desc.array_layer_count`, `mip_level_count`, sampled usage, and initial `SRVGraphics`. Bind the returned single `RenderTarget` with `set_texture`, not `set_texture_array`.

- [ ] **Step 5: Run texture-array tests GREEN and architecture guard**

Run:

```powershell
.\RunTests.bat Debug --test-case="Texture2DArray*"
.\RunArchGate.bat
```

Expected: descriptor cases pass; ArchGate reports no new boundary violation; `git diff -- project/src/engine/Graphics` is empty.

- [ ] **Step 6: Commit the Function wrapper**

```powershell
git add project/src/tests/Terrain/terrain_texture_array_tests.cpp project/src/engine/Function/Render/RenderDevice.h project/src/engine/Function/Render/RenderDevice.cpp project/src/engine/Function/Render/Renderer.h project/src/engine/Function/Render/Renderer.cpp
git diff --cached --check
git commit -m "feat(render): add texture 2d array wrapper"
```

Expected: no `project/src/engine/Graphics/` file in the commit.

### Task 3: Build TerrainRenderAsset content-generation and readiness state

**Files:**
- Create: `project/src/engine/Function/Render/TerrainRenderAsset.h`
- Create: `project/src/engine/Function/Render/TerrainRenderAsset.cpp`
- Create: `project/src/tests/Terrain/terrain_render_asset_tests.cpp`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.h`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`

- [ ] **Step 1: Write the content-generation state-machine test**

```cpp
TEST_CASE("Terrain render asset publishes only the newest completed content generation")
{
    AshEngine::TerrainRenderAssetState state{};
    state.begin_content_generation(7, 2);
    CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Pending);
    CHECK(state.mark_component_uploaded(7, { 0, 0 }));
    state.begin_content_generation(8, 1);
    CHECK_FALSE(state.mark_component_uploaded(7, { 1, 0 }));
    CHECK(state.mark_component_uploaded(8, { 0, 0 }));
    CHECK(state.publish_content_generation(8));
    CHECK(state.published_content_generation() == 8);
    CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Ready);
}
```

Add a failure case proving `mark_failed(8)` remains failed until a newer content generation succeeds.

Add a packing case with height mapping `{0, 100}` and component heights `{0, 50, 100}`. The first packed word must be `0x80000000` (sample 0 in low 16 bits, rounded sample 1 in high 16 bits), and the second must be `0x0000FFFF` with zero padding. Explicit eight-lane weights must split into two RGBA8 arrays without reordering; an empty weight vector must materialize Layer 0 = 255 and Layers 1–7 = 0 for every sample. Reject non-257 × 257 component dimensions, mismatched weight counts, non-finite heights, or weight sums other than 255.

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain render asset*"
```

Expected: compile failure for the new state type.

- [ ] **Step 3: Implement the pure state type**

```cpp
enum class TerrainRenderReadiness : uint8_t { Pending, Ready, Failed };

class TerrainRenderAssetState
{
public:
    void begin_content_generation(uint64_t content_generation, uint32_t required_uploads);
    bool mark_component_uploaded(uint64_t content_generation, TerrainComponentCoord coord);
    bool publish_content_generation(uint64_t content_generation);
    void mark_failed(uint64_t content_generation);
    TerrainRenderReadiness readiness() const;
    uint64_t published_content_generation() const;
};
```

Use a 1024-bit fixed completion mask indexed by `coord.z * 32 + coord.x` and content-generation checks; do not wait on a fence in this type.

- [ ] **Step 4: Add GPU resource ownership**

`TerrainRenderAsset` owns one packed-height `StorageBuffer`, one dirty-weight staging `StorageBuffer`, two `RGBA8_UNORM` UAV/SRV `RenderTarget` atlases, one coarse weight target, three material `Texture2DArray` targets, frame-boundary atlas slot metadata, and `TerrainRenderAssetState`. Height slots use `ceil(257*257/2)` `uint32_t` words per component. Encode each float with the Phase 1 mapping and round-to-nearest rule `floor(clamp((height-offset)/range, 0, 1) * 65535 + 0.5)`, put even samples in low 16 bits and odd samples in high 16 bits, and zero the final padding lane. Split each exact-255 eight-lane weight into lanes 0–3 and 4–7; materialize empty weights as implicit Layer 0. On a new immutable snapshot, compare each row-major component `shared_ptr` with the previously accepted snapshot; derive a private `TerrainGpuComponentUpload` for every changed pointer and all resident pointers on first load.

- [ ] **Step 5: Integrate request/finalize with RenderAssetManager**

Add:

```cpp
std::shared_ptr<TerrainRenderAsset> request_terrain_asset(
    const std::string& asset_path,
    const std::shared_ptr<const TerrainAssetSnapshot>& snapshot);
bool finalize_pending_terrain_asset(const std::shared_ptr<TerrainRenderAsset>& asset);
```

Increment `m_pending_render_asset_count` and `m_activity_epoch` once on request ownership, decrement once on Ready/Failed, include failed Terrain keys in `query_readiness`, and require GPU work on the render thread like textures/static meshes.

- [ ] **Step 6: Run state/readiness tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain render asset*"
```

Expected: pending, stale-content-generation, Ready, Failed, immutable-pointer diff, and manager epoch cases pass.

- [ ] **Step 7: Commit render-asset ownership**

```powershell
git add project/src/engine/Function/Render/TerrainRenderAsset.h project/src/engine/Function/Render/TerrainRenderAsset.cpp project/src/tests/Terrain/terrain_render_asset_tests.cpp project/src/engine/Function/Render/RenderAssetManager.h project/src/engine/Function/Render/RenderAssetManager.cpp
git diff --cached --check
git commit -m "feat(render): add terrain render asset readiness"
```

Expected: one render-asset commit with no SceneRenderer or shader changes.

### Task 4: Carry Terrain through RenderScene and presentation

**Files:**
- Create: `project/src/engine/Function/Render/TerrainRenderProxy.h`
- Create: `project/src/engine/Function/Render/TerrainRenderProxy.cpp`
- Create: `project/src/tests/Terrain/terrain_render_scene_tests.cpp`
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`

- [ ] **Step 1: Write the immutable proxy/frame RED test**

```cpp
TEST_CASE("Visible terrain frame keeps an immutable content-generation snapshot")
{
    auto mutable5 = std::make_shared<AshEngine::TerrainAssetSnapshot>();
    mutable5->asset_id = 42;
    mutable5->content_generation = 5;
    std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot5 = mutable5;
    AshEngine::RenderTerrainProxy proxy{};
    CHECK(proxy.initialize(9, snapshot5, glm::mat4(1.0f), true, true, true));
    AshEngine::VisibleTerrainFrame visible = proxy.make_visible_frame();
    auto mutable6 = std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot5);
    mutable6->content_generation = 6;
    std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot6 = mutable6;
    CHECK(proxy.replace_snapshot(snapshot6));
    CHECK(visible.asset_snapshot->content_generation == 5);
    CHECK(proxy.make_visible_frame().asset_snapshot->content_generation == 6);
    CHECK(visible.entity_id == 9);
}
```

The test fixture must copy a `const TerrainAssetSnapshot`; mutable Phase 1 working data must never enter `VisibleRenderFrame`.

- [ ] **Step 2: Run focused tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Visible terrain frame*"
```

Expected: compile failure for `RenderTerrainProxy` and `VisibleTerrainFrame`.

- [ ] **Step 3: Add proxy and visible-frame values**

```cpp
struct VisibleTerrainFrame
{
    EntityId entity_id = 0;
    glm::mat4 world_transform{ 1.0f };
    PrimitiveBounds world_bounds{};
    std::shared_ptr<const TerrainAssetSnapshot> asset_snapshot{};
    std::shared_ptr<TerrainRenderAsset> render_asset{};
    bool casts_shadow = true;
    bool receives_shadow = true;
};
```

`RenderTerrainProxy` stores the entity id, immutable snapshot, render asset, transform, bounds, and flags. Transform updates recompute bounds only.

- [ ] **Step 4: Add RenderScene rebuild/update/frame-copy paths**

Add `rebuild_terrains_from_scene`, `update_terrain_transforms_from_scene`, `m_terrain_proxies`, and `VisibleRenderFrame::terrains`. During rebuild request the render asset with `RenderAssetManager::request_terrain_asset`; during `build_visible_render_frame` copy only visible terrain proxies whose world AABB intersects the view frustum.

- [ ] **Step 5: Add independent presentation version tracking**

Extend `ScenePresentationSubsystem::Impl::SceneState` with `last_terrain_version`. Compare `Scene::get_render_terrain_version`; call `rebuild_terrains_from_scene` only when that value changes, and call transform update from the existing transform-version branch. Initialize the tracked value during full rebuild.

- [ ] **Step 6: Run frame and version tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="*terrain frame*"
.\RunTests.bat Debug --test-case="*terrain presentation*"
```

Expected: immutable content generation, visibility, transform-only update, full rebuild, and independent terrain revision cases pass.

- [ ] **Step 7: Commit the presentation bridge**

```powershell
git add project/src/engine/Function/Render/TerrainRenderProxy.h project/src/engine/Function/Render/TerrainRenderProxy.cpp project/src/tests/Terrain/terrain_render_scene_tests.cpp project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Function/Render/ScenePresentationSubsystem.cpp
git diff --cached --check
git commit -m "feat(render): bridge terrain scene snapshots"
```

Expected: no shader or Editor files staged.

### Task 5: Implement deterministic Component LOD batches

**Files:**
- Create: `project/src/engine/Function/Render/TerrainLod.h`
- Create: `project/src/engine/Function/Render/TerrainLod.cpp`
- Create: `project/src/tests/Terrain/terrain_lod_tests.cpp`

- [ ] **Step 1: Write projected-error and adjacency RED tests**

```cpp
TEST_CASE("Terrain LOD repair limits neighbors and emits zero-based batches")
{
    AshEngine::TerrainLodInput input = AshEngine::make_full_terrain_lod_test_input();
    input.requested_lods[0] = 0;
    input.requested_lods[1] = 4;
    AshEngine::TerrainLodResult result{};
    CHECK(AshEngine::build_terrain_lod_batches(input, result));
    CHECK(std::abs(int(result.components[0].lod) - int(result.components[1].lod)) <= 1);
    CHECK(result.batches.size() <= 9);
    for (const auto& batch : result.batches)
        CHECK(batch.first_instance == 0);
}
```

Add cases for frustum rejection, 1024 visible components, camera-height changes, and stable output ordering.

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain LOD*"
```

Expected: compile failure for the LOD API.

- [ ] **Step 3: Define the pure LOD contract**

```cpp
struct TerrainLodBatch
{
    uint8_t lod = 0;
    uint32_t first_instance = 0;
    std::vector<TerrainInstanceData> instances{};
};

struct TerrainLodResult
{
    std::vector<TerrainVisibleComponent> components{};
    std::vector<TerrainLodBatch> batches{};
};

bool build_terrain_lod_batches(const TerrainLodInput& input, TerrainLodResult& out_result);
```

- [ ] **Step 4: Implement quadtree culling and projected-error selection**

Traverse Phase 1 component bounds, reject quadtree nodes outside the frustum, select one of nine LODs from projected geometric error, then iterate horizontal/vertical neighbors until every difference is at most one.

- [ ] **Step 5: Emit stable per-LOD instances**

Sort by `(lod, coord.z, coord.x)`, emit one batch per non-empty LOD, store neighbor-edge masks and morph factors in `TerrainInstanceData`, and force every batch `first_instance` to zero.

- [ ] **Step 6: Run LOD tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain LOD*"
```

Expected: all culling, error, adjacency, stability, and first-instance assertions pass.

- [ ] **Step 7: Commit pure LOD logic**

```powershell
git add project/src/engine/Function/Render/TerrainLod.h project/src/engine/Function/Render/TerrainLod.cpp project/src/tests/Terrain/terrain_lod_tests.cpp
git diff --cached --check
git commit -m "feat(render): add terrain component lod batches"
```

Expected: a pure Function logic commit that runs without a GPU.

### Task 6: Add dirty weight-atlas graph passes

**Files:**
- Create: `project/src/engine/Function/Render/TerrainRenderPass.h`
- Create: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Create: `project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl`
- Create: `project/src/tests/Terrain/terrain_render_graph_tests.cpp`

- [ ] **Step 1: Write no-dirty/dirty RenderGraph RED tests**

```cpp
TEST_CASE("Terrain atlas update declares texture UAV before GBuffer SRV")
{
    AshEngine::RenderGraphBuilder graph = AshEngine::RenderGraphBuilder::create_headless_for_tests("TerrainGraph");
    AshEngine::RenderTargetDesc atlas_desc{ 4144, 4144, AshEngine::RenderTextureFormat::RGBA8_UNORM, true, true, "TerrainWeights" };
    auto atlas = graph.register_external_texture_desc_for_tests(atlas_desc, "TerrainWeights0");
    CHECK(AshEngine::add_terrain_atlas_contract_for_tests(graph, atlas, true));
    const auto& passes = graph.get_passes_for_tests();
    CHECK(passes.size() == 2);
    CHECK(passes[0].texture_usages[0].access == AshEngine::RenderGraphAccess::ComputeUAV);
    CHECK(passes[1].texture_usages[0].access == AshEngine::RenderGraphAccess::GraphicsSRV);
}
```

Add a `dirty=false` case proving no compute pass exists.

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain atlas*"
```

Expected: compile failure for the Terrain graph contract helper.

- [ ] **Step 3: Add the graph resource/result contract**

```cpp
struct TerrainGraphResources
{
    RenderGraphTextureRef weight_atlas_0{};
    RenderGraphTextureRef weight_atlas_1{};
    RenderGraphTextureRef coarse_weights{};
    bool has_update_pass = false;
};

TerrainGraphResources TerrainRenderPass::prepare_graph(
    RenderGraphBuilder& graph,
    const VisibleRenderFrame& frame);
```

Register persistent atlas targets as external textures. Add a compute pass only if the current content generation has private `TerrainGpuComponentUpload` entries derived from changed immutable component pointers.

- [ ] **Step 4: Implement the atlas compute shader and dispatch**

`TerrainAtlasUpdate.hlsl` reads packed dirty RGBA8 words from `ByteAddressBuffer TerrainWeightUpload`, writes `RWTexture2D<unorm float4> TerrainWeightAtlas0/1`, fills one-texel gutters from edge samples, and updates the 1025 x 1025 coarse map. Dispatch one 8 x 8 group grid per dirty component slot.

- [ ] **Step 5: Declare exact texture dependencies**

The compute setup calls `pass.write_texture(resources.weight_atlas_0, RenderGraphAccess::ComputeUAV)`, `pass.write_texture(resources.weight_atlas_1, RenderGraphAccess::ComputeUAV)`, and `pass.write_texture(resources.coarse_weights, RenderGraphAccess::ComputeUAV)`. The later GBuffer setup declares all three with `pass.read_texture(resource, RenderGraphAccess::GraphicsSRV)`. Do not add RenderGraph buffer nodes; bind `TerrainWeightUpload` through `ComputeProgram::set_storage_buffer` and let existing RenderDevice barriers transition it.

- [ ] **Step 6: Run graph tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain atlas*"
```

Expected: dirty graph has update-before-read access; clean graph has no atlas compute pass; graph compile reports no cycle.

- [ ] **Step 7: Commit the atlas path**

```powershell
git add project/src/engine/Function/Render/TerrainRenderPass.h project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl project/src/tests/Terrain/terrain_render_graph_tests.cpp
git diff --cached --check
git commit -m "feat(render): add terrain weight atlas updates"
```

Expected: graph declarations mention atlas textures only.

### Task 7: Add shared grids and Terrain surface shaders

**Files:**
- Create: `project/src/engine/Shaders/Terrain/TerrainCommon.hlsli`
- Create: `project/src/engine/Shaders/Terrain/TerrainSurface.hlsl`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_graph_tests.cpp`

- [ ] **Step 1: Write shared-grid and binding RED tests**

Add tests asserting nine index buffers contain `6 * resolution * resolution` indices for resolutions `256..1`, all batches have `first_instance == 0`, and reflected bindings include `TerrainHeightWords`, `TerrainInstances`, both atlases, coarse weights, three material arrays, and samplers.

- [ ] **Step 2: Run focused tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain shared grid*"
.\RunTests.bat Debug --test-case="Terrain shader bindings*"
```

Expected: missing grid factory or missing shader bindings.

- [ ] **Step 3: Create nine shared index buffers**

Generate row-major triangle-list indices once in `TerrainRenderPass::initialize`. Use 32-bit indices for LOD0, bind no per-component vertex buffer, and derive local grid coordinates from `SV_VertexID` plus the index value in the vertex shader.

- [ ] **Step 4: Implement packed-height and morph helpers**

In `TerrainCommon.hlsli`, decode two R16 samples from each `uint`, map through `height_offset + normalized * height_range`, read halo samples for central-difference normals, morph toward the next coarser grid, and align edge vertices using the neighbor mask.

- [ ] **Step 5: Implement fixed top-four material blending**

Read the two weight atlases, select the four largest non-zero weights with deterministic layer-index tie breaking, normalize them, and sample BaseColor/Normal/ORM `Texture2DArray` slices. A pixel with all zero values uses layer 0 weight 1.

- [ ] **Step 6: Create GBuffer and depth/shadow programs**

Create programs from `TerrainSurface.hlsl` using `TERRAIN_GBUFFER=1` and `TERRAIN_DEPTH_ONLY=1`. GBuffer writes the existing deferred HQ layout including motion vectors; depth-only uses the same height/morph path and no material arrays.

- [ ] **Step 7: Run shader/grid tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain shared grid*"
.\RunTests.bat Debug --test-case="Terrain shader bindings*"
.\build_sandbox.bat Debug
```

Expected: tests pass, DXC compiles both variants, and Sandbox build exits 0.

- [ ] **Step 8: Commit shared geometry and shaders**

```powershell
git add project/src/engine/Shaders/Terrain/TerrainCommon.hlsli project/src/engine/Shaders/Terrain/TerrainSurface.hlsl project/src/engine/Function/Render/TerrainRenderPass.h project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/tests/Terrain/terrain_render_graph_tests.cpp
git diff --cached --check
git commit -m "feat(render): draw shared-grid terrain surfaces"
```

Expected: one shader/pass commit with no backend-specific Terrain file.

### Task 8: Integrate Terrain into GBuffer, shadows, debug view, and temporal state

**Files:**
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.h`
- Modify: `project/src/engine/Function/Render/TerrainRenderPass.cpp`
- Modify: `project/src/tests/Terrain/terrain_render_graph_tests.cpp`

- [ ] **Step 1: Write integration-order RED tests**

Add a headless graph test asserting: optional atlas update precedes the existing `SceneGBufferPass`; Terrain GBuffer executes inside that pass; the existing GBuffer -> AO -> directional-shadow -> deferred-lighting order remains unchanged; and Terrain shadow draw is reached through each applicable directional-shadow callback. Add a capture-ready case where Particle is Ready but the Terrain content generation is Pending and the result is false.

- [ ] **Step 2: Run focused tests and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain SceneRenderer*"
```

Expected: pass order or readiness conjunction assertion fails.

- [ ] **Step 3: Own TerrainRenderPass in SceneRenderer lifecycle**

Initialize after shadow pass dependencies, shutdown before renderer release, and release scene-runtime Terrain resources with `release_scene_runtime_state`.

- [ ] **Step 4: Draw Terrain inside the existing clear pass**

Before creating `SceneGBufferPass`, call `TerrainRenderPass::prepare_graph`. In that pass's setup lambda declare atlas/coarse `GraphicsSRV` reads; in its execute lambda call static mesh rendering followed by `TerrainRenderPass::render_gbuffer`. Do not create a second GBuffer clear pass.

- [ ] **Step 5: Extend directional-shadow callbacks**

Replace the static-only callback with a composite callback that invokes `render_shadow_static_meshes_to_pass` and then `TerrainRenderPass::render_shadow` for the same shadow view/cascade. Skip Terrain entries with `casts_shadow == false`.

- [ ] **Step 6: Add timing and debug registrations**

Wrap GBuffer and shadow Terrain draw bodies with Phase 0 stable hashes `Terrain.GBuffer` and `Terrain.Shadow`. Register both weight atlases, coarse weights, and an LOD-color debug output through `RenderDebugView`; do not add a Terrain enum to Graphics.

- [ ] **Step 7: Invalidate history and combine readiness**

Track the last published Terrain `content_generation` per temporal view. When it changes, invalidate affected TAA history before rendering the new height. Implement capture-ready as `particle_ready && terrain_render_pass.is_capture_ready(frame)`; the Terrain result requires every visible current content generation uploaded and atlas compute completion signaled.

- [ ] **Step 8: Run integration tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain SceneRenderer*"
```

Expected: order, no-second-clear, shadow participation, timing names, history invalidation, and readiness conjunction pass.

- [ ] **Step 9: Commit SceneRenderer integration**

```powershell
git add project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/TerrainRenderPass.h project/src/engine/Function/Render/TerrainRenderPass.cpp project/src/tests/Terrain/terrain_render_graph_tests.cpp
git diff --cached --check
git commit -m "feat(render): integrate terrain deferred rendering"
```

Expected: existing AO/light/particle/post-process ordering remains unchanged.

### Task 9: Add deterministic Terrain fixtures and readiness coverage

**Files:**
- Create: `product/assets/scenes/Terrain.scene.json`
- Create: `product/assets/terrain/TerrainGate.AshTerrain`
- Create: `project/src/tests/Terrain/terrain_readiness_tests.cpp`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`

- [ ] **Step 1: Write readiness state-transition RED tests**

```cpp
TEST_CASE("Terrain readiness waits for compose upload atlas and scene submit")
{
    AshEngine::TerrainReadinessInputs inputs{};
    inputs.asset_load = AshEngine::TerrainReadinessStage::Ready;
    inputs.compose = AshEngine::TerrainReadinessStage::Ready;
    inputs.height_upload = AshEngine::TerrainReadinessStage::Ready;
    inputs.atlas_update = AshEngine::TerrainReadinessStage::Pending;
    inputs.scene_packet_succeeded = true;
    CHECK(AshEngine::evaluate_terrain_readiness(inputs) == AshEngine::TerrainReadinessStage::Pending);
    inputs.atlas_update = AshEngine::TerrainReadinessStage::Ready;
    CHECK(AshEngine::evaluate_terrain_readiness(inputs) == AshEngine::TerrainReadinessStage::Ready);
}
```

Add failure precedence and stale-content-generation cases.

- [ ] **Step 2: Run the focused test and observe RED**

```powershell
.\RunTests.bat Debug --test-case="Terrain readiness*"
```

Expected: compile failure for the readiness evaluator.

- [ ] **Step 3: Implement the pure readiness evaluator**

Return `Failed` if any stage failed, `Pending` if any stage is not Ready for the current `content_generation`, and `Ready` only when the current Scene packet succeeded. Feed this value into RenderAssetManager readiness and the existing Scene submission snapshot; never use elapsed frames as success evidence.

- [ ] **Step 4: Create the deterministic fixture**

Generate `TerrainGate.AshTerrain` through the Phase 1 asset writer, not by hand. It must contain near LOD0, visible Component boundaries, all nine LOD ranges, eight material regions, one four-layer blend region, and deterministic height/weight data. `Terrain.scene.json` uses schema 6, one Terrain entity, one camera, one directional light, and disables unrelated particles/volumetrics.

- [ ] **Step 5: Run readiness tests GREEN**

```powershell
.\RunTests.bat Debug --test-case="Terrain readiness*"
```

Expected: Pending/Failed/Ready and stale-content-generation cases pass.

- [ ] **Step 6: Commit fixtures and readiness**

```powershell
git add product/assets/scenes/Terrain.scene.json product/assets/terrain/TerrainGate.AshTerrain project/src/tests/Terrain/terrain_readiness_tests.cpp project/src/engine/Function/Render/RenderAssetManager.cpp project/src/engine/Function/Render/ScenePresentationSubsystem.cpp
git diff --cached --check
git commit -m "test(terrain): add readiness render fixture"
```

Expected: no golden image or perf baseline is committed.

### Task 10: Run Phase 2 dual-backend exit gates

**Files:**
- Verify: all Phase 2 files above
- Do not modify: `tools/render/goldens/terrain/`
- Do not modify: `tools/perf/perf_gate_baselines.json`

- [ ] **Step 1: Run full unit tests in both configurations**

```powershell
.\RunTests.bat Debug
.\RunTests.bat Release
```

Expected: both commands exit 0 with no failed doctest or legacy bridge assertion.

- [ ] **Step 2: Run fresh generation and four builds**

```powershell
.\generate_vs2022.bat
.\build_editor.bat Debug
.\build_editor.bat Release
.\build_sandbox.bat Debug
.\build_sandbox.bat Release
```

Expected: all commands exit 0; Terrain shaders compile and runtime artifacts synchronize.

- [ ] **Step 3: Run architecture and plan gates**

```powershell
.\RunArchGate.bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\AIDevDoctor.ps1 -Mode ValidatePlan
```

Expected: no new architecture violation; plan validation exits 0.

- [ ] **Step 4: Run four readiness combinations**

```powershell
.\run.bat editor vulkan Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
.\run.bat editor dx12 Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox vulkan Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
.\run.bat sandbox dx12 Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120
```

Expected: all exit 0 only after Terrain load/compose/upload/atlas/present signals complete; logs contain no fixed-frame success condition.

- [ ] **Step 5: Run validation-enabled captures in an isolated worktree**

Enable the existing Vulkan and DX12 validation settings only in the isolated validation worktree, run:

```powershell
.\run.bat sandbox vulkan Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120 --dump-frame=Intermediate/test-reports/terrain-phase2-vulkan.png
.\run.bat sandbox dx12 Debug --scene=product/assets/scenes/Terrain.scene.json --smoke-test-seconds=120 --dump-frame=Intermediate/test-reports/terrain-phase2-dx12.png
```

Expected: both exit 0; no API, descriptor, UAV/SRV barrier, synchronization, lifetime, or leak error; both PNG files exist.

- [ ] **Step 6: Run RenderGate without blessing**

```powershell
.\RunRenderGate.bat
```

Expected: existing scenes pass. Terrain may produce an unblessed candidate report, but this task must not run `-BlessGolden` or directly edit goldens.

- [ ] **Step 7: Run Standard PerfGate regression**

```powershell
.\RunPerfGate.bat -Profile Standard
```

Expected: `PASS`; any `FAIL` blocks Phase 3. Record `WARN` with its measured reason.

- [ ] **Step 8: Inspect the complete Phase 2 diff and status**

```powershell
git diff --check
git log --oneline --grep="terrain" -10
git status --short
git diff --name-only HEAD~10..HEAD | Sort-Object
```

Expected: no whitespace error, only Phase 2 files plus previously completed phase files, and no direct Graphics/RHI resource-interface change.

## Phase 2 completion criteria

- Scene schema v6 round-trips Terrain and v5 scenes load without one.
- Asset `content_generation` reaches RenderScene through immutable values and an independent Scene revision.
- Function texture arrays work through existing RHI `TextureCreation`; Graphics resource/upload interfaces are unchanged.
- Height data is packed R16 in StorageBuffer slots; weight changes use StorageBuffer upload plus compute-written RGBA8 atlases.
- RenderGraph declares atlas texture UAV/SRV ordering and makes no false claim about buffer tracking.
- Nine stable LOD batches obey neighbor difference at most one and `firstInstance == 0`.
- Terrain draws inside the existing GBuffer clear pass and every applicable directional-shadow callback.
- Readiness is `content_generation`/upload/atlas/scene-submit driven, with no frame-count success rule.
- Vulkan and DX12 builds, readiness, validation, existing RenderGate, and Standard PerfGate pass.
