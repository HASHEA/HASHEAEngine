# Sandbox SceneConfig Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Sandbox load `product/assets/scenes/Sandbox.scene.json` as its single standard scene, and make `scene_config` carry per-scene `AmbientOcclusion`, `DirectionalShadows`, and `RenderDebugView` settings.

**Architecture:** `Scene` owns a `SceneRenderConfig`, serializes it as top-level `scene_config`, and versions it separately from primitive, transform, light, and environment changes. `ScenePresentationSubsystem` snapshots that config through `RenderScene` into `VisibleRenderFrame`, and `SceneRenderer` passes the frame config to AO, directional shadows, and render debug view. `Engine.ini` remains the process and machine-level config source for RHI backend, validation, VSync, and environment lighting bake/cache policy.

**Tech Stack:** C++17, nlohmann JSON (`json.hpp`), EnTT scene storage, AshEngine Function/Scene and Function/Render APIs, Premake/MSBuild, `Sandbox.exe --engine-self-test`, Vulkan and DX12 smoke validation.

---

## Guardrails

- Do not modify `project/src/editor`.
- Keep all Engine-facing API changes under `project/src/engine/Function`.
- The worktree is dirty; stage only files listed in the active task.
- Keep generated logs under `Intermediate/logs`, test reports under `Intermediate/test-reports`, and temporary files under `Intermediate/test-temp`.
- Update root `README.md` and `docs/EngineDeveloperGuide.md` before final validation because this changes scene/config/runtime behavior.
- Keep `[RHI]`, `[Rendering]`, validation, and `[EnvironmentLighting]` in `product/config/Engine.ini`.
- Remove `AmbientOcclusion`, `DirectionalShadows`, and `RenderDebugView` as authoritative scene defaults from `product/config/Engine.ini`; the runtime fallback loaders stay available for tests and non-scene paths.

## File Map

- Create `project/src/engine/Function/Render/RenderDebugViewConfig.h`: small config-only header for `RenderDebugViewConfig` and runtime config functions.
- Modify `project/src/engine/Function/Render/RenderDebugView.h/.cpp`: include the new config header, remove config declarations from the heavy render-debug-view header, and accept frame config during pass insertion.
- Modify `project/src/engine/Function/Render/AmbientOcclusionConfig.h/.cpp`: expose token parse helpers and a shared sanitize helper for JSON and INI paths.
- Modify `project/src/engine/Function/Render/DirectionalShadowConfig.h/.cpp`: expose a shared sanitize helper for JSON and INI paths.
- Create `project/src/engine/Function/Scene/SceneConfig.h/.cpp`: define `SceneRenderConfig`, default construction, and equality.
- Modify `project/src/engine/Function/Scene/Scene.h/.cpp`: own the scene render config, add API/versioning, bump scene file version to 4, and serialize/deserialize `scene_config`.
- Modify `project/src/engine/Function/Render/RenderScene.h/.cpp`: store scene render config and copy it into `VisibleRenderFrame`.
- Modify `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`: watch `Scene::get_render_config_version()` and refresh render config without rebuilding primitives.
- Modify `project/src/engine/Function/Render/AmbientOcclusionPass.h/.cpp`: accept `const AmbientOcclusionConfig&` from the frame.
- Modify `project/src/engine/Function/Render/DirectionalShadowPass.h/.cpp`: accept `const DirectionalShadowConfig&` from the frame and reset size-dependent cache resources when needed.
- Modify `project/src/engine/Function/Render/SceneRenderer.cpp`: use `frame.render_config` instead of process-global AO/shadow/debug-view config for scene rendering.
- Modify `project/src/sandbox/App/SandboxStandardScene.h/.cpp`: load `Sandbox.scene.json` and bind the free camera to the loaded primary camera.
- Modify `project/src/sandbox/App/SandboxApplication.h/.cpp`: remove the glTF model selector default path and start the standard scene file directly.
- Create `product/assets/scenes/Sandbox.scene.json`: standard Sandbox scene asset containing Sponza, camera, lights, environment, and `scene_config`.
- Modify `product/config/Engine.ini`: keep process-level config, remove per-scene visual default sections.
- Modify `project/src/engine/Base/EngineSelfTests.cpp`: add headless tests for scene config defaults, parsing, round-trip, versioning, and render-frame snapshot.
- Modify `README.md`, `docs/EngineDeveloperGuide.md`, and `docs/ScenePresentationSubsystemGuide.md`: document the new scene/config data flow.

---

### Task 0: Pre-Flight Safety Check

**Files:**
- Inspect only: repository state and nested agent instructions

- [ ] **Step 1: Check local instructions**

Run:

```powershell
rg --files -g AGENTS.md -g AGENTS.override.md
```

Expected: no deeper instruction file under the files listed in this plan that changes the Engine/Editor boundary.

- [ ] **Step 2: Check worktree dirtiness**

Run:

```powershell
git status --short
```

Expected: the worktree is dirty. Treat pre-existing modified and untracked files as user work. Do not restore or delete them.

- [ ] **Step 3: Confirm the current committed design spec**

Run:

```powershell
git show --stat --oneline 23db3a3
```

Expected: commit `23db3a3 Document sandbox scene config design` exists and contains `docs/superpowers/specs/2026-05-25-sandbox-scene-config-design.md`.

---

### Task 1: Config Header Boundaries And Shared Sanitizers

**Files:**
- Create: `project/src/engine/Function/Render/RenderDebugViewConfig.h`
- Modify: `project/src/engine/Function/Render/RenderDebugView.h`
- Modify: `project/src/engine/Function/Render/RenderDebugView.cpp`
- Modify: `project/src/engine/Function/Render/AmbientOcclusionConfig.h`
- Modify: `project/src/engine/Function/Render/AmbientOcclusionConfig.cpp`
- Modify: `project/src/engine/Function/Render/DirectionalShadowConfig.h`
- Modify: `project/src/engine/Function/Render/DirectionalShadowConfig.cpp`

- [ ] **Step 1: Create the render debug config header**

Create `project/src/engine/Function/Render/RenderDebugViewConfig.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include <string>

namespace AshEngine
{
	struct ASH_API RenderDebugViewConfig
	{
		bool enabled = false;
		std::string selected = "Off";
	};

	ASH_API RenderDebugViewConfig make_default_render_debug_view_config();
	ASH_API RenderDebugViewConfig load_runtime_render_debug_view_config(const char* config_path);
	ASH_API void set_runtime_render_debug_view_config(const RenderDebugViewConfig& config);
	ASH_API RenderDebugViewConfig get_runtime_render_debug_view_config();
}
```

- [ ] **Step 2: Verify the config header stays lightweight**

Confirm `RenderDebugViewConfig.h` includes only `Base/hcore.h` and standard library headers. It must not include `RenderDevice.h`, `RenderGraphFwd.h`, or `UIContext.h`.

Expected: `SceneConfig.h` can include `RenderDebugViewConfig.h` without pulling render graph or UI declarations into `Scene.h`.

- [ ] **Step 3: Include the small config header from RenderDebugView**

Modify `project/src/engine/Function/Render/RenderDebugView.h`:

```cpp
#include "Function/Render/RenderDebugViewConfig.h"
```

Then remove the in-header `RenderDebugViewConfig` struct and these function declarations from `RenderDebugView.h` because they now live in `RenderDebugViewConfig.h`:

```cpp
ASH_API RenderDebugViewConfig make_default_render_debug_view_config();
ASH_API RenderDebugViewConfig load_runtime_render_debug_view_config(const char* config_path);
ASH_API void set_runtime_render_debug_view_config(const RenderDebugViewConfig& config);
ASH_API RenderDebugViewConfig get_runtime_render_debug_view_config();
```

- [ ] **Step 4: Keep RenderDebugView.cpp compiling**

Modify `project/src/engine/Function/Render/RenderDebugView.cpp` so it still sees the config declarations through `RenderDebugView.h`. No new include is required if `RenderDebugView.h` includes `RenderDebugViewConfig.h`.

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build passes. If it fails because another file used `RenderDebugViewConfig` without `RenderDebugView.h`, include `Function/Render/RenderDebugViewConfig.h` in that file.

- [ ] **Step 5: Expose AO parse helpers and sanitizer**

Modify `project/src/engine/Function/Render/AmbientOcclusionConfig.h`:

```cpp
#include <string_view>

ASH_API bool try_parse_ambient_occlusion_mode(std::string_view value, AmbientOcclusionMode& out_mode);
ASH_API bool try_parse_ambient_occlusion_quality(std::string_view value, AmbientOcclusionQuality& out_quality);
ASH_API AmbientOcclusionConfig sanitize_ambient_occlusion_config(
	const AmbientOcclusionConfig& config,
	const AmbientOcclusionConfig& fallback);
```

