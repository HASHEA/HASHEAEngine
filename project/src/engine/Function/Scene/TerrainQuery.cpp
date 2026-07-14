#include "Function/Scene/TerrainQuery.h"

#include "Function/Asset/AssetDatabase.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

namespace AshEngine
{
	namespace
	{
		constexpr float k_direction_epsilon = 1.0e-8f;
		constexpr float k_intersection_epsilon = 1.0e-6f;

		auto finite_vec2(const glm::vec2& value) -> bool
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		auto finite_vec3(const glm::vec3& value) -> bool
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		auto validate_snapshot_header(const TerrainAssetSnapshot& snapshot) -> bool
		{
			if (snapshot.failed || !is_valid_terrain_grid_layout(snapshot.layout))
			{
				return false;
			}
			const size_t expected_components =
				static_cast<size_t>(snapshot.layout.component_count_x) *
				snapshot.layout.component_count_z;
			return snapshot.components.size() == expected_components;
		}

		auto validate_component_shape(
			const TerrainAssetSnapshot& snapshot,
			TerrainComponentCoord expected_coord,
			const TerrainComponentSnapshot& component) -> bool
		{
			const TerrainSampleRect rect =
				get_terrain_component_snapshot_rect(snapshot.layout, expected_coord);
			const size_t expected_samples =
				static_cast<size_t>(rect.width()) * rect.height();
			return !rect.empty() && component.coord == expected_coord &&
				component.sample_width == rect.width() &&
				component.sample_height == rect.height() &&
				component.heights.size() == expected_samples;
		}

		auto sample_height(
			const TerrainAssetSnapshot& snapshot,
			const glm::vec2& terrain_local_xz,
			bool allow_upper_edge,
			float& out_height) -> TerrainQueryStatus
		{
			if (!finite_vec2(terrain_local_xz))
			{
				return TerrainQueryStatus::Failed;
			}
			const float spacing = snapshot.layout.sample_spacing_meters;
			const float maximum_x =
				static_cast<float>(snapshot.layout.sample_count_x - 1u) * spacing;
			const float maximum_z =
				static_cast<float>(snapshot.layout.sample_count_z - 1u) * spacing;
			const bool inside_x = terrain_local_xz.x >= 0.0f &&
				(allow_upper_edge ? terrain_local_xz.x <= maximum_x : terrain_local_xz.x < maximum_x);
			const bool inside_z = terrain_local_xz.y >= 0.0f &&
				(allow_upper_edge ? terrain_local_xz.y <= maximum_z : terrain_local_xz.y < maximum_z);
			if (!inside_x || !inside_z)
			{
				return TerrainQueryStatus::Outside;
			}

			const double sample_x = static_cast<double>(terrain_local_xz.x) / spacing;
			const double sample_z = static_cast<double>(terrain_local_xz.y) / spacing;
			const uint32_t x0 = std::min(
				static_cast<uint32_t>(std::floor(sample_x)),
				snapshot.layout.sample_count_x - 1u);
			const uint32_t z0 = std::min(
				static_cast<uint32_t>(std::floor(sample_z)),
				snapshot.layout.sample_count_z - 1u);
			const uint32_t x1 = std::min(x0 + 1u, snapshot.layout.sample_count_x - 1u);
			const uint32_t z1 = std::min(z0 + 1u, snapshot.layout.sample_count_z - 1u);
			const TerrainComponentCoord owner =
				get_terrain_sample_owner(snapshot.layout, x0, z0);
			const size_t component_index =
				static_cast<size_t>(owner.z) * snapshot.layout.component_count_x + owner.x;
			const auto& component = snapshot.components[component_index];
			if (!component)
			{
				return TerrainQueryStatus::Pending;
			}
			if (!validate_component_shape(snapshot, owner, *component))
			{
				return TerrainQueryStatus::Failed;
			}
			const TerrainSampleRect rect =
				get_terrain_component_snapshot_rect(snapshot.layout, owner);
			const auto component_height = [&](uint32_t global_x, uint32_t global_z)
			{
				const size_t index =
					static_cast<size_t>(global_z - rect.min_z) * component->sample_width +
					(global_x - rect.min_x);
				return component->heights[index];
			};
			const float h00 = component_height(x0, z0);
			const float h10 = component_height(x1, z0);
			const float h01 = component_height(x0, z1);
			const float h11 = component_height(x1, z1);
			if (!std::isfinite(h00) || !std::isfinite(h10) ||
				!std::isfinite(h01) || !std::isfinite(h11))
			{
				return TerrainQueryStatus::Failed;
			}
			const double tx = sample_x - x0;
			const double tz = sample_z - z0;
			const double top = h00 + (static_cast<double>(h10) - h00) * tx;
			const double bottom = h01 + (static_cast<double>(h11) - h01) * tx;
			const double result = top + (bottom - top) * tz;
			if (!std::isfinite(result) ||
				result < std::numeric_limits<float>::lowest() ||
				result > std::numeric_limits<float>::max())
			{
				return TerrainQueryStatus::Failed;
			}
			out_height = static_cast<float>(result);
			return TerrainQueryStatus::Ready;
		}

