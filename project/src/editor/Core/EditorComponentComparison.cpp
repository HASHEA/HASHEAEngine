#include "Core/EditorComponentComparison.h"

namespace AshEditor
{
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
			refLeft.outer_cone_angle_degrees == refRight.outer_cone_angle_degrees;
	}

	bool MeshComponentsEqual(
		const AshEngine::MeshComponent& refLeft,
		const AshEngine::MeshComponent& refRight)
	{
		return
			refLeft.asset_path == refRight.asset_path &&
			refLeft.mesh_index == refRight.mesh_index &&
			refLeft.visible == refRight.visible &&
			refLeft.mobility == refRight.mobility &&
			refLeft.layer_mask == refRight.layer_mask;
	}
}
