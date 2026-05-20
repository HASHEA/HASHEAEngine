# Ambient Occlusion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 AshEngine deferred scene path 中接入可配置 `Off` / `SSAO` / `HBAO` / `GTAO` 的屏幕空间 AO，并通过一张统一的 `SceneAmbientOcclusion` texture 供 deferred lighting 消费。

**Architecture:** 新增 `AmbientOcclusionConfig` 负责 typed runtime config，新增 `AmbientOcclusionPass` 负责 AO 资源、shader program 和 RenderGraph pass 注册。`SceneRenderer` 只在 GBuffer 与 lighting 之间调用 AO pass，`DeferredLightingPass` 只消费统一 AO texture，不知道具体算法。第一版三种算法都用 fullscreen raster path，避免扩展 RHI compute/UAV 路径。

**Tech Stack:** C++17, HLSL, AshEngine Function/Render, RenderGraph, RenderDevice/Renderer, DXC shader reflection, Vulkan + DX12 shared RHI validation, Engine self-test.

---

## File Structure

- Create: `project/src/engine/Function/Render/AmbientOcclusionConfig.h`
  - AO mode / quality enum、config struct、INI loader、runtime config getter/setter。
- Create: `project/src/engine/Function/Render/AmbientOcclusionConfig.cpp`
  - `IniConfig` 解析、默认值、clamp、string mapping、进程级 runtime snapshot。
- Create: `project/src/engine/Function/Render/AmbientOcclusionPass.h`
  - `AmbientOcclusionPass` facade，初始化、shutdown、RenderGraph pass 注册。
- Create: `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`
  - neutral AO texture、sampler、graphics program 创建，SSAO/HBAO/GTAO/blur pass 注册。
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli`
  - fullscreen VS、root constants、depth/normal decode、position reconstruction、AO helpers。
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl`
  - SSAO baseline。
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionHBAO.hlsl`
  - horizon-search AO。
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionGTAO.hlsl`
  - spatial-only GTAO approximation。
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl`
  - depth-aware AO blur。
- Modify: `project/src/engine/Function/Application.cpp`
  - 在 runtime render feature config 后加载并发布 AO config。
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
  - 增加 `ambient_occlusion` ref。
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
  - 持有 `AmbientOcclusionPass`。
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
  - 初始化/shutdown AO pass，并在 GBuffer 后注册 AO。
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
  - `add_passes()` 继续接收 `SceneDeferredGraphResources`，但要求 `ambient_occlusion` 有效。
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
  - lighting pass 声明 AO SRV，绑定 AO texture，shader hash 自动通过 `DeferredCommon.hlsli` 更新。
- Modify: `project/src/engine/Shaders/Deferred/DeferredCommon.hlsli`
  - 新增 `SceneAmbientOcclusion` resource，计算 `materialAO * screenAO`，保留 unlit/emissive 行为。
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
  - AO config parser self-test，deferred graph AO dependency self-test。
- Modify: `product/config/Engine.ini`
  - 增加 `[AmbientOcclusion]` 默认段，保留已有 RHI/validation 配置。
- Modify: `README.md`
  - 更新当前渲染状态与计划链接。
- Modify: `docs/EngineDeveloperGuide.md`
  - 记录 AO config、graph 位置、shader binding、验证要求。
- Modify: `docs/RenderGraphAPISpec.md`
  - 更新 deferred graph 示例链路。

---

### Task 1: Add Ambient Occlusion Runtime Config

**Files:**
- Create: `project/src/engine/Function/Render/AmbientOcclusionConfig.h`
- Create: `project/src/engine/Function/Render/AmbientOcclusionConfig.cpp`
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Write failing self-tests for AO config parsing**

In `project/src/engine/Base/EngineSelfTests.cpp`, add the include near other render includes:

```cpp
#include "Function/Render/AmbientOcclusionConfig.h"
```

Add this function near `test_render_feature_config_does_not_register_reverse_z_switch()`:

```cpp
auto test_ambient_occlusion_config_parses_modes_and_clamps_values() -> bool
{
	const std::filesystem::path test_dir = engine_self_test_dir();
	const std::filesystem::path config_path = test_dir / "ambient_occlusion_self_test.ini";
	{
		std::ofstream config_file(config_path, std::ios::trunc);
		config_file <<
			"[AmbientOcclusion]\n"
			"Mode=HBAO\n"
			"Quality=High\n"
			"Radius=-4.0\n"
			"Intensity=3.5\n"
			"Power=0.0\n"
			"HalfResolution=true\n"
			"Blur=false\n";
	}

	const AmbientOcclusionConfig config = load_runtime_ambient_occlusion_config(config_path.string().c_str());
	const bool parsed =
		config.mode == AmbientOcclusionMode::HBAO &&
		config.quality == AmbientOcclusionQuality::High &&
		config.radius > 0.0f &&
		config.intensity == 3.5f &&
		config.power > 0.0f &&
		config.half_resolution &&
		!config.blur;
	if (!parsed)
	{
		return report_self_test_failure("AmbientOcclusion config", "valid AO config values were not parsed or clamped correctly");
	}

	const std::filesystem::path invalid_config_path = test_dir / "ambient_occlusion_invalid_self_test.ini";
	{
		std::ofstream config_file(invalid_config_path, std::ios::trunc);
		config_file <<
			"[AmbientOcclusion]\n"
			"Mode=NotAMode\n"
			"Quality=NotAQuality\n"
			"Radius=not-a-number\n";
	}

	const AmbientOcclusionConfig invalid_config = load_runtime_ambient_occlusion_config(invalid_config_path.string().c_str());
	const AmbientOcclusionConfig defaults = make_default_ambient_occlusion_config();
	return (invalid_config.mode == defaults.mode &&
		invalid_config.quality == defaults.quality &&
		invalid_config.radius == defaults.radius) ||
		report_self_test_failure("AmbientOcclusion config", "invalid AO config values did not fall back to defaults");
}
```

Add the call in `run_engine_base_self_tests()` immediately after `test_render_feature_config_does_not_register_reverse_z_switch()`:

```cpp
all_passed = test_ambient_occlusion_config_parses_modes_and_clamps_values() && all_passed;
```

- [ ] **Step 2: Run self-test build path and confirm the test fails to compile**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: compile fails because `AmbientOcclusionConfig.h` does not exist yet.

- [ ] **Step 3: Create `AmbientOcclusionConfig.h`**

Create `project/src/engine/Function/Render/AmbientOcclusionConfig.h`:

```cpp
#pragma once

#include "Base/hcore.h"

namespace AshEngine
{
	enum class AmbientOcclusionMode : uint8_t
	{
		Off = 0,
		SSAO,
		HBAO,
		GTAO
	};

