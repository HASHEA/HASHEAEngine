# Sunlight Directional Shadow Pass Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the current directional shadow system into a single-sunlight high-quality CSM path and an unlimited ordinary directional-light transient cascade path.

**Architecture:** `LightComponent::sunlight` defines the semantic sun role, and scene loading rejects more than one directional sunlight. `SunLightShadowPass` owns the existing static-cache/dynamic-overlay/mask path for that one sunlight, while `DirectionalLightShadowPass` renders one ordinary directional light at a time immediately before its deferred lighting pass. `SceneRenderer` becomes the shadow/lighting orchestrator and `DeferredLightingPass` exposes explicit base and per-light submission methods.

**Tech Stack:** C++17, AshEngine Function/Scene, Function/Render RenderGraph, HLSL deferred/shadow shaders, Premake + MSBuild, Vulkan and DX12 shared render path.

---

## File Structure

- Modify `project/src/engine/Function/Scene/SceneComponents.h`
  - Add `LightComponent::sunlight`.
- Modify `project/src/engine/Function/Scene/Scene.cpp`
  - Add reflection metadata, load/save JSON, environment sunlight setup, and one-sunlight validation.
- Modify `project/src/engine/Function/Render/RenderScene.h`
  - Add `VisibleLightData::sunlight`.
- Modify `project/src/engine/Function/Render/RenderScene.cpp`
  - Copy sanitized sunlight data from scene lights into visible frame lights.
- Create `project/src/engine/Function/Render/SunLightShadowPass.h`
  - Public high-quality sunlight shadow pass API.
- Create `project/src/engine/Function/Render/SunLightShadowPass.cpp`
  - Moved and renamed current high-quality directional shadow implementation.
- Create `project/src/engine/Function/Render/DirectionalLightShadowPass.h`
  - Public ordinary directional-light transient shadow pass API.
- Create `project/src/engine/Function/Render/DirectionalLightShadowPass.cpp`
  - New per-light transient cascade atlas and mask implementation.
- Modify `project/src/engine/Function/Render/DirectionalShadowPass.h`
  - Remove the pass class after the split, or convert it into shared directional shadow types if the implementation chooses not to create a separate `DirectionalShadowTypes.h`.
- Modify `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
  - Remove after the split, or move shared helpers into `SunLightShadowPass.cpp` / `DirectionalLightShadowPass.cpp`.
- Modify `project/src/engine/Function/Render/DeferredLightingPass.h`
  - Replace monolithic lighting loop API with base and per-light methods.
- Modify `project/src/engine/Function/Render/DeferredLightingPass.cpp`
  - Move existing base, directional, point, and spot pass bodies into separate methods.
- Modify `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
  - Rename high-quality debug resources to sunlight resources and add last ordinary directional transient resources for debug view.
- Modify `project/src/engine/Function/Render/SceneRenderer.h`
  - Own both pass instances.
- Modify `project/src/engine/Function/Render/SceneRenderer.cpp`
  - Orchestrate sunlight shadow once, then ordinary directional shadow immediately before each ordinary directional lighting pass.
- Modify `project/src/engine/Base/EngineSelfTests.cpp`
  - Add scene serialization, sunlight validation, render snapshot, planner, and graph ordering self-tests.
- Modify `docs/EngineDeveloperGuide.md`
  - Document `sunlight`, the pass split, RenderGraph ordering, and validation expectations.
- Modify `README.md`
  - Update the directional shadow summary and document index.

---

### Task 1: Scene Sunlight Data Contract

**Files:**
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `README.md`

- [ ] **Step 1: Write failing scene sunlight self-tests**

Insert these tests in `project/src/engine/Base/EngineSelfTests.cpp` after `test_render_scene_extracts_light_snapshot()`:

