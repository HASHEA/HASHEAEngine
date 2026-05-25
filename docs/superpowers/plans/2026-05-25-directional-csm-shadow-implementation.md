# Directional CSM Shadow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement first-phase non-VSM directional-light CSM shadows with static cache, dynamic overlay, a high-resolution near cascade, and a reusable per-light screen shadow mask.

**Architecture:** `DirectionalShadowPass` owns runtime config, cascade planning, atlas/cache resources, shadow depth/copy/mask programs, and shader-visible cascade buffers. `SceneRenderer` builds shadow-caster frame data and inserts shadow depth before deferred lighting; `DeferredLightingPass` is split into base/emissive plus per-light accumulation passes so each shadowed directional light can clear/project/use the same screen mask before its lighting draw. Directional light count remains unbounded at scene level; GPU memory is bounded by atlas/cache budgets and low-priority lights fall back to unshadowed lighting when shadow resources are exhausted.

**Tech Stack:** C++17, HLSL, AshEngine Function/Render, RenderGraph raster passes, Renderer/RenderDevice buffers and textures, DXC shader reflection, Vulkan + DX12 shared RHI validation, Engine self-test, Tracy via `Base/hprofiler.h`.

---

## File Map

- Create: `project/src/engine/Function/Render/DirectionalShadowConfig.h`
  - Runtime config, parser helpers, defaults, typed public enums.
- Create: `project/src/engine/Function/Render/DirectionalShadowConfig.cpp`
  - `[DirectionalShadows]` INI loading, clamp rules, process-level config storage.
- Create: `project/src/engine/Function/Render/DirectionalShadowPass.h`
  - Directional CSM plan types, atlas/cache metadata, pass facade, test-visible pure planning helpers.
- Create: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
  - Cascade split/matrix/tile planning, static cache LRU, persistent resources, graph pass registration, shader binding.
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli`
  - Shared fullscreen vertex, root constants, world reconstruction, cascade structs, atlas sampling helpers.
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowDepthTileClear.hlsl`
  - Raster depth tile clear for atlas sub-rects.
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowDepthCopy.hlsl`
  - Static-cache depth tile copy into dynamic atlas.
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl`
  - Screen-space CSM projection into `SceneDirectionalShadowMask`.
- Create: `project/src/engine/Shaders/Deferred/DeferredDirectionalLightingShadowed.hlsl`
  - Directional lighting variant that multiplies diffuse/specular by the per-light screen shadow mask.
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
  - Add shadow controls to `LightComponent`.
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
  - Add light shadow property descriptors and scene JSON load/save fields.
- Modify: `project/src/engine/Function/Asset/AshAssetSerializer.cpp`
  - Preserve light shadow fields in `.ashasset` load/save.
- Modify: `project/src/engine/Function/Render/RenderScene.h`
  - Add light shadow intent, mesh mobility/bounds, all-shadow-caster draw list, and scene revision fields to frame data.
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
  - Extract light shadow data and populate both camera-visible draws and shadow-caster draws.
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
  - Add directional shadow atlas/mask refs for debug registration and deferred lighting handoff.
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
  - Own `DirectionalShadowPass`; add shadow-specific mesh draw helper.
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
  - Initialize/shutdown shadow pass, render shadow casters through `PassFamily::DepthOnly`, insert shadow depth passes after AO and before lighting, register debug views.
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
  - Accept directional shadow outputs and expose per-light pass structure.
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
  - Create shadowed directional program, split accumulation into base pass plus per-light passes, call shadow mask projection before shadowed directional lights.
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
  - Add config, scene extraction, cascade planner, cache, and render graph contract tests.
- Modify: `project/src/engine/Function/Application.cpp`
  - Load and publish directional shadow config after global render feature config.
- Modify: `product/config/Engine.ini`
  - Add `[DirectionalShadows]` defaults.
- Modify: `docs/EngineDeveloperGuide.md`
  - Document runtime config, render path, resource strategy, and validation expectations.
- Modify: `docs/RenderGraphAPISpec.md`
  - Update the scene deferred graph example to include directional shadow depth and per-light shadow mask projection.
- Modify: `README.md`
  - Link this implementation plan and summarize current shadow plan status.

---

### Task 1: Add Runtime Config And Scene Shadow Intent

**Files:**
- Create: `project/src/engine/Function/Render/DirectionalShadowConfig.h`
- Create: `project/src/engine/Function/Render/DirectionalShadowConfig.cpp`
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Function/Asset/AshAssetSerializer.cpp`
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-tests for directional shadow config parsing**

In `project/src/engine/Base/EngineSelfTests.cpp`, add:

```cpp
#include "Function/Render/DirectionalShadowConfig.h"
```

Add this self-test near the AO config tests:

```cpp
auto test_directional_shadow_config_parses_and_clamps_values() -> bool
{
	const std::filesystem::path config_path = engine_self_test_dir() / "directional_shadow_self_test.ini";
	{
		std::ofstream config_file(config_path, std::ios::trunc);
		config_file <<
			"[DirectionalShadows]\n"
			"Enabled=true\n"
			"DefaultCascadeCount=9\n"
			"DefaultShadowDistance=-20\n"
			"NearShadowDistance=0\n"
			"SplitLambda=2.5\n"
			"NearCascadeResolution=4096\n"
			"OuterCascadeResolution=0\n"
			"DynamicAtlasSize=1024\n"
			"StaticCacheAtlasSize=4096\n"
			"StaticCacheBudgetMB=256\n"
			"DepthBias=0.003\n"
			"NormalBias=0.15\n"
			"PCFRadius=2\n";
	}

	const DirectionalShadowConfig config = load_runtime_directional_shadow_config(config_path.string().c_str());
	const bool parsed =
		config.enabled &&
		config.default_cascade_count == 4u &&
		config.default_shadow_distance > 0.0f &&
		config.near_shadow_distance > 0.0f &&
		config.split_lambda == 1.0f &&
		config.near_cascade_resolution == 4096u &&
		config.outer_cascade_resolution >= 256u &&
		config.dynamic_atlas_size == 1024u &&
		config.static_cache_atlas_size == 4096u &&
		config.static_cache_budget_mb == 256u &&
		config.depth_bias == 0.003f &&
		config.normal_bias == 0.15f &&
		config.pcf_radius == 2u;
	if (!parsed)
	{
		return report_self_test_failure("DirectionalShadow config", "valid config values were not parsed or clamped correctly");
	}

	const std::filesystem::path invalid_path = engine_self_test_dir() / "directional_shadow_invalid_self_test.ini";
	{
		std::ofstream config_file(invalid_path, std::ios::trunc);
		config_file <<
			"[DirectionalShadows]\n"
			"Enabled=not-a-bool\n"
			"DefaultCascadeCount=not-a-number\n"
			"SplitLambda=not-a-number\n";
	}

	const DirectionalShadowConfig invalid_config = load_runtime_directional_shadow_config(invalid_path.string().c_str());
	const DirectionalShadowConfig defaults = make_default_directional_shadow_config();
	return (invalid_config.enabled == defaults.enabled &&
		invalid_config.default_cascade_count == defaults.default_cascade_count &&
		invalid_config.split_lambda == defaults.split_lambda) ||
		report_self_test_failure("DirectionalShadow config", "invalid values did not fall back to defaults");
}
```

Register it in `run_engine_base_self_tests()` after AO config tests:

```cpp
all_passed = test_directional_shadow_config_parses_and_clamps_values() && all_passed;
```

- [ ] **Step 2: Write failing self-test for light shadow extraction**

Extend `test_render_scene_extracts_light_snapshot()`:

```cpp
directional_light.casts_shadow = true;
directional_light.shadow_priority = 42u;
directional_light.shadow_distance = 150.0f;
directional_light.shadow_cascade_count = 3u;
directional_light.near_shadow_distance = 18.0f;
```

Extend `directional_ok`:

```cpp
const bool directional_ok =
	frame.lights[0].type == LightType::Directional &&
	glm::length(frame.lights[0].direction_ws) > 0.9f &&
	frame.lights[0].casts_shadow &&
	frame.lights[0].shadow_priority == 42u &&
	frame.lights[0].shadow_distance == 150.0f &&
	frame.lights[0].shadow_cascade_count == 3u &&
	frame.lights[0].near_shadow_distance == 18.0f;
