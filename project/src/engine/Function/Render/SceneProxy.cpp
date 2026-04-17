#include "Function/Render/SceneProxy.h"

#include <array>

namespace AshEngine
{
	namespace
	{
		static auto transform_point(const glm::mat4& matrix, const glm::vec3& point) -> glm::vec3
		{
			return glm::vec3(matrix * glm::vec4(point, 1.0f));
		}
	}

	EntityId PrimitiveSceneProxy::get_entity_id() const
	{
		return m_entity_id;
	}

	uint64_t PrimitiveSceneProxy::get_primitive_id() const
	{
		return m_primitive_id;
	}

	const glm::mat4& PrimitiveSceneProxy::get_world_transform() const
	{
		return m_world_transform;
	}

	const PrimitiveBounds& PrimitiveSceneProxy::get_bounds() const
	{
		return m_bounds;
	}

	uint32_t PrimitiveSceneProxy::get_layer_mask() const
	{
		return m_layer_mask;
	}

	SceneMobility PrimitiveSceneProxy::get_mobility() const
	{
		return m_mobility;
	}

	bool PrimitiveSceneProxy::is_visible() const
	{
		return m_visible;
	}

	void StaticMeshPrimitiveProxy::initialize(
		uint64_t primitive_id,
		const SceneMeshExtractionDesc& desc,
		const std::shared_ptr<StaticMeshRenderAsset>& render_asset,
		const SceneMeshBounds& local_bounds)
	{
		m_entity_id = desc.entity_id;
		m_primitive_id = primitive_id;
		m_world_transform = desc.world_transform;
		m_layer_mask = desc.layer_mask;
		m_mobility = desc.mobility;
		m_visible = desc.visible;
		m_render_asset = render_asset;
		m_sections = render_asset ? render_asset->sections : std::vector<StaticMeshRenderSection>{};
		update_bounds(local_bounds);
	}

	const std::shared_ptr<StaticMeshRenderAsset>& StaticMeshPrimitiveProxy::get_render_asset() const
	{
		return m_render_asset;
	}

	const std::vector<StaticMeshRenderSection>& StaticMeshPrimitiveProxy::get_sections() const
	{
		return m_sections;
	}

	void StaticMeshPrimitiveProxy::update_bounds(const SceneMeshBounds& local_bounds)
	{
		m_bounds = {};
		m_bounds.is_valid = local_bounds.is_valid;
		m_bounds.local_min = local_bounds.local_min;
		m_bounds.local_max = local_bounds.local_max;
		if (!local_bounds.is_valid)
		{
			return;
		}

		const glm::vec3 min = local_bounds.local_min;
		const glm::vec3 max = local_bounds.local_max;
		const std::array<glm::vec3, 8> corners =
		{
			glm::vec3(min.x, min.y, min.z),
			glm::vec3(max.x, min.y, min.z),
			glm::vec3(min.x, max.y, min.z),
			glm::vec3(max.x, max.y, min.z),
			glm::vec3(min.x, min.y, max.z),
			glm::vec3(max.x, min.y, max.z),
			glm::vec3(min.x, max.y, max.z),
			glm::vec3(max.x, max.y, max.z),
		};

		glm::vec3 world_min = transform_point(m_world_transform, corners[0]);
		glm::vec3 world_max = world_min;
		for (const glm::vec3& corner : corners)
		{
			const glm::vec3 transformed = transform_point(m_world_transform, corner);
			world_min = glm::min(world_min, transformed);
			world_max = glm::max(world_max, transformed);
		}

		m_bounds.world_min = world_min;
		m_bounds.world_max = world_max;
		m_bounds.center = (world_min + world_max) * 0.5f;
		m_bounds.extents = (world_max - world_min) * 0.5f;
	}
}