	enum class AmbientOcclusionQuality : uint8_t
	{
		Low = 0,
		Medium,
		High
	};

	struct ASH_API AmbientOcclusionConfig
	{
		AmbientOcclusionMode mode = AmbientOcclusionMode::Off;
		AmbientOcclusionQuality quality = AmbientOcclusionQuality::Medium;
		float radius = 1.5f;
		float intensity = 1.0f;
		float power = 1.0f;
		bool half_resolution = false;
		bool blur = true;
	};

	ASH_API const char* ambient_occlusion_mode_name(AmbientOcclusionMode mode);
	ASH_API const char* ambient_occlusion_quality_name(AmbientOcclusionQuality quality);
	ASH_API AmbientOcclusionConfig make_default_ambient_occlusion_config();
	ASH_API AmbientOcclusionConfig load_runtime_ambient_occlusion_config(const char* config_path);
	ASH_API void set_runtime_ambient_occlusion_config(const AmbientOcclusionConfig& config);
	ASH_API AmbientOcclusionConfig get_runtime_ambient_occlusion_config();
}
```

- [ ] **Step 4: Create `AmbientOcclusionConfig.cpp`**

Create `project/src/engine/Function/Render/AmbientOcclusionConfig.cpp`:

```cpp
#include "Function/Render/AmbientOcclusionConfig.h"

#include "Base/IniConfig.h"
#include "Base/hlog.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_token(std::string value) -> std::string
		{
			value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), value.end());
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return value;
		}

		auto parse_ao_mode(const std::string& value, AmbientOcclusionMode& out_mode) -> bool
		{
			const std::string token = normalize_token(value);
			if (token == "off")
			{
				out_mode = AmbientOcclusionMode::Off;
				return true;
			}
			if (token == "ssao")
			{
				out_mode = AmbientOcclusionMode::SSAO;
				return true;
			}
			if (token == "hbao")
			{
				out_mode = AmbientOcclusionMode::HBAO;
				return true;
			}
			if (token == "gtao")
			{
				out_mode = AmbientOcclusionMode::GTAO;
				return true;
			}
			return false;
		}

		auto parse_ao_quality(const std::string& value, AmbientOcclusionQuality& out_quality) -> bool
		{
			const std::string token = normalize_token(value);
			if (token == "low")
			{
				out_quality = AmbientOcclusionQuality::Low;
				return true;
			}
			if (token == "medium")
			{
				out_quality = AmbientOcclusionQuality::Medium;
				return true;
			}
			if (token == "high")
			{
				out_quality = AmbientOcclusionQuality::High;
				return true;
			}
			return false;
		}

		auto clamp_positive(float value, float fallback, float minimum, float maximum) -> float
		{
			if (!(value > 0.0f))
			{
				return fallback;
			}
			return std::clamp(value, minimum, maximum);
		}

		auto try_get_ini_string(const IniConfig& ini_config, const char* section, const char* key, std::string& out_value) -> bool
		{
			if (!ini_config.has_value(section, key))
			{
				return false;
			}
			out_value = ini_config.get_string(section, key, "");
			return true;
		}

		auto try_parse_float_value(const std::string& text, float& out_value) -> bool
		{
			const std::string trimmed = trim_ini_string(text);
			if (trimmed.empty())
			{
				return false;
			}

			char* parse_end = nullptr;
			errno = 0;
			const float parsed = std::strtof(trimmed.c_str(), &parse_end);
			if (parse_end == trimmed.c_str() || *parse_end != '\0' || errno == ERANGE)
			{
				return false;
			}
			out_value = parsed;
			return true;
		}

		auto try_get_ini_float(const IniConfig& ini_config, const char* section, const char* key, float& out_value) -> bool
		{
			std::string text{};
			return try_get_ini_string(ini_config, section, key, text) && try_parse_float_value(text, out_value);
		}

		auto runtime_config_storage() -> AmbientOcclusionConfig&
		{
			static AmbientOcclusionConfig config = make_default_ambient_occlusion_config();
			return config;
		}

		auto runtime_config_mutex() -> std::mutex&
		{
			static std::mutex mutex;
			return mutex;
		}
	}

	const char* ambient_occlusion_mode_name(AmbientOcclusionMode mode)
	{
		switch (mode)
		{
		case AmbientOcclusionMode::SSAO:
			return "SSAO";
		case AmbientOcclusionMode::HBAO:
			return "HBAO";
		case AmbientOcclusionMode::GTAO:
			return "GTAO";
		case AmbientOcclusionMode::Off:
		default:
			return "Off";
		}
	}

	const char* ambient_occlusion_quality_name(AmbientOcclusionQuality quality)
	{
		switch (quality)
		{
		case AmbientOcclusionQuality::Low:
			return "Low";
		case AmbientOcclusionQuality::High:
			return "High";
		case AmbientOcclusionQuality::Medium:
		default:
			return "Medium";
		}
	}

	AmbientOcclusionConfig make_default_ambient_occlusion_config()
	{
		return AmbientOcclusionConfig{};
	}

	AmbientOcclusionConfig load_runtime_ambient_occlusion_config(const char* config_path)
	{
		AmbientOcclusionConfig config = make_default_ambient_occlusion_config();
		IniConfig ini_config{};
		if (!ini_config.load(config_path))
		{
			HLogInfo("Ambient occlusion config file '{}' was not found. Using default AO config.", resolve_runtime_config_path(config_path).string());
			return config;
		}

		std::string mode_text{};
		if (try_get_ini_string(ini_config, "AmbientOcclusion", "Mode", mode_text))
		{
			AmbientOcclusionMode parsed_mode = config.mode;
			if (parse_ao_mode(mode_text, parsed_mode))
			{
				config.mode = parsed_mode;
			}
			else
			{
				HLogWarning("AmbientOcclusion.Mode '{}' is invalid. Keeping default '{}'.", mode_text, ambient_occlusion_mode_name(config.mode));
			}
		}

		std::string quality_text{};
		if (try_get_ini_string(ini_config, "AmbientOcclusion", "Quality", quality_text))
		{
			AmbientOcclusionQuality parsed_quality = config.quality;
			if (parse_ao_quality(quality_text, parsed_quality))
			{
				config.quality = parsed_quality;
			}
			else
			{
				HLogWarning("AmbientOcclusion.Quality '{}' is invalid. Keeping default '{}'.", quality_text, ambient_occlusion_quality_name(config.quality));
			}
		}

		float value = 0.0f;
		if (try_get_ini_float(ini_config, "AmbientOcclusion", "Radius", value))
		{
			config.radius = clamp_positive(value, config.radius, 0.05f, 20.0f);
		}
		if (try_get_ini_float(ini_config, "AmbientOcclusion", "Intensity", value))
		{
			config.intensity = clamp_positive(value, config.intensity, 0.0f, 8.0f);
		}
		if (try_get_ini_float(ini_config, "AmbientOcclusion", "Power", value))
		{
			config.power = clamp_positive(value, config.power, 0.05f, 8.0f);
		}

		bool bool_value = false;
		if (ini_config.try_get_bool("AmbientOcclusion", "HalfResolution", bool_value))
		{
			config.half_resolution = bool_value;
		}
		if (ini_config.try_get_bool("AmbientOcclusion", "Blur", bool_value))
		{
			config.blur = bool_value;
		}

		HLogInfo(
			"Runtime ambient occlusion config loaded. mode={} quality={} radius={} intensity={} power={} half_resolution={} blur={}.",
			ambient_occlusion_mode_name(config.mode),
			ambient_occlusion_quality_name(config.quality),
			config.radius,
			config.intensity,
			config.power,
			config.half_resolution,
			config.blur);
		return config;
	}

	void set_runtime_ambient_occlusion_config(const AmbientOcclusionConfig& config)
	{
		std::lock_guard<std::mutex> lock(runtime_config_mutex());
		runtime_config_storage() = config;
	}

	AmbientOcclusionConfig get_runtime_ambient_occlusion_config()
	{
		std::lock_guard<std::mutex> lock(runtime_config_mutex());
		return runtime_config_storage();
	}
}
```

- [ ] **Step 5: Publish AO config during application initialization**

In `project/src/engine/Function/Application.cpp`, add:

```cpp
#include "Function/Render/AmbientOcclusionConfig.h"
```

Immediately after the existing render feature config load/publish block, add:

```cpp
const AmbientOcclusionConfig ambientOcclusionConfig = load_runtime_ambient_occlusion_config(config.backendConfigPath);
set_runtime_ambient_occlusion_config(ambientOcclusionConfig);
```

- [ ] **Step 6: Build and run self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits `0`.

- [ ] **Step 7: Commit config task**

Run:

```bash
git add project/src/engine/Function/Render/AmbientOcclusionConfig.h project/src/engine/Function/Render/AmbientOcclusionConfig.cpp project/src/engine/Function/Application.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add ambient occlusion runtime config"
```

---

### Task 2: Lock RenderGraph AO Dependency Contract With Self-Test

**Files:**
- Modify: `project/src/engine/Function/Render/SceneDeferredGraphResources.h`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add AO texture ref to deferred graph resources**

In `project/src/engine/Function/Render/SceneDeferredGraphResources.h`, add:

```cpp
RenderGraphTextureRef ambient_occlusion{};
```

The struct should become:

```cpp
struct SceneDeferredGraphResources
{
	std::vector<RenderGraphTextureRef> gbuffer_targets{};
	RenderGraphTextureRef depth{};
	RenderGraphTextureRef ambient_occlusion{};
	RenderGraphTextureRef lighting_diffuse{};
	RenderGraphTextureRef lighting_specular{};
	RenderGraphTextureRef scene_hdr_linear{};
};
```

- [ ] **Step 2: Update deferred graph self-test to include AO producer**

In `test_scene_deferred_graph_resources_describe_live_pass_chain()` in `project/src/engine/Base/EngineSelfTests.cpp`, after depth creation and before lighting target creation, add:

```cpp
RenderGraphTextureDesc ambient_occlusion_desc{};
ambient_occlusion_desc.width = 64;
ambient_occlusion_desc.height = 64;
ambient_occlusion_desc.format = RenderTextureFormat::RGBA8_UNORM;
ambient_occlusion_desc.shader_resource = true;
resources.ambient_occlusion = graph.create_texture(ambient_occlusion_desc, "SceneAmbientOcclusion");
```

After `SceneGBufferPass`, insert:

```cpp
graph.add_raster_pass(
	"SceneAmbientOcclusionPass",
	RenderGraphPassFlags::None,
	[&](RenderGraphRasterPassBuilder& pass)
	{
		pass.read_texture(resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
		pass.read_texture(resources.depth, RenderGraphAccess::GraphicsSRV);
		pass.write_color(0, resources.ambient_occlusion, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });
	},
	[](RenderGraphRasterContext&)
	{
		return true;
	});