		auto intersect_axis(
			float origin,
			float direction,
			float minimum,
			float maximum,
			float& in_out_entry,
			float& in_out_exit) -> bool
		{
			if (std::abs(direction) <= k_direction_epsilon)
			{
				return origin >= minimum && origin <= maximum;
			}
			float first = (minimum - origin) / direction;
			float second = (maximum - origin) / direction;
			if (first > second)
			{
				std::swap(first, second);
			}
			in_out_entry = std::max(in_out_entry, first);
			in_out_exit = std::min(in_out_exit, second);
			return in_out_entry <= in_out_exit;
		}

		auto intersect_aabb(
			const glm::vec3& origin,
			const glm::vec3& direction,
			const glm::vec3& minimum,
			const glm::vec3& maximum,
			float max_distance,
			float& out_entry) -> bool
		{
			float entry = 0.0f;
			float exit = max_distance;
			if (!intersect_axis(origin.x, direction.x, minimum.x, maximum.x, entry, exit) ||
				!intersect_axis(origin.y, direction.y, minimum.y, maximum.y, entry, exit) ||
				!intersect_axis(origin.z, direction.z, minimum.z, maximum.z, entry, exit))
			{
				return false;
			}
			out_entry = entry;
			return true;
		}

		auto intersect_xz_rect(
			const glm::vec3& origin,
			const glm::vec3& direction,
			const glm::vec2& minimum,
			const glm::vec2& maximum,
			float max_distance,
			float& out_entry) -> bool
		{
			float entry = 0.0f;
			float exit = max_distance;
			if (!intersect_axis(origin.x, direction.x, minimum.x, maximum.x, entry, exit) ||
				!intersect_axis(origin.z, direction.z, minimum.y, maximum.y, entry, exit))
			{
				return false;
			}
			out_entry = entry;
			return true;
		}

		struct LevelShape
		{
			uint32_t width = 0u;
			uint32_t height = 0u;
		};

		auto validate_spatial_data(
			const TerrainComponentSnapshot& component,
			std::vector<LevelShape>& out_shapes) -> bool
		{
			uint32_t width = (component.sample_width - 1u + 3u) / 4u;
			uint32_t height = (component.sample_height - 1u + 3u) / 4u;
			size_t expected_offset = 0u;
			while (true)
			{
				if (out_shapes.size() >= 9u ||
					component.min_max_level_offsets[out_shapes.size()] != expected_offset)
				{
					return false;
				}
				out_shapes.push_back({ width, height });
				expected_offset += static_cast<size_t>(width) * height;
				if (width == 1u && height == 1u)
				{
					break;
				}
				width = (width + 1u) / 2u;
				height = (height + 1u) / 2u;
			}
			if (expected_offset != component.min_max_levels.size())
			{
				return false;
			}
			for (size_t index = out_shapes.size();
				index < component.min_max_level_offsets.size();
				++index)
			{
				if (component.min_max_level_offsets[index] != expected_offset)
				{
					return false;
				}
			}
			for (const glm::vec2 range : component.min_max_levels)
			{
				if (!finite_vec2(range) || range.x > range.y)
				{
					return false;
				}
			}
			return true;
		}

