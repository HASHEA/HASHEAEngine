# Terrain Phase 1 Asset Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Terrain asset and CPU-logic foundation: immutable snapshots, sparse edit layers, deterministic brushes, local queries, resilient `.AshTerrain` storage, RAW/PNG/EXR interchange, and exact `AssetDatabase` snapshot publication/invalidation.

**Architecture:** Asset-owned value types use a global-sample single source of truth and publish immutable `TerrainAssetSnapshot` / `TerrainComponentSnapshot` generations. Pure composition, brush, storage, codec, and query code remains independent of Scene, Render, Graphics, and Editor; `AssetDatabase` owns async loading and exact cache replacement. Scene v6, rendering, readiness integration, and authoring UI are separate Phase 2/3 plans.

**Tech Stack:** C++17, doctest, EnTT-independent Function/Asset logic, Windows Imaging Component, TinyEXR v3.2.0 with vendored miniz, Premake5/MSBuild, Windows x64.

---

## Scope and phase boundary

This plan implements only Phase 1 from `docs/sdd/SDD-2026-07-13-terrain-system.md`.

Included:

- 8193 x 8193 production layout and small synthetic layouts for unit tests.
- `TerrainAssetId`, `TerrainLayerId`, `TerrainAssetSnapshot`, `TerrainComponentSnapshot`, and `TerrainDirtyComponentPayload`.
- R16 base height mapping, sparse height/weight edit blocks, deterministic composition, 7 tools across 6 brush-kernel families, patch encoding/replay, min/max pyramids, LOD error, and terrain-local queries.
- `.AshTerrain` version 1, two index descriptors, CRC32, raw/RLE blocks, append-only incremental save, recovery, and optimize.
- RAW R16/R32F, WIC 8/16-bit grayscale PNG, TinyEXR half/float single-channel import/export, crop and deterministic Catmull-Rom resampling.
- `AssetType::Terrain`, sync/async Terrain load, exact immutable snapshot publication, exact invalidation, and load-state/error propagation.

Excluded:

- `TerrainComponent`, Scene schema v6, Scene serialization, world transforms, and Scene-space query adapters.
- RenderAssetManager, readiness epochs, GPU resources, RenderGraph, shaders, Vulkan, DX12, GPU timing, RenderGate, and PerfGate.
- TerrainEditorService, Terrain Mode, Inspector, UndoRedoService, and UI.
- Any public RHI change or texture-region upload API.
- `project/src/tests/premake5.lua`. Its `**.cpp` wildcard already discovers `project/src/tests/Terrain/*.cpp`, and the file contains mixed-owner gizmo entries.

## Preconditions

- Phase 0 GPU timing and empty-Editor baseline work has passed its approved gate before starting this plan.
- Execute in `D:\workspace\AshEngine\HASHEAEngine\.worktrees\terrain-system-design`.
- Before each commit, run `git status --short`, stage only the files named by that task, and inspect `git diff --cached`.
- Do not edit or stage shared-worktree gizmo files, `product/assets/scenes/Sandbox.scene.json`, `product/config/editor/imgui.ini`, or `project/src/tests/premake5.lua`.
- Run the initial baseline once:

```bat
RunTests.bat Debug
RunArchGate.bat
```

Expected: both commands exit `0`. Stop and diagnose any pre-existing failure before writing the first RED test.

## Locked public names and ownership

The following names are shared contracts with later plans and must not be renamed:

```cpp
namespace AshEngine
{
	using TerrainAssetId = uint64_t;

	struct TerrainLayerId;
	struct TerrainAssetSnapshot;
	struct TerrainComponentSnapshot;
	struct TerrainDirtyComponentPayload;

	enum class TerrainQueryStatus : uint8_t
	{
		Ready = 0,
		Pending,
		Outside,
		Failed
	};
}
```

`TerrainAssetSnapshot` and every reachable component/layer payload are immutable after publication. Mutable working data is held in `TerrainWorkingSet`; Phase 3 will own that working set inside TerrainEditorService.

## File map

### New engine files

- `project/src/engine/Function/Asset/TerrainData.h/.cpp` — public IDs, layout, block keys, immutable snapshot types, height mapping, flat terrain factory, ownership and halo helpers.
- `project/src/engine/Function/Asset/TerrainComposition.h/.cpp` — sparse edit-layer composition, exact RGBA8 weight quantization, dirty-neighbor expansion, immutable component snapshot publication.
- `project/src/engine/Function/Asset/TerrainBrush.h/.cpp` — world-metric stroke sampling, six pure brush-kernel families, canonical changed-rectangle tracking, and public patch contracts.
- `project/src/engine/Function/Asset/TerrainEditPatch.cpp` — exact logical-byte validation and atomic batch Undo/Redo replay.
- `project/src/engine/Function/Asset/TerrainBlockCodec.h/.cpp` — deterministic byte RLE shared by stroke patches and container blocks.
- `project/src/engine/Function/Asset/TerrainSpatialData.h/.cpp` — min/max hierarchy and per-LOD geometric error generation.
- `project/src/engine/Function/Asset/TerrainContainer.h/.cpp` — public load/save/optimize reports and `.AshTerrain` orchestration.
- `project/src/engine/Function/Asset/TerrainContainerFormat.h/.cpp` — private fixed-layout disk structs, little-endian conversion, CRC32, raw/RLE block codecs, descriptor validation.
- `project/src/engine/Function/Asset/TerrainImport.h/.cpp` — format-neutral import/export descriptors, cancellation, crop/Catmull-Rom dispatch, memory-limit checks.
- `project/src/engine/Function/Asset/TerrainRawCodec.cpp` — RAW R16/R32F streaming codec.
- `project/src/engine/Function/Asset/TerrainPngCodecWin.cpp` — WIC PNG codec with COM MTA and exact 16-bit pixel-format verification.
- `project/src/engine/Function/Asset/TerrainExrCodec.cpp` — TinyEXR isolation; no TinyEXR types in public headers.
- `project/src/engine/Function/Scene/TerrainQuery.h/.cpp` — snapshot-local height/normal/ray query plus AssetDatabase prefetch state; it does not include `Scene.h` in Phase 1.

### Existing engine/build files

- `project/src/engine/Function/Asset/AssetDatabase.h/.cpp` — Terrain type/load/cache/publish/invalidate.
- `project/src/engine/premake5.lua` — TinyEXR/miniz source/include and Windows WIC/COM libraries only.

### Third-party files

- `project/thirdparty/tinyexr/tinyexr.h`
- `project/thirdparty/tinyexr/deps/miniz/miniz.h`
- `project/thirdparty/tinyexr/deps/miniz/miniz.c`
- `project/thirdparty/tinyexr/LICENSE`
- `project/thirdparty/tinyexr/VERSION.md`

### New tests

- `project/src/tests/Terrain/terrain_data_tests.cpp`
- `project/src/tests/Terrain/terrain_composition_tests.cpp`
- `project/src/tests/Terrain/terrain_block_codec_tests.cpp`
- `project/src/tests/Terrain/terrain_brush_tests.cpp`
- `project/src/tests/Terrain/terrain_patch_tests.cpp`
- `project/src/tests/Terrain/terrain_spatial_tests.cpp`
- `project/src/tests/Terrain/terrain_query_tests.cpp`
- `project/src/tests/Terrain/terrain_container_tests.cpp`
- `project/src/tests/Terrain/terrain_import_tests.cpp`
- `project/src/tests/Terrain/terrain_asset_database_tests.cpp`
- `project/src/tests/Terrain/TerrainTestUtils.h`

### Documentation updated at Phase 1 completion

- `docs/specs/features/terrain.md`
- `docs/specs/modules/asset.md`
- `docs/specs/modules/scene.md`
- `docs/CODEBASE_MAP.md`
- `docs/VERIFY.md`

## Task 1: Lock Terrain IDs, layout, ownership, and immutable snapshot types

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainData.h`
- Create: `project/src/engine/Function/Asset/TerrainData.cpp`
- Create: `project/src/tests/Terrain/TerrainTestUtils.h`
- Create: `project/src/tests/Terrain/terrain_data_tests.cpp`

- [ ] **Step 1: Add the RED ownership test**

Create `terrain_data_tests.cpp` with this first test:

```cpp
#include "Function/Asset/TerrainData.h"
#include "doctest.h"

TEST_CASE("Terrain data maps shared and outer samples to one owning component")
{
	const AshEngine::TerrainGridLayout layout = AshEngine::make_default_terrain_grid_layout();
	CHECK(layout.sample_count_x == 8193u);
	CHECK(layout.sample_count_z == 8193u);
	CHECK(layout.component_count_x == 32u);
	CHECK(layout.component_count_z == 32u);

	const AshEngine::TerrainComponentCoord owner_00{ 0u, 0u };
	const AshEngine::TerrainComponentCoord owner_11{ 1u, 1u };
	const AshEngine::TerrainComponentCoord owner_31{ 31u, 31u };
	CHECK(AshEngine::get_terrain_sample_owner(layout, 255u, 255u) == owner_00);
	CHECK(AshEngine::get_terrain_sample_owner(layout, 256u, 256u) == owner_11);
	CHECK(AshEngine::get_terrain_sample_owner(layout, 8192u, 8192u) == owner_31);

	const AshEngine::TerrainSampleRect last_owned =
		AshEngine::get_terrain_component_owned_rect(layout, { 31u, 31u });
	CHECK(last_owned.width() == 257u);
	CHECK(last_owned.height() == 257u);
}
```

- [ ] **Step 2: Run the RED test**

Run:

```bat
RunTests.bat Debug --test-case="Terrain data maps*"
```

Expected: build fails with `C1083` because `Function/Asset/TerrainData.h` does not exist.

- [ ] **Step 3: Add the locked public value types**

Create `TerrainData.h` with these declarations and keep these exact cross-plan names:

```cpp
#pragma once