Modify `project/src/engine/Function/Render/AmbientOcclusionConfig.cpp` by replacing internal calls to `parse_ao_mode` and `parse_ao_quality` with the exported helpers. Keep `parse_ao_debug_view` internal because `scene_config` does not carry AO debug view.

Add this sanitizer implementation:

```cpp
AmbientOcclusionConfig sanitize_ambient_occlusion_config(
	const AmbientOcclusionConfig& config,
	const AmbientOcclusionConfig& fallback)
{
	AmbientOcclusionConfig result = config;
	result.radius = clamp_range(result.radius, fallback.radius, 0.05f, 20.0f);
	result.intensity = clamp_range(result.intensity, fallback.intensity, 0.0f, 8.0f);
	result.power = clamp_range(result.power, fallback.power, 0.05f, 8.0f);
	result.temporal_blend = clamp_range(result.temporal_blend, fallback.temporal_blend, 0.0f, 0.98f);
	result.temporal_depth_threshold = clamp_range(result.temporal_depth_threshold, fallback.temporal_depth_threshold, 0.000001f, 0.25f);
	result.temporal_normal_threshold = clamp_range(result.temporal_normal_threshold, fallback.temporal_normal_threshold, 0.0f, 1.0f);
	return result;
}
```

At the end of `load_runtime_ambient_occlusion_config`, before the info log, add:

```cpp
config = sanitize_ambient_occlusion_config(config, make_default_ambient_occlusion_config());
```

- [ ] **Step 6: Expose the directional shadow sanitizer**

Modify `project/src/engine/Function/Render/DirectionalShadowConfig.h`:

```cpp
ASH_API DirectionalShadowConfig sanitize_directional_shadow_config(
	const DirectionalShadowConfig& config,
	const DirectionalShadowConfig& fallback);
```

Modify `project/src/engine/Function/Render/DirectionalShadowConfig.cpp`:

```cpp
DirectionalShadowConfig sanitize_directional_shadow_config(
	const DirectionalShadowConfig& config,
	const DirectionalShadowConfig& fallback)
{
	DirectionalShadowConfig result = config;
	result.default_cascade_count = std::clamp(result.default_cascade_count, 1u, 4u);
	result.default_shadow_distance = clamp_positive(result.default_shadow_distance, fallback.default_shadow_distance, 1.0f, 10000.0f);
	result.near_shadow_distance = clamp_positive(result.near_shadow_distance, fallback.near_shadow_distance, 0.25f, result.default_shadow_distance);
	result.split_lambda = std::clamp(result.split_lambda, 0.0f, 1.0f);
	result.near_cascade_resolution = clamp_power_of_two(result.near_cascade_resolution, 512u, 4096u, fallback.near_cascade_resolution);
	result.outer_cascade_resolution = clamp_power_of_two(result.outer_cascade_resolution, 256u, 4096u, fallback.outer_cascade_resolution);
	result.dynamic_atlas_size = clamp_power_of_two(result.dynamic_atlas_size, 1024u, 8192u, fallback.dynamic_atlas_size);
	result.static_cache_atlas_size = clamp_power_of_two(result.static_cache_atlas_size, 1024u, 8192u, fallback.static_cache_atlas_size);
	result.static_cache_budget_mb = std::clamp(result.static_cache_budget_mb, 16u, 512u);
	result.depth_bias = std::clamp(result.depth_bias, 0.0f, 0.05f);
	result.normal_bias = std::clamp(result.normal_bias, 0.0f, 1.0f);
	result.pcf_radius = std::clamp(result.pcf_radius, 0u, 3u);
	return result;
}
```

At the end of `load_runtime_directional_shadow_config`, before the info log, add:

```cpp
config = sanitize_directional_shadow_config(config, make_default_directional_shadow_config());
```

- [ ] **Step 7: Build and commit the header-boundary change**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build passes.

Commit only this task:

```powershell
git add project/src/engine/Function/Render/RenderDebugViewConfig.h project/src/engine/Function/Render/RenderDebugView.h project/src/engine/Function/Render/RenderDebugView.cpp project/src/engine/Function/Render/AmbientOcclusionConfig.h project/src/engine/Function/Render/AmbientOcclusionConfig.cpp project/src/engine/Function/Render/DirectionalShadowConfig.h project/src/engine/Function/Render/DirectionalShadowConfig.cpp
git commit -m "Refactor render config headers for scene config"
```

---

### Task 2: SceneRenderConfig API And Versioning

**Files:**
- Create: `project/src/engine/Function/Scene/SceneConfig.h`
- Create: `project/src/engine/Function/Scene/SceneConfig.cpp`
- Modify: `project/src/engine/Function/Scene/Scene.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add the failing self-test**

In `project/src/engine/Base/EngineSelfTests.cpp`, near the existing scene render version tests, add:

```cpp
auto test_scene_render_config_version_isolated_from_other_render_versions() -> bool
{
	Scene scene = Scene::create("SceneRenderConfigVersionSelfTest");
	const uint64_t primitive_before = scene.get_render_primitive_version();
	const uint64_t transform_before = scene.get_render_transform_version();
	const uint64_t light_before = scene.get_render_light_version();
	const uint64_t environment_before = scene.get_render_environment_version();
	const uint64_t render_config_before = scene.get_render_config_version();

	SceneRenderConfig config = scene.get_render_config();
	config.ambient_occlusion.mode = AmbientOcclusionMode::HBAO;
	config.ambient_occlusion.quality = AmbientOcclusionQuality::High;
	config.render_debug_view.enabled = true;
	config.render_debug_view.selected = "SceneDeferredSceneHDRLinear";

	if (!scene.set_render_config(config))
	{
		return report_self_test_failure("Scene render config versions", "set_render_config returned false for a valid scene");
	}

	const bool ok =
		scene.get_render_primitive_version() == primitive_before &&
		scene.get_render_transform_version() == transform_before &&
		scene.get_render_light_version() == light_before &&
		scene.get_render_environment_version() == environment_before &&
		scene.get_render_config_version() != render_config_before;
	return ok ||
		report_self_test_failure("Scene render config versions", "render config changes invalidated unrelated render versions");
}
```

Add it to `run_engine_base_self_tests()` immediately after `test_scene_environment_version_isolated_from_primitives()`:

```cpp
all_passed = test_scene_render_config_version_isolated_from_other_render_versions() && all_passed;
```

- [ ] **Step 2: Run the failing test build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: compile fails because `SceneRenderConfig`, `Scene::get_render_config`, `Scene::set_render_config`, and `Scene::get_render_config_version` do not exist.

- [ ] **Step 3: Create SceneConfig.h**

Create `project/src/engine/Function/Scene/SceneConfig.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/AmbientOcclusionConfig.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/RenderDebugViewConfig.h"

namespace AshEngine
{
	struct ASH_API SceneRenderConfig
	{
		AmbientOcclusionConfig ambient_occlusion{};
		DirectionalShadowConfig directional_shadows{};
		RenderDebugViewConfig render_debug_view{};
	};

	ASH_API SceneRenderConfig make_default_scene_render_config();
	ASH_API bool scene_render_config_equal(const SceneRenderConfig& lhs, const SceneRenderConfig& rhs);
}
```

- [ ] **Step 4: Create SceneConfig.cpp**

Create `project/src/engine/Function/Scene/SceneConfig.cpp`:

```cpp
#include "Function/Scene/SceneConfig.h"

namespace AshEngine
{
	namespace
	{
		auto ambient_occlusion_config_equal(const AmbientOcclusionConfig& lhs, const AmbientOcclusionConfig& rhs) -> bool
		{
			return lhs.mode == rhs.mode &&
				lhs.quality == rhs.quality &&
				lhs.radius == rhs.radius &&
				lhs.intensity == rhs.intensity &&
				lhs.power == rhs.power &&
				lhs.half_resolution == rhs.half_resolution &&
				lhs.blur == rhs.blur &&
				lhs.temporal == rhs.temporal &&
				lhs.temporal_blend == rhs.temporal_blend &&
				lhs.temporal_depth_threshold == rhs.temporal_depth_threshold &&
				lhs.temporal_normal_threshold == rhs.temporal_normal_threshold &&
				lhs.debug_view == rhs.debug_view;
		}