```cpp
auto test_scene_light_sunlight_json_round_trip() -> bool
{
	const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
	std::filesystem::create_directories(test_dir);
	const std::filesystem::path scene_path = test_dir / "sunlight.scene.json";

	{
		std::ofstream file(scene_path, std::ios::trunc);
		file <<
			"{\n"
			"  \"version\": 4,\n"
			"  \"name\": \"SunlightRoundTrip\",\n"
			"  \"next_entity_id\": 3,\n"
			"  \"entities\": [\n"
			"    {\n"
			"      \"id\": 1,\n"
			"      \"name\": \"Sun\",\n"
			"      \"transform\": {},\n"
			"      \"light\": {\n"
			"        \"type\": 0,\n"
			"        \"sunlight\": true,\n"
			"        \"casts_shadow\": true\n"
			"      }\n"
			"    },\n"
			"    {\n"
			"      \"id\": 2,\n"
			"      \"name\": \"Point\",\n"
			"      \"transform\": {},\n"
			"      \"light\": {\n"
			"        \"type\": 1,\n"
			"        \"sunlight\": true,\n"
			"        \"range\": 8.0\n"
			"      }\n"
			"    }\n"
			"  ]\n"
			"}\n";
	}

	std::string error{};
	Scene scene = Scene::load_from_file(scene_path, &error);
	if (!scene.is_valid())
	{
		return report_self_test_failure("Scene sunlight JSON", error.empty() ? "sunlight scene failed to load" : error.c_str());
	}

	Entity sun = scene.find_entity(1u);
	Entity point = scene.find_entity(2u);
	if (!sun.is_valid() || !point.is_valid() || !sun.has_light_component() || !point.has_light_component())
	{
		return report_self_test_failure("Scene sunlight JSON", "sunlight test entities were not loaded");
	}
	const LightComponent sun_light = sun.get_light_component();
	const LightComponent point_light = point.get_light_component();
	if (!sun_light.sunlight || point_light.sunlight)
	{
		return report_self_test_failure("Scene sunlight JSON", "directional sunlight was not preserved or point sunlight was not sanitized");
	}

	const std::filesystem::path saved_path = test_dir / "sunlight.saved.scene.json";
	if (!scene.save_to_file(saved_path, &error))
	{
		return report_self_test_failure("Scene sunlight JSON", error.empty() ? "sunlight scene failed to save" : error.c_str());
	}

	Scene round_trip = Scene::load_from_file(saved_path, &error);
	if (!round_trip.is_valid())
	{
		return report_self_test_failure("Scene sunlight JSON", error.empty() ? "saved sunlight scene failed to load" : error.c_str());
	}
	const bool ok =
		round_trip.find_entity(1u).get_light_component().sunlight &&
		!round_trip.find_entity(2u).get_light_component().sunlight;
	return ok || report_self_test_failure("Scene sunlight JSON", "sunlight did not survive save/load round trip");
}

auto test_scene_rejects_multiple_directional_sunlights() -> bool
{
	const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
	std::filesystem::create_directories(test_dir);
	const std::filesystem::path scene_path = test_dir / "two_suns.scene.json";

	{
		std::ofstream file(scene_path, std::ios::trunc);
		file <<
			"{\n"
			"  \"version\": 4,\n"
			"  \"name\": \"TwoSuns\",\n"
			"  \"next_entity_id\": 3,\n"
			"  \"entities\": [\n"
			"    { \"id\": 1, \"name\": \"SunA\", \"transform\": {}, \"light\": { \"type\": 0, \"sunlight\": true } },\n"
			"    { \"id\": 2, \"name\": \"SunB\", \"transform\": {}, \"light\": { \"type\": 0, \"sunlight\": true } }\n"
			"  ]\n"
			"}\n";
	}

	std::string error{};
	Scene scene = Scene::load_from_file(scene_path, &error);
	const bool ok =
		!scene.is_valid() &&
		error.find("sunlight") != std::string::npos;
	return ok || report_self_test_failure("Scene sunlight validation", "scene accepted two directional sunlight components");
}

auto test_scene_environment_metadata_creates_sunlight() -> bool
{
	const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
	std::filesystem::create_directories(test_dir);
	const std::filesystem::path ibl_path = test_dir / "metadata_sun.ashibl";
	const std::filesystem::path scene_path = test_dir / "environment_sun.scene.json";

	EnvironmentMapCookedData cooked{};
	fill_environment_map_test_pattern(cooked);
	cooked.dominant_light.valid = true;
	cooked.dominant_light.direction = glm::normalize(glm::vec3(0.25f, 0.9f, 0.15f));
	cooked.dominant_light.luminance = 8.0f;
	cooked.dominant_light.source = "self-test";

	std::string error{};
	if (!write_ashibl_file(ibl_path, cooked, &error))
	{
		return report_self_test_failure("Scene environment sunlight", error.empty() ? "failed to write test ashibl" : error.c_str());
	}

	{
		std::ofstream file(scene_path, std::ios::trunc);
		file <<
			"{\n"
			"  \"version\": 4,\n"
			"  \"name\": \"EnvironmentSun\",\n"
			"  \"next_entity_id\": 2,\n"
			"  \"entities\": [\n"
			"    {\n"
			"      \"id\": 1,\n"
			"      \"name\": \"Environment\",\n"
			"      \"transform\": {},\n"
			"      \"environment\": {\n"
			"        \"active\": true,\n"
			"        \"ibl_asset_path\": \"" << ibl_path.generic_string() << "\"\n"
			"      }\n"
			"    }\n"
			"  ]\n"
			"}\n";
	}

	Scene scene = Scene::load_from_file(scene_path, &error);
	if (!scene.is_valid())
	{
		return report_self_test_failure("Scene environment sunlight", error.empty() ? "environment scene failed to load" : error.c_str());
	}

	Entity environment_sun{};
	for (const Entity& entity : scene.get_entities())
	{
		if (entity.is_valid() && entity.get_name() == "EnvironmentSunLight")
		{
			environment_sun = entity;
			break;
		}
	}

	const bool ok =
		environment_sun.is_valid() &&
		environment_sun.has_light_component() &&
		environment_sun.get_light_component().type == LightType::Directional &&
		environment_sun.get_light_component().sunlight &&
		environment_sun.get_light_component().casts_shadow;
	return ok || report_self_test_failure("Scene environment sunlight", "environment metadata did not create a shadow-casting sunlight");
}
```

Add these calls near the existing scene self-test calls:

```cpp
all_passed = test_scene_light_sunlight_json_round_trip() && all_passed;
all_passed = test_scene_rejects_multiple_directional_sunlights() && all_passed;
all_passed = test_scene_environment_metadata_creates_sunlight() && all_passed;
```

- [ ] **Step 2: Run the failing check**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: build fails because `LightComponent` has no `sunlight` member.

- [ ] **Step 3: Add `LightComponent::sunlight`**

Modify `project/src/engine/Function/Scene/SceneComponents.h`:

```cpp
struct LightComponent
{
	LightType type = LightType::Directional;
	glm::vec3 color{ 1.0f, 1.0f, 1.0f };
	float intensity = 1.0f;
	float range = 10.0f;
	float inner_cone_angle_degrees = 30.0f;
	float outer_cone_angle_degrees = 45.0f;
	bool casts_shadow = true;
	bool sunlight = false;
	uint32_t shadow_priority = 128;
	float shadow_distance = 0.0f;
	uint32_t shadow_cascade_count = 0;
	float near_shadow_distance = 0.0f;
};
```

- [ ] **Step 4: Add scene metadata, load/save, environment setup, and validation**

Modify `project/src/engine/Function/Scene/Scene.cpp` light property metadata:

```cpp
{ "casts_shadow", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(LightComponent, casts_shadow)), static_cast<uint32_t>(sizeof(bool)), nullptr },
{ "sunlight", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(LightComponent, sunlight)), static_cast<uint32_t>(sizeof(bool)), nullptr },
{ "shadow_priority", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(LightComponent, shadow_priority)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
```

Add helpers in the anonymous namespace near the environment-sun helpers:

```cpp
static auto sanitize_light_component(LightComponent component) -> LightComponent
{
	if (component.type != LightType::Directional)
	{
		component.sunlight = false;
	}
	return component;
}

static auto validate_single_directional_sunlight(const Scene& scene, std::string* out_error) -> bool
{
	uint32_t sunlight_count = 0u;
	for (const Entity& entity : scene.get_entities())
	{
		if (!entity.is_valid() || !entity.has_light_component())
		{
			continue;
		}
		const LightComponent light = entity.get_light_component();
		if (light.type == LightType::Directional && light.sunlight)
		{
			++sunlight_count;
		}
	}

	if (sunlight_count > 1u)
	{
		make_scene_error(out_error, "Scene contains more than one directional sunlight.");
		return false;
	}
	return true;
}
```

Sanitize direct component writes:

```cpp
bool Entity::add_light_component(const LightComponent& component)
{
	return emplace_or_replace_entity_component(
		std::static_pointer_cast<Scene::Impl>(m_impl),
		m_id,
		sanitize_light_component(component));
}
```

Read and save JSON:

```cpp
light.casts_shadow = light_json.value("casts_shadow", light.casts_shadow);
light.sunlight = light_json.value("sunlight", light.sunlight);
light.shadow_priority = light_json.value("shadow_priority", light.shadow_priority);
light = sanitize_light_component(light);
```

```cpp
{ "casts_shadow", light->casts_shadow },
{ "sunlight", light->sunlight },
{ "shadow_priority", light->shadow_priority },
```

Set the environment-created light as sunlight:

```cpp
sun_light.type = LightType::Directional;
sun_light.sunlight = true;
sun_light.color = glm::vec3(1.0f, 0.95f, 0.88f);
sun_light.intensity = k_environment_sun_light_intensity;
sun_light.casts_shadow = true;
```

Validate after environment synchronization and before the loaded scene is returned:

```cpp
ASH_PROCESS_ERROR(create_or_update_environment_sun_light(scene, out_error));
ASH_PROCESS_ERROR(validate_single_directional_sunlight(scene, out_error));
```

- [ ] **Step 5: Run the scene sunlight self-tests**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds, self-test output reports all engine self-tests passed.

- [ ] **Step 6: Commit the data contract**

```powershell
git add project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.cpp project/src/engine/Base/EngineSelfTests.cpp README.md
git commit -m "Add scene sunlight light flag"
```

---

### Task 2: RenderScene Sunlight Snapshot

**Files:**
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`

- [ ] **Step 1: Extend the visible light snapshot self-test**

In `test_render_scene_extracts_light_snapshot()`, set the directional light as sunlight:

```cpp
directional_light.sunlight = true;
```

Extend `directional_ok`:

```cpp
frame.lights[0].sunlight &&
frame.lights[0].shadow_priority == 42u &&
```

Add a point-light sanitation check:

```cpp
point_light.sunlight = true;
```

Extend `point_ok`:

```cpp
!frame.lights[1].sunlight &&
frame.lights[1].range == 7.0f;
```

- [ ] **Step 2: Run the failing snapshot check**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: build fails because `VisibleLightData` has no `sunlight` member.

- [ ] **Step 3: Add `VisibleLightData::sunlight`**

Modify `project/src/engine/Function/Render/RenderScene.h`:

```cpp
float intensity = 1.0f;
bool casts_shadow = true;
bool sunlight = false;
uint32_t shadow_priority = 128;
```

- [ ] **Step 4: Copy sanitized sunlight into the render snapshot**

Modify `make_visible_light_data()` in `project/src/engine/Function/Render/RenderScene.cpp`:

```cpp
out_light.intensity = light.intensity;
out_light.casts_shadow = light.casts_shadow;
out_light.sunlight = light.type == LightType::Directional && light.sunlight;
out_light.shadow_priority = light.shadow_priority;
```

- [ ] **Step 5: Run the snapshot check**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test output reports all engine self-tests passed.

- [ ] **Step 6: Commit the render snapshot change**

```powershell
git add project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Propagate sunlight into render light snapshots"
```

---

### Task 3: Rename High-Quality Directional Shadow Path To `SunLightShadowPass`

**Files:**
- Rename: `project/src/engine/Function/Render/DirectionalShadowPass.h` to `project/src/engine/Function/Render/SunLightShadowPass.h`
- Rename: `project/src/engine/Function/Render/DirectionalShadowPass.cpp` to `project/src/engine/Function/Render/SunLightShadowPass.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`

- [ ] **Step 1: Write failing sunlight planner tests**

Rename existing high-quality planner test names from `directional_shadow` to `sunlight_shadow` and set every light that should use the high-quality planner to:

```cpp
light.sunlight = true;
```

Add this test next to the planner tests:

```cpp
auto test_sunlight_shadow_planner_ignores_ordinary_directional_lights() -> bool
{
	DirectionalShadowConfig config = make_default_directional_shadow_config();
	config.default_cascade_count = 2u;
	config.dynamic_atlas_size = 2048u;
	config.near_cascade_resolution = 1024u;
	config.outer_cascade_resolution = 512u;

	VisibleRenderFrame frame{};
	frame.reverse_z = false;
	frame.camera_position = { 0.0f, 2.0f, -8.0f };
	frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
	frame.view_projection = frame.projection * frame.view;

	VisibleLightData ordinary{};
	ordinary.entity_id = 1u;
	ordinary.type = LightType::Directional;
	ordinary.direction_ws = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.1f));
	ordinary.casts_shadow = true;
	ordinary.sunlight = false;
	frame.lights.push_back(ordinary);

	VisibleLightData sunlight = ordinary;
	sunlight.entity_id = 2u;
	sunlight.sunlight = true;
	frame.lights.push_back(sunlight);

	DirectionalShadowFramePlan plan{};
	const bool ok_plan = build_sunlight_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan);
	const bool ok =
		ok_plan &&
		plan.input_directional_shadow_light_count == 1u &&
		plan.shadowed_lights.size() == 1u &&
		plan.shadowed_lights[0].frame_light_index == 1u;
	return ok || report_self_test_failure("SunLightShadow planner", "sunlight planner included ordinary directional lights");
}
```

Register the new test:

```cpp
all_passed = test_sunlight_shadow_planner_ignores_ordinary_directional_lights() && all_passed;
```

- [ ] **Step 2: Run the failing compile**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: build fails because `SunLightShadowPass` and `build_sunlight_shadow_frame_plan_for_tests` do not exist.

- [ ] **Step 3: Move and rename the high-quality pass**

Run:

```powershell
git mv project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/SunLightShadowPass.h
git mv project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Function/Render/SunLightShadowPass.cpp
```

Edit the renamed files:

```cpp
#include "Function/Render/SunLightShadowPass.h"
```

```cpp
class SunLightShadowPass;
```

```cpp
struct SunLightShadowPassOutputs
{
	RenderGraphTextureRef dynamic_atlas{};
	RenderGraphTextureRef static_cache_atlas{};
	RenderGraphTextureRef shadow_mask{};
	RenderGraphTextureRef cascade_debug{};
	DirectionalShadowFramePlan plan{};
	std::shared_ptr<StorageBuffer> cascade_buffer = nullptr;
	bool has_shadowed_lights() const { return !plan.shadowed_lights.empty() && dynamic_atlas && shadow_mask; }
};
```

```cpp
class SunLightShadowPass
{
public:
	bool initialize(Renderer* renderer);
	void shutdown();
	SunLightShadowPassOutputs add_depth_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		const DirectionalShadowConfig& config,
		uint64_t render_frame_index,
		const DirectionalShadowCasterDrawCallback& draw_callback);
	bool add_shadow_mask_pass(
		RenderGraphBuilder& graph,
		const SunLightShadowPassOutputs& outputs,
		uint32_t shadowed_light_plan_index,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context);
	bool add_cascade_debug_pass(
		RenderGraphBuilder& graph,
		const SunLightShadowPassOutputs& outputs,
		RenderGraphTextureRef scene_depth,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context);
};
```

- [ ] **Step 4: Filter only sunlight in the high-quality planner**

Modify the candidate collection in `SunLightShadowPass.cpp`:

```cpp
for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
{
	const VisibleLightData& light = frame.lights[light_index];
	if (light.type != LightType::Directional || !light.sunlight || !light.casts_shadow)
	{
		continue;
	}
	if (glm::length(light.direction_ws) <= 0.0001f)
	{
		continue;
	}
	candidates.push_back({ light_index, light });
}

