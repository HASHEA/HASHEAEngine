#pragma once

#include "Base/hcore.h"
#include "Function/Render/AmbientOcclusionConfig.h"
#include "Function/Render/BloomConfig.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/VolumetricLightingConfig.h"

namespace AshEngine
{
	struct ASH_API SceneRenderConfig
	{
		AmbientOcclusionConfig ambient_occlusion{};
		DirectionalShadowConfig directional_shadows{};
		BloomConfig bloom{};
		VolumetricLightingConfig volumetric_lighting{};
	};

	ASH_API SceneRenderConfig make_default_scene_render_config();
	ASH_API bool scene_render_config_equal(const SceneRenderConfig& lhs, const SceneRenderConfig& rhs);
}