		auto directional_shadow_config_equal(const DirectionalShadowConfig& lhs, const DirectionalShadowConfig& rhs) -> bool
		{
			return lhs.enabled == rhs.enabled &&
				lhs.default_cascade_count == rhs.default_cascade_count &&
				lhs.default_shadow_distance == rhs.default_shadow_distance &&
				lhs.near_shadow_distance == rhs.near_shadow_distance &&
				lhs.split_lambda == rhs.split_lambda &&
				lhs.near_cascade_resolution == rhs.near_cascade_resolution &&
				lhs.outer_cascade_resolution == rhs.outer_cascade_resolution &&
				lhs.dynamic_atlas_size == rhs.dynamic_atlas_size &&
				lhs.static_cache_atlas_size == rhs.static_cache_atlas_size &&
				lhs.static_cache_budget_mb == rhs.static_cache_budget_mb &&
				lhs.depth_bias == rhs.depth_bias &&
				lhs.normal_bias == rhs.normal_bias &&
				lhs.pcf_radius == rhs.pcf_radius;
		}
	}

	SceneRenderConfig make_default_scene_render_config()
	{
		SceneRenderConfig config{};
		config.ambient_occlusion = make_default_ambient_occlusion_config();
		config.directional_shadows = make_default_directional_shadow_config();
		config.render_debug_view = make_default_render_debug_view_config();
		return config;
	}

	bool scene_render_config_equal(const SceneRenderConfig& lhs, const SceneRenderConfig& rhs)
	{
		return ambient_occlusion_config_equal(lhs.ambient_occlusion, rhs.ambient_occlusion) &&
			directional_shadow_config_equal(lhs.directional_shadows, rhs.directional_shadows) &&
			lhs.render_debug_view.enabled == rhs.render_debug_view.enabled &&
			lhs.render_debug_view.selected == rhs.render_debug_view.selected;
	}
}
```

- [ ] **Step 5: Add Scene API declarations**

Modify `project/src/engine/Function/Scene/Scene.h`:

```cpp
#include "Function/Scene/SceneConfig.h"
```

Add these public methods beside the existing render version getters:

```cpp
const SceneRenderConfig& get_render_config() const;
bool set_render_config(const SceneRenderConfig& config);
uint64_t get_render_config_version() const;
```

- [ ] **Step 6: Add Scene storage and initialize defaults**

Modify `SceneStorage` in `project/src/engine/Function/Scene/Scene.cpp`:

```cpp
SceneRenderConfig render_config = make_default_scene_render_config();
uint64_t render_config_version = 0;
```

In `Scene::create`, after `change_version` is allocated, set:

```cpp
impl->storage.render_config = make_default_scene_render_config();
impl->storage.render_config_version = impl->storage.change_version;
```

- [ ] **Step 7: Implement Scene render config API**

Add implementations near the existing render version getters:

```cpp
const SceneRenderConfig& Scene::get_render_config() const
{
	static const SceneRenderConfig k_default_config = make_default_scene_render_config();
	return m_impl ? m_impl->storage.render_config : k_default_config;
}

bool Scene::set_render_config(const SceneRenderConfig& config)
{
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
	ASH_PROCESS_ERROR(m_impl != nullptr);

	const SceneRenderConfig sanitized_config{
		sanitize_ambient_occlusion_config(config.ambient_occlusion, make_default_ambient_occlusion_config()),
		sanitize_directional_shadow_config(config.directional_shadows, make_default_directional_shadow_config()),
		config.render_debug_view
	};

	if (!scene_render_config_equal(m_impl->storage.render_config, sanitized_config))
	{
		m_impl->storage.render_config = sanitized_config;
		m_impl->storage.dirty = true;
		m_impl->storage.change_version = allocate_scene_change_version();
		m_impl->storage.render_config_version = m_impl->storage.change_version;
	}

	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}

uint64_t Scene::get_render_config_version() const
{
	return m_impl ? m_impl->storage.render_config_version : 0;
}
```

If aggregate initialization fails because of compiler rules, replace `const SceneRenderConfig sanitized_config{ ... }` with assignment into a local `SceneRenderConfig sanitized_config{};`.

- [ ] **Step 8: Run self-tests**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build passes and self-tests pass.

- [ ] **Step 9: Commit the Scene API change**

```powershell
git add project/src/engine/Function/Scene/SceneConfig.h project/src/engine/Function/Scene/SceneConfig.cpp project/src/engine/Function/Scene/Scene.h project/src/engine/Function/Scene/Scene.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add scene render config ownership"
```

---

### Task 3: SceneConfig JSON Serialization

**Files:**
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add failing load/default/round-trip self-test**

Add this test near the scene render config version self-test:

```cpp
auto test_scene_render_config_json_defaults_and_round_trip() -> bool
{
	const std::filesystem::path test_dir = engine_self_test_dir() / "scene_render_config";
	std::filesystem::create_directories(test_dir);

	const std::filesystem::path old_scene_path = test_dir / "old_scene.scene.json";
	{
		std::ofstream file(old_scene_path, std::ios::trunc);
		file <<
			"{\n"
			"  \"version\": 3,\n"
			"  \"name\": \"OldScene\",\n"
			"  \"next_entity_id\": 1,\n"
			"  \"entities\": []\n"
			"}\n";
	}

	std::string error{};
	Scene old_scene = Scene::load_from_file(old_scene_path, &error);
	const SceneRenderConfig defaults = make_default_scene_render_config();
	if (!old_scene.is_valid() || !scene_render_config_equal(old_scene.get_render_config(), defaults))
	{
		return report_self_test_failure("Scene render config JSON", "version 3 scene without scene_config did not load defaults");
	}

	const std::filesystem::path scene_path = test_dir / "configured_scene.scene.json";
	{
		std::ofstream file(scene_path, std::ios::trunc);
		file <<
			"{\n"
			"  \"version\": 4,\n"
			"  \"name\": \"ConfiguredScene\",\n"
			"  \"next_entity_id\": 1,\n"
			"  \"scene_config\": {\n"
			"    \"ambient_occlusion\": {\n"
			"      \"mode\": \"HBAO\",\n"
			"      \"quality\": \"High\",\n"
			"      \"radius\": 99.0,\n"
			"      \"intensity\": 2.5,\n"
			"      \"power\": 2.0,\n"
			"      \"half_resolution\": true,\n"
			"      \"blur\": false,\n"
			"      \"temporal\": true,\n"
			"      \"temporal_blend\": 0.9,\n"
			"      \"temporal_depth_threshold\": 0.02,\n"
			"      \"temporal_normal_threshold\": 0.5\n"
			"    },\n"
			"    \"directional_shadows\": {\n"
			"      \"enabled\": true,\n"
			"      \"default_cascade_count\": 7,\n"
			"      \"default_shadow_distance\": 240.0,\n"
			"      \"near_shadow_distance\": 24.0,\n"
			"      \"split_lambda\": 0.8,\n"
			"      \"near_cascade_resolution\": 3000,\n"
			"      \"outer_cascade_resolution\": 300,\n"
			"      \"dynamic_atlas_size\": 3000,\n"
			"      \"static_cache_atlas_size\": 3000,\n"
			"      \"static_cache_budget_mb\": 96,\n"
			"      \"depth_bias\": 0.002,\n"
			"      \"normal_bias\": 0.06,\n"
			"      \"pcf_radius\": 2\n"
			"    },\n"
			"    \"render_debug_view\": {\n"
			"      \"enabled\": true,\n"
			"      \"selected\": \"SceneDeferredSceneHDRLinear\"\n"
			"    }\n"
			"  },\n"
			"  \"entities\": []\n"
			"}\n";
	}

	Scene configured_scene = Scene::load_from_file(scene_path, &error);
	if (!configured_scene.is_valid())
	{
		return report_self_test_failure("Scene render config JSON", error.empty() ? "configured scene did not load" : error.c_str());
	}

	const SceneRenderConfig loaded = configured_scene.get_render_config();
	const bool parsed_ok =
		loaded.ambient_occlusion.mode == AmbientOcclusionMode::HBAO &&
		loaded.ambient_occlusion.quality == AmbientOcclusionQuality::High &&
		loaded.ambient_occlusion.radius == 20.0f &&
		loaded.ambient_occlusion.half_resolution &&
		!loaded.ambient_occlusion.blur &&
		loaded.directional_shadows.default_cascade_count == 4u &&
		loaded.directional_shadows.near_cascade_resolution == 4096u &&
		loaded.directional_shadows.outer_cascade_resolution == 512u &&
		loaded.directional_shadows.dynamic_atlas_size == 4096u &&
		loaded.directional_shadows.static_cache_atlas_size == 4096u &&
		loaded.render_debug_view.enabled &&
		loaded.render_debug_view.selected == "SceneDeferredSceneHDRLinear";
	if (!parsed_ok)
	{
		return report_self_test_failure("Scene render config JSON", "scene_config fields were not parsed and sanitized as expected");
	}

	const std::filesystem::path saved_path = test_dir / "saved_scene.scene.json";
	if (!configured_scene.save_to_file(saved_path, &error))
	{
		return report_self_test_failure("Scene render config JSON", error.empty() ? "failed to save configured scene" : error.c_str());
	}

	Scene round_trip_scene = Scene::load_from_file(saved_path, &error);
	if (!round_trip_scene.is_valid() || !scene_render_config_equal(round_trip_scene.get_render_config(), loaded))
	{
		return report_self_test_failure("Scene render config JSON", "scene_config did not survive save/load round trip");
	}

	return true;
}
```

Add it to `run_engine_base_self_tests()` after `test_scene_render_config_version_isolated_from_other_render_versions()`:

```cpp
all_passed = test_scene_render_config_json_defaults_and_round_trip() && all_passed;
```

- [ ] **Step 2: Run the failing test build**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test fails because `scene_config` is not read or written.

- [ ] **Step 3: Bump the scene file version**

Modify `project/src/engine/Function/Scene/Scene.cpp`:

```cpp
static constexpr uint32_t k_scene_file_version = 4;
```

Expected: version 3 files remain loadable because the loader only rejects versions greater than `k_scene_file_version`.

- [ ] **Step 4: Add JSON parse helpers in Scene.cpp**

Add helpers in the anonymous namespace of `Scene.cpp`. Keep warnings non-fatal for individual invalid fields:

```cpp
template <typename TValue>
auto try_get_json_value(const json& object, const char* key, TValue& out_value) -> bool
{
	if (!object.is_object())
	{
		return false;
	}
	const auto it = object.find(key);
	if (it == object.end() || it->is_null())
	{
		return false;
	}
	try
	{
		out_value = it->get<TValue>();
		return true;
	}
	catch (const std::exception& exception)
	{
		HLogWarning("SceneConfig field '{}' has invalid type: {}.", key, exception.what());
		return false;
	}
}