#include "Base/hcore.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	using TerrainAssetId = uint64_t;

	static constexpr uint32_t k_terrain_sample_count = 8193u;
	static constexpr uint32_t k_terrain_component_count = 32u;
	static constexpr uint32_t k_terrain_component_quad_count = 256u;
	static constexpr uint32_t k_terrain_component_sample_count = 257u;
	static constexpr uint32_t k_terrain_material_layer_count = 8u;

	struct TerrainLayerId
	{
		std::array<uint8_t, 16> bytes{};
		ASH_API bool is_valid() const;
		friend bool operator==(const TerrainLayerId& lhs, const TerrainLayerId& rhs)
		{
			return lhs.bytes == rhs.bytes;
		}
		friend bool operator!=(const TerrainLayerId& lhs, const TerrainLayerId& rhs)
		{
			return !(lhs == rhs);
		}
	};

	struct TerrainComponentCoord
	{
		uint16_t x = 0;
		uint16_t z = 0;
		friend bool operator==(const TerrainComponentCoord& lhs, const TerrainComponentCoord& rhs)
		{
			return lhs.x == rhs.x && lhs.z == rhs.z;
		}
	};

	struct TerrainSampleRect
	{
		uint32_t min_x = 0;
		uint32_t min_z = 0;
		uint32_t max_x_exclusive = 0;
		uint32_t max_z_exclusive = 0;
		uint32_t width() const { return max_x_exclusive - min_x; }
		uint32_t height() const { return max_z_exclusive - min_z; }
		bool empty() const { return min_x >= max_x_exclusive || min_z >= max_z_exclusive; }
	};

	struct TerrainGridLayout
	{
		uint32_t sample_count_x = k_terrain_sample_count;
		uint32_t sample_count_z = k_terrain_sample_count;
		uint32_t component_count_x = k_terrain_component_count;
		uint32_t component_count_z = k_terrain_component_count;
		uint32_t component_quad_count = k_terrain_component_quad_count;
		float sample_spacing_meters = 1.0f;
	};

	struct TerrainHeightMapping
	{
		float height_offset = 0.0f;
		float height_range = 1024.0f;
	};

	enum class TerrainHeightBlendMode : uint8_t
	{
		Additive = 0,
		Alpha
	};

	struct TerrainMaterialLayerDesc
	{
		std::string name{};
		std::string base_color_asset_path{};
		std::string normal_asset_path{};
		std::string orm_asset_path{};
	};

	struct TerrainComponentSnapshot
	{
		TerrainComponentCoord coord{};
		uint64_t content_generation = 0;
		uint32_t sample_width = 0;
		uint32_t sample_height = 0;
		std::vector<float> heights{};
		std::vector<std::array<uint8_t, k_terrain_material_layer_count>> weights{};
		std::array<uint32_t, 10> min_max_level_offsets{};
		std::vector<glm::vec2> min_max_levels{};
		std::array<float, 9> lod_errors{};
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
		std::vector<std::shared_ptr<const TerrainComponentSnapshot>> components{};
	};

	struct TerrainDirtyComponentPayload
	{
		TerrainComponentCoord coord{};
		uint64_t content_generation = 0;
		std::shared_ptr<const TerrainComponentSnapshot> component{};
	};

	ASH_API auto make_default_terrain_grid_layout() -> TerrainGridLayout;
	ASH_API auto is_valid_terrain_grid_layout(const TerrainGridLayout& layout) -> bool;
	ASH_API auto get_terrain_sample_owner(
		const TerrainGridLayout& layout,
		uint32_t sample_x,
		uint32_t sample_z) -> TerrainComponentCoord;
	ASH_API auto get_terrain_component_owned_rect(
		const TerrainGridLayout& layout,
		TerrainComponentCoord coord) -> TerrainSampleRect;
	ASH_API auto get_terrain_component_snapshot_rect(
		const TerrainGridLayout& layout,
		TerrainComponentCoord coord) -> TerrainSampleRect;
	ASH_API auto collect_terrain_components_sharing_sample(
		const TerrainGridLayout& layout,
		uint32_t sample_x,
		uint32_t sample_z) -> std::vector<TerrainComponentCoord>;
}
```

- [ ] **Step 4: Implement single-owner and halo math**

In `TerrainData.cpp`, implement these exact rules:

```cpp
owner_x = min(sample_x / component_quad_count, component_count_x - 1);
owner_z = min(sample_z / component_quad_count, component_count_z - 1);
owned_min = component_index * component_quad_count;
owned_max_exclusive = next_component_min;
last_owned_max_exclusive = sample_count;
snapshot_max_exclusive = min(owned_min + component_quad_count + 1, sample_count);
```

Reject invalid layouts where spacing is non-finite/non-positive, counts are zero, or
`sample_count != component_count * component_quad_count + 1`. For a sample on an internal X or Z boundary, `collect_terrain_components_sharing_sample` returns both adjacent component indices; on a corner it returns four, sorted by `z` then `x`, without duplicates.

`TerrainAssetSnapshot::components` always has `component_count_x * component_count_z` entries in row-major order `z * component_count_x + x`. A null entry means the component is not resident and Terrain query returns `Pending`; it never means an empty or flat component.

- [ ] **Step 5: Add small-layout test utilities**

Create `TerrainTestUtils.h` with a 9 x 9, 2 x 2, four-quad layout factory and deterministic component lookup helper. This keeps unit tests below one MiB rather than allocating an 8193 x 8193 asset.

```cpp
#pragma once

#include "Function/Asset/TerrainData.h"

namespace TerrainTests
{
	inline auto MakeSmallLayout() -> AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x = 9u;
		layout.sample_count_z = 9u;
		layout.component_count_x = 2u;
		layout.component_count_z = 2u;
		layout.component_quad_count = 4u;
		layout.sample_spacing_meters = 1.0f;
		return layout;
	}
}
```

- [ ] **Step 6: Run the GREEN ownership tests**

Run:

```bat
RunTests.bat Debug --test-case="Terrain data*"
```

Expected: the Terrain data cases pass with zero failed assertions.

- [ ] **Step 7: Commit the locked data contract**

```bat
git add project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainData.cpp project/src/tests/Terrain/TerrainTestUtils.h project/src/tests/Terrain/terrain_data_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add asset snapshot data contract"
```

Expected: one focused commit; `project/src/tests/premake5.lua` is absent from `git diff --cached --name-only`.

## Task 2: Add R16 height mapping and flat immutable snapshots

**Files:**

- Modify: `project/src/engine/Function/Asset/TerrainData.h`
- Modify: `project/src/engine/Function/Asset/TerrainData.cpp`
- Modify: `project/src/tests/Terrain/terrain_data_tests.cpp`

- [ ] **Step 1: Add RED height mapping and flat snapshot cases**

Append tests that require these behaviors:

```cpp
TEST_CASE("Terrain data maps R16 endpoints and midpoint linearly")
{
	const AshEngine::TerrainHeightMapping mapping{ -128.0f, 512.0f };
	CHECK(AshEngine::decode_terrain_height_r16(0u, mapping) == doctest::Approx(-128.0f));
	CHECK(AshEngine::decode_terrain_height_r16(65535u, mapping) == doctest::Approx(384.0f));
	const uint16_t encoded = AshEngine::encode_terrain_height_r16(64.0f, mapping);
	CHECK(AshEngine::decode_terrain_height_r16(encoded, mapping) == doctest::Approx(64.0f).epsilon(0.0001));
}

TEST_CASE("Terrain data creates a complete flat immutable snapshot")
{
	const AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(7u, layout, { -10.0f, 20.0f }, 2.5f, snapshot, &error));
	REQUIRE(snapshot != nullptr);
	CHECK(snapshot->asset_id == 7u);
	CHECK(snapshot->content_generation == 1u);
	CHECK(snapshot->components.size() == 4u);
	for (const auto& component : snapshot->components)
	{
		REQUIRE(component != nullptr);
		CHECK(component->heights.size() == 25u);
		CHECK(component->weights.empty());
	}
}
```

- [ ] **Step 2: Run the RED data tests**

```bat
RunTests.bat Debug --test-case="Terrain data*"
```

Expected: compile fails because `encode_terrain_height_r16`, `decode_terrain_height_r16`, and `create_flat_terrain_snapshot` are undeclared.

- [ ] **Step 3: Add the exact public declarations**

Add to `TerrainData.h`:

```cpp
ASH_API auto encode_terrain_height_r16(float world_height, const TerrainHeightMapping& mapping) -> uint16_t;
ASH_API auto decode_terrain_height_r16(uint16_t encoded_height, const TerrainHeightMapping& mapping) -> float;
ASH_API auto create_flat_terrain_snapshot(
	TerrainAssetId asset_id,
	const TerrainGridLayout& layout,
	const TerrainHeightMapping& mapping,
	float world_height,
	std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
	std::string* out_error = nullptr) -> bool;
```

- [ ] **Step 4: Implement finite validation and deterministic quantization**

Implement encoding as:

```cpp
normalized = clamp((world_height - height_offset) / height_range, 0.0f, 1.0f);
encoded = static_cast<uint16_t>(floor(normalized * 65535.0f + 0.5f));
```

Require finite `height_offset`, finite positive `height_range`, and finite input height. Fill one row-major global R16 vector and assign it to `base_heights`; fill every component snapshot from its 257-style snapshot rect. Leave `weights` empty to encode the implicit Layer 0 = 255 / Layers 1-7 = 0 state without allocating eight bytes per sample. Publish through a local mutable `shared_ptr<TerrainAssetSnapshot>` converted to `shared_ptr<const TerrainAssetSnapshot>` only after every component succeeds.

- [ ] **Step 5: Run the GREEN data suite**

```bat
RunTests.bat Debug --test-case="Terrain data*"
```

Expected: all Terrain data tests pass; no allocation uses production-sized storage in the test process.

- [ ] **Step 6: Commit height mapping and flat creation**

```bat
git add project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainData.cpp project/src/tests/Terrain/terrain_data_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): create flat immutable snapshots"
```

## Task 3: Compose sparse non-destructive height and weight layers

**Files:**

- Modify: `project/src/engine/Function/Asset/TerrainData.h`
- Create: `project/src/engine/Function/Asset/TerrainComposition.h`
- Create: `project/src/engine/Function/Asset/TerrainComposition.cpp`
- Create: `project/src/tests/Terrain/terrain_composition_tests.cpp`

- [ ] **Step 1: Add RED Additive/Alpha composition tests**

Create a small-layout fixture with base height 10, an Additive layer containing delta 4/mask 0.5/strength 0.5, and an Alpha layer containing target 30/mask 0.25/strength 1. Assert the selected sample is 15.75:

```cpp
const float after_additive = 10.0f + 4.0f * 0.5f * 0.5f;
const float final_height = glm::mix(after_additive, 30.0f, 0.25f);
CHECK(final_height == doctest::Approx(15.75f));
```

Also hide each layer in turn and assert only occupied component coordinates enter the dirty set.

- [ ] **Step 2: Add the RED exact-weight test**

Use floating weights `{ 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.3 }`; require all eight output bytes sum to exactly 255 and the rounding remainder is assigned to the largest layer, index 7. Add an all-zero case that returns `{255,0,0,0,0,0,0,0}`.

- [ ] **Step 3: Run the RED composition tests**

```bat
RunTests.bat Debug --test-case="Terrain composition*"
```

Expected: build fails because `TerrainComposition.h` and sparse layer types do not exist.

- [ ] **Step 4: Add sparse working-set types**

Add these mutable, unpublished types to `TerrainData.h`:

```cpp
struct TerrainSparseHeightBlock
{
	TerrainComponentCoord owner{};
	TerrainSampleRect changed_rect{};
	std::vector<float> values{};
	std::vector<float> coverage{};
};

struct TerrainSparseWeightBlock
{
	TerrainComponentCoord owner{};
	TerrainSampleRect changed_rect{};
	std::vector<std::array<float, k_terrain_material_layer_count>> values{};
	std::vector<float> coverage{};
};

