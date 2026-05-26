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
		return config;
	}

	bool scene_render_config_equal(const SceneRenderConfig& lhs, const SceneRenderConfig& rhs)
	{
		return ambient_occlusion_config_equal(lhs.ambient_occlusion, rhs.ambient_occlusion) &&
			directional_shadow_config_equal(lhs.directional_shadows, rhs.directional_shadows);
	}
}
