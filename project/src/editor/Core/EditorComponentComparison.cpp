#include "Core/EditorComponentComparison.h"

#include <cstddef>
#include <vector>

namespace AshEditor
{
	namespace
	{
		bool MeshMaterialOverridesEqual(
			const std::vector<AshEngine::MeshMaterialOverride>& refLeft,
			const std::vector<AshEngine::MeshMaterialOverride>& refRight)
		{
			if (refLeft.size() != refRight.size())
			{
				return false;
			}

			for (size_t uIndex = 0; uIndex < refLeft.size(); ++uIndex)
			{
				if (refLeft[uIndex].material_slot != refRight[uIndex].material_slot ||
					refLeft[uIndex].material_path != refRight[uIndex].material_path)
				{
					return false;
				}
			}
			return true;
		}
	}

	bool TransformComponentsEqual(
		const AshEngine::TransformComponent& refLeft,
		const AshEngine::TransformComponent& refRight)
	{
		return
			refLeft.position == refRight.position &&
			refLeft.rotation_euler_degrees == refRight.rotation_euler_degrees &&
			refLeft.scale == refRight.scale;
	}

	bool CameraComponentsEqual(
		const AshEngine::CameraComponent& refLeft,
		const AshEngine::CameraComponent& refRight)
	{
		return
			refLeft.primary == refRight.primary &&
			refLeft.reverse_z == refRight.reverse_z &&
			refLeft.projection == refRight.projection &&
			refLeft.fov_y_degrees == refRight.fov_y_degrees &&
			refLeft.near_plane == refRight.near_plane &&
			refLeft.far_plane == refRight.far_plane &&
			refLeft.orthographic_height == refRight.orthographic_height;
	}

	bool LightComponentsEqual(
		const AshEngine::LightComponent& refLeft,
		const AshEngine::LightComponent& refRight)
	{
		return
			refLeft.type == refRight.type &&
			refLeft.color == refRight.color &&
			refLeft.intensity == refRight.intensity &&
			refLeft.range == refRight.range &&
			refLeft.inner_cone_angle_degrees == refRight.inner_cone_angle_degrees &&
			refLeft.outer_cone_angle_degrees == refRight.outer_cone_angle_degrees &&
			refLeft.casts_shadow == refRight.casts_shadow &&
			refLeft.sunlight == refRight.sunlight &&
			refLeft.shadow_priority == refRight.shadow_priority &&
			refLeft.shadow_distance == refRight.shadow_distance &&
			refLeft.shadow_cascade_count == refRight.shadow_cascade_count &&
			refLeft.near_shadow_distance == refRight.near_shadow_distance;
	}

	bool MeshComponentsEqual(
		const AshEngine::MeshComponent& refLeft,
		const AshEngine::MeshComponent& refRight)
	{
		return
			refLeft.asset_path == refRight.asset_path &&
			refLeft.mesh_index == refRight.mesh_index &&
			MeshMaterialOverridesEqual(refLeft.material_overrides, refRight.material_overrides) &&
			refLeft.visible == refRight.visible &&
			refLeft.mobility == refRight.mobility &&
			refLeft.layer_mask == refRight.layer_mask;
	}

	bool EnvironmentComponentsEqual(
		const AshEngine::EnvironmentComponent& refLeft,
		const AshEngine::EnvironmentComponent& refRight)
	{
		return
			refLeft.active == refRight.active &&
			refLeft.ibl_asset_path == refRight.ibl_asset_path &&
			refLeft.source_texture_path == refRight.source_texture_path &&
			refLeft.intensity == refRight.intensity &&
			refLeft.lighting_intensity == refRight.lighting_intensity &&
			refLeft.background_intensity == refRight.background_intensity &&
			refLeft.rotation_degrees == refRight.rotation_degrees &&
			refLeft.visible_background == refRight.visible_background &&
			refLeft.affect_lighting == refRight.affect_lighting;
	}

	bool ParticleComponentsEqual(
		const AshEngine::ParticleComponent& refLeft,
		const AshEngine::ParticleComponent& refRight)
	{
		return
			refLeft.max_particles == refRight.max_particles &&
			refLeft.spawn_rate == refRight.spawn_rate &&
			refLeft.lifetime == refRight.lifetime &&
			refLeft.lifetime_variance == refRight.lifetime_variance &&
			refLeft.initial_speed == refRight.initial_speed &&
			refLeft.spread_angle_degrees == refRight.spread_angle_degrees &&
			refLeft.constant_acceleration == refRight.constant_acceleration &&
			refLeft.start_size == refRight.start_size &&
			refLeft.end_size == refRight.end_size &&
			refLeft.start_color == refRight.start_color &&
			refLeft.end_color == refRight.end_color &&
			refLeft.blend_mode == refRight.blend_mode &&
			refLeft.random_seed == refRight.random_seed &&
			refLeft.emitting == refRight.emitting;
	}

	bool TerrainComponentsEqual(
		const AshEngine::TerrainComponent& refLeft,
		const AshEngine::TerrainComponent& refRight)
	{
		return
			refLeft.asset_path == refRight.asset_path &&
			refLeft.visible == refRight.visible &&
			refLeft.casts_shadow == refRight.casts_shadow &&
			refLeft.receives_shadow == refRight.receives_shadow &&
			refLeft.material_layer_overrides == refRight.material_layer_overrides;
	}
}