auto deserialize_scene_render_config(const json& root) -> SceneRenderConfig
{
	SceneRenderConfig config = make_default_scene_render_config();
	const auto scene_config_it = root.find("scene_config");
	if (scene_config_it == root.end() || !scene_config_it->is_object())
	{
		return config;
	}

	const json& scene_config = *scene_config_it;
	if (const auto ao_it = scene_config.find("ambient_occlusion"); ao_it != scene_config.end() && ao_it->is_object())
	{
		std::string mode{};
		if (try_get_json_value(*ao_it, "mode", mode))
		{
			AmbientOcclusionMode parsed = config.ambient_occlusion.mode;
			if (try_parse_ambient_occlusion_mode(mode, parsed))
			{
				config.ambient_occlusion.mode = parsed;
			}
			else
			{
				HLogWarning("SceneConfig ambient_occlusion.mode '{}' is invalid. Keeping default '{}'.", mode, ambient_occlusion_mode_name(config.ambient_occlusion.mode));
			}
		}

		std::string quality{};
		if (try_get_json_value(*ao_it, "quality", quality))
		{
			AmbientOcclusionQuality parsed = config.ambient_occlusion.quality;
			if (try_parse_ambient_occlusion_quality(quality, parsed))
			{
				config.ambient_occlusion.quality = parsed;
			}
			else
			{
				HLogWarning("SceneConfig ambient_occlusion.quality '{}' is invalid. Keeping default '{}'.", quality, ambient_occlusion_quality_name(config.ambient_occlusion.quality));
			}
		}

		try_get_json_value(*ao_it, "radius", config.ambient_occlusion.radius);
		try_get_json_value(*ao_it, "intensity", config.ambient_occlusion.intensity);
		try_get_json_value(*ao_it, "power", config.ambient_occlusion.power);
		try_get_json_value(*ao_it, "half_resolution", config.ambient_occlusion.half_resolution);
		try_get_json_value(*ao_it, "blur", config.ambient_occlusion.blur);
		try_get_json_value(*ao_it, "temporal", config.ambient_occlusion.temporal);
		try_get_json_value(*ao_it, "temporal_blend", config.ambient_occlusion.temporal_blend);
		try_get_json_value(*ao_it, "temporal_depth_threshold", config.ambient_occlusion.temporal_depth_threshold);
		try_get_json_value(*ao_it, "temporal_normal_threshold", config.ambient_occlusion.temporal_normal_threshold);
		config.ambient_occlusion = sanitize_ambient_occlusion_config(config.ambient_occlusion, make_default_ambient_occlusion_config());
	}

	if (const auto shadows_it = scene_config.find("directional_shadows"); shadows_it != scene_config.end() && shadows_it->is_object())
	{
		try_get_json_value(*shadows_it, "enabled", config.directional_shadows.enabled);
		try_get_json_value(*shadows_it, "default_cascade_count", config.directional_shadows.default_cascade_count);
		try_get_json_value(*shadows_it, "default_shadow_distance", config.directional_shadows.default_shadow_distance);
		try_get_json_value(*shadows_it, "near_shadow_distance", config.directional_shadows.near_shadow_distance);
		try_get_json_value(*shadows_it, "split_lambda", config.directional_shadows.split_lambda);
		try_get_json_value(*shadows_it, "near_cascade_resolution", config.directional_shadows.near_cascade_resolution);
		try_get_json_value(*shadows_it, "outer_cascade_resolution", config.directional_shadows.outer_cascade_resolution);
		try_get_json_value(*shadows_it, "dynamic_atlas_size", config.directional_shadows.dynamic_atlas_size);
		try_get_json_value(*shadows_it, "static_cache_atlas_size", config.directional_shadows.static_cache_atlas_size);
		try_get_json_value(*shadows_it, "static_cache_budget_mb", config.directional_shadows.static_cache_budget_mb);
		try_get_json_value(*shadows_it, "depth_bias", config.directional_shadows.depth_bias);
		try_get_json_value(*shadows_it, "normal_bias", config.directional_shadows.normal_bias);
		try_get_json_value(*shadows_it, "pcf_radius", config.directional_shadows.pcf_radius);
		config.directional_shadows = sanitize_directional_shadow_config(config.directional_shadows, make_default_directional_shadow_config());
	}

	if (const auto debug_it = scene_config.find("render_debug_view"); debug_it != scene_config.end() && debug_it->is_object())
	{
		try_get_json_value(*debug_it, "enabled", config.render_debug_view.enabled);
		try_get_json_value(*debug_it, "selected", config.render_debug_view.selected);
		if (config.render_debug_view.selected.empty())
		{
			config.render_debug_view.selected = "Off";
		}
	}

	return config;
}
```

- [ ] **Step 5: Add JSON write helper in Scene.cpp**

Add:

```cpp
auto serialize_scene_render_config(const SceneRenderConfig& config) -> json
{
	return json{
		{ "ambient_occlusion", json{
			{ "mode", ambient_occlusion_mode_name(config.ambient_occlusion.mode) },
			{ "quality", ambient_occlusion_quality_name(config.ambient_occlusion.quality) },
			{ "radius", config.ambient_occlusion.radius },
			{ "intensity", config.ambient_occlusion.intensity },
			{ "power", config.ambient_occlusion.power },
			{ "half_resolution", config.ambient_occlusion.half_resolution },
			{ "blur", config.ambient_occlusion.blur },
			{ "temporal", config.ambient_occlusion.temporal },
			{ "temporal_blend", config.ambient_occlusion.temporal_blend },
			{ "temporal_depth_threshold", config.ambient_occlusion.temporal_depth_threshold },
			{ "temporal_normal_threshold", config.ambient_occlusion.temporal_normal_threshold }
		} },
		{ "directional_shadows", json{
			{ "enabled", config.directional_shadows.enabled },
			{ "default_cascade_count", config.directional_shadows.default_cascade_count },
			{ "default_shadow_distance", config.directional_shadows.default_shadow_distance },
			{ "near_shadow_distance", config.directional_shadows.near_shadow_distance },
			{ "split_lambda", config.directional_shadows.split_lambda },
			{ "near_cascade_resolution", config.directional_shadows.near_cascade_resolution },
			{ "outer_cascade_resolution", config.directional_shadows.outer_cascade_resolution },
			{ "dynamic_atlas_size", config.directional_shadows.dynamic_atlas_size },
			{ "static_cache_atlas_size", config.directional_shadows.static_cache_atlas_size },
			{ "static_cache_budget_mb", config.directional_shadows.static_cache_budget_mb },
			{ "depth_bias", config.directional_shadows.depth_bias },
			{ "normal_bias", config.directional_shadows.normal_bias },
			{ "pcf_radius", config.directional_shadows.pcf_radius }
		} },
		{ "render_debug_view", json{
			{ "enabled", config.render_debug_view.enabled },
			{ "selected", config.render_debug_view.selected.empty() ? std::string("Off") : config.render_debug_view.selected }
		} }
	};
}
```

- [ ] **Step 6: Wire load and save**

In `Scene::load_from_file`, after version validation and before entities are parsed, set:

```cpp
scene.m_impl->storage.render_config = deserialize_scene_render_config(root);
```

At the clean-load tail, after `change_version` is allocated, add:

```cpp
scene.m_impl->storage.render_config_version = scene.m_impl->storage.change_version;
```

In `Scene::save_to_file`, after `root["next_entity_id"]`:

```cpp
root["scene_config"] = serialize_scene_render_config(m_impl->storage.render_config);
```

- [ ] **Step 7: Run self-tests and commit**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-tests pass.

Commit:

```powershell
git add project/src/engine/Function/Scene/Scene.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Serialize scene render config"
```

---

### Task 4: RenderScene Snapshot Data Flow

**Files:**
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add failing frame snapshot self-test**

Add near other render-scene tests:

```cpp
auto test_render_scene_copies_scene_render_config_to_visible_frame() -> bool
{
	Scene scene = Scene::create("RenderSceneConfigSnapshotSelfTest");
	SceneRenderConfig config = scene.get_render_config();
	config.ambient_occlusion.mode = AmbientOcclusionMode::GTAO;
	config.directional_shadows.default_shadow_distance = 321.0f;
	config.render_debug_view.enabled = true;
	config.render_debug_view.selected = "SceneDeferredAO";
	scene.set_render_config(config);

	RenderScene render_scene{};
	if (!render_scene.rebuild_render_config_from_scene(scene))
	{
		return report_self_test_failure("RenderScene render config snapshot", "failed to rebuild render config from scene");
	}

	SceneView view{};
	SceneViewDesc desc{};
	desc.viewport_width = 128;
	desc.viewport_height = 128;
	const glm::mat4 view_matrix = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection_matrix = glm::perspectiveLH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	if (!build_scene_view_from_matrices(desc, view_matrix, projection_matrix, glm::vec3(0.0f, 0.0f, -5.0f), view))
	{
		return report_self_test_failure("RenderScene render config snapshot", "failed to build test view");
	}

	VisibleRenderFrame frame{};
	if (!render_scene.build_visible_render_frame(7, view, frame))
	{
		return report_self_test_failure("RenderScene render config snapshot", "failed to build visible frame");
	}

	const bool ok =
		frame.render_config.ambient_occlusion.mode == AmbientOcclusionMode::GTAO &&
		frame.render_config.directional_shadows.default_shadow_distance == 321.0f &&
		frame.render_config.render_debug_view.enabled &&
		frame.render_config.render_debug_view.selected == "SceneDeferredAO";
	return ok ||
		report_self_test_failure("RenderScene render config snapshot", "VisibleRenderFrame did not carry SceneRenderConfig");
}
```

Add to `run_engine_base_self_tests()` after existing render scene extraction tests:

```cpp
all_passed = test_render_scene_copies_scene_render_config_to_visible_frame() && all_passed;
```

- [ ] **Step 2: Run failing build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: compile fails because `VisibleRenderFrame::render_config` and `RenderScene::rebuild_render_config_from_scene` do not exist.

- [ ] **Step 3: Add render config to VisibleRenderFrame and RenderScene**

Modify `RenderScene.h`:

```cpp
#include "Function/Scene/SceneConfig.h"
```

Add to `VisibleRenderFrame`:

```cpp
SceneRenderConfig render_config{};
```

Add to `RenderScene` public API:

```cpp
bool rebuild_render_config_from_scene(const Scene& scene);
```

Add to private data:

```cpp
SceneRenderConfig m_render_config = make_default_scene_render_config();
```

- [ ] **Step 4: Implement rebuild and snapshot copying**

Modify `RenderScene.cpp`.

In copy/move operations, copy or move `m_render_config` together with `m_lights` and `m_environment`.

In `rebuild_from_scene`, after lights/environment rebuild:

```cpp
ASH_PROCESS_ERROR(rebuild_render_config_from_scene(scene));
```

Add:

```cpp
bool RenderScene::rebuild_render_config_from_scene(const Scene& scene)
{
	ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_render_config_from_scene", AshEngine::Profile::Color::Scene);
	std::scoped_lock<std::mutex> lock(m_mutex);
	m_render_config = scene.get_render_config();
	return true;
}
```

In `build_visible_render_frame`, while holding the mutex and copying other frame data:

```cpp
out_frame.render_config = m_render_config;
```

In `build_visible_light_frame`, also copy:

```cpp
out_frame.render_config = m_render_config;
```

- [ ] **Step 5: Update ScenePresentationSubsystem version tracking**

Modify `SceneState` in `ScenePresentationSubsystem.cpp`:

```cpp
uint64_t last_render_config_version = 0;
```

In update logic, read:

```cpp
const uint64_t scene_render_config_version = binding.scene->get_render_config_version();
```

On full rebuild, assign:

```cpp
scene_state->last_render_config_version = scene_render_config_version;
```

Add a config-only branch after environment-only branch:

```cpp
else if (scene_state->last_render_config_version != scene_render_config_version)
{
	ASH_PROFILE_SCOPE_NC("ScenePresentation::RebuildRenderSceneConfig", AshEngine::Profile::Color::Scene);
	scene_state->render_scene_valid = scene_state->render_scene.rebuild_render_config_from_scene(*binding.scene);
	scene_state->last_render_config_version = scene_render_config_version;
	if (!scene_state->render_scene_valid)
	{
		HLogError(
			"ScenePresentationSubsystem: failed to rebuild render config for binding '{}' and scene '{}'.",
			binding.debug_name,
			binding.scene->get_name());
	}
}
```

- [ ] **Step 6: Run tests and commit**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-tests pass.

Commit:

```powershell
git add project/src/engine/Function/Render/RenderScene.h project/src/engine/Function/Render/RenderScene.cpp project/src/engine/Function/Render/ScenePresentationSubsystem.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Snapshot scene render config into visible frames"
```

---

### Task 5: Render Passes Consume Frame Config

**Files:**
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.h`
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.h`
- Modify: `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
- Modify: `project/src/engine/Function/Render/RenderDebugView.h`
- Modify: `project/src/engine/Function/Render/RenderDebugView.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`