out_plan.input_directional_shadow_light_count = static_cast<uint32_t>(candidates.size());
if (candidates.size() > 1u)
{
	return false;
}
```

Rename the test helper:

```cpp
ASH_API bool build_sunlight_shadow_frame_plan_for_tests(
	const VisibleRenderFrame& frame,
	const DirectionalShadowConfig& config,
	uint32_t output_width,
	uint32_t output_height,
	DirectionalShadowFramePlan& out_plan);
```

- [ ] **Step 5: Rename debug and RenderDoc pass names for sunlight**

Use these strings in the renamed high-quality pass:

```cpp
"SunLightShadowCascadeBuffer"
"SunLightShadowDynamicAtlas"
"SunLightShadowStaticCache"
"SceneSunLightShadowMask"
"SceneSunLightShadowCascadeIndex"
"SceneSunLightShadowDynamicAtlasClearPass"
"SceneSunLightShadowStaticCacheRefreshPass_"
"SceneSunLightShadowDynamicCascadePass_"
"SceneSunLightShadowMaskPass_"
"SceneSunLightShadowCascadeDebugPass"
```

- [ ] **Step 6: Update includes and owner member names**

Change includes that used the old pass header:

```cpp
#include "Function/Render/SunLightShadowPass.h"
```

In `SceneRenderer.h`:

```cpp
SunLightShadowPass m_sunlight_shadow_pass{};
```

In `DeferredLightingPass.h`, forward declare the sunlight pass until Task 5 removes the pass dependency:

```cpp
class SunLightShadowPass;
struct SunLightShadowPassOutputs;
```

- [ ] **Step 7: Run tests for the renamed sunlight pass**

Run:

```powershell
.\generate_vs2022.bat
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: Premake regenerates project files, build succeeds, self-tests pass.

- [ ] **Step 8: Commit the sunlight rename**

```powershell
git add project/src/engine/Function/Render/SunLightShadowPass.h project/src/engine/Function/Render/SunLightShadowPass.cpp project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git add -u project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp
git commit -m "Rename high quality directional shadows to sunlight pass"
```

---

### Task 4: Ordinary `DirectionalLightShadowPass`

**Files:**
- Create: `project/src/engine/Function/Render/DirectionalLightShadowPass.h`
- Create: `project/src/engine/Function/Render/DirectionalLightShadowPass.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing ordinary directional pass planner tests**

Add tests near the sunlight planner tests:

```cpp
auto test_directional_light_shadow_planner_has_no_shared_light_budget() -> bool
{
	DirectionalShadowConfig config = make_default_directional_shadow_config();
	config.default_cascade_count = 4u;
	config.default_shadow_distance = 160.0f;
	config.near_shadow_distance = 16.0f;
	config.dynamic_atlas_size = 2048u;
	config.near_cascade_resolution = 1024u;
	config.outer_cascade_resolution = 512u;

	VisibleRenderFrame frame{};
	frame.reverse_z = true;
	frame.camera_position = { 0.0f, 0.0f, -10.0f };
	frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
	frame.view_projection = frame.projection * frame.view;

	for (uint32_t index = 0; index < 12u; ++index)
	{
		VisibleLightData light{};
		light.entity_id = 100u + index;
		light.type = LightType::Directional;
		light.direction_ws = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
		light.casts_shadow = true;
		light.sunlight = false;
		frame.lights.push_back(light);
	}

	for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
	{
		DirectionalShadowFramePlan plan{};
		if (!build_directional_light_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, light_index, plan))
		{
			return report_self_test_failure("DirectionalLightShadow planner", "ordinary directional planner failed");
		}
		if (plan.input_directional_shadow_light_count != 1u ||
			plan.skipped_shadow_light_count != 0u ||
			plan.shadowed_lights.size() != 1u ||
			plan.shadowed_lights[0].frame_light_index != light_index ||
			plan.cascades.size() != 4u)
		{
			return report_self_test_failure("DirectionalLightShadow planner", "ordinary directional planner used shared light budget or wrong light index");
		}
	}
	return true;
}

auto test_directional_light_shadow_planner_ignores_sunlight() -> bool
{
	DirectionalShadowConfig config = make_default_directional_shadow_config();
	VisibleRenderFrame frame{};
	frame.reverse_z = false;
	frame.camera_position = { 0.0f, 2.0f, -8.0f };
	frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
	frame.view_projection = frame.projection * frame.view;

	VisibleLightData sunlight{};
	sunlight.entity_id = 42u;
	sunlight.type = LightType::Directional;
	sunlight.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
	sunlight.casts_shadow = true;
	sunlight.sunlight = true;
	frame.lights.push_back(sunlight);

	DirectionalShadowFramePlan plan{};
	const bool ok =
		build_directional_light_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, 0u, plan) &&
		plan.input_directional_shadow_light_count == 0u &&
		plan.shadowed_lights.empty() &&
		plan.cascades.empty();
	return ok || report_self_test_failure("DirectionalLightShadow planner", "ordinary directional planner accepted sunlight");
}

