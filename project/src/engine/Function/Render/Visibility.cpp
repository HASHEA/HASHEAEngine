#include "Function/Render/Visibility.h"

namespace AshEngine
{
	namespace
	{
		static auto intersects_frustum(const PrimitiveBounds& bounds, const SceneView& view) -> bool
		{
			if (!bounds.is_valid)
			{
				return false;
			}

			for (const SceneFrustumPlane& plane : view.frustum_planes)
			{
				const glm::vec3 positive_vertex =
				{
					plane.normal.x >= 0.0f ? bounds.world_max.x : bounds.world_min.x,
					plane.normal.y >= 0.0f ? bounds.world_max.y : bounds.world_min.y,
					plane.normal.z >= 0.0f ? bounds.world_max.z : bounds.world_min.z
				};

				if (glm::dot(plane.normal, positive_vertex) + plane.distance < 0.0f)
				{
					return false;
				}
			}

			return true;
		}
	}

	bool run_visibility_query(const VisibilityQuery& query, VisibilityResult& out_result)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_result = {};
		ASH_PROCESS_ERROR(query.primitives);
		ASH_PROCESS_ERROR(query.view && query.view->is_valid);

		for (const std::shared_ptr<StaticMeshPrimitiveProxy>& primitive : *query.primitives)
		{
			if (!primitive || !primitive->is_visible())
			{
				continue;
			}
			if (!intersects_frustum(primitive->get_bounds(), *query.view))
			{
				continue;
			}

			out_result.visible_primitives.primitive_ids.push_back(primitive->get_primitive_id());
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