- [ ] **Step 1: Change AmbientOcclusionPass API**

Modify `AmbientOcclusionPass.h`:

```cpp
AmbientOcclusionPassOutputs add_passes(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	const SceneRenderViewContext& view_context,
	const AmbientOcclusionConfig& config);
```

Modify `AmbientOcclusionPass.cpp` implementation signature to match. At the start of `add_passes`, before selecting programs, add:

```cpp
if (m_config.temporal != config.temporal ||
	m_config.half_resolution != config.half_resolution ||
	m_config.quality != config.quality)
{
	reset_temporal_history();
}
m_config = config;
```

- [ ] **Step 2: Change DirectionalShadowPass API**

Modify `DirectionalShadowPass.h`:

```cpp
DirectionalShadowPassOutputs add_depth_passes(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneRenderViewContext& view_context,
	const DirectionalShadowConfig& config,
	uint64_t render_frame_index,
	const DirectionalShadowCasterDrawCallback& draw_callback);
```

Change private `build_frame_plan` to accept config:

```cpp
bool build_frame_plan(
	const VisibleRenderFrame& frame,
	const DirectionalShadowConfig& config,
	uint32_t output_width,
	uint32_t output_height,
	DirectionalShadowFramePlan& out_plan);
```

Add a private helper:

```cpp
void reset_static_cache_resources();
```

- [ ] **Step 3: Implement DirectionalShadowPass config update**

Modify `DirectionalShadowPass.cpp`. At the start of `add_depth_passes`, add:

```cpp
const bool static_cache_layout_changed =
	m_config.static_cache_atlas_size != config.static_cache_atlas_size ||
	m_config.static_cache_budget_mb != config.static_cache_budget_mb ||
	m_config.near_cascade_resolution != config.near_cascade_resolution ||
	m_config.outer_cascade_resolution != config.outer_cascade_resolution;
if (static_cache_layout_changed)
{
	reset_static_cache_resources();
}
m_config = config;
```

