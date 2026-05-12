# Deferred GBuffer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first deferred `Surface.StaticMesh` GBuffer path with a fixed `DeferredHQ` layout while keeping user material shaders unaware of forward/deferred and MRT packing.

**Architecture:** Add a renderer-owned GBuffer layout registry that returns a `DeferredHQ` layout key and semantic map. Add a new `PassFamily::GBuffer` material permutation consumed only by the engine host shader. Route opaque and masked static meshes through a GBuffer MRT pass, then a simple fullscreen deferred lighting pass that writes the existing view output target; keep the forward path available as fallback.

**Tech Stack:** C++17, AshEngine Function Render layer, existing Renderer/RenderDevice pass model, HLSL + DXC, Vulkan and DX12 RHI backends.

---

## File Structure

- Create `project/src/engine/Function/Render/GBufferLayout.h`: public Function-layer GBuffer layout types and query helpers.
- Create `project/src/engine/Function/Render/GBufferLayout.cpp`: fixed `DeferredHQ` layout construction and hashing.
- Create `project/src/engine/Function/Render/SceneDeferredResources.h`: per-view GBuffer/depth resource holder.
- Create `project/src/engine/Function/Render/SceneDeferredResources.cpp`: render target creation, resize reuse, and attachment lookup.
- Create `project/src/engine/Function/Render/DeferredLightingPass.h`: deferred lighting pass facade.
- Create `project/src/engine/Function/Render/DeferredLightingPass.cpp`: fullscreen program creation, GBuffer binding, and output pass.
- Create `project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshGBuffer.hlsl`: static mesh GBuffer host shader.
- Create `project/src/engine/Shaders/Deferred/DeferredLighting.hlsl`: first fullscreen deferred lighting shader.
- Modify `project/src/engine/Function/Render/EngineShaderFamilyRegistry.h/.cpp`: add `PassFamily::GBuffer` and host path lookup.
- Modify `project/src/engine/Function/Render/MaterialShaderSourceBuilder.cpp`: generate GBuffer pass macros.
- Modify `project/src/engine/Function/Render/MaterialShaderMap.h/.cpp`: include GBuffer usage in permutation keys and pass relevance.
- Modify `project/src/engine/Function/Render/MaterialRenderProxy.h/.cpp`: prepare/cache GBuffer graphics program resources.
- Modify `project/src/engine/Function/Render/SceneRenderer.h/.cpp`: own deferred resources and submit GBuffer plus lighting.
- Modify `project/src/engine/Base/EngineSelfTests.cpp`: add headless tests for `DeferredHQ` layout semantics.
- Modify `README.md` and `docs/EngineDeveloperGuide.md`: document deferred layout and material boundary.

## Task 1: GBuffer Layout Foundation

**Files:**
- Create: `project/src/engine/Function/Render/GBufferLayout.h`
- Create: `project/src/engine/Function/Render/GBufferLayout.cpp`
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`

- [ ] **Step 1: Add the layout header**

Create `project/src/engine/Function/Render/GBufferLayout.h`:

```cpp
#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace AshEngine
{
	enum class SceneRenderFeature : uint64_t
	{
		None = 0,
		DeferredLighting = 1ull << 0,
		TemporalMotionVector3D = 1ull << 1,
		ExtendedMaterialData = 1ull << 2,
		HDRMaterialEmission = 1ull << 3,
		MaterialId = 1ull << 4,
		DebugGBuffer = 1ull << 5
	};

	enum class GBufferSemantic : uint8_t
	{
		BaseColor = 0,
		ShadingModelAndFlags,
		MetallicRoughnessAOAndSpecular,
		CustomData,
		MotionVector3D,
		TemporalFlags,
		NormalOct,
		EmissiveOrCustom
	};

	struct GBufferLayoutKey
	{
		uint64_t feature_flags = 0;
		uint32_t quality_tier = 0;
		uint32_t platform_tier = 0;

		bool operator==(const GBufferLayoutKey& rhs) const;
	};

	struct GBufferAttachmentDesc
	{
		std::string_view name{};
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		bool clear_to_zero = true;
	};

	struct GBufferSemanticMapping
	{
		GBufferSemantic semantic = GBufferSemantic::BaseColor;
		uint8_t attachment_index = 0;
		uint8_t component_mask = 0;
	};

	struct GBufferLayoutDesc
	{
		GBufferLayoutKey key{};
		uint64_t layout_hash = 0;
		std::vector<GBufferAttachmentDesc> attachments{};
		std::vector<GBufferSemanticMapping> semantic_mappings{};
	};

	ASH_API uint64_t make_scene_render_feature_flags(std::initializer_list<SceneRenderFeature> features);
	ASH_API GBufferLayoutKey make_deferred_hq_gbuffer_layout_key();
	ASH_API const GBufferLayoutDesc& get_deferred_hq_gbuffer_layout();
	ASH_API const GBufferSemanticMapping* find_gbuffer_semantic_mapping(
		const GBufferLayoutDesc& layout,
		GBufferSemantic semantic);
}
```

- [ ] **Step 2: Add the layout implementation**

Create `project/src/engine/Function/Render/GBufferLayout.cpp` with a deterministic fixed `DeferredHQ` layout:

```cpp
#include "Function/Render/GBufferLayout.h"

