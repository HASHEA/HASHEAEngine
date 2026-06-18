# Volumetric Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 AshEngine Engine 侧实现 scene-owned Volumetric Lighting 子系统，支持 directional / point / spot 体积散射、HDR 合成、debug view、screen-space lightshaft fallback，并通过 Vulkan / DX12 共享路径验证。

**Architecture:** 体积光配置存储在 scene JSON 的 `scene_config.volumetric_lighting`，沿 `Scene -> RenderScene -> VisibleRenderFrame -> SceneRenderer` 快照传递。渲染实现集中在 `Function/Render/VolumetricLightingPass.*`，froxel volume 第一版编码为 2D atlas，compute pass 写 UAV，fullscreen raster pass 做 HDR composite / fallback，接在 SkyBackground 后、Bloom 前。

**Tech Stack:** C++17、AshEngine Function/Scene、Function/Render、RenderGraph raster/compute pass、HLSL、DXC、Vulkan、DX12、Premake/MSBuild、`Sandbox.exe --engine-self-test`。

---

## 文件结构

- Create: `project/src/engine/Function/Render/VolumetricLightingConfig.h`
  定义 quality、debug view、配置结构和 parse/sanitize API。
- Create: `project/src/engine/Function/Render/VolumetricLightingConfig.cpp`
  实现默认值、字符串解析、数值 clamp。
- Modify: `project/src/engine/Function/Scene/SceneConfig.h`
  把 `VolumetricLightingConfig` 加入 `SceneRenderConfig`。
- Modify: `project/src/engine/Function/Scene/SceneConfig.cpp`
  将 volumetric config 纳入默认值和 equality。
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
  读取 / 写出 `scene_config.volumetric_lighting`，并在 `Scene::set_render_config()` 中 sanitize。
- Modify: `project/src/engine/Function/Render/RenderScene.h`
  如现有 `VisibleRenderFrame` 已包含整个 `SceneRenderConfig`，只需确保新字段自然传递。
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
  使用现有 render config snapshot 路径，无需新增单独字段。
- Create: `project/src/engine/Function/Render/VolumetricLightingPass.h`
  声明 pass outputs、public lifecycle 和 `add_passes()`。
- Create: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`
  创建 programs / samplers / buffers，声明 RenderGraph passes，维护 history。
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
  增加 volumetric debug / output refs，便于 `SceneRenderer` 注册 debug item。
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
  持有 `VolumetricLightingPass m_volumetric_lighting_pass`。
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
  初始化 / shutdown pass，并插入到 SkyBackground 后、Bloom 前。
- Create: `project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli`
  共享 froxel atlas、phase、light attenuation、fullscreen helper。
- Create: `project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl`
  compute 写 density atlas。
- Create: `project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl`
  compute 注入 directional / point / spot scattering。
- Create: `project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl`
  compute 做 history blend 和 validity。
- Create: `project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl`
  compute 将 atlas 积分成 screen-space lighting。
- Create: `project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl`
  fullscreen raster 合成 HDR。
- Create: `project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl`
  fullscreen raster fallback，生成 screen-space shafts。
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
  增加配置、JSON、snapshot、source contract、RenderGraph 顺序测试。
- Modify: `product/assets/scenes/Sandbox.scene.json`
  在标准 scene 中显式开启 Low/Medium 体积光验证路径。
- Modify: `README.md`
  更新当前渲染能力和标准 scene 描述。
- Modify: `docs/EngineDeveloperGuide.md`
  更新 scene render config、deferred path、debug/validation/self-test 说明。

---

### Task 1: VolumetricLightingConfig 合同

**Files:**
- Create: `project/src/engine/Function/Render/VolumetricLightingConfig.h`
- Create: `project/src/engine/Function/Render/VolumetricLightingConfig.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 写失败 self-test**

在 `project/src/engine/Base/EngineSelfTests.cpp` 的 render config include 区加入：

```cpp
#include "Function/Render/VolumetricLightingConfig.h"
```

在 `test_bloom_config_defaults_and_sanitization()` 后添加：

```cpp
		auto test_volumetric_lighting_config_defaults_and_sanitization() -> bool
		{
			VolumetricLightingConfig defaults = make_default_volumetric_lighting_config();
			if (defaults.enabled ||
				defaults.quality != VolumetricLightingQuality::Medium ||
				defaults.froxel_resolution_scale != 0.5f ||
				defaults.froxel_depth_slices != 64u ||
				defaults.max_lights != 64u ||
				defaults.density != 0.02f ||
				defaults.scattering_intensity != 1.0f ||
				defaults.extinction_scale != 1.0f ||
				defaults.anisotropy != 0.35f ||
				!defaults.history ||
				defaults.history_blend != 0.9f ||
				defaults.screen_space_fallback ||
				defaults.debug_view != VolumetricLightingDebugView::Off)
			{
				return report_self_test_failure("VolumetricLighting config", "default config does not match design contract");
			}

			VolumetricLightingQuality quality = VolumetricLightingQuality::Low;
			VolumetricLightingDebugView debug_view = VolumetricLightingDebugView::Off;
			if (!try_parse_volumetric_lighting_quality("Epic", quality) || quality != VolumetricLightingQuality::Epic)
			{
				return report_self_test_failure("VolumetricLighting config", "failed to parse Epic quality");
			}
			if (!try_parse_volumetric_lighting_debug_view("IntegratedLighting", debug_view) ||
				debug_view != VolumetricLightingDebugView::IntegratedLighting)
			{
				return report_self_test_failure("VolumetricLighting config", "failed to parse IntegratedLighting debug view");
			}

			VolumetricLightingConfig invalid = defaults;
			invalid.enabled = true;
			invalid.froxel_resolution_scale = 2.0f;
			invalid.froxel_depth_slices = 4096u;
			invalid.max_lights = 10000u;
			invalid.density = -4.0f;
			invalid.scattering_intensity = -8.0f;
			invalid.extinction_scale = -2.0f;
			invalid.anisotropy = 4.0f;
			invalid.history_blend = 1.0f;

			const VolumetricLightingConfig sanitized =
				sanitize_volumetric_lighting_config(invalid, defaults);
			const bool sanitized_ok =
				sanitized.enabled &&
				sanitized.froxel_resolution_scale == 1.0f &&
				sanitized.froxel_depth_slices == 128u &&
				sanitized.max_lights == 256u &&
				sanitized.density == 0.0f &&
				sanitized.scattering_intensity == 0.0f &&
				sanitized.extinction_scale == 0.0f &&
				sanitized.anisotropy == 0.95f &&
				sanitized.history_blend == 0.98f;
			if (!sanitized_ok)
			{
				return report_self_test_failure("VolumetricLighting config", "sanitize did not clamp fields as expected");
			}

			return true;
		}
```

在 `run_engine_base_self_tests()` 中 `test_bloom_config_defaults_and_sanitization()` 后注册：

```cpp
		all_passed = test_volumetric_lighting_config_defaults_and_sanitization() && all_passed;
```

- [ ] **Step 2: 运行 self-test 确认失败**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: 构建前会因 `VolumetricLightingConfig.h` 不存在而编译失败；若旧二进制被直接运行，后续 build 步骤仍会暴露缺失 include。

- [ ] **Step 3: 创建配置头文件**

Create `project/src/engine/Function/Render/VolumetricLightingConfig.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include <cstdint>
#include <string_view>

namespace AshEngine
{
	enum class VolumetricLightingQuality : uint8_t
	{
		Low = 0,
		Medium,
		High,
		Epic
	};

	enum class VolumetricLightingDebugView : uint8_t
	{
		Off = 0,
		Density,
		Scattering,
		IntegratedLighting,
		HistoryValidity,
		CompositeHDR,
		ScreenSpaceMask,
		ScreenSpaceFinal
	};

	struct ASH_API VolumetricLightingConfig
	{
		bool enabled = false;
		VolumetricLightingQuality quality = VolumetricLightingQuality::Medium;
		float froxel_resolution_scale = 0.5f;
		uint32_t froxel_depth_slices = 64u;
		uint32_t max_lights = 64u;
		float density = 0.02f;
		float scattering_intensity = 1.0f;
		float extinction_scale = 1.0f;
		float anisotropy = 0.35f;
		bool history = true;
		float history_blend = 0.9f;
		bool screen_space_fallback = false;
		VolumetricLightingDebugView debug_view = VolumetricLightingDebugView::Off;
	};

	ASH_API const char* volumetric_lighting_quality_name(VolumetricLightingQuality quality);
	ASH_API const char* volumetric_lighting_debug_view_name(VolumetricLightingDebugView view);
	ASH_API bool try_parse_volumetric_lighting_quality(std::string_view value, VolumetricLightingQuality& out_quality);
	ASH_API bool try_parse_volumetric_lighting_debug_view(std::string_view value, VolumetricLightingDebugView& out_view);
	ASH_API VolumetricLightingConfig sanitize_volumetric_lighting_config(
		const VolumetricLightingConfig& config,
		const VolumetricLightingConfig& fallback);
	ASH_API VolumetricLightingConfig make_default_volumetric_lighting_config();
}
```

- [ ] **Step 4: 创建配置实现**

Create `project/src/engine/Function/Render/VolumetricLightingConfig.cpp`:

```cpp
#include "Function/Render/VolumetricLightingConfig.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_volumetric_token(std::string value) -> std::string
		{
			std::string token{};
			token.reserve(value.size());
			for (char ch : value)
			{
				if (ch == '_' || ch == '-' || ch == ' ')
				{
					continue;
				}
				token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			}
			return token;
		}
	}

	const char* volumetric_lighting_quality_name(VolumetricLightingQuality quality)
	{
		switch (quality)
		{
		case VolumetricLightingQuality::Low:
			return "Low";
		case VolumetricLightingQuality::High:
			return "High";
		case VolumetricLightingQuality::Epic:
			return "Epic";
		case VolumetricLightingQuality::Medium:
		default:
			return "Medium";
		}
	}

	const char* volumetric_lighting_debug_view_name(VolumetricLightingDebugView view)
	{
		switch (view)
		{
		case VolumetricLightingDebugView::Density:
			return "Density";
		case VolumetricLightingDebugView::Scattering:
			return "Scattering";
		case VolumetricLightingDebugView::IntegratedLighting:
			return "IntegratedLighting";
		case VolumetricLightingDebugView::HistoryValidity:
			return "HistoryValidity";
		case VolumetricLightingDebugView::CompositeHDR:
			return "CompositeHDR";
		case VolumetricLightingDebugView::ScreenSpaceMask:
			return "ScreenSpaceMask";
		case VolumetricLightingDebugView::ScreenSpaceFinal:
			return "ScreenSpaceFinal";
		case VolumetricLightingDebugView::Off:
		default:
			return "Off";
		}
	}

	bool try_parse_volumetric_lighting_quality(std::string_view value, VolumetricLightingQuality& out_quality)
	{
		const std::string token = normalize_volumetric_token(std::string(value));
		if (token == "low")
		{
			out_quality = VolumetricLightingQuality::Low;
			return true;
		}
		if (token == "medium")
		{
			out_quality = VolumetricLightingQuality::Medium;
			return true;
		}
		if (token == "high")
		{
			out_quality = VolumetricLightingQuality::High;
			return true;
		}
		if (token == "epic")
		{
			out_quality = VolumetricLightingQuality::Epic;
			return true;
		}
		return false;
	}

	bool try_parse_volumetric_lighting_debug_view(std::string_view value, VolumetricLightingDebugView& out_view)
	{
		const std::string token = normalize_volumetric_token(std::string(value));
		if (token == "off")
		{
			out_view = VolumetricLightingDebugView::Off;
			return true;
		}
		if (token == "density")
		{
			out_view = VolumetricLightingDebugView::Density;
			return true;
		}
		if (token == "scattering")
		{
			out_view = VolumetricLightingDebugView::Scattering;
			return true;
		}
		if (token == "integratedlighting")
		{
			out_view = VolumetricLightingDebugView::IntegratedLighting;
			return true;
		}
		if (token == "historyvalidity")
		{
			out_view = VolumetricLightingDebugView::HistoryValidity;
			return true;
		}
		if (token == "compositehdr")
		{
			out_view = VolumetricLightingDebugView::CompositeHDR;
			return true;
		}
		if (token == "screenspacemask")
		{
			out_view = VolumetricLightingDebugView::ScreenSpaceMask;
			return true;
		}
		if (token == "screenspacefinal")
		{
			out_view = VolumetricLightingDebugView::ScreenSpaceFinal;
			return true;
		}
		return false;
	}

	VolumetricLightingConfig sanitize_volumetric_lighting_config(
		const VolumetricLightingConfig& config,
		const VolumetricLightingConfig& fallback)
	{
		(void)fallback;
		VolumetricLightingConfig result = config;
		result.froxel_resolution_scale = std::clamp(result.froxel_resolution_scale, 0.25f, 1.0f);
		result.froxel_depth_slices = std::clamp<uint32_t>(result.froxel_depth_slices, 16u, 128u);
		result.max_lights = std::clamp<uint32_t>(result.max_lights, 1u, 256u);
		result.density = std::clamp(result.density, 0.0f, 2.0f);
		result.scattering_intensity = std::clamp(result.scattering_intensity, 0.0f, 16.0f);
		result.extinction_scale = std::clamp(result.extinction_scale, 0.0f, 16.0f);
		result.anisotropy = std::clamp(result.anisotropy, -0.95f, 0.95f);
		result.history_blend = std::clamp(result.history_blend, 0.0f, 0.98f);
		return result;
	}

	VolumetricLightingConfig make_default_volumetric_lighting_config()
	{
		return {};
	}
}
```

- [ ] **Step 5: 构建并确认 self-test 通过新增配置测试**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds；self-test exits `0` or fails only on later tasks not yet implemented if steps were batched.

- [ ] **Step 6: 提交 Task 1**

```powershell
git add project/src/engine/Function/Render/VolumetricLightingConfig.h project/src/engine/Function/Render/VolumetricLightingConfig.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add volumetric lighting config"
```

---

### Task 2: SceneRenderConfig 与 scene JSON

**Files:**
- Modify: `project/src/engine/Function/Scene/SceneConfig.h`
- Modify: `project/src/engine/Function/Scene/SceneConfig.cpp`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 写失败的 render config snapshot 测试**

在 `test_render_scene_copies_scene_render_config_to_visible_frame()` 中 bloom 设置后加入：

```cpp
			config.volumetric_lighting.enabled = true;
			config.volumetric_lighting.quality = VolumetricLightingQuality::High;
			config.volumetric_lighting.froxel_resolution_scale = 0.75f;
			config.volumetric_lighting.froxel_depth_slices = 96u;
			config.volumetric_lighting.max_lights = 48u;
			config.volumetric_lighting.debug_view = VolumetricLightingDebugView::CompositeHDR;
```

在 `const bool ok =` 条件末尾加入：

```cpp
				full_frame.render_config.volumetric_lighting.enabled &&
				full_frame.render_config.volumetric_lighting.quality == VolumetricLightingQuality::High &&
				full_frame.render_config.volumetric_lighting.froxel_resolution_scale == 0.75f &&
				full_frame.render_config.volumetric_lighting.debug_view == VolumetricLightingDebugView::CompositeHDR;
```

- [ ] **Step 2: 写失败的 JSON round-trip 测试**

在 `test_scene_render_config_json_defaults_and_round_trip()` 的 configured scene JSON 中，`"bloom"` object 后增加逗号和 `volumetric_lighting` object：

```cpp
					"    },\n"
					"    \"volumetric_lighting\": {\n"
					"      \"enabled\": true,\n"
					"      \"quality\": \"Epic\",\n"
					"      \"froxel_resolution_scale\": 2.0,\n"
					"      \"froxel_depth_slices\": 4096,\n"
					"      \"max_lights\": 2048,\n"
					"      \"density\": -4.0,\n"
					"      \"scattering_intensity\": 32.0,\n"
					"      \"extinction_scale\": 20.0,\n"
					"      \"anisotropy\": 4.0,\n"
					"      \"history\": false,\n"
					"      \"history_blend\": 1.0,\n"
					"      \"screen_space_fallback\": true,\n"
					"      \"debug_view\": \"ScreenSpaceFinal\"\n"
					"    }\n"
```

在 `parsed_ok` 条件中加入：

```cpp
				loaded.volumetric_lighting.enabled &&
				loaded.volumetric_lighting.quality == VolumetricLightingQuality::Epic &&
				loaded.volumetric_lighting.froxel_resolution_scale == 1.0f &&
				loaded.volumetric_lighting.froxel_depth_slices == 128u &&
				loaded.volumetric_lighting.max_lights == 256u &&
				loaded.volumetric_lighting.density == 0.0f &&
				loaded.volumetric_lighting.scattering_intensity == 16.0f &&
				loaded.volumetric_lighting.extinction_scale == 16.0f &&
				loaded.volumetric_lighting.anisotropy == 0.95f &&
				!loaded.volumetric_lighting.history &&
				loaded.volumetric_lighting.history_blend == 0.98f &&
				loaded.volumetric_lighting.screen_space_fallback &&
				loaded.volumetric_lighting.debug_view == VolumetricLightingDebugView::ScreenSpaceFinal;
```

- [ ] **Step 3: 更新 Engine.ini authority 测试**

在 `test_engine_ini_excludes_scene_render_config_sections()` 中把 section 检查扩展为：

```cpp
			if (engine_ini_source.find("[AmbientOcclusion]") != std::string::npos ||
				engine_ini_source.find("[DirectionalShadows]") != std::string::npos ||
				engine_ini_source.find("[Bloom]") != std::string::npos ||
				engine_ini_source.find("[VolumetricLighting]") != std::string::npos)
```

- [ ] **Step 4: 运行测试确认失败**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: compile fails because `SceneRenderConfig::volumetric_lighting` does not exist.

- [ ] **Step 5: 修改 SceneConfig 头文件**

In `project/src/engine/Function/Scene/SceneConfig.h` add:

```cpp
#include "Function/Render/VolumetricLightingConfig.h"
```

In `SceneRenderConfig` add:

```cpp
		VolumetricLightingConfig volumetric_lighting{};
```

- [ ] **Step 6: 修改 SceneConfig 实现**

In `project/src/engine/Function/Scene/SceneConfig.cpp` add helper:

```cpp
		auto volumetric_lighting_config_equal(
			const VolumetricLightingConfig& lhs,
			const VolumetricLightingConfig& rhs) -> bool
		{
			return lhs.enabled == rhs.enabled &&
				lhs.quality == rhs.quality &&
				lhs.froxel_resolution_scale == rhs.froxel_resolution_scale &&
				lhs.froxel_depth_slices == rhs.froxel_depth_slices &&
				lhs.max_lights == rhs.max_lights &&
				lhs.density == rhs.density &&
				lhs.scattering_intensity == rhs.scattering_intensity &&
				lhs.extinction_scale == rhs.extinction_scale &&
				lhs.anisotropy == rhs.anisotropy &&
				lhs.history == rhs.history &&
				lhs.history_blend == rhs.history_blend &&
				lhs.screen_space_fallback == rhs.screen_space_fallback &&
				lhs.debug_view == rhs.debug_view;
		}
```

In `make_default_scene_render_config()` add:

```cpp
		config.volumetric_lighting = make_default_volumetric_lighting_config();
```

In `scene_render_config_equal()` include:

```cpp
			volumetric_lighting_config_equal(lhs.volumetric_lighting, rhs.volumetric_lighting);
```

- [ ] **Step 7: 修改 Scene.cpp 反序列化**

In `deserialize_scene_render_config()` after bloom parsing block add:

```cpp
			if (const auto volumetric_it = scene_config.find("volumetric_lighting");
				volumetric_it != scene_config.end() && volumetric_it->is_object())
			{
				try_get_json_value(*volumetric_it, "enabled", config.volumetric_lighting.enabled);

				std::string quality{};
				if (try_get_json_value(*volumetric_it, "quality", quality))
				{
					VolumetricLightingQuality parsed = config.volumetric_lighting.quality;
					if (try_parse_volumetric_lighting_quality(quality, parsed))
					{
						config.volumetric_lighting.quality = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig volumetric_lighting.quality '{}' is invalid. Keeping default '{}'.",
							quality,
							volumetric_lighting_quality_name(config.volumetric_lighting.quality));
					}
				}

				try_get_json_value(*volumetric_it, "froxel_resolution_scale", config.volumetric_lighting.froxel_resolution_scale);
				try_get_json_value(*volumetric_it, "froxel_depth_slices", config.volumetric_lighting.froxel_depth_slices);
				try_get_json_value(*volumetric_it, "max_lights", config.volumetric_lighting.max_lights);
				try_get_json_value(*volumetric_it, "density", config.volumetric_lighting.density);
				try_get_json_value(*volumetric_it, "scattering_intensity", config.volumetric_lighting.scattering_intensity);
				try_get_json_value(*volumetric_it, "extinction_scale", config.volumetric_lighting.extinction_scale);
				try_get_json_value(*volumetric_it, "anisotropy", config.volumetric_lighting.anisotropy);
				try_get_json_value(*volumetric_it, "history", config.volumetric_lighting.history);
				try_get_json_value(*volumetric_it, "history_blend", config.volumetric_lighting.history_blend);
				try_get_json_value(*volumetric_it, "screen_space_fallback", config.volumetric_lighting.screen_space_fallback);

				std::string debug_view{};
				if (try_get_json_value(*volumetric_it, "debug_view", debug_view))
				{
					VolumetricLightingDebugView parsed = config.volumetric_lighting.debug_view;
					if (try_parse_volumetric_lighting_debug_view(debug_view, parsed))
					{
						config.volumetric_lighting.debug_view = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig volumetric_lighting.debug_view '{}' is invalid. Keeping default '{}'.",
							debug_view,
							volumetric_lighting_debug_view_name(config.volumetric_lighting.debug_view));
					}
				}

				config.volumetric_lighting = sanitize_volumetric_lighting_config(
					config.volumetric_lighting,
					make_default_volumetric_lighting_config());
			}
```

- [ ] **Step 8: 修改 Scene.cpp 序列化**

In `serialize_scene_render_config()` add a `volumetric_lighting` object after `bloom`:

```cpp
				{ "volumetric_lighting", json{
					{ "enabled", config.volumetric_lighting.enabled },
					{ "quality", volumetric_lighting_quality_name(config.volumetric_lighting.quality) },
					{ "froxel_resolution_scale", config.volumetric_lighting.froxel_resolution_scale },
					{ "froxel_depth_slices", config.volumetric_lighting.froxel_depth_slices },
					{ "max_lights", config.volumetric_lighting.max_lights },
					{ "density", config.volumetric_lighting.density },
					{ "scattering_intensity", config.volumetric_lighting.scattering_intensity },
					{ "extinction_scale", config.volumetric_lighting.extinction_scale },
					{ "anisotropy", config.volumetric_lighting.anisotropy },
					{ "history", config.volumetric_lighting.history },
					{ "history_blend", config.volumetric_lighting.history_blend },
					{ "screen_space_fallback", config.volumetric_lighting.screen_space_fallback },
					{ "debug_view", volumetric_lighting_debug_view_name(config.volumetric_lighting.debug_view) },
				} },
```

- [ ] **Step 9: 修改 Scene::set_render_config()**

In `Scene::set_render_config()` add:

```cpp
		sanitized_config.volumetric_lighting =
			sanitize_volumetric_lighting_config(config.volumetric_lighting, make_default_volumetric_lighting_config());
```

- [ ] **Step 10: 构建并运行 self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds；self-test exits `0`.

- [ ] **Step 11: 提交 Task 2**

```powershell
git add project/src/engine/Function/Scene/SceneConfig.h project/src/engine/Function/Scene/SceneConfig.cpp project/src/engine/Function/Scene/Scene.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add volumetric lighting scene config"
```

---

### Task 3: RenderGraph 骨架与 source contract

**Files:**
- Create: `project/src/engine/Function/Render/VolumetricLightingPass.h`
- Create: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`
- Create: `project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli`
- Create: `project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl`
- Create: `project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl`
- Create: `project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl`
- Create: `project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl`
- Create: `project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl`
- Create: `project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 写失败 source contract 测试**

Add after bloom source contract tests:

```cpp
		auto test_volumetric_lighting_shader_source_contract() -> bool
		{
			const bool common_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli",
				{ "AshVolumetricFullscreen", "AshVolumetricPhaseHG", "AshVolumetricAtlasUV", "AshRootConstants" });
			const bool density_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl",
				{ "CSMain", "SceneVolumetricDensity", "AshVolumetricConfig0" });
			const bool injection_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl",
				{ "CSMain", "SceneVolumetricDensity", "SceneVolumetricScattering", "SceneVolumetricLights" });
			const bool temporal_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl",
				{ "CSMain", "SceneVolumetricScattering", "SceneVolumetricScatteringHistory", "SceneVolumetricHistoryValidity" });
			const bool integrate_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl",
				{ "CSMain", "SceneVolumetricScatteringTemporal", "SceneVolumetricIntegratedLighting" });
			const bool composite_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl",
				{ "VSMain", "PSMain", "SceneHDRLinear", "SceneVolumetricIntegratedLighting" });
			const bool fallback_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl",
				{ "VSMain", "PSMain", "SceneDepth", "PSScreenSpaceLightShaftOutput" });

			return (common_ok && density_ok && injection_ok && temporal_ok && integrate_ok && composite_ok && fallback_ok) ||
				report_self_test_failure("VolumetricLighting shader source contract", "shader sources are missing required entry points or binding names");
		}

		auto test_volumetric_lighting_pass_source_contract() -> bool
		{
			const bool header_ok = file_contains_all(
				"project/src/engine/Function/Render/VolumetricLightingPass.h",
				{ "VolumetricLightingPassOutputs", "SceneVolumetricCompositeHDR", "add_passes" });
			const bool source_ok = file_contains_all(
				"project/src/engine/Function/Render/VolumetricLightingPass.cpp",
				{
					"SceneVolumetricDensityPass",
					"SceneVolumetricLightInjectionPass",
					"SceneVolumetricTemporalPass",
					"SceneVolumetricIntegratePass",
					"SceneVolumetricCompositePass",
					"SceneLightShaftScreenSpacePass",
					"RenderGraphAccess::ComputeUAV",
					"RenderGraphAccess::ComputeSRV",
					"ASH_PROFILE_SCOPE_NC"
				});

			return (header_ok && source_ok) ||
				report_self_test_failure("VolumetricLighting pass source contract", "pass source is missing graph or profiling contract");
		}
```

Register:

```cpp
		all_passed = test_volumetric_lighting_shader_source_contract() && all_passed;
		all_passed = test_volumetric_lighting_pass_source_contract() && all_passed;
```

- [ ] **Step 2: 构建确认失败**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: build succeeds if tests compile, but `Sandbox.exe --engine-self-test` fails because files do not exist.

- [ ] **Step 3: 创建 VolumetricLightingPass.h**

Create `project/src/engine/Function/Render/VolumetricLightingPass.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/VolumetricLightingConfig.h"
#include <memory>
#include <unordered_map>

namespace AshEngine
{
	class ComputeProgram;
	class GraphicsProgram;
	class Renderer;
	class RenderSampler;
	class StorageBuffer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	struct VolumetricLightingPassOutputs
	{
		RenderGraphTextureRef scene_hdr_linear{};
		RenderGraphTextureRef density{};
		RenderGraphTextureRef scattering{};
		RenderGraphTextureRef temporal_scattering{};
		RenderGraphTextureRef integrated_lighting{};
		RenderGraphTextureRef history_validity{};
		RenderGraphTextureRef composite_hdr{};
		RenderGraphTextureRef screen_space_mask{};
		RenderGraphTextureRef screen_space_final{};
	};

	class VolumetricLightingPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		VolumetricLightingPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context,
			const VolumetricLightingConfig& config);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<ComputeProgram> m_density_program = nullptr;
		std::unique_ptr<ComputeProgram> m_light_injection_program = nullptr;
		std::unique_ptr<ComputeProgram> m_temporal_program = nullptr;
		std::unique_ptr<ComputeProgram> m_integrate_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_composite_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_screen_space_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
		std::shared_ptr<StorageBuffer> m_light_buffer = nullptr;
	};
}
```

- [ ] **Step 4: 创建 HLSL common**

Create `project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli`:

```hlsl
#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct VSFullscreenOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSFullscreenOutput AshVolumetricFullscreen(uint vertex_id)
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
	float4x4 AshInvViewProjection;
	float4x4 AshPrevViewProjection;
	float4 AshVolumetricAtlasSize;
	float4 AshVolumetricConfig0;
	float4 AshVolumetricConfig1;
	float4 AshCameraPositionAndFlags;
	float4 AshScreenLightPositionAndParams;
};

float2 AshVolumetricAtlasUV(uint2 pixel, uint slice, uint slices_per_row, float2 atlas_inv_size)
{
	uint tile_x = slice % slices_per_row;
	uint tile_y = slice / max(slices_per_row, 1u);
	uint2 atlas_pixel = uint2(tile_x * AshVolumetricAtlasSize.x + pixel.x, tile_y * AshVolumetricAtlasSize.y + pixel.y);
	return (float2(atlas_pixel) + 0.5) * atlas_inv_size;
}

float AshVolumetricPhaseHG(float cos_theta, float g)
{
	float g2 = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4);
	return (1.0 - g2) / max(12.56637061 * denom * sqrt(denom), 1e-4);
}
```

- [ ] **Step 5: 创建 compute/raster shader skeletons**

Create `project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl`:

```hlsl
#include "VolumetricLightingCommon.hlsli"

RWTexture2D<float4> SceneVolumetricDensity : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}
	float density = max(AshVolumetricConfig0.x, 0.0);
	float extinction = density * max(AshVolumetricConfig0.y, 0.0);
	SceneVolumetricDensity[dispatch_id.xy] = float4(density, extinction, 0.0, 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl`:

```hlsl
#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricDensity : register(t0);
struct VolumetricLightData
{
	float4 position_range;
	float4 direction_type;
	float4 color_intensity;
	float4 cone_shadow;
};

StructuredBuffer<VolumetricLightData> SceneVolumetricLights : register(t1);
RWTexture2D<float4> SceneVolumetricScattering : register(u0);
SamplerState ScenePointClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}
	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), 1.0.xx);
	float density = SceneVolumetricDensity.SampleLevel(ScenePointClampSampler, uv, 0).r;
	float light_count = AshVolumetricConfig1.x;
	float3 scattering = density * AshVolumetricConfig0.z;
	scattering *= saturate(light_count / max(AshVolumetricConfig0.w, 1.0));
	SceneVolumetricScattering[dispatch_id.xy] = float4(scattering, density);
}
```

Create `project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl`:

```hlsl
#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricScattering : register(t0);
Texture2D<float4> SceneVolumetricScatteringHistory : register(t1);
RWTexture2D<float4> SceneVolumetricScatteringTemporal : register(u0);
RWTexture2D<float4> SceneVolumetricHistoryValidity : register(u1);
SamplerState SceneLinearClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}
	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), 1.0.xx);
	float4 current_value = SceneVolumetricScattering.SampleLevel(SceneLinearClampSampler, uv, 0);
	float4 history_value = SceneVolumetricScatteringHistory.SampleLevel(SceneLinearClampSampler, uv, 0);
	float blend = saturate(AshVolumetricConfig1.y);
	float4 filtered = lerp(current_value, history_value, blend);
	SceneVolumetricScatteringTemporal[dispatch_id.xy] = filtered;
	SceneVolumetricHistoryValidity[dispatch_id.xy] = float4(blend.xxx, 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl`:

```hlsl
#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricScatteringTemporal : register(t0);
RWTexture2D<float4> SceneVolumetricIntegratedLighting : register(u0);
SamplerState SceneLinearClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.x;
	uint height = (uint)AshVolumetricAtlasSize.y;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}
	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), 1.0.xx);
	float3 lighting = SceneVolumetricScatteringTemporal.SampleLevel(SceneLinearClampSampler, uv, 0).rgb;
	SceneVolumetricIntegratedLighting[dispatch_id.xy] = float4(lighting, 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl`:

```hlsl
#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
Texture2D<float4> SceneVolumetricIntegratedLighting : register(t1);
SamplerState SceneLinearClampSampler : register(s0);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshVolumetricFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float3 hdr = SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb;
	float3 volumetric = SceneVolumetricIntegratedLighting.Sample(SceneLinearClampSampler, input.uv).rgb;
	return float4(hdr + volumetric, 1.0);
}
```

Create `project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl`:

```hlsl
#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
Texture2D<float> SceneDepth : register(t1);
SamplerState SceneLinearClampSampler : register(s0);
SamplerState ScenePointClampSampler : register(s1);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshVolumetricFullscreen(vertex_id);
}

struct PSScreenSpaceLightShaftOutput
{
	float4 screen_space_mask : SV_Target0;
	float4 screen_space_final : SV_Target1;
};

PSScreenSpaceLightShaftOutput PSMain(VSFullscreenOutput input)
{
	float3 hdr = SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb;
	float2 light_uv = AshScreenLightPositionAndParams.xy;
	float2 delta = light_uv - input.uv;
	float shaft = 0.0;
	float weight = 1.0;
	for (uint index = 0; index < 16u; ++index)
	{
		float t = (float(index) + 0.5) / 16.0;
		float2 uv = saturate(input.uv + delta * t);
		float depth = SceneDepth.Sample(ScenePointClampSampler, uv);
		float visible = depth > 0.000001 ? 1.0 : 0.0;
		shaft += visible * weight;
		weight *= 0.92;
	}
	shaft = shaft / 16.0 * AshScreenLightPositionAndParams.z;
	PSScreenSpaceLightShaftOutput output;
	output.screen_space_mask = float4(shaft.xxx, 1.0);
	output.screen_space_final = float4(hdr + shaft.xxx, 1.0);
	return output;
}
```

- [ ] **Step 6: 创建 VolumetricLightingPass.cpp skeleton**

Create `project/src/engine/Function/Render/VolumetricLightingPass.cpp` with the skeleton from Task 4 and Task 5 as the implementation target. At this task, include shader paths, program creation, sampler creation, and disabled return:

```cpp
#include "Function/Render/VolumetricLightingPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_density_shader_path = "project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl";
		static constexpr const char* k_light_injection_shader_path = "project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl";
		static constexpr const char* k_temporal_shader_path = "project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl";
		static constexpr const char* k_integrate_shader_path = "project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl";
		static constexpr const char* k_composite_shader_path = "project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl";
		static constexpr const char* k_screen_space_shader_path = "project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl";
		static constexpr const char* k_common_shader_path = "project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli";
		static constexpr RenderColorValue k_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct VolumetricRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::mat4 prev_view_projection{ 1.0f };
			glm::vec4 atlas_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 config0{ 0.02f, 1.0f, 1.0f, 64.0f };
			glm::vec4 config1{ 0.0f, 0.9f, 0.0f, 0.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 screen_light_position_and_params{ 0.5f, 0.5f, 1.0f, 0.0f };
		};

		static_assert(sizeof(VolumetricRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto build_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_common_shader_path);
			return hash_value;
		}

		auto make_compute_desc(const char* shader_path, const char* name) -> ComputeProgramDesc
		{
			ComputeProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.compute_entry = "CSMain";
			desc.source_hash = build_source_hash(shader_path);
			desc.name = name;
			return desc;
		}

		auto make_graphics_desc(const char* shader_path, const char* name) -> GraphicsProgramDesc
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::None;
			state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			state.depth_test = false;
			state.depth_write = false;
			state.blend_mode = RenderBlendMode::Opaque;

			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}
	}

	bool VolumetricLightingPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("VolumetricLightingPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void VolumetricLightingPass::shutdown()
	{
		m_screen_space_program.reset();
		m_composite_program.reset();
		m_integrate_program.reset();
		m_temporal_program.reset();
		m_light_injection_program.reset();
		m_density_program.reset();
		m_light_buffer.reset();
		m_linear_clamp_sampler.reset();
		m_point_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool VolumetricLightingPass::create_resources(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		RenderSamplerDesc point_desc{};
		point_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		point_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		point_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		point_desc.min_filter = RenderSamplerFilter::Nearest;
		point_desc.mag_filter = RenderSamplerFilter::Nearest;
		point_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(point_desc, "SceneVolumetricPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		RenderSamplerDesc linear_desc = point_desc;
		linear_desc.min_filter = RenderSamplerFilter::Linear;
		linear_desc.mag_filter = RenderSamplerFilter::Linear;
		linear_desc.mip_filter = RenderSamplerFilter::Linear;
		m_linear_clamp_sampler = renderer.create_sampler(linear_desc, "SceneVolumetricLinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool VolumetricLightingPass::create_programs(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_density_program = renderer.create_compute_program(make_compute_desc(k_density_shader_path, "SceneVolumetricDensity"));
		m_light_injection_program = renderer.create_compute_program(make_compute_desc(k_light_injection_shader_path, "SceneVolumetricLightInjection"));
		m_temporal_program = renderer.create_compute_program(make_compute_desc(k_temporal_shader_path, "SceneVolumetricTemporal"));
		m_integrate_program = renderer.create_compute_program(make_compute_desc(k_integrate_shader_path, "SceneVolumetricIntegrate"));
		m_composite_program = renderer.create_graphics_program(make_graphics_desc(k_composite_shader_path, "SceneVolumetricComposite"));
		m_screen_space_program = renderer.create_graphics_program(make_graphics_desc(k_screen_space_shader_path, "SceneLightShaftScreenSpace"));
		ASH_PROCESS_ERROR(m_density_program && m_light_injection_program && m_temporal_program &&
			m_integrate_program && m_composite_program && m_screen_space_program);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	VolumetricLightingPassOutputs VolumetricLightingPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef scene_hdr_linear,
		const SceneRenderViewContext& view_context,
		const VolumetricLightingConfig& config)
	{
		ASH_PROFILE_SCOPE_NC("VolumetricLightingPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(VolumetricLightingPassOutputs, outputs, VolumetricLightingPassOutputs{}, VolumetricLightingPassOutputs{});
		(void)graph;
		(void)frame;
		(void)deferred_resources;
		(void)view_context;
		outputs.scene_hdr_linear = scene_hdr_linear;
		const VolumetricLightingConfig sanitized =
			sanitize_volumetric_lighting_config(config, make_default_volumetric_lighting_config());
		ASH_PROCESS_SUCCESS(!sanitized.enabled || sanitized.scattering_intensity <= 0.0f);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(scene_hdr_linear);
		ASH_PROCESS_GUARD_RETURN_END(outputs, VolumetricLightingPassOutputs{});
	}
}
```

- [ ] **Step 7: 运行 self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: source contract tests pass. Runtime visual behavior unchanged because pass is not integrated into SceneRenderer yet.

- [ ] **Step 8: 提交 Task 3**

```powershell
git add project/src/engine/Function/Render/VolumetricLightingPass.h project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add volumetric lighting pass skeleton"
```

---

### Task 4: VolumetricLightingPass RenderGraph 主路径

**Files:**
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.h`
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 写 headless graph contract 测试**

Add helper test after pass source contract:

If `project/src/engine/Base/EngineSelfTests.cpp` does not already include `<algorithm>`, add it with the other standard-library includes because this test uses `std::any_of`.

```cpp
		auto test_volumetric_lighting_pass_adds_expected_graph_chain_for_tests() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("VolumetricLightingGraphSelfTest");
			RenderTargetDesc hdr_desc{};
			hdr_desc.width = 128;
			hdr_desc.height = 64;
			hdr_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			hdr_desc.shader_resource = true;
			RenderTargetDesc depth_desc = hdr_desc;
			depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			RenderGraphTextureRef hdr = graph.register_external_texture_desc_for_tests(hdr_desc, "SceneHDRLinear");
			RenderGraphTextureRef depth = graph.register_external_texture_desc_for_tests(depth_desc, "SceneDeferredDepth");

			VolumetricLightingConfig config = make_default_volumetric_lighting_config();
			config.enabled = true;
			config.history = true;
			config.screen_space_fallback = false;

			const bool ok = VolumetricLightingPass::add_passes_for_tests(graph, hdr, depth, 128, 64, config);
			if (!ok)
			{
				return report_self_test_failure("VolumetricLighting graph", "test graph helper failed");
			}

			const std::vector<RenderGraphPassNode>& passes = graph.get_passes_for_tests();
			const std::vector<RenderGraphTextureNode>& textures = graph.get_textures_for_tests();
			const auto has_pass = [&passes](const char* name) -> bool
			{
				return std::any_of(passes.begin(), passes.end(), [name](const RenderGraphPassNode& pass)
				{
					return pass.name == name;
				});
			};
			const auto has_texture = [&textures](const char* name) -> bool
			{
				return std::any_of(textures.begin(), textures.end(), [name](const RenderGraphTextureNode& texture)
				{
					return texture.name == name;
				});
			};

			const bool graph_ok =
				has_pass("SceneVolumetricDensityPass") &&
				has_pass("SceneVolumetricLightInjectionPass") &&
				has_pass("SceneVolumetricTemporalPass") &&
				has_pass("SceneVolumetricIntegratePass") &&
				has_pass("SceneVolumetricCompositePass") &&
				has_texture("SceneVolumetricDensity") &&
				has_texture("SceneVolumetricScattering") &&
				has_texture("SceneVolumetricScatteringTemporal") &&
				has_texture("SceneVolumetricIntegratedLighting") &&
				has_texture("SceneVolumetricCompositeHDR");
			return graph_ok ||
				report_self_test_failure("VolumetricLighting graph", "graph chain is missing expected passes or textures");
		}