```

In `SceneDeferredLightingAccumPass` setup, add:

```cpp
pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
```

Update expected live pass checks:

```cpp
ok = ok && resources.ambient_occlusion;
ok = ok && result.live_pass_indices.size() == 5u;
ok = ok && result.live_pass_indices[0] == 0u;
ok = ok && result.live_pass_indices[1] == 1u;
ok = ok && result.live_pass_indices[2] == 2u;
ok = ok && result.live_pass_indices[3] == 3u;
ok = ok && result.live_pass_indices[4] == 4u;
ok = ok && result.texture_lifetimes[resources.ambient_occlusion.index].first_pass == 1u;
ok = ok && result.texture_lifetimes[resources.ambient_occlusion.index].last_pass == 2u;
ok = ok && result.texture_lifetimes[resources.lighting_diffuse.index].first_pass == 2u;
ok = ok && result.texture_lifetimes[resources.lighting_diffuse.index].last_pass == 3u;
ok = ok && result.texture_lifetimes[resources.lighting_specular.index].first_pass == 2u;
ok = ok && result.texture_lifetimes[resources.lighting_specular.index].last_pass == 3u;
ok = ok && result.texture_lifetimes[resources.scene_hdr_linear.index].first_pass == 3u;
ok = ok && result.texture_lifetimes[resources.scene_hdr_linear.index].last_pass == 4u;
```

Update transition count expectations to match the new pass:

```cpp
ok = ok && result.pass_barriers[1].transitions.size() == 3u;
ok = ok && result.pass_barriers[2].transitions.size() >= 9u;
ok = ok && result.pass_barriers[3].transitions.size() == 3u;
ok = ok && result.pass_barriers[4].transitions.size() == 2u;
```

- [ ] **Step 3: Run self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits `0`; the deferred graph test now verifies AO producer/consumer lifetime.

- [ ] **Step 4: Commit graph contract task**

Run:

```bash
git add project/src/engine/Function/Render/SceneDeferredGraphResources.h project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add ambient occlusion render graph contract"
```

---

### Task 3: Add AmbientOcclusionPass Facade And Resource Ownership

**Files:**
- Create: `project/src/engine/Function/Render/AmbientOcclusionPass.h`
- Create: `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`

- [ ] **Step 1: Create the pass header**

Create `project/src/engine/Function/Render/AmbientOcclusionPass.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/AmbientOcclusionConfig.h"
#include "Function/Render/RenderGraphFwd.h"
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class RenderTarget;
	class Renderer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	class AmbientOcclusionPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		RenderGraphTextureRef add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		GraphicsProgram* select_program() const;

		Renderer* m_renderer = nullptr;
		AmbientOcclusionConfig m_config{};
		std::unique_ptr<GraphicsProgram> m_ssao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_hbao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_gtao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_blur_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderTarget> m_neutral_ao_texture = nullptr;
	};
}
```

- [ ] **Step 2: Create the pass implementation**

Create `project/src/engine/Function/Render/AmbientOcclusionPass.cpp` with these core pieces:

```cpp
#include "Function/Render/AmbientOcclusionPass.h"

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
#include <array>
#include <cstring>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_ao_common_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli";
		static constexpr const char* k_ao_ssao_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl";
		static constexpr const char* k_ao_hbao_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionHBAO.hlsl";
		static constexpr const char* k_ao_gtao_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionGTAO.hlsl";
		static constexpr const char* k_ao_blur_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl";
		static constexpr RenderColorValue k_ao_clear_color{ 1.0f, 1.0f, 1.0f, 1.0f };

		struct AmbientOcclusionRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 ao_params0{ 1.5f, 1.0f, 1.0f, 0.0f };
			glm::vec4 ao_params1{ 8.0f, 4.0f, 4.0f, 0.02f };
		};

		static_assert(sizeof(AmbientOcclusionRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto sample_count_for_quality(AmbientOcclusionQuality quality) -> float
		{
			switch (quality)
			{
			case AmbientOcclusionQuality::Low:
				return 6.0f;
			case AmbientOcclusionQuality::High:
				return 16.0f;
			case AmbientOcclusionQuality::Medium:
			default:
				return 10.0f;
			}
		}

		auto direction_count_for_quality(AmbientOcclusionQuality quality) -> float
		{
			switch (quality)
			{
			case AmbientOcclusionQuality::Low:
				return 4.0f;
			case AmbientOcclusionQuality::High:
				return 8.0f;
			case AmbientOcclusionQuality::Medium:
			default:
				return 6.0f;
			}
		}

		auto build_ao_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_ao_common_shader_path);
			return hash_value;
		}

		auto make_ao_program_desc(const char* shader_path, const char* name, const GraphicsProgramState& state) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_ao_shader_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}

		void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext& view_context)
		{
			draw_desc.reverse_z = view_context.reverse_z;
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

		void attach_root_constants(GraphicsDrawDesc& draw_desc, GraphicsProgram* program, const AmbientOcclusionRootConstants& constants)
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

		auto make_root_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target,
			const AmbientOcclusionConfig& config) -> AmbientOcclusionRootConstants
		{
			AmbientOcclusionRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			const float width = output_target ? static_cast<float>(output_target->get_width()) : 1.0f;
			const float height = output_target ? static_cast<float>(output_target->get_height()) : 1.0f;
			constants.viewport_size = {
				std::max(width, 1.0f),
				std::max(height, 1.0f),
				1.0f / std::max(width, 1.0f),
				1.0f / std::max(height, 1.0f)
			};
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, frame.reverse_z ? 1.0f : 0.0f);
			constants.ao_params0 = glm::vec4(config.radius, config.intensity, config.power, static_cast<float>(config.mode));
			constants.ao_params1 = glm::vec4(
				sample_count_for_quality(config.quality),
				direction_count_for_quality(config.quality),
				config.quality == AmbientOcclusionQuality::High ? 6.0f : 4.0f,
				0.02f);
			return constants;
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const AmbientOcclusionRootConstants& constants,
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
}
```

Then implement member functions below the namespace helper block:

```cpp
bool AmbientOcclusionPass::initialize(Renderer* renderer)
{
	ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::initialize", AshEngine::Profile::Color::Scene);
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
	ASH_PROCESS_ERROR(renderer != nullptr);
	m_renderer = renderer;
	m_config = get_runtime_ambient_occlusion_config();
	ASH_PROCESS_ERROR(create_resources(*renderer));
	ASH_PROCESS_ERROR(create_programs(*renderer));
	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}