namespace AshEngine
{
	namespace
	{
		constexpr uint8_t k_component_rgb = 0x7u;
		constexpr uint8_t k_component_a = 0x8u;
		constexpr uint8_t k_component_rgba = 0xFu;
		constexpr uint8_t k_component_rg = 0x3u;
		constexpr uint8_t k_component_ba = 0xCu;

		uint64_t hash_layout(const GBufferLayoutDesc& layout)
		{
			uint64_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, layout.key.feature_flags);
			ASH_HASH::hash_combine(hash_value, layout.key.quality_tier);
			ASH_HASH::hash_combine(hash_value, layout.key.platform_tier);
			for (const GBufferAttachmentDesc& attachment : layout.attachments)
			{
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(attachment.format));
			}
			for (const GBufferSemanticMapping& mapping : layout.semantic_mappings)
			{
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(mapping.semantic));
				ASH_HASH::hash_combine(hash_value, mapping.attachment_index);
				ASH_HASH::hash_combine(hash_value, mapping.component_mask);
			}
			return hash_value;
		}

		GBufferLayoutDesc build_deferred_hq_layout()
		{
			GBufferLayoutDesc layout{};
			layout.key = make_deferred_hq_gbuffer_layout_key();
			layout.attachments = {
				{ "GBufferA", RenderTextureFormat::RGBA8_UNORM, true },
				{ "GBufferB", RenderTextureFormat::RGBA8_UNORM, true },
				{ "GBufferC", RenderTextureFormat::RGBA8_UNORM, true },
				{ "GBufferD", RenderTextureFormat::RGBA16_SFLOAT, true },
				{ "GBufferE", RenderTextureFormat::RGBA16_SFLOAT, true }
			};
			layout.semantic_mappings = {
				{ GBufferSemantic::BaseColor, 0, k_component_rgb },
				{ GBufferSemantic::ShadingModelAndFlags, 0, k_component_a },
				{ GBufferSemantic::MetallicRoughnessAOAndSpecular, 1, k_component_rgba },
				{ GBufferSemantic::CustomData, 2, k_component_rgba },
				{ GBufferSemantic::MotionVector3D, 3, k_component_rgb },
				{ GBufferSemantic::TemporalFlags, 3, k_component_a },
				{ GBufferSemantic::NormalOct, 4, k_component_rg },
				{ GBufferSemantic::EmissiveOrCustom, 4, k_component_ba }
			};
			layout.layout_hash = hash_layout(layout);
			return layout;
		}
	}

	bool GBufferLayoutKey::operator==(const GBufferLayoutKey& rhs) const
	{
		return feature_flags == rhs.feature_flags &&
			quality_tier == rhs.quality_tier &&
			platform_tier == rhs.platform_tier;
	}

	uint64_t make_scene_render_feature_flags(std::initializer_list<SceneRenderFeature> features)
	{
		uint64_t flags = 0;
		for (SceneRenderFeature feature : features)
		{
			flags |= static_cast<uint64_t>(feature);
		}
		return flags;
	}

	GBufferLayoutKey make_deferred_hq_gbuffer_layout_key()
	{
		return {
			make_scene_render_feature_flags({
				SceneRenderFeature::DeferredLighting,
				SceneRenderFeature::TemporalMotionVector3D,
				SceneRenderFeature::ExtendedMaterialData,
				SceneRenderFeature::HDRMaterialEmission
			}),
			1u,
			0u
		};
	}

	const GBufferLayoutDesc& get_deferred_hq_gbuffer_layout()
	{
		static const GBufferLayoutDesc layout = build_deferred_hq_layout();
		return layout;
	}

	const GBufferSemanticMapping* find_gbuffer_semantic_mapping(
		const GBufferLayoutDesc& layout,
		GBufferSemantic semantic)
	{
		for (const GBufferSemanticMapping& mapping : layout.semantic_mappings)
		{
			if (mapping.semantic == semantic)
			{
				return &mapping;
			}
		}
		return nullptr;
	}
}
```

- [ ] **Step 3: Add a headless layout self-test**

Modify `project/src/engine/Base/EngineSelfTests.cpp` includes:

```cpp
#include "Function/Render/GBufferLayout.h"
```

Add a test near the other render self-tests:

```cpp
auto test_deferred_hq_gbuffer_layout_contract() -> bool
{
	const GBufferLayoutDesc& layout = get_deferred_hq_gbuffer_layout();
	if (layout.attachments.size() != 5)
	{
		return report_self_test_failure("DeferredHQ GBuffer layout", "layout did not expose five color attachments");
	}
	if (layout.attachments[0].format != RenderTextureFormat::RGBA8_UNORM ||
		layout.attachments[1].format != RenderTextureFormat::RGBA8_UNORM ||
		layout.attachments[2].format != RenderTextureFormat::RGBA8_UNORM ||
		layout.attachments[3].format != RenderTextureFormat::RGBA16_SFLOAT ||
		layout.attachments[4].format != RenderTextureFormat::RGBA16_SFLOAT)
	{
		return report_self_test_failure("DeferredHQ GBuffer layout", "attachment formats changed unexpectedly");
	}

	const GBufferSemanticMapping* motion =
		find_gbuffer_semantic_mapping(layout, GBufferSemantic::MotionVector3D);
	const GBufferSemanticMapping* normal =
		find_gbuffer_semantic_mapping(layout, GBufferSemantic::NormalOct);
	const bool ok =
		layout.layout_hash != 0 &&
		motion && motion->attachment_index == 3 && motion->component_mask == 0x7u &&
		normal && normal->attachment_index == 4 && normal->component_mask == 0x3u;
	return ok ||
		report_self_test_failure("DeferredHQ GBuffer layout", "semantic mappings did not match the design contract");
}
```

Call it from `run_engine_base_self_tests()`:

```cpp
all_passed = test_deferred_hq_gbuffer_layout_contract() && all_passed;
```

- [ ] **Step 4: Build and run headless verification**

Run:

```powershell
cmd /c "build_sandbox.bat Debug x64"
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build exits `0`, self-test exits `0`.