		auto intersect_triangle(
			const glm::vec3& origin,
			const glm::vec3& direction,
			const glm::vec3& a,
			const glm::vec3& b,
			const glm::vec3& c,
			float max_distance,
			float& out_distance) -> bool
		{
			const glm::vec3 edge_ab = b - a;
			const glm::vec3 edge_ac = c - a;
			const glm::vec3 p = glm::cross(direction, edge_ac);
			const float determinant = glm::dot(edge_ab, p);
			if (std::abs(determinant) <= k_intersection_epsilon)
			{
				return false;
			}
			const float inverse = 1.0f / determinant;
			const glm::vec3 translated = origin - a;
			const float u = glm::dot(translated, p) * inverse;
			if (u < -k_intersection_epsilon || u > 1.0f + k_intersection_epsilon)
			{
				return false;
			}
			const glm::vec3 q = glm::cross(translated, edge_ab);
			const float v = glm::dot(direction, q) * inverse;
			if (v < -k_intersection_epsilon || u + v > 1.0f + k_intersection_epsilon)
			{
				return false;
			}
			const float distance = glm::dot(edge_ac, q) * inverse;
			if (distance < 0.0f || distance > max_distance)
			{
				return false;
			}
			out_distance = distance;
			return true;
		}

		struct ComponentCandidate
		{
			const TerrainComponentSnapshot* component = nullptr;
			TerrainComponentCoord coord{};
			std::vector<LevelShape> shapes{};
			float entry = 0.0f;
		};

		struct NodeCandidate
		{
			uint32_t level = 0u;
			uint32_t x = 0u;
			uint32_t z = 0u;
			float entry = 0.0f;
		};

		auto node_entry(
			const TerrainAssetSnapshot& snapshot,
			const ComponentCandidate& candidate,
			const glm::vec3& origin,
			const glm::vec3& direction,
			float max_distance,
			uint32_t level,
			uint32_t node_x,
			uint32_t node_z,
			float& out_entry) -> bool
		{
			const LevelShape shape = candidate.shapes[level];
			const size_t range_index =
				candidate.component->min_max_level_offsets[level] +
				static_cast<size_t>(node_z) * shape.width + node_x;
			const glm::vec2 range = candidate.component->min_max_levels[range_index];
			const uint32_t cell_span = 4u << level;
			const uint32_t min_cell_x = node_x * cell_span;
			const uint32_t min_cell_z = node_z * cell_span;
			const uint32_t max_cell_x = std::min(
				min_cell_x + cell_span, candidate.component->sample_width - 1u);
			const uint32_t max_cell_z = std::min(
				min_cell_z + cell_span, candidate.component->sample_height - 1u);
			const float spacing = snapshot.layout.sample_spacing_meters;
			const uint32_t component_min_x =
				static_cast<uint32_t>(candidate.coord.x) * snapshot.layout.component_quad_count;
			const uint32_t component_min_z =
				static_cast<uint32_t>(candidate.coord.z) * snapshot.layout.component_quad_count;
			return intersect_aabb(
				origin,
				direction,
				{
					static_cast<float>(component_min_x + min_cell_x) * spacing,
					range.x,
					static_cast<float>(component_min_z + min_cell_z) * spacing
				},
				{
					static_cast<float>(component_min_x + max_cell_x) * spacing,
					range.y,
					static_cast<float>(component_min_z + max_cell_z) * spacing
				},
				max_distance,
				out_entry);
		}