struct TerrainEditLayer
{
	TerrainLayerId id{};
	std::string name{};
	bool visible = true;
	float strength = 1.0f;
	TerrainHeightBlendMode height_blend_mode = TerrainHeightBlendMode::Additive;
	std::vector<TerrainSparseHeightBlock> height_blocks{};
	std::vector<TerrainSparseWeightBlock> weight_blocks{};
};

struct TerrainWorkingSet
{
	TerrainAssetId asset_id = 0;
	std::filesystem::path source_path{};
	TerrainGridLayout layout{};
	TerrainHeightMapping height_mapping{};
	uint64_t content_generation = 0;
	uint64_t residency_revision = 0;
	std::vector<uint16_t> base_heights{};
	std::array<TerrainMaterialLayerDesc, k_terrain_material_layer_count> material_layers{};
	std::vector<TerrainEditLayer> edit_layers{};
	std::vector<std::shared_ptr<const TerrainComponentSnapshot>> components{};
};
```

Move the `TerrainAssetSnapshot` definition below `TerrainEditLayer`, then add this field before container work starts:

```cpp
std::shared_ptr<const std::vector<TerrainEditLayer>> edit_layers{};
```

Update `create_flat_terrain_snapshot` to publish an empty const edit-layer vector. The `base_heights` vector is row-major global storage and is the only authoritative copy of shared border samples.

Every `TerrainSparseHeightBlock::changed_rect`, `TerrainSparseWeightBlock::changed_rect`, and `TerrainEditPatch::changed_rect` uses global sample coordinates with exclusive maxima. The corresponding value/coverage byte arrays are row-major over that exact rectangle; owner-local rectangles are never stored in public data.

- [ ] **Step 5: Declare composition operations**

Create `TerrainComposition.h` with:

```cpp
#pragma once

#include "Function/Asset/TerrainData.h"

namespace AshEngine
{
	ASH_API auto quantize_terrain_weights(
		const std::array<float, k_terrain_material_layer_count>& weights)
		-> std::array<uint8_t, k_terrain_material_layer_count>;

	ASH_API auto collect_dirty_terrain_components(
		const TerrainGridLayout& layout,
		const TerrainSampleRect& changed_samples)
		-> std::vector<TerrainComponentCoord>;

	ASH_API auto compose_terrain_components(
		const TerrainWorkingSet& working_set,
		const std::vector<TerrainComponentCoord>& requested_components,
		std::vector<TerrainDirtyComponentPayload>& out_payloads,
		std::string* out_error = nullptr) -> bool;

	ASH_API auto make_terrain_working_set(
		const TerrainAssetSnapshot& snapshot,
		TerrainWorkingSet& out_working_set,
		std::string* out_error = nullptr) -> bool;

	ASH_API auto publish_terrain_working_set(
		TerrainWorkingSet& working_set,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		std::string* out_error = nullptr) -> bool;
}
```

- [ ] **Step 6: Implement deterministic layer traversal**

Sort block lookup by `owner.z`, then `owner.x`; preserve layer vector order. Apply only visible layers. Clamp layer strength and coverage to `[0,1]`, reject non-finite values, clamp floating weights to non-negative values, normalize, round each lane to nearest integer, and add `255 - current_sum` to the largest normalized lane; ties choose the lowest layer index. Keep `TerrainComponentSnapshot::weights` empty when no weight block affects that component; allocate the full row-major eight-lane vector only for affected components.

Each composed `TerrainComponentSnapshot` and its `TerrainDirtyComponentPayload` carry exactly `working_set.content_generation`.

`make_terrain_working_set` copies immutable source arrays into mutable vectors. The initial Task 3 publication path verifies generations and shares unchanged const components; Task 4A tightens it to exact full-dirty-set matching, atomic working-set component replacement, and dirty clearing. Neither path mutates the input snapshot or an existing component.

- [ ] **Step 7: Implement dirty neighbor expansion**

For each changed global sample, include its owner and every component whose 257 x 257 snapshot rectangle contains that sample. Sort unique results by `z`, then `x`. This makes a corner sample dirty exactly four components without duplicating persistent storage.

- [ ] **Step 8: Run the GREEN composition suite**

```bat
RunTests.bat Debug --test-case="Terrain composition*"
```

Expected: Additive/Alpha, hidden-layer, dirty-neighbor, and exact-255 cases pass.

- [ ] **Step 9: Commit layer composition**

```bat
git add project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainComposition.h project/src/engine/Function/Asset/TerrainComposition.cpp project/src/tests/Terrain/terrain_composition_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): compose sparse edit layers"
```

## Task 4A: Enforce canonical blocks and close generation/dirty publication

**Files:**

- Modify: `project/src/engine/Function/Asset/TerrainData.h`
- Modify: `project/src/engine/Function/Asset/TerrainComposition.h`
- Modify: `project/src/engine/Function/Asset/TerrainComposition.cpp`
- Modify: `project/src/tests/Terrain/terrain_composition_tests.cpp`

- [ ] **Step 1: Add RED canonical-block tests**

Add fixtures with duplicate Height owners, duplicate Weight owners, an all-zero block, and a block whose non-zero coverage does not touch its declared outer rectangle. Assert deep working-set creation rejects each non-canonical domain independently, while one Height plus one Weight block for the same `(layer, owner)` remains valid. Lock the invariant that coverage is finite in `[0,1]`, every stored block is the minimal non-zero coverage rectangle, and block order is irrelevant because owner identity is unique.

- [ ] **Step 2: Add RED generation/dirty publication tests**

Extend `TerrainWorkingSet` expectations to cover a sorted, unique `dirty_components` vector. Assert:

- a changed boundary sample contributes the full halo and merges without duplicates;
- publication rejects missing, extra, duplicate, wrong-generation, or wrong-coordinate payloads;
- every failed publication preserves working-set component pointers, dirty set, output snapshot, and generation;
- exact payload success updates the working-set component pointers, clears dirty state, publishes the same pointers, and shares all unchanged component pointers.

- [ ] **Step 3: Run the RED closure suite**

```bat
RunTests.bat Debug --test-case="Terrain composition*"
```

Expected: canonical duplicate and exact-dirty publication assertions fail.

- [ ] **Step 4: Make dirty state explicit and publication atomic**

Add `std::vector<TerrainComponentCoord> dirty_components{}` to `TerrainWorkingSet`. Change publication to take a mutable working set:

```cpp
ASH_API auto publish_terrain_working_set(
    TerrainWorkingSet& working_set,
    const std::vector<TerrainDirtyComponentPayload>& dirty_components,
    std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
    std::string* out_error = nullptr) -> bool;
```

Validate that payload coordinates are exactly equal to the full sorted working-set dirty vector and that payload/component generations equal `working_set.content_generation`. Build every throwing temporary first. On success, use only no-throw moves/swaps to replace the working-set component vector, clear dirty coordinates, and publish the immutable snapshot as one commit point. On failure, leave both inputs and the caller's previous `out_snapshot` untouched.

- [ ] **Step 5: Enforce one canonical block per domain/owner**

During shallow/deep layer validation, reject duplicate owner coordinates within `height_blocks` and within `weight_blocks`; do not reject the same owner across the two domains. Reject coverage outside `[0,1]`, all-zero stored blocks, and non-minimal zero-only outer rows/columns. Preserve deterministic traversal by sorting non-owning block pointers `owner.z`, then `owner.x`; do not reorder caller storage as a side effect.

- [ ] **Step 6: Run GREEN and commit the closure**

```bat
RunTests.bat Debug --test-case="Terrain composition*"
RunTests.bat Debug --test-case="Terrain data*"
RunArchGate.bat
git add project/src/engine/Function/Asset/TerrainData.h project/src/engine/Function/Asset/TerrainComposition.h project/src/engine/Function/Asset/TerrainComposition.cpp project/src/tests/Terrain/terrain_composition_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): close dirty component publication"
```

Expected: focused suites and ArchGate pass; cached diff contains only the four named files.

## Task 4B: Add deterministic RLE and world-metric stroke resampling

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainBlockCodec.h`
- Create: `project/src/engine/Function/Asset/TerrainBlockCodec.cpp`
- Create: `project/src/engine/Function/Asset/TerrainBrush.h`
- Create: `project/src/engine/Function/Asset/TerrainBrush.cpp`
- Create: `project/src/tests/Terrain/terrain_block_codec_tests.cpp`
- Create: `project/src/tests/Terrain/terrain_brush_tests.cpp`

- [ ] **Step 1: Add RED RLE malformed-input tests**

Round-trip empty, repeated, and mixed vectors. Assert encoding uses maximal runs and little-endian `(uint32 count, uint8 value)` records. For decode, lock zero count, truncated record, expected-size overflow, decoded-size mismatch, and trailing partial record failures; every encode/decode failure preserves the caller's previous output vector. Code review additionally verifies the encoder's `uint64_t remaining` loop splits a theoretical run longer than `UINT32_MAX` without allocating such a multi-gigabyte unit fixture.

- [ ] **Step 2: Add RED world-metric resampling tests**

Compare endpoint-only and densely subdivided representations of the same terrain-local polyline under non-uniform metric `{2, 0.5}`. Assert identical positions/pressures, exact first and final endpoints, linear pressure interpolation, no duplicate exact-spacing endpoint, later-sample replacement for adjacent metric-distance squared `<= 1e-12`, and the specified empty/single-point behavior. Add invalid finite/range cases for spacing, metric, position, and pressure.

- [ ] **Step 3: Run the RED codec/sampling suites**

```bat
RunTests.bat Debug --test-case="Terrain block codec*"
RunTests.bat Debug --test-case="Terrain stroke sampling*"
```

Expected: compile fails because the new headers do not exist.

- [ ] **Step 4: Add the locked public brush and patch types**

Create `TerrainBrush.h` with these public shapes and no Editor dependency:

```cpp
enum class TerrainBrushTool : uint8_t
{
    Raise = 0,
    Lower,
    Smooth,
    Flatten,
    Noise,
    Paint,
    Erase
};

struct TerrainBrushMetric
{
    glm::vec2 world_meters_per_terrain_meter{ 1.0f, 1.0f };
};

struct TerrainBrushParameters
{
    TerrainBrushTool tool = TerrainBrushTool::Raise;
    float radius_meters = 16.0f;
    float strength = 1.0f;
    float falloff = 0.5f;
    float stroke_spacing_meters = 1.0f;
    TerrainLayerId layer_id{};
    uint32_t material_layer_index = 0;
    uint64_t random_seed = 0;
};

struct TerrainStrokeSample
{
    glm::vec2 terrain_local_xz{};
    float pressure = 1.0f;
};

enum class TerrainEditPatchDomain : uint8_t { Height = 0, Weight };
enum class TerrainEditPatchDirection : uint8_t { Undo = 0, Redo };

struct TerrainEditPatch
{
    TerrainAssetId asset_id = 0;
    TerrainLayerId layer_id{};
    TerrainComponentCoord owner{};
    TerrainEditPatchDomain domain = TerrainEditPatchDomain::Height;
    TerrainSampleRect changed_rect{};
    uint64_t stroke_generation = 0;
    TerrainBlockCodec before_codec = TerrainBlockCodec::None;
    TerrainBlockCodec after_codec = TerrainBlockCodec::None;
    std::vector<uint8_t> before_bytes{};
    std::vector<uint8_t> after_bytes{};
};

ASH_API auto resample_terrain_stroke(
    const std::vector<TerrainStrokeSample>& input,
    const TerrainBrushMetric& metric,
    float spacing_meters,
    std::vector<TerrainStrokeSample>& out_samples,
    std::string* out_error = nullptr) -> bool;

ASH_API auto apply_terrain_brush_stroke(
    TerrainWorkingSet& working_set,
    const TerrainBrushParameters& params,
    const TerrainBrushMetric& metric,
    const std::vector<TerrainStrokeSample>& raw_input,
    std::vector<TerrainEditPatch>& out_patches,
    std::vector<TerrainComponentCoord>& out_dirty_components,
    std::string* out_error = nullptr) -> bool;

ASH_API auto apply_terrain_edit_patches(
    TerrainWorkingSet& working_set,
    const std::vector<TerrainEditPatch>& patches,
    TerrainEditPatchDirection direction,
    std::vector<TerrainComponentCoord>& out_dirty_components,
    std::string* out_error = nullptr) -> bool;
```

Both apply functions return the full sorted post-success `working_set.dirty_components` through `out_dirty_components`; replay failure preserves that output, while invalid brush input clears brush outputs as specified by the SDD.

Create `TerrainBlockCodec.h` with:

```cpp
enum class TerrainBlockCodec : uint8_t { None = 0, Rle };

ASH_API auto encode_terrain_rle(
    const std::vector<uint8_t>& decoded,
    std::vector<uint8_t>& out_encoded) -> bool;
ASH_API auto decode_terrain_rle(
    const std::vector<uint8_t>& encoded,
    size_t expected_decoded_size,
    std::vector<uint8_t>& out_decoded) -> bool;
```

- [ ] **Step 5: Implement strict RLE**

Encode maximal runs and split only a run longer than `UINT32_MAX`. Decode with checked addition before allocation/write. Build into a temporary vector and swap only after exact size validation. RLE is a byte codec only; raw Height/Weight logical schemas are added in Task 4C.

- [ ] **Step 6: Implement metric resampling once**

Scale terrain-local deltas by both positive metric axes before measuring distance. Accumulate segment and next-emission distance in `double`, interpolate position and pressure at exact spacing multiples, carry remainder across segments, and append the exact final sample once. The public resampler exists for unit tests and diagnostics; `apply_terrain_brush_stroke` is the only production entry and will invoke it exactly once internally.

- [ ] **Step 7: Run GREEN twice and commit primitives**

```bat
RunTests.bat Debug --test-case="Terrain block codec*"
RunTests.bat Debug --test-case="Terrain stroke sampling*"
RunTests.bat Debug --test-case="Terrain stroke sampling*"
git add project/src/engine/Function/Asset/TerrainBlockCodec.h project/src/engine/Function/Asset/TerrainBlockCodec.cpp project/src/engine/Function/Asset/TerrainBrush.h project/src/engine/Function/Asset/TerrainBrush.cpp project/src/tests/Terrain/terrain_block_codec_tests.cpp project/src/tests/Terrain/terrain_brush_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add world metric stroke sampling"
```

Expected: both sampling runs are element-for-element identical; RLE tests pass.

## Task 4C: Apply the six deterministic brush-kernel families

**Files:**

- Modify: `project/src/engine/Function/Asset/TerrainBrush.cpp`
- Modify: `project/src/tests/Terrain/terrain_brush_tests.cpp`

- [ ] **Step 1: Add RED validation and compatibility tests**

Lock all finite/range constraints: `0 < radius <= 2048`, positive spacing and metric axes, strength/falloff/pressure in `[0,1]`, valid tool/layer, and Paint/Erase material index `< 8`. Assert Additive accepts only Raise/Lower/Noise, Alpha accepts only Smooth/Flatten, and Paint/Erase ignore height blend mode. Any incompatible or invalid stroke leaves working set/generation/dirty unchanged and clears patch/dirty outputs.

- [ ] **Step 2: Add RED frozen-source and kernel tests**

Use small multi-layer fixtures and assert:

- the entire stroke reads frozen `Base + visible layers through selected`, never higher layers or pending component snapshots;
- Raise/Lower share signed Additive math and Noise locks the approved SplitMix64 hash for fixed global coordinates/seed;
- Smooth uses the frozen four-neighbor average with Terrain-edge coordinate clamp;
- Flatten captures the first resampled dab's bilinear frozen height exactly once;
- Paint uses a selected-lane one-hot target;
- Erase zeroes the selected lane, renormalizes other non-zero frozen weights, and falls back to Layer 0;
- `falloff == 1` has influence for `r < 1` and zero at/on/outside the boundary without a degenerate smoothstep.

- [ ] **Step 3: Add RED canonical patch tests**

Assert one changed stroke:

- increments generation exactly once and stores that post-increment value in every `stroke_generation`;
- emits at most one patch per `(layer, domain, owner)`, sorted `owner.z`, `owner.x`, Height before Weight;
- stores minimal non-zero changed rectangles and merges the full halo into sorted unique dirty state;
- serializes Height as little-endian `value, coverage` floats (8 bytes/sample) and Weight as eight target floats plus coverage (36 bytes/sample);
- chooses raw/RLE independently for before and after, selecting RLE only when strictly smaller;
- removes a canonical block when all resulting coverage is zero;
- returns empty patches, no new dirty coordinates, and no generation change for an empty/no-op stroke.

Include a reduced synthetic layout with large sample spacing to exercise the exact 2048 m boundary without allocating a production-size stroke. Use it to prove checked owner/area/byte arithmetic and touched-block-only candidates; do not copy `base_heights`, the whole working set, or untouched layer blocks, and do not add an arbitrary memory cap.

- [ ] **Step 4: Run the RED kernel suite**

```bat
RunTests.bat Debug --test-case="Terrain brush*"
```

Expected: kernel/patch assertions fail because brush application is not implemented.

- [ ] **Step 5: Freeze authoritative through-selected sources**

Locate the selected layer by UUID, validate tool compatibility before allocation, and compose the frozen height/weight source directly from Base plus visible layers up to and including it. Do not read `TerrainComponentSnapshot` caches. Capture the Flatten target by bilinear sampling the first resampled dab. Allocate only owner-local frozen/candidate data needed by the stroke footprint.

- [ ] **Step 6: Apply dabs in deterministic order**

Use the exact SDD radial function. For Additive blocks, accumulate premultiplied signed contribution with `c' = a + c*(1-a)` and store `d' = p'/c'`. For Alpha height and Weight blocks, use source-over target/coverage. Hash Noise with the exact approved constants and unsigned wraparound. After all dabs, shrink or remove canonical blocks and generate deterministic patches from first-touch before logical bytes and final after logical bytes.

- [ ] **Step 7: Commit the stroke atomically**

Finish all candidate blocks, patches, dirty-union vectors, and checked generation before mutation. Commit by no-throw swaps, increment generation once, set each patch's `stroke_generation` to that new generation, and return the full post-stroke dirty set. Allocation, checked-arithmetic, validation, or generation-overflow failure leaves the working set unchanged.

- [ ] **Step 8: Run GREEN twice and commit kernels**

```bat
RunTests.bat Debug --test-case="Terrain brush*"
RunTests.bat Debug --test-case="Terrain brush*"
git add project/src/engine/Function/Asset/TerrainBrush.cpp project/src/tests/Terrain/terrain_brush_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): apply deterministic brush kernels"
```

Expected: both runs produce the same fixed Noise patch hash and all seven tools/six kernel families pass.

## Task 4D: Replay Terrain edit patches atomically for Undo/Redo

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainEditPatch.cpp`
- Create: `project/src/tests/Terrain/terrain_patch_tests.cpp`

- [ ] **Step 1: Add the full brush-to-history RED test**

Apply a multi-owner brush, snapshot logical block bytes, call `apply_terrain_edit_patches(..., Undo, ...)`, then Redo. Assert Undo restores exact before bytes, Redo restores exact after bytes, each replay increments current generation exactly once rather than restoring `stroke_generation`, and both operations return the full halo dirty set.

- [ ] **Step 2: Add RED batch atomicity and malformed tests**

Cover invalid asset/layer/owner/domain/rect/codec/stride, duplicate `(layer, domain, owner)`, mixed stroke generations, zero-count/truncated/overflow/wrong-size RLE, non-finite logical floats, out-of-range coverage, current logical bytes not equal to the direction's source bytes, allocation failure injection, and `UINT64_MAX` generation. Put one bad patch after one valid patch and assert working set bytes, generation, dirty set, and caller output are all unchanged. Assert a target with all-zero coverage removes its canonical block. An empty batch succeeds without changing bytes/generation and returns the current full dirty vector.

- [ ] **Step 3: Run the RED replay suite**

```bat
RunTests.bat Debug --test-case="Terrain patch*"
```

Expected: link fails because `apply_terrain_edit_patches` is not implemented.

- [ ] **Step 4: Decode and validate the whole batch first**

For Undo, require current logical bytes equal each patch's decoded `after` and target decoded `before`; reverse these for Redo. Validate every identity, rectangle, independent codec, exact logical stride, finite value, and coverage before building owner-local candidate blocks. `stroke_generation` is diagnostic/original ordering metadata: require one non-zero value shared by the batch, but never require current generation to equal it.

- [ ] **Step 5: Build canonical candidates without partial mutation**

Apply decoded target rectangles into copies of only the touched canonical blocks. Shrink zero-only borders, remove all-zero blocks, compute the full halo dirty union, and check the single next generation. Do not change caller outputs during validation/candidate construction.

- [ ] **Step 6: Commit once and run GREEN**

After every candidate succeeds, swap all touched layer block vectors and dirty/generation state in one no-throw commit. Set `out_dirty_components` to the full post-replay working-set dirty vector only on success.

```bat
RunTests.bat Debug --test-case="Terrain patch*"
RunTests.bat Debug --test-case="Terrain brush*"
RunTests.bat Debug --test-case="Terrain composition*"
RunArchGate.bat
git add project/src/engine/Function/Asset/TerrainEditPatch.cpp project/src/tests/Terrain/terrain_patch_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): replay edit patches atomically"
```

Expected: complete brush -> patch -> Undo -> Redo and every failure-atomicity case pass.

## Task 5: Build min/max data, LOD error, and snapshot-local queries

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainSpatialData.h`
- Create: `project/src/engine/Function/Asset/TerrainSpatialData.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainData.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainComposition.cpp`
- Create: `project/src/engine/Function/Scene/TerrainQuery.h`
- Create: `project/src/engine/Function/Scene/TerrainQuery.cpp`
- Create: `project/src/tests/Terrain/terrain_spatial_tests.cpp`
- Create: `project/src/tests/Terrain/terrain_query_tests.cpp`