Implement:

```cpp
void DirectionalShadowPass::reset_static_cache_resources()
{
	m_static_cache_atlas.reset();
	m_static_cache_entries.clear();
	m_static_cache_free_tiles.clear();
	m_static_cache_cursor_x = 0u;
	m_static_cache_cursor_y = 0u;
	m_static_cache_row_height = 0u;
	m_static_cache_evicted_tile_count = 0u;
}
```

Pass `config` to `build_frame_plan` and to `DirectionalShadowDetail::build_directional_shadow_frame_plan_internal`.

- [ ] **Step 4: Change RenderDebugView API**

Modify `RenderDebugView.h`:

```cpp
bool add_pass(
	RenderGraphBuilder& graph,
	RenderGraphTextureRef output_target,
	const SceneRenderViewContext& view_context,
	const RenderDebugViewConfig& config);
```

Modify `RenderDebugView.cpp` implementation to remove this line from `add_pass`:

```cpp
const RenderDebugViewConfig config = get_runtime_render_debug_view_config();
```

Use the passed `config` directly. Keep `draw_ui` using `get_runtime_render_debug_view_config()` and `set_runtime_render_debug_view_config()` as an in-memory process override UI path.

- [ ] **Step 5: Use frame config from SceneRenderer**

Modify `SceneRenderer.cpp`.

Replace AO call:

```cpp
const AmbientOcclusionPassOutputs ao_outputs = m_ambient_occlusion_pass.add_passes(
	graph,
	frame,
	deferred_resources,
	view_context,
	frame.render_config.ambient_occlusion);
```

Replace directional shadow call:

```cpp
directional_shadow_outputs = m_directional_shadow_pass.add_depth_passes(
	graph,
	frame,
	view_context,
	frame.render_config.directional_shadows,
	render_frame_index,
	draw_shadow_casters);
```

Replace render debug view pass call:

```cpp
m_render_debug_view.add_pass(graph, final_output, view_context, frame.render_config.render_debug_view);
```

Remove `get_runtime_directional_shadow_config()` from the scene rendering path.

- [ ] **Step 6: Build, self-test, and commit**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build and self-tests pass.

Commit:

```powershell
git add project/src/engine/Function/Render/AmbientOcclusionPass.h project/src/engine/Function/Render/AmbientOcclusionPass.cpp project/src/engine/Function/Render/DirectionalShadowPass.h project/src/engine/Function/Render/DirectionalShadowPass.cpp project/src/engine/Function/Render/RenderDebugView.h project/src/engine/Function/Render/RenderDebugView.cpp project/src/engine/Function/Render/SceneRenderer.cpp
git commit -m "Use scene render config in render passes"
```

---

### Task 6: Standard Sandbox Scene Loader

**Files:**
- Modify: `project/src/sandbox/App/SandboxStandardScene.h`
- Modify: `project/src/sandbox/App/SandboxStandardScene.cpp`
- Modify: `project/src/sandbox/App/SandboxApplication.h`
- Modify: `project/src/sandbox/App/SandboxApplication.cpp`

- [ ] **Step 1: Refactor SandboxStandardScene state names**

Modify `SandboxStandardSceneLoadState`:

```cpp
enum class SandboxStandardSceneLoadState : uint8_t
{
	Idle = 0,
	LoadingScene,
	Ready,
	Failed
};
```

Modify `SandboxStandardSceneSnapshot`:

```cpp
SandboxStandardSceneLoadState load_state = SandboxStandardSceneLoadState::Idle;
std::string failure_detail{};
std::filesystem::path scene_path{};
AshEngine::Scene scene{};
AshEngine::EntityId primary_camera_entity_id = 0;
float recommended_camera_move_speed = 8.0f;
```

Remove `sample_asset_path`.

- [ ] **Step 2: Replace public sample model APIs**

In `SandboxStandardScene.h`, remove:

```cpp
static auto get_sample_asset_root_path() -> const std::filesystem::path&;
static auto get_canonical_sample_asset_path() -> const std::filesystem::path&;
static auto discover_sample_asset_paths(const AshEngine::AssetDatabase& asset_database) -> std::vector<std::filesystem::path>;
static auto make_sample_asset_label(const std::filesystem::path& sample_asset_path) -> std::string;
auto start(AshEngine::AssetDatabase& asset_database, const std::filesystem::path& sample_asset_path) -> bool;
```

Add:

```cpp
static auto get_standard_scene_path() -> const std::filesystem::path&;
```

Keep:

```cpp
auto start(AshEngine::AssetDatabase& asset_database) -> bool;
```

- [ ] **Step 3: Replace private async model state**

In `SandboxStandardScene.h`, remove:

```cpp
std::shared_future<std::shared_ptr<const AshEngine::Model>> m_model_future{};
```

Remove private methods:

```cpp
auto _build_runtime_snapshot(
	const std::shared_ptr<const AshEngine::Model>& model,
	const std::filesystem::path& sample_asset_path,
	SandboxStandardSceneSnapshot& out_snapshot,
	std::string& out_error) -> bool;
auto _create_primary_camera(...);
auto _create_default_lights(...);
```

Add private methods:

```cpp
auto _load_scene_snapshot(
	AshEngine::AssetDatabase& asset_database,
	SandboxStandardSceneSnapshot& out_snapshot,
	std::string& out_error) const -> bool;
auto _find_primary_camera(
	AshEngine::Scene& scene,
	AshEngine::EntityId& out_camera_entity_id,
	float& out_recommended_move_speed,
	std::string& out_error) const -> bool;
auto _validate_referenced_assets(
	const AshEngine::Scene& scene,
	const AshEngine::AssetDatabase& asset_database,
	std::string& out_error) const -> bool;
```

- [ ] **Step 4: Implement scene path loading**

In `SandboxStandardScene.cpp`, set:

```cpp
static constexpr char k_standard_scene_path[] = "product/assets/scenes/Sandbox.scene.json";
static constexpr float k_default_camera_move_speed = 8.0f;
```

Implement:

```cpp
auto SandboxStandardScene::get_standard_scene_path() -> const std::filesystem::path&
{
	static const std::filesystem::path k_scene_path = k_standard_scene_path;
	return k_scene_path;
}
```

Implement `start` as a synchronous scene load:

```cpp
auto SandboxStandardScene::start(AshEngine::AssetDatabase& asset_database) -> bool
{
	std::string failure_detail{};
	SandboxStandardSceneSnapshot snapshot{};
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

	ASH_PROCESS_ERROR(asset_database.is_valid());
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_asset_database = &asset_database;
		m_snapshot = {};
		m_snapshot.load_state = SandboxStandardSceneLoadState::LoadingScene;
		m_snapshot.scene_path = get_standard_scene_path();
		m_free_camera_controller.reset();
		m_has_logic_tick_time = false;
	}

	ASH_PROCESS_ERROR(_load_scene_snapshot(asset_database, snapshot, failure_detail));

	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_snapshot = std::move(snapshot);
		m_free_camera_controller.reset();
		m_free_camera_controller.bind_camera_entity(m_snapshot.primary_camera_entity_id);
		m_free_camera_controller.set_move_speed(m_snapshot.recommended_camera_move_speed);
		m_has_logic_tick_time = false;
	}

	ASH_PROCESS_GUARD_END(bResult, false);
	if (!bResult)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		_set_failure_locked(failure_detail.empty() ? "Sandbox standard scene failed to load." : std::move(failure_detail));
	}
	return bResult;
}
```

- [ ] **Step 5: Implement snapshot loading and validation**

Add:

```cpp
auto SandboxStandardScene::_load_scene_snapshot(
	AshEngine::AssetDatabase& asset_database,
	SandboxStandardSceneSnapshot& out_snapshot,
	std::string& out_error) const -> bool
{
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
	out_snapshot = {};
	out_snapshot.scene_path = get_standard_scene_path();

	if (!std::filesystem::exists(out_snapshot.scene_path))
	{
		out_error = "Sandbox standard scene file is missing: '" + out_snapshot.scene_path.generic_string() + "'.";
		ASH_PROCESS_ERROR(false);
	}

	out_snapshot.scene = AshEngine::Scene::load_from_file(out_snapshot.scene_path, &out_error);
	ASH_PROCESS_ERROR(out_snapshot.scene.is_valid());
	ASH_PROCESS_ERROR(_validate_referenced_assets(out_snapshot.scene, asset_database, out_error));
	ASH_PROCESS_ERROR(_find_primary_camera(
		out_snapshot.scene,
		out_snapshot.primary_camera_entity_id,
		out_snapshot.recommended_camera_move_speed,
		out_error));

	out_snapshot.load_state = SandboxStandardSceneLoadState::Ready;
	out_snapshot.failure_detail.clear();
	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

Add:

```cpp
auto SandboxStandardScene::_find_primary_camera(
	AshEngine::Scene& scene,
	AshEngine::EntityId& out_camera_entity_id,
	float& out_recommended_move_speed,
	std::string& out_error) const -> bool
{
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
	for (const AshEngine::Entity& entity : scene.get_entities_with_component(AshEngine::SceneComponentType::Camera))
	{
		const AshEngine::CameraComponent camera = entity.get_camera_component();
		if (camera.primary)
		{
			out_camera_entity_id = entity.get_id();
			out_recommended_move_speed = k_default_camera_move_speed;
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}
	}
	out_error = "Sandbox standard scene does not contain a primary camera.";
	ASH_PROCESS_ERROR(false);
	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

Add:

```cpp
auto SandboxStandardScene::_validate_referenced_assets(
	const AshEngine::Scene& scene,
	const AshEngine::AssetDatabase& asset_database,
	std::string& out_error) const -> bool
{
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
	for (const AshEngine::SceneMeshExtractionDesc& mesh_desc : scene.extract_mesh_entities())
	{
		const std::filesystem::path asset_path = std::filesystem::path(mesh_desc.mesh.asset_path).lexically_normal();
		if (asset_path.empty() || asset_database.find_asset_by_path(asset_path) == nullptr)
		{
			out_error = "Sandbox standard scene references missing mesh asset: '" + asset_path.generic_string() + "'.";
			ASH_PROCESS_ERROR(false);
		}
	}

	AshEngine::SceneEnvironmentExtractionDesc environment{};
	if (scene.extract_active_environment(environment) && !environment.ibl_asset_path.empty())
	{
		const std::filesystem::path ibl_path = environment.ibl_asset_path;
		if (!std::filesystem::exists(ibl_path))
		{
			out_error = "Sandbox standard scene references missing IBL asset: '" + ibl_path.generic_string() + "'.";
			ASH_PROCESS_ERROR(false);
		}
	}
	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

- [ ] **Step 6: Simplify update_logic**

In `SandboxStandardScene::update_logic`, remove all async model future branches. Keep only:

```cpp
if (load_state == SandboxStandardSceneLoadState::Failed)
{
	ASH_PROCESS_ERROR(false);
}

if (load_state == SandboxStandardSceneLoadState::Ready)
{
	SandboxStandardSceneSnapshot working_snapshot{};
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		working_snapshot = m_snapshot;
	}

	if (m_free_camera_controller.get_camera_entity_id() == 0 && working_snapshot.primary_camera_entity_id != 0)
	{
		m_free_camera_controller.bind_camera_entity(working_snapshot.primary_camera_entity_id);
		m_free_camera_controller.set_move_speed(working_snapshot.recommended_camera_move_speed);
	}

	std::string update_error{};
	const double logic_delta_seconds = _consume_logic_delta_seconds();
	if (!m_free_camera_controller.update(working_snapshot.scene, input, logic_delta_seconds, update_error))
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		_set_failure_locked(update_error.empty() ? "Failed to update the Sandbox standard scene free camera." : std::move(update_error));
		ASH_PROCESS_ERROR(false);
	}

	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_snapshot = std::move(working_snapshot);
	}
}
```

- [ ] **Step 7: Remove model selection from SandboxApplication**

In `SandboxApplication.h`, remove `SandboxModelSelectionState`, `m_modelSelectionMutex`, `m_modelSelectionState`, and these methods:

```cpp
auto _initialize_model_selection_options() -> bool;
auto _switch_standard_scene_model(int32_t model_index) -> bool;
auto _process_pending_model_selection() -> bool;
auto _draw_model_selection_overlay() -> void;
auto _request_model_selection_from_ui(int32_t model_index) -> void;
auto _get_model_selection_state_copy() const -> SandboxModelSelectionState;
auto _set_model_selection_status(const std::string& status) -> void;
auto _sync_model_selection_status_from_scene() -> void;
```

In `SandboxApplication.cpp`, remove `get_startup_model_override`, `normalize_startup_model_path`, `_initialize_model_selection_options`, `_switch_standard_scene_model`, `_process_pending_model_selection`, `_draw_model_selection_overlay`, `_request_model_selection_from_ui`, `_get_model_selection_state_copy`, `_set_model_selection_status`, and `_sync_model_selection_status_from_scene`.

Change `_initialize_paths_and_assets` tail:

```cpp
return true;
```

Change `_start_standard_scene`:

```cpp
auto SandboxApplication::_start_standard_scene() -> bool
{
	if (!m_standardScene.start(m_assetDatabase))
	{
		const std::string failure_detail = m_standardScene.get_failure_detail();
		HLogError(
			"Sandbox failed to start the standard-scene runtime: {}",
			failure_detail.empty() ? "Unknown error." : failure_detail);
		return false;
	}

	HLogInfo(
		"Sandbox standard scene runtime is using '{}'.",
		SandboxStandardScene::get_standard_scene_path().generic_string());
	return true;
}
```

Change `_on_logic_update` to remove `_process_pending_model_selection()`.

Change `_on_gui`:

```cpp
auto SandboxApplication::_on_gui() -> void
{
}
```

Change summary logging to print `snapshot.scene_path` instead of `snapshot.sample_asset_path`.

- [ ] **Step 8: Build and commit**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build and self-tests pass. Runtime smoke can still fail until the scene file is added in Task 7.

Commit:

```powershell
git add project/src/sandbox/App/SandboxStandardScene.h project/src/sandbox/App/SandboxStandardScene.cpp project/src/sandbox/App/SandboxApplication.h project/src/sandbox/App/SandboxApplication.cpp
git commit -m "Load Sandbox from a standard scene file"
```

---

### Task 7: Add Sandbox.scene.json

**Files:**
- Create: `product/assets/scenes/Sandbox.scene.json`

- [ ] **Step 1: Confirm Sponza is a single model mesh**

Run:

```powershell
$j = Get-Content -Raw product/assets/models/gltfs/Sponza/glTF/Sponza.gltf | ConvertFrom-Json; "meshes=$($j.meshes.Count) nodes=$($j.nodes.Count) scenes=$($j.scenes.Count)"
```

Expected:

```text
meshes=1 nodes=1 scenes=1
```

- [ ] **Step 2: Create the standard scene asset**

Create `product/assets/scenes/Sandbox.scene.json`:

```json
{
  "version": 4,
  "name": "Sandbox",
  "next_entity_id": 8,
  "scene_config": {
    "ambient_occlusion": {
      "mode": "HBAO",
      "quality": "Medium",
      "radius": 1.5,
      "intensity": 1.0,
      "power": 1.0,
      "half_resolution": true,
      "blur": true,
      "temporal": true,
      "temporal_blend": 0.85,
      "temporal_depth_threshold": 0.01,
      "temporal_normal_threshold": 0.75
    },
    "directional_shadows": {
      "enabled": true,
      "default_cascade_count": 4,
      "default_shadow_distance": 160.0,
      "near_shadow_distance": 16.0,
      "split_lambda": 0.65,
      "near_cascade_resolution": 2048,
      "outer_cascade_resolution": 1024,
      "dynamic_atlas_size": 4096,
      "static_cache_atlas_size": 4096,
      "static_cache_budget_mb": 64,
      "depth_bias": 0.0015,
      "normal_bias": 0.05,
      "pcf_radius": 1
    },
    "render_debug_view": {
      "enabled": false,
      "selected": "SceneDeferredSceneHDRLinear"
    }
  },
  "entities": [
    {
      "id": 1,
      "parent": 0,
      "name": "Sandbox",
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation_euler_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      }
    },
    {
      "id": 2,
      "parent": 1,
      "name": "Sponza",
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation_euler_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "mesh": {
        "asset_path": "models/gltfs/Sponza/glTF/Sponza.gltf",
        "mesh_index": 0,
        "visible": true,
        "mobility": 0,
        "layer_mask": 1
      }
    },
    {
      "id": 3,
      "parent": 1,
      "name": "MainCamera",
      "transform": {
        "position": [0.0, 2.0, -8.0],
        "rotation_euler_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "camera": {
        "primary": true,
        "reverse_z": true,
        "projection": 0,
        "fov_y_degrees": 60.0,
        "near_plane": 0.1,
        "far_plane": 5000.0,
        "orthographic_height": 10.0
      }
    },
    {
      "id": 4,
      "parent": 1,
      "name": "DirectionalLight",
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation_euler_degrees": [58.0, -23.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "light": {
        "type": 0,
        "color": [1.0, 0.95, 0.88],
        "intensity": 2.5,
        "range": 10.0,
        "inner_cone_angle_degrees": 30.0,
        "outer_cone_angle_degrees": 45.0,
        "casts_shadow": true,
        "shadow_priority": 128,
        "shadow_distance": 160.0,
        "shadow_cascade_count": 4,
        "near_shadow_distance": 16.0
      }
    },
    {
      "id": 5,
      "parent": 1,
      "name": "PointLight",
      "transform": {
        "position": [0.0, 1.0, 0.0],
        "rotation_euler_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "light": {
        "type": 1,
        "color": [0.45, 0.68, 1.0],
        "intensity": 24.0,
        "range": 12.0,
        "inner_cone_angle_degrees": 30.0,
        "outer_cone_angle_degrees": 45.0,
        "casts_shadow": false,
        "shadow_priority": 64,
        "shadow_distance": 0.0,
        "shadow_cascade_count": 0,
        "near_shadow_distance": 0.0
      }
    },
    {
      "id": 6,
      "parent": 1,
      "name": "SpotLight",
      "transform": {
        "position": [1.0, 1.0, 0.0],
        "rotation_euler_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "light": {
        "type": 2,
        "color": [1.0, 0.0, 0.0],
        "intensity": 36.0,
        "range": 16.0,
        "inner_cone_angle_degrees": 18.0,
        "outer_cone_angle_degrees": 36.0,
        "casts_shadow": false,
        "shadow_priority": 64,
        "shadow_distance": 0.0,
        "shadow_cascade_count": 0,
        "near_shadow_distance": 0.0
      }
    },
    {
      "id": 7,
      "parent": 1,
      "name": "SkyEnvironment",
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation_euler_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "environment": {
        "active": true,
        "ibl_asset_path": "product/assets/textures/skybox/citrus_orchard_puresky_4k.ashibl",
        "source_texture_path": "product/assets/textures/skybox/citrus_orchard_puresky_4k.hdr",
        "intensity": 1.0,
        "rotation_degrees": 0.0,
        "visible_background": true,
        "affect_lighting": true
      }
    }
  ]
}
```

- [ ] **Step 3: Verify scene load through self-test path**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build and self-tests pass.

- [ ] **Step 4: Smoke Sandbox once**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: process exits with code 0 and logs mention `Sandbox standard scene runtime is using 'product/assets/scenes/Sandbox.scene.json'`.

- [ ] **Step 5: Commit the scene asset**

```powershell
git add product/assets/scenes/Sandbox.scene.json
git commit -m "Add standard Sandbox scene asset"
```

---

### Task 8: Config Cleanup And Documentation

**Files:**
- Modify: `product/config/Engine.ini`
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `docs/ScenePresentationSubsystemGuide.md`

- [ ] **Step 1: Clean Engine.ini authoritative scene defaults**

Modify `product/config/Engine.ini` so it keeps process-level sections only. The file must still include these sections:

```ini
[RHI]
Backend=Vulkan

