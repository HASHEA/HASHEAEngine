# Bloom Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a UE-style Gaussian bloom pass to the deferred HDR scene path, disabled by default and enabled in the standard Sandbox scene.

**Architecture:** Bloom is a new Engine Function/Render post-process module. Scene JSON owns `scene_config.bloom`; `RenderScene` copies it into `VisibleRenderFrame`; `SceneRenderer` inserts `BloomPass` after sky/background HDR and before tone map. The pass uses RenderGraph raster screen passes and `RGBA16_SFLOAT` transient resources, so Vulkan/DX12 state transitions stay pass-boundary driven.

**Tech Stack:** C++17, AshEngine Function/Render, RenderGraph raster passes, HLSL screen shaders, Engine self-tests, Premake/MSBuild.

---

## File Structure

- Create `project/src/engine/Function/Render/BloomConfig.h`: public scene-owned bloom config enums, structs, parse helpers, sanitization, defaults.
- Create `project/src/engine/Function/Render/BloomConfig.cpp`: names, string parsing, clamping, default stage values.
- Modify `project/src/engine/Function/Scene/SceneConfig.h`: add `BloomConfig bloom`.
- Modify `project/src/engine/Function/Scene/SceneConfig.cpp`: compare/copy bloom config in scene render config equality.
- Modify `project/src/engine/Function/Scene/Scene.cpp`: load/save `scene_config.bloom`.
- Create `project/src/engine/Function/Render/BloomPass.h`: `BloomPassOutputs` and `BloomPass` interface.
- Create `project/src/engine/Function/Render/BloomPass.cpp`: sampler/program creation, graph pass chain, debug-selection output.
- Create `project/src/engine/Shaders/Deferred/BloomCommon.hlsli`: full-screen triangle helpers and shared root constants.
- Create `project/src/engine/Shaders/Deferred/BloomSetup.hlsl`: threshold + soft-knee bright extraction.
- Create `project/src/engine/Shaders/Deferred/BloomDownsample.hlsl`: filtered 2x downsample.
- Create `project/src/engine/Shaders/Deferred/BloomUpsample.hlsl`: upsample + stage tint/radius combine.
- Create `project/src/engine/Shaders/Deferred/BloomComposite.hlsl`: add final bloom to HDR.
- Modify `project/src/engine/Function/Render/SceneDeferredGraphResources.h`: add bloom graph refs if integration is cleaner with shared resources.
- Modify `project/src/engine/Function/Render/SceneRenderer.h`: own `BloomPass`.
- Modify `project/src/engine/Function/Render/SceneRenderer.cpp`: initialize/shutdown bloom, insert passes, register debug items.
- Modify `project/src/engine/Base/EngineSelfTests.cpp`: config, JSON, propagation, source contract, graph contract tests.
- Modify `product/assets/scenes/Sandbox.scene.json`: enable bloom in the standard scene only.
- Modify `README.md`, `docs/EngineDeveloperGuide.md`, `docs/RenderGraphAPISpec.md`: document scene-owned bloom and updated render path.

Premake already includes `**.h`, `**.cpp`, `**.hlsl`, and `**.hlsli` under `project/src/engine`, so no project-file edit is required for new Engine source and shader files.

---

### Task 1: Bloom Config Model And Sanitization

**Files:**
- Create: `project/src/engine/Function/Render/BloomConfig.h`
- Create: `project/src/engine/Function/Render/BloomConfig.cpp`
- Modify: `project/src/engine/Function/Scene/SceneConfig.h`
- Modify: `project/src/engine/Function/Scene/SceneConfig.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-test for bloom defaults, parsing, and sanitization**

Add this include near the other render config includes in `project/src/engine/Base/EngineSelfTests.cpp`:

```cpp
#include "Function/Render/BloomConfig.h"
```

Add this test in the anonymous self-test namespace near the existing render config tests:

```cpp
auto test_bloom_config_defaults_and_sanitization() -> bool
{
	BloomConfig defaults = make_default_bloom_config();
	if (defaults.enabled ||
		defaults.quality != BloomQuality::High ||
		defaults.intensity != 0.6f ||
		defaults.threshold != 1.0f ||
		defaults.soft_knee != 0.5f ||
		defaults.size_scale != 1.0f ||
		defaults.debug_view != BloomDebugView::Off ||
		defaults.stages.size() != 6u)
	{
		return report_self_test_failure("Bloom config", "default bloom config does not match the design contract");
	}

	BloomQuality quality = BloomQuality::Low;
	BloomDebugView debug_view = BloomDebugView::Off;
	if (!try_parse_bloom_quality("Epic", quality) || quality != BloomQuality::Epic)
	{
		return report_self_test_failure("Bloom config", "failed to parse Epic quality");
	}
	if (!try_parse_bloom_debug_view("CompositeHDR", debug_view) || debug_view != BloomDebugView::CompositeHDR)
	{
		return report_self_test_failure("Bloom config", "failed to parse CompositeHDR debug view");
	}

	BloomConfig invalid = defaults;
	invalid.enabled = true;
	invalid.intensity = -4.0f;
	invalid.threshold = 1000.0f;
	invalid.soft_knee = -1.0f;
	invalid.size_scale = 50.0f;
	invalid.stages[0].size = -8.0f;
	invalid.stages[0].tint = glm::vec3(-1.0f, 16.0f, 0.5f);

	const BloomConfig sanitized = sanitize_bloom_config(invalid, defaults);
	const bool sanitized_ok =
		sanitized.enabled &&
		sanitized.intensity == 0.0f &&
		sanitized.threshold == 64.0f &&
		sanitized.soft_knee == 0.0f &&
		sanitized.size_scale == 8.0f &&
		sanitized.stages[0].size == 0.0f &&
		sanitized.stages[0].tint.x == 0.0f &&
		sanitized.stages[0].tint.y == 8.0f &&
		sanitized.stages[0].tint.z == 0.5f;
	if (!sanitized_ok)
	{
		return report_self_test_failure("Bloom config", "sanitize_bloom_config did not clamp fields as expected");
	}

	return true;
}
```

Add it to `run_engine_base_self_tests()` after `test_engine_ini_excludes_scene_render_config_sections()`:

```cpp
all_passed = test_bloom_config_defaults_and_sanitization() && all_passed;
```

- [ ] **Step 2: Run build and confirm the test fails to compile**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: compile fails because `Function/Render/BloomConfig.h` and `BloomConfig` do not exist.

- [ ] **Step 3: Implement `BloomConfig.h`**

Create `project/src/engine/Function/Render/BloomConfig.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include <array>
#include <cstdint>
#include <string_view>
#include <glm/glm.hpp>

namespace AshEngine
{
	enum class BloomQuality : uint8_t
	{
		Low = 0,
		Medium,
		High,
		Epic
	};

	enum class BloomDebugView : uint8_t
	{
		Off = 0,
		Setup,
		Mip1,
		Mip2,
		Mip3,
		Mip4,
		Mip5,
		Mip6,
		Final,
		CompositeHDR
	};

	struct ASH_API BloomStageConfig
	{
		float size = 1.0f;
		glm::vec3 tint{ 1.0f, 1.0f, 1.0f };
	};

	struct ASH_API BloomConfig
	{
		bool enabled = false;
		BloomQuality quality = BloomQuality::High;
		float intensity = 0.6f;
		float threshold = 1.0f;
		float soft_knee = 0.5f;
		float size_scale = 1.0f;
		std::array<BloomStageConfig, 6> stages{};
		BloomDebugView debug_view = BloomDebugView::Off;
	};