```

Expected result before implementation: build fails because `DirectionalShadowConfig.h` and the new `LightComponent` / `VisibleLightData` fields do not exist.

- [ ] **Step 3: Create `DirectionalShadowConfig.h`**

Create `project/src/engine/Function/Render/DirectionalShadowConfig.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include <cstdint>

namespace AshEngine
{
	struct ASH_API DirectionalShadowConfig
	{
		bool enabled = true;
		uint32_t default_cascade_count = 4;
		float default_shadow_distance = 160.0f;
		float near_shadow_distance = 16.0f;
		float split_lambda = 0.65f;
		uint32_t near_cascade_resolution = 2048;
		uint32_t outer_cascade_resolution = 1024;
		uint32_t dynamic_atlas_size = 4096;
		uint32_t static_cache_atlas_size = 4096;
		uint32_t static_cache_budget_mb = 64;
		float depth_bias = 0.0015f;
		float normal_bias = 0.05f;
		uint32_t pcf_radius = 1;
	};

	ASH_API DirectionalShadowConfig make_default_directional_shadow_config();
	ASH_API DirectionalShadowConfig load_runtime_directional_shadow_config(const char* config_path);
	ASH_API void set_runtime_directional_shadow_config(const DirectionalShadowConfig& config);
	ASH_API DirectionalShadowConfig get_runtime_directional_shadow_config();
}
```

- [ ] **Step 4: Create `DirectionalShadowConfig.cpp`**

Use the same parsing style as `AmbientOcclusionConfig.cpp`: `IniConfig`, clamp helpers, process-level storage protected by `std::mutex`, and `HLogInfo` on successful load.

Clamp rules:

```cpp
config.default_cascade_count = std::clamp(parsed, 1u, 4u);
config.default_shadow_distance = clamp_positive(value, config.default_shadow_distance, 1.0f, 10000.0f);
config.near_shadow_distance = clamp_positive(value, config.near_shadow_distance, 0.25f, config.default_shadow_distance);
config.split_lambda = std::clamp(value, 0.0f, 1.0f);
config.near_cascade_resolution = clamp_power_of_two(value, 512u, 4096u, config.near_cascade_resolution);
config.outer_cascade_resolution = clamp_power_of_two(value, 256u, 4096u, config.outer_cascade_resolution);
config.dynamic_atlas_size = clamp_power_of_two(value, 1024u, 8192u, config.dynamic_atlas_size);
config.static_cache_atlas_size = clamp_power_of_two(value, 1024u, 8192u, config.static_cache_atlas_size);
config.static_cache_budget_mb = std::clamp(value, 16u, 512u);
config.depth_bias = std::clamp(value, 0.0f, 0.05f);
config.normal_bias = std::clamp(value, 0.0f, 1.0f);
config.pcf_radius = std::clamp(value, 0u, 3u);
```

- [ ] **Step 5: Add light shadow fields**

In `project/src/engine/Function/Scene/SceneComponents.h`, extend `LightComponent`:

```cpp
bool casts_shadow = true;
uint32_t shadow_priority = 128;
float shadow_distance = 0.0f;
uint32_t shadow_cascade_count = 0;
float near_shadow_distance = 0.0f;
```

In `project/src/engine/Function/Scene/Scene.cpp`, add property descriptors:

```cpp
{ "casts_shadow", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(LightComponent, casts_shadow)), static_cast<uint32_t>(sizeof(bool)), nullptr },
{ "shadow_priority", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(LightComponent, shadow_priority)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
{ "shadow_distance", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, shadow_distance)), static_cast<uint32_t>(sizeof(float)), nullptr },
{ "shadow_cascade_count", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(LightComponent, shadow_cascade_count)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
{ "near_shadow_distance", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, near_shadow_distance)), static_cast<uint32_t>(sizeof(float)), nullptr },
```

In scene JSON load, after cone fields:

```cpp
light.casts_shadow = light_json.value("casts_shadow", light.casts_shadow);
light.shadow_priority = light_json.value("shadow_priority", light.shadow_priority);
light.shadow_distance = light_json.value("shadow_distance", light.shadow_distance);
light.shadow_cascade_count = light_json.value("shadow_cascade_count", light.shadow_cascade_count);
light.near_shadow_distance = light_json.value("near_shadow_distance", light.near_shadow_distance);
```

In scene JSON save and `AshAssetSerializer.cpp`, add the same keys under `"light"`.

- [ ] **Step 6: Extend render-frame light and mesh data**

In `project/src/engine/Function/Render/RenderScene.h`, extend `VisibleStaticMeshDraw`:

```cpp
PrimitiveBounds bounds{};
SceneMobility mobility = SceneMobility::Static;
```

Extend `VisibleLightData`:

```cpp
bool casts_shadow = true;
uint32_t shadow_priority = 128;
float shadow_distance = 0.0f;
uint32_t shadow_cascade_count = 0;
float near_shadow_distance = 0.0f;
```

Extend `VisibleRenderFrame`:

```cpp
uint64_t static_scene_revision = 0;
uint64_t transform_scene_revision = 0;
uint64_t light_scene_revision = 0;
std::vector<VisibleStaticMeshDraw> shadow_caster_static_mesh_draws{};
```

In `RenderScene.cpp`, copy `LightComponent` fields in `make_visible_light_data()`, and fill `bounds` / `mobility` for both camera-visible draws and all shadow caster draws. Use all visible static mesh primitives for `shadow_caster_static_mesh_draws`, not only camera-frustum visible primitives, so off-camera casters can still shadow visible receivers.

- [ ] **Step 7: Load runtime config in `Application`**

In `project/src/engine/Function/Application.cpp`, include:

```cpp
#include "Function/Render/DirectionalShadowConfig.h"
```

After render feature config and AO config are published:

```cpp
const DirectionalShadowConfig directionalShadowConfig =
	load_runtime_directional_shadow_config(config.backendConfigPath);
set_runtime_directional_shadow_config(directionalShadowConfig);
```

- [ ] **Step 8: Build and run self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits `0`.

- [ ] **Step 9: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/DirectionalShadowConfig.h project/src/engine/Function/Render/DirectionalShadowConfig.cpp project/src/engine/Function/Application.cpp project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.cpp project/src/engine/Function/Asset/AshAssetSerializer.cpp project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add directional shadow runtime data"
```

---

### Task 2: Add Pure CSM Planning, Budgeting, And Static Cache Metadata

**Files:**
- Create: `project/src/engine/Function/Render/DirectionalShadowPass.h`
- Create: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add failing planner tests**

Add these tests to `EngineSelfTests.cpp` after the light snapshot test:

```cpp
auto test_directional_shadow_planner_uses_tile_budget_without_light_count_limit() -> bool
{
	DirectionalShadowConfig config = make_default_directional_shadow_config();
	config.dynamic_atlas_size = 2048;
	config.near_cascade_resolution = 1024;
	config.outer_cascade_resolution = 512;
	config.default_cascade_count = 4;

	VisibleRenderFrame frame{};
	frame.reverse_z = true;
	frame.camera_position = { 0.0f, 0.0f, 0.0f };
	frame.view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
	frame.view_projection = frame.projection * frame.view;

	for (uint32_t index = 0; index < 12u; ++index)
	{
		VisibleLightData light{};
		light.entity_id = 100u + index;
		light.type = LightType::Directional;
		light.direction_ws = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
		light.casts_shadow = true;
		light.shadow_priority = 255u - index;
		frame.lights.push_back(light);
	}

	DirectionalShadowFramePlan plan{};
	const bool ok_plan = build_directional_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan);
	const bool budgeted =
		ok_plan &&
		plan.input_directional_shadow_light_count == 12u &&
		plan.shadowed_lights.size() > 0u &&
		plan.shadowed_lights.size() < 12u &&
		plan.cascades.size() <= plan.dynamic_tiles.capacity_tiles &&
		plan.skipped_shadow_light_count > 0u;
	return budgeted ||
		report_self_test_failure("DirectionalShadow planner", "planner did not degrade by atlas tile budget");
}

auto test_directional_shadow_planner_builds_monotonic_cascades() -> bool
{
	DirectionalShadowConfig config = make_default_directional_shadow_config();
	config.default_cascade_count = 4;
	config.default_shadow_distance = 160.0f;
	config.near_shadow_distance = 12.0f;
	config.split_lambda = 0.65f;

	VisibleRenderFrame frame{};
	frame.reverse_z = false;
	frame.camera_position = { 0.0f, 2.0f, -8.0f };
	frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
	frame.view_projection = frame.projection * frame.view;
	VisibleLightData light{};
	light.entity_id = 1;
	light.type = LightType::Directional;
	light.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
	light.casts_shadow = true;
	frame.lights.push_back(light);

	DirectionalShadowFramePlan plan{};
	if (!build_directional_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan) || plan.cascades.size() != 4u)
	{
		return report_self_test_failure("DirectionalShadow planner", "planner did not create four cascades");
	}

	bool monotonic = plan.cascades[0].cache_mode == DirectionalShadowCacheMode::NearEveryFrame;
	for (size_t index = 1; index < plan.cascades.size(); ++index)
	{
		monotonic = monotonic &&
			plan.cascades[index - 1].split_near < plan.cascades[index - 1].split_far &&
			plan.cascades[index - 1].split_far <= plan.cascades[index].split_near + 0.001f &&
			plan.cascades[index].split_near < plan.cascades[index].split_far;
	}
	return monotonic ||
		report_self_test_failure("DirectionalShadow planner", "cascade splits were not monotonic");
}
```

Register both tests in `run_engine_base_self_tests()`.

- [ ] **Step 2: Define planning and cache types**

Create the first version of `DirectionalShadowPass.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	class StorageBuffer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;

	enum class DirectionalShadowCacheMode : uint8_t
	{
		Uncached = 0,
		StaticCached,
		StaticRefresh,
		NearEveryFrame
	};

	enum class ShadowCasterMobilityFilter : uint8_t
	{
		All = 0,
		StaticOnly,
		DynamicOnly
	};

	struct DirectionalShadowAtlasTile
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t resolution = 0;
	};

	struct DirectionalShadowTileBudget
	{
		uint32_t atlas_size = 0;
		uint32_t capacity_tiles = 0;
		uint32_t used_tiles = 0;
	};

	struct DirectionalShadowCascadePlan
	{
		uint32_t light_plan_index = 0;
		EntityId light_entity_id = 0;
		uint32_t cascade_index = 0;
		float split_near = 0.0f;
		float split_far = 0.0f;
		float depth_bias = 0.0f;
		float normal_bias = 0.0f;
		glm::mat4 light_view_projection{ 1.0f };
		DirectionalShadowAtlasTile dynamic_tile{};
		DirectionalShadowAtlasTile static_cache_tile{};
		DirectionalShadowCacheMode cache_mode = DirectionalShadowCacheMode::Uncached;
		bool has_static_cache_tile = false;
	};

	struct DirectionalShadowLightPlan
	{
		uint32_t frame_light_index = 0;
		EntityId light_entity_id = 0;
		uint32_t first_cascade = 0;
		uint32_t cascade_count = 0;
		uint32_t shadow_priority = 0;
		bool shadowed = false;
	};

	struct DirectionalShadowFramePlan
	{
		std::vector<DirectionalShadowLightPlan> shadowed_lights{};
		std::vector<DirectionalShadowCascadePlan> cascades{};
		DirectionalShadowTileBudget dynamic_tiles{};
		uint32_t input_directional_shadow_light_count = 0;
		uint32_t skipped_shadow_light_count = 0;
	};

	struct DirectionalShadowPassOutputs
	{
		RenderGraphTextureRef dynamic_atlas{};
		RenderGraphTextureRef static_cache_atlas{};
		RenderGraphTextureRef shadow_mask{};
		DirectionalShadowFramePlan plan{};
		std::shared_ptr<StorageBuffer> cascade_buffer = nullptr;
		bool has_shadowed_lights() const { return !plan.shadowed_lights.empty() && dynamic_atlas && shadow_mask; }
	};

	using DirectionalShadowCasterDrawCallback = std::function<bool(
		const VisibleRenderFrame& shadow_frame,
		const SceneRenderViewContext& shadow_view_context,
		RenderGraphRasterContext& context,
		uint64_t render_frame_index,
		ShadowCasterMobilityFilter mobility_filter)>;

	class DirectionalShadowPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		DirectionalShadowPassOutputs add_depth_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			uint64_t render_frame_index,
			const DirectionalShadowCasterDrawCallback& draw_callback);

		bool add_shadow_mask_pass(
			RenderGraphBuilder& graph,
			const DirectionalShadowPassOutputs& outputs,
			uint32_t shadowed_light_plan_index,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);

	private:
		Renderer* m_renderer = nullptr;
		DirectionalShadowConfig m_config{};
		std::unique_ptr<GraphicsProgram> m_tile_clear_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_depth_copy_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_shadow_mask_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderTarget> m_static_cache_atlas = nullptr;
		std::shared_ptr<StorageBuffer> m_cascade_buffer = nullptr;
	};

	ASH_API bool build_directional_shadow_frame_plan_for_tests(
		const VisibleRenderFrame& frame,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		DirectionalShadowFramePlan& out_plan);
}
```

- [ ] **Step 3: Implement pure planner**

In `DirectionalShadowPass.cpp`, implement:

```cpp
static auto resolve_light_cascade_count(const VisibleLightData& light, const DirectionalShadowConfig& config) -> uint32_t
{
	const uint32_t requested = light.shadow_cascade_count != 0u ? light.shadow_cascade_count : config.default_cascade_count;
	return std::clamp(requested, 1u, 4u);
}

static auto resolve_shadow_distance(const VisibleLightData& light, const DirectionalShadowConfig& config) -> float
{
	return light.shadow_distance > 0.0f ? light.shadow_distance : config.default_shadow_distance;
}

static auto resolve_near_shadow_distance(const VisibleLightData& light, const DirectionalShadowConfig& config, float shadow_distance) -> float
{
	const float requested = light.near_shadow_distance > 0.0f ? light.near_shadow_distance : config.near_shadow_distance;
	return std::clamp(requested, 0.25f, shadow_distance);
}

static auto compute_cascade_split_far(uint32_t cascade_index, uint32_t cascade_count, float near_depth, float far_depth, float split_lambda) -> float
{
	const float p = static_cast<float>(cascade_index + 1u) / static_cast<float>(cascade_count);
	const float linear = near_depth + (far_depth - near_depth) * p;
	const float logarithmic = near_depth * std::pow(far_depth / std::max(near_depth, 0.0001f), p);
	return glm::mix(linear, logarithmic, split_lambda);
}
```