		auto test_leaf_cells(
			const TerrainAssetSnapshot& snapshot,
			const ComponentCandidate& candidate,
			uint32_t leaf_x,
			uint32_t leaf_z,
			const glm::vec3& origin,
			const glm::vec3& direction,
			float max_distance,
			float& in_out_nearest,
			TerrainRayHit& in_out_hit) -> void
		{
			const uint32_t min_cell_x = leaf_x * 4u;
			const uint32_t min_cell_z = leaf_z * 4u;
			const uint32_t max_cell_x = std::min(
				min_cell_x + 4u, candidate.component->sample_width - 1u);
			const uint32_t max_cell_z = std::min(
				min_cell_z + 4u, candidate.component->sample_height - 1u);
			const uint32_t component_min_x =
				static_cast<uint32_t>(candidate.coord.x) * snapshot.layout.component_quad_count;
			const uint32_t component_min_z =
				static_cast<uint32_t>(candidate.coord.z) * snapshot.layout.component_quad_count;
			const float spacing = snapshot.layout.sample_spacing_meters;
			const auto vertex = [&](uint32_t local_x, uint32_t local_z)
			{
				return glm::vec3{
					static_cast<float>(component_min_x + local_x) * spacing,
					candidate.component->heights[
						static_cast<size_t>(local_z) * candidate.component->sample_width + local_x],
					static_cast<float>(component_min_z + local_z) * spacing
				};
			};

			for (uint32_t cell_z = min_cell_z; cell_z < max_cell_z; ++cell_z)
			{
				for (uint32_t cell_x = min_cell_x; cell_x < max_cell_x; ++cell_x)
				{
					const glm::vec3 p00 = vertex(cell_x, cell_z);
					const glm::vec3 p10 = vertex(cell_x + 1u, cell_z);
					const glm::vec3 p11 = vertex(cell_x + 1u, cell_z + 1u);
					const glm::vec3 p01 = vertex(cell_x, cell_z + 1u);
					for (const std::array<glm::vec3, 3>& triangle : {
						std::array<glm::vec3, 3>{ p00, p10, p11 },
						std::array<glm::vec3, 3>{ p00, p11, p01 } })
					{
						float distance = 0.0f;
						if (!intersect_triangle(
								origin,
								direction,
								triangle[0],
								triangle[1],
								triangle[2],
								std::min(max_distance, in_out_nearest),
								distance) ||
							distance >= in_out_nearest)
						{
							continue;
						}
						glm::vec3 normal = glm::normalize(glm::cross(
							triangle[1] - triangle[0],
							triangle[2] - triangle[0]));
						if (normal.y < 0.0f)
						{
							normal = -normal;
						}
						in_out_nearest = distance;
						in_out_hit.distance = distance;
						in_out_hit.position = origin + direction * distance;
						in_out_hit.normal = normal;
						in_out_hit.component = candidate.coord;
						in_out_hit.local_sample = {
							in_out_hit.position.x / spacing,
							in_out_hit.position.z / spacing
						};
					}
				}
			}
		}

		auto traverse_component(
			const TerrainAssetSnapshot& snapshot,
			const ComponentCandidate& candidate,
			const glm::vec3& origin,
			const glm::vec3& direction,
			float max_distance,
			float& in_out_nearest,
			TerrainRayHit& in_out_hit) -> void
		{
			std::vector<NodeCandidate> open{};
			const uint32_t root_level = static_cast<uint32_t>(candidate.shapes.size() - 1u);
			open.push_back({ root_level, 0u, 0u, candidate.entry });
			while (!open.empty())
			{
				const auto next = std::min_element(
					open.begin(), open.end(), [](const NodeCandidate& lhs, const NodeCandidate& rhs)
					{
						return lhs.entry < rhs.entry;
					});
				const NodeCandidate node = *next;
				open.erase(next);
				if (node.entry > in_out_nearest)
				{
					break;
				}
				if (node.level == 0u)
				{
					test_leaf_cells(
						snapshot,
						candidate,
						node.x,
						node.z,
						origin,
						direction,
						max_distance,
						in_out_nearest,
						in_out_hit);
					continue;
				}
				const LevelShape child_shape = candidate.shapes[node.level - 1u];
				for (uint32_t child_z = node.z * 2u;
					child_z < std::min(node.z * 2u + 2u, child_shape.height);
					++child_z)
				{
					for (uint32_t child_x = node.x * 2u;
						child_x < std::min(node.x * 2u + 2u, child_shape.width);
						++child_x)
					{
						float entry = 0.0f;
						if (node_entry(
								snapshot,
								candidate,
								origin,
								direction,
								std::min(max_distance, in_out_nearest),
								node.level - 1u,
								child_x,
								child_z,
								entry))
						{
							open.push_back({ node.level - 1u, child_x, child_z, entry });
						}
					}
				}
			}
		}
	}