	ASH_API const char* bloom_quality_name(BloomQuality quality);
	ASH_API const char* bloom_debug_view_name(BloomDebugView view);
	ASH_API bool try_parse_bloom_quality(std::string_view value, BloomQuality& out_quality);
	ASH_API bool try_parse_bloom_debug_view(std::string_view value, BloomDebugView& out_view);
	ASH_API BloomConfig sanitize_bloom_config(const BloomConfig& config, const BloomConfig& fallback);
	ASH_API BloomConfig make_default_bloom_config();
}
```

- [ ] **Step 4: Implement `BloomConfig.cpp`**

Create `project/src/engine/Function/Render/BloomConfig.cpp`:

```cpp
#include "Function/Render/BloomConfig.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_bloom_token(std::string_view value) -> std::string
		{
			std::string result(value);
			result.erase(std::remove_if(result.begin(), result.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0 || ch == '_' || ch == '-';
			}), result.end());
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return result;
		}

		auto clamp_stage(BloomStageConfig stage) -> BloomStageConfig
		{
			stage.size = std::clamp(stage.size, 0.0f, 16.0f);
			stage.tint.x = std::clamp(stage.tint.x, 0.0f, 8.0f);
			stage.tint.y = std::clamp(stage.tint.y, 0.0f, 8.0f);
			stage.tint.z = std::clamp(stage.tint.z, 0.0f, 8.0f);
			return stage;
		}
	}

	const char* bloom_quality_name(BloomQuality quality)
	{
		switch (quality)
		{
		case BloomQuality::Low:
			return "Low";
		case BloomQuality::Medium:
			return "Medium";
		case BloomQuality::Epic:
			return "Epic";
		case BloomQuality::High:
		default:
			return "High";
		}
	}

	const char* bloom_debug_view_name(BloomDebugView view)
	{
		switch (view)
		{
		case BloomDebugView::Setup:
			return "Setup";
		case BloomDebugView::Mip1:
			return "Mip1";
		case BloomDebugView::Mip2:
			return "Mip2";
		case BloomDebugView::Mip3:
			return "Mip3";
		case BloomDebugView::Mip4:
			return "Mip4";
		case BloomDebugView::Mip5:
			return "Mip5";
		case BloomDebugView::Mip6:
			return "Mip6";
		case BloomDebugView::Final:
			return "Final";
		case BloomDebugView::CompositeHDR:
			return "CompositeHDR";
		case BloomDebugView::Off:
		default:
			return "Off";
		}
	}

	bool try_parse_bloom_quality(std::string_view value, BloomQuality& out_quality)
	{
		const std::string token = normalize_bloom_token(value);
		if (token == "low")
		{
			out_quality = BloomQuality::Low;
			return true;
		}
		if (token == "medium")
		{
			out_quality = BloomQuality::Medium;
			return true;
		}
		if (token == "high")
		{
			out_quality = BloomQuality::High;
			return true;
		}
		if (token == "epic")
		{
			out_quality = BloomQuality::Epic;
			return true;
		}
		return false;
	}

	bool try_parse_bloom_debug_view(std::string_view value, BloomDebugView& out_view)
	{
		const std::string token = normalize_bloom_token(value);
		if (token.empty() || token == "off" || token == "none")
		{
			out_view = BloomDebugView::Off;
			return true;
		}
		if (token == "setup")
		{
			out_view = BloomDebugView::Setup;
			return true;
		}
		if (token == "mip1")
		{
			out_view = BloomDebugView::Mip1;
			return true;
		}
		if (token == "mip2")
		{
			out_view = BloomDebugView::Mip2;
			return true;
		}
		if (token == "mip3")
		{
			out_view = BloomDebugView::Mip3;
			return true;
		}
		if (token == "mip4")
		{
			out_view = BloomDebugView::Mip4;
			return true;
		}
		if (token == "mip5")
		{
			out_view = BloomDebugView::Mip5;
			return true;
		}
		if (token == "mip6")
		{
			out_view = BloomDebugView::Mip6;
			return true;
		}
		if (token == "final")
		{
			out_view = BloomDebugView::Final;
			return true;
		}
		if (token == "compositehdr")
		{
			out_view = BloomDebugView::CompositeHDR;
			return true;
		}
		return false;
	}

	BloomConfig sanitize_bloom_config(const BloomConfig& config, const BloomConfig& fallback)
	{
		BloomConfig result = config;
		result.intensity = std::clamp(result.intensity, 0.0f, 10.0f);
		result.threshold = std::clamp(result.threshold, -1.0f, 64.0f);
		result.soft_knee = std::clamp(result.soft_knee, 0.0f, 1.0f);
		result.size_scale = std::clamp(result.size_scale, 0.1f, 8.0f);
		for (BloomStageConfig& stage : result.stages)
		{
			stage = clamp_stage(stage);
		}
		if (result.stages.empty())
		{
			result.stages = fallback.stages;
		}
		return result;
	}

	BloomConfig make_default_bloom_config()
	{
		BloomConfig config{};
		config.enabled = false;
		config.quality = BloomQuality::High;
		config.intensity = 0.6f;
		config.threshold = 1.0f;
		config.soft_knee = 0.5f;
		config.size_scale = 1.0f;
		config.stages = {
			BloomStageConfig{ 0.3f, glm::vec3(1.0f, 1.0f, 1.0f) },
			BloomStageConfig{ 1.0f, glm::vec3(1.0f, 0.95f, 0.9f) },
			BloomStageConfig{ 2.0f, glm::vec3(0.9f, 0.95f, 1.0f) },
			BloomStageConfig{ 4.0f, glm::vec3(0.8f, 0.9f, 1.0f) },
			BloomStageConfig{ 8.0f, glm::vec3(0.7f, 0.8f, 1.0f) },
			BloomStageConfig{ 16.0f, glm::vec3(0.6f, 0.7f, 1.0f) }
		};
		config.debug_view = BloomDebugView::Off;
		return config;
	}
}
```

- [ ] **Step 5: Wire bloom into `SceneRenderConfig`**

Modify `project/src/engine/Function/Scene/SceneConfig.h`:

```cpp
#include "Function/Render/BloomConfig.h"
```

Change `SceneRenderConfig` to:

```cpp
struct ASH_API SceneRenderConfig
{
	AmbientOcclusionConfig ambient_occlusion{};
	DirectionalShadowConfig directional_shadows{};
	BloomConfig bloom{};
};
```

Modify `project/src/engine/Function/Scene/SceneConfig.cpp` by adding:

```cpp
auto bloom_stage_equal(const BloomStageConfig& lhs, const BloomStageConfig& rhs) -> bool
{
	return lhs.size == rhs.size &&
		lhs.tint.x == rhs.tint.x &&
		lhs.tint.y == rhs.tint.y &&
		lhs.tint.z == rhs.tint.z;
}

auto bloom_config_equal(const BloomConfig& lhs, const BloomConfig& rhs) -> bool
{
	if (lhs.enabled != rhs.enabled ||
		lhs.quality != rhs.quality ||
		lhs.intensity != rhs.intensity ||
		lhs.threshold != rhs.threshold ||
		lhs.soft_knee != rhs.soft_knee ||
		lhs.size_scale != rhs.size_scale ||
		lhs.debug_view != rhs.debug_view)
	{
		return false;
	}

	for (size_t index = 0; index < lhs.stages.size(); ++index)
	{
		if (!bloom_stage_equal(lhs.stages[index], rhs.stages[index]))
		{
			return false;
		}
	}
	return true;
}
```

Set defaults in `make_default_scene_render_config()`:

```cpp
config.bloom = make_default_bloom_config();
```

Update `scene_render_config_equal()`:

```cpp
return ambient_occlusion_config_equal(lhs.ambient_occlusion, rhs.ambient_occlusion) &&
	directional_shadow_config_equal(lhs.directional_shadows, rhs.directional_shadows) &&
	bloom_config_equal(lhs.bloom, rhs.bloom);