Tile packing for the first implementation:

```cpp
static auto try_allocate_tile(uint32_t atlas_size, uint32_t resolution, uint32_t& cursor_x, uint32_t& cursor_y, uint32_t& row_height, DirectionalShadowAtlasTile& out_tile) -> bool
{
	if (resolution == 0u || resolution > atlas_size)
	{
		return false;
	}
	if (cursor_x + resolution > atlas_size)
	{
		cursor_x = 0u;
		cursor_y += row_height;
		row_height = 0u;
	}
	if (cursor_y + resolution > atlas_size)
	{
		return false;
	}
	out_tile = { cursor_x, cursor_y, resolution, resolution, resolution };
	cursor_x += resolution;
	row_height = std::max(row_height, resolution);
	return true;
}
```

`build_directional_shadow_frame_plan_for_tests()` must:

- Collect all `LightType::Directional` lights with `casts_shadow=true`.
- Sort by `shadow_priority` descending, then stable frame light index ascending.
- Allocate atlas tiles until the dynamic atlas is full.
- Never use a hard directional-light count constant.
- Mark cascade `0` as `NearEveryFrame`.
- Mark outer cascades as `StaticRefresh` on the first cache miss; `StaticCached` after cache metadata validates in later tasks.
- Increment `skipped_shadow_light_count` when a light cannot receive all requested cascades.

- [ ] **Step 4: Build and run self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: planner tests pass and no render path has changed yet.

- [ ] **Step 5: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add directional CSM shadow planner"
```

---

### Task 3: Add Mobility-Aware Shadow Caster Submission

**Files:**
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add failing self-test for shadow caster frame data**

Add a self-test using a headless `RenderScene` light-only path and direct `VisibleRenderFrame` construction:

```cpp
auto test_visible_static_mesh_draws_carry_shadow_mobility() -> bool
{
	VisibleStaticMeshDraw static_draw{};
	static_draw.entity_id = 1;
	static_draw.mobility = SceneMobility::Static;
	VisibleStaticMeshDraw movable_draw{};
	movable_draw.entity_id = 2;
	movable_draw.mobility = SceneMobility::Movable;

	VisibleRenderFrame frame{};
	frame.shadow_caster_static_mesh_draws.push_back(static_draw);
	frame.shadow_caster_static_mesh_draws.push_back(movable_draw);

	const uint32_t static_count = count_shadow_casters_for_tests(frame, ShadowCasterMobilityFilter::StaticOnly);
	const uint32_t dynamic_count = count_shadow_casters_for_tests(frame, ShadowCasterMobilityFilter::DynamicOnly);
	const uint32_t all_count = count_shadow_casters_for_tests(frame, ShadowCasterMobilityFilter::All);
	const bool ok = static_count == 1u && dynamic_count == 1u && all_count == 2u;
	return ok || report_self_test_failure("DirectionalShadow caster filter", "shadow caster mobility filter did not classify draws");
}
```

Expose this helper in `DirectionalShadowPass.h`:

```cpp
ASH_API uint32_t count_shadow_casters_for_tests(
	const VisibleRenderFrame& frame,
	ShadowCasterMobilityFilter filter);
```

- [ ] **Step 2: Add shadow-specific static mesh render helper**

In `SceneRenderer.h`, add:

```cpp
bool render_shadow_static_meshes_to_pass(
	const VisibleRenderFrame& frame,
	const SceneRenderViewContext& view_context,
	RenderGraphRasterContext& pass_context,
	uint64_t render_frame_index,
	ShadowCasterMobilityFilter mobility_filter);
```

In `SceneRenderer.cpp`, implement by copying `frame` to a local `shadow_frame`, filtering `frame.shadow_caster_static_mesh_draws`, assigning that vector to `shadow_frame.static_mesh_draws`, and then calling existing `render_static_meshes_to_pass(..., PassFamily::DepthOnly)`:

```cpp
VisibleRenderFrame shadow_frame = frame;
shadow_frame.static_mesh_draws.clear();
for (const VisibleStaticMeshDraw& draw : frame.shadow_caster_static_mesh_draws)
{
	const bool static_match = draw.mobility == SceneMobility::Static || draw.mobility == SceneMobility::Stationary;
	const bool dynamic_match = draw.mobility == SceneMobility::Movable;
	if (mobility_filter == ShadowCasterMobilityFilter::All ||
		(mobility_filter == ShadowCasterMobilityFilter::StaticOnly && static_match) ||
		(mobility_filter == ShadowCasterMobilityFilter::DynamicOnly && dynamic_match))
	{
		shadow_frame.static_mesh_draws.push_back(draw);
	}
}
return render_static_meshes_to_pass(shadow_frame, view_context, pass_context, render_frame_index, PassFamily::DepthOnly);
```

- [ ] **Step 3: Preserve light-view projection for shadow depth**

When a shadow pass calls the helper, it passes a shadow copy of `VisibleRenderFrame` whose `view_projection` is the cascade `light_view_projection`. Do not change material HLSL for depth-only; the existing instance data path already uses `frame.view_projection * draw.world_transform`.

- [ ] **Step 4: Build and run self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: caster filter test passes.

- [ ] **Step 5: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add mobility-aware shadow caster submission"
```

---

### Task 4: Add Shadow Atlas Resources, Tile Clear, And Depth Copy

**Files:**
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.h`
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli`
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowDepthTileClear.hlsl`
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowDepthCopy.hlsl`

- [ ] **Step 1: Add shader common file**

Create `DirectionalShadowCommon.hlsli`:

```hlsl
struct VSFullscreenOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSFullscreenOutput VSFullscreen(uint vertex_id : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    VSFullscreenOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}

struct DirectionalShadowCascadeShaderData
{
    float4x4 world_to_shadow_clip;
    float4 atlas_uv_scale_bias;
    float4 split_depth_bias;     // x near, y far, z depth bias, w normal bias
    float4 texel_size_flags;     // xy texel size, z cascade index, w enabled
};
```

- [ ] **Step 2: Add tile clear shader**

Create `DirectionalShadowDepthTileClear.hlsl`:

```hlsl
#include "DirectionalShadowCommon.hlsli"

cbuffer AshRootConstants : register(b0)
{
    float4 AshShadowClearParams; // x depth
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float PSMain(VSFullscreenOutput input) : SV_Depth
{
    return AshShadowClearParams.x;
}
```

- [ ] **Step 3: Add depth copy shader**

Create `DirectionalShadowDepthCopy.hlsl`:

```hlsl
#include "DirectionalShadowCommon.hlsli"

Texture2D<float> DirectionalShadowStaticCache : register(t0);
SamplerState ScenePointClampSampler : register(s0);

cbuffer AshRootConstants : register(b0)
{
    float4 AshShadowCopyScaleBias; // xy scale, zw bias from target tile uv to source atlas uv
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float PSMain(VSFullscreenOutput input) : SV_Depth
{
    const float2 source_uv = input.uv * AshShadowCopyScaleBias.xy + AshShadowCopyScaleBias.zw;
    return DirectionalShadowStaticCache.SampleLevel(ScenePointClampSampler, source_uv, 0);
}
```

- [ ] **Step 4: Create persistent static cache and graph dynamic atlas**

In `DirectionalShadowPass::create_resources()`:

```cpp
RenderTargetDesc static_cache_desc{};
static_cache_desc.width = static_cast<uint16_t>(m_config.static_cache_atlas_size);
static_cache_desc.height = static_cast<uint16_t>(m_config.static_cache_atlas_size);
static_cache_desc.format = RenderTextureFormat::D32_SFLOAT;
static_cache_desc.shader_resource = true;
static_cache_desc.unordered_access = false;
static_cache_desc.use_optimized_clear_value = true;
static_cache_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
static_cache_desc.name = "DirectionalShadowStaticCache";
m_static_cache_atlas = renderer.create_render_target(static_cache_desc);
ASH_PROCESS_ERROR(m_static_cache_atlas != nullptr);
```

