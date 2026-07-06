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

		auto bloom_stage_config_equal(const BloomStageConfig& lhs, const BloomStageConfig& rhs) -> bool
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
				if (!bloom_stage_config_equal(lhs.stages[index], rhs.stages[index]))
				{
					return false;
				}
			}
			return true;
		}

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

		auto temporal_aa_config_equal(
			const TemporalAAConfig& lhs,
			const TemporalAAConfig& rhs) -> bool
		{
			return lhs.enabled == rhs.enabled &&
				lhs.jitter_sequence_length == rhs.jitter_sequence_length &&
				lhs.history_blend == rhs.history_blend &&
				lhs.variance_gamma == rhs.variance_gamma &&
				lhs.luminance_weighting == rhs.luminance_weighting &&
				lhs.debug_view == rhs.debug_view;
		}

		auto tone_map_config_equal(const ToneMapConfig& lhs, const ToneMapConfig& rhs) -> bool
		{
			return lhs.exposure == rhs.exposure;
		}
	}

	SceneRenderConfig make_default_scene_render_config()
	{
		SceneRenderConfig config{};
		config.ambient_occlusion = make_default_ambient_occlusion_config();
		config.directional_shadows = make_default_directional_shadow_config();
		config.bloom = make_default_bloom_config();
		config.volumetric_lighting = make_default_volumetric_lighting_config();
		config.temporal_aa = make_default_temporal_aa_config();
		config.tonemap = make_default_tone_map_config();
		return config;
	}

	bool scene_render_config_equal(const SceneRenderConfig& lhs, const SceneRenderConfig& rhs)
	{
		return ambient_occlusion_config_equal(lhs.ambient_occlusion, rhs.ambient_occlusion) &&
			directional_shadow_config_equal(lhs.directional_shadows, rhs.directional_shadows) &&
			bloom_config_equal(lhs.bloom, rhs.bloom) &&
			volumetric_lighting_config_equal(lhs.volumetric_lighting, rhs.volumetric_lighting) &&
			temporal_aa_config_equal(lhs.temporal_aa, rhs.temporal_aa) &&
			tone_map_config_equal(lhs.tonemap, rhs.tonemap);
	}
}