```

- [ ] **Step 6: Run build and self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test includes no `Bloom config` failure.

- [ ] **Step 7: Commit config model**

Run:

```powershell
git add project/src/engine/Function/Render/BloomConfig.h project/src/engine/Function/Render/BloomConfig.cpp project/src/engine/Function/Scene/SceneConfig.h project/src/engine/Function/Scene/SceneConfig.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add scene bloom configuration"
```

---

### Task 2: Scene JSON And Visible Frame Propagation

**Files:**
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Extend scene JSON self-test before implementation**

In `test_render_scene_copies_scene_render_config_to_visible_frame()`, set bloom fields before `scene.set_render_config(config)`:

```cpp
config.bloom.enabled = true;
config.bloom.quality = BloomQuality::Epic;
config.bloom.intensity = 1.25f;
config.bloom.threshold = 0.75f;
config.bloom.debug_view = BloomDebugView::Final;
```

Extend the `ok` condition:

```cpp
light_frame.render_config.bloom.enabled &&
light_frame.render_config.bloom.quality == BloomQuality::Epic &&
light_frame.render_config.bloom.intensity == 1.25f &&
full_frame.render_config.bloom.enabled &&
full_frame.render_config.bloom.debug_view == BloomDebugView::Final
```

In `test_scene_render_config_json_defaults_and_round_trip()`, add this `bloom` block to the configured scene JSON after `directional_shadows`:

```cpp
"    \"bloom\": {\n"
"      \"enabled\": true,\n"
"      \"quality\": \"Epic\",\n"
"      \"intensity\": 1.5,\n"
"      \"threshold\": -4.0,\n"
"      \"soft_knee\": 2.0,\n"
"      \"size_scale\": 10.0,\n"
"      \"debug_view\": \"CompositeHDR\",\n"
"      \"stages\": [\n"
"        { \"size\": 0.5, \"tint\": [1.0, 1.0, 1.0] },\n"
"        { \"size\": 1.5, \"tint\": [1.0, 0.8, 0.7] },\n"
"        { \"size\": 2.5, \"tint\": [0.8, 0.9, 1.0] },\n"
"        { \"size\": 4.5, \"tint\": [0.7, 0.8, 1.0] },\n"
"        { \"size\": 8.5, \"tint\": [0.6, 0.7, 1.0] },\n"
"        { \"size\": 20.0, \"tint\": [-1.0, 9.0, 0.25] }\n"
"      ]\n"
"    }\n"
```

Extend `parsed_ok`:

```cpp
loaded.bloom.enabled &&
loaded.bloom.quality == BloomQuality::Epic &&
loaded.bloom.intensity == 1.5f &&
loaded.bloom.threshold == -1.0f &&
loaded.bloom.soft_knee == 1.0f &&
loaded.bloom.size_scale == 8.0f &&
loaded.bloom.debug_view == BloomDebugView::CompositeHDR &&
loaded.bloom.stages[5].size == 16.0f &&
loaded.bloom.stages[5].tint.x == 0.0f &&
loaded.bloom.stages[5].tint.y == 8.0f &&
loaded.bloom.stages[5].tint.z == 0.25f
```

In `test_engine_ini_excludes_scene_render_config_sections()`, add this check:

```cpp
engine_ini_source.find("[Bloom]") != std::string::npos
```

Expected: the build succeeds, but `Sandbox.exe --engine-self-test` fails because `scene_config.bloom` is not parsed or serialized.

- [ ] **Step 2: Implement bloom JSON helpers**

In `project/src/engine/Function/Scene/Scene.cpp`, add these helpers near the existing scene config JSON helpers:

```cpp
static auto try_get_json_vec3(const json& object, const char* key, glm::vec3& out_value) -> bool
{
	const auto value_it = object.find(key);
	if (value_it == object.end() || !value_it->is_array() || value_it->size() != 3u)
	{
		return false;
	}

	try
	{
		out_value.x = (*value_it)[0].get<float>();
		out_value.y = (*value_it)[1].get<float>();
		out_value.z = (*value_it)[2].get<float>();
		return true;
	}
	catch (const std::exception& exception)
	{
		HLogWarning("SceneConfig field '{}' has invalid vec3 value: {}.", key, exception.what());
		return false;
	}
}

static auto serialize_vec3(const glm::vec3& value) -> json
{
	return json::array({ value.x, value.y, value.z });
}
```

- [ ] **Step 3: Parse `scene_config.bloom`**

Inside `deserialize_scene_render_config()`, after directional shadows parsing, add:

```cpp
if (const auto bloom_it = scene_config.find("bloom"); bloom_it != scene_config.end() && bloom_it->is_object())
{
	try_get_json_value(*bloom_it, "enabled", config.bloom.enabled);

	std::string quality{};
	if (try_get_json_value(*bloom_it, "quality", quality))
	{
		BloomQuality parsed = config.bloom.quality;
		if (try_parse_bloom_quality(quality, parsed))
		{
			config.bloom.quality = parsed;
		}
		else
		{
			HLogWarning(
				"SceneConfig bloom.quality '{}' is invalid. Keeping default '{}'.",
				quality,
				bloom_quality_name(config.bloom.quality));
		}
	}

	try_get_json_value(*bloom_it, "intensity", config.bloom.intensity);
	try_get_json_value(*bloom_it, "threshold", config.bloom.threshold);
	try_get_json_value(*bloom_it, "soft_knee", config.bloom.soft_knee);
	try_get_json_value(*bloom_it, "size_scale", config.bloom.size_scale);

	std::string debug_view{};
	if (try_get_json_value(*bloom_it, "debug_view", debug_view))
	{
		BloomDebugView parsed = config.bloom.debug_view;
		if (try_parse_bloom_debug_view(debug_view, parsed))
		{
			config.bloom.debug_view = parsed;
		}
		else
		{
			HLogWarning(
				"SceneConfig bloom.debug_view '{}' is invalid. Keeping default '{}'.",
				debug_view,
				bloom_debug_view_name(config.bloom.debug_view));
		}
	}

	if (const auto stages_it = bloom_it->find("stages"); stages_it != bloom_it->end() && stages_it->is_array())
	{
		const size_t stage_count = std::min(config.bloom.stages.size(), stages_it->size());
		for (size_t index = 0; index < stage_count; ++index)
		{
			const json& stage_json = (*stages_it)[index];
			if (!stage_json.is_object())
			{
				continue;
			}
			try_get_json_value(stage_json, "size", config.bloom.stages[index].size);
			try_get_json_vec3(stage_json, "tint", config.bloom.stages[index].tint);
		}
	}

	config.bloom = sanitize_bloom_config(config.bloom, make_default_bloom_config());
}
```

- [ ] **Step 4: Serialize `scene_config.bloom`**

Extend `serialize_scene_render_config()` by adding a `bloom` entry:

```cpp
{ "bloom", json{
	{ "enabled", config.bloom.enabled },
	{ "quality", bloom_quality_name(config.bloom.quality) },
	{ "intensity", config.bloom.intensity },
	{ "threshold", config.bloom.threshold },
	{ "soft_knee", config.bloom.soft_knee },
	{ "size_scale", config.bloom.size_scale },
	{ "debug_view", bloom_debug_view_name(config.bloom.debug_view) },
	{ "stages", json::array({
		json{ { "size", config.bloom.stages[0].size }, { "tint", serialize_vec3(config.bloom.stages[0].tint) } },
		json{ { "size", config.bloom.stages[1].size }, { "tint", serialize_vec3(config.bloom.stages[1].tint) } },
		json{ { "size", config.bloom.stages[2].size }, { "tint", serialize_vec3(config.bloom.stages[2].tint) } },
		json{ { "size", config.bloom.stages[3].size }, { "tint", serialize_vec3(config.bloom.stages[3].tint) } },
		json{ { "size", config.bloom.stages[4].size }, { "tint", serialize_vec3(config.bloom.stages[4].tint) } },
		json{ { "size", config.bloom.stages[5].size }, { "tint", serialize_vec3(config.bloom.stages[5].tint) } }
	}) }
} }
```

- [ ] **Step 5: Sanitize bloom in `Scene::set_render_config()`**

In `Scene::set_render_config()`, after directional shadows sanitization, add:

```cpp
sanitized_config.bloom =
	sanitize_bloom_config(config.bloom, make_default_bloom_config());
