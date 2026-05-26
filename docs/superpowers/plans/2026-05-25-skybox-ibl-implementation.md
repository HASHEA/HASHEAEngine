# Skybox IBL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a single-active-scene EnvironmentComponent, `.ashibl` uncompressed IBL assets, runtime/offline IBL baking, and deferred skybox + PBR IBL rendering to AshEngine.

**Architecture:** Scene owns authoring state through `EnvironmentComponent`; `RenderScene` snapshots one active environment into `VisibleRenderFrame`. Asset and bake code produce/consume one `.ashibl` cooked contract, while `SceneRenderer` only binds `EnvironmentMapRuntimeResource` through dedicated `EnvironmentLightingPass` and `SkyBackgroundPass`. First implementation uses an Engine CPU reference baker so runtime generation and offline generation share one deterministic path before any GPU readback path is required.

**Tech Stack:** C++17, HLSL, AshEngine Function/Scene, Function/Render, RenderGraph, RenderDevice/Renderer, DXC shader reflection, Vulkan + DX12 shared RHI validation, Engine self-test, Tracy via `Base/hprofiler.h`.

---

## Scope Check

The approved spec spans Scene authoring, cooked asset format, texture upload, baker API, renderer passes, shaders, docs, and validation. These are tightly coupled by one runtime contract, so this remains one implementation plan, but each task below produces a self-contained and testable vertical slice. Do not start render pass work before the Scene snapshot, `.ashibl` contract, and cubemap upload tasks compile and pass self-tests.

## File Map

- Create: `project/src/engine/Function/Render/EnvironmentMapAsset.h`
  - `.ashibl` public constants, payload structs, cooked data structs, read/write API, CPU payload helpers.
- Create: `project/src/engine/Function/Render/EnvironmentMapAsset.cpp`
  - `.ashibl` validation, metadata JSON serialization, payload table serialization, uncompressed payload read/write.
- Create: `project/src/engine/Function/Render/EnvironmentMapBaker.h`
  - Engine-facing bake desc, report, CPU reference baker facade, runtime/offline entry points.
- Create: `project/src/engine/Function/Render/EnvironmentMapBaker.cpp`
  - HDR/equirect sampling, cubemap generation, irradiance convolution, GGX prefilter, BRDF LUT generation.
- Create: `project/src/engine/Function/Render/EnvironmentLightingPass.h`
  - Deferred IBL lighting pass facade.
- Create: `project/src/engine/Function/Render/EnvironmentLightingPass.cpp`
  - Environment lighting graphics program, sampler, graph pass registration, IBL binding.
- Create: `project/src/engine/Function/Render/SkyBackgroundPass.h`
  - Sky background pass facade.
- Create: `project/src/engine/Function/Render/SkyBackgroundPass.cpp`
  - Sky graphics program, sampler, graph pass registration, depth background test binding.
- Create: `project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli`
  - Shared environment constants, cubemap rotation, Fresnel and BRDF LUT helpers.
- Create: `project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl`
  - Fullscreen deferred IBL diffuse/specular accumulation.
- Create: `project/src/engine/Shaders/Deferred/SkyBackground.hlsl`
  - Fullscreen HDR sky background pass.
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
  - Split lighting accumulation and composite registration so environment lighting can run between them.
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
  - Keep existing lighting/composite code but expose separate graph registration methods.
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
  - Add `EnvironmentComponent` and `SceneComponentType::Environment`.
- Modify: `project/src/engine/Function/Scene/Scene.h`
  - Add Entity environment component methods, `SceneEnvironmentExtractionDesc`, extraction API, environment version getter.
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
  - Add descriptors, serialization, component read/write, active environment extraction, version updates.
- Modify: `project/src/engine/Function/Asset/AshAssetSerializer.cpp`
  - Preserve environment component in `.ashasset` prefab/model serialization.
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
  - Add `RG16_SFLOAT`, cube upload descriptors, `create_texture_cube()`, validation helper.
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
  - Implement cube texture creation and upload through shared RHI.
- Modify: `project/src/engine/Function/Render/Renderer.h`
  - Expose `create_texture_cube()`.
- Modify: `project/src/engine/Function/Render/Renderer.cpp`
  - Forward cube texture creation to `RenderDevice`.
- Modify: `project/src/engine/Function/Render/RenderFormatUtils.h`
  - Ensure `RG16_SFLOAT` row pitch and block info are public.
- Modify: `project/src/engine/Function/Render/RenderFormatUtils.cpp`
  - Map `RG16_SFLOAT` to/from RHI and row pitch.
- Modify: `project/src/engine/Graphics/RHICommon.h`
  - Confirm shared `ASH_FORMAT_R16G16_SFLOAT` path is available through public format mapping.
- Modify: `project/src/engine/Function/Render/RenderAssetManager.h`
  - Add environment asset request/cache/fallback API.
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
  - Load `.ashibl`, create GPU cubemaps/LUT, dispatch runtime bake fallback, cache runtime resources.