```

Register in `run_engine_base_self_tests()`:

```cpp
		all_passed = test_volumetric_lighting_pass_adds_expected_graph_chain_for_tests() && all_passed;
```

- [ ] **Step 2: 暴露 headless graph helper**

In `VolumetricLightingPass.h` public section add:

```cpp
		static bool add_passes_for_tests(
			RenderGraphBuilder& graph,
			RenderGraphTextureRef scene_hdr_linear,
			RenderGraphTextureRef scene_depth,
			uint32_t output_width,
			uint32_t output_height,
			const VolumetricLightingConfig& config);
```

- [ ] **Step 3: 实现 atlas desc/helper**

In `VolumetricLightingPass.cpp` anonymous namespace add:

```cpp
		auto to_graph_dimension(uint32_t value) -> uint16_t
		{
			return static_cast<uint16_t>(std::clamp<uint32_t>(value, 1u, UINT16_MAX));
		}

		auto make_color_texture_desc(uint32_t width, uint32_t height, bool unordered_access) -> RenderGraphTextureDesc
		{
			RenderGraphTextureDesc desc{};
			desc.width = to_graph_dimension(width);
			desc.height = to_graph_dimension(height);
			desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			desc.shader_resource = true;
			desc.unordered_access = unordered_access;
			desc.use_optimized_clear_value = true;
			desc.optimized_clear_color = k_clear_color;
			return desc;
		}

		struct VolumetricAtlasDesc
		{
			uint32_t tile_width = 1;
			uint32_t tile_height = 1;
			uint32_t depth_slices = 1;
			uint32_t slices_per_row = 1;
			uint32_t atlas_width = 1;
			uint32_t atlas_height = 1;
		};

		auto make_atlas_desc(uint32_t output_width, uint32_t output_height, const VolumetricLightingConfig& config) -> VolumetricAtlasDesc
		{
			VolumetricAtlasDesc desc{};
			desc.tile_width = std::max<uint32_t>(static_cast<uint32_t>(static_cast<float>(output_width) * config.froxel_resolution_scale), 1u);
			desc.tile_height = std::max<uint32_t>(static_cast<uint32_t>(static_cast<float>(output_height) * config.froxel_resolution_scale), 1u);
			desc.depth_slices = std::max<uint32_t>(config.froxel_depth_slices, 1u);
			desc.slices_per_row = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(desc.depth_slices))));
			desc.atlas_width = desc.tile_width * desc.slices_per_row;
			desc.atlas_height = desc.tile_height * ((desc.depth_slices + desc.slices_per_row - 1u) / desc.slices_per_row);
			return desc;
		}