- [ ] **Step 5: Commit Task 1**

```powershell
git add project/src/engine/Function/Render/GBufferLayout.h project/src/engine/Function/Render/GBufferLayout.cpp project/src/engine/Base/EngineSelfTests.cpp
git commit -m "Add DeferredHQ GBuffer layout contract"
```

## Task 2: Material Pass And Host Shader Plumbing

**Files:**
- Modify: `project/src/engine/Function/Render/EngineShaderFamilyRegistry.h`
- Modify: `project/src/engine/Function/Render/EngineShaderFamilyRegistry.cpp`
- Modify: `project/src/engine/Function/Render/MaterialShaderSourceBuilder.cpp`
- Modify: `project/src/engine/Function/Render/MaterialShaderMap.h`
- Modify: `project/src/engine/Function/Render/MaterialShaderMap.cpp`
- Create: `project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshGBuffer.hlsl`

- [ ] **Step 1: Add `GBuffer` to pass enum and family desc**

In `EngineShaderFamilyRegistry.h`, extend `PassFamily`:

```cpp
enum class PassFamily : uint8_t
{
	DepthOnly = 0,
	BasePass,
	GBuffer
};
```

Add `std::string gbuffer_shader_path{};` to `EngineShaderFamilyDesc`.

- [ ] **Step 2: Resolve the GBuffer host path**

In `EngineShaderFamilyRegistry.cpp`, update `resolve_host_shader_path` so `PassFamily::GBuffer` returns `gbuffer_shader_path`.

In registry initialization for `SurfaceStaticMesh`, set:

```cpp
family.gbuffer_shader_path =
	"project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshGBuffer.hlsl";
```

- [ ] **Step 3: Include GBuffer in material source usage names and macros**

In `MaterialShaderSourceBuilder.cpp`, update the usage name helper:

```cpp
case PassFamily::DepthOnly:
	return "SurfaceStaticMesh_DepthOnly";
case PassFamily::GBuffer:
	return "SurfaceStaticMesh_GBuffer";
case PassFamily::BasePass:
default:
	return "SurfaceStaticMesh_BasePass";
```

Add generated macros:

```cpp
bindings << "#define ASH_PASS_GBUFFER " << (usage.pass == PassFamily::GBuffer ? 1 : 0) << "\n";
if (usage.pass == PassFamily::GBuffer)
{
	bindings << "#define ASH_GBUFFER_LAYOUT_DEFERRED_HQ 1\n";
	bindings << "#define ASH_GBUFFER_OUTPUT_COUNT 5\n";
	bindings << "#define ASH_GBUFFER_HAS_MOTION_VECTOR_3D 1\n";
}
```

- [ ] **Step 4: Include GBuffer in material permutation hash**

In `MaterialShaderMap.h`, ensure `MaterialPermutationKeyHash` already hashes `usage.pass`. Add no new field in this task. If later layout hash is added to `MaterialUsageDesc`, update equality and hash in that same task.

- [ ] **Step 5: Mark pass relevance for GBuffer**

In `MaterialShaderMap.cpp`, when building `MaterialPassRelevance`, set a new field:

```cpp
resource.pass_relevance.supports_gbuffer_pass = usage.pass == PassFamily::GBuffer;
```

Add `bool supports_gbuffer_pass = false;` to `MaterialPassRelevance`.

- [ ] **Step 6: Add first GBuffer host shader**

Create `SurfaceStaticMeshGBuffer.hlsl` by copying the vertex input/output and material node setup from `SurfaceStaticMeshBasePass.hlsl`. Replace the pixel output with:

```hlsl
struct AshGBufferOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float4 target4 : SV_Target4;
};

inline float2 AshEncodeNormalOct(float3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1e-5);
    float2 encoded = normal.xy;
    if (normal.z < 0.0)
    {
        encoded = (1.0 - abs(encoded.yx)) * (encoded.xy >= 0.0 ? 1.0 : -1.0);
    }
    return encoded * 0.5 + 0.5;
}

inline AshGBufferOutput EncodeSurfaceStaticMeshGBuffer(AshPixelParameters params, AshPixelMainNode node)
{
#if ASH_MATERIAL_BLEND_MODE_MASKED
    if (node.opacity_mask < ASH_MATERIAL_ALPHA_CUTOFF)
    {
        discard;
    }
#endif

    AshGBufferOutput output;
    const float3 normal = EvaluateSurfaceStaticMeshNormal(params, node);
    output.target0 = float4(saturate(node.base_color), 0.0);
    output.target1 = float4(saturate(node.metallic), saturate(node.roughness), saturate(node.ambient_occlusion), 0.5);
    output.target2 = float4(0.0, 0.0, 0.0, 0.0);
    output.target3 = float4(0.0, 0.0, 0.0, 0.0);
    output.target4 = float4(AshEncodeNormalOct(normal), saturate(node.emissive.r), saturate(node.emissive.g));
    return output;
}

AshGBufferOutput PSMain(VSOutput input)
{
    AshPixelParameters params = BuildSurfaceStaticMeshPixelParameters(input);
    AshPixelMainNode node = AshInitializePixelMainNode();
    CalculatePixelMainNode(params, node);
    return EncodeSurfaceStaticMeshGBuffer(params, node);
}
```

This first shader intentionally writes zero motion vector and uses a simple emissive bootstrap value because previous-frame data is a later task.

- [ ] **Step 7: Build shader plumbing**

Run:

```powershell
cmd /c "build_sandbox.bat Debug x64"
```

Expected: build exits `0`.

- [ ] **Step 8: Commit Task 2**

```powershell
git add project/src/engine/Function/Render/EngineShaderFamilyRegistry.h project/src/engine/Function/Render/EngineShaderFamilyRegistry.cpp project/src/engine/Function/Render/MaterialShaderSourceBuilder.cpp project/src/engine/Function/Render/MaterialShaderMap.h project/src/engine/Function/Render/MaterialShaderMap.cpp project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshGBuffer.hlsl
git commit -m "Add Surface.StaticMesh GBuffer material pass"
```

## Task 3: MaterialRenderProxy GBuffer Resource