auto test_directional_light_shadow_uses_all_shadow_casters() -> bool
{
	return get_directional_light_shadow_caster_filter_for_tests() == ShadowCasterMobilityFilter::All ||
		report_self_test_failure("DirectionalLightShadow caster filter", "ordinary directional shadows must draw all casters every frame");
}
```

Register the tests:

```cpp
all_passed = test_directional_light_shadow_planner_has_no_shared_light_budget() && all_passed;
all_passed = test_directional_light_shadow_planner_ignores_sunlight() && all_passed;
all_passed = test_directional_light_shadow_uses_all_shadow_casters() && all_passed;
```

- [ ] **Step 2: Run the failing compile**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: build fails because `build_directional_light_shadow_frame_plan_for_tests` and `get_directional_light_shadow_caster_filter_for_tests` do not exist.

- [ ] **Step 3: Create `DirectionalLightShadowPass.h`**

Create `project/src/engine/Function/Render/DirectionalLightShadowPass.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SunLightShadowPass.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	class StorageBuffer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;

	struct DirectionalLightShadowPassOutputs
	{
		RenderGraphTextureRef dynamic_atlas{};
		RenderGraphTextureRef shadow_mask{};
		DirectionalShadowFramePlan plan{};
		std::shared_ptr<StorageBuffer> cascade_buffer = nullptr;
		uint32_t frame_light_index = 0u;
		bool has_shadow() const { return dynamic_atlas && shadow_mask && cascade_buffer && !plan.shadowed_lights.empty(); }
	};

	class DirectionalLightShadowPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		DirectionalLightShadowPassOutputs add_shadow_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			uint32_t frame_light_index,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowConfig& config,
			uint64_t render_frame_index,
			const DirectionalShadowCasterDrawCallback& draw_callback);
		bool add_shadow_mask_pass(
			RenderGraphBuilder& graph,
			const DirectionalLightShadowPassOutputs& outputs,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		bool build_frame_plan(
			const VisibleRenderFrame& frame,
			const DirectionalShadowConfig& config,
			uint32_t output_width,
			uint32_t output_height,
			uint32_t frame_light_index,
			DirectionalShadowFramePlan& out_plan) const;
		std::shared_ptr<StorageBuffer> create_cascade_buffer(
			const DirectionalShadowFramePlan& plan,
			uint32_t atlas_size,
			uint32_t frame_light_index) const;

	private:
		Renderer* m_renderer = nullptr;
		DirectionalShadowConfig m_config{};
		std::unique_ptr<GraphicsProgram> m_tile_clear_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_shadow_mask_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
	};

	ASH_API bool build_directional_light_shadow_frame_plan_for_tests(
		const VisibleRenderFrame& frame,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		uint32_t frame_light_index,
		DirectionalShadowFramePlan& out_plan);

	ASH_API ShadowCasterMobilityFilter get_directional_light_shadow_caster_filter_for_tests();
}
```

- [ ] **Step 4: Implement ordinary per-light planning**

Create `project/src/engine/Function/Render/DirectionalLightShadowPass.cpp` and reuse the cascade math helpers from the sunlight pass. The planner must use this candidate gate:

```cpp
if (frame_light_index >= frame.lights.size())
{
	out_plan = {};
	return true;
}

const VisibleLightData& light = frame.lights[frame_light_index];
if (light.type != LightType::Directional || light.sunlight || !light.casts_shadow || glm::length(light.direction_ws) <= 0.0001f)
{
	out_plan = {};
	return true;
}
```

The planner must allocate one light without a global light budget:

```cpp
out_plan = {};
out_plan.input_directional_shadow_light_count = 1u;
out_plan.dynamic_tiles.atlas_size = config.dynamic_atlas_size;

DirectionalShadowLightPlan light_plan{};
light_plan.frame_light_index = frame_light_index;
light_plan.light_plan_index = 0u;
light_plan.light_entity_id = light.entity_id;
light_plan.first_cascade = 0u;
light_plan.cascade_count = resolve_light_cascade_count(light, config);
light_plan.light_direction_ws = glm::normalize(light.direction_ws);
light_plan.shadowed = true;
```

Each cascade must use dynamic tiles and no static cache:

```cpp
DirectionalShadowCascadePlan cascade_plan{};
cascade_plan.light_plan_index = 0u;
cascade_plan.light_entity_id = light.entity_id;
cascade_plan.cascade_index = cascade_index;
cascade_plan.split_near = split_near;
cascade_plan.split_far = split_far;
cascade_plan.depth_bias = config.depth_bias;
cascade_plan.normal_bias = config.normal_bias;
cascade_plan.light_view_projection = build_cascade_light_view_projection(frame, light.direction_ws, split_near, split_far);
cascade_plan.dynamic_tile = dynamic_tile;
cascade_plan.cache_mode = DirectionalShadowCacheMode::NearEveryFrame;
cascade_plan.has_static_cache_tile = false;
out_plan.cascades.push_back(cascade_plan);
```

If any tile cannot fit in a single-light atlas, return an empty plan and set `skipped_shadow_light_count = 1u`. Do not sort lights and do not count other ordinary lights.

- [ ] **Step 5: Create immutable per-light cascade buffers**

Implement `create_cascade_buffer()` so each ordinary light receives a distinct storage buffer snapshot:

```cpp
StorageBufferDesc desc{};
desc.size = required_size;
desc.stride = static_cast<uint32_t>(sizeof(DirectionalShadowCascadeShaderData));
desc.initial_data = cascade_data.data();
const std::string buffer_name = "DirectionalLightShadowCascadeBuffer_" + std::to_string(frame_light_index);
desc.name = buffer_name.c_str();
return m_renderer->create_storage_buffer(desc);
```

Do not update a shared `m_cascade_buffer` for ordinary directional lights. Graph execution happens after graph construction, so a shared mutable buffer can be overwritten before a shadow mask pass reads it.

- [ ] **Step 6: Add ordinary atlas, cascade, and mask passes**

`add_shadow_passes()` must create resources named by frame light index:

```cpp
const std::string atlas_name = "SceneDirectionalLightShadowAtlas_" + std::to_string(frame_light_index);
const std::string mask_name = "SceneDirectionalLightShadowMask_" + std::to_string(frame_light_index);
outputs.dynamic_atlas = graph.create_texture(dynamic_desc, atlas_name.c_str());
outputs.shadow_mask = graph.create_texture(mask_desc, mask_name.c_str());
outputs.cascade_buffer = create_cascade_buffer(outputs.plan, m_config.dynamic_atlas_size, frame_light_index);
```

Register clear and depth passes with these names:

```cpp
"SceneDirectionalLightShadowAtlasClearPass_" + std::to_string(frame_light_index)
"SceneDirectionalLightShadowCascadePass_" + std::to_string(frame_light_index) + "_" + std::to_string(cascade_index)
```

Every ordinary cascade draw must use:

```cpp
ShadowCasterMobilityFilter::All
```

`add_shadow_mask_pass()` should mirror the sunlight mask path, but bind `outputs.dynamic_atlas`, `outputs.shadow_mask`, and `outputs.cascade_buffer`. The pass name must be:

```cpp
"SceneDirectionalLightShadowMaskPass_" + std::to_string(outputs.frame_light_index)
```

- [ ] **Step 7: Run ordinary directional pass self-tests**

Run:

```powershell
.\generate_vs2022.bat
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-tests pass and the generated solution includes `DirectionalLightShadowPass.cpp/.h`.