```

- [ ] **Step 4: 实现 add_passes_for_tests()**

In `VolumetricLightingPass.cpp` add:

```cpp
	bool VolumetricLightingPass::add_passes_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef scene_hdr_linear,
		RenderGraphTextureRef scene_depth,
		uint32_t output_width,
		uint32_t output_height,
		const VolumetricLightingConfig& config)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const VolumetricLightingConfig sanitized =
			sanitize_volumetric_lighting_config(config, make_default_volumetric_lighting_config());
		ASH_PROCESS_ERROR(sanitized.enabled);
		ASH_PROCESS_ERROR(scene_hdr_linear && scene_depth);

		const VolumetricAtlasDesc atlas = make_atlas_desc(output_width, output_height, sanitized);
		RenderGraphTextureRef density = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricDensity");
		RenderGraphTextureRef scattering = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricScattering");
		RenderGraphTextureRef temporal = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricScatteringTemporal");
		RenderGraphTextureRef validity = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricHistoryValidity");
		RenderGraphTextureRef integrated = graph.create_texture(
			make_color_texture_desc(output_width, output_height, true),
			"SceneVolumetricIntegratedLighting");
		RenderGraphTextureRef composite = graph.create_texture(
			make_color_texture_desc(output_width, output_height, false),
			"SceneVolumetricCompositeHDR");
		ASH_PROCESS_ERROR(density && scattering && temporal && validity && integrated && composite);

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricDensityPass",
			RenderGraphPassFlags::None,
			[density](RenderGraphComputePassBuilder& pass)
			{
				pass.write_texture(density, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricLightInjectionPass",
			RenderGraphPassFlags::None,
			[density, scattering](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(density, RenderGraphAccess::ComputeSRV);
				pass.write_texture(scattering, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricTemporalPass",
			RenderGraphPassFlags::None,
			[scattering, temporal, validity](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(scattering, RenderGraphAccess::ComputeSRV);
				pass.write_texture(temporal, RenderGraphAccess::ComputeUAV);
				pass.write_texture(validity, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricIntegratePass",
			RenderGraphPassFlags::None,
			[temporal, integrated](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(temporal, RenderGraphAccess::ComputeSRV);
				pass.write_texture(integrated, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneVolumetricCompositePass",
			RenderGraphPassFlags::None,
			[scene_hdr_linear, integrated, composite](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(integrated, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, composite, RenderLoadAction::Clear, k_clear_color);
			},
			[](RenderGraphRasterContext&) { return true; }));

		graph.extract_texture(composite);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
```

- [ ] **Step 5: 运行 self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test exits `0`; graph helper proves pass names and texture names exist.

- [ ] **Step 6: 提交 Task 4**

```powershell
git add project/src/engine/Function/Render/VolumetricLightingPass.h project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add volumetric lighting graph contract"
```

---

### Task 5: 运行时 compute pass 与 HDR composite

**Files:**
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`
- Modify: `project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl`
- Modify: `project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl`
- Modify: `project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl`
- Modify: `project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl`

- [ ] **Step 1: 写运行时 contract 测试**

Extend `test_volumetric_lighting_pass_source_contract()` source token list with:

```cpp
					"m_density_program->set_rw_texture(\"SceneVolumetricDensity\"",
					"m_light_injection_program->set_texture(\"SceneVolumetricDensity\"",
					"m_light_injection_program->set_storage_buffer(\"SceneVolumetricLights\"",
					"m_integrate_program->set_rw_texture(\"SceneVolumetricIntegratedLighting\"",
					"m_composite_program->set_texture(\"SceneHDRLinear\"",
					"context.dispatch",
					"context.draw"
```

- [ ] **Step 2: 运行 self-test 确认失败**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test fails with `VolumetricLighting pass source contract`.

- [ ] **Step 3: 添加 light buffer 数据结构**

In `VolumetricLightingPass.cpp` anonymous namespace add:

```cpp
		struct VolumetricLightShaderData
		{
			glm::vec4 position_range{ 0.0f };
			glm::vec4 direction_type{ 0.0f };
			glm::vec4 color_intensity{ 0.0f };
			glm::vec4 cone_shadow{ 1.0f, 1.0f, 0.0f, 0.0f };
		};

		auto visible_light_type_to_shader_type(LightType type) -> float
		{
			switch (type)
			{
			case LightType::Directional:
				return 0.0f;
			case LightType::Point:
				return 1.0f;
			case LightType::Spot:
				return 2.0f;
			default:
				return 0.0f;
			}
		}
```

- [ ] **Step 4: 添加 fullscreen draw helper**

In `VolumetricLightingPass.cpp` anonymous namespace add:

```cpp
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

		void attach_root_constants(GraphicsDrawDesc& draw_desc, GraphicsProgram* program, const VolumetricRootConstants& constants)
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
			const VolumetricRootConstants& constants,
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
```

- [ ] **Step 5: 添加 compute constants helper**

In `VolumetricLightingPass.cpp` anonymous namespace add:

```cpp
		auto make_root_constants(
			const VisibleRenderFrame& frame,
			const VolumetricAtlasDesc& atlas,
			uint32_t output_width,
			uint32_t output_height,
			const VolumetricLightingConfig& config,
			uint32_t light_count) -> VolumetricRootConstants
		{
			VolumetricRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			constants.prev_view_projection = frame.view_projection;
			constants.atlas_size = glm::vec4(
				static_cast<float>(atlas.tile_width),
				static_cast<float>(atlas.tile_height),
				static_cast<float>(atlas.atlas_width),
				static_cast<float>(atlas.atlas_height));
			constants.config0 = glm::vec4(
				config.density,
				config.extinction_scale,
				config.scattering_intensity,
				static_cast<float>(std::max<uint32_t>(config.max_lights, 1u)));
			constants.config1 = glm::vec4(
				static_cast<float>(light_count),
				config.history_blend,
				static_cast<float>(output_width),
				static_cast<float>(output_height));
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, frame.reverse_z ? 1.0f : 0.0f);
			return constants;
		}
```

- [ ] **Step 6: 添加 light buffer upload helper**

Add private method declaration in `VolumetricLightingPass.h`:

```cpp
		bool upload_light_buffer(const VisibleRenderFrame& frame, const VolumetricLightingConfig& config, uint32_t& out_light_count);
```

Implement in `VolumetricLightingPass.cpp`:

```cpp
	bool VolumetricLightingPass::upload_light_buffer(
		const VisibleRenderFrame& frame,
		const VolumetricLightingConfig& config,
		uint32_t& out_light_count)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_light_count = 0;
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		std::vector<VolumetricLightShaderData> light_data{};
		light_data.reserve(std::min<size_t>(frame.lights.size(), config.max_lights));
		for (const VisibleLightData& light : frame.lights)
		{
			if (light_data.size() >= config.max_lights)
			{
				break;
			}

			VolumetricLightShaderData data{};
			data.position_range = glm::vec4(light.position_ws, light.range);
			data.direction_type = glm::vec4(light.direction_ws, visible_light_type_to_shader_type(light.type));
			data.color_intensity = glm::vec4(light.color, light.intensity);
			data.cone_shadow = glm::vec4(
				light.inner_cone_cos,
				light.outer_cone_cos,
				light.casts_shadow ? 1.0f : 0.0f,
				light.sunlight ? 1.0f : 0.0f);
			light_data.push_back(data);
		}

		if (light_data.empty())
		{
			light_data.push_back({});
		}
		out_light_count = static_cast<uint32_t>(std::min<size_t>(frame.lights.size(), config.max_lights));

		const uint32_t required_size = static_cast<uint32_t>(light_data.size() * sizeof(VolumetricLightShaderData));
		if (!m_light_buffer || m_light_buffer->get_size() < required_size)
		{
			StorageBufferDesc desc{};
			desc.size = required_size;
			desc.stride = static_cast<uint32_t>(sizeof(VolumetricLightShaderData));
			desc.cpu_write = true;
			desc.initial_data = light_data.data();
			desc.name = "SceneVolumetricLightBuffer";
			m_light_buffer = m_renderer->create_storage_buffer(desc);
			ASH_PROCESS_ERROR(m_light_buffer != nullptr);
		}
		else
		{
			ASH_PROCESS_ERROR(m_light_buffer->update(0, required_size, light_data.data()));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
```

- [ ] **Step 7: 完成 `add_passes()` 主路径**

Replace the no-op body after config validation with:

```cpp
		ASH_PROCESS_ERROR(m_density_program && m_light_injection_program && m_temporal_program &&
			m_integrate_program && m_composite_program);
		ASH_PROCESS_ERROR(m_point_clamp_sampler && m_linear_clamp_sampler);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);

		const uint32_t output_width = std::max<uint32_t>(view_context.output_target->get_width(), 1u);
		const uint32_t output_height = std::max<uint32_t>(view_context.output_target->get_height(), 1u);
		const VolumetricAtlasDesc atlas = make_atlas_desc(output_width, output_height, sanitized);

		uint32_t light_count = 0;
		ASH_PROCESS_ERROR(upload_light_buffer(frame, sanitized, light_count));
		const VolumetricRootConstants constants =
			make_root_constants(frame, atlas, output_width, output_height, sanitized, light_count);

		outputs.density = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricDensity");
		outputs.scattering = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricScattering");
		outputs.temporal_scattering = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricScatteringTemporal");
		outputs.history_validity = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricHistoryValidity");
		outputs.integrated_lighting = graph.create_texture(make_color_texture_desc(output_width, output_height, true), "SceneVolumetricIntegratedLighting");
		outputs.composite_hdr = graph.create_texture(make_color_texture_desc(output_width, output_height, false), "SceneVolumetricCompositeHDR");
		ASH_PROCESS_ERROR(outputs.density && outputs.scattering && outputs.temporal_scattering &&
			outputs.history_validity && outputs.integrated_lighting && outputs.composite_hdr);

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricDensityPass",
			RenderGraphPassFlags::None,
			[density = outputs.density](RenderGraphComputePassBuilder& pass)
			{
				pass.write_texture(density, RenderGraphAccess::ComputeUAV);
			},
			[this, density = outputs.density, constants, atlas](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricDensityPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> density_target = context.get_texture(density);
				ASH_PROCESS_ERROR(density_target);
				ASH_PROCESS_ERROR(m_density_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_density_program->set_rw_texture("SceneVolumetricDensity", density_target));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_density_program.get();
				dispatch.group_count_x = (atlas.atlas_width + 7u) / 8u;
				dispatch.group_count_y = (atlas.atlas_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));
```

Then add matching light injection, temporal, integrate, and composite passes:

```cpp
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricLightInjectionPass",
			RenderGraphPassFlags::None,
			[density = outputs.density, scattering = outputs.scattering](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(density, RenderGraphAccess::ComputeSRV);
				pass.write_texture(scattering, RenderGraphAccess::ComputeUAV);
			},
			[this, density = outputs.density, scattering = outputs.scattering, constants, atlas](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricLightInjectionPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> density_target = context.get_texture(density);
				std::shared_ptr<RenderTarget> scattering_target = context.get_texture(scattering);
				ASH_PROCESS_ERROR(density_target && scattering_target && m_light_buffer);
				ASH_PROCESS_ERROR(m_light_injection_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_light_injection_program->set_texture("SceneVolumetricDensity", density_target));
				ASH_PROCESS_ERROR(m_light_injection_program->set_storage_buffer("SceneVolumetricLights", m_light_buffer));
				ASH_PROCESS_ERROR(m_light_injection_program->set_rw_texture("SceneVolumetricScattering", scattering_target));
				ASH_PROCESS_ERROR(m_light_injection_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_light_injection_program.get();
				dispatch.group_count_x = (atlas.atlas_width + 7u) / 8u;
				dispatch.group_count_y = (atlas.atlas_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricTemporalPass",
			RenderGraphPassFlags::None,
			[scattering = outputs.scattering, temporal = outputs.temporal_scattering, validity = outputs.history_validity](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(scattering, RenderGraphAccess::ComputeSRV);
				pass.write_texture(temporal, RenderGraphAccess::ComputeUAV);
				pass.write_texture(validity, RenderGraphAccess::ComputeUAV);
			},
			[this, scattering = outputs.scattering, temporal = outputs.temporal_scattering, validity = outputs.history_validity, constants, atlas](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricTemporalPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> scattering_target = context.get_texture(scattering);
				std::shared_ptr<RenderTarget> temporal_target = context.get_texture(temporal);
				std::shared_ptr<RenderTarget> validity_target = context.get_texture(validity);
				ASH_PROCESS_ERROR(scattering_target && temporal_target && validity_target);
				ASH_PROCESS_ERROR(m_temporal_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneVolumetricScattering", scattering_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneVolumetricScatteringHistory", scattering_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_rw_texture("SceneVolumetricScatteringTemporal", temporal_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_rw_texture("SceneVolumetricHistoryValidity", validity_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_temporal_program.get();
				dispatch.group_count_x = (atlas.atlas_width + 7u) / 8u;
				dispatch.group_count_y = (atlas.atlas_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricIntegratePass",
			RenderGraphPassFlags::None,
			[temporal = outputs.temporal_scattering, integrated = outputs.integrated_lighting](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(temporal, RenderGraphAccess::ComputeSRV);
				pass.write_texture(integrated, RenderGraphAccess::ComputeUAV);
			},
			[this, temporal = outputs.temporal_scattering, integrated = outputs.integrated_lighting, constants, output_width, output_height](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricIntegratePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> temporal_target = context.get_texture(temporal);
				std::shared_ptr<RenderTarget> integrated_target = context.get_texture(integrated);
				ASH_PROCESS_ERROR(temporal_target && integrated_target);
				ASH_PROCESS_ERROR(m_integrate_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_integrate_program->set_texture("SceneVolumetricScatteringTemporal", temporal_target));
				ASH_PROCESS_ERROR(m_integrate_program->set_rw_texture("SceneVolumetricIntegratedLighting", integrated_target));
				ASH_PROCESS_ERROR(m_integrate_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_integrate_program.get();
				dispatch.group_count_x = (output_width + 7u) / 8u;
				dispatch.group_count_y = (output_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneVolumetricCompositePass",
			RenderGraphPassFlags::None,
			[scene_hdr_linear, integrated = outputs.integrated_lighting, composite = outputs.composite_hdr](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(integrated, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, composite, RenderLoadAction::Clear, k_clear_color);
			},
			[this, scene_hdr_linear, integrated = outputs.integrated_lighting, composite = outputs.composite_hdr, constants, &view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricCompositePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> hdr = context.get_texture(scene_hdr_linear);
				std::shared_ptr<RenderTarget> integrated_target = context.get_texture(integrated);
				std::shared_ptr<RenderTarget> composite_target = context.get_texture(composite);
				ASH_PROCESS_ERROR(hdr && integrated_target && composite_target);
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneHDRLinear", hdr));
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneVolumetricIntegratedLighting", integrated_target));
				ASH_PROCESS_ERROR(m_composite_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_composite_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		outputs.scene_hdr_linear = outputs.composite_hdr;
```

- [ ] **Step 8: 更新 light injection shader**

Replace `VolumetricLightInjection.hlsl` with:

```hlsl
#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricDensity : register(t0);
struct VolumetricLightData
{
	float4 position_range;
	float4 direction_type;
	float4 color_intensity;
	float4 cone_shadow;
};

StructuredBuffer<VolumetricLightData> SceneVolumetricLights : register(t1);
RWTexture2D<float4> SceneVolumetricScattering : register(u0);
SamplerState ScenePointClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), 1.0.xx);
	float density = SceneVolumetricDensity.SampleLevel(ScenePointClampSampler, uv, 0).r;
	uint light_count = (uint)AshVolumetricConfig1.x;
	float3 scattering = 0.0.xxx;
	for (uint light_index = 0u; light_index < min(light_count, 256u); ++light_index)
	{
		VolumetricLightData light = SceneVolumetricLights[light_index];
		float type = light.direction_type.w;
		float attenuation = type == 0.0 ? 1.0 : saturate(light.position_range.w / max(light.position_range.w + 1.0, 1.0));
		scattering += light.color_intensity.rgb * light.color_intensity.w * attenuation;
	}
	scattering *= density * AshVolumetricConfig0.z / max((float)max(light_count, 1u), 1.0);
	SceneVolumetricScattering[dispatch_id.xy] = float4(scattering, density);
}
```

- [ ] **Step 9: 构建并运行 self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds；self-test exits `0`.

- [ ] **Step 10: 提交 Task 5**

```powershell
git add project/src/engine/Function/Render/VolumetricLightingPass.h project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Implement volumetric lighting render graph passes"
```

---

### Task 6: SceneRenderer 集成与 RenderDebugView

**Files:**
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 写失败集成顺序测试**

Add after `test_scene_renderer_bloom_integration_contract()`:

```cpp
		auto test_scene_renderer_volumetric_lighting_integration_contract() -> bool
		{
			std::ifstream header_file("project/src/engine/Function/Render/SceneRenderer.h");
			std::ifstream source_file("project/src/engine/Function/Render/SceneRenderer.cpp");
			if (!header_file.is_open() || !source_file.is_open())
			{
				return report_self_test_failure("SceneRenderer volumetric lighting integration", "failed to open SceneRenderer source files");
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
				header.find("#include \"Function/Render/VolumetricLightingPass.h\"") != std::string::npos &&
				header.find("VolumetricLightingPass m_volumetric_lighting_pass") != std::string::npos;
			const size_t sky_pos = source.find("m_sky_background_pass.add_pass");
			const size_t volumetric_pos = source.find("m_volumetric_lighting_pass.add_passes");
			const size_t bloom_pos = source.find("m_bloom_pass.add_passes");
			const size_t tone_pos = source.find("m_post_process_tone_map_pass.add_pass");
			const bool order_ok =
				sky_pos != std::string::npos &&
				volumetric_pos != std::string::npos &&
				bloom_pos != std::string::npos &&
				tone_pos != std::string::npos &&
				sky_pos < volumetric_pos &&
				volumetric_pos < bloom_pos &&
				bloom_pos < tone_pos;
			const bool debug_ok =
				source.find("\"SceneVolumetricDensity\"") != std::string::npos &&
				source.find("\"SceneVolumetricScattering\"") != std::string::npos &&
				source.find("\"SceneVolumetricIntegratedLighting\"") != std::string::npos &&
				source.find("\"SceneVolumetricCompositeHDR\"") != std::string::npos;

			return (header_ok && order_ok && debug_ok) ||
				report_self_test_failure("SceneRenderer volumetric lighting integration", "pass is not owned, ordered, or debug-registered correctly");
		}
```

Register:

```cpp
		all_passed = test_scene_renderer_volumetric_lighting_integration_contract() && all_passed;
```

- [ ] **Step 2: 运行 self-test 确认失败**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test fails on SceneRenderer volumetric integration.

- [ ] **Step 3: 扩展 SceneDeferredGraphResources**

In `SceneDeferredGraphResources.h` add:

```cpp
		RenderGraphTextureRef volumetric_density{};
		RenderGraphTextureRef volumetric_scattering{};
		RenderGraphTextureRef volumetric_integrated_lighting{};
		RenderGraphTextureRef volumetric_history_validity{};
		RenderGraphTextureRef volumetric_composite_hdr{};
		RenderGraphTextureRef lightshaft_screen_space_mask{};
		RenderGraphTextureRef lightshaft_screen_space_final{};
```

- [ ] **Step 4: 修改 SceneRenderer.h**

Add include:

```cpp
#include "Function/Render/VolumetricLightingPass.h"
```

Add member near Bloom:

```cpp
		VolumetricLightingPass m_volumetric_lighting_pass{};
```

- [ ] **Step 5: 初始化和 shutdown**

In `SceneRenderer::initialize()` after sky/background or before Bloom initialization:

```cpp
		ASH_PROCESS_ERROR(m_volumetric_lighting_pass.initialize(m_renderer));
```

In `SceneRenderer::shutdown()` before `m_bloom_pass.shutdown()` or near other pass shutdowns:

```cpp
		m_volumetric_lighting_pass.shutdown();
```

- [ ] **Step 6: 插入 pass**

In `SceneRenderer::render_visible_frame()` after:

```cpp
			ASH_PROCESS_ERROR(m_sky_background_pass.add_pass(
				graph,
				frame,
				graph_resources.depth,
				graph_resources.scene_hdr_linear,
				view_context));
```

add:

```cpp
			const VolumetricLightingPassOutputs volumetric_outputs = m_volumetric_lighting_pass.add_passes(
				graph,
				frame,
				graph_resources,
				graph_resources.scene_hdr_linear,
				view_context,
				frame.render_config.volumetric_lighting);
			ASH_PROCESS_ERROR(volumetric_outputs.scene_hdr_linear);
			graph_resources.scene_hdr_linear = volumetric_outputs.scene_hdr_linear;
			graph_resources.volumetric_density = volumetric_outputs.density;
			graph_resources.volumetric_scattering = volumetric_outputs.scattering;
			graph_resources.volumetric_integrated_lighting = volumetric_outputs.integrated_lighting;
			graph_resources.volumetric_history_validity = volumetric_outputs.history_validity;
			graph_resources.volumetric_composite_hdr = volumetric_outputs.composite_hdr;
			graph_resources.lightshaft_screen_space_mask = volumetric_outputs.screen_space_mask;
			graph_resources.lightshaft_screen_space_final = volumetric_outputs.screen_space_final;
```

- [ ] **Step 7: 注册 debug items**

Immediately after pass insertion add:

```cpp
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricDensity",
				"Volumetric Density",
				volumetric_outputs.density,
				RenderDebugVisualization::Scalar,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricScattering",
				"Volumetric Scattering",
				volumetric_outputs.scattering,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricIntegratedLighting",
				"Volumetric Integrated Lighting",
				volumetric_outputs.integrated_lighting,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricCompositeHDR",
				"Volumetric Composite HDR",
				volumetric_outputs.composite_hdr,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricHistoryValidity",
				"Volumetric History Validity",
				volumetric_outputs.history_validity,
				RenderDebugVisualization::Scalar,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneLightShaftOcclusionMask",
				"LightShaft Screen Space Mask",
				volumetric_outputs.screen_space_mask,
				RenderDebugVisualization::Scalar,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneLightShaftScreenSpaceFinal",
				"LightShaft Screen Space Final",
				volumetric_outputs.screen_space_final,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
```

- [ ] **Step 8: self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test exits `0`.

- [ ] **Step 9: 提交 Task 6**

```powershell
git add project/src/engine/Function/Render/SceneDeferredGraphResources.h project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Integrate volumetric lighting into scene renderer"
```

---

### Task 7: Screen-space lightshaft fallback

**Files:**
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`
- Modify: `project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 写 graph fallback 测试**

Add in `test_volumetric_lighting_pass_adds_expected_graph_chain_for_tests()` after main path checks:

```cpp
			RenderGraphBuilder fallback_graph = RenderGraphBuilder::create_headless_for_tests("VolumetricLightingFallbackGraphSelfTest");
			RenderGraphTextureRef fallback_hdr = fallback_graph.register_external_texture_desc_for_tests(hdr_desc, "SceneHDRLinear");
			RenderGraphTextureRef fallback_depth = fallback_graph.register_external_texture_desc_for_tests(depth_desc, "SceneDeferredDepth");
			config.screen_space_fallback = true;
			const bool fallback_added = VolumetricLightingPass::add_passes_for_tests(
				fallback_graph,
				fallback_hdr,
				fallback_depth,
				128,
				64,
				config);
			if (!fallback_added)
			{
				return report_self_test_failure("VolumetricLighting graph", "fallback graph helper failed");
			}
			const std::vector<RenderGraphPassNode>& fallback_passes = fallback_graph.get_passes_for_tests();
			const bool fallback_ok = std::any_of(
				fallback_passes.begin(),
				fallback_passes.end(),
				[](const RenderGraphPassNode& pass)
				{
					return pass.name == "SceneLightShaftScreenSpacePass";
				});
			if (!fallback_ok)
			{
				return report_self_test_failure("VolumetricLighting graph", "screen-space fallback pass was not added");
			}
```

- [ ] **Step 2: 运行 self-test 确认失败**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: fallback graph test fails.

- [ ] **Step 3: 实现 helper fallback branch**

At top of `add_passes_for_tests()` after config validation:

```cpp
		if (sanitized.screen_space_fallback)
		{
			RenderGraphTextureRef mask = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftOcclusionMask");
			RenderGraphTextureRef composite = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftScreenSpaceCompositeHDR");
			ASH_PROCESS_ERROR(mask && composite);
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneLightShaftScreenSpacePass",
				RenderGraphPassFlags::None,
				[scene_hdr_linear, scene_depth, mask, composite](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(scene_depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, mask, RenderLoadAction::Clear, k_clear_color);
					pass.write_color(1, composite, RenderLoadAction::Clear, k_clear_color);
				},
				[](RenderGraphRasterContext&) { return true; }));
			graph.extract_texture(mask);
			graph.extract_texture(composite);
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}
```

- [ ] **Step 4: 实现 runtime fallback branch**

At top of `add_passes()` after output dimensions:

```cpp
		if (sanitized.screen_space_fallback)
		{
			outputs.screen_space_mask = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftOcclusionMask");
			outputs.screen_space_final = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftScreenSpaceCompositeHDR");
			ASH_PROCESS_ERROR(outputs.screen_space_mask && outputs.screen_space_final);

			VolumetricRootConstants constants =
				make_root_constants(frame, make_atlas_desc(output_width, output_height, sanitized), output_width, output_height, sanitized, 1u);
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneLightShaftScreenSpacePass",
				RenderGraphPassFlags::None,
				[scene_hdr_linear, depth = deferred_resources.depth, mask = outputs.screen_space_mask, output = outputs.screen_space_final](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, mask, RenderLoadAction::Clear, k_clear_color);
					pass.write_color(1, output, RenderLoadAction::Clear, k_clear_color);
				},
				[this, scene_hdr_linear, depth = deferred_resources.depth, output = outputs.screen_space_final, constants, &view_context](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneLightShaftScreenSpacePass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> hdr = context.get_texture(scene_hdr_linear);
					std::shared_ptr<RenderTarget> depth_target = context.get_texture(depth);
					std::shared_ptr<RenderTarget> output_target = context.get_texture(output);
					ASH_PROCESS_ERROR(hdr && depth_target && output_target);
					ASH_PROCESS_ERROR(m_screen_space_program->set_texture("SceneHDRLinear", hdr));
					ASH_PROCESS_ERROR(m_screen_space_program->set_texture("SceneDepth", depth_target));
					ASH_PROCESS_ERROR(m_screen_space_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
					ASH_PROCESS_ERROR(m_screen_space_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_screen_space_program.get(), constants, view_context)));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				}));
			outputs.scene_hdr_linear = outputs.screen_space_final;
			ASH_PROCESS_GUARD_RETURN_END(outputs, VolumetricLightingPassOutputs{});
		}
```

- [ ] **Step 5: 运行 self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test exits `0`.

- [ ] **Step 6: 提交 Task 7**

```powershell
git add project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add screen-space lightshaft fallback"
```

---

### Task 8: Temporal history 持久资源

**Files:**
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.h`
- Modify: `project/src/engine/Function/Render/VolumetricLightingPass.cpp`
- Modify: `project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl`

- [ ] **Step 1: 添加 source contract**

Extend `test_volumetric_lighting_pass_source_contract()` token list:

```cpp
					"VolumetricHistoryEntry",
					"m_history_entries",
					"SceneVolumetricScatteringHistory",
					"SceneVolumetricHistoryWrite"
```

- [ ] **Step 2: 运行 self-test 确认失败**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: source contract fails.

- [ ] **Step 3: 扩展 header**

In `VolumetricLightingPass.h` add includes:

```cpp
#include <array>
```

Add private struct/member:

```cpp
		struct VolumetricHistoryEntry
		{
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t write_index = 0;
			bool valid = false;
			std::array<std::shared_ptr<RenderTarget>, 2> scattering{};
		};

		std::unordered_map<uint64_t, VolumetricHistoryEntry> m_history_entries{};
```

- [ ] **Step 4: 添加 history texture helper**

In `VolumetricLightingPass.cpp` add private method declaration:

```cpp
		bool ensure_history_entry(uint64_t view_key, const VolumetricAtlasDesc& atlas, VolumetricHistoryEntry*& out_entry);
```

Implement:

```cpp
	bool VolumetricLightingPass::ensure_history_entry(
		uint64_t view_key,
		const VolumetricAtlasDesc& atlas,
		VolumetricHistoryEntry*& out_entry)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_entry = nullptr;
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		VolumetricHistoryEntry& entry = m_history_entries[view_key];
		const bool size_changed = entry.width != atlas.atlas_width || entry.height != atlas.atlas_height;
		if (size_changed)
		{
			entry = {};
			entry.width = atlas.atlas_width;
			entry.height = atlas.atlas_height;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(entry.scattering.size()); ++index)
		{
			if (!entry.scattering[index])
			{
				RenderTargetDesc desc{};
				desc.width = to_graph_dimension(atlas.atlas_width);
				desc.height = to_graph_dimension(atlas.atlas_height);
				desc.format = RenderTextureFormat::RGBA16_SFLOAT;
				desc.shader_resource = true;
				desc.unordered_access = true;
				desc.name = index == 0 ? "SceneVolumetricHistoryA" : "SceneVolumetricHistoryB";
				desc.use_optimized_clear_value = true;
				desc.optimized_clear_color = k_clear_color;
				entry.scattering[index] = m_renderer->create_render_target(desc);
				ASH_PROCESS_ERROR(entry.scattering[index] != nullptr);
			}
		}

		out_entry = &entry;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
```

- [ ] **Step 5: Wire history resources in temporal pass**

Before temporal pass creation in runtime `add_passes()`:

```cpp
		VolumetricHistoryEntry* history_entry = nullptr;
		RenderGraphTextureRef history_read{};
		RenderGraphTextureRef history_write{};
		const bool history_enabled = sanitized.history;
		if (history_enabled)
		{
			ASH_PROCESS_ERROR(ensure_history_entry(view_context.view_id, atlas, history_entry));
			const uint32_t read_index = history_entry->valid ? (history_entry->write_index ^ 1u) : history_entry->write_index;
			const uint32_t write_index = history_entry->write_index;
			history_read = graph.register_external_texture(
				history_entry->scattering[read_index],
				"SceneVolumetricScatteringHistory",
				RenderGraphAccess::ComputeSRV);
			history_write = graph.register_external_texture(
				history_entry->scattering[write_index],
				"SceneVolumetricHistoryWrite",
				RenderGraphAccess::ComputeUAV);
			ASH_PROCESS_ERROR(history_read && history_write);
		}
```

Modify temporal pass setup:

```cpp
				if (history_enabled)
				{
					pass.read_texture(history_read, RenderGraphAccess::ComputeSRV);
					pass.write_texture(history_write, RenderGraphAccess::ComputeUAV);
				}
```

Modify execute lambda capture and body:

```cpp
			[this, scattering = outputs.scattering, temporal = outputs.temporal_scattering, validity = outputs.history_validity,
				history_enabled, history_read, history_write, history_entry, constants, atlas](RenderGraphComputeContext& context) -> bool
```

Inside execute:

```cpp
				std::shared_ptr<RenderTarget> history_read_target = history_enabled ? context.get_texture(history_read) : scattering_target;
				std::shared_ptr<RenderTarget> history_write_target = history_enabled ? context.get_texture(history_write) : temporal_target;
				ASH_PROCESS_ERROR(history_read_target && history_write_target);
				ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneVolumetricScatteringHistory", history_read_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_rw_texture("SceneVolumetricHistoryWrite", history_write_target));
```

After successful dispatch:

```cpp
				if (history_enabled && history_entry)
				{
					history_entry->valid = true;
					history_entry->write_index ^= 1u;
				}
```

- [ ] **Step 6: Update temporal shader write target**

In `VolumetricTemporal.hlsl` add:

```hlsl
RWTexture2D<float4> SceneVolumetricHistoryWrite : register(u2);
```

After temporal write add:

```hlsl
	SceneVolumetricHistoryWrite[dispatch_id.xy] = filtered;
```

- [ ] **Step 7: Build/self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test exits `0`.

- [ ] **Step 8: Commit**

```powershell
git add project/src/engine/Function/Render/VolumetricLightingPass.h project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add volumetric lighting history resources"
```

---

### Task 9: Sandbox scene 配置与运行验证

**Files:**
- Modify: `product/assets/scenes/Sandbox.scene.json`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: 添加 Sandbox scene contract self-test**

Add:

```cpp
		auto test_sandbox_scene_enables_volumetric_lighting() -> bool
		{
			std::ifstream scene_file("product/assets/scenes/Sandbox.scene.json");
			if (!scene_file.is_open())
			{
				return report_self_test_failure("Sandbox volumetric scene config", "failed to open Sandbox.scene.json");
			}
			const std::string source{
				std::istreambuf_iterator<char>(scene_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				source.find("\"volumetric_lighting\"") != std::string::npos &&
				source.find("\"enabled\": true") != std::string::npos &&
				source.find("\"quality\": \"Low\"") != std::string::npos;
			return ok ||
				report_self_test_failure("Sandbox volumetric scene config", "standard Sandbox scene does not enable Low volumetric lighting");
		}
```

Register:

```cpp
		all_passed = test_sandbox_scene_enables_volumetric_lighting() && all_passed;
```

- [ ] **Step 2: 运行 self-test 确认失败**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test fails on Sandbox volumetric scene config.

- [ ] **Step 3: 修改 Sandbox.scene.json**

Under `scene_config`, add after `bloom`:

```json
    "volumetric_lighting": {
      "anisotropy": 0.35,
      "debug_view": "Off",
      "density": 0.015,
      "enabled": true,
      "extinction_scale": 1.0,
      "froxel_depth_slices": 32,
      "froxel_resolution_scale": 0.25,
      "history": true,
      "history_blend": 0.85,
      "max_lights": 64,
      "quality": "Low",
      "scattering_intensity": 0.25,
      "screen_space_fallback": false
    }
```

- [ ] **Step 4: Build/self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: self-test exits `0`.

- [ ] **Step 5: Run Sandbox smoke on current backend**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: process exits `0`; logs contain no validation/debug-layer error and no shader binding error for `SceneVolumetric`.

- [ ] **Step 6: Commit**

```powershell
git add product/assets/scenes/Sandbox.scene.json project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Enable volumetric lighting in sandbox scene"
```

---

### Task 10: 文档更新

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`

- [ ] **Step 1: Update README**

In `README.md` 当前状态列表中，把 deferred / Bloom 相关句子扩展为：

```markdown
- Shadow 当前区分 sunlight 与普通 directional light；当前 deferred lighting 已接入 base/emissive、per-light directional（含 shadowed 变体）、point、spot，场景级 `scene_config` 可配置屏幕空间 AO（`Off` / `SSAO` / `HBAO` / `GTAO`）、方向光阴影、Bloom 和 Volumetric Lighting；composite（线性 HDR 中转 RT）、sky/background、体积光 HDR 合成、Bloom 与独立全屏 tone-map pass 已接入。
```

In Sandbox progress row, include:

```markdown
标准 Sandbox scene 显式开启 Low Volumetric Lighting，用于覆盖 froxel 体积光、Bloom 和 tone-map 的共享渲染链路。
```

- [ ] **Step 2: Update EngineDeveloperGuide**

In Scene render config section around `scene_config` JSON, add `volumetric_lighting` sample object matching Sandbox Low config.

In deferred render path paragraph, add:

```markdown
Volumetric Lighting 在 `SceneSkyBackgroundPass` 后、`BloomPass` 前运行。主路径使用 frustum-aligned froxel atlas，通过 compute pass 写入 density、scattering、temporal scattering 和 integrated lighting，再以 fullscreen composite 写回线性 HDR；`screen_space_fallback=true` 时使用 depth mask + radial blur 的 lightshaft fallback。默认旧场景关闭，标准 Sandbox scene 使用 Low 配置开启。
```

In self-test coverage paragraph, add:

```markdown
Volumetric Lighting config defaults/sanitize、scene JSON round-trip、RenderScene snapshot、RenderGraph pass contract、SceneRenderer integration order
```

- [ ] **Step 3: 搜索确认文档包含关键词**

Run:

```powershell
rg -n "Volumetric Lighting|volumetric_lighting|SceneVolumetric" README.md docs/EngineDeveloperGuide.md
```

Expected: output includes README and EngineDeveloperGuide matches.

- [ ] **Step 4: Commit docs**

```powershell
git add README.md docs/EngineDeveloperGuide.md
git commit -m "Document volumetric lighting render path"
```

---

### Task 11: 完整验证矩阵

**Files:**
- No source edits unless validation finds a bug.

- [ ] **Step 1: Regenerate/build Sandbox**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: build succeeds and copies `Engine.dll` to `product/bin64/Debug-windows-x86_64/`.

- [ ] **Step 2: Build Editor**

Run:

```powershell
.\build_editor.bat Debug x64
```

Expected: build succeeds and copies `Engine.dll` / `Editor.exe` runtime artifacts.

- [ ] **Step 3: Run self-test**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exits `0`.

- [ ] **Step 4: Validate Sandbox Vulkan**

Set `product/config/Engine.ini` backend to Vulkan if needed using the existing project backend config pattern, then run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
```

Expected: exits `0`; `product/logs` contains no Vulkan validation error, no shader reflection/binding error, and no VMA leak report.

- [ ] **Step 5: Validate Sandbox DX12**

Set `product/config/Engine.ini` backend to DX12 if needed, then run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
```

Expected: exits `0`; logs contain no DX12 debug layer error and no shader binding error.

- [ ] **Step 6: Validate Editor Vulkan**

Set backend to Vulkan, then run:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected: exits `0`; logs contain no validation/debug-layer error.

- [ ] **Step 7: Validate Editor DX12**

Set backend to DX12, then run:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Expected: exits `0`; logs contain no DX12 debug layer error.

- [ ] **Step 8: Inspect git status**

Run:

```powershell
git status --short
```

Expected: only intentional source/doc/scene changes are present. `product/config/editor/imgui.ini` may remain as pre-existing user change and must not be committed with this work.

- [ ] **Step 9: Final commit for validation fixes**

If validation required source fixes, commit them:

```powershell
git add project/src/engine/Function/Render/VolumetricLightingPass.cpp project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl README.md docs/EngineDeveloperGuide.md
git commit -m "Stabilize volumetric lighting validation"
```

Expected: use this command only after replacing the listed paths with the exact files changed by validation fixes; no commit is created if no validation fixes were needed.