- Modify: `project/src/engine/Function/Render/RenderScene.h`
  - Add `VisibleEnvironmentData`, optional environment on `VisibleRenderFrame`, cached environment snapshot.
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
  - Extract active environment and copy it into visible frames.
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`
  - Track `render_environment_version`, refresh environment snapshot without primitive rebuild.
- Modify: `project/src/engine/Function/Render/SceneRenderView.h`
  - Carry the prepared `EnvironmentMapRuntimeResource` for the current view submit.
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
  - Add environment lighting/debug refs if needed by pass handoff and a sky HDR output ref.
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
  - Own `EnvironmentLightingPass` and `SkyBackgroundPass`.
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
  - Initialize/shutdown passes, consume prepared environment resource, register graph passes between lighting/composite/tone-map.
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
  - Keep dynamic lighting independent; environment pass adds IBL through its own pass.
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
  - Add Scene environment, `.ashibl`, texture cube upload validation, CPU baker, graph contract tests.
- Modify: `docs/EngineDeveloperGuide.md`
  - Document EnvironmentComponent, `.ashibl`, baker, graph position, validation.
- Modify: `docs/RenderGraphAPISpec.md`
  - Update deferred graph example.
- Modify: `README.md`
  - Link spec and this plan; summarize current skybox/IBL implementation state.

---

### Task 1: Add EnvironmentComponent And Scene Extraction

**Files:**
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
- Modify: `project/src/engine/Function/Scene/Scene.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Function/Asset/AshAssetSerializer.cpp`
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-test for active environment selection**

In `project/src/engine/Base/EngineSelfTests.cpp`, add this test near `test_render_scene_extracts_light_snapshot()`:

```cpp
auto test_scene_extracts_single_active_environment() -> bool
{
	Scene scene{};
	scene.create("Environment Test");

	Entity inactive = scene.create_entity("Inactive Environment");
	EnvironmentComponent inactive_env{};
	inactive_env.active = false;
	inactive_env.ibl_asset_path = "assets/env/inactive.ashibl";
	inactive.add_environment_component(inactive_env);

	Entity first = scene.create_entity("First Environment");
	EnvironmentComponent first_env{};
	first_env.active = true;
	first_env.ibl_asset_path = "assets/env/first.ashibl";
	first_env.source_texture_path = "assets/env/first.hdr";
	first_env.intensity = 2.0f;
	first_env.rotation_degrees = 45.0f;
	first_env.visible_background = true;
	first_env.affect_lighting = true;
	first.add_environment_component(first_env);

	Entity second = scene.create_entity("Second Environment");
	EnvironmentComponent second_env{};
	second_env.active = true;
	second_env.ibl_asset_path = "assets/env/second.ashibl";
	second.add_environment_component(second_env);

	SceneEnvironmentExtractionDesc extracted{};
	if (!scene.extract_active_environment(extracted))
	{
		return report_self_test_failure("Scene environment extraction", "active environment was not extracted");
	}

	const bool ok =
		extracted.entity_id == first.get_id() &&
		extracted.ibl_asset_path == "assets/env/first.ashibl" &&
		extracted.source_texture_path == "assets/env/first.hdr" &&
		extracted.intensity == 2.0f &&
		extracted.rotation_degrees == 45.0f &&
		extracted.visible_background &&
		extracted.affect_lighting;
	return ok || report_self_test_failure("Scene environment extraction", "first active environment was not selected deterministically");
}
```

Register it in `run_engine_base_self_tests()` after the light snapshot test:

```cpp
all_passed = test_scene_extracts_single_active_environment() && all_passed;
```

Expected before implementation: compile fails because `EnvironmentComponent`, `add_environment_component()`, and `extract_active_environment()` do not exist.

- [ ] **Step 2: Write failing self-test for environment render version isolation**

Add this test near `test_scene_render_versions()`:

```cpp
auto test_scene_environment_version_isolated_from_primitives() -> bool
{
	Scene scene{};
	scene.create("Environment Version Test");
	Entity environment = scene.create_entity("Environment");

	const uint64_t primitive_before = scene.get_render_primitive_version();
	const uint64_t transform_before = scene.get_render_transform_version();
	const uint64_t light_before = scene.get_render_light_version();
	const uint64_t environment_before = scene.get_render_environment_version();

	EnvironmentComponent component{};
	component.active = true;
	component.ibl_asset_path = "assets/env/test.ashibl";
	environment.add_environment_component(component);

	const bool ok =
		scene.get_render_primitive_version() == primitive_before &&
		scene.get_render_transform_version() == transform_before &&
		scene.get_render_light_version() == light_before &&
		scene.get_render_environment_version() != environment_before;
	return ok || report_self_test_failure("Scene environment versions", "environment changes invalidated unrelated render versions");
}
```

Register it after `test_scene_render_versions()`:

```cpp
all_passed = test_scene_environment_version_isolated_from_primitives() && all_passed;
```

Expected before implementation: compile fails because `get_render_environment_version()` does not exist.

- [ ] **Step 3: Add `EnvironmentComponent` to Scene components**

In `project/src/engine/Function/Scene/SceneComponents.h`, add `Environment` to `SceneComponentType` after `Mesh` and add:

```cpp
struct EnvironmentComponent
{
	bool active = true;
	std::string ibl_asset_path{};
	std::string source_texture_path{};
	float intensity = 1.0f;
	float rotation_degrees = 0.0f;
	bool visible_background = true;
	bool affect_lighting = true;
};
```

Keep the struct plain CPU data. Do not add renderer resources or backend handles.

- [ ] **Step 4: Add Entity environment methods and Scene extraction declarations**

In `project/src/engine/Function/Scene/Scene.h`, add:

```cpp
struct ASH_API SceneEnvironmentExtractionDesc
{
	EntityId entity_id = 0;
	std::string ibl_asset_path{};
	std::string source_texture_path{};
	float intensity = 1.0f;
	float rotation_degrees = 0.0f;
	bool visible_background = true;
	bool affect_lighting = true;
};
```

Add methods to `Entity`:

```cpp
bool has_environment_component() const;
EnvironmentComponent get_environment_component() const;
bool add_environment_component(const EnvironmentComponent& component = {});
bool set_environment_component(const EnvironmentComponent& component);
bool remove_environment_component();
```

Add methods to `Scene`:

```cpp
bool extract_active_environment(SceneEnvironmentExtractionDesc& out_environment) const;
uint64_t get_render_environment_version() const;
```

- [ ] **Step 5: Implement environment version updates and descriptors**

In `project/src/engine/Function/Scene/Scene.cpp`, add `render_environment_version` to `SceneStorage` and add:

```cpp
static auto mark_scene_render_environment_modified(SceneStorage& storage) -> void
{
	mark_scene_storage_modified(storage);
	storage.render_environment_version = allocate_scene_change_version();
}
```

Extend component mutation helpers so `EnvironmentComponent` add/set/remove uses `mark_scene_render_environment_modified()`. Add property descriptors for all six fields:

```cpp
static ScenePropertyDesc k_environment_properties[] =
{
	{ "active", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(EnvironmentComponent, active)), static_cast<uint32_t>(sizeof(bool)), nullptr },
	{ "ibl_asset_path", ScenePropertyType::String, static_cast<uint32_t>(offsetof(EnvironmentComponent, ibl_asset_path)), static_cast<uint32_t>(sizeof(std::string)), nullptr },
	{ "source_texture_path", ScenePropertyType::String, static_cast<uint32_t>(offsetof(EnvironmentComponent, source_texture_path)), static_cast<uint32_t>(sizeof(std::string)), nullptr },
	{ "intensity", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(EnvironmentComponent, intensity)), static_cast<uint32_t>(sizeof(float)), nullptr },
	{ "rotation_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(EnvironmentComponent, rotation_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
	{ "visible_background", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(EnvironmentComponent, visible_background)), static_cast<uint32_t>(sizeof(bool)), nullptr },
	{ "affect_lighting", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(EnvironmentComponent, affect_lighting)), static_cast<uint32_t>(sizeof(bool)), nullptr }
};
```

Extend `k_scene_component_descs`, `Entity::has_component()`, `Entity::get_component_types()`, `read_component()`, and `write_component()`.

- [ ] **Step 6: Implement active environment extraction and serialization**

In `Scene::extract_active_environment()`, scan `m_impl->storage.entity_order` in order. Select the first valid active component:

```cpp
bool Scene::extract_active_environment(SceneEnvironmentExtractionDesc& out_environment) const
{
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
	out_environment = {};
	ASH_PROCESS_ERROR(is_valid());

	bool found_active = false;
	bool warned_multiple = false;
	for (EntityId id : m_impl->storage.entity_order)
	{
		Entity entity = find_entity(id);
		if (!entity.is_valid() || !entity.has_environment_component())
		{
			continue;
		}

		const EnvironmentComponent component = entity.get_environment_component();
		if (!component.active)
		{
			continue;
		}

		if (found_active)
		{
			if (!warned_multiple)
			{
				HLogWarn("Scene '{}': multiple active EnvironmentComponent entries found; using the first active environment.", get_name());
				warned_multiple = true;
			}
			continue;
		}

		out_environment.entity_id = id;
		out_environment.ibl_asset_path = component.ibl_asset_path;
		out_environment.source_texture_path = component.source_texture_path;
		out_environment.intensity = component.intensity;
		out_environment.rotation_degrees = component.rotation_degrees;
		out_environment.visible_background = component.visible_background;
		out_environment.affect_lighting = component.affect_lighting;
		found_active = true;
	}

	bResult = found_active;
	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

Extend scene JSON load/save with:

```json
"environment": {
  "active": true,
  "ibl_asset_path": "assets/env/studio.ashibl",
  "source_texture_path": "assets/env/studio.hdr",
  "intensity": 1.0,
  "rotation_degrees": 0.0,
  "visible_background": true,
  "affect_lighting": true
}
```

Extend `AshAssetSerializer.cpp` with the same fields for asset node serialization.

- [ ] **Step 7: Add render snapshot plumbing**

In `RenderScene.h`, add:

```cpp
struct ASH_API VisibleEnvironmentData
{
	EntityId entity_id = 0;
	std::string ibl_asset_path{};
	std::string source_texture_path{};
	float intensity = 1.0f;
	float rotation_degrees = 0.0f;
	bool visible_background = true;
	bool affect_lighting = true;
};
```

Add `std::optional<VisibleEnvironmentData> environment;` to `VisibleRenderFrame`, include `<optional>`, and add members to `RenderScene`:

```cpp
std::optional<VisibleEnvironmentData> m_environment{};
```

Add `bool rebuild_environment_from_scene(const Scene& scene);`. It calls `scene.extract_active_environment()` and fills `m_environment`.

- [ ] **Step 8: Track environment version in ScenePresentationSubsystem**

In `ScenePresentationSubsystem.cpp`, extend `Impl::SceneState` with `last_environment_version`. In `update_presentations()`, read:

```cpp
const uint64_t scene_environment_version = binding.scene->get_render_environment_version();
```

When rebuilding primitives, also refresh environment. Add an independent branch:

```cpp
else if (scene_state->last_environment_version != scene_environment_version)
{
	scene_state->render_scene_valid = scene_state->render_scene.rebuild_environment_from_scene(*binding.scene);
	scene_state->last_environment_version = scene_environment_version;
}
```

Ensure environment refresh does not call `rebuild_from_scene()` unless primitive version changed.

- [ ] **Step 9: Run self-tests**

Run:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: PASS for `Scene environment extraction` and `Scene environment versions`. If binary is stale, run:

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

- [ ] **Step 10: Commit**

```bash
git add project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.h project/src/engine/Function/Scene/Scene.cpp project/src/engine/Function/Asset/AshAssetSerializer.cpp project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Function/Render/ScenePresentationSubsystem.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add scene environment component snapshot"
```

---

### Task 2: Add .ashibl Cooked Data And Reader/Writer

**Files:**
- Create: `project/src/engine/Function/Render/EnvironmentMapAsset.h`
- Create: `project/src/engine/Function/Render/EnvironmentMapAsset.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-test for `.ashibl` round trip**

Add include:

```cpp
#include "Function/Render/EnvironmentMapAsset.h"
```

Add this test near texture decode tests:

```cpp
auto test_ashibl_round_trip_uncompressed_payloads() -> bool
{
	const std::filesystem::path path = engine_self_test_dir() / "round_trip.ashibl";

	EnvironmentMapCookedData data{};
	data.build_desc.source_texture_path = "assets/env/generated.hdr";
	data.build_desc.radiance_size = 2;
	data.build_desc.irradiance_size = 1;
	data.build_desc.prefilter_size = 2;
	data.build_desc.prefilter_mip_count = 2;
	data.build_desc.brdf_lut_size = 2;
	data.source_content_hash = 0x12345678ull;
	fill_environment_map_test_pattern(data);

	std::string error{};
	if (!write_ashibl_file(path, data, &error))
	{
		return report_self_test_failure("AshIBL round trip", error.empty() ? "write failed" : error.c_str());
	}

	EnvironmentMapCookedData loaded{};
	if (!read_ashibl_file(path, loaded, &error))
	{
		return report_self_test_failure("AshIBL round trip", error.empty() ? "read failed" : error.c_str());
	}

	const bool ok =
		loaded.source_content_hash == data.source_content_hash &&
		loaded.radiance.subresources.size() == data.radiance.subresources.size() &&
		loaded.irradiance.subresources.size() == data.irradiance.subresources.size() &&
		loaded.prefiltered_specular.subresources.size() == data.prefiltered_specular.subresources.size() &&
		loaded.brdf_lut.pixel_data == data.brdf_lut.pixel_data;
	return ok || report_self_test_failure("AshIBL round trip", "loaded payloads did not match written data");
}
```

Register it after cooked texture decode tests. Expected before implementation: compile fails because `EnvironmentMapAsset.h` does not exist.

- [ ] **Step 2: Create `EnvironmentMapAsset.h`**

Create the header with these public types:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AshEngine
{
	static constexpr char k_ashibl_magic[8] = { 'A', 'S', 'H', 'I', 'B', 'L', '\0', '\0' };
	static constexpr uint32_t k_ashibl_version = 1;
	static constexpr uint32_t k_ashibl_face_count = 6;

	enum class AshIBLPayloadKind : uint32_t
	{
		RadianceCubemap = 1,
		IrradianceCubemap = 2,
		PrefilteredSpecularCubemap = 3,
		BRDFLut2D = 4,
		PreviewThumbnail2D = 5
	};

	enum class AshIBLCompression : uint32_t
	{
		None = 0
	};

	struct ASH_API TextureSubresourcePayload
	{
		uint32_t mip_level = 0;
		uint32_t array_layer = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t row_pitch = 0;
		std::vector<uint8_t> pixel_data{};
	};

	struct ASH_API TextureCubePayload
	{
		RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mip_count = 1;
		std::vector<TextureSubresourcePayload> subresources{};
	};

	struct ASH_API Texture2DPayload
	{
		RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t row_pitch = 0;
		std::vector<uint8_t> pixel_data{};
	};

	struct ASH_API EnvironmentMapBuildDesc
	{
		std::string source_texture_path{};
		uint32_t radiance_size = 1024;
		uint32_t irradiance_size = 64;
		uint32_t prefilter_size = 256;
		uint32_t prefilter_mip_count = 8;
		uint32_t brdf_lut_size = 256;
		RenderTextureFormat hdr_format = RenderTextureFormat::RGBA16_SFLOAT;
		uint32_t sample_count = 1024;
	};

	struct ASH_API EnvironmentMapCookedData
	{
		EnvironmentMapBuildDesc build_desc{};
		uint64_t source_content_hash = 0;
		TextureCubePayload radiance{};
		TextureCubePayload irradiance{};
		TextureCubePayload prefiltered_specular{};
		Texture2DPayload brdf_lut{};
	};

	ASH_API bool validate_environment_map_cooked_data(const EnvironmentMapCookedData& data, std::string* out_error = nullptr);
	ASH_API bool write_ashibl_file(const std::filesystem::path& path, const EnvironmentMapCookedData& data, std::string* out_error = nullptr);
	ASH_API bool read_ashibl_file(const std::filesystem::path& path, EnvironmentMapCookedData& out_data, std::string* out_error = nullptr);
	ASH_API void fill_environment_map_test_pattern(EnvironmentMapCookedData& data);
}
```

- [ ] **Step 3: Implement validation helpers**

In `EnvironmentMapAsset.cpp`, implement `validate_environment_map_cooked_data()` with these checks:

```cpp
static bool make_error(std::string* out_error, std::string message)
{
	if (out_error)
	{
		*out_error = std::move(message);
	}
	return false;
}
```

Validation must reject:

- cube width/height equal to zero.
- cube face count not represented by `mip_count * 6` subresources.
- missing radiance, irradiance, prefiltered specular, or BRDF LUT data.
- any payload format not equal to `RGBA16_SFLOAT`, except BRDF LUT which may use `RG16_SFLOAT` after Task 3.
- row pitch smaller than tight row pitch from `RenderFormatUtils`.

- [ ] **Step 4: Implement uncompressed `.ashibl` writer**

Use binary `std::ofstream`. Write:

1. Zero-initialized header placeholder.
2. Metadata JSON string using `nlohmann::json`.
3. Payload table of fixed-size packed records.
4. Raw payload bytes with 16-byte alignment before each payload.
5. Seek to start and rewrite header.

Use stable payload ordering:

```text
RadianceCubemap
IrradianceCubemap
PrefilteredSpecularCubemap
BRDFLut2D
```

Set `AshIBLCompression::None` for every payload.

- [ ] **Step 5: Implement `.ashibl` reader**

Use binary `std::ifstream`. Validate:

```cpp
std::memcmp(header.magic, k_ashibl_magic, sizeof(k_ashibl_magic)) == 0
header.version == k_ashibl_version
header.payload_count >= 4
metadata_offset + metadata_size <= file_size
payload_table_offset + payload_table_size <= file_size
```

For each payload desc, validate:

```cpp
desc.compression == AshIBLCompression::None
desc.byte_offset + desc.byte_size <= file_size
desc.face_count == 6 for cubemaps
desc.face_count == 1 for BRDF LUT
```

Reconstruct `EnvironmentMapCookedData` by reading each raw payload into the correct subresource vector.

- [ ] **Step 6: Run self-test**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: PASS for `AshIBL round trip`.

- [ ] **Step 7: Commit**

```bash
git add project/src/engine/Function/Render/EnvironmentMapAsset.h project/src/engine/Function/Render/EnvironmentMapAsset.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add uncompressed AshIBL asset format"
```

---

### Task 3: Add Shared RG16 Format And Cubemap Upload API

**Files:**
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
- Modify: `project/src/engine/Function/Render/Renderer.h`
- Modify: `project/src/engine/Function/Render/Renderer.cpp`
- Modify: `project/src/engine/Function/Render/RenderFormatUtils.h`
- Modify: `project/src/engine/Function/Render/RenderFormatUtils.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-test for RG16 format and cube desc validation**

In `EngineSelfTests.cpp`, add:

```cpp
auto test_texture_cube_upload_contract() -> bool
{
	const bool rg16_ok =
		render_texture_format_to_rhi(RenderTextureFormat::RG16_SFLOAT) == RHI::ASH_FORMAT_R16G16_SFLOAT &&
		calculate_render_texture_tight_row_pitch(RenderTextureFormat::RG16_SFLOAT, 4u) == 16u;
	if (!rg16_ok)
	{
		return report_self_test_failure("Texture cube upload contract", "RG16_SFLOAT format mapping or row pitch is invalid");
	}

	std::array<uint8_t, 8u * 4u * 4u * 6u> pixels{};
	std::array<TextureSubresourceUploadDesc, 6> faces{};
	for (uint32_t face = 0; face < 6; ++face)
	{
		faces[face].mip_level = 0;
		faces[face].array_layer = face;
		faces[face].data = pixels.data() + face * 8u * 4u * 4u;
		faces[face].row_pitch = 8u * 4u;
		faces[face].slice_pitch = 8u * 4u * 4u;
	}

	TextureCubeUploadDesc desc{};
	desc.width = 4;
	desc.height = 4;
	desc.format = RenderTextureFormat::RGBA16_SFLOAT;
	desc.mip_level_count = 1;
	desc.subresources = faces.data();
	desc.subresource_count = static_cast<uint32_t>(faces.size());

	std::string error{};
	const bool valid_ok = validate_texture_cube_upload_desc(desc, &error);
	desc.subresource_count = 5;
	const bool invalid_rejected = !validate_texture_cube_upload_desc(desc, &error);
	return (valid_ok && invalid_rejected) ||
		report_self_test_failure("Texture cube upload contract", "valid cube desc was rejected or invalid desc was accepted");
}
```

Register it near texture self-tests. Expected before implementation: compile fails because `RG16_SFLOAT`, `TextureCubeUploadDesc`, and `validate_texture_cube_upload_desc()` do not exist.

- [ ] **Step 2: Add `RG16_SFLOAT` to RenderTextureFormat**

In `RenderDevice.h`, add `RG16_SFLOAT` after `RGBA16_SFLOAT`. Update `RenderFormatUtils.cpp`:

```cpp
case RenderTextureFormat::RG16_SFLOAT:
	return RHI::ASH_FORMAT_R16G16_SFLOAT;
```

and reverse mapping:

```cpp
case RHI::ASH_FORMAT_R16G16_SFLOAT:
	return RenderTextureFormat::RG16_SFLOAT;
```

In `get_render_texture_format_block_info()`, set bytes per block to `4`.

- [ ] **Step 3: Add cube upload structs and validation helper**

In `RenderDevice.h`, add:

```cpp
struct TextureSubresourceUploadDesc
{
	uint32_t mip_level = 0;
	uint32_t array_layer = 0;
	const void* data = nullptr;
	uint32_t row_pitch = 0;
	uint32_t slice_pitch = 0;
};

struct TextureCubeUploadDesc
{
	uint16_t width = 1;
	uint16_t height = 1;
	RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
	uint8_t mip_level_count = 1;
	const TextureSubresourceUploadDesc* subresources = nullptr;
	uint32_t subresource_count = 0;
	const char* name = nullptr;
};

ASH_API bool validate_texture_cube_upload_desc(const TextureCubeUploadDesc& desc, std::string* out_error = nullptr);
```

Validation rules:

```text
width > 0
height > 0
mip_level_count > 0
format != Unknown
subresource_count == mip_level_count * 6 when subresources != nullptr
array_layer < 6 for every subresource
row_pitch >= tight row pitch for that mip
slice_pitch >= row_pitch * mip_height for uncompressed formats
```

- [ ] **Step 4: Add `create_texture_cube()` to Renderer and RenderDevice**

In `RenderDevice.h` and `Renderer.h`, add:

```cpp
std::shared_ptr<RenderTarget> create_texture_cube(const TextureCubeUploadDesc& desc);
```

In `Renderer.cpp`, forward to `m_render_device`.

- [ ] **Step 5: Implement cube texture creation in RenderDevice**

In `RenderDevice.cpp`, add a sibling to `create_texture_2d_impl()` named `create_texture_cube_impl()`. Use:

```cpp
texture_creation.width = desc.width;
texture_creation.height = desc.height;
texture_creation.depth = 1;
texture_creation.array_layer_count = 6;
texture_creation.mip_level_count = desc.mip_level_count;
texture_creation.type = RHI::AshImageType::Ash_TextureCube;
texture_creation.format = render_texture_format_to_rhi(desc.format);
texture_creation.uUsageFlags = RHI::ASH_TEXTURE_USAGE_SHADER_RESOURCE_BIT | RHI::ASH_TEXTURE_USAGE_TRANSFER_DST_BIT;
texture_creation.initial_state = RHI::AshResourceState::CopyDst;
```

Upload each subresource with the same backend upload path used by 2D textures, mapping `(mip_level, array_layer)` into the RHI subresource range. After upload, transition the texture to `SRVGraphics`.

- [ ] **Step 6: Run self-test and build**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: PASS for `Texture cube upload contract`.

- [ ] **Step 7: Commit**

```bash
git add project/src/engine/Function/Render/RenderDevice.h project/src/engine/Function/Render/RenderDevice.cpp project/src/engine/Function/Render/Renderer.h project/src/engine/Function/Render/Renderer.cpp project/src/engine/Function/Render/RenderFormatUtils.h project/src/engine/Function/Render/RenderFormatUtils.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add shared cubemap texture upload API"
```

---

### Task 4: Load Environment Runtime Resources Through RenderAssetManager

**Files:**
- Modify: `project/src/engine/Function/Render/RenderAssetManager.h`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/engine/Function/Render/EnvironmentMapAsset.h`
- Modify: `project/src/engine/Function/Render/SceneRenderView.h`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-test for runtime resource metadata fallback**

Add a headless test that does not create GPU resources:

```cpp
auto test_environment_asset_key_and_fallback_policy() -> bool
{
	const std::string cooked_key = make_environment_map_asset_key("assets/env/studio.ashibl", "");
	const std::string runtime_key = make_environment_map_asset_key("", "assets/env/studio.hdr");
	const std::string fallback_key = make_environment_map_asset_key("", "");

	const bool ok =
		cooked_key == "ashibl:assets/env/studio.ashibl" &&
		runtime_key == "source:assets/env/studio.hdr" &&
		fallback_key == "fallback:";
	return ok || report_self_test_failure("Environment asset key", "environment asset keys are not stable");
}
```

Register it after `.ashibl` tests. Expected before implementation: compile fails because `make_environment_map_asset_key()` does not exist.

- [ ] **Step 2: Add runtime resource structs**

In `EnvironmentMapAsset.h`, add:

```cpp
enum class EnvironmentMapAssetState : uint8_t
{
	Loading = 0,
	Ready,
	Failed
};

struct ASH_API EnvironmentMapRuntimeResource
{
	std::shared_ptr<RenderTarget> radiance_cubemap = nullptr;
	std::shared_ptr<RenderTarget> irradiance_cubemap = nullptr;
	std::shared_ptr<RenderTarget> prefiltered_specular_cubemap = nullptr;
	std::shared_ptr<RenderTarget> brdf_lut = nullptr;
	EnvironmentMapAssetState state = EnvironmentMapAssetState::Ready;
	std::string last_error{};
	uint64_t change_version = 1;
};

ASH_API std::string make_environment_map_asset_key(const std::string& ibl_asset_path, const std::string& source_texture_path);
```

- [ ] **Step 3: Add RenderAssetManager request API**

In `RenderAssetManager.h`, add:

```cpp
std::shared_ptr<EnvironmentMapRuntimeResource> request_environment_map_asset(
	const std::string& ibl_asset_path,
	const std::string& source_texture_path);
std::shared_ptr<EnvironmentMapRuntimeResource> request_fallback_environment_map();
```

Add maps:

```cpp
std::unordered_map<std::string, std::shared_ptr<EnvironmentMapRuntimeResource>> m_environment_maps{};
std::shared_ptr<EnvironmentMapRuntimeResource> m_fallback_environment_map{};
```

- [ ] **Step 4: Implement GPU resource creation from cooked data**

In `RenderAssetManager.cpp`, add helper:

```cpp
bool create_environment_runtime_resource_from_cooked_data(
	Renderer& renderer,
	const EnvironmentMapCookedData& data,
	EnvironmentMapRuntimeResource& out_resource,
	std::string* out_error);
```

It must:

1. Convert `TextureCubePayload` into `TextureCubeUploadDesc`.
2. Call `renderer.create_texture_cube()` for radiance, irradiance, prefiltered specular.
3. Call `renderer.create_texture_2d()` for BRDF LUT.
4. Set `state=Ready` only when all four resources exist.

- [ ] **Step 5: Implement request fallback**

`request_fallback_environment_map()` creates a black/neutral environment:

```text
radiance: 1x1 cube RGBA16F black
irradiance: 1x1 cube RGBA16F black
prefiltered_specular: 1x1 cube RGBA16F black
brdf_lut: 1x1 RG16F with value (0, 0)
```

Use `create_texture_cube()` and `create_texture_2d()`. If fallback creation fails, mark the resource failed and log one warning.

- [ ] **Step 6: Implement request cooked asset**

`request_environment_map_asset()` rules:

```text
ibl_asset_path not empty -> read .ashibl -> create runtime resource
ibl_asset_path empty and source_texture_path not empty -> Task 5 runtime bake path
both empty -> fallback resource
failure -> fallback resource with warning
```

For this task, source runtime bake can return fallback until Task 5 lands. Do not throw or fail the frame when `.ashibl` cannot be read.

- [ ] **Step 7: Pass prepared resource through SceneRenderViewContext**

In `SceneRenderView.h`, forward declare `EnvironmentMapRuntimeResource` and add:

```cpp
std::shared_ptr<EnvironmentMapRuntimeResource> environment_resource = nullptr;
```

In `ScenePresentationSubsystem.cpp`, immediately before `render_visible_frame()`, prepare the resource if `packet.visible_frame->environment` exists:

```cpp
if (packet.visible_frame->environment)
{
	const VisibleEnvironmentData& environment = *packet.visible_frame->environment;
	view_context.environment_resource =
		m_impl->render_asset_manager->request_environment_map_asset(
			environment.ibl_asset_path,
			environment.source_texture_path);
}
```

If the request returns null, keep `environment_resource` null and let the render passes skip environment work.

- [ ] **Step 8: Run build and self-test**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: PASS for `Environment asset key`; build validates runtime resource types compile.

- [ ] **Step 9: Commit**

```bash
git add project/src/engine/Function/Render/RenderAssetManager.h project/src/engine/Function/Render/RenderAssetManager.cpp project/src/engine/Function/Render/EnvironmentMapAsset.h project/src/engine/Function/Render/SceneRenderView.h project/src/engine/Function/Render/ScenePresentationSubsystem.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Load environment map runtime resources"
```

---

### Task 5: Add Runtime And Offline CPU Reference Baker

**Files:**
- Create: `project/src/engine/Function/Render/EnvironmentMapBaker.h`
- Create: `project/src/engine/Function/Render/EnvironmentMapBaker.cpp`
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-test for CPU baker outputs**

Add include:

```cpp
#include "Function/Render/EnvironmentMapBaker.h"
```

Add:

```cpp
static bool write_test_hdr_equirectangular(const std::filesystem::path& path)
{
	const int width = 8;
	const int height = 4;
	std::vector<float> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3u;
			pixels[index + 0] = static_cast<float>(x) / static_cast<float>(width - 1);
			pixels[index + 1] = static_cast<float>(y) / static_cast<float>(height - 1);
			pixels[index + 2] = 0.25f;
		}
	}
	return stbi_write_hdr(path.string().c_str(), width, height, 3, pixels.data()) != 0;
}

auto test_environment_map_cpu_baker_generates_required_payloads() -> bool
{
	const std::filesystem::path hdr_path = engine_self_test_dir() / "environment_baker_test.hdr";
	if (!write_test_hdr_equirectangular(hdr_path))
	{
		return report_self_test_failure("EnvironmentMap baker", "failed to write source HDR fixture");
	}

	EnvironmentMapBuildDesc desc{};
	desc.source_texture_path = hdr_path.string();
	desc.radiance_size = 4;
	desc.irradiance_size = 2;
	desc.prefilter_size = 4;
	desc.prefilter_mip_count = 3;
	desc.brdf_lut_size = 4;
	desc.sample_count = 64;

	EnvironmentMapCookedData data{};
	EnvironmentBakeReport report{};
	if (!EnvironmentMapBaker::bake_to_cooked_data(desc, data, &report))
	{
		return report_self_test_failure("EnvironmentMap baker", report.message.empty() ? "bake failed" : report.message.c_str());
	}

	const bool ok =
		data.radiance.subresources.size() == 6u &&
		data.irradiance.subresources.size() == 6u &&
		data.prefiltered_specular.subresources.size() == 18u &&
		data.brdf_lut.width == 4u &&
		!data.brdf_lut.pixel_data.empty();
	return ok || report_self_test_failure("EnvironmentMap baker", "baker did not generate all required payloads");
}
```

Expected before implementation: compile fails because `EnvironmentMapBaker.h` does not exist.

- [ ] **Step 2: Create baker public API**

Create `EnvironmentMapBaker.h`:

```cpp
#pragma once

#include "Function/Render/EnvironmentMapAsset.h"

namespace AshEngine
{
	struct ASH_API EnvironmentBakeReport
	{
		bool succeeded = false;
		std::string message{};
		uint32_t generated_radiance_faces = 0;
		uint32_t generated_irradiance_faces = 0;
		uint32_t generated_prefilter_mips = 0;
	};

	class ASH_API EnvironmentMapBaker
	{
	public:
		static bool bake_to_cooked_data(
			const EnvironmentMapBuildDesc& desc,
			EnvironmentMapCookedData& out_data,
			EnvironmentBakeReport* out_report = nullptr);

		static bool write_ashibl(
			const EnvironmentMapCookedData& data,
			const std::filesystem::path& output_path,
			EnvironmentBakeReport* out_report = nullptr);

		static bool read_ashibl(
			const std::filesystem::path& input_path,
			EnvironmentMapCookedData& out_data,
			EnvironmentBakeReport* out_report = nullptr);
	};
}
```

- [ ] **Step 3: Implement source HDR decode and equirect sampling**

In `EnvironmentMapBaker.cpp`, use `decode_texture_source_from_file()` with `TextureColorSpace::Linear`. Convert source pixels to float4 sampling helpers:

```cpp
struct Float4
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
};
```

Implement:

```cpp
static glm::vec3 cube_direction(uint32_t face, float u, float v);
static glm::vec2 direction_to_equirect_uv(const glm::vec3& direction);
static Float4 sample_equirect_bilinear(const TextureSourceData& source, const glm::vec2& uv);
```

Use face order `+X, -X, +Y, -Y, +Z, -Z`.

- [ ] **Step 4: Generate radiance cubemap**

For `mip=0`, sample equirect into each face. First implementation stores only mip0 for radiance because prefiltered specular owns roughness mips. Fill `TextureCubePayload` with `mip_count=1`, six subresources, `RGBA16_SFLOAT` payloads. Convert float32 to IEEE half using the project’s existing half conversion helper if one exists; otherwise add a local `float_to_half_bits()` in `EnvironmentMapBaker.cpp`.

- [ ] **Step 5: Generate irradiance cubemap**

For every irradiance texel, cosine-integrate over the hemisphere around the normal direction. Use deterministic Hammersley samples:

```cpp
static float radical_inverse_vdc(uint32_t bits);
static glm::vec2 hammersley(uint32_t index, uint32_t count);
```

For each sample:

```text
sample_dir = tangent_to_world(cosine_sample_hemisphere(xi), normal)
irradiance += sample_equirect(sample_dir) * cos_theta
```

Normalize by accumulated weight. Store six `RGBA16_SFLOAT` subresources.

- [ ] **Step 6: Generate prefiltered specular cubemap**

For each mip, compute roughness:

```cpp
float roughness = mip_count <= 1 ? 0.0f : static_cast<float>(mip) / static_cast<float>(mip_count - 1);
```

Use GGX importance sampling around reflection direction. Store `mip_count * 6` subresources in `mip -> face` order. For very small test sizes, clamp mip width/height to at least 1.

- [ ] **Step 7: Generate BRDF LUT**

Generate `RG16_SFLOAT` `brdf_lut_size x brdf_lut_size`. Integrate split-sum BRDF with Hammersley samples. Store x/y as half floats in a 4-byte pixel.

- [ ] **Step 8: Wire runtime source fallback into RenderAssetManager**

In `request_environment_map_asset()`, when `source_texture_path` is provided and no cooked asset is usable:

```cpp
EnvironmentMapBuildDesc desc{};
desc.source_texture_path = source_texture_path;
EnvironmentMapCookedData cooked{};
EnvironmentBakeReport report{};
if (EnvironmentMapBaker::bake_to_cooked_data(desc, cooked, &report))
{
	create_environment_runtime_resource_from_cooked_data(*m_renderer, cooked, *resource, &error);
}
```

If runtime bake succeeds, optionally write cache to:

```text
product/caches/EnvironmentCaches/<source_hash>.ashibl
```

Do not write cache when source hash is zero or source path is empty.

- [ ] **Step 9: Run self-test**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: PASS for `EnvironmentMap baker` and prior `.ashibl` tests.

- [ ] **Step 10: Commit**

```bash
git add project/src/engine/Function/Render/EnvironmentMapBaker.h project/src/engine/Function/Render/EnvironmentMapBaker.cpp project/src/engine/Function/Render/RenderAssetManager.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add environment map CPU baker"
```

---

### Task 6: Add Deferred Environment Lighting Pass

**Files:**
- Create: `project/src/engine/Function/Render/EnvironmentLightingPass.h`
- Create: `project/src/engine/Function/Render/EnvironmentLightingPass.cpp`
- Create: `project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli`
- Create: `project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl`
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderView.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing graph contract self-test**

Extend `test_scene_deferred_render_graph_resources()` to include an environment lighting pass after lighting and before composite:

```cpp
ASH_PROCESS_ERROR(graph.add_raster_pass(
	"SceneDeferredEnvironmentLightingPass",
	RenderGraphPassFlags::None,
	[&](RenderGraphRasterPassBuilder& pass)
	{
		for (RenderGraphTextureRef gbuffer : resources.gbuffer_targets)
		{
			pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
		}
		pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
		pass.read_depth(resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
		pass.write_color(0, resources.lighting_diffuse, RenderLoadAction::Load, {});
		pass.write_color(1, resources.lighting_specular, RenderLoadAction::Load, {});
	},
	[](RenderGraphRasterContext&) -> bool { return true; }));
```

Update the pass order assertion to require:

```text
SceneDeferredLightingAccumPass before SceneDeferredEnvironmentLightingPass
SceneDeferredEnvironmentLightingPass before SceneDeferredCompositePass
```

Expected before implementation: this test compiles only after resources are available, but the main implementation still lacks the real pass.

- [ ] **Step 2: Create `EnvironmentLightingPass.h`**

Define:

```cpp
class ASH_API EnvironmentLightingPass
{
public:
	bool initialize(Renderer* renderer);
	void shutdown();
	bool add_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context);

private:
	Renderer* m_renderer = nullptr;
	std::unique_ptr<GraphicsProgram> m_program{};
	std::shared_ptr<RenderSampler> m_linear_clamp_sampler{};
};
```

- [ ] **Step 3: Split deferred lighting accumulation and composite registration**

In `DeferredLightingPass.h`, replace the single public registration surface with:

```cpp
bool add_lighting_accumulation_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	const SceneRenderViewContext& view_context);

bool add_composite_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context);

bool add_passes(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context);
```

`add_passes()` remains as a compatibility wrapper:

```cpp
ASH_PROCESS_ERROR(add_lighting_accumulation_pass(graph, frame, deferred_resources, view_context));
ASH_PROCESS_ERROR(add_composite_pass(graph, frame, deferred_resources, output_target, view_context));
```

Move existing `SceneDeferredLightingAccumPass` code into `add_lighting_accumulation_pass()` and existing `SceneDeferredCompositePass` code into `add_composite_pass()`. Do not change shader math in this refactor.

- [ ] **Step 4: Implement program creation**

In `EnvironmentLightingPass.cpp`, create program from:

```text
project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl
```

Use fullscreen triangle state:

```cpp
GraphicsProgramState state{};
state.cull_mode = RenderCullMode::None;
state.primitive_topology = RenderPrimitiveTopology::TriangleList;
state.depth_test = false;
state.depth_write = false;
state.blend_mode = RenderBlendMode::Additive;
```

Create sampler:

```cpp
RenderSamplerDesc sampler_desc{};
sampler_desc.min_filter = RenderSamplerFilter::Linear;
sampler_desc.mag_filter = RenderSamplerFilter::Linear;
sampler_desc.mip_filter = RenderSamplerFilter::Linear;
sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
```

- [ ] **Step 5: Implement graph pass registration**

`add_pass()` returns true without registering if `!frame.environment`, `!frame.environment->affect_lighting`, or `view_context.environment_resource` is null. Otherwise add `SceneDeferredEnvironmentLightingPass`, declare reads for GBuffer/depth/AO, write diffuse/specular with `RenderLoadAction::Load`, bind:

```cpp
m_program->set_texture("SceneGBufferA", gbuffer_a);
m_program->set_texture("SceneGBufferB", gbuffer_b);
m_program->set_texture("SceneGBufferC", gbuffer_c);
m_program->set_texture("SceneGBufferD", gbuffer_d);
m_program->set_texture("SceneGBufferE", gbuffer_e);
m_program->set_texture("SceneDepth", depth);
m_program->set_texture("SceneAmbientOcclusion", ambient_occlusion);
m_program->set_texture("SceneEnvironmentIrradiance", view_context.environment_resource->irradiance_cubemap);
m_program->set_texture("SceneEnvironmentPrefilteredSpecular", view_context.environment_resource->prefiltered_specular_cubemap);
m_program->set_texture("SceneEnvironmentBRDFLUT", view_context.environment_resource->brdf_lut);
m_program->set_sampler("SceneEnvironmentSampler", m_linear_clamp_sampler);
```

Inline constants include view/projection inverse data already used by deferred common plus:

```cpp
float intensity;
float rotation_radians;
uint32_t reverse_z;
```

- [ ] **Step 6: Add HLSL shared helpers**

Create `EnvironmentCommon.hlsli` with:

```hlsl
float3 AshRotateEnvironmentDirection(float3 direction_ws, float rotation_radians)
{
    const float s = sin(rotation_radians);
    const float c = cos(rotation_radians);
    return float3(
        c * direction_ws.x - s * direction_ws.z,
        direction_ws.y,
        s * direction_ws.x + c * direction_ws.z);
}

float3 AshFresnelSchlickRoughness(float cos_theta, float3 f0, float roughness)
{
    return f0 + (max(1.0.xxx - roughness.xxx, f0) - f0) * pow(saturate(1.0 - cos_theta), 5.0);
}
```

- [ ] **Step 7: Add deferred IBL shader**

Create `DeferredEnvironmentLighting.hlsl`. Include `DeferredCommon.hlsli` and `EnvironmentCommon.hlsli`. Pixel shader:

```hlsl
AshDeferredSurface surface = AshDecodeDeferredSurface(input.uv);
if (!surface.valid || surface.shading_model == ASH_SHADING_MODEL_EMPTY || surface.shading_model == ASH_SHADING_MODEL_UNLIT)
{
    return zero output;
}
```

Compute diffuse/specular using the formulas from the spec and write MRT:

```hlsl
output.diffuse = float4(diffuse_ibl, 1.0);
output.specular = float4(specular_ibl, 1.0);
```

- [ ] **Step 8: Integrate into SceneRenderer**

Add `EnvironmentLightingPass m_environment_lighting_pass;` to `SceneRenderer`. Initialize/shutdown next to `DeferredLightingPass`. In `render_visible_frame()`, replace:

```cpp
m_deferred_lighting_pass.add_passes(...);
```

with:

```cpp
m_deferred_lighting_pass.add_lighting_accumulation_pass(graph, frame, graph_resources, view_context);
m_environment_lighting_pass.add_pass(graph, frame, graph_resources, view_context);
m_deferred_lighting_pass.add_composite_pass(graph, frame, graph_resources, output, view_context);
```

If `view_context.environment_resource` is missing or failed, skip the pass and log warning once.

- [ ] **Step 9: Run build**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds; self-test graph contract passes.

- [ ] **Step 10: Commit**

```bash
git add project/src/engine/Function/Render/EnvironmentLightingPass.h project/src/engine/Function/Render/EnvironmentLightingPass.cpp project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl project/src/engine/Function/Render/SceneDeferredGraphResources.h project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/SceneRenderView.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add deferred environment lighting pass"
```

---

### Task 7: Add Sky Background Pass

**Files:**
- Create: `project/src/engine/Function/Render/SkyBackgroundPass.h`
- Create: `project/src/engine/Function/Render/SkyBackgroundPass.cpp`
- Create: `project/src/engine/Shaders/Deferred/SkyBackground.hlsl`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing graph order test for sky before tone-map**

Extend the deferred graph self-test to insert:

```text
SceneSkyBackgroundPass
```

after `SceneDeferredCompositePass` and before `SceneDeferredToneMapPass`. Assert:

```text
SceneDeferredCompositePass before SceneSkyBackgroundPass
SceneSkyBackgroundPass before SceneDeferredToneMapPass
```

- [ ] **Step 2: Create `SkyBackgroundPass.h`**

Define:

```cpp
class ASH_API SkyBackgroundPass
{
public:
	bool initialize(Renderer* renderer);
	void shutdown();
	bool add_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		RenderGraphTextureRef depth,
		RenderGraphTextureRef& in_out_scene_hdr_linear,
		const SceneRenderViewContext& view_context);

private:
	Renderer* m_renderer = nullptr;
	std::unique_ptr<GraphicsProgram> m_program{};
	std::shared_ptr<RenderSampler> m_linear_clamp_sampler{};
};
```

- [ ] **Step 3: Implement sky pass program**

Use shader path:

```text
project/src/engine/Shaders/Deferred/SkyBackground.hlsl
```

Program state:

```cpp
state.cull_mode = RenderCullMode::None;
state.primitive_topology = RenderPrimitiveTopology::TriangleList;
state.depth_test = false;
state.depth_write = false;
state.blend_mode = RenderBlendMode::Opaque;
```

- [ ] **Step 4: Implement graph pass registration**

Skip if `!frame.environment`, `!frame.environment->visible_background`, or `view_context.environment_resource` is null. If skipped, leave `in_out_scene_hdr_linear` unchanged. Otherwise create `SceneDeferredSceneHDRWithSky` as a transient HDR texture matching `SceneDeferredSceneHDRLinear`, add `SceneSkyBackgroundPass`, and assign `in_out_scene_hdr_linear = sky_output`.

Declare:

```cpp
pass.read_texture(depth, RenderGraphAccess::GraphicsSRV);
pass.read_texture(input_scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
pass.write_color(0, output_scene_hdr_linear, RenderLoadAction::Clear, {});
```

Bind:

```cpp
m_program->set_texture("SceneDepth", depth_target);
m_program->set_texture("SceneHDRLinear", input_scene_hdr);
m_program->set_texture("SceneEnvironmentRadiance", view_context.environment_resource->radiance_cubemap);
m_program->set_sampler("SceneEnvironmentSampler", m_linear_clamp_sampler);
```

- [ ] **Step 5: Add sky shader**

`SkyBackground.hlsl` reconstructs a world ray from fullscreen UV and inverse projection/view data. If `AshSceneDepthIsBackground(depth)` is false, return existing `SceneHDRLinear`. If background, sample `SceneEnvironmentRadiance` with rotated ray and write:

```hlsl
return float4(sky_radiance * intensity, 1.0);
```

Use existing reverse-Z flag logic from `DeferredCommon.hlsli`.

- [ ] **Step 6: Integrate into SceneRenderer**

Add `SkyBackgroundPass m_sky_background_pass;`. Initialize/shutdown with other passes. Call after composite has produced `graph_resources.scene_hdr_linear` and before tone-map consumes it. The intended order in code is:

```cpp
m_deferred_lighting_pass.add_lighting_accumulation_pass(...);
m_environment_lighting_pass.add_pass(...);
m_deferred_lighting_pass.add_composite_pass(...);
m_sky_background_pass.add_pass(... graph_resources.depth, graph_resources.scene_hdr_linear, view_context);
m_post_process_tone_map_pass.add_pass(...);
```

- [ ] **Step 7: Run build/self-test**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: graph order test passes.

- [ ] **Step 8: Commit**

```bash
git add project/src/engine/Function/Render/SkyBackgroundPass.h project/src/engine/Function/Render/SkyBackgroundPass.cpp project/src/engine/Shaders/Deferred/SkyBackground.hlsl project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add sky background render pass"
```

---

### Task 8: Add Scene Demo Hook And Runtime Fallback Exercise

**Files:**
- Modify: `project/src/engine/Function/Render/RenderAssetManager.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `product/config/Engine.ini`

- [ ] **Step 1: Add generated `.ashibl` self-test fixture**

Extend the `.ashibl` self-test to write a small valid cooked asset to:

```text
Intermediate/test-temp/engine/generated_environment.ashibl
```

Then read it through `read_ashibl_file()` and validate it with `validate_environment_map_cooked_data()`. This keeps the test headless and avoids creating RHI resources inside `--engine-self-test`.

- [ ] **Step 2: Add runtime fallback logging policy**

In `RenderAssetManager.cpp`, add `m_logged_environment_warnings` and log once for:

```text
failed_ashibl:<path>
failed_runtime_bake:<path>
multiple_active_environment:<scene-name>
```

Warnings must include the selected fallback path.

- [ ] **Step 3: Add Engine.ini section**

In `product/config/Engine.ini`, add:

```ini
[EnvironmentLighting]
RuntimeBakeCache=true
DefaultRadianceSize=1024
DefaultIrradianceSize=64
DefaultPrefilterSize=256
DefaultPrefilterMipCount=8
DefaultBRDFLUTSize=256
DefaultSampleCount=1024
```

Use these values as the default `EnvironmentMapBuildDesc` source when `request_environment_map_asset()` dispatches a runtime bake from `source_texture_path`.

- [ ] **Step 4: Run self-test and a short Sandbox smoke**

```powershell
./build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: self-test passes; Sandbox exits normally without environment warnings in scenes that do not define an environment.

- [ ] **Step 5: Commit**

```bash
git add project/src/engine/Function/Render/RenderAssetManager.cpp project/src/engine/Base/EngineSelfTests.cpp product/config/Engine.ini
git commit -m "Exercise environment fallback path"
```

---

### Task 9: Documentation And README

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `docs/RenderGraphAPISpec.md`

- [ ] **Step 1: Update README**

Add a rendering status bullet under the existing renderer overview:

```markdown
- Skybox / IBL design and implementation plan:
  - Spec: `docs/superpowers/specs/2026-05-25-skybox-ibl-design.md`
  - Plan: `docs/superpowers/plans/2026-05-25-skybox-ibl-implementation.md`
  - First implementation uses a single active `EnvironmentComponent`, uncompressed `.ashibl` assets, and deferred environment lighting / sky background passes.
```

- [ ] **Step 2: Update EngineDeveloperGuide**

Add a section near deferred rendering docs:

```markdown
### Skybox 与 IBL Environment

Scene authoring uses one active `EnvironmentComponent` per Scene. Runtime rendering consumes `VisibleEnvironmentData` from `VisibleRenderFrame`; render code must not read `Scene` directly. Environment assets use `.ashibl` v1: uncompressed radiance cubemap, irradiance cubemap, prefiltered specular cubemap, and BRDF LUT. Runtime source baking and Editor offline baking both go through `EnvironmentMapBaker` and produce the same `EnvironmentMapCookedData`.

Deferred graph order:

`SceneGBufferPass -> SceneAmbientOcclusionPass -> SceneDeferredLightingAccumPass -> SceneDeferredEnvironmentLightingPass -> SceneDeferredCompositePass -> SceneSkyBackgroundPass -> SceneDeferredToneMapPass`
```

- [ ] **Step 3: Update RenderGraphAPISpec**

Update the scene deferred graph example to include:

```text
SceneDeferredEnvironmentLightingPass
SceneSkyBackgroundPass
```

Document that both passes must declare depth/HDR dependencies through RenderGraph and must not inject backend barriers inside active render passes.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/EngineDeveloperGuide.md docs/RenderGraphAPISpec.md
git commit -m "Document skybox IBL implementation path"
```

---

### Task 10: Full Validation

**Files:**
- No source edits expected unless validation finds a defect.

- [ ] **Step 1: Regenerate/build Debug x64**

Run:

```powershell
./build_sandbox.bat Debug x64
./build_editor.bat Debug x64
```

Expected: both builds exit 0.

- [ ] **Step 2: Run engine self-test**

Run:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit 0 and all environment, `.ashibl`, cube upload, baker, graph tests pass.

- [ ] **Step 3: Run required runtime matrix**

Use `product/config/Engine.ini` backend selection or the project’s existing backend override workflow. For each backend, run normal startup and graceful shutdown:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
.\product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Matrix:

```text
Sandbox Vulkan
Sandbox DX12
Editor Vulkan
Editor DX12
```

Expected: each run exits normally.

- [ ] **Step 4: Inspect logs**

Check:

```text
product/logs/
```

Expected:

- no Vulkan validation errors.
- no DX12 debug-layer errors.
- no VMA leak report.
- no repeated environment warnings.
- no shader reflection binding failures for `SceneEnvironmentIrradiance`, `SceneEnvironmentPrefilteredSpecular`, `SceneEnvironmentBRDFLUT`, or `SceneEnvironmentRadiance`.

- [ ] **Step 5: Commit validation fixes or create final integration commit**

If validation required fixes:

```bash
git add <fixed files>
git commit -m "Fix skybox IBL validation issues"
```

If no fixes were needed, do not create an empty commit.

---

## Self-Review Coverage

- Scene authoring: Task 1 covers `EnvironmentComponent`, descriptors, serialization, extraction, single active selection, and environment version isolation.
- `.ashibl` format: Task 2 covers uncompressed v1 reader/writer, metadata, payload validation, and round trip.
- Cubemap upload: Task 3 covers shared `RG16_SFLOAT`, Function-layer cube upload, and validation helper.
- Runtime/offline production: Task 5 covers one CPU reference baker API used by both runtime fallback and offline `.ashibl` writing.
- Runtime consumption: Task 4 covers `EnvironmentMapRuntimeResource`, fallback, and asset manager caching.
- Deferred PBR IBL: Task 6 covers environment lighting pass and shader bindings.
- Sky background: Task 7 covers HDR sky pass before tone-map.
- Docs and README: Task 9 covers required docs.
- Validation: Task 10 covers Vulkan + DX12 Sandbox and Editor matrix.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-25-skybox-ibl-implementation.md`. Two execution options:

**1. Subagent-Driven (recommended)** - Dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