```

- [ ] **Step 6: Run build and self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test includes no scene render config JSON or render config snapshot failure.

- [ ] **Step 7: Commit scene JSON support**

Run:

```powershell
git add project/src/engine/Function/Scene/Scene.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Serialize scene bloom settings"
```

---

### Task 3: Bloom Shader Contract And Pass Skeleton

**Files:**
- Create: `project/src/engine/Function/Render/BloomPass.h`
- Create: `project/src/engine/Function/Render/BloomPass.cpp`
- Create: `project/src/engine/Shaders/Deferred/BloomCommon.hlsli`
- Create: `project/src/engine/Shaders/Deferred/BloomSetup.hlsl`
- Create: `project/src/engine/Shaders/Deferred/BloomDownsample.hlsl`
- Create: `project/src/engine/Shaders/Deferred/BloomUpsample.hlsl`
- Create: `project/src/engine/Shaders/Deferred/BloomComposite.hlsl`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add failing source contract test**

Add this helper in `project/src/engine/Base/EngineSelfTests.cpp`:

```cpp
auto file_contains_all(const char* path, const std::vector<const char*>& needles) -> bool
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return false;
	}
	const std::string source{
		std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>()
	};
	for (const char* needle : needles)
	{
		if (source.find(needle) == std::string::npos)
		{
			return false;
		}
	}
	return true;
}
```

If a helper with the same purpose already exists, reuse the existing helper and do not add a duplicate.

Add this self-test:

```cpp
auto test_bloom_shader_source_contract() -> bool
{
	const bool setup_ok = file_contains_all(
		"project/src/engine/Shaders/Deferred/BloomSetup.hlsl",
		{ "VSMain", "PSMain", "SceneHDRLinear", "AshBloomThresholdSoftKnee", "SceneLinearClampSampler" });
	const bool downsample_ok = file_contains_all(
		"project/src/engine/Shaders/Deferred/BloomDownsample.hlsl",
		{ "VSMain", "PSMain", "BloomInput", "AshBloomSourceSize", "SceneLinearClampSampler" });
	const bool upsample_ok = file_contains_all(
		"project/src/engine/Shaders/Deferred/BloomUpsample.hlsl",
		{ "VSMain", "PSMain", "BloomLowInput", "BloomHighInput", "AshBloomStageTintRadius", "SceneLinearClampSampler" });
	const bool composite_ok = file_contains_all(
		"project/src/engine/Shaders/Deferred/BloomComposite.hlsl",
		{ "VSMain", "PSMain", "SceneHDRLinear", "SceneBloomFinal", "AshBloomCompositeParams", "SceneLinearClampSampler" });

	return (setup_ok && downsample_ok && upsample_ok && composite_ok) ||
		report_self_test_failure("Bloom shader source contract", "bloom shaders are missing required entry points or binding names");
}
```

Add it to `run_engine_base_self_tests()` after `test_bloom_config_defaults_and_sanitization()`:

```cpp
all_passed = test_bloom_shader_source_contract() && all_passed;
```

Expected: self-test fails because the shader files do not exist.

- [ ] **Step 2: Create `BloomPass.h`**

Create `project/src/engine/Function/Render/BloomPass.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/BloomConfig.h"
#include "Function/Render/RenderGraphFwd.h"
#include <array>
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class Renderer;
	class RenderSampler;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	struct BloomPassOutputs
	{
		RenderGraphTextureRef scene_hdr_linear{};
		RenderGraphTextureRef setup{};
		std::array<RenderGraphTextureRef, 6> mips{};
		RenderGraphTextureRef final_bloom{};
		RenderGraphTextureRef composite_hdr{};
	};

	class BloomPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		BloomPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context,
			const BloomConfig& config);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_setup_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_downsample_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_upsample_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_composite_program = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
	};
}
```

- [ ] **Step 3: Create shared bloom shader include**

Create `project/src/engine/Shaders/Deferred/BloomCommon.hlsli`:

```hlsl
#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct VSFullscreenOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSFullscreenOutput AshBloomFullscreen(uint vertex_id)
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

cbuffer AshRootConstants : register(b0)
{
	float4 AshBloomSourceSize;
	float4 AshBloomTargetSize;
	float4 AshBloomThresholdSoftKnee;
	float4 AshBloomStageTintRadius;
	float4 AshBloomCompositeParams;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshBloomFullscreen(vertex_id);
}

float AshBloomLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 AshBloomPositive(float3 color)
{
	return max(color, 0.0);
}
```

- [ ] **Step 4: Create bloom shaders**

Create `project/src/engine/Shaders/Deferred/BloomSetup.hlsl`:

```hlsl
#include "BloomCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
SamplerState SceneLinearClampSampler : register(s0);

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float3 color = AshBloomPositive(SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb);
	float threshold = AshBloomThresholdSoftKnee.x;
	float soft_knee = AshBloomThresholdSoftKnee.y;

	if (threshold < 0.0)
	{
		return float4(color, 1.0);
	}

	float luminance = AshBloomLuminance(color);
	float knee = max(threshold * soft_knee, 1e-5);
	float soft = saturate((luminance - threshold + knee) / (2.0 * knee));
	soft = soft * soft * knee;
	float contribution = max(luminance - threshold, soft);
	float weight = contribution / max(luminance, 1e-5);
	return float4(color * saturate(weight), 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/BloomDownsample.hlsl`:

```hlsl
#include "BloomCommon.hlsli"

Texture2D<float4> BloomInput : register(t0);
SamplerState SceneLinearClampSampler : register(s0);

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float2 texel = AshBloomSourceSize.zw;
	float3 color = 0.0;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(-1.0, -1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2( 1.0, -1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(-1.0,  1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2( 1.0,  1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv).rgb * 4.0;
	color *= 0.125;
	return float4(AshBloomPositive(color), 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/BloomUpsample.hlsl`:

```hlsl
#include "BloomCommon.hlsli"

Texture2D<float4> BloomLowInput : register(t0);
Texture2D<float4> BloomHighInput : register(t1);
SamplerState SceneLinearClampSampler : register(s0);

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float2 texel = AshBloomSourceSize.zw * max(AshBloomStageTintRadius.w, 0.0);
	float3 low = 0.0;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(-1.0, 0.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2( 1.0, 0.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(0.0, -1.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(0.0,  1.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv).rgb * 4.0;
	low *= 0.125;

	float3 high = BloomHighInput.Sample(SceneLinearClampSampler, input.uv).rgb;
	float3 tint = AshBloomStageTintRadius.rgb;
	return float4(AshBloomPositive(high + low * tint), 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/BloomComposite.hlsl`:

```hlsl
#include "BloomCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
Texture2D<float4> SceneBloomFinal : register(t1);
SamplerState SceneLinearClampSampler : register(s0);

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float3 hdr = SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb;
	float3 bloom = SceneBloomFinal.Sample(SceneLinearClampSampler, input.uv).rgb;
	float intensity = AshBloomCompositeParams.x;
	return float4(AshBloomPositive(hdr + bloom * intensity), 1.0);
}
```

- [ ] **Step 5: Create pass skeleton with resource/program creation**

Create `project/src/engine/Function/Render/BloomPass.cpp` with initialization, shutdown, shader source hashing, fullscreen draw helpers, and disabled-pass behavior:

```cpp
#include "Function/Render/BloomPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cstring>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_bloom_common_shader_path =
			"project/src/engine/Shaders/Deferred/BloomCommon.hlsli";
		static constexpr const char* k_bloom_setup_shader_path =
			"project/src/engine/Shaders/Deferred/BloomSetup.hlsl";
		static constexpr const char* k_bloom_downsample_shader_path =
			"project/src/engine/Shaders/Deferred/BloomDownsample.hlsl";
		static constexpr const char* k_bloom_upsample_shader_path =
			"project/src/engine/Shaders/Deferred/BloomUpsample.hlsl";
		static constexpr const char* k_bloom_composite_shader_path =
			"project/src/engine/Shaders/Deferred/BloomComposite.hlsl";

		struct BloomRootConstants
		{
			glm::vec4 source_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 target_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 threshold_soft_knee{ 1.0f, 0.5f, 0.0f, 0.0f };
			glm::vec4 stage_tint_radius{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 composite_params{ 0.6f, 0.0f, 0.0f, 0.0f };
		};

		static_assert(sizeof(BloomRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto build_bloom_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_bloom_common_shader_path);
			return hash_value;
		}

		auto make_program_desc(const char* shader_path, const char* name, const GraphicsProgramState& state) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_bloom_shader_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}

		void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext& view_context)
		{
			draw_desc.has_viewport = view_context.has_viewport;
			if (view_context.has_viewport)
			{
				draw_desc.viewport = view_context.viewport;
			}
			draw_desc.has_scissor = view_context.has_scissor;
			if (view_context.has_scissor)
			{
				draw_desc.scissor = view_context.scissor;
			}
		}

		void attach_root_constants(GraphicsDrawDesc& draw_desc, GraphicsProgram* program, const BloomRootConstants& constants)
		{
			RHI::ShaderParameterBlockLayout layout{};
			if (!program || !program->get_parameter_block_layout("AshRootConstants", layout) || layout.byte_size == 0)
			{
				return;
			}
			draw_desc.const_data_size = std::min<uint32_t>(
				static_cast<uint32_t>(sizeof(constants)),
				std::min<uint32_t>(layout.byte_size, GraphicsDrawDesc::InlineConstDataCapacity));
			draw_desc.inline_const_data_valid = true;
			std::memcpy(draw_desc.inline_const_data.data(), &constants, draw_desc.const_data_size);
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const BloomRootConstants& constants,
			const SceneRenderViewContext& view_context) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.vertex_count = 3u;
			draw_desc.instance_count = 1u;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}
	}

	bool BloomPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("BloomPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void BloomPass::shutdown()
	{
		m_composite_program.reset();
		m_upsample_program.reset();
		m_downsample_program.reset();
		m_setup_program.reset();
		m_linear_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool BloomPass::create_resources(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Linear;
		sampler_desc.mag_filter = RenderSamplerFilter::Linear;
		sampler_desc.mip_filter = RenderSamplerFilter::Linear;
		m_linear_clamp_sampler = renderer.create_sampler(sampler_desc, "SceneBloomLinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool BloomPass::create_programs(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		GraphicsProgramState state{};
		state.cull_mode = RenderCullMode::None;
		state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		state.depth_test = false;
		state.depth_write = false;
		state.blend_mode = RenderBlendMode::Opaque;

		m_setup_program = renderer.create_graphics_program(make_program_desc(k_bloom_setup_shader_path, "SceneBloomSetup", state));
		m_downsample_program = renderer.create_graphics_program(make_program_desc(k_bloom_downsample_shader_path, "SceneBloomDownsample", state));
		m_upsample_program = renderer.create_graphics_program(make_program_desc(k_bloom_upsample_shader_path, "SceneBloomUpsample", state));
		m_composite_program = renderer.create_graphics_program(make_program_desc(k_bloom_composite_shader_path, "SceneBloomComposite", state));
		ASH_PROCESS_ERROR(m_setup_program && m_downsample_program && m_upsample_program && m_composite_program);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	BloomPassOutputs BloomPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		RenderGraphTextureRef scene_hdr_linear,
		const SceneRenderViewContext& view_context,
		const BloomConfig& config)
	{
		ASH_PROFILE_SCOPE_NC("BloomPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(BloomPassOutputs, outputs, BloomPassOutputs{}, BloomPassOutputs{});
		outputs.scene_hdr_linear = scene_hdr_linear;
		ASH_PROCESS_ERROR(scene_hdr_linear);
		ASH_PROCESS_GUARD_RETURN_END(outputs, BloomPassOutputs{});
	}
}
```

- [ ] **Step 6: Run build and self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and shader source contract passes. Bloom is not integrated yet, so no visual behavior changes.

- [ ] **Step 7: Commit shader and pass skeleton**

Run:

```powershell
git add project/src/engine/Function/Render/BloomPass.h project/src/engine/Function/Render/BloomPass.cpp project/src/engine/Shaders/Deferred/BloomCommon.hlsli project/src/engine/Shaders/Deferred/BloomSetup.hlsl project/src/engine/Shaders/Deferred/BloomDownsample.hlsl project/src/engine/Shaders/Deferred/BloomUpsample.hlsl project/src/engine/Shaders/Deferred/BloomComposite.hlsl project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add bloom pass shader skeleton"
```

---

### Task 4: Bloom RenderGraph Pass Chain

**Files:**
- Modify: `project/src/engine/Function/Render/BloomPass.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add source contract test for pass names and graph declarations**

Add this self-test:

```cpp
auto test_bloom_pass_source_contract() -> bool
{
	std::ifstream pass_file("project/src/engine/Function/Render/BloomPass.cpp");
	if (!pass_file.is_open())
	{
		return report_self_test_failure("Bloom pass source contract", "failed to open BloomPass.cpp");
	}
	const std::string source{
		std::istreambuf_iterator<char>(pass_file),
		std::istreambuf_iterator<char>()
	};

	const char* required_tokens[] = {
		"SceneBloomSetupPass",
		"SceneBloomDownsamplePass",
		"SceneBloomUpsamplePass",
		"SceneBloomCompositePass",
		"pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV)",
		"pass.write_color(0, outputs.setup",
		"pass.read_texture(outputs.final_bloom, RenderGraphAccess::GraphicsSRV)",
		"outputs.scene_hdr_linear = outputs.composite_hdr",
		"ASH_PROFILE_SCOPE_NC(\"SceneBloomSetupPass\"",
		"ASH_PROFILE_SCOPE_NC(\"SceneBloomCompositePass\""
	};

	for (const char* token : required_tokens)
	{
		if (source.find(token) == std::string::npos)
		{
			return report_self_test_failure("Bloom pass source contract", "BloomPass.cpp is missing a required graph contract token");
		}
	}

	return true;
}
```

Add it to `run_engine_base_self_tests()` after `test_bloom_shader_source_contract()`:

```cpp
all_passed = test_bloom_pass_source_contract() && all_passed;
```

Expected: self-test fails because `BloomPass::add_passes()` still returns the input unchanged.

- [ ] **Step 2: Add helper functions to `BloomPass.cpp`**

Inside the anonymous namespace in `BloomPass.cpp`, add:

```cpp
static constexpr RenderColorValue k_bloom_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

auto active_bloom_mip_count(BloomQuality quality) -> uint32_t
{
	switch (quality)
	{
	case BloomQuality::Low:
		return 3u;
	case BloomQuality::Medium:
		return 4u;
	case BloomQuality::Epic:
		return 6u;
	case BloomQuality::High:
	default:
		return 5u;
	}
}

auto make_bloom_texture_desc(uint32_t width, uint32_t height) -> RenderGraphTextureDesc
{
	RenderGraphTextureDesc desc{};
	desc.width = static_cast<uint16_t>(std::max<uint32_t>(width, 1u));
	desc.height = static_cast<uint16_t>(std::max<uint32_t>(height, 1u));
	desc.format = RenderTextureFormat::RGBA16_SFLOAT;
	desc.shader_resource = true;
	desc.unordered_access = false;
	desc.use_optimized_clear_value = true;
	desc.optimized_clear_color = k_bloom_clear_color;
	return desc;
}

auto make_size_constants(
	const std::shared_ptr<RenderTarget>& source,
	const std::shared_ptr<RenderTarget>& target) -> BloomRootConstants
{
	BloomRootConstants constants{};
	const float source_width = source ? static_cast<float>(std::max<uint32_t>(source->get_width(), 1u)) : 1.0f;
	const float source_height = source ? static_cast<float>(std::max<uint32_t>(source->get_height(), 1u)) : 1.0f;
	const float target_width = target ? static_cast<float>(std::max<uint32_t>(target->get_width(), 1u)) : 1.0f;
	const float target_height = target ? static_cast<float>(std::max<uint32_t>(target->get_height(), 1u)) : 1.0f;
	constants.source_size = glm::vec4(source_width, source_height, 1.0f / source_width, 1.0f / source_height);
	constants.target_size = glm::vec4(target_width, target_height, 1.0f / target_width, 1.0f / target_height);
	return constants;
}

auto select_debug_texture(const BloomPassOutputs& outputs, BloomDebugView debug_view) -> RenderGraphTextureRef
{
	switch (debug_view)
	{
	case BloomDebugView::Setup:
		return outputs.setup;
	case BloomDebugView::Mip1:
		return outputs.mips[0];
	case BloomDebugView::Mip2:
		return outputs.mips[1];
	case BloomDebugView::Mip3:
		return outputs.mips[2];
	case BloomDebugView::Mip4:
		return outputs.mips[3];
	case BloomDebugView::Mip5:
		return outputs.mips[4];
	case BloomDebugView::Mip6:
		return outputs.mips[5];
	case BloomDebugView::Final:
		return outputs.final_bloom;
	case BloomDebugView::CompositeHDR:
		return outputs.composite_hdr;
	case BloomDebugView::Off:
	default:
		return {};
	}
}
```

- [ ] **Step 3: Implement setup and downsample passes**

Replace `BloomPass::add_passes()` with a real chain. Start with validation, setup texture, and downsample loop:

```cpp
BloomPassOutputs BloomPass::add_passes(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	RenderGraphTextureRef scene_hdr_linear,
	const SceneRenderViewContext& view_context,
	const BloomConfig& config)
{
	ASH_PROFILE_SCOPE_NC("BloomPass::add_passes", AshEngine::Profile::Color::Scene);
	ASH_PROCESS_GUARD_RETURN(BloomPassOutputs, outputs, BloomPassOutputs{}, BloomPassOutputs{});
	outputs.scene_hdr_linear = scene_hdr_linear;
	ASH_PROCESS_ERROR(m_renderer != nullptr);
	ASH_PROCESS_ERROR(m_setup_program && m_downsample_program && m_upsample_program && m_composite_program);
	ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
	ASH_PROCESS_ERROR(scene_hdr_linear);
	ASH_PROCESS_ERROR(view_context.output_target != nullptr);

	const BloomConfig sanitized_config = sanitize_bloom_config(config, make_default_bloom_config());
	if (!sanitized_config.enabled || sanitized_config.intensity <= 0.0f)
	{
		ASH_PROCESS_GUARD_RETURN_END(outputs, BloomPassOutputs{});
	}

	const uint32_t output_width = std::max<uint32_t>(view_context.output_target->get_width(), 1u);
	const uint32_t output_height = std::max<uint32_t>(view_context.output_target->get_height(), 1u);
	const uint32_t active_mip_count = active_bloom_mip_count(sanitized_config.quality);

	outputs.setup = graph.create_texture(make_bloom_texture_desc(output_width, output_height), "SceneBloomSetup");
	ASH_PROCESS_ERROR(outputs.setup);

	ASH_PROCESS_ERROR(graph.add_raster_pass(
		"SceneBloomSetupPass",
		RenderGraphPassFlags::None,
		[&](RenderGraphRasterPassBuilder& pass)
		{
			pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
			pass.write_color(0, outputs.setup, RenderLoadAction::Clear, k_bloom_clear_color);
		},
		[this, scene_hdr_linear, setup = outputs.setup, &view_context, sanitized_config](RenderGraphRasterContext& context) -> bool
		{
			ASH_PROFILE_SCOPE_NC("SceneBloomSetupPass", AshEngine::Profile::Color::Draw);
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			std::shared_ptr<RenderTarget> input = context.get_texture(scene_hdr_linear);
			std::shared_ptr<RenderTarget> output = context.get_texture(setup);
			ASH_PROCESS_ERROR(input && output);
			BloomRootConstants constants = make_size_constants(input, output);
			constants.threshold_soft_knee = glm::vec4(sanitized_config.threshold, sanitized_config.soft_knee, 0.0f, 0.0f);
			ASH_PROCESS_ERROR(m_setup_program->set_texture("SceneHDRLinear", input));
			ASH_PROCESS_ERROR(m_setup_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
			ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_setup_program.get(), constants, view_context)));
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}));

	RenderGraphTextureRef previous = outputs.setup;
	uint32_t mip_width = output_width;
	uint32_t mip_height = output_height;
	for (uint32_t mip_index = 0; mip_index < active_mip_count; ++mip_index)
	{
		mip_width = std::max<uint32_t>(mip_width / 2u, 1u);
		mip_height = std::max<uint32_t>(mip_height / 2u, 1u);
		const std::string name = "SceneBloomMip" + std::to_string(mip_index + 1u);
		outputs.mips[mip_index] = graph.create_texture(make_bloom_texture_desc(mip_width, mip_height), name.c_str());
		ASH_PROCESS_ERROR(outputs.mips[mip_index]);

		const RenderGraphTextureRef input_ref = previous;
		const RenderGraphTextureRef output_ref = outputs.mips[mip_index];
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneBloomDownsamplePass",
			RenderGraphPassFlags::None,
			[input_ref, output_ref](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(input_ref, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, output_ref, RenderLoadAction::Clear, k_bloom_clear_color);
			},
			[this, input_ref, output_ref, &view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneBloomDownsamplePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> input = context.get_texture(input_ref);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_ref);
				ASH_PROCESS_ERROR(input && output);
				BloomRootConstants constants = make_size_constants(input, output);
				ASH_PROCESS_ERROR(m_downsample_program->set_texture("BloomInput", input));
				ASH_PROCESS_ERROR(m_downsample_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_downsample_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		previous = outputs.mips[mip_index];
	}
```

Keep this code open because the next step appends the upsample and composite section before the guard return.

- [ ] **Step 4: Add upsample and composite passes**

Continue inside `BloomPass::add_passes()` after the downsample loop:

```cpp
	RenderGraphTextureRef combined = outputs.mips[active_mip_count - 1u];
	for (int32_t mip_index = static_cast<int32_t>(active_mip_count) - 2; mip_index >= 0; --mip_index)
	{
		const uint32_t stage_index = static_cast<uint32_t>(mip_index);
		const std::shared_ptr<RenderTarget> output_target = view_context.output_target;
		const uint32_t target_width = std::max<uint32_t>(output_width >> (stage_index + 1u), 1u);
		const uint32_t target_height = std::max<uint32_t>(output_height >> (stage_index + 1u), 1u);
		const std::string name = "SceneBloomMip" + std::to_string(stage_index + 1u) + "Combined";
		RenderGraphTextureRef combined_output = graph.create_texture(make_bloom_texture_desc(target_width, target_height), name.c_str());
		ASH_PROCESS_ERROR(combined_output);

		const RenderGraphTextureRef low_input = combined;
		const RenderGraphTextureRef high_input = outputs.mips[stage_index];
		const BloomStageConfig stage = sanitized_config.stages[stage_index];
		const float radius = stage.size * sanitized_config.size_scale;
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneBloomUpsamplePass",
			RenderGraphPassFlags::None,
			[low_input, high_input, combined_output](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(low_input, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(high_input, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, combined_output, RenderLoadAction::Clear, k_bloom_clear_color);
			},
			[this, low_input, high_input, combined_output, &view_context, stage, radius](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneBloomUpsamplePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> low = context.get_texture(low_input);
				std::shared_ptr<RenderTarget> high = context.get_texture(high_input);
				std::shared_ptr<RenderTarget> output = context.get_texture(combined_output);
				ASH_PROCESS_ERROR(low && high && output);
				BloomRootConstants constants = make_size_constants(low, output);
				constants.stage_tint_radius = glm::vec4(stage.tint, radius);
				ASH_PROCESS_ERROR(m_upsample_program->set_texture("BloomLowInput", low));
				ASH_PROCESS_ERROR(m_upsample_program->set_texture("BloomHighInput", high));
				ASH_PROCESS_ERROR(m_upsample_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_upsample_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		outputs.mips[stage_index] = combined_output;
		combined = combined_output;
	}

	outputs.final_bloom = combined;
	outputs.composite_hdr = graph.create_texture(make_bloom_texture_desc(output_width, output_height), "SceneBloomCompositeHDR");
	ASH_PROCESS_ERROR(outputs.final_bloom && outputs.composite_hdr);

	ASH_PROCESS_ERROR(graph.add_raster_pass(
		"SceneBloomCompositePass",
		RenderGraphPassFlags::None,
		[scene_hdr_linear, final_bloom = outputs.final_bloom, composite_hdr = outputs.composite_hdr](RenderGraphRasterPassBuilder& pass)
		{
			pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
			pass.read_texture(final_bloom, RenderGraphAccess::GraphicsSRV);
			pass.write_color(0, composite_hdr, RenderLoadAction::Clear, k_bloom_clear_color);
		},
		[this, scene_hdr_linear, final_bloom = outputs.final_bloom, composite_hdr = outputs.composite_hdr, &view_context, sanitized_config](RenderGraphRasterContext& context) -> bool
		{
			ASH_PROFILE_SCOPE_NC("SceneBloomCompositePass", AshEngine::Profile::Color::Draw);
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			std::shared_ptr<RenderTarget> hdr = context.get_texture(scene_hdr_linear);
			std::shared_ptr<RenderTarget> bloom = context.get_texture(final_bloom);
			std::shared_ptr<RenderTarget> output = context.get_texture(composite_hdr);
			ASH_PROCESS_ERROR(hdr && bloom && output);
			BloomRootConstants constants = make_size_constants(hdr, output);
			constants.composite_params = glm::vec4(sanitized_config.intensity, 0.0f, 0.0f, 0.0f);
			ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneHDRLinear", hdr));
			ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneBloomFinal", bloom));
			ASH_PROCESS_ERROR(m_composite_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
			ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_composite_program.get(), constants, view_context)));
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}));

	outputs.scene_hdr_linear = outputs.composite_hdr;
	if (RenderGraphTextureRef debug_texture = select_debug_texture(outputs, sanitized_config.debug_view))
	{
		outputs.scene_hdr_linear = debug_texture;
	}

	ASH_PROCESS_GUARD_RETURN_END(outputs, BloomPassOutputs{});
}
```

Add `#include <string>` at the top of `BloomPass.cpp` for pass resource names.

- [ ] **Step 5: Build and run self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test includes no `Bloom pass source contract` failure.

- [ ] **Step 6: Commit RenderGraph chain**

Run:

```powershell
git add project/src/engine/Function/Render/BloomPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add bloom RenderGraph passes"
```

---

### Task 5: SceneRenderer Integration, Debug View, And Sandbox Enablement

**Files:**
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `product/assets/scenes/Sandbox.scene.json`

- [ ] **Step 1: Add failing SceneRenderer source contract test**

Add this self-test:

```cpp
auto test_scene_renderer_bloom_integration_contract() -> bool
{
	std::ifstream header_file("project/src/engine/Function/Render/SceneRenderer.h");
	std::ifstream source_file("project/src/engine/Function/Render/SceneRenderer.cpp");
	if (!header_file.is_open() || !source_file.is_open())
	{
		return report_self_test_failure("SceneRenderer bloom integration", "failed to open SceneRenderer source files");
	}
	const std::string header{
		std::istreambuf_iterator<char>(header_file),
		std::istreambuf_iterator<char>()
	};
	const std::string source{
		std::istreambuf_iterator<char>(source_file),
		std::istreambuf_iterator<char>()
	};

	const bool header_ok =
		header.find("#include \"Function/Render/BloomPass.h\"") != std::string::npos &&
		header.find("BloomPass m_bloom_pass") != std::string::npos;
	const size_t sky_pos = source.find("m_sky_background_pass.add_pass");
	const size_t bloom_pos = source.find("m_bloom_pass.add_passes");
	const size_t tone_pos = source.find("m_post_process_tone_map_pass.add_pass");
	const bool order_ok =
		sky_pos != std::string::npos &&
		bloom_pos != std::string::npos &&
		tone_pos != std::string::npos &&
		sky_pos < bloom_pos &&
		bloom_pos < tone_pos;
	const bool debug_ok =
		source.find("\"SceneBloomSetup\"") != std::string::npos &&
		source.find("\"SceneBloomFinal\"") != std::string::npos &&
		source.find("\"SceneBloomCompositeHDR\"") != std::string::npos;

	return (header_ok && order_ok && debug_ok) ||
		report_self_test_failure("SceneRenderer bloom integration", "bloom pass is not owned, ordered, or debug-registered correctly");
}
```

Add it to `run_engine_base_self_tests()` after `test_bloom_pass_source_contract()`:

```cpp
all_passed = test_scene_renderer_bloom_integration_contract() && all_passed;
```

Expected: self-test fails because `SceneRenderer` does not include or call `BloomPass`.

- [ ] **Step 2: Own and initialize `BloomPass`**

Modify `project/src/engine/Function/Render/SceneRenderer.h`:

```cpp
#include "Function/Render/BloomPass.h"
```

Add the member before `PostProcessToneMapPass m_post_process_tone_map_pass{}`:

```cpp
BloomPass m_bloom_pass{};
```

In `SceneRenderer::initialize()`, after sky/background or before tone-map initialization, add:

```cpp
ASH_PROCESS_ERROR(m_bloom_pass.initialize(m_renderer));
```

In `SceneRenderer::shutdown()`, before tone-map or near other render pass shutdowns, add:

```cpp
m_bloom_pass.shutdown();
```

- [ ] **Step 3: Add debug registration helper calls**

After `m_sky_background_pass.add_pass()` and before tone-map in `SceneRenderer.cpp`, insert:

```cpp
const BloomPassOutputs bloom_outputs = m_bloom_pass.add_passes(
	graph,
	frame,
	graph_resources.scene_hdr_linear,
	view_context,
	frame.render_config.bloom);
ASH_PROCESS_ERROR(bloom_outputs.scene_hdr_linear);
graph_resources.scene_hdr_linear = bloom_outputs.scene_hdr_linear;

register_render_debug_item(
	m_render_debug_view,
	"SceneBloomSetup",
	"Bloom Setup",
	bloom_outputs.setup,
	RenderDebugVisualization::LinearHDR,
	RenderTextureFormat::RGBA16_SFLOAT,
	output_width,
	output_height);
for (uint32_t bloom_mip_index = 0; bloom_mip_index < static_cast<uint32_t>(bloom_outputs.mips.size()); ++bloom_mip_index)
{
	const RenderGraphTextureRef bloom_mip = bloom_outputs.mips[bloom_mip_index];
	const std::string debug_name = "SceneBloomMip" + std::to_string(bloom_mip_index + 1u);
	const std::string display_name = "Bloom Mip " + std::to_string(bloom_mip_index + 1u);
	register_render_debug_item(
		m_render_debug_view,
		debug_name.c_str(),
		display_name.c_str(),
		bloom_mip,
		RenderDebugVisualization::LinearHDR,
		RenderTextureFormat::RGBA16_SFLOAT,
		std::max<uint32_t>(output_width >> (bloom_mip_index + 1u), 1u),
		std::max<uint32_t>(output_height >> (bloom_mip_index + 1u), 1u));
}
register_render_debug_item(
	m_render_debug_view,
	"SceneBloomFinal",
	"Bloom Final",
	bloom_outputs.final_bloom,
	RenderDebugVisualization::LinearHDR,
	RenderTextureFormat::RGBA16_SFLOAT,
	output_width,
	output_height);
register_render_debug_item(
	m_render_debug_view,
	"SceneBloomCompositeHDR",
	"Bloom Composite HDR",
	bloom_outputs.composite_hdr,
	RenderDebugVisualization::LinearHDR,
	RenderTextureFormat::RGBA16_SFLOAT,
	output_width,
	output_height);
```

Add `#include <string>` to `SceneRenderer.cpp` if it is not already present.

- [ ] **Step 4: Enable bloom in Sandbox scene**

In `product/assets/scenes/Sandbox.scene.json`, add this `bloom` object under top-level `scene_config` next to `ambient_occlusion` and `directional_shadows`:

```json
"bloom": {
  "debug_view": "Off",
  "enabled": true,
  "intensity": 0.6,
  "quality": "High",
  "size_scale": 1.0,
  "soft_knee": 0.5,
  "stages": [
    { "size": 0.3, "tint": [1.0, 1.0, 1.0] },
    { "size": 1.0, "tint": [1.0, 0.95, 0.9] },
    { "size": 2.0, "tint": [0.9, 0.95, 1.0] },
    { "size": 4.0, "tint": [0.8, 0.9, 1.0] },
    { "size": 8.0, "tint": [0.7, 0.8, 1.0] },
    { "size": 16.0, "tint": [0.6, 0.7, 1.0] }
  ],
  "threshold": 1.0
}
```

Keep existing JSON formatting stable.

- [ ] **Step 5: Build and run self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test includes no `SceneRenderer bloom integration` failure.

- [ ] **Step 6: Smoke test Sandbox once before docs**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: process exits 0 through normal smoke-test shutdown. Logs under `product/logs/` contain no RenderGraph compiler error and no shader binding error for bloom resources.

- [ ] **Step 7: Commit integration**

Run:

```powershell
git add project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Base/EngineSelfTests.cpp product/assets/scenes/Sandbox.scene.json
git commit -m "Integrate bloom into scene rendering"
```

---

### Task 6: Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `docs/RenderGraphAPISpec.md`

- [ ] **Step 1: Update README render path and config summary**

In `README.md`, update the deferred path descriptions to include bloom:

```text
SceneGBufferPass -> SceneAmbientOcclusionPass -> SceneDeferredLightingAccumPass -> SceneDeferredEnvironmentLightingPass -> SceneDeferredCompositePass -> SceneSkyBackgroundPass -> SceneBloomPass -> SceneDeferredToneMapPass
```

Add `scene_config.bloom` to the scene-owned render settings paragraph:

```text
Sandbox 标准场景从 `product/assets/scenes/Sandbox.scene.json` 启动；scene JSON 顶层 `scene_config` 拥有场景级渲染设置：`ambient_occlusion`、`directional_shadows` 和 `bloom`。Bloom 默认对旧场景关闭，标准 Sandbox scene 开启，用于验证 HDR bloom + tone-map 主链路。
```

- [ ] **Step 2: Update Engine developer guide**

In `docs/EngineDeveloperGuide.md`, update the DynamicRHI/config section to state that `[Bloom]` does not belong in `Engine.ini`:

```text
`product/config/Engine.ini` 不再作为每场景 `AmbientOcclusion`、`DirectionalShadows` 或 `Bloom` 默认值的权威来源；这些设置存储在 scene JSON 顶层 `scene_config` 中，并沿 `Scene -> ScenePresentationSubsystem -> RenderScene -> VisibleRenderFrame -> SceneRenderer` 传递。
```

In the render flow section, describe:

```text
Bloom 在 `SceneDeferredSceneHDRLinear` 完成 sky/background 合成后、tone-map 前运行，使用 `BloomPass` 提交 threshold setup、multi-resolution downsample/upsample 和 HDR composite。所有 bloom 中间 RT 均为 graph transient `RGBA16_SFLOAT`，并可通过 Render Debug View 选择 `SceneBloomSetup`、`SceneBloomMip1..6`、`SceneBloomFinal` 或 `SceneBloomCompositeHDR`。
```

- [ ] **Step 3: Update RenderGraph API spec**

In `docs/RenderGraphAPISpec.md`, update the scene deferred example chain:

```text
SceneGBufferPass -> SceneAmbientOcclusionPass -> SceneDirectionalShadowDepthPass -> SceneDeferredLightingBasePass -> per-light passes -> SceneDeferredEnvironmentLightingPass -> SceneDeferredCompositePass -> SceneSkyBackgroundPass -> SceneBloomSetupPass -> SceneBloomDownsamplePasses -> SceneBloomUpsamplePasses -> SceneBloomCompositePass -> SceneDeferredToneMapPass
```

Add bloom resources to the resource list:

```text
- `SceneBloomSetup` / `SceneBloomMip1..6` / `SceneBloomFinal` / `SceneBloomCompositeHDR`：graph transient `RGBA16_SFLOAT` bloom resources. Bloom reads `SceneDeferredSceneHDRLinear` after sky/background, writes a composited HDR texture, and tone-map consumes the selected bloom output.
```

- [ ] **Step 4: Verify docs mention bloom and no forbidden Engine.ini section**

Run:

```powershell
rg -n "SceneBloom|scene_config.*bloom|\\[Bloom\\]" README.md docs product/config/Engine.ini
```

Expected: README/docs mention bloom, and `product/config/Engine.ini` does not contain `[Bloom]`.

- [ ] **Step 5: Commit docs**

Run:

```powershell
git add README.md docs/EngineDeveloperGuide.md docs/RenderGraphAPISpec.md
git commit -m "Document bloom render path"
```

---

### Task 7: Final Validation

**Files:**
- No planned source edits unless validation finds a defect.

- [ ] **Step 1: Run Engine self-test**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit code 0. No self-test failure mentions bloom, scene render config, RenderGraph, shader source contract, or Engine.ini config authority.

- [ ] **Step 2: Run full shared-path validation**

Use the repository validation loop for this shared render path change. Run the established validation command or skill-backed validation flow that performs:

```text
Sandbox + Vulkan, 25 second graceful smoke shutdown
Sandbox + DX12, 25 second graceful smoke shutdown
Editor + Vulkan, 25 second graceful smoke shutdown
Editor + DX12, 25 second graceful smoke shutdown
```

Expected for every pass:

```text
requested backend matches actual backend
process exits 0
no RenderGraph compiler error
no Vulkan validation error
no DX12 debug-layer error
no shader compile or resource binding error
no shutdown leak report
```

- [ ] **Step 3: Inspect logs for bloom-specific failures**

Run:

```powershell
rg -n "Bloom|SceneBloom|RenderGraph.*error|shader.*error|validation|leak" product/logs
```

Expected: bloom pass names may appear in normal logs or debug labels; there are no error-level bloom, RenderGraph, validation, or leak messages.

- [ ] **Step 4: Commit validation fixes if needed**

If validation required source fixes, commit only those fixes:

```powershell
git add project/src/engine/Function/Render/BloomPass.cpp project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Base/EngineSelfTests.cpp README.md docs/EngineDeveloperGuide.md docs/RenderGraphAPISpec.md product/assets/scenes/Sandbox.scene.json
git commit -m "Fix bloom validation issues"
```

If no fixes were needed, do not create an empty commit.