**Files:**
- Modify: `project/src/engine/Function/Render/MaterialRenderProxy.h`
- Modify: `project/src/engine/Function/Render/MaterialRenderProxy.cpp`

- [ ] **Step 1: Extend proxy state**

Add public getter in `MaterialRenderProxy.h`:

```cpp
const MaterialResource* get_surface_staticmesh_gbuffer_resource() const;
```

Add private members:

```cpp
const MaterialResource* m_surface_staticmesh_gbuffer_template = nullptr;
MaterialResource m_surface_staticmesh_gbuffer_resource{};
std::unique_ptr<GraphicsProgram> m_surface_staticmesh_gbuffer_program = nullptr;
```

- [ ] **Step 2: Request the GBuffer template**

In `ensure_v2_resource_templates()`, request:

```cpp
MaterialUsageDesc gbuffer_usage{};
gbuffer_usage.domain = MaterialDomain::Surface;
gbuffer_usage.family = EngineShaderFamily::SurfaceStaticMesh;
gbuffer_usage.pass = PassFamily::GBuffer;
gbuffer_usage.capability_mask = capability_mask;
m_surface_staticmesh_gbuffer_template =
	m_material_system->find_or_create_resource(*m_material, gbuffer_usage, &error);
ASH_PROCESS_ERROR(m_surface_staticmesh_gbuffer_template != nullptr);
```

- [ ] **Step 3: Create and bind the GBuffer program**

In `ensure_program(Renderer& renderer)`, mirror the base pass creation:

```cpp
if (!m_surface_staticmesh_gbuffer_program && m_surface_staticmesh_gbuffer_template)
{
	ASH_PROCESS_ERROR(create_v2_program_instance(
		*m_surface_staticmesh_gbuffer_template,
		renderer,
		m_surface_staticmesh_gbuffer_program));
	m_surface_staticmesh_gbuffer_resource = *m_surface_staticmesh_gbuffer_template;
	m_surface_staticmesh_gbuffer_resource.program = m_surface_staticmesh_gbuffer_program.get();
}
```

In `bind_v2_program_resources()`, apply the same binding logic to `m_surface_staticmesh_gbuffer_resource.program`.

- [ ] **Step 4: Add getter implementation**

```cpp
const MaterialResource* MaterialRenderProxy::get_surface_staticmesh_gbuffer_resource() const
{
	return m_surface_staticmesh_gbuffer_resource.program ? &m_surface_staticmesh_gbuffer_resource : nullptr;
}
```

- [ ] **Step 5: Build and smoke current forward path**

Run:

```powershell
cmd /c "build_sandbox.bat Debug x64"
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: build exits `0`, smoke exits `0`.

- [ ] **Step 6: Commit Task 3**

```powershell
git add project/src/engine/Function/Render/MaterialRenderProxy.h project/src/engine/Function/Render/MaterialRenderProxy.cpp
git commit -m "Prepare material proxies for GBuffer pass"
```

## Task 4: Deferred Frame Resources And Lighting Pass

**Files:**
- Create: `project/src/engine/Function/Render/SceneDeferredResources.h`
- Create: `project/src/engine/Function/Render/SceneDeferredResources.cpp`
- Create: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Create: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Create: `project/src/engine/Shaders/Deferred/DeferredLighting.hlsl`

- [ ] **Step 1: Add deferred resources holder**

Create `SceneDeferredResources.h`:

```cpp
#pragma once

#include "Function/Render/GBufferLayout.h"
#include <memory>
#include <vector>

namespace AshEngine
{
	class Renderer;
	class RenderTarget;

	class SceneDeferredResources
	{
	public:
		bool ensure(Renderer& renderer, uint32_t width, uint32_t height, const GBufferLayoutDesc& layout);
		void reset();

		const GBufferLayoutDesc* get_layout() const;
		const std::vector<std::shared_ptr<RenderTarget>>& get_gbuffer_targets() const;
		const std::shared_ptr<RenderTarget>& get_depth_target() const;