In `add_depth_passes()`, create:

```cpp
RenderGraphTextureDesc dynamic_desc{};
dynamic_desc.width = static_cast<uint16_t>(m_config.dynamic_atlas_size);
dynamic_desc.height = static_cast<uint16_t>(m_config.dynamic_atlas_size);
dynamic_desc.format = RenderTextureFormat::D32_SFLOAT;
dynamic_desc.shader_resource = true;
dynamic_desc.unordered_access = false;
dynamic_desc.use_optimized_clear_value = true;
dynamic_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
outputs.dynamic_atlas = graph.create_texture(dynamic_desc, "DirectionalShadowDynamicAtlas");

outputs.static_cache_atlas =
	graph.register_external_texture(m_static_cache_atlas, "DirectionalShadowStaticCache", RenderGraphAccess::GraphicsSRV);
```

- [ ] **Step 5: Add clear/copy program creation**

Both programs use fullscreen triangle, depth test disabled for color, depth write enabled, depth compare `Always`, cull none:

```cpp
GraphicsProgramState depth_write_state{};
depth_write_state.cull_mode = RenderCullMode::None;
depth_write_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
depth_write_state.depth_test = true;
depth_write_state.depth_write = true;
depth_write_state.depth_compare = RenderCompareOp::Always;
depth_write_state.blend_mode = RenderBlendMode::Opaque;
```

Program names:

```cpp
"DirectionalShadowDepthTileClear"
"DirectionalShadowDepthCopy"
```

Hash each shader with `DirectionalShadowCommon.hlsli`.

- [ ] **Step 6: Build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build succeeds and shader reflection consumes `DirectionalShadowStaticCache`, `ScenePointClampSampler`, and `AshRootConstants`.

- [ ] **Step 7: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli project/src/engine/Shaders/Shadow/DirectionalShadowDepthTileClear.hlsl project/src/engine/Shaders/Shadow/DirectionalShadowDepthCopy.hlsl
git commit -m "Add directional shadow atlas resources"
```

---

### Task 5: Render Static Cache And Dynamic Atlas Depth

**Files:**
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add render graph contract test**

Add a test that creates a headless graph with depth resources and calls a test helper:

```cpp
auto test_directional_shadow_graph_adds_depth_before_lighting() -> bool
{
	RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("DirectionalShadowGraphSelfTest");
	RenderTargetDesc output_desc{};
	output_desc.width = 128;
	output_desc.height = 128;
	output_desc.format = RenderTextureFormat::RGBA8_UNORM;
	RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "SceneOutput");

	RenderGraphTextureDesc depth_desc{};
	depth_desc.width = 128;
	depth_desc.height = 128;
	depth_desc.format = RenderTextureFormat::D32_SFLOAT;
	depth_desc.shader_resource = true;
	RenderGraphTextureRef dynamic_atlas = graph.create_texture(depth_desc, "DirectionalShadowDynamicAtlas");

	DirectionalShadowFramePlan plan{};
	DirectionalShadowCascadePlan cascade{};
	cascade.cache_mode = DirectionalShadowCacheMode::NearEveryFrame;
	cascade.dynamic_tile = { 0u, 0u, 1024u, 1024u, 1024u };
	plan.cascades.push_back(cascade);

	add_directional_shadow_depth_passes_for_tests(graph, dynamic_atlas, plan);
	graph.add_raster_pass(
		"ShadowConsumer",
		RenderGraphPassFlags::None,
		[&](RenderGraphRasterPassBuilder& pass)
		{
			pass.read_texture(dynamic_atlas, RenderGraphAccess::GraphicsSRV);
			pass.write_color(0, output, RenderLoadAction::Clear, {});
		},
		[](RenderGraphRasterContext&)
		{
			return true;
		});

	RenderGraphCompileResult result{};
	const bool ok =
		graph.compile_for_tests(result) &&
		result.live_pass_indices.size() >= 2u &&
		result.pass_barriers.back().transitions.size() >= 1u;
	return ok || report_self_test_failure("DirectionalShadow graph", "shadow depth producer was not preserved before consumer");
}
```

Expose `add_directional_shadow_depth_passes_for_tests()` in `DirectionalShadowPass.h` as a graph-only helper that declares the same depth write/read contract without real draw callbacks.

- [ ] **Step 2: Add dynamic atlas clear pass**

At the start of `DirectionalShadowPass::add_depth_passes()`, when there is at least one cascade:

```cpp
ASH_PROCESS_ERROR(graph.add_raster_pass(
	"SceneDirectionalShadowDynamicAtlasClearPass",
	RenderGraphPassFlags::None,
	[dynamic_atlas = outputs.dynamic_atlas](RenderGraphRasterPassBuilder& pass)
	{
		pass.write_depth(dynamic_atlas, RenderLoadAction::Clear, { 1.0f, 0u });
	},
	[](RenderGraphRasterContext&)
	{
		return true;
	}));
```

- [ ] **Step 3: Add static cache refresh pass per refresh cascade**

For each cascade with `StaticRefresh`, add a pass:

```cpp
pass.write_depth(outputs.static_cache_atlas, RenderLoadAction::Load, { 1.0f, 0u });
```

Execute:

1. Set viewport/scissor to `cascade.static_cache_tile`.
2. Draw `DirectionalShadowDepthTileClear` with depth `1.0`.
3. Copy `frame` to `shadow_frame`, set `shadow_frame.view_projection = cascade.light_view_projection`.
4. Call the draw callback with `ShadowCasterMobilityFilter::StaticOnly`.

Expected behavior: refreshing one tile does not clear other valid static cache tiles.

- [ ] **Step 4: Add dynamic atlas pass per cascade**

For each cascade, add a pass with:

```cpp
pass.write_depth(outputs.dynamic_atlas, RenderLoadAction::Load, { 1.0f, 0u });
```

Execution rules:

- `NearEveryFrame` and `Uncached`: tile-clear dynamic tile, then draw `ShadowCasterMobilityFilter::All`.
- `StaticCached`: draw `DirectionalShadowDepthCopy` from static cache tile to dynamic tile, then draw `ShadowCasterMobilityFilter::DynamicOnly`.
- `StaticRefresh`: after static cache refresh, draw `DirectionalShadowDepthCopy`, then draw dynamic casters.

First implementation can have zero movable casters in the default scene; the callback still needs to handle `DynamicOnly` cleanly.

- [ ] **Step 5: Keep shadow depth view context non-reverse-Z**

Create a local shadow view context per cascade:

```cpp
SceneRenderViewContext shadow_view_context = view_context;
shadow_view_context.reverse_z = false;
shadow_view_context.depth_clear_value = { 1.0f, 0u };
shadow_view_context.has_viewport = true;
shadow_view_context.viewport = make_viewport_from_tile(cascade.dynamic_tile);
shadow_view_context.has_scissor = true;
shadow_view_context.scissor = make_scissor_from_tile(cascade.dynamic_tile);
shadow_view_context.debug_name = "DirectionalShadowCascade";
```

For static cache refresh, use the static cache tile for viewport/scissor.

- [ ] **Step 6: Build and self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: graph contract test passes; no runtime validation yet.

- [ ] **Step 7: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Render directional shadow depth atlases"
```

---

### Task 6: Add Screen-Space Directional Shadow Mask Projection