	TerrainQueryStatus query_height(
		const TerrainAssetSnapshot& snapshot,
		const glm::vec2& terrain_local_xz,
		float& out_height)
	{
		if (!validate_snapshot_header(snapshot))
		{
			return TerrainQueryStatus::Failed;
		}
		float height = 0.0f;
		const TerrainQueryStatus status =
			sample_height(snapshot, terrain_local_xz, false, height);
		if (status == TerrainQueryStatus::Ready)
		{
			out_height = height;
		}
		return status;
	}

	TerrainQueryStatus query_normal(
		const TerrainAssetSnapshot& snapshot,
		const glm::vec2& terrain_local_xz,
		glm::vec3& out_normal)
	{
		if (!validate_snapshot_header(snapshot))
		{
			return TerrainQueryStatus::Failed;
		}
		float center = 0.0f;
		const TerrainQueryStatus center_status =
			sample_height(snapshot, terrain_local_xz, false, center);
		if (center_status != TerrainQueryStatus::Ready)
		{
			return center_status;
		}
		const float spacing = snapshot.layout.sample_spacing_meters;
		const float maximum_x =
			static_cast<float>(snapshot.layout.sample_count_x - 1u) * spacing;
		const float maximum_z =
			static_cast<float>(snapshot.layout.sample_count_z - 1u) * spacing;
		const float x0 = std::max(0.0f, terrain_local_xz.x - spacing);
		const float x1 = std::min(maximum_x, terrain_local_xz.x + spacing);
		const float z0 = std::max(0.0f, terrain_local_xz.y - spacing);
		const float z1 = std::min(maximum_z, terrain_local_xz.y + spacing);
		float height_x0 = 0.0f;
		float height_x1 = 0.0f;
		float height_z0 = 0.0f;
		float height_z1 = 0.0f;
		const std::array<TerrainQueryStatus, 4> statuses{
			sample_height(snapshot, { x0, terrain_local_xz.y }, true, height_x0),
			sample_height(snapshot, { x1, terrain_local_xz.y }, true, height_x1),
			sample_height(snapshot, { terrain_local_xz.x, z0 }, true, height_z0),
			sample_height(snapshot, { terrain_local_xz.x, z1 }, true, height_z1)
		};
		if (std::find(statuses.begin(), statuses.end(), TerrainQueryStatus::Failed) != statuses.end())
		{
			return TerrainQueryStatus::Failed;
		}
		if (std::find(statuses.begin(), statuses.end(), TerrainQueryStatus::Pending) != statuses.end())
		{
			return TerrainQueryStatus::Pending;
		}
		if (std::find(statuses.begin(), statuses.end(), TerrainQueryStatus::Outside) != statuses.end())
		{
			return TerrainQueryStatus::Outside;
		}
		const float gradient_x = (height_x1 - height_x0) / (x1 - x0);
		const float gradient_z = (height_z1 - height_z0) / (z1 - z0);
		const glm::vec3 normal = glm::normalize(glm::vec3{ -gradient_x, 1.0f, -gradient_z });
		if (!finite_vec3(normal))
		{
			return TerrainQueryStatus::Failed;
		}
		out_normal = normal;
		return TerrainQueryStatus::Ready;
	}

