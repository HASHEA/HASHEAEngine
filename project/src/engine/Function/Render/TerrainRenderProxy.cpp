#include "Function/Render/TerrainRenderProxy.h"

#include <array>
#include <cmath>
#include <utility>

namespace AshEngine
{
	namespace
	{
		auto is_finite_matrix(const glm::mat4& matrix) -> bool
		{
			for (uint32_t column = 0u; column < 4u; ++column)
			{
				for (uint32_t row = 0u; row < 4u; ++row)
				{
					if (!std::isfinite(matrix[column][row]))
					{
						return false;
					}
				}
			}
			return true;
		}

		auto is_valid_snapshot_bounds(const TerrainAssetSnapshot& snapshot) -> bool
		{
			return !snapshot.failed &&
				snapshot.layout.sample_count_x >= 2u &&
				snapshot.layout.sample_count_z >= 2u &&
				std::isfinite(snapshot.layout.sample_spacing_meters) &&
				snapshot.layout.sample_spacing_meters > 0.0f &&
				std::isfinite(snapshot.height_mapping.height_offset) &&
				std::isfinite(snapshot.height_mapping.height_range) &&
				snapshot.height_mapping.height_range > 0.0f;
		}

		auto transform_point(
			const glm::mat4& matrix,
			const glm::vec3& point) -> glm::vec3
		{
			return glm::vec3(matrix * glm::vec4(point, 1.0f));
		}

		auto is_finite_vector(const glm::vec3& vector) -> bool
		{
			return std::isfinite(vector.x) &&
				std::isfinite(vector.y) &&
				std::isfinite(vector.z);
		}

		auto try_build_bounds(
			const TerrainAssetSnapshot& snapshot,
			const glm::mat4& world_transform,
			PrimitiveBounds& out_bounds) -> bool
		{
			if (!is_valid_snapshot_bounds(snapshot) ||
				!is_finite_matrix(world_transform))
			{
				return false;
			}

			const float maximum_x =
				static_cast<float>(snapshot.layout.sample_count_x - 1u) *
				snapshot.layout.sample_spacing_meters;
			const float maximum_z =
				static_cast<float>(snapshot.layout.sample_count_z - 1u) *
				snapshot.layout.sample_spacing_meters;
			const glm::vec3 local_min{
				0.0f,
				snapshot.height_mapping.height_offset,
				0.0f
			};
			const glm::vec3 local_max{
				maximum_x,
				snapshot.height_mapping.height_offset +
					snapshot.height_mapping.height_range,
				maximum_z
			};
			if (!is_finite_vector(local_min) || !is_finite_vector(local_max))
			{
				return false;
			}

			const std::array<glm::vec3, 8> corners =
			{
				glm::vec3(local_min.x, local_min.y, local_min.z),
				glm::vec3(local_max.x, local_min.y, local_min.z),
				glm::vec3(local_min.x, local_max.y, local_min.z),
				glm::vec3(local_max.x, local_max.y, local_min.z),
				glm::vec3(local_min.x, local_min.y, local_max.z),
				glm::vec3(local_max.x, local_min.y, local_max.z),
				glm::vec3(local_min.x, local_max.y, local_max.z),
				glm::vec3(local_max.x, local_max.y, local_max.z)
			};

			glm::vec3 world_min = transform_point(world_transform, corners[0]);
			if (!is_finite_vector(world_min))
			{
				return false;
			}
			glm::vec3 world_max = world_min;
			for (const glm::vec3& corner : corners)
			{
				const glm::vec3 transformed =
					transform_point(world_transform, corner);
				if (!is_finite_vector(transformed))
				{
					return false;
				}
				world_min = glm::min(world_min, transformed);
				world_max = glm::max(world_max, transformed);
			}

			const glm::vec3 center = (world_min + world_max) * 0.5f;
			const glm::vec3 extents = (world_max - world_min) * 0.5f;
			if (!is_finite_vector(center) || !is_finite_vector(extents))
			{
				return false;
			}

			PrimitiveBounds candidate{};
			candidate.is_valid = true;
			candidate.local_min = local_min;
			candidate.local_max = local_max;
			candidate.world_min = world_min;
			candidate.world_max = world_max;
			candidate.center = center;
			candidate.extents = extents;
			out_bounds = candidate;
			return true;
		}
	}

	bool RenderTerrainProxy::initialize(
		EntityId entity_id,
		const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
		const glm::mat4& world_transform,
		bool visible,
		bool casts_shadow,
		bool receives_shadow,
		std::shared_ptr<TerrainRenderAsset> render_asset)
	{
		PrimitiveBounds bounds{};
		if (entity_id == 0u || !snapshot ||
			!try_build_bounds(*snapshot, world_transform, bounds))
		{
			return false;
		}

		m_entity_id = entity_id;
		m_world_transform = world_transform;
		m_bounds = bounds;
		m_snapshot = snapshot;
		m_render_asset = std::move(render_asset);
		m_visible = visible;
		m_casts_shadow = casts_shadow;
		m_receives_shadow = receives_shadow;
		return true;
	}

	bool RenderTerrainProxy::replace_snapshot(
		const std::shared_ptr<const TerrainAssetSnapshot>& snapshot)
	{
		if (!snapshot || !is_valid_snapshot_bounds(*snapshot))
		{
			return false;
		}
		if (snapshot == m_snapshot)
		{
			return true;
		}
		if (m_snapshot &&
			snapshot->content_generation <= m_snapshot->content_generation)
		{
			return false;
		}

		PrimitiveBounds bounds{};
		if (!try_build_bounds(*snapshot, m_world_transform, bounds))
		{
			return false;
		}

		m_snapshot = snapshot;
		m_bounds = bounds;
		return true;
	}

	bool RenderTerrainProxy::update_world_transform(
		const glm::mat4& world_transform)
	{
		PrimitiveBounds bounds{};
		if (!m_snapshot ||
			!try_build_bounds(*m_snapshot, world_transform, bounds))
		{
			return false;
		}
		m_world_transform = world_transform;
		m_bounds = bounds;
		return true;
	}

	EntityId RenderTerrainProxy::get_entity_id() const
	{
		return m_entity_id;
	}

	bool RenderTerrainProxy::is_visible() const
	{
		return m_visible;
	}

	const PrimitiveBounds& RenderTerrainProxy::get_bounds() const
	{
		return m_bounds;
	}

	const std::shared_ptr<const TerrainAssetSnapshot>&
		RenderTerrainProxy::get_snapshot() const
	{
		return m_snapshot;
	}

	const std::shared_ptr<TerrainRenderAsset>&
		RenderTerrainProxy::get_render_asset() const
	{
		return m_render_asset;
	}

	VisibleTerrainFrame RenderTerrainProxy::make_visible_frame() const
	{
		VisibleTerrainFrame frame{};
		frame.entity_id = m_entity_id;
		frame.world_transform = m_world_transform;
		frame.world_bounds = m_bounds;
		frame.asset_snapshot = m_snapshot;
		frame.render_asset = m_render_asset;
		frame.casts_shadow = m_casts_shadow;
		frame.receives_shadow = m_receives_shadow;
		return frame;
	}

}