**Files:**
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.h`
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
- Create: `project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl`

- [ ] **Step 1: Add cascade shader data buffer**

In `DirectionalShadowPass.cpp`, define:

```cpp
struct DirectionalShadowCascadeShaderData
{
	glm::mat4 world_to_shadow_clip{ 1.0f };
	glm::vec4 atlas_uv_scale_bias{ 1.0f, 1.0f, 0.0f, 0.0f };
	glm::vec4 split_depth_bias{ 0.0f };
	glm::vec4 texel_size_flags{ 0.0f };
};
static_assert(sizeof(DirectionalShadowCascadeShaderData) == 112u);
```

Create/update `m_cascade_buffer` with `StorageBufferDesc`:

```cpp
StorageBufferDesc desc{};
desc.size = static_cast<uint32_t>(cascade_data.size() * sizeof(DirectionalShadowCascadeShaderData));
desc.stride = static_cast<uint32_t>(sizeof(DirectionalShadowCascadeShaderData));
desc.cpu_write = true;
desc.initial_data = cascade_data.data();
desc.name = "DirectionalShadowCascadeBuffer";
```

If existing buffer is too small, recreate it; otherwise call `update(0, desc.size, cascade_data.data())`.

- [ ] **Step 2: Add shadow mask texture**

In `DirectionalShadowPass::add_depth_passes()`, create one graph transient mask when any shadowed light exists:

```cpp
RenderGraphTextureDesc mask_desc{};
mask_desc.width = static_cast<uint16_t>(view_context.output_target->get_width());
mask_desc.height = static_cast<uint16_t>(view_context.output_target->get_height());
mask_desc.format = RenderTextureFormat::RGBA8_UNORM;
mask_desc.shader_resource = true;
mask_desc.unordered_access = false;
mask_desc.use_optimized_clear_value = true;
mask_desc.optimized_clear_color = { 1.0f, 1.0f, 1.0f, 1.0f };
outputs.shadow_mask = graph.create_texture(mask_desc, "SceneDirectionalShadowMask");
```

- [ ] **Step 3: Create `DirectionalShadowMask.hlsl`**

```hlsl
#include "DirectionalShadowCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture2D<float> DirectionalShadowDynamicAtlas : register(t1);
StructuredBuffer<DirectionalShadowCascadeShaderData> SceneDirectionalShadowCascades : register(t2);
SamplerState ScenePointClampSampler : register(s0);

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4x4 AshView;
    float4 AshViewportSize;
    float4 AshShadowLightParams; // x first cascade, y cascade count, z reverseZ, w pcf radius
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    return world.xyz / max(world.w, 1e-6);
}

bool IsBackgroundDepth(float depth)
{
    return AshShadowLightParams.z > 0.5 ? depth <= 0.000001 : depth >= 0.999999;
}