- [ ] **Step 1: Add RED min/max and LOD tests**

Create a 9 x 9 component snapshot with a single peak. Assert the level-zero bounds contain exact min/max for each 4 x 4-cell leaf block, every parent contains all children, and all nine `lod_errors` are finite, non-negative, and monotonically non-decreasing from fine to coarse LOD. Add a production-size count assertion that the min/max hierarchy contains at most `64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2 + 1` `glm::vec2` entries per component.

- [ ] **Step 2: Add RED query status tests**

Create tests for:

- a bilinear height query at a known sub-sample coordinate;
- a centered-difference normal on a plane `height = 2*x + 3*z`;
- `Outside` at negative and upper-exclusive coordinates;
- `Pending` when the requested component pointer is absent;
- `Failed` when `TerrainAssetSnapshot::failed` is true;
- exact ray intersection against the two triangles of a heightfield cell, including nearest-hit distance.

- [ ] **Step 3: Run both RED suites**

```bat
RunTests.bat Debug --test-case="Terrain spatial*"
RunTests.bat Debug --test-case="Terrain query*"
```

Expected: builds fail because `TerrainSpatialData.h` and `TerrainQuery.h` are absent.

- [ ] **Step 4: Add spatial build declarations**

Create `TerrainSpatialData.h`:

```cpp
#pragma once

#include "Function/Asset/TerrainData.h"

namespace AshEngine
{
	ASH_API auto build_terrain_component_spatial_data(
		TerrainComponentSnapshot& component,
		uint32_t sample_width,
		uint32_t sample_height,
		std::string* out_error = nullptr) -> bool;
}
```

Level zero contains one min/max pair per 4 x 4-cell block, not one pair per cell; this caps the full production hierarchy near 43 MiB instead of more than 500 MiB. Each next level combines 2 x 2 children. LOD error is the maximum absolute difference between the full-resolution height and bilinear reconstruction from that LOD's retained vertices.

Update `compose_terrain_components` and `create_flat_terrain_snapshot` to call `build_terrain_component_spatial_data` before converting a mutable component to `shared_ptr<const TerrainComponentSnapshot>`. Height, weights, min/max, and LOD errors must therefore publish in one immutable generation.

- [ ] **Step 5: Add the locked query status and local API**

Create `Function/Scene/TerrainQuery.h` without including `Scene.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"
#include <cfloat>
#include <glm/glm.hpp>

namespace AshEngine
{
	class AssetDatabase;

	enum class TerrainQueryStatus : uint8_t
	{
		Ready = 0,
		Pending,
		Outside,
		Failed
	};

	struct TerrainRay
	{
		glm::vec3 origin{};
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
	};

	struct TerrainRayHit
	{
		float distance = 0.0f;
		glm::vec3 position{};
		glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
		TerrainComponentCoord component{};
		glm::vec2 local_sample{};
	};

	ASH_API auto query_height(
		const TerrainAssetSnapshot& snapshot,
		const glm::vec2& terrain_local_xz,
		float& out_height) -> TerrainQueryStatus;
	ASH_API auto query_normal(
		const TerrainAssetSnapshot& snapshot,
		const glm::vec2& terrain_local_xz,
		glm::vec3& out_normal) -> TerrainQueryStatus;
	ASH_API auto ray_cast_terrain(
		const TerrainAssetSnapshot& snapshot,
		const TerrainRay& ray,
		float max_distance,
		TerrainRayHit& out_hit) -> TerrainQueryStatus;
}
```

- [ ] **Step 6: Implement exact status precedence**

Return `Failed` first when the snapshot is failed or its layout/data is invalid. Return `Outside` when the local X/Z lies outside `[0, (sample_count-1)*spacing]`. Return `Pending` when the owning component snapshot is absent. Only write output values on `Ready`.

- [ ] **Step 7: Implement hierarchical exact ray traversal**

Intersect the ray with component AABBs, visit components in increasing entry distance, descend the min/max hierarchy front-to-back to a 4 x 4-cell leaf block, then test those cells' triangles `(00,10,11)` and `(00,11,01)` exactly. Reject zero-length/non-finite directions. Stop after the nearest hit; do not march with a fixed step.

- [ ] **Step 8: Run the GREEN spatial and query suites**

```bat
RunTests.bat Debug --test-case="Terrain spatial*"
RunTests.bat Debug --test-case="Terrain query*"
```

Expected: both suites pass, including `Ready`, `Pending`, `Outside`, and `Failed` branches.

- [ ] **Step 9: Commit spatial and local query logic**

```bat
git add project/src/engine/Function/Asset/TerrainSpatialData.h project/src/engine/Function/Asset/TerrainSpatialData.cpp project/src/engine/Function/Asset/TerrainData.cpp project/src/engine/Function/Asset/TerrainComposition.cpp project/src/engine/Function/Scene/TerrainQuery.h project/src/engine/Function/Scene/TerrainQuery.cpp project/src/tests/Terrain/terrain_spatial_tests.cpp project/src/tests/Terrain/terrain_query_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add CPU spatial queries"
```

## Task 6: Define `.AshTerrain` v1 disk records and generation-one round trip

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainContainerFormat.h`
- Create: `project/src/engine/Function/Asset/TerrainContainerFormat.cpp`
- Create: `project/src/engine/Function/Asset/TerrainContainer.h`
- Create: `project/src/engine/Function/Asset/TerrainContainer.cpp`
- Create: `project/src/tests/Terrain/terrain_container_tests.cpp`

- [ ] **Step 1: Add a RED generation-one round-trip test**

Create a small flat snapshot, save it, load it, and assert magic/version, layout, generation, all component heights, all material descriptors, and implicit empty weight vectors are preserved. `TerrainAssetId` is an AssetDatabase runtime identity and is not persisted; standalone container load returns ID zero. Read the first eight file bytes and require `{'A','S','H','T','E','R','R','\0'}`.

- [ ] **Step 2: Add RED raw/RLE and CRC corruption tests**

Create one constant component that should choose RLE and one non-repeating component that should choose raw. The test may include Asset-private `TerrainContainerFormat.h` only to locate payload offsets for fault injection. Flip one byte inside each block payload in copied files and require load to return `TerrainContainerResult::Corrupt` with a non-empty error containing the block offset.

- [ ] **Step 3: Run the RED container suite**

```bat
RunTests.bat Debug --test-case="Terrain container*"
```

Expected: build fails because `TerrainContainer.h` does not exist.

- [ ] **Step 4: Add fixed-width internal disk structs**

Create `TerrainContainerFormat.h` as an Asset-private header. Use explicitly sized integers, byte arrays, and `static_assert` on every disk struct. Do not serialize an in-memory C++ object with `sizeof(object)`.

```cpp
#pragma once