	TerrainQueryStatus ray_cast_terrain(
		const TerrainAssetSnapshot& snapshot,
		const TerrainRay& ray,
		float max_distance,
		TerrainRayHit& out_hit)
	{
		if (!validate_snapshot_header(snapshot) || !finite_vec3(ray.origin) ||
			!finite_vec3(ray.direction) || !std::isfinite(max_distance) || max_distance <= 0.0f)
		{
			return TerrainQueryStatus::Failed;
		}
		const float direction_length = glm::length(ray.direction);
		if (!std::isfinite(direction_length) || direction_length <= k_direction_epsilon)
		{
			return TerrainQueryStatus::Failed;
		}
		const glm::vec3 direction = ray.direction / direction_length;

		try
		{
			std::vector<ComponentCandidate> candidates{};
			candidates.reserve(snapshot.components.size());
			float nearest_pending_entry = std::numeric_limits<float>::infinity();
			const float spacing = snapshot.layout.sample_spacing_meters;
			for (uint32_t component_z = 0u;
				component_z < snapshot.layout.component_count_z;
				++component_z)
			{
				for (uint32_t component_x = 0u;
					component_x < snapshot.layout.component_count_x;
					++component_x)
				{
					const TerrainComponentCoord coord{
						static_cast<uint16_t>(component_x),
						static_cast<uint16_t>(component_z)
					};
					const size_t index =
						static_cast<size_t>(component_z) * snapshot.layout.component_count_x + component_x;
					const uint32_t min_sample_x = component_x * snapshot.layout.component_quad_count;
					const uint32_t min_sample_z = component_z * snapshot.layout.component_quad_count;
					const uint32_t max_sample_x = min_sample_x + snapshot.layout.component_quad_count;
					const uint32_t max_sample_z = min_sample_z + snapshot.layout.component_quad_count;
					float xz_entry = 0.0f;
					if (!intersect_xz_rect(
							ray.origin,
							direction,
							{ static_cast<float>(min_sample_x) * spacing, static_cast<float>(min_sample_z) * spacing },
							{ static_cast<float>(max_sample_x) * spacing, static_cast<float>(max_sample_z) * spacing },
							max_distance,
							xz_entry))
					{
						continue;
					}
					const auto& component = snapshot.components[index];
					if (!component)
					{
						nearest_pending_entry = std::min(nearest_pending_entry, xz_entry);
						continue;
					}
					if (!validate_component_shape(snapshot, coord, *component))
					{
						return TerrainQueryStatus::Failed;
					}
					ComponentCandidate candidate{};
					candidate.component = component.get();
					candidate.coord = coord;
					candidate.shapes.reserve(9u);
					if (!validate_spatial_data(*component, candidate.shapes))
					{
						return TerrainQueryStatus::Failed;
					}
					const uint32_t root_level = static_cast<uint32_t>(candidate.shapes.size() - 1u);
					if (node_entry(
							snapshot,
							candidate,
							ray.origin,
							direction,
							max_distance,
							root_level,
							0u,
							0u,
							candidate.entry))
					{
						candidates.push_back(std::move(candidate));
					}
				}
			}
			std::sort(candidates.begin(), candidates.end(),
				[](const ComponentCandidate& lhs, const ComponentCandidate& rhs)
				{
					return lhs.entry < rhs.entry;
				});

			float nearest = std::numeric_limits<float>::infinity();
			TerrainRayHit hit{};
			for (const ComponentCandidate& candidate : candidates)
			{
				if (candidate.entry > nearest)
				{
					break;
				}
				traverse_component(
					snapshot,
					candidate,
					ray.origin,
					direction,
					max_distance,
					nearest,
					hit);
			}
			if (nearest_pending_entry <= nearest)
			{
				return TerrainQueryStatus::Pending;
			}
			if (!std::isfinite(nearest))
			{
				return TerrainQueryStatus::Outside;
			}
			out_hit = hit;
			return TerrainQueryStatus::Ready;
		}
		catch (const std::bad_alloc&)
		{
			return TerrainQueryStatus::Failed;
		}
		catch (const std::length_error&)
		{
			return TerrainQueryStatus::Failed;
		}
	}

	auto prefetch_query_region(
		AssetDatabase& database,
		TerrainAssetId asset_id,
		const TerrainSampleRect& sample_region) -> TerrainQueryStatus
	{
		if (sample_region.empty())
		{
			return TerrainQueryStatus::Outside;
		}

		auto future = database.load_terrain_by_id_async(asset_id);
		if (!future.valid())
		{
			return TerrainQueryStatus::Failed;
		}
		if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			return TerrainQueryStatus::Pending;
		}

		std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		try
		{
			snapshot = future.get();
		}
		catch (...)
		{
			return TerrainQueryStatus::Failed;
		}
		if (!snapshot || snapshot->failed ||
			!is_valid_terrain_grid_layout(snapshot->layout))
		{
			return TerrainQueryStatus::Failed;
		}
		if (sample_region.min_x >= snapshot->layout.sample_count_x ||
			sample_region.min_z >= snapshot->layout.sample_count_z ||
			sample_region.max_x_exclusive > snapshot->layout.sample_count_x ||
			sample_region.max_z_exclusive > snapshot->layout.sample_count_z)
		{
			return TerrainQueryStatus::Outside;
		}
		return TerrainQueryStatus::Ready;
	}
}