- [ ] **Step 8: Commit ordinary directional shadow pass**

```powershell
git add project/src/engine/Function/Render/DirectionalLightShadowPass.h project/src/engine/Function/Render/DirectionalLightShadowPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add transient ordinary directional shadow pass"
```

---

### Task 5: Split `DeferredLightingPass` Into Base And Per-Light Methods

**Files:**
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write the graph ordering self-test**

Replace `test_directional_shadow_deferred_graph_contract()` with a graph that has two ordinary directional light shadow masks and verifies each mask lifetime ends at its own lighting pass:

```cpp
const bool ok =
	compiled &&
	result.live_pass_indices.size() == 10u &&
	result.texture_lifetimes[first_shadow_mask.index].first_pass == 4u &&
	result.texture_lifetimes[first_shadow_mask.index].last_pass == 5u &&
	result.texture_lifetimes[second_shadow_mask.index].first_pass == 6u &&
	result.texture_lifetimes[second_shadow_mask.index].last_pass == 7u &&
	result.texture_lifetimes[resources.lighting_diffuse.index].first_pass == 3u &&
	result.texture_lifetimes[resources.lighting_diffuse.index].last_pass == 8u;
```

Use pass names:

```cpp
"SceneDirectionalLightShadowMaskPass_0"
"SceneDeferredDirectionalLightingShadowedPass_0"
"SceneDirectionalLightShadowMaskPass_1"
"SceneDeferredDirectionalLightingShadowedPass_1"
```

- [ ] **Step 2: Run the graph test**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: the ordering test fails until the renderer uses per-light shadow/mask ordering.

- [ ] **Step 3: Replace the `DeferredLightingPass` public API**

Modify `DeferredLightingPass.h`:

```cpp
bool add_base_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context);

bool add_directional_light_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context,
	uint32_t frame_light_index,
	RenderGraphTextureRef shadow_mask = {});

bool add_point_light_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context,
	uint32_t frame_light_index);

bool add_spot_light_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context,
	uint32_t frame_light_index);
```

Remove the `DirectionalShadowPass*` and `DirectionalShadowPassOutputs*` parameters from `DeferredLightingPass`.

- [ ] **Step 4: Move the existing base pass body into `add_base_pass()`**

Move lines equivalent to the current `SceneDeferredLightingBasePass` registration into:

```cpp
bool DeferredLightingPass::add_base_pass(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context)
```

Keep the pass name:

```cpp
"SceneDeferredLightingBasePass"
```

Keep the same Tracy scope:

```cpp
ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_base_pass", AshEngine::Profile::Color::Scene);
```

- [ ] **Step 5: Move the directional light body into `add_directional_light_pass()`**

Implement directional pass naming:

```cpp
const bool use_directional_shadow = shadow_mask.is_valid();
const std::string light_pass_name =
	use_directional_shadow ?
	"SceneDeferredDirectionalLightingShadowedPass_" + std::to_string(frame_light_index) :
	"SceneDeferredDirectionalLightingPass_" + std::to_string(frame_light_index);
```

Resource declaration:

```cpp
if (use_directional_shadow)
{
	pass.read_texture(shadow_mask, RenderGraphAccess::GraphicsSRV);
}
pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Load, k_lighting_accum_clear_color);
pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Load, k_lighting_accum_clear_color);
```

Execution:

```cpp
const VisibleLightData& light = frame.lights[frame_light_index];
if (use_directional_shadow)
{
	std::shared_ptr<RenderTarget> resolved_shadow_mask = context.get_texture(shadow_mask);
	ASH_PROCESS_ERROR(resolved_shadow_mask != nullptr);
	ASH_PROCESS_ERROR(m_shadowed_directional_program->set_texture("SceneDirectionalShadowMask", resolved_shadow_mask));
	ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
		m_shadowed_directional_program.get(),
		make_directional_constants(frame, output, light),
		view_context)));
}
else
{
	ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
		m_directional_program.get(),
		make_directional_constants(frame, output, light),
		view_context)));
}
```

- [ ] **Step 6: Move point and spot bodies into dedicated methods**

Point pass names:

```cpp
"SceneDeferredPointLightingPass_" + std::to_string(frame_light_index)
```

Spot pass names:

```cpp
"SceneDeferredSpotLightingPass_" + std::to_string(frame_light_index)
```

Both methods must keep the current GBuffer, AO, depth, lighting-diffuse, and lighting-specular resource declarations.

- [ ] **Step 7: Keep `add_passes()` as a no-shadow compatibility wrapper**

Implement `add_passes()` as:

```cpp
ASH_PROCESS_ERROR(add_base_pass(graph, frame, deferred_resources, output_target, view_context));
for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
{
	const VisibleLightData& light = frame.lights[light_index];
	if (light.type == LightType::Directional)
	{
		ASH_PROCESS_ERROR(add_directional_light_pass(graph, frame, deferred_resources, output_target, view_context, light_index));
	}
	else if (light.type == LightType::Point)
	{
		ASH_PROCESS_ERROR(add_point_light_pass(graph, frame, deferred_resources, output_target, view_context, light_index));
	}
	else if (light.type == LightType::Spot)
	{
		ASH_PROCESS_ERROR(add_spot_light_pass(graph, frame, deferred_resources, output_target, view_context, light_index));
	}
}
ASH_PROCESS_ERROR(add_composite_pass(graph, frame, deferred_resources, output_target, view_context));
```

The main scene renderer will use the explicit methods.

- [ ] **Step 8: Run self-tests**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds. The ordering self-test can still fail until Task 6 wires `SceneRenderer`.

- [ ] **Step 9: Commit the deferred lighting API split**

```powershell
git add project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Split deferred lighting pass submission"
```

---

### Task 6: SceneRenderer Shadow And Lighting Orchestration

**Files:**
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Own both shadow passes**

Modify `SceneRenderer.h` includes:

```cpp
#include "Function/Render/DirectionalLightShadowPass.h"
#include "Function/Render/SunLightShadowPass.h"
```