	private:
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		uint64_t m_layout_hash = 0;
		const GBufferLayoutDesc* m_layout = nullptr;
		std::vector<std::shared_ptr<RenderTarget>> m_gbuffer_targets{};
		std::shared_ptr<RenderTarget> m_depth_target = nullptr;
	};
}
```

- [ ] **Step 2: Implement resource creation**

Create `SceneDeferredResources.cpp` with `ensure()` creating one `RenderTargetDesc` per layout attachment. Use names `SceneGBufferA` through `SceneGBufferE`, `shader_resource=true`, `unordered_access=false`, `use_optimized_clear_value=true`, and zero clear color. Create depth as `RenderTextureFormat::D32_SFLOAT` with optimized depth clear `{1.0f, 0}`.

- [ ] **Step 3: Add deferred lighting shader**

Create `DeferredLighting.hlsl`:

```hlsl
Texture2D<float4> SceneGBufferA : register(t0);
Texture2D<float4> SceneGBufferB : register(t1);
Texture2D<float4> SceneGBufferC : register(t2);
Texture2D<float4> SceneGBufferD : register(t3);
Texture2D<float4> SceneGBufferE : register(t4);
Texture2D<float> SceneDepth : register(t5);
SamplerState SceneLinearClampSampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
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

    VSOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const float4 gbuffer_a = SceneGBufferA.Sample(SceneLinearClampSampler, input.uv);
    const float4 gbuffer_b = SceneGBufferB.Sample(SceneLinearClampSampler, input.uv);
    const float4 gbuffer_e = SceneGBufferE.Sample(SceneLinearClampSampler, input.uv);

    const float3 base_color = saturate(gbuffer_a.rgb);
    const float roughness = saturate(gbuffer_b.g);
    const float ao = saturate(gbuffer_b.b);
    const float3 emissive = float3(gbuffer_e.b, gbuffer_e.a, 0.0);
    const float diffuse = lerp(0.75, 1.0, 1.0 - roughness);
    return float4(saturate(base_color * diffuse * ao + emissive), 1.0);
}
```

- [ ] **Step 4: Add lighting pass facade**

Create `DeferredLightingPass.h/.cpp`. The pass owns one `std::unique_ptr<GraphicsProgram>`, creates it from `project/src/engine/Shaders/Deferred/DeferredLighting.hlsl`, binds five GBuffer targets and depth by name, binds default sampler, begins a pass with the view output target, and draws a fullscreen triangle with `vertex_count = 3`.

- [ ] **Step 5: Build**

Run:

```powershell
cmd /c "build_sandbox.bat Debug x64"
```

Expected: build exits `0`.

- [ ] **Step 6: Commit Task 4**

```powershell
git add project/src/engine/Function/Render/SceneDeferredResources.h project/src/engine/Function/Render/SceneDeferredResources.cpp project/src/engine/Function/Render/DeferredLightingPass.h project/src/engine/Function/Render/DeferredLightingPass.cpp project/src/engine/Shaders/Deferred/DeferredLighting.hlsl
git commit -m "Add deferred frame resources and lighting pass"
```

## Task 5: SceneRenderer Deferred Path

**Files:**
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`

- [ ] **Step 1: Add deferred members**

In `SceneRenderer.h`, include deferred headers and add:

```cpp
SceneDeferredResources m_deferred_resources{};
DeferredLightingPass m_deferred_lighting_pass{};
bool m_use_deferred_static_mesh_path = true;
```

- [ ] **Step 2: Initialize and shutdown lighting pass**

In `SceneRenderer::initialize(Renderer* renderer)`, after `m_renderer = renderer`, call:

```cpp
ASH_PROCESS_ERROR(m_deferred_lighting_pass.initialize(renderer));
```

In `shutdown()`, call:

```cpp
m_deferred_lighting_pass.shutdown();
m_deferred_resources.reset();
```

- [ ] **Step 3: Split static mesh submission helper**

Extract the current static mesh draw body into a private helper:

```cpp
bool render_static_meshes_to_pass(
	const VisibleRenderFrame& frame,
	const SceneRenderViewContext& view_context,
	Renderer::GraphicsPassContext& pass_context,
	PassFamily pass_family);
```

When `pass_family == PassFamily::GBuffer`, request `section.material_proxy->get_surface_staticmesh_gbuffer_resource()`. When `pass_family == PassFamily::BasePass`, keep the existing base pass getter.

- [ ] **Step 4: Build GBuffer pass desc**

In `render_visible_frame`, branch when `m_use_deferred_static_mesh_path` is true:

```cpp
const GBufferLayoutDesc& layout = get_deferred_hq_gbuffer_layout();
ASH_PROCESS_ERROR(m_deferred_resources.ensure(*m_renderer, output_width, output_height, layout));

PassDesc gbuffer_pass_desc{};
gbuffer_pass_desc.name = "SceneGBufferPass";
gbuffer_pass_desc.allow_reorder_draws = true;
for (const std::shared_ptr<RenderTarget>& target : m_deferred_resources.get_gbuffer_targets())
{
	gbuffer_pass_desc.color_attachments.push_back({ target, RenderLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 0.0f } });
}
gbuffer_pass_desc.depth_attachment = {
	m_deferred_resources.get_depth_target(),
	view_context.depth_load_action,
	view_context.depth_clear_value
};
```

Submit static mesh draws with `PassFamily::GBuffer`, end the pass, transition targets for sampling through the existing resource binding path, then call:

```cpp
ASH_PROCESS_ERROR(m_deferred_lighting_pass.render(
	*m_renderer,
	m_deferred_resources,
	view_context.output_target,
	view_context));
```

- [ ] **Step 5: Keep forward fallback intact**

Move the old current pass body into the `else` branch so disabling `m_use_deferred_static_mesh_path` still renders with `Surface.StaticMesh.BasePass`.

- [ ] **Step 6: Build and run DX12 smoke**

Run:

```powershell
cmd /c "build_sandbox.bat Debug x64"
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
cmd /c "set ASH_SANDBOX_MODEL=models/gltfs/DamagedHelmet/glTF/DamagedHelmet.gltf& product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5"
```

Expected: both smoke runs exit `0`, logs show `SceneGBufferPass` and deferred lighting pass creation.

- [ ] **Step 7: Commit Task 5**

```powershell
git add project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp
git commit -m "Route static mesh rendering through deferred GBuffer"
```

## Task 6: Documentation And Cross-Backend Validation

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Optional update: `docs/ScenePresentationSubsystemGuide.md` if the public scene submission flow description needs deferred-specific wording.

- [ ] **Step 1: Update README render status**

In `README.md`, update the rendering status bullets to say opaque/masked static mesh rendering can run through the DeferredHQ GBuffer path, with forward retained as fallback and transparent future path.

- [ ] **Step 2: Update EngineDeveloperGuide**

Add a section under the render stack describing:

```text
DeferredHQ GBuffer:
- User material shaders only fill material nodes.
- Engine host shaders own GBuffer encoding.
- Layout is selected by view-level feature flags.
- First preset uses five MRTs: three RGBA8 and two RGBA16F.
- MotionVector3D is screen-space xy plus z.
```

- [ ] **Step 3: Run path-limited diff check**

Run:

```powershell
git diff --check -- README.md docs\EngineDeveloperGuide.md project\src\engine\Function\Render project\src\engine\Shaders
```

Expected: no output, exit `0`.

- [ ] **Step 4: Run validation matrix**

Run:

```powershell
cmd /c "build_sandbox.bat Debug x64"
product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
cmd /c "set ASH_SANDBOX_MODEL=models/gltfs/DamagedHelmet/glTF/DamagedHelmet.gltf& product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5"
```

Then switch `product/config/Engine.ini` to Vulkan temporarily and run:

```powershell
product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
cmd /c "set ASH_SANDBOX_MODEL=models/gltfs/DamagedHelmet/glTF/DamagedHelmet.gltf& product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5"
```

Restore `product/config/Engine.ini` to its original backend value after Vulkan smoke.

Run Editor smoke on both backends:

```powershell
product\bin64\Debug-windows-x86_64\Editor.exe --smoke-test-seconds=5
```

Expected: all runs exit `0`; Vulkan logs report no live VMA allocations; DX12 logs contain no debug-layer error/corruption related to MRT, render pass formats, or PSO creation.

- [ ] **Step 5: Commit Task 6**

```powershell
git add README.md docs/EngineDeveloperGuide.md docs/ScenePresentationSubsystemGuide.md
git commit -m "Document deferred GBuffer render path"
```

## Self-Review Checklist

- Spec coverage: covered material boundary, dynamic layout model, DeferredHQ layout, shader generation, SceneRenderer flow, RHI risk, validation.
- Type consistency: `GBufferLayoutDesc`, `SceneDeferredResources`, `DeferredLightingPass`, `PassFamily::GBuffer`, and `MotionVector3D` names are consistent across tasks.
- Scope: plan implements fixed `DeferredHQ` first and leaves real previous-frame velocity as an explicit later implementation slice.
- Incomplete-content scan: no incomplete sections are required to execute the first deferred vertical slice.