float SampleCascadeShadow(uint cascade_buffer_index, float3 position_ws)
{
    DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[cascade_buffer_index];
    const float4 shadow_clip = mul(cascade.world_to_shadow_clip, float4(position_ws, 1.0));
    const float3 shadow_ndc = shadow_clip.xyz / max(shadow_clip.w, 1e-6);
    float2 tile_uv = shadow_ndc.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    if (tile_uv.x < 0.0 || tile_uv.y < 0.0 || tile_uv.x > 1.0 || tile_uv.y > 1.0 || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0)
    {
        return 1.0;
    }

    const float2 atlas_uv = tile_uv * cascade.atlas_uv_scale_bias.xy + cascade.atlas_uv_scale_bias.zw;
    const int radius = (int)round(AshShadowLightParams.w);
    float lit = 0.0;
    float count = 0.0;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 sample_uv = atlas_uv + float2((float)x, (float)y) * cascade.texel_size_flags.xy;
            const float shadow_depth = DirectionalShadowDynamicAtlas.SampleLevel(ScenePointClampSampler, sample_uv, 0);
            lit += (shadow_ndc.z - cascade.split_depth_bias.z) <= shadow_depth ? 1.0 : 0.0;
            count += 1.0;
        }
    }
    return lit / max(count, 1.0);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float scene_depth = SceneDepth.SampleLevel(ScenePointClampSampler, input.uv, 0);
    if (IsBackgroundDepth(scene_depth))
    {
        return 1.0.xxxx;
    }

    const float3 position_ws = ReconstructWorldPosition(input.uv, scene_depth);
    const float view_depth = abs(mul(AshView, float4(position_ws, 1.0)).z);
    const uint first_cascade = (uint)round(AshShadowLightParams.x);
    const uint cascade_count = (uint)round(AshShadowLightParams.y);
    for (uint cascade_index = 0; cascade_index < cascade_count; ++cascade_index)
    {
        const uint buffer_index = first_cascade + cascade_index;
        DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[buffer_index];
        if (view_depth >= cascade.split_depth_bias.x && view_depth <= cascade.split_depth_bias.y)
        {
            const float shadow = SampleCascadeShadow(buffer_index, position_ws);
            return float4(shadow, shadow, shadow, 1.0);
        }
    }
    return 1.0.xxxx;
}
```

- [ ] **Step 4: Implement `add_shadow_mask_pass()`**

`DirectionalShadowPass::add_shadow_mask_pass()` must:

- Return true without adding a pass if `outputs.has_shadowed_lights()` is false.
- Validate the requested `shadowed_light_plan_index`.
- Read `deferred_resources.depth` and `outputs.dynamic_atlas`.
- Write `outputs.shadow_mask` with clear color `1.0`.
- Bind `SceneDepth`, `DirectionalShadowDynamicAtlas`, `SceneDirectionalShadowCascades`, and `ScenePointClampSampler`.
- Draw a fullscreen triangle.

Root constants:

```cpp
struct DirectionalShadowMaskRootConstants
{
	glm::mat4 inv_view_projection{ 1.0f };
	glm::mat4 view{ 1.0f };
	glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec4 shadow_light_params{ 0.0f }; // first cascade, cascade count, reverseZ, pcf radius
};
static_assert(sizeof(DirectionalShadowMaskRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);
```

- [ ] **Step 5: Build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: shader reflection consumes `SceneDirectionalShadowCascades` as a storage buffer and `DirectionalShadowDynamicAtlas` as a texture.

- [ ] **Step 6: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl
git commit -m "Project directional shadows to screen mask"
```

---

### Task 7: Split Deferred Lighting Into Per-Light Passes

**Files:**
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Create: `project/src/engine/Shaders/Deferred/DeferredDirectionalLightingShadowed.hlsl`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add graph contract test for per-light mask use**

Update `test_scene_deferred_graph_resources_describe_live_pass_chain()` or add a focused test that expects:

```text
SceneDeferredLightingBasePass
SceneDirectionalShadowMaskPass
SceneDeferredDirectionalLightingShadowedPass
SceneDeferredCompositePass
```

The test should verify:

```cpp
pass.read_texture(scene_directional_shadow_mask, RenderGraphAccess::GraphicsSRV);
pass.write_color(0, lighting_diffuse, RenderLoadAction::Load, {});
pass.write_color(1, lighting_specular, RenderLoadAction::Load, {});
```

- [ ] **Step 2: Add shadowed directional shader**

Create `DeferredDirectionalLightingShadowed.hlsl`:

```hlsl
#include "DeferredCommon.hlsli"

Texture2D<float4> SceneDirectionalShadowMask : register(t9);

struct PSOutput
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

PSOutput PSMain(VSFullscreenOutput input)
{
    AshDeferredSurface surface = AshDecodeDeferredSurface(input.uv);
    const float3 light_dir_to_light = normalize(-AshLightDirectionAndIntensity.xyz);
    const float3 radiance = AshLightColorAndType.rgb * AshLightDirectionAndIntensity.w;
    AshSplitLighting lit = AshEvaluateDynamicLight_Split(surface, light_dir_to_light, radiance);
    const float shadow = saturate(SceneDirectionalShadowMask.Sample(ScenePointClampSampler, input.uv).r);
    lit.diffuse *= shadow;
    lit.specular *= shadow;

    PSOutput output;
    output.diffuse = float4(lit.diffuse, 1.0);
    output.specular = float4(lit.specular, 1.0);
    return output;
}
```

- [ ] **Step 3: Extend deferred lighting pass signature**

In `DeferredLightingPass.h`, forward declare:

```cpp
class DirectionalShadowPass;
struct DirectionalShadowPassOutputs;
```

Update `add_passes()`:

```cpp
bool add_passes(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context,
	DirectionalShadowPass* directional_shadow_pass = nullptr,
	const DirectionalShadowPassOutputs* directional_shadow_outputs = nullptr);
```

- [ ] **Step 4: Create shadowed directional program**

Add member:

```cpp
std::unique_ptr<GraphicsProgram> m_shadowed_directional_program = nullptr;
```

Create it in `create_programs()` using the same additive fullscreen state as `m_directional_program`, with name:

```cpp
"SceneDeferredDirectionalLightingShadowed"
```

Release it in `shutdown()`.

- [ ] **Step 5: Split accumulation pass**

Replace the current monolithic `SceneDeferredLightingAccumPass` with:

1. `SceneDeferredLightingBasePass`
   - Reads GBuffer, depth, AO.
   - Writes diffuse/specular with `RenderLoadAction::Clear`.
   - Draws `m_base_emissive_program`.
2. One light pass per visible light.
   - Reads GBuffer, depth, AO.
   - Writes diffuse/specular with `RenderLoadAction::Load`.
   - Directional + shadowed: call `directional_shadow_pass->add_shadow_mask_pass(...)` before the light pass, then read `directional_shadow_outputs->shadow_mask` and draw `m_shadowed_directional_program`.
   - Directional unshadowed: draw `m_directional_program`.
   - Point/spot: keep existing volume draw behavior.
3. Existing `SceneDeferredCompositePass`.

The per-light loop should resolve shadowed directional light plans by `frame_light_index`:

```cpp
const DirectionalShadowLightPlan* shadow_plan =
	find_shadow_plan_for_frame_light(*directional_shadow_outputs, frame_light_index);
const bool use_directional_shadow =
	light.type == LightType::Directional &&
	shadow_plan &&
	shadow_plan->shadowed &&
	directional_shadow_pass &&
	directional_shadow_outputs &&
	directional_shadow_outputs->has_shadowed_lights();
```

If shadow output is missing or budget skipped the light, render it through the unshadowed directional path.

- [ ] **Step 6: Bind mask only for shadowed directional program**

Inside the shadowed light execute lambda:

```cpp
std::shared_ptr<RenderTarget> shadow_mask = context.get_texture(directional_shadow_outputs->shadow_mask);
ASH_PROCESS_ERROR(shadow_mask != nullptr);
ASH_PROCESS_ERROR(m_shadowed_directional_program->set_texture("SceneDirectionalShadowMask", shadow_mask));
```

Keep GBuffer/AO/depth sampler binding shared with existing programs.

- [ ] **Step 7: Build and self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds, self-test exits `0`, and the deferred graph contract reflects split lighting passes.

- [ ] **Step 8: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Shaders/Deferred/DeferredDirectionalLightingShadowed.hlsl project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Apply directional shadow mask in deferred lighting"
```

---

### Task 8: Wire DirectionalShadowPass Into SceneRenderer And Debug View

**Files:**
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`

- [ ] **Step 1: Extend deferred graph resources**

In `SceneDeferredGraphResources.h`:

```cpp
RenderGraphTextureRef directional_shadow_dynamic_atlas{};
RenderGraphTextureRef directional_shadow_static_cache{};
RenderGraphTextureRef directional_shadow_mask{};
```

- [ ] **Step 2: Add pass ownership**

In `SceneRenderer.h`, include:

```cpp
#include "Function/Render/DirectionalShadowPass.h"
```

Add member before `DeferredLightingPass`:

```cpp
DirectionalShadowPass m_directional_shadow_pass{};
```

Initialize after `m_ambient_occlusion_pass` and before `m_deferred_lighting_pass`:

```cpp
ASH_PROCESS_ERROR(m_directional_shadow_pass.initialize(m_renderer));
```

Shutdown before `m_deferred_lighting_pass.shutdown()` or immediately after it:

```cpp
m_directional_shadow_pass.shutdown();
```

- [ ] **Step 3: Insert shadow depth after AO and before lighting**

In `SceneRenderer::render_visible_frame()`, after AO registration and before deferred lighting:

```cpp
const DirectionalShadowPassOutputs directional_shadow_outputs =
	m_directional_shadow_pass.add_depth_passes(
		graph,
		frame,
		view_context,
		render_frame_index,
		[this](
			const VisibleRenderFrame& shadow_frame,
			const SceneRenderViewContext& shadow_view_context,
			RenderGraphRasterContext& context,
			uint64_t shadow_render_frame_index,
			ShadowCasterMobilityFilter mobility_filter) -> bool
		{
			return render_shadow_static_meshes_to_pass(
				shadow_frame,
				shadow_view_context,
				context,
				shadow_render_frame_index,
				mobility_filter);
		});

graph_resources.directional_shadow_dynamic_atlas = directional_shadow_outputs.dynamic_atlas;
graph_resources.directional_shadow_static_cache = directional_shadow_outputs.static_cache_atlas;
graph_resources.directional_shadow_mask = directional_shadow_outputs.shadow_mask;
```

Then call deferred lighting with:

```cpp
ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_passes(
	graph,
	frame,
	graph_resources,
	output,
	view_context,
	&m_directional_shadow_pass,
	&directional_shadow_outputs));
```

- [ ] **Step 4: Register debug view items**

Register when refs are valid:

```cpp
register_render_debug_item(
	m_render_debug_view,
	"DirectionalShadowDynamicAtlas",
	"Directional Shadow Dynamic Atlas",
	graph_resources.directional_shadow_dynamic_atlas,
	RenderDebugVisualization::Depth,
	RenderTextureFormat::D32_SFLOAT,
	get_runtime_directional_shadow_config().dynamic_atlas_size,
	get_runtime_directional_shadow_config().dynamic_atlas_size);

register_render_debug_item(
	m_render_debug_view,
	"DirectionalShadowStaticCache",
	"Directional Shadow Static Cache",
	graph_resources.directional_shadow_static_cache,
	RenderDebugVisualization::Depth,
	RenderTextureFormat::D32_SFLOAT,
	get_runtime_directional_shadow_config().static_cache_atlas_size,
	get_runtime_directional_shadow_config().static_cache_atlas_size);

register_render_debug_item(
	m_render_debug_view,
	"SceneDirectionalShadowMask",
	"Directional Shadow Mask",
	graph_resources.directional_shadow_mask,
	RenderDebugVisualization::Scalar,
	RenderTextureFormat::RGBA8_UNORM,
	output_width,
	output_height);
```

- [ ] **Step 5: Build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build succeeds. If shader reflection reports missing resources, align names with `DirectionalShadowMask.hlsl` and `DeferredDirectionalLightingShadowed.hlsl`.

- [ ] **Step 6: Commit task**

Run:

```bash
git add project/src/engine/Function/Render/SceneDeferredGraphResources.h project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/DirectionalShadowPass.cpp
git commit -m "Wire directional shadows into scene renderer"
```

---

### Task 9: Add Config Defaults And Documentation

**Files:**
- Modify: `product/config/Engine.ini`
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `docs/RenderGraphAPISpec.md`

- [ ] **Step 1: Add directional shadow defaults**

In `product/config/Engine.ini`, add:

```ini
[DirectionalShadows]
Enabled=true
DefaultCascadeCount=4
DefaultShadowDistance=160.0
NearShadowDistance=16.0
SplitLambda=0.65
NearCascadeResolution=2048
OuterCascadeResolution=1024
DynamicAtlasSize=4096
StaticCacheAtlasSize=4096
StaticCacheBudgetMB=64
DepthBias=0.0015
NormalBias=0.05
PCFRadius=1
```

- [ ] **Step 2: Update README rendering summary**

Update the shadow status bullet to state that phase one implements:

```markdown
方向光阴影第一阶段采用非 VSM 的 CSM atlas + static cache + dynamic overlay + reusable per-light screen shadow mask；方向光数量在 scene 数据层不设硬上限，shadow work 由 atlas/cache budget 降级。
```

Add this plan link under the directional shadow design link:

```markdown
- Directional CSM Shadow 实现计划：[`docs/superpowers/plans/2026-05-25-directional-csm-shadow-implementation.md`](docs/superpowers/plans/2026-05-25-directional-csm-shadow-implementation.md)
```

- [ ] **Step 3: Update `EngineDeveloperGuide.md`**

In the render config section, add `[DirectionalShadows]` and list all config keys. In the DeferredHQ section, update the path to:

```text
SceneGBufferPass
-> SceneAmbientOcclusionPass
-> SceneDirectionalShadowDepthPass
-> SceneDeferredLightingBasePass
-> per shadowed directional light:
   SceneDirectionalShadowMaskPass -> SceneDeferredDirectionalLightingShadowedPass
-> unshadowed per-light deferred passes
-> SceneDeferredCompositePass
-> SceneDeferredToneMapPass
```

Document these constraints:

- No VSM in this phase.
- Point/spot shadows remain unimplemented.
- Directional light count has no scene-level hard limit.
- Shadow memory is bounded by dynamic atlas, static cache atlas, and static cache budget.
- Cascade data is bound through a storage buffer, not deferred lighting inline constants.
- Static cache refresh and dynamic atlas copy are raster passes so they stay legal across Vulkan and DX12 without a graph copy subresource API.

- [ ] **Step 4: Update `RenderGraphAPISpec.md`**

In the scene deferred example, add:

```cpp
// 3. Directional shadow depth
pass.write_depth(directional_shadow_dynamic_atlas, RenderLoadAction::Load, { 1.0f, 0u });

// 4. Directional shadow mask before one shadowed light
pass.read_texture(scene_depth, RenderGraphAccess::GraphicsSRV);
pass.read_texture(directional_shadow_dynamic_atlas, RenderGraphAccess::GraphicsSRV);
pass.write_color(0, scene_directional_shadow_mask, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });

// 5. Shadowed directional light
pass.read_texture(scene_directional_shadow_mask, RenderGraphAccess::GraphicsSRV);
pass.write_color(0, lighting_diffuse, RenderLoadAction::Load, {});
pass.write_color(1, lighting_specular, RenderLoadAction::Load, {});
```

- [ ] **Step 5: Docs sanity**

Run:

```powershell
git diff --check
```

Expected: no whitespace errors in touched docs/config files.

- [ ] **Step 6: Commit task**

Run:

```bash
git add product/config/Engine.ini README.md docs/EngineDeveloperGuide.md docs/RenderGraphAPISpec.md
git commit -m "Document directional shadow runtime path"
```

---

### Task 10: Final Build And Vulkan/DX12 Validation

**Files:**
- All touched files.
- Generated logs go under `Intermediate/logs` or `Intermediate/test-reports`.

- [ ] **Step 1: Build Sandbox and Editor**

Run:

```powershell
./build_sandbox.bat Debug x64
./build_editor.bat Debug x64
```

Expected: both builds succeed and `Engine.dll` is synchronized into `product/bin64/Debug-windows-x86_64/`.

- [ ] **Step 2: Run self-test**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exits `0`.

- [ ] **Step 3: Run Vulkan smoke tests**

Set `[RHI] Backend=Vulkan` in `product/config/Engine.ini`, keep `[DirectionalShadows] Enabled=true`, then run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected:

- requested backend matches actual backend in logs.
- no Vulkan validation errors.
- no RenderGraph compiler errors.
- no shader binding errors for `DirectionalShadowDynamicAtlas`, `SceneDirectionalShadowMask`, or `SceneDirectionalShadowCascades`.
- no Vulkan VMA shutdown leaks.

- [ ] **Step 4: Run DX12 smoke tests**

Set `[RHI] Backend=DX12`, keep `[DirectionalShadows] Enabled=true`, then run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected:

- requested backend matches actual backend in logs.
- no DX12 debug-layer errors.
- no descriptor binding errors.
- no shutdown resource leaks.

- [ ] **Step 5: Inspect visual/debug output**

With `[RenderDebugView] Enabled=true`, inspect:

```ini
Selected=DirectionalShadowDynamicAtlas
Selected=DirectionalShadowStaticCache
Selected=SceneDirectionalShadowMask
```

Expected:

- dynamic atlas contains non-empty depth for the default directional light.
- static cache shows refreshed outer cascade tiles after the first frame.
- shadow mask is mostly white with darker regions where receivers are shadowed.
- disabling `[DirectionalShadows] Enabled=false` restores unshadowed deferred lighting.

- [ ] **Step 6: Run final diff review**

Run:

```powershell
git diff -- project/src/engine/Function/Render/DirectionalShadowConfig.h project/src/engine/Function/Render/DirectionalShadowConfig.cpp project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.cpp project/src/engine/Function/Asset/AshAssetSerializer.cpp project/src/engine/Base/EngineSelfTests.cpp project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli project/src/engine/Shaders/Shadow/DirectionalShadowDepthTileClear.hlsl project/src/engine/Shaders/Shadow/DirectionalShadowDepthCopy.hlsl project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl project/src/engine/Shaders/Deferred/DeferredDirectionalLightingShadowed.hlsl product/config/Engine.ini README.md docs/EngineDeveloperGuide.md docs/RenderGraphAPISpec.md
```

Expected: diff is scoped to first-phase directional CSM shadows and documentation.

- [ ] **Step 7: Commit validation fixes if needed**

If validation required source fixes:

```bash
git add <fixed files>
git commit -m "Fix directional shadow validation issues"
```

If validation did not require source changes, do not create an empty commit.

---

## Self-Review

- Spec coverage:
  - Non-VSM directional CSM: Tasks 2, 4, 5, 6, 7, 8.
  - No point/spot shadows: Task 7 keeps point/spot lighting unchanged.
  - Unbounded directional light count at scene level: Task 2 budgets by atlas tiles and skips shadow work without removing lights.
  - Static cache + dynamic overlay: Tasks 4 and 5.
  - High-resolution near cascade every frame: Task 2 marks cascade 0 `NearEveryFrame`; Task 5 redraws it from all casters.
  - Reusable per-light screen shadow mask: Tasks 6 and 7.
  - Cascade data outside inline constants: Task 6 uses a storage buffer.
  - Engine-only implementation and no Editor code changes: File map stays outside `project/src/editor`.
  - Vulkan/DX12 validation: Task 10.
- Red-flag scan:
  - No unresolved marker terms remain in this plan.
- Type consistency:
  - Config type is consistently `DirectionalShadowConfig`.
  - Runtime pass facade is consistently `DirectionalShadowPass`.
  - Frame output type is consistently `DirectionalShadowPassOutputs`.
  - Cache enum is consistently `DirectionalShadowCacheMode`.
  - Shadow mask graph resource is consistently `SceneDirectionalShadowMask`.
