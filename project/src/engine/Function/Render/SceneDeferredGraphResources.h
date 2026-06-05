#pragma once

#include "Function/Render/RenderGraphFwd.h"
#include <vector>

namespace AshEngine
{
	struct SceneDeferredGraphResources
	{
		std::vector<RenderGraphTextureRef> gbuffer_targets{};
		RenderGraphTextureRef depth{};
		RenderGraphTextureRef ambient_occlusion{};
		RenderGraphTextureRef lighting_diffuse{};
		RenderGraphTextureRef lighting_specular{};
		RenderGraphTextureRef scene_hdr_linear{};
		RenderGraphTextureRef volumetric_density{};
		RenderGraphTextureRef volumetric_scattering{};
		RenderGraphTextureRef volumetric_integrated_lighting{};
		RenderGraphTextureRef volumetric_history_validity{};
		RenderGraphTextureRef volumetric_composite_hdr{};
		RenderGraphTextureRef lightshaft_screen_space_mask{};
		RenderGraphTextureRef lightshaft_screen_space_final{};
		RenderGraphTextureRef sunlight_shadow_dynamic_atlas{};
		RenderGraphTextureRef sunlight_shadow_static_cache{};
		RenderGraphTextureRef sunlight_shadow_mask{};
		RenderGraphTextureRef sunlight_shadow_cascade_debug{};
		RenderGraphTextureRef directional_light_shadow_transient_atlas{};
		RenderGraphTextureRef directional_light_shadow_transient_mask{};
	};
}