#include "Function/Asset/TerrainData.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AshEngine::TerrainContainerFormat
{
	static constexpr std::array<uint8_t, 8> k_magic =
		{ 'A', 'S', 'H', 'T', 'E', 'R', 'R', 0 };
	static constexpr uint32_t k_version = 1u;
	static constexpr uint32_t k_little_endian_marker = 0x01020304u;

	enum class BlockKind : uint8_t
	{
		Metadata = 0,
		BaseHeight,
		EditHeight,
		EditWeight,
		ComposedComponent,
		MinMax,
		LodError
	};

#pragma pack(push, 1)
	struct IndexDescriptorDisk
	{
		uint64_t generation_le = 0;
		uint64_t index_offset_le = 0;
		uint64_t index_size_le = 0;
		uint32_t index_crc32_le = 0;
		uint32_t reserved_le = 0;
	};

	struct FileHeaderDisk
	{
		std::array<uint8_t, 8> magic{};
		uint32_t version_le = 0;
		uint32_t endian_marker_le = 0;
		uint32_t header_size_le = 0;
		uint32_t reserved_le = 0;
		std::array<IndexDescriptorDisk, 2> index_descriptors{};
		std::array<uint8_t, 8> reserved_bytes{};
	};

	struct BlockRecordDisk
	{
		std::array<uint8_t, 16> layer_id{};
		uint8_t kind = 0;
		uint8_t codec = 0;
		uint16_t channel_le = 0;
		uint16_t component_x_le = 0;
		uint16_t component_z_le = 0;
		uint64_t offset_le = 0;
		uint64_t stored_size_le = 0;
		uint64_t decoded_size_le = 0;
		uint32_t payload_crc32_le = 0;
		uint32_t reserved_le = 0;
	};
#pragma pack(pop)

	static_assert(sizeof(IndexDescriptorDisk) == 32u);
	static_assert(sizeof(FileHeaderDisk) == 96u);
	static_assert(sizeof(BlockRecordDisk) == 56u);

	auto crc32(const uint8_t* bytes, size_t size) -> uint32_t;
}
```

- [ ] **Step 5: Add the public container API**

Create `TerrainContainer.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class TerrainContainerResult : uint8_t
	{
		Success = 0,
		RecoveredPreviousGeneration,
		NotFound,
		UnsupportedVersion,
		Corrupt,
		IoFailure,
		InvalidData
	};

	struct TerrainContainerLoadReport
	{
		uint64_t loaded_generation = 0;
		bool recovered_previous_generation = false;
		uint32_t decoded_block_count = 0;
	};

	struct TerrainContainerSaveReport
	{
		uint64_t previous_generation = 0;
		uint64_t committed_generation = 0;
		uint64_t bytes_appended = 0;
		uint32_t blocks_written = 0;
	};

	ASH_API auto load_terrain_container(
		const std::filesystem::path& path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainContainerLoadReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto save_terrain_container_incremental(
		const std::filesystem::path& path,
		const TerrainAssetSnapshot& snapshot,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		TerrainContainerSaveReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto optimize_terrain_container(
		const std::filesystem::path& path,
		TerrainContainerSaveReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;
}
```

- [ ] **Step 6: Implement bounded CRC32 and RLE**

Use CRC-32/ISO-HDLC polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR `0xFFFFFFFF`. Reuse `encode_terrain_rle` / `decode_terrain_rle` from `TerrainBlockCodec`; do not introduce a second RLE implementation.

- [ ] **Step 7: Implement generation-one save ordering**

For a new file:

1. Write a zeroed 96-byte header with magic/version/endian marker.
2. Append metadata and component blocks.
3. Append the sorted index and compute its CRC.
4. Flush the stream.
5. Call `FlushFileBuffers` on Windows.
6. Write descriptor slot 0 at its fixed header offset.
7. Flush the stream and call `FlushFileBuffers` again.

Choose RLE only when its encoded byte count is strictly smaller than raw. Sort index records by kind, Layer ID bytes, component Z, component X, then channel so identical logical content produces identical index bytes.

Metadata stores layout, height mapping, material descriptors, layer order/visibility/strength/blend mode, and snapshot generation. Base-height blocks are encoded from `snapshot.base_heights`; sparse edit blocks are encoded from `snapshot.edit_layers`; composed component/min-max/LOD blocks are caches and may be regenerated. Reject save when either immutable source pointer is null.

- [ ] **Step 8: Implement defensive generation-one load**

Validate file bounds before every allocation or seek. Reject block sizes that exceed file length, decoded component sizes that do not match layout, duplicate live block keys, invalid Layer IDs, and more than eight material layers. Decode into a mutable snapshot and publish it as const only after the entire selected index succeeds.

- [ ] **Step 9: Run the GREEN container suite**

```bat
RunTests.bat Debug --test-case="Terrain container*"
```

Expected: generation-one round trip and CRC corruption cases pass.

- [ ] **Step 10: Commit the initial container**

```bat
git add project/src/engine/Function/Asset/TerrainContainerFormat.h project/src/engine/Function/Asset/TerrainContainerFormat.cpp project/src/engine/Function/Asset/TerrainContainer.h project/src/engine/Function/Asset/TerrainContainer.cpp project/src/tests/Terrain/terrain_container_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add versioned terrain container"
```

## Task 7: Add append-only generations, crash recovery, and optimize

**Files:**

- Modify: `project/src/engine/Function/Asset/TerrainContainer.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainContainerFormat.cpp`
- Modify: `project/src/tests/Terrain/terrain_container_tests.cpp`

- [ ] **Step 1: Add the RED incremental-size test**

Save generation 1, record file size, change one component, save generation 2 with one `TerrainDirtyComponentPayload`, and assert:

```cpp
CHECK(report.previous_generation == 1u);
CHECK(report.committed_generation == 2u);
CHECK(report.blocks_written < snapshot.components.size());
CHECK(size_after_generation_2 > size_after_generation_1);
CHECK(size_after_generation_2 - size_after_generation_1 < size_after_generation_1);
```

Reload and verify the changed component is generation 2 while unchanged logical data comes from generation 1 records.

- [ ] **Step 2: Add RED descriptor recovery tests**

Produce two valid generations, then create three byte-for-byte copies:

- Corrupt the newer descriptor CRC: load must return `RecoveredPreviousGeneration` and generation 1.
- Truncate the file halfway through the newer appended index without changing either descriptor: load must select generation 1.
- Corrupt both descriptors: load must return `Corrupt`, publish no snapshot, and preserve a non-empty diagnostic.

Perform corruption by opening copies with `std::fstream`, seeking to fixed offsets from `FileHeaderDisk`, and flipping exactly one byte. No test-only production hook is introduced.

- [ ] **Step 3: Add the RED failed-save preservation test**

After a valid generation 1, pass a dirty payload whose component generation does not match the snapshot generation. Require `InvalidData`, byte-for-byte unchanged file contents, and successful reload of generation 1. This proves validation failures do not touch the committed asset.

- [ ] **Step 4: Add the RED optimize test**

Create at least four generations changing the same component, call `optimize_terrain_container`, require the file shrinks, the logical snapshot remains equal, and a second optimize produces the same logical result without increasing size.

- [ ] **Step 5: Run the RED recovery cases**

```bat
RunTests.bat Debug --test-case="Terrain container incremental*"
RunTests.bat Debug --test-case="Terrain container recovery*"
RunTests.bat Debug --test-case="Terrain container optimize*"
```

Expected: the new cases fail because only generation-one behavior exists.

- [ ] **Step 6: Implement append-before-commit**

Load the current live index, replace only keys named by valid dirty payloads, append new payloads and a complete merged index, flush, and overwrite the older/invalid descriptor slot. Never overwrite a live payload. Reject `content_generation <= previous_generation` and mismatched payload generations before opening the file for write.

- [ ] **Step 7: Implement two-slot selection**

Validate each descriptor independently: generation non-zero, index range within file, exact index CRC, record count bounded by index size, all record ranges valid. Choose the highest valid generation. If the higher descriptor exists but is invalid and the lower is valid, return `RecoveredPreviousGeneration` and set the report flag.

- [ ] **Step 8: Implement atomic optimize**

Write only live blocks to `<name>.optimize.tmp` without changing the logical content generation, validate that temporary file by calling `load_terrain_container`, flush it, then replace the source using Windows `ReplaceFileW` when the destination exists and `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` otherwise. On failure, leave the source untouched and remove only the temporary file.

- [ ] **Step 9: Run all container tests GREEN**

```bat
RunTests.bat Debug --test-case="Terrain container*"
```

Expected: initial, incremental, recovery, failed-save, and optimize cases pass.

- [ ] **Step 10: Commit resilient generations**

```bat
git add project/src/engine/Function/Asset/TerrainContainer.cpp project/src/engine/Function/Asset/TerrainContainerFormat.cpp project/src/tests/Terrain/terrain_container_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): recover incremental terrain generations"
```

## Task 8: Add format-neutral import, crop/resample, and RAW streaming

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainImport.h`
- Create: `project/src/engine/Function/Asset/TerrainImport.cpp`
- Create: `project/src/engine/Function/Asset/TerrainRawCodec.cpp`
- Create: `project/src/tests/Terrain/terrain_import_tests.cpp`

- [ ] **Step 1: Add RED RAW round-trip cases**

Write a 5 x 3 R16 RAW fixture with values `{0, 1, 32768, 65534, 65535}` repeated by row. Import little-endian and big-endian forms, with X and Z flips independently. Export and re-import R16 and R32F. Assert R16 error is no larger than `height_range / 65535` and R32F is byte-stable for finite source values. Exercise `FinalComposedHeight`, `BaseHeight`, one `HeightEditLayer`, and one `MaterialWeightLayer`, including invalid layer/index rejection before destination creation.

- [ ] **Step 2: Add RED size-policy and cancellation cases**

Require:

- mismatched input with `Reject` returns `InvalidDimensions`;
- explicit center crop returns the expected source subrectangle;
- deterministic Catmull-Rom gives identical output bytes on two runs;
- cancellation after the first processed row returns `Cancelled` and does not publish a snapshot or final output file;
- estimated peak memory above 1 GiB returns `MemoryLimitExceeded` before allocation.

- [ ] **Step 3: Run the RED import suite**

```bat
RunTests.bat Debug --test-case="Terrain import RAW*"
```

Expected: build fails because `TerrainImport.h` does not exist.

- [ ] **Step 4: Add the format-neutral public API**

Create `TerrainImport.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class TerrainHeightFileFormat : uint8_t
	{
		RawR16 = 0,
		RawR32F,
		Png,
		Exr
	};

	enum class TerrainByteOrder : uint8_t
	{
		LittleEndian = 0,
		BigEndian
	};

	enum class TerrainResizePolicy : uint8_t
	{
		Reject = 0,
		Crop,
		CatmullRom
	};

	enum class TerrainExportSource : uint8_t
	{
		FinalComposedHeight = 0,
		BaseHeight,
		HeightEditLayer,
		MaterialWeightLayer
	};

	enum class TerrainImportResult : uint8_t
	{
		Success = 0,
		Cancelled,
		InvalidArguments,
		InvalidDimensions,
		UnsupportedFormat,
		DecodeFailure,
		EncodeFailure,
		MemoryLimitExceeded,
		IoFailure
	};

	class ASH_API TerrainCancellationToken
	{
	public:
		TerrainCancellationToken();
		void cancel();
		bool is_cancelled() const;
	private:
		std::shared_ptr<std::atomic<bool>> m_cancelled{};
	};

	struct TerrainHeightImportDesc
	{
		std::filesystem::path source_path{};
		TerrainHeightFileFormat format = TerrainHeightFileFormat::RawR16;
		TerrainGridLayout target_layout{};
		TerrainHeightMapping height_mapping{};
		uint32_t source_width = 0;
		uint32_t source_height = 0;
		TerrainByteOrder byte_order = TerrainByteOrder::LittleEndian;
		TerrainResizePolicy resize_policy = TerrainResizePolicy::Reject;
		bool flip_x = false;
		bool flip_z = false;
		std::string exr_channel{};
		uint64_t peak_memory_limit_bytes = 1024ull * 1024ull * 1024ull;
		TerrainCancellationToken cancellation{};
	};

	struct TerrainHeightExportDesc
	{
		std::filesystem::path destination_path{};
		TerrainHeightFileFormat format = TerrainHeightFileFormat::RawR16;
		TerrainExportSource source = TerrainExportSource::FinalComposedHeight;
		TerrainLayerId source_layer_id{};
		uint32_t material_layer_index = 0;
		TerrainByteOrder byte_order = TerrainByteOrder::LittleEndian;
		std::string exr_channel = "Y";
		TerrainCancellationToken cancellation{};
	};

	struct TerrainImportReport
	{
		uint32_t source_width = 0;
		uint32_t source_height = 0;
		uint32_t source_bits_per_sample = 0;
		std::vector<std::string> warnings{};
	};

	ASH_API auto import_terrain_height(
		TerrainAssetId asset_id,
		const TerrainHeightImportDesc& desc,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainImportReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainImportResult;

	ASH_API auto import_terrain_height_to_container(
		TerrainAssetId asset_id,
		const TerrainHeightImportDesc& desc,
		const std::filesystem::path& destination_path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainImportReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainImportResult;

	ASH_API auto export_terrain_height(
		const TerrainAssetSnapshot& snapshot,
		const TerrainHeightExportDesc& desc,
		std::string* out_error = nullptr) -> TerrainImportResult;
}
```

- [ ] **Step 5: Implement checked memory and deterministic resize dispatch**

Perform all `width * height * bytes_per_sample` calculations with checked `uint64_t` multiplication. Reject before allocation when the peak estimate exceeds the configured limit. Crop is explicit center crop with lower-coordinate bias for odd differences. Catmull-Rom uses double-precision coefficients, clamps source coordinates at edges, and iterates output rows then columns deterministically.

Export source extraction is format-independent: final height reads composed components, Base reads and decodes `base_heights`, height-layer export requires an existing `source_layer_id`, and material-weight export requires `material_layer_index < 8` and writes normalized scalar weights in `[0,1]`. Missing layers and invalid indices return `InvalidArguments` before opening the destination.

- [ ] **Step 6: Implement RAW row streaming**

Read/write one row at a time, endian-swap explicitly, apply X/Z flips through source-row/source-column selection, and check cancellation between rows. Export to `<destination>.tmp`; flush and rename only after all rows succeed. On failure or cancellation, remove the temporary file.

`import_terrain_height_to_container` imports into an immutable snapshot, saves `<destination>.import.tmp` with `save_terrain_container_incremental`, reloads that temporary container for complete CRC/index validation, then atomically renames it to the destination. It publishes `out_snapshot` only after rename succeeds. Cancellation, decode failure, validation failure, or rename failure removes the temporary file and leaves any existing destination untouched.

- [ ] **Step 7: Run RAW tests GREEN**

```bat
RunTests.bat Debug --test-case="Terrain import RAW*"
```

Expected: RAW round trip, flips, resize policy, cancellation, and memory-limit cases pass.

- [ ] **Step 8: Commit RAW import/export**

```bat
git add project/src/engine/Function/Asset/TerrainImport.h project/src/engine/Function/Asset/TerrainImport.cpp project/src/engine/Function/Asset/TerrainRawCodec.cpp project/src/tests/Terrain/terrain_import_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): import and export RAW heights"
```

## Task 9: Add exact 8/16-bit grayscale PNG through WIC

**Files:**

- Create: `project/src/engine/Function/Asset/TerrainPngCodecWin.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainImport.cpp`
- Modify: `project/src/engine/premake5.lua`
- Modify: `project/src/tests/Terrain/terrain_import_tests.cpp`

- [ ] **Step 1: Add RED PNG precision tests**

Export a 16-bit grayscale PNG from a small gradient, import it, and require R16-bounded error. Add a generated 8-bit grayscale PNG and require success plus the exact warning text `8-bit PNG height source reduces terrain precision.` in `TerrainImportReport`.

- [ ] **Step 2: Add the exact WIC negotiation source-contract assertion**

In the PNG doctest, read `project/src/engine/Function/Asset/TerrainPngCodecWin.cpp` and require the source contains all three tokens below. This makes removal of the mandatory post-`SetPixelFormat` check a failing contract test even though installed WIC codecs normally accept the requested format.

```cpp
CHECK(source.find("SetPixelFormat") != std::string::npos);
CHECK(source.find("GUID_WICPixelFormat16bppGray") != std::string::npos);
CHECK(source.find("IsEqualGUID") != std::string::npos);
```

- [ ] **Step 3: Run the RED PNG cases**

```bat
RunTests.bat Debug --test-case="Terrain import PNG*"
```

Expected: PNG cases fail with `UnsupportedFormat` because WIC dispatch is absent.

- [ ] **Step 4: Link WIC and COM once for all Windows configurations**

In `project/src/engine/premake5.lua`, add a common Windows filter outside Debug/Release-specific blocks:

```lua
filter "system:windows"
	links
	{
		"windowscodecs",
		"ole32",
	}

filter {}
```

Do not edit `project/src/tests/premake5.lua`.

- [ ] **Step 5: Implement COM MTA lifetime and exact pixel negotiation**

In `TerrainPngCodecWin.cpp`, call `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the executing worker and pair a successful initialization with `CoUninitialize`. Treat `RPC_E_CHANGED_MODE` as failure because this worker contract requires MTA. For 16-bit export call `IWICBitmapFrameEncode::SetPixelFormat`, then compare the returned GUID to `GUID_WICPixelFormat16bppGray`; any other GUID returns `EncodeFailure` before writing the final path.

- [ ] **Step 6: Keep height samples linear**

Decode `GUID_WICPixelFormat8bppGray` and `GUID_WICPixelFormat16bppGray` as integer scalar values. Do not create a color transform, read an sRGB profile, apply gamma, exposure, or premultiplication. Reject color PNG formats rather than implicitly converting RGB luminance.

- [ ] **Step 7: Generate and build after premake change**

```bat
generate_vs2022.bat
build_tests.bat Debug
```

Expected: generation and Tests build exit `0`; Engine links `windowscodecs.lib` and `ole32.lib`.

- [ ] **Step 8: Run PNG tests GREEN**

```bat
RunTests.bat Debug --test-case="Terrain import PNG*"
```

Expected: 8-bit warning, 16-bit round trip, and negotiation-failure tests pass.

- [ ] **Step 9: Commit WIC PNG support**

```bat
git add project/src/engine/Function/Asset/TerrainPngCodecWin.cpp project/src/engine/Function/Asset/TerrainImport.h project/src/engine/Function/Asset/TerrainImport.cpp project/src/engine/premake5.lua project/src/tests/Terrain/terrain_import_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add linear PNG height codec"
```

## Task 10: Vendor TinyEXR v3.2.0 and add isolated EXR import/export

**Files:**

- Create: `project/thirdparty/tinyexr/tinyexr.h`
- Create: `project/thirdparty/tinyexr/deps/miniz/miniz.h`
- Create: `project/thirdparty/tinyexr/deps/miniz/miniz.c`
- Create: `project/thirdparty/tinyexr/LICENSE`
- Create: `project/thirdparty/tinyexr/VERSION.md`
- Create: `project/src/engine/Function/Asset/TerrainExrCodec.cpp`
- Modify: `project/src/engine/Function/Asset/TerrainImport.cpp`
- Modify: `project/src/engine/premake5.lua`
- Modify: `project/src/tests/Terrain/terrain_import_tests.cpp`

- [ ] **Step 1: Add RED half/float channel tests**

Create EXRs at runtime with channels `Y` and `Height`. Require explicit channel selection, half and float round trips, linear negative/positive height preservation, rejection of a missing channel, cancellation before final rename, malformed-file failure, and the 1 GiB limit before a contiguous EXR export buffer is allocated.

- [ ] **Step 2: Run the RED EXR cases**

```bat
RunTests.bat Debug --test-case="Terrain import EXR*"
```

Expected: EXR cases fail with `UnsupportedFormat`.

- [ ] **Step 3: Fetch and verify the approved upstream revision outside the repository**

Run from PowerShell:

```powershell
$source = Join-Path $env:TEMP ("tinyexr-v3.2.0-" + [guid]::NewGuid().ToString('N'))
git clone --depth 1 --branch v3.2.0 https://github.com/syoyo/tinyexr.git $source
git -C $source rev-parse --short HEAD
```

Expected: the short commit begins with approved release commit `6f470c9`. Stop if it does not match.

- [ ] **Step 4: Vendor only approved files**

Copy `tinyexr.h`, `LICENSE`, and the miniz header/source used by the approved codec into the exact paths listed above. Do not vendor examples, tests, CMake files, zlib, OpenEXR, or build artifacts.

- [ ] **Step 5: Record immutable provenance**

Compute SHA-256 for every vendored source/license file:

```powershell
Get-FileHash -Algorithm SHA256 project/thirdparty/tinyexr/tinyexr.h
Get-FileHash -Algorithm SHA256 project/thirdparty/tinyexr/deps/miniz/miniz.h
Get-FileHash -Algorithm SHA256 project/thirdparty/tinyexr/deps/miniz/miniz.c
Get-FileHash -Algorithm SHA256 project/thirdparty/tinyexr/LICENSE
```

Create `VERSION.md` with version `v3.2.0`, commit `6f470c9`, source URL, the four emitted hashes, TinyEXR BSD-3-Clause, miniz MIT/public-domain terms, vendored date `2026-07-13`, and the upgrade rule “replace approved files and update every hash”. Do not leave a blank hash or provisional marker.

- [ ] **Step 6: Add TinyEXR/miniz to Engine only**

In `project/src/engine/premake5.lua`, add:

```lua
files
{
	thirdparty .. "/tinyexr/deps/miniz/miniz.c",
}

includedirs
{
	thirdparty .. "/tinyexr",
	thirdparty .. "/tinyexr/deps/miniz",
}
```

Keep TinyEXR out of Tests, Editor, Sandbox, Scene, Render, and Graphics include paths.

- [ ] **Step 7: Isolate implementation macros in one translation unit**

At the top of `TerrainExrCodec.cpp` use:

```cpp
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_ZLIB 0
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
```

No TinyEXR declaration or type appears in `TerrainImport.h` or `TerrainData.h`. Copy decoded channel values into engine-owned `std::vector<float>` before freeing TinyEXR memory.

- [ ] **Step 8: Enforce channel, allocation, cancellation, and temp-file rules**

List channels before decoding and require exact user-selected channel match. Accept only half/float input for Terrain. Check dimensions and peak bytes before image allocation. Write export to a temporary path, check cancellation before encode and before rename, free every TinyEXR error string with `FreeEXRErrorMessage`, and delete the temporary file on all failures.

- [ ] **Step 9: Fresh-generate and build the full matrix**

```bat
generate_vs2022.bat
build_editor.bat Debug
build_sandbox.bat Debug
build_editor.bat Release
build_sandbox.bat Release
```

Expected: all five commands exit `0`; no TinyEXR symbol or type is referenced by Editor/Sandbox source.

- [ ] **Step 10: Run EXR and full import suites GREEN**

```bat
RunTests.bat Debug --test-case="Terrain import EXR*"
RunTests.bat Debug --test-case="Terrain import*"
```

Expected: EXR half/float/channel/malformed/cancel/memory cases and all RAW/PNG cases pass.

- [ ] **Step 11: Commit the approved dependency and EXR codec**

```bat
git add project/thirdparty/tinyexr project/src/engine/Function/Asset/TerrainExrCodec.cpp project/src/engine/Function/Asset/TerrainImport.cpp project/src/engine/premake5.lua project/src/tests/Terrain/terrain_import_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): add pinned EXR height codec"
```

Expected: cached diff contains only approved TinyEXR/miniz files, Asset codec changes, engine premake, and Terrain tests.

## Task 11: Integrate exact Terrain snapshot load, publish, and invalidation into AssetDatabase

**Files:**

- Modify: `project/src/engine/Function/Asset/AssetDatabase.h`
- Modify: `project/src/engine/Function/Asset/AssetDatabase.cpp`
- Modify: `project/src/engine/Function/Scene/TerrainQuery.h`
- Modify: `project/src/engine/Function/Scene/TerrainQuery.cpp`
- Create: `project/src/tests/Terrain/terrain_asset_database_tests.cpp`

- [ ] **Step 1: Add the RED asset detection/load test**

Save a small valid `terrain/test.AshTerrain`, call `AssetDatabase::create(root)`, and require:

```cpp
const AshEngine::AssetInfo* info = database.find_asset_by_path("terrain/test.AshTerrain");
REQUIRE(info != nullptr);
CHECK(info->type == AshEngine::AssetType::Terrain);