Modify members:

```cpp
SunLightShadowPass m_sunlight_shadow_pass{};
DirectionalLightShadowPass m_directional_light_shadow_pass{};
```

- [ ] **Step 2: Initialize and shut down both pass instances**

In `SceneRenderer::initialize()`:

```cpp
ASH_PROCESS_ERROR(m_sunlight_shadow_pass.initialize(renderer));
ASH_PROCESS_ERROR(m_directional_light_shadow_pass.initialize(renderer));
```

In `SceneRenderer::shutdown()`:

```cpp
m_directional_light_shadow_pass.shutdown();
m_sunlight_shadow_pass.shutdown();
```

- [ ] **Step 3: Rename graph resource fields**

Modify `SceneDeferredGraphResources.h`:

```cpp
RenderGraphTextureRef sunlight_shadow_dynamic_atlas{};
RenderGraphTextureRef sunlight_shadow_static_cache{};
RenderGraphTextureRef sunlight_shadow_mask{};
RenderGraphTextureRef sunlight_shadow_cascade_debug{};
RenderGraphTextureRef directional_light_shadow_transient_atlas{};
RenderGraphTextureRef directional_light_shadow_transient_mask{};
```

- [ ] **Step 4: Build sunlight shadows before deferred base**

In `SceneRenderer::render_visible_frame()`, replace the old `m_directional_shadow_pass.add_depth_passes()` block with:

```cpp
SunLightShadowPassOutputs sunlight_shadow_outputs{};
if (directional_shadow_config.enabled)
{
	sunlight_shadow_outputs = m_sunlight_shadow_pass.add_depth_passes(
		graph,
		frame,
		view_context,
		directional_shadow_config,
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
}
```

Register debug resources with sunlight names:

```cpp
graph_resources.sunlight_shadow_dynamic_atlas = sunlight_shadow_outputs.dynamic_atlas;
graph_resources.sunlight_shadow_static_cache = sunlight_shadow_outputs.static_cache_atlas;
graph_resources.sunlight_shadow_mask = sunlight_shadow_outputs.shadow_mask;
graph_resources.sunlight_shadow_cascade_debug = sunlight_shadow_outputs.cascade_debug;
```

- [ ] **Step 5: Add deferred base pass before the light loop**

Call:

```cpp
ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_base_pass(
	graph,
	frame,
	graph_resources,
	output,
	view_context));
```

- [ ] **Step 6: Add the orchestrated per-light loop**

Use this shape in `SceneRenderer::render_visible_frame()`:

```cpp
for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
{
	const VisibleLightData& light = frame.lights[light_index];
	if (light.type == LightType::Directional)
	{
		RenderGraphTextureRef directional_shadow_mask{};
		if (directional_shadow_config.enabled && light.casts_shadow)
		{
			if (light.sunlight)
			{
				const DirectionalShadowLightPlan* shadow_plan =
					find_shadow_plan_for_frame_light(sunlight_shadow_outputs, light_index);
				if (shadow_plan && shadow_plan->shadowed && sunlight_shadow_outputs.has_shadowed_lights())
				{
					ASH_PROCESS_ERROR(m_sunlight_shadow_pass.add_shadow_mask_pass(
						graph,
						sunlight_shadow_outputs,
						shadow_plan->light_plan_index,
						frame,
						graph_resources,
						view_context));
					directional_shadow_mask = sunlight_shadow_outputs.shadow_mask;
				}
			}
			else
			{
				DirectionalLightShadowPassOutputs ordinary_outputs =
					m_directional_light_shadow_pass.add_shadow_passes(
						graph,
						frame,
						light_index,
						view_context,
						directional_shadow_config,
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
				ASH_PROCESS_ERROR(ordinary_outputs.has_shadow());
				ASH_PROCESS_ERROR(m_directional_light_shadow_pass.add_shadow_mask_pass(
					graph,
					ordinary_outputs,
					frame,
					graph_resources,
					view_context));
				graph_resources.directional_light_shadow_transient_atlas = ordinary_outputs.dynamic_atlas;
				graph_resources.directional_light_shadow_transient_mask = ordinary_outputs.shadow_mask;
				directional_shadow_mask = ordinary_outputs.shadow_mask;
			}
		}
		ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_directional_light_pass(
			graph,
			frame,
			graph_resources,
			output,
			view_context,
			light_index,
			directional_shadow_mask));
	}
	else if (light.type == LightType::Point)
	{
		ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_point_light_pass(
			graph,
			frame,
			graph_resources,
			output,
			view_context,
			light_index));
	}
	else if (light.type == LightType::Spot)
	{
		ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_spot_light_pass(
			graph,
			frame,
			graph_resources,
			output,
			view_context,
			light_index));
	}
}
```

This loop deliberately treats an ordinary shadowed directional light that fails to produce a mask as a graph build failure.

- [ ] **Step 7: Keep environment, composite, sky, tone-map order**

After the per-light loop, keep this order:

```cpp
ASH_PROCESS_ERROR(m_environment_lighting_pass.add_pass(graph, frame, graph_resources, view_context));
ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_composite_pass(graph, frame, graph_resources, output, view_context));
ASH_PROCESS_ERROR(m_sky_background_pass.add_pass(graph, frame, graph_resources.depth, graph_resources.scene_hdr_linear, view_context));
```

- [ ] **Step 8: Register split debug views**

Register sunlight debug views:

```cpp
"SunLightShadowDynamicAtlas"
"SunLightShadowStaticCache"
"SceneSunLightShadowMask"
"SceneSunLightShadowCascadeIndex"
```

Register ordinary directional last-frame debug views when available:

```cpp
"DirectionalLightShadowTransientAtlas"
"DirectionalLightShadowTransientMask"
```

- [ ] **Step 9: Run ordering and render self-tests**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-tests pass, including the updated per-light mask lifetime test.

- [ ] **Step 10: Commit renderer orchestration**

```powershell
git add project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/SceneDeferredGraphResources.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Orchestrate sunlight and ordinary directional shadows"
```

---

### Task 7: Documentation And Scene Asset Migration

**Files:**
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `README.md`
- Modify: `product/assets/scenes/Sandbox.scene.json`

- [ ] **Step 1: Update `docs/EngineDeveloperGuide.md` directional shadow docs**

Replace the paragraph that says directional light shadow work is constrained by atlas/cache budget with:

```markdown
`directional_shadows.enabled=false` 时跳过 sunlight 与普通 directional light 的 shadow depth / mask pass，directional light 走 unshadowed 路径。方向光数量在 scene 数据层不设硬上限，但 `LightComponent.sunlight=true` 的 directional light 全场景最多只能有一个。Sunlight 使用 `SunLightShadowPass` 的大场景 CSM、static cache、dynamic overlay、screen-space shadow mask 和 cascade debug view；普通 `LightType::Directional && !sunlight` 使用 `DirectionalLightShadowPass`，在每个普通方向光自己的 deferred lighting pass 之前临时 clear atlas、绘制所有 shadow caster、生成该光的 screen-space mask，并在下一个普通方向光处理时复用同类 transient graph 资源生命周期。普通方向光不使用 static cache，不区分 static/dynamic caster，也不因为共享 directional shadow atlas budget 跳过光照。
```

Add this line near the scene component documentation:

```markdown
- `LightComponent.sunlight` 只对 directional light 有意义；Scene load/save 会保存该字段，非 directional light 会被清洗为 `false`，并且 load 完成后拒绝超过一个 directional sunlight。
```

- [ ] **Step 2: Update root README summary**

Update the top shadow summary to state:

```markdown
- Shadow 当前区分 sunlight 与普通 directional light：全场景最多一个 `sunlight=true` 的 directional light 使用大场景 CSM + static cache + dynamic overlay；普通 directional light 使用逐光 transient cascade shadow，每个普通方向光在自己的 deferred lighting pass 前绘制 shadow，不再受共享 directionallight shadow budget 降级为 unshadowed。Point/spot 阴影与 VSM 仍待后续阶段。
```

Add document links:

```markdown
- SunLight / DirectionalLight Shadow Pass 拆分设计：[`docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md`](docs/superpowers/specs/2026-05-26-sunlight-directional-shadow-pass-split-design.md)
- SunLight / DirectionalLight Shadow Pass 拆分实现计划：[`docs/superpowers/plans/2026-05-26-sunlight-directional-shadow-pass-split-implementation.md`](docs/superpowers/plans/2026-05-26-sunlight-directional-shadow-pass-split-implementation.md)
```

- [ ] **Step 3: Make Sandbox scene explicit**

In `product/assets/scenes/Sandbox.scene.json`, set the hand-authored directional light:

```json
"sunlight": false
```

Do not add a hand-authored sunlight when the scene already has active IBL metadata that creates `EnvironmentSunLight`.

- [ ] **Step 4: Run JSON and docs checks**

Run:

```powershell
rg -n "\"sunlight\"|SunLightShadowPass|DirectionalLightShadowPass|shadow budget|directional shadow atlas budget" README.md docs/EngineDeveloperGuide.md product/assets/scenes/Sandbox.scene.json
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: `rg` shows the new split wording, and self-tests pass.

- [ ] **Step 5: Commit docs and scene migration**

```powershell
git add docs/EngineDeveloperGuide.md README.md product/assets/scenes/Sandbox.scene.json
git commit -m "Document sunlight and ordinary directional shadow split"
```

---

### Task 8: Final Build And Backend Validation

**Files:**
- No source edits.
- Generated logs go under `Intermediate/logs`.
- RenderDoc captures, if taken, go under `Intermediate/renderdoc_captures`.

- [ ] **Step 1: Regenerate Visual Studio project files**

Run:

```powershell
.\generate_vs2022.bat
```

Expected: exits 0 and prints `[Premake] Done.`

- [ ] **Step 2: Build Sandbox and Editor**

Run:

```powershell
.\build_sandbox.bat Debug x64
.\build_editor.bat Debug x64
```

Expected: both scripts print `build succeeded`.

- [ ] **Step 3: Run headless engine self-tests**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exits 0 and reports all engine self-tests passed.

- [ ] **Step 4: Smoke Sandbox on Vulkan**

Set `product/config/Engine.ini`:

```ini
[RHI]
Backend=Vulkan
```

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
```

Expected: exits 0 by graceful smoke-test shutdown. Logs under `product/logs/` contain Vulkan backend startup and no validation errors.

- [ ] **Step 5: Smoke Sandbox on DX12**

Set `product/config/Engine.ini`:

```ini
[RHI]
Backend=DX12
```

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
```

Expected: exits 0 by graceful smoke-test shutdown. Logs under `product/logs/` contain DX12 backend startup and no debug-layer errors.

- [ ] **Step 6: Smoke Editor on Vulkan**

Set `product/config/Engine.ini`:

```ini
[RHI]
Backend=Vulkan
```

Run:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected: exits 0 by graceful smoke-test shutdown. Logs contain Vulkan backend startup and no validation errors.

- [ ] **Step 7: Smoke Editor on DX12**

Set `product/config/Engine.ini`:

```ini
[RHI]
Backend=DX12
```

Run:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected: exits 0 by graceful smoke-test shutdown. Logs contain DX12 backend startup and no debug-layer errors.

- [ ] **Step 8: Restore the preferred local backend**

Set `product/config/Engine.ini` back to the user's preferred value. If the starting value was Vulkan, restore:

```ini
[RHI]
Backend=Vulkan
```

- [ ] **Step 9: Commit validation-only config restore if needed**

If validation changed only `product/config/Engine.ini` and the final content matches the pre-validation content, do not commit it. If the task intentionally changes runtime config, commit it with the source changes that require it.

---

## Self-Review Checklist

- Spec coverage:
  - `LightComponent::sunlight`: Task 1.
  - At most one directional sunlight: Task 1.
  - EnvironmentSunLight uses sunlight: Task 1.
  - `VisibleLightData::sunlight`: Task 2.
  - `SunLightShadowPass` owns high-quality static-cache path: Task 3.
  - `DirectionalLightShadowPass` owns ordinary per-light transient path: Task 4.
  - Ordinary directional lights have no shared shadow light-count budget: Task 4.
  - Ordinary directional pass draws all casters every frame: Task 4.
  - Per-light shadow before per-light deferred lighting: Tasks 5 and 6.
  - Point and spot lighting preserved: Tasks 5 and 6.
  - Docs and README updated: Task 7.
  - Vulkan + DX12 Sandbox and Editor validation: Task 8.
- Storage buffer overwrite risk:
  - Task 4 creates a distinct storage buffer snapshot per ordinary directional light.
- Engine / Editor boundary:
  - No files under `project/src/editor` are changed.
- Generated artifacts:
  - Logs and captures are directed to `Intermediate` or existing `product/logs`.