void AmbientOcclusionPass::shutdown()
{
	m_blur_program.reset();
	m_gtao_program.reset();
	m_hbao_program.reset();
	m_ssao_program.reset();
	m_point_clamp_sampler.reset();
	m_neutral_ao_texture.reset();
	m_renderer = nullptr;
}
```

- [ ] **Step 3: Add resource creation**

In `AmbientOcclusionPass.cpp`, implement:

```cpp
bool AmbientOcclusionPass::create_resources(Renderer& renderer)
{
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

	RenderSamplerDesc sampler_desc{};
	sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
	sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
	sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
	sampler_desc.min_filter = RenderSamplerFilter::Nearest;
	sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
	sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
	m_point_clamp_sampler = renderer.create_sampler(sampler_desc, "SceneAmbientOcclusionPointClampSampler");
	ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

	const std::array<uint8_t, 4> neutral_pixel{ 255u, 255u, 255u, 255u };
	TextureUploadDesc neutral_desc{};
	neutral_desc.width = 1;
	neutral_desc.height = 1;
	neutral_desc.format = RenderTextureFormat::RGBA8_UNORM;
	neutral_desc.initial_data = neutral_pixel.data();
	neutral_desc.row_pitch = 4u;
	neutral_desc.mip_level_count = 1u;
	neutral_desc.srgb = false;
	neutral_desc.name = "SceneAmbientOcclusionNeutral";
	m_neutral_ao_texture = renderer.create_texture_2d(neutral_desc);
	ASH_PROCESS_ERROR(m_neutral_ao_texture != nullptr);

	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

- [ ] **Step 4: Add program creation and mode selection**

In `AmbientOcclusionPass.cpp`, implement:

```cpp
bool AmbientOcclusionPass::create_programs(Renderer& renderer)
{
	ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::create_programs", AshEngine::Profile::Color::Pipeline);
	ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

	GraphicsProgramState fullscreen_state{};
	fullscreen_state.cull_mode = RenderCullMode::None;
	fullscreen_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
	fullscreen_state.depth_test = false;
	fullscreen_state.depth_write = false;
	fullscreen_state.blend_mode = RenderBlendMode::Opaque;

	if (m_config.mode == AmbientOcclusionMode::SSAO)
	{
		m_ssao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_ssao_shader_path, "SceneAmbientOcclusionSSAO", fullscreen_state));
		ASH_PROCESS_ERROR(m_ssao_program != nullptr);
	}
	else if (m_config.mode == AmbientOcclusionMode::HBAO)
	{
		m_hbao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_hbao_shader_path, "SceneAmbientOcclusionHBAO", fullscreen_state));
		ASH_PROCESS_ERROR(m_hbao_program != nullptr);
	}
	else if (m_config.mode == AmbientOcclusionMode::GTAO)
	{
		m_gtao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_gtao_shader_path, "SceneAmbientOcclusionGTAO", fullscreen_state));
		ASH_PROCESS_ERROR(m_gtao_program != nullptr);
	}

	if (m_config.mode != AmbientOcclusionMode::Off && m_config.blur)
	{
		m_blur_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_blur_shader_path, "SceneAmbientOcclusionBlur", fullscreen_state));
		ASH_PROCESS_ERROR(m_blur_program != nullptr);
	}

	ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}

GraphicsProgram* AmbientOcclusionPass::select_program() const
{
	switch (m_config.mode)
	{
	case AmbientOcclusionMode::SSAO:
		return m_ssao_program.get();
	case AmbientOcclusionMode::HBAO:
		return m_hbao_program.get();
	case AmbientOcclusionMode::GTAO:
		return m_gtao_program.get();
	case AmbientOcclusionMode::Off:
	default:
		return nullptr;
	}
}
```

- [ ] **Step 5: Add graph pass registration**

In `AmbientOcclusionPass.cpp`, implement:

```cpp
RenderGraphTextureRef AmbientOcclusionPass::add_passes(
	RenderGraphBuilder& graph,
	const VisibleRenderFrame& frame,
	const SceneDeferredGraphResources& deferred_resources,
	const SceneRenderViewContext& view_context)
{
	ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::add_passes", AshEngine::Profile::Color::Scene);
	ASH_PROCESS_GUARD_RETURN(RenderGraphTextureRef, result, {}, {});
	ASH_PROCESS_ERROR(m_renderer != nullptr);
	ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
	ASH_PROCESS_ERROR(m_neutral_ao_texture != nullptr);
	ASH_PROCESS_ERROR(view_context.output_target != nullptr);

	if (m_config.mode == AmbientOcclusionMode::Off)
	{
		result = graph.register_external_texture(m_neutral_ao_texture, "SceneAmbientOcclusionNeutral", RenderGraphAccess::GraphicsSRV);
		break;
	}

	ASH_PROCESS_ERROR(deferred_resources.depth);
	ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
	GraphicsProgram* ao_program = select_program();
	ASH_PROCESS_ERROR(ao_program != nullptr);

	const uint32_t resolution_divisor = m_config.half_resolution ? 2u : 1u;
	const uint16_t width = static_cast<uint16_t>(std::max<uint32_t>(view_context.output_target->get_width() / resolution_divisor, 1u));
	const uint16_t height = static_cast<uint16_t>(std::max<uint32_t>(view_context.output_target->get_height() / resolution_divisor, 1u));
	RenderGraphTextureDesc ao_desc{};
	ao_desc.width = width;
	ao_desc.height = height;
	ao_desc.format = RenderTextureFormat::RGBA8_UNORM;
	ao_desc.shader_resource = true;
	ao_desc.unordered_access = false;
	ao_desc.use_optimized_clear_value = true;
	ao_desc.optimized_clear_color = k_ao_clear_color;

	RenderGraphTextureRef raw_ao = graph.create_texture(ao_desc, m_config.blur ? "SceneAmbientOcclusionRaw" : "SceneAmbientOcclusion");
	RenderGraphTextureRef final_ao = raw_ao;
	if (m_config.blur)
	{
		final_ao = graph.create_texture(ao_desc, "SceneAmbientOcclusion");
	}

	ASH_PROCESS_ERROR(graph.add_raster_pass(
		"SceneAmbientOcclusionPass",
		RenderGraphPassFlags::None,
		[&](RenderGraphRasterPassBuilder& pass)
		{
			pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
			pass.read_texture(deferred_resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
			pass.write_color(0, raw_ao, RenderLoadAction::Clear, k_ao_clear_color);
		},
		[this, &frame, &deferred_resources, &view_context, raw_ao, ao_program](RenderGraphRasterContext& context) -> bool
		{
			ASH_PROFILE_SCOPE_NC("SceneAmbientOcclusionPass", AshEngine::Profile::Color::Draw);
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
			std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(deferred_resources.gbuffer_targets[4]);
			std::shared_ptr<RenderTarget> output = context.get_texture(raw_ao);
			ASH_PROCESS_ERROR(depth && gbuffer_e && output);
			ASH_PROCESS_ERROR(ao_program->set_texture("SceneDepth", depth));
			ASH_PROCESS_ERROR(ao_program->set_texture("SceneGBufferE", gbuffer_e));
			ASH_PROCESS_ERROR(ao_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
			ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
				ao_program,
				make_root_constants(frame, output, m_config),
				view_context)));
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}));

	if (m_config.blur)
	{
		ASH_PROCESS_ERROR(m_blur_program != nullptr);
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneAmbientOcclusionBlurPass",
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(raw_ao, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, final_ao, RenderLoadAction::Clear, k_ao_clear_color);
			},
			[this, &frame, &deferred_resources, &view_context, raw_ao, final_ao](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneAmbientOcclusionBlurPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> input = context.get_texture(raw_ao);
				std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
				std::shared_ptr<RenderTarget> output = context.get_texture(final_ao);
				ASH_PROCESS_ERROR(input && depth && output);
				ASH_PROCESS_ERROR(m_blur_program->set_texture("SceneAmbientOcclusionInput", input));
				ASH_PROCESS_ERROR(m_blur_program->set_texture("SceneDepth", depth));
				ASH_PROCESS_ERROR(m_blur_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_blur_program.get(),
					make_root_constants(frame, output, m_config),
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));
	}

	result = final_ao;
	ASH_PROCESS_GUARD_RETURN_END(result, {});
}
```

- [ ] **Step 6: Build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: compile fails only because AO shader files do not exist yet. If C++ compile fails before shader file access, fix the C++ errors before moving to Task 4.

- [ ] **Step 7: Commit pass facade if C++ compiles up to shader file dependency**

Run:

```bash
git add project/src/engine/Function/Render/AmbientOcclusionPass.h project/src/engine/Function/Render/AmbientOcclusionPass.cpp
git commit -m "Add ambient occlusion render pass facade"
```

---

### Task 4: Add AO Shaders For SSAO, HBAO, GTAO, And Blur

**Files:**
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli`
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl`
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionHBAO.hlsl`
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionGTAO.hlsl`
- Create: `project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl`

- [ ] **Step 1: Create AO common shader include**

Create `project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli`:

```hlsl
Texture2D<float> SceneDepth : register(t0);
Texture2D<float4> SceneGBufferE : register(t1);
Texture2D<float4> SceneAmbientOcclusionInput : register(t2);
SamplerState ScenePointClampSampler : register(s0);

struct VSFullscreenOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4 AshViewportSize;
    float4 AshCameraPositionAndFlags;
    float4 AshAOParams0;
    float4 AshAOParams1;
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

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

bool AshAOIsReverseZ()
{
    return AshCameraPositionAndFlags.w > 0.5;
}

bool AshAOSceneDepthIsBackground(float depth)
{
    return AshAOIsReverseZ() ? depth <= 0.000001 : depth >= 0.999999;
}

float3 AshAODecodeNormalOct(float2 encoded)
{
    float2 f = encoded * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += lerp(t.xx, -t.xx, step(0.0.xx, n.xy));
    return normalize(n);
}

float3 AshAOReconstructWorldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    return world.xyz / max(world.w, 1e-6);
}

struct AshAOSurface
{
    bool valid;
    float depth;
    float3 position_ws;
    float3 normal_ws;
};

AshAOSurface AshAOLoadSurface(float2 uv)
{
    AshAOSurface surface;
    surface.valid = false;
    surface.depth = SceneDepth.SampleLevel(ScenePointClampSampler, uv, 0);
    surface.position_ws = 0.0.xxx;
    surface.normal_ws = float3(0.0, 0.0, 1.0);
    if (AshAOSceneDepthIsBackground(surface.depth))
    {
        return surface;
    }

    const float4 gbuffer_e = SceneGBufferE.SampleLevel(ScenePointClampSampler, uv, 0);
    surface.position_ws = AshAOReconstructWorldPosition(uv, surface.depth);
    surface.normal_ws = AshAODecodeNormalOct(gbuffer_e.rg);
    surface.valid = true;
    return surface;
}

float AshAOViewScaledRadiusUv(AshAOSurface surface)
{
    const float radius = AshAOParams0.x;
    const float view_distance = max(length(surface.position_ws - AshCameraPositionAndFlags.xyz), 0.5);
    return saturate(radius / view_distance) * 0.25;
}

float AshAOApplyCurve(float occlusion)
{
    const float intensity = AshAOParams0.y;
    const float power_value = max(AshAOParams0.z, 0.05);
    return pow(saturate(1.0 - occlusion * intensity), power_value);
}

float AshAOContribution(AshAOSurface center, AshAOSurface sample_surface)
{
    if (!sample_surface.valid)
    {
        return 0.0;
    }
    const float3 delta = sample_surface.position_ws - center.position_ws;
    const float distance_ws = length(delta);
    const float radius = max(AshAOParams0.x, 0.05);
    if (distance_ws <= 0.0001 || distance_ws > radius)
    {
        return 0.0;
    }
    const float3 direction_ws = delta / distance_ws;
    const float facing = saturate(dot(center.normal_ws, direction_ws) - AshAOParams1.w);
    const float falloff = saturate(1.0 - distance_ws / radius);
    return facing * falloff;
}

float2 AshAORotate(float2 value, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(value.x * c - value.y * s, value.x * s + value.y * c);
}
```

- [ ] **Step 2: Create SSAO shader**

Create `project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl`:

```hlsl
#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshAOSurface center = AshAOLoadSurface(input.uv);
    if (!center.valid)
    {
        return 1.0.xxxx;
    }

    static const float2 k_offsets[16] = {
        float2( 0.5381,  0.1856), float2(-0.4319,  0.3416),
        float2( 0.1197, -0.4580), float2(-0.7935, -0.0978),
        float2( 0.2788,  0.6997), float2(-0.2287, -0.7863),
        float2( 0.7022, -0.4103), float2(-0.5174,  0.8041),
        float2( 0.9251,  0.1247), float2(-0.1246, -0.9321),
        float2( 0.3994, -0.1412), float2(-0.3319,  0.0440),
        float2( 0.0472,  0.3151), float2(-0.6331, -0.4828),
        float2( 0.8121,  0.5713), float2(-0.9175,  0.2976)
    };

    const uint sample_count = (uint)clamp(round(AshAOParams1.x), 1.0, 16.0);
    const float radius_uv = AshAOViewScaledRadiusUv(center);
    float occlusion = 0.0;
    for (uint i = 0; i < sample_count; ++i)
    {
        const float2 sample_uv = saturate(input.uv + k_offsets[i] * radius_uv);
        AshAOSurface sample_surface = AshAOLoadSurface(sample_uv);
        occlusion += AshAOContribution(center, sample_surface);
    }

    const float ao = AshAOApplyCurve(occlusion / max((float)sample_count, 1.0));
    return float4(ao, ao, ao, 1.0);
}
```

- [ ] **Step 3: Create HBAO shader**

Create `project/src/engine/Shaders/Deferred/AmbientOcclusionHBAO.hlsl`:

```hlsl
#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshAOSurface center = AshAOLoadSurface(input.uv);
    if (!center.valid)
    {
        return 1.0.xxxx;
    }

    const uint direction_count = (uint)clamp(round(AshAOParams1.y), 2.0, 8.0);
    const uint step_count = (uint)clamp(round(AshAOParams1.z), 2.0, 6.0);
    const float radius_uv = AshAOViewScaledRadiusUv(center);
    float occlusion = 0.0;

    for (uint direction_index = 0; direction_index < direction_count; ++direction_index)
    {
        const float angle = ((float)direction_index + 0.5) * 6.2831853 / (float)direction_count;
        const float2 direction_uv = float2(cos(angle), sin(angle));
        float horizon = 0.0;
        for (uint step_index = 1; step_index <= step_count; ++step_index)
        {
            const float step_ratio = (float)step_index / (float)step_count;
            const float2 sample_uv = saturate(input.uv + direction_uv * radius_uv * step_ratio);
            AshAOSurface sample_surface = AshAOLoadSurface(sample_uv);
            horizon = max(horizon, AshAOContribution(center, sample_surface));
        }
        occlusion += horizon;
    }

    const float ao = AshAOApplyCurve(occlusion / max((float)direction_count, 1.0));
    return float4(ao, ao, ao, 1.0);
}
```

- [ ] **Step 4: Create GTAO shader**

Create `project/src/engine/Shaders/Deferred/AmbientOcclusionGTAO.hlsl`:

```hlsl
#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshAOSurface center = AshAOLoadSurface(input.uv);
    if (!center.valid)
    {
        return 1.0.xxxx;
    }

    const uint slice_count = (uint)clamp(round(AshAOParams1.y), 2.0, 8.0);
    const uint step_count = (uint)clamp(round(AshAOParams1.z), 2.0, 6.0);
    const float radius_uv = AshAOViewScaledRadiusUv(center);
    float visibility_loss = 0.0;

    for (uint slice_index = 0; slice_index < slice_count; ++slice_index)
    {
        const float angle = ((float)slice_index + 0.25) * 3.14159265 / (float)slice_count;
        const float2 axis = float2(cos(angle), sin(angle));
        float slice_occlusion = 0.0;
        [unroll]
        for (int side = -1; side <= 1; side += 2)
        {
            float horizon = 0.0;
            for (uint step_index = 1; step_index <= step_count; ++step_index)
            {
                const float step_ratio = (float)step_index / (float)step_count;
                const float2 sample_uv = saturate(input.uv + axis * (float)side * radius_uv * step_ratio);
                AshAOSurface sample_surface = AshAOLoadSurface(sample_uv);
                horizon = max(horizon, AshAOContribution(center, sample_surface));
            }
            slice_occlusion += horizon;
        }
        visibility_loss += slice_occlusion * 0.5;
    }

    const float ao = AshAOApplyCurve(visibility_loss / max((float)slice_count, 1.0));
    return float4(ao, ao, ao, 1.0);
}
```

- [ ] **Step 5: Create blur shader**

Create `project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl`:

```hlsl
#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float center_depth = SceneDepth.SampleLevel(ScenePointClampSampler, input.uv, 0);
    if (AshAOSceneDepthIsBackground(center_depth))
    {
        return 1.0.xxxx;
    }

    float weighted_sum = 0.0;
    float weight_sum = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const float2 sample_uv = saturate(input.uv + float2((float)x, (float)y) * AshViewportSize.zw);
            const float sample_depth = SceneDepth.SampleLevel(ScenePointClampSampler, sample_uv, 0);
            const float ao = SceneAmbientOcclusionInput.SampleLevel(ScenePointClampSampler, sample_uv, 0).r;
            const float depth_weight = saturate(1.0 - abs(sample_depth - center_depth) * 64.0);
            const float spatial_weight = 1.0 / (1.0 + abs((float)x) + abs((float)y));
            const float weight = depth_weight * spatial_weight;
            weighted_sum += ao * weight;
            weight_sum += weight;
        }
    }

    const float blurred = weighted_sum / max(weight_sum, 1e-5);
    return float4(blurred, blurred, blurred, 1.0);
}
```

- [ ] **Step 6: Build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build succeeds, or shader reflection errors name the specific missing/invalid binding. Fix shader binding names to exactly match C++: `SceneDepth`, `SceneGBufferE`, `SceneAmbientOcclusionInput`, `ScenePointClampSampler`, `AshRootConstants`.

- [ ] **Step 7: Commit shader task**

Run:

```bash
git add project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl project/src/engine/Shaders/Deferred/AmbientOcclusionHBAO.hlsl project/src/engine/Shaders/Deferred/AmbientOcclusionGTAO.hlsl project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl
git commit -m "Add ambient occlusion shaders"
```

---

### Task 5: Bind AO Into Deferred Lighting

**Files:**
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Modify: `project/src/engine/Shaders/Deferred/DeferredCommon.hlsli`

- [ ] **Step 1: Add AO resource to `DeferredCommon.hlsli`**

In `project/src/engine/Shaders/Deferred/DeferredCommon.hlsli`, add after `SceneLightingSpecular`:

```hlsl
Texture2D<float4> SceneAmbientOcclusion : register(t8);
```

Extend `AshDeferredSurface`:

```hlsl
float material_ao;
float screen_ao;
float ao;
```

In `AshDecodeDeferredSurface`, initialize:

```hlsl
surface.material_ao = 1.0;
surface.screen_ao = 1.0;
surface.ao = 1.0;
```

Replace the current AO assignment:

```hlsl
surface.material_ao = saturate(gbuffer_b.b);
surface.screen_ao = saturate(SceneAmbientOcclusion.Sample(ScenePointClampSampler, uv).r);
surface.ao = surface.material_ao * surface.screen_ao;
```

In `AshEvaluateBaseEmissive_Split`, preserve unlit behavior by using material AO only:

```hlsl
if (surface.shading_model == ASH_SHADING_MODEL_UNLIT)
{
    result.diffuse = surface.base_color * surface.material_ao + surface.emissive;
    return result;
}
```

- [ ] **Step 2: Bind AO texture in deferred lighting pass setup**

In `DeferredLightingPass::add_passes()`, add validation:

```cpp
ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
```

In `SceneDeferredLightingAccumPass` setup, add:

```cpp
pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
```

In the execute lambda, fetch AO:

```cpp
std::shared_ptr<RenderTarget> ambient_occlusion = context.get_texture(deferred_resources.ambient_occlusion);
ASH_PROCESS_ERROR(gbuffer_a && gbuffer_b && gbuffer_c && gbuffer_d && gbuffer_e && depth && ambient_occlusion && output);
```

Inside the program binding loop, add:

```cpp
ASH_PROCESS_ERROR(program->set_texture("SceneAmbientOcclusion", ambient_occlusion));
```

- [ ] **Step 3: Build**

Run:

```powershell
./build_sandbox.bat Debug x64
```

Expected: build succeeds. Shader reflection should consume `SceneAmbientOcclusion` for base/emissive, directional, point, and spot programs.

- [ ] **Step 4: Commit lighting task**

Run:

```bash
git add project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Shaders/Deferred/DeferredCommon.hlsli
git commit -m "Apply ambient occlusion in deferred lighting"
```

---

### Task 6: Wire AmbientOcclusionPass Into SceneRenderer

**Files:**
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`

- [ ] **Step 1: Add pass member**

In `SceneRenderer.h`, include:

```cpp
#include "Function/Render/AmbientOcclusionPass.h"
```

Add member before `DeferredLightingPass`:

```cpp
AmbientOcclusionPass m_ambient_occlusion_pass{};
```

- [ ] **Step 2: Initialize and shutdown AO pass**

In `SceneRenderer::initialize()` after `m_renderer` validation and before deferred lighting initialize:

```cpp
ASH_PROCESS_ERROR(m_ambient_occlusion_pass.initialize(m_renderer));
```

In `SceneRenderer::shutdown()` before `m_renderer = nullptr`:

```cpp
m_ambient_occlusion_pass.shutdown();
```

- [ ] **Step 3: Insert AO between GBuffer and lighting**

In `SceneRenderer::render_visible_frame()`, after `SceneGBufferPass` registration and before `m_deferred_lighting_pass.add_passes()`, add:

```cpp
graph_resources.ambient_occlusion = m_ambient_occlusion_pass.add_passes(
	graph,
	frame,
	graph_resources,
	view_context);
ASH_PROCESS_ERROR(graph_resources.ambient_occlusion);
```

No other `SceneRenderer` code should branch on AO mode.

- [ ] **Step 4: Build and run self-test**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits `0`.

- [ ] **Step 5: Commit scene renderer task**

Run:

```bash
git add project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp
git commit -m "Insert ambient occlusion into scene renderer"
```

---

### Task 7: Add Runtime Config Defaults And Documentation

**Files:**
- Modify: `product/config/Engine.ini`
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `docs/RenderGraphAPISpec.md`

- [ ] **Step 1: Add default AO config section without disturbing existing config**

Open `product/config/Engine.ini` and preserve current `[RHI]`, `[VulkanValidation]`, and `[DX12Validation]` values. Add this section if it is not already present:

```ini
[AmbientOcclusion]
Mode=Off
Quality=Medium
Radius=1.5
Intensity=1.0
Power=1.0
HalfResolution=false
Blur=true
```

- [ ] **Step 2: Update README current rendering summary**

In `README.md`, update the deferred path bullet to mention AO:

```markdown
- DeferredHQ GBuffer 静态网格路径：第一版使用 5 张 GBuffer（三张 `RGBA8_UNORM`、两张 `RGBA16_SFLOAT`）加 D32 depth；可选 `SceneAmbientOcclusionPass` 会在 GBuffer 与 deferred lighting 之间生成统一 AO texture（`Off` / `SSAO` / `HBAO` / `GTAO` 由 `Engine.ini` 控制），随后 deferred lighting、composite 和 tone-map 完成最终输出。
```

Add the implementation plan link near the AO design link:

```markdown
- 环境光遮蔽（AO）实现计划：[`docs/superpowers/plans/2026-05-20-ambient-occlusion-implementation.md`](docs/superpowers/plans/2026-05-20-ambient-occlusion-implementation.md)
```

- [ ] **Step 3: Update EngineDeveloperGuide**

In `docs/EngineDeveloperGuide.md`, update the render switch/config section to include:

```markdown
### Ambient Occlusion 配置

屏幕空间 AO 由 `[AmbientOcclusion]` 配置控制，运行时在 `Application::initialize()` 中加载并发布给 render path：

```ini
[AmbientOcclusion]
Mode=Off
Quality=Medium
Radius=1.5
Intensity=1.0
Power=1.0
HalfResolution=false
Blur=true
```

`Mode` 支持 `Off`、`SSAO`、`HBAO`、`GTAO`。默认 `Off` 会让 `AmbientOcclusionPass` 注册一张 neutral 1x1 white AO texture，保证 deferred lighting 始终绑定同一个 `SceneAmbientOcclusion` shader resource。
```

Update the deferred path section to:

```markdown
SceneGBufferPass -> SceneAmbientOcclusionPass -> SceneDeferredLightingAccumPass -> SceneDeferredCompositePass -> SceneDeferredToneMapPass
```

- [ ] **Step 4: Update RenderGraphAPISpec deferred chain**

In `docs/RenderGraphAPISpec.md`, update the deferred graph example to include:

```cpp
// 2. Ambient Occlusion
pass.read_texture(depth, RenderGraphAccess::GraphicsSRV);
pass.read_texture(gbuffer_e, RenderGraphAccess::GraphicsSRV);
pass.write_color(0, ambient_occlusion, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });

// 3. Lighting
pass.read_texture(ambient_occlusion, RenderGraphAccess::GraphicsSRV);
```

Renumber the later composite/tone-map comments in that section.

- [ ] **Step 5: Build docs-only-safe code state**

Run:

```powershell
./build_sandbox.bat Debug x64
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds and self-test exits `0`.

- [ ] **Step 6: Commit docs/config task**

Run:

```bash
git add product/config/Engine.ini README.md docs/EngineDeveloperGuide.md docs/RenderGraphAPISpec.md
git commit -m "Document ambient occlusion runtime path"
```

---

### Task 8: Validate All AO Modes On Vulkan And DX12

**Files:**
- No source changes unless validation exposes a bug.
- Generated logs go under `Intermediate/logs`.

- [ ] **Step 1: Build Sandbox and Editor**

Run:

```powershell
./build_sandbox.bat Debug x64
./build_editor.bat Debug x64
```

Expected: both builds succeed and sync `Engine.dll` into `product/bin64/Debug-windows-x86_64/`.

- [ ] **Step 2: Run self-test**

Run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exits `0`.

- [ ] **Step 3: Validate Vulkan and DX12 for each AO mode**

For each mode `Off`, `SSAO`, `HBAO`, `GTAO`, update only `[AmbientOcclusion] Mode=<mode>` and use the validation-loop expectation:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=25
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=25
```

Run once with `[RHI] Backend=Vulkan` and once with `[RHI] Backend=DX12`.

Expected for every run:

- requested backend matches actual backend in logs.
- no Vulkan validation errors.
- no DX12 debug-layer errors.
- no RenderGraph compiler errors.
- no shader binding errors for `SceneAmbientOcclusion`.
- no shutdown resource leaks.

- [ ] **Step 4: Inspect logs**

Check latest logs under:

```text
product/logs/
```

Expected: no AO-related `HLogError`, no validation errors, and AO mode appears in config load logs.

- [ ] **Step 5: Commit validation fixes or final validation note**

If validation required fixes:

```bash
git add <fixed files>
git commit -m "Fix ambient occlusion validation issues"
```

If no source fixes were needed, do not create an empty commit.

---

## Self-Review

- Spec coverage:
  - Runtime `Off` / `SSAO` / `HBAO` / `GTAO`: Task 1, Task 3, Task 4, Task 7.
  - Unified `SceneAmbientOcclusion`: Task 2, Task 3, Task 5, Task 6.
  - Algorithm details isolated from `SceneRenderer` / `DeferredLightingPass`: Task 3, Task 5, Task 6.
  - Material AO multiplied by screen AO: Task 5.
  - Neutral `Off` path: Task 3, Task 5, Task 7.
  - Vulkan/DX12 validation: Task 8.
  - Documentation updates: Task 7.
- Red-flag scan:
  - No unresolved marker text or vague implementation-only task remains.
- Type consistency:
  - Config types use `AmbientOcclusionMode`, `AmbientOcclusionQuality`, and `AmbientOcclusionConfig` consistently.
  - Pass facade uses `AmbientOcclusionPass::add_passes()` and returns `RenderGraphTextureRef`.
  - Deferred resource field is consistently `ambient_occlusion`.