[Rendering]
VSync=false

[VulkanValidation]
Enabled=true
GpuAssisted=false
SynchronizationValidation=true
BreakOnValidationError=false

[DX12Validation]
Enabled=true
GpuValidation=true
```

Keep the existing `[EnvironmentLighting]` section and its current keys. Remove these sections from the checked-in runtime config:

```ini
[AmbientOcclusion]
[DirectionalShadows]
[RenderDebugView]
```

- [ ] **Step 2: Update README**

In `README.md`, add or update the runtime/config overview with these facts:

```md
- Sandbox now starts from `product/assets/scenes/Sandbox.scene.json`; the scene file owns the standard model, camera, lights, environment, and per-scene render settings.
- Per-scene render settings live under top-level `scene_config` in scene JSON. The first migrated settings are `ambient_occlusion`, `directional_shadows`, and `render_debug_view`.
- `product/config/Engine.ini` remains the process-level config source for RHI backend, validation, VSync, and environment lighting bake/cache policy.
```

- [ ] **Step 3: Update EngineDeveloperGuide**

In `docs/EngineDeveloperGuide.md`, update the `DynamicRHI` and render-switch sections:

```md
`product/config/Engine.ini` is no longer the authoritative source for per-scene `AmbientOcclusion`, `DirectionalShadows`, or `RenderDebugView` defaults. Those settings are stored in scene JSON under `scene_config` and travel through `Scene -> ScenePresentationSubsystem -> RenderScene -> VisibleRenderFrame -> SceneRenderer`.
```

Update the Sandbox section:

```md
Sandbox's standard path loads `product/assets/scenes/Sandbox.scene.json`. That scene contains the Sponza mesh reference, primary camera, directional / point / spot lights, active sky environment, and the standard per-scene render config.
```

- [ ] **Step 4: Update ScenePresentationSubsystem guide**

In `docs/ScenePresentationSubsystemGuide.md`, add the new frame data item:

```md
`VisibleRenderFrame` carries a `SceneRenderConfig` snapshot. `ScenePresentationSubsystem` watches `Scene::get_render_config_version()` and refreshes `RenderScene` render config without rebuilding primitives, transforms, lights, or environment data.
```

- [ ] **Step 5: Build, self-test, and commit docs/config**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build and self-tests pass.

Commit only this task's files. If `README.md` or `product/config/Engine.ini` contains unrelated pre-existing edits, inspect the diff and stage only the hunks from this task.

```powershell
git add product/config/Engine.ini docs/EngineDeveloperGuide.md docs/ScenePresentationSubsystemGuide.md
git add -p README.md
git commit -m "Document scene-owned render config"
```

---

### Task 9: Final Runtime Validation

**Files:**
- Inspect logs under `product/logs`
- Write validation logs under `Intermediate/logs` if command output is captured

- [ ] **Step 1: Build Sandbox and Editor**

Run:

```powershell
./build_sandbox.bat Debug x64
./build_editor.bat Debug x64
```

Expected: both builds pass.

- [ ] **Step 2: Run headless self-tests**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code 0.

- [ ] **Step 3: Run Sandbox Vulkan smoke**

Set `[RHI] Backend=Vulkan` in `product/config/Engine.ini`, then run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
```

Expected: exit code 0, graceful shutdown, no validation/debug-layer errors, no resource leak output.

- [ ] **Step 4: Run Sandbox DX12 smoke**

Set `[RHI] Backend=DX12` in `product/config/Engine.ini`, then run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
```

Expected: exit code 0, graceful shutdown, no validation/debug-layer errors, no resource leak output.

- [ ] **Step 5: Run Editor Vulkan smoke**

Set `[RHI] Backend=Vulkan` in `product/config/Engine.ini`, then run:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected: exit code 0, graceful shutdown, no validation/debug-layer errors, no resource leak output.

- [ ] **Step 6: Run Editor DX12 smoke**

Set `[RHI] Backend=DX12` in `product/config/Engine.ini`, then run:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected: exit code 0, graceful shutdown, no validation/debug-layer errors, no resource leak output.

- [ ] **Step 7: Restore intended checked-in backend**

Set `[RHI] Backend=Vulkan` in `product/config/Engine.ini` unless the branch intentionally changes the checked-in backend.

- [ ] **Step 8: Commit any validation-driven fixes**

If validation required fixes, commit only the changed task files:

```powershell
git status --short
git add <exact task files>
git commit -m "Stabilize scene config runtime validation"
```

Expected: no unrelated files are staged.

---

## Self-Review Results

- Spec coverage: the plan covers scene-owned render config, v3 compatibility, v4 saves, AO/shadow/debug-view migration, render-frame snapshotting, Sandbox scene loading, `Engine.ini` cleanup, docs, and Vulkan/DX12 validation.
- Boundary check: no task modifies `project/src/editor`; Editor is only run for validation.
- Type consistency: `SceneRenderConfig`, `get_render_config`, `set_render_config`, `get_render_config_version`, `VisibleRenderFrame::render_config`, and `RenderScene::rebuild_render_config_from_scene` names are consistent across tasks.
- Placeholder scan: no open implementation markers are intentionally left in the plan.
- Worktree hygiene: each commit command names exact files and calls out `git add -p README.md` for the known dirty documentation file.