std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
REQUIRE(database.load_terrain_by_id(info->id, snapshot));
REQUIRE(snapshot != nullptr);
CHECK(snapshot->asset_id == info->id);
CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Loaded);
```

- [ ] **Step 2: Add RED async/cache tests**

Require sync and async loads return the same immutable pointer while cached. Require two async requests return shared results and a corrupt file produces null plus `AssetLoadState::Failed` and a non-empty asset error.

- [ ] **Step 3: Add RED publish/invalidate ordering tests**

Load disk generation 1/revision 0, publish immutable generation 1/revision 1, then generation 2/revision 0, and require subsequent sync/async loads return the newest tuple. Publishing generation 1/revision 0 or republishing generation 1/revision 1 must fail and retain the newer snapshot. Exact invalidation must remove only this Terrain cache, reset its load state to `Unloaded`, and make the next load read disk generation 1. Rewrite a previously corrupt file, invalidate it, and require a successful reload.

Add a source-contract case that reads `AssetDatabase.cpp` and requires `terrain_load_serial_by_id`, `captured_load_serial`, and `current_load_serial == captured_load_serial` to appear in the Terrain worker completion path before `terrain_cache[asset_id]`. These locked local names make the stale-worker guard mechanically reviewable without a production sleep/delay seam.

- [ ] **Step 4: Run the RED AssetDatabase suite**

```bat
RunTests.bat Debug --test-case="Terrain AssetDatabase*"
```

Expected: compile fails because `AssetType::Terrain` and Terrain load/publication methods are absent.

- [ ] **Step 5: Append Terrain without renumbering existing AssetType values**

In `AssetDatabase.h`, append `Terrain` after `Binary`:

```cpp
	enum class AssetType : uint8_t
	{
		Unknown = 0,
		Directory,
		Scene,
		Shader,
		Texture,
		Mesh,
		Model,
		Prefab,
		Material,
		Text,
		Binary,
		Terrain
	};
```

In `detect_asset_type`, case-normalize `.ashterrain` and return `Terrain` before the Binary fallback.

- [ ] **Step 6: Add the exact AssetDatabase API**

Add to `AssetDatabase.h`:

```cpp
		bool load_terrain_by_id(
			TerrainAssetId id,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot);
		bool load_terrain_by_path(
			const std::filesystem::path& path,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot);
		std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> load_terrain_by_id_async(
			TerrainAssetId id);
		std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> load_terrain_by_path_async(
			const std::filesystem::path& path);
		bool publish_terrain_snapshot(
			TerrainAssetId id,
			std::shared_ptr<const TerrainAssetSnapshot> snapshot);
		bool invalidate_terrain_snapshot(TerrainAssetId id);
```

Include `TerrainData.h` directly. `TerrainAssetId` and `AssetId` are both `uint64_t`; do not add overloads that differ only by these aliases.

- [ ] **Step 7: Add const cache and in-flight maps**

In `AssetDatabase::Impl` add:

```cpp
struct InflightTerrainLoad
{
	uint64_t serial = 0;
	std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> future{};
};

std::unordered_map<TerrainAssetId, std::shared_ptr<const TerrainAssetSnapshot>> terrain_cache{};
std::unordered_map<TerrainAssetId, InflightTerrainLoad> inflight_terrain_loads{};
std::unordered_map<TerrainAssetId, uint64_t> terrain_load_serial_by_id{};
```

Clear all three in every existing `refresh()` success/failure cache-reset path. Copy `AssetInfo`, relative path, and ID before releasing the mutex; never retain pointers returned by `find_asset_by_*` across a refresh.

- [ ] **Step 8: Implement sync and async Terrain load**

Follow existing Mesh async ownership: cache hit returns a ready shared future, in-flight hit returns the existing future, and the first request increments `terrain_load_serial_by_id[id]`, stores it in a local named `captured_load_serial`, records that serial with the shared future, marks `Loading`, and dispatches `load_terrain_container`. At worker completion, read a local named `current_load_serial` and update cache/load-state and erase the in-flight entry only under `current_load_serial == captured_load_serial` and matching in-flight serial. A stale worker resolves its promise to the currently published cache entry, or null if invalidation left no entry; it never writes cache or load-state. Set `snapshot->asset_id` through a mutable local copy before publication; never `const_cast` a published snapshot.

- [ ] **Step 9: Implement exact publication/invalidation**

`publish_terrain_snapshot` requires an indexed Terrain ID, non-null/non-failed snapshot, matching `asset_id`, and a lexicographically newer `(content_generation, residency_revision)` tuple than the cached snapshot. It increments the per-ID load serial, replaces only `terrain_cache[id]`, erases only `inflight_terrain_loads[id]`, and marks this ID `Loaded`. This allows later phases to publish newly resident components without inventing a new content generation while still rejecting stale worker results.

`invalidate_terrain_snapshot` requires an indexed Terrain ID, increments its load serial, erases only Terrain cache/in-flight entries for that ID, and sets its load state to `Unloaded` with an empty error. It does not call `refresh()` and does not disturb Mesh/Model/Material/AshAsset caches. Add a deterministic source-contract assertion that the worker completion compares its captured serial before any Terrain cache or load-state mutation; do not add a production delay hook.

- [ ] **Step 10: Implement prefetch without blocking**

Add the final declaration to `TerrainQuery.h`:

```cpp
ASH_API auto prefetch_query_region(
	AssetDatabase& database,
	TerrainAssetId asset_id,
	const TerrainSampleRect& sample_region) -> TerrainQueryStatus;
```

The implementation validates the sample rectangle, starts `load_terrain_by_id_async`, then calls `wait_for(std::chrono::seconds(0))`. Return `Pending` if not ready, `Failed` for null/failed results, `Outside` for an empty/out-of-layout region, and `Ready` for a valid loaded snapshot. Do not call `.get()` until the future is ready and do not wait on a worker.

- [ ] **Step 11: Run AssetDatabase and query tests GREEN**

```bat
RunTests.bat Debug --test-case="Terrain AssetDatabase*"
RunTests.bat Debug --test-case="Terrain query*"
```

Expected: type detection, sync/async cache identity, stale publication rejection, exact invalidation, failed-load recovery, and non-blocking prefetch pass.

- [ ] **Step 12: Run all Terrain tests before commit**

```bat
RunTests.bat Debug --test-case="Terrain*"
```

Expected: every Terrain test case passes with zero failed assertions.

- [ ] **Step 13: Commit AssetDatabase integration**

```bat
git add project/src/engine/Function/Asset/AssetDatabase.h project/src/engine/Function/Asset/AssetDatabase.cpp project/src/engine/Function/Scene/TerrainQuery.h project/src/engine/Function/Scene/TerrainQuery.cpp project/src/tests/Terrain/terrain_asset_database_tests.cpp
git diff --cached --check
git commit -m "feat(terrain): publish immutable asset snapshots"
```

## Task 12: Update Phase 1 specs and run the complete asset-core gate

**Files:**

- Create: `docs/specs/features/terrain.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/specs/modules/scene.md`
- Modify: `docs/CODEBASE_MAP.md`
- Modify: `docs/VERIFY.md`

- [ ] **Step 1: Write the current-state Terrain feature spec**

Document only implemented Phase 1 behavior: fixed production layout, small test layouts, global sample ownership, immutable snapshots, sparse layers, brushes, local query status, container generation/recovery rules, formats, and AssetDatabase APIs. Mark Scene component, rendering, Editor, and performance integration as not implemented; do not describe planned API as current behavior.

- [ ] **Step 2: Update Asset and Scene module specs**

In `asset.md`, add the exact Terrain files and sync/async/publish/invalidate contracts. In `scene.md`, list `TerrainQuery.h/.cpp` as snapshot-local Phase 1 query logic and explicitly state that world-space Scene adapters and Scene v6 are not yet present.

- [ ] **Step 3: Update code map and verification matrix**

Add Terrain Asset entry points to `CODEBASE_MAP.md`. Add a `Terrain Asset / CPU logic` row to `VERIFY.md` requiring Debug/Release tests, ArchGate, fresh build after dependency changes, and dual-backend readiness smoke because AssetDatabase is shared by Editor/Sandbox.

- [ ] **Step 4: Check documentation and source diffs**

```bat
git diff --check
rg -n "not implemented" docs/specs/features/terrain.md docs/specs/modules/scene.md
```

Expected: `git diff --check` exits `0`; `rg` prints only the explicit Phase 2/3 exclusions required by Steps 1-2.

- [ ] **Step 5: Fresh-generate and build all required targets**

```bat
generate_vs2022.bat
build_editor.bat Debug
build_sandbox.bat Debug
build_editor.bat Release
build_sandbox.bat Release
```

Expected: all five commands exit `0` with TinyEXR/miniz compiled into Engine and no missing WIC/COM symbols.

- [ ] **Step 6: Run Debug and Release unit gates**

```bat
RunTests.bat Debug
RunTests.bat Release
```

Expected: all test cases and assertions pass in both configurations.

- [ ] **Step 7: Run architecture and dual-backend lifecycle gates**

```bat
RunArchGate.bat
run.bat all Debug --smoke-test-seconds=120
```

Expected: ArchGate exits `0` with no new boundary violation; Editor and Sandbox readiness exit `0` on both backends. No RenderGate is required because this phase changes no rendered frame data or shader.

- [ ] **Step 8: Run the AI plan audit**

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

Expected: plan validation exits `0` and reports no missing required verification for Asset/premake changes.

- [ ] **Step 9: Confirm the forbidden mixed-owner file is untouched**

```bat
git diff --name-only HEAD | findstr /I /X "project/src/tests/premake5.lua"
```

Expected: `findstr` prints nothing. If it prints the path, stop and remove only this task's accidental hunk without disturbing existing gizmo work.

- [ ] **Step 10: Commit Phase 1 documentation and verification evidence**

```bat
git add docs/specs/features/terrain.md docs/specs/modules/asset.md docs/specs/modules/scene.md docs/CODEBASE_MAP.md docs/VERIFY.md
git diff --cached --check
git commit -m "docs(terrain): specify phase one asset core"
```

- [ ] **Step 11: Verify final Phase 1 history and cleanliness**

```bat
git status --short
git log --oneline --max-count=12
git diff HEAD~12..HEAD -- project/src/tests/premake5.lua
```

Expected: the implementation worktree has no uncommitted Phase 1 files; recent history contains focused Terrain commits; the final command has no Terrain-produced diff.

## Phase 1 completion contract

Phase 1 is complete only when all of the following are true:

- The five locked cross-plan names compile and are exported where Tests.exe calls non-inline functions.
- No published `TerrainAssetSnapshot` or `TerrainComponentSnapshot` is mutated.
- A shared height sample has one persistent owner and every dependent component snapshot is dirtied.
- Eight quantized weights always sum to exactly 255.
- Equivalent stroke paths at different input sampling rates produce identical output.
- Terrain ray queries use min/max traversal and exact triangle intersection, not fixed stepping.
- A partial/corrupt newest `.AshTerrain` generation recovers the preceding generation; two corrupt descriptors fail without publishing data.
- Invalid save input and failed optimize replacement leave the preceding generation readable.
- RAW, PNG, and EXR round trips meet their declared precision, cancellation, linear-value, and memory contracts.
- TinyEXR provenance, commit, hashes, and both TinyEXR/miniz licenses are recorded.
- `AssetDatabase` can publish a newer immutable snapshot and invalidate one Terrain ID without a global refresh.
- Debug/Release tests, fresh builds, ArchGate, Vulkan/DX12 lifecycle smoke, and AIDevDoctor pass.
- No Scene v6, render, RHI, shader, Editor, golden, perf baseline, or mixed-owner test premake change appears in the Phase 1 diff.

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-13-terrain-phase-1-asset-core.md`. Execute it with `superpowers:subagent-driven-development` for fresh task agents and two-stage review between tasks, or with `superpowers:executing-plans` for inline batches with checkpoints. The parent Terrain plan chooses the execution mode for all phase plans consistently.
