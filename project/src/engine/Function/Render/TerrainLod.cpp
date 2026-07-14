#include "Function/Render/TerrainLod.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>

namespace AshEngine
{
	namespace
	{
		struct ComponentBounds
		{
			glm::vec3 minimum{ 0.0f };
			glm::vec3 maximum{ 0.0f };
			bool valid = false;
		};

		struct PreparedComponent
		{
			TerrainComponentCoord coord{};
			ComponentBounds bounds{};
			std::array<float, k_terrain_lod_count> lod_errors{};
			bool present = false;
		};

		auto is_finite(const glm::vec3& value) -> bool
		{
			return std::isfinite(value.x) &&
				std::isfinite(value.y) &&
				std::isfinite(value.z);
		}

		auto is_finite(const glm::mat4& value) -> bool
		{
			for (uint32_t column = 0u; column < 4u; ++column)
			{
				for (uint32_t row = 0u; row < 4u; ++row)
				{
					if (!std::isfinite(value[column][row]))
					{
						return false;
					}
				}
			}
			return true;
		}

		auto is_valid_view(const SceneView& view) -> bool
		{
			if (!view.is_valid || view.desc.viewport_height == 0u ||
				!is_finite(view.camera_position) ||
				!std::isfinite(view.projection[1][1]) ||
				std::abs(view.projection[1][1]) <=
					std::numeric_limits<float>::epsilon())
			{
				return false;
			}
			for (const SceneFrustumPlane& plane : view.frustum_planes)
			{
				if (!is_finite(plane.normal) || !std::isfinite(plane.distance))
				{
					return false;
				}
			}
			return true;
		}

		auto transform_point(
			const glm::mat4& transform,
			const glm::vec3& point) -> glm::vec3
		{
			return glm::vec3(transform * glm::vec4(point, 1.0f));
		}

		auto build_component_bounds(
			const TerrainGridLayout& layout,
			const TerrainComponentSnapshot& component,
			const glm::mat4& world_transform,
			ComponentBounds& out_bounds) -> bool
		{
			if (component.min_max_levels.empty())
			{
				return false;
			}
			const glm::vec2 height_range = component.min_max_levels.back();
			if (!std::isfinite(height_range.x) ||
				!std::isfinite(height_range.y) ||
				height_range.x > height_range.y)
			{
				return false;
			}

			const float component_size =
				static_cast<float>(layout.component_quad_count) *
				layout.sample_spacing_meters;
			const float minimum_x =
				static_cast<float>(component.coord.x) * component_size;
			const float minimum_z =
				static_cast<float>(component.coord.z) * component_size;
			const glm::vec3 local_minimum{
				minimum_x, height_range.x, minimum_z
			};
			const glm::vec3 local_maximum{
				minimum_x + component_size,
				height_range.y,
				minimum_z + component_size
			};
			if (!is_finite(local_minimum) || !is_finite(local_maximum))
			{
				return false;
			}

			const std::array<glm::vec3, 8> corners =
			{
				glm::vec3(local_minimum.x, local_minimum.y, local_minimum.z),
				glm::vec3(local_maximum.x, local_minimum.y, local_minimum.z),
				glm::vec3(local_minimum.x, local_maximum.y, local_minimum.z),
				glm::vec3(local_maximum.x, local_maximum.y, local_minimum.z),
				glm::vec3(local_minimum.x, local_minimum.y, local_maximum.z),
				glm::vec3(local_maximum.x, local_minimum.y, local_maximum.z),
				glm::vec3(local_minimum.x, local_maximum.y, local_maximum.z),
				glm::vec3(local_maximum.x, local_maximum.y, local_maximum.z)
			};

			glm::vec3 minimum = transform_point(world_transform, corners[0]);
			if (!is_finite(minimum))
			{
				return false;
			}
			glm::vec3 maximum = minimum;
			for (const glm::vec3& corner : corners)
			{
				const glm::vec3 transformed =
					transform_point(world_transform, corner);
				if (!is_finite(transformed))
				{
					return false;
				}
				minimum = glm::min(minimum, transformed);
				maximum = glm::max(maximum, transformed);
			}

			out_bounds.minimum = minimum;
			out_bounds.maximum = maximum;
			out_bounds.valid = true;
			return true;
		}

		auto intersects_frustum(
			const ComponentBounds& bounds,
			const SceneView& view) -> bool
		{
			if (!bounds.valid)
			{
				return false;
			}
			for (const SceneFrustumPlane& plane : view.frustum_planes)
			{
				const glm::vec3 positive_vertex{
					plane.normal.x >= 0.0f ? bounds.maximum.x : bounds.minimum.x,
					plane.normal.y >= 0.0f ? bounds.maximum.y : bounds.minimum.y,
					plane.normal.z >= 0.0f ? bounds.maximum.z : bounds.minimum.z
				};
				if (glm::dot(plane.normal, positive_vertex) +
					plane.distance < 0.0f)
				{
					return false;
				}
			}
			return true;
		}

		auto merge_bounds(
			const ComponentBounds& lhs,
			const ComponentBounds& rhs) -> ComponentBounds
		{
			if (!lhs.valid)
			{
				return rhs;
			}
			if (!rhs.valid)
			{
				return lhs;
			}
			ComponentBounds result{};
			result.minimum = glm::min(lhs.minimum, rhs.minimum);
			result.maximum = glm::max(lhs.maximum, rhs.maximum);
			result.valid = true;
			return result;
		}

		auto collect_visible_components(
			const std::vector<PreparedComponent>& components,
			uint32_t component_count_x,
			const SceneView& view,
			uint32_t minimum_x,
			uint32_t minimum_z,
			uint32_t maximum_x,
			uint32_t maximum_z,
			std::vector<uint32_t>& out_indices) -> bool
		{
			ComponentBounds node_bounds{};
			for (uint32_t z = minimum_z; z < maximum_z; ++z)
			{
				for (uint32_t x = minimum_x; x < maximum_x; ++x)
				{
					const PreparedComponent& component =
						components[z * component_count_x + x];
					if (component.present)
					{
						node_bounds = merge_bounds(
							node_bounds, component.bounds);
					}
				}
			}
			if (!node_bounds.valid || !intersects_frustum(node_bounds, view))
			{
				return true;
			}

			const uint32_t width = maximum_x - minimum_x;
			const uint32_t height = maximum_z - minimum_z;
			if (width == 1u && height == 1u)
			{
				const uint32_t index =
					minimum_z * component_count_x + minimum_x;
				if (components[index].present)
				{
					out_indices.push_back(index);
				}
				return true;
			}

			const uint32_t middle_x = minimum_x + std::max(1u, width / 2u);
			const uint32_t middle_z = minimum_z + std::max(1u, height / 2u);
			const std::array<std::array<uint32_t, 4>, 4> children =
			{
				std::array<uint32_t, 4>{ minimum_x, minimum_z, middle_x, middle_z },
				std::array<uint32_t, 4>{ middle_x, minimum_z, maximum_x, middle_z },
				std::array<uint32_t, 4>{ minimum_x, middle_z, middle_x, maximum_z },
				std::array<uint32_t, 4>{ middle_x, middle_z, maximum_x, maximum_z }
			};
			for (const auto& child : children)
			{
				if (child[0] >= child[2] || child[1] >= child[3])
				{
					continue;
				}
				if (!collect_visible_components(
					components,
					component_count_x,
					view,
					child[0], child[1], child[2], child[3],
					out_indices))
				{
					return false;
				}
			}
			return true;
		}

		auto distance_to_bounds(
			const glm::vec3& point,
			const ComponentBounds& bounds) -> float
		{
			const glm::vec3 delta = glm::max(
				glm::max(bounds.minimum - point, glm::vec3(0.0f)),
				point - bounds.maximum);
			return glm::length(delta);
		}

		auto projected_error_pixels(
			float world_error,
			float distance,
			float projection_scale_pixels) -> float
		{
			if (world_error <= 0.0f)
			{
				return 0.0f;
			}
			if (distance <= std::numeric_limits<float>::epsilon())
			{
				return std::numeric_limits<float>::infinity();
			}
			return world_error * projection_scale_pixels / distance;
		}

		auto select_projected_lod(
			const PreparedComponent& component,
			const SceneView& view,
			float vertical_world_scale,
			float projection_scale_pixels,
			float maximum_error_pixels) -> uint8_t
		{
			const float distance = distance_to_bounds(
				view.camera_position, component.bounds);
			for (int lod = static_cast<int>(k_terrain_lod_count) - 1;
				lod >= 0;
				--lod)
			{
				const float pixels = projected_error_pixels(
					component.lod_errors[static_cast<size_t>(lod)] *
						vertical_world_scale,
					distance,
					projection_scale_pixels);
				if (pixels <= maximum_error_pixels)
				{
					return static_cast<uint8_t>(lod);
				}
			}
			return 0u;
		}

		auto compute_morph_factor(
			const PreparedComponent& component,
			uint8_t lod,
			const SceneView& view,
			float vertical_world_scale,
			float projection_scale_pixels,
			float maximum_error_pixels) -> float
		{
			if (lod + 1u >= k_terrain_lod_count)
			{
				return 0.0f;
			}
			const float next_error = component.lod_errors[lod + 1u];
			if (next_error <= component.lod_errors[lod])
			{
				return 0.0f;
			}
			const float distance = distance_to_bounds(
				view.camera_position, component.bounds);
			const float pixels = projected_error_pixels(
				next_error * vertical_world_scale,
				distance,
				projection_scale_pixels);
			if (!std::isfinite(pixels))
			{
				return 0.0f;
			}
			return std::clamp(
				2.0f - pixels / maximum_error_pixels,
				0.0f,
				1.0f);
		}

		auto try_refine_pair(
			TerrainVisibleComponent& lhs,
			TerrainVisibleComponent& rhs) -> int32_t
		{
			const int difference =
				static_cast<int>(lhs.lod) - static_cast<int>(rhs.lod);
			if (difference > 1)
			{
				lhs.lod = static_cast<uint8_t>(rhs.lod + 1u);
				return 0;
			}
			if (difference < -1)
			{
				rhs.lod = static_cast<uint8_t>(lhs.lod + 1u);
				return 1;
			}
			return -1;
		}
	}

	auto build_terrain_lod_batches(
		const TerrainLodInput& input,
		TerrainLodResult& out_result) -> bool
	{
		if (!input.asset_snapshot || input.asset_snapshot->failed ||
			!is_valid_terrain_grid_layout(input.asset_snapshot->layout) ||
			!is_finite(input.world_transform) || !is_valid_view(input.view) ||
			!std::isfinite(input.max_screen_error_pixels) ||
			input.max_screen_error_pixels <= 0.0f)
		{
			return false;
		}

		const TerrainGridLayout& layout = input.asset_snapshot->layout;
		const uint64_t component_count_64 =
			static_cast<uint64_t>(layout.component_count_x) *
			layout.component_count_z;
		if (component_count_64 == 0u ||
			component_count_64 > std::numeric_limits<uint32_t>::max())
		{
			return false;
		}
		const uint32_t component_count =
			static_cast<uint32_t>(component_count_64);
		if (input.asset_snapshot->components.size() != component_count ||
			(!input.requested_lods.empty() &&
				input.requested_lods.size() != component_count))
		{
			return false;
		}

		const float vertical_world_scale = glm::length(
			glm::vec3(input.world_transform[1]));
		const float projection_scale_pixels =
			std::abs(input.view.projection[1][1]) *
			static_cast<float>(input.view.desc.viewport_height) * 0.5f;
		if (!std::isfinite(vertical_world_scale) ||
			vertical_world_scale <= 0.0f ||
			!std::isfinite(projection_scale_pixels) ||
			projection_scale_pixels <= 0.0f)
		{
			return false;
		}

		std::vector<PreparedComponent> prepared(component_count);
		for (uint32_t index = 0u; index < component_count; ++index)
		{
			const std::shared_ptr<const TerrainComponentSnapshot>& source =
				input.asset_snapshot->components[index];
			if (!source)
			{
				continue;
			}
			const TerrainComponentCoord expected{
				static_cast<uint16_t>(index % layout.component_count_x),
				static_cast<uint16_t>(index / layout.component_count_x)
			};
			if (!(source->coord == expected))
			{
				return false;
			}
			PreparedComponent component{};
			component.coord = source->coord;
			float previous_error = -1.0f;
			for (size_t lod = 0u; lod < component.lod_errors.size(); ++lod)
			{
				const float error = source->lod_errors[lod];
				if (!std::isfinite(error) || error < 0.0f ||
					(lod > 0u && error < previous_error))
				{
					return false;
				}
				component.lod_errors[lod] = error;
				previous_error = error;
			}
			if (!build_component_bounds(
				layout, *source, input.world_transform, component.bounds))
			{
				return false;
			}
			component.present = true;
			prepared[index] = component;
		}

		if (!input.requested_lods.empty())
		{
			for (uint8_t requested : input.requested_lods)
			{
				if (requested != k_terrain_lod_automatic &&
					requested >= k_terrain_lod_count)
				{
					return false;
				}
			}
		}

		std::vector<uint32_t> visible_indices{};
		visible_indices.reserve(component_count);
		if (!collect_visible_components(
			prepared,
			layout.component_count_x,
			input.view,
			0u, 0u,
			layout.component_count_x,
			layout.component_count_z,
			visible_indices))
		{
			return false;
		}
		std::sort(
			visible_indices.begin(),
			visible_indices.end(),
			[&prepared](uint32_t lhs, uint32_t rhs)
			{
				const TerrainComponentCoord lhs_coord = prepared[lhs].coord;
				const TerrainComponentCoord rhs_coord = prepared[rhs].coord;
				return lhs_coord.z < rhs_coord.z ||
					(lhs_coord.z == rhs_coord.z && lhs_coord.x < rhs_coord.x);
			});

		TerrainLodResult candidate{};
		candidate.components.reserve(visible_indices.size());
		std::vector<int32_t> visible_lookup(component_count, -1);
		for (uint32_t source_index : visible_indices)
		{
			const PreparedComponent& source = prepared[source_index];
			TerrainVisibleComponent component{};
			component.coord = source.coord;
			component.world_min = source.bounds.minimum;
			component.world_max = source.bounds.maximum;
			const uint8_t requested = input.requested_lods.empty()
				? k_terrain_lod_automatic
				: input.requested_lods[source_index];
			component.lod = requested == k_terrain_lod_automatic
				? select_projected_lod(
					source,
					input.view,
					vertical_world_scale,
					projection_scale_pixels,
					input.max_screen_error_pixels)
				: requested;
			visible_lookup[source_index] =
				static_cast<int32_t>(candidate.components.size());
			candidate.components.push_back(component);
		}

		std::deque<uint32_t> repair_queue{};
		std::vector<bool> queued(candidate.components.size(), false);
		for (uint32_t index = 0u;
			index < candidate.components.size();
			++index)
		{
			repair_queue.push_back(index);
			queued[index] = true;
		}
		auto queue_component = [&](int32_t component_index)
		{
			if (component_index >= 0 &&
				!queued[static_cast<size_t>(component_index)])
			{
				repair_queue.push_back(static_cast<uint32_t>(component_index));
				queued[static_cast<size_t>(component_index)] = true;
			}
		};
		while (!repair_queue.empty())
		{
			const uint32_t visible_index = repair_queue.front();
			repair_queue.pop_front();
			queued[visible_index] = false;
			TerrainVisibleComponent& component =
				candidate.components[visible_index];
			const uint32_t x = component.coord.x;
			const uint32_t z = component.coord.z;
			const std::array<int32_t, 4> neighbors =
			{
				x > 0u
					? visible_lookup[z * layout.component_count_x + x - 1u]
					: -1,
				x + 1u < layout.component_count_x
					? visible_lookup[z * layout.component_count_x + x + 1u]
					: -1,
				z > 0u
					? visible_lookup[(z - 1u) * layout.component_count_x + x]
					: -1,
				z + 1u < layout.component_count_z
					? visible_lookup[(z + 1u) * layout.component_count_x + x]
					: -1
			};
			for (int32_t neighbor_index : neighbors)
			{
				if (neighbor_index < 0)
				{
					continue;
				}
				TerrainVisibleComponent& neighbor =
					candidate.components[static_cast<size_t>(neighbor_index)];
				const int32_t refined = try_refine_pair(component, neighbor);
				if (refined == 0)
				{
					queue_component(static_cast<int32_t>(visible_index));
					for (int32_t adjacent : neighbors)
					{
						queue_component(adjacent);
					}
				}
				else if (refined == 1)
				{
					queue_component(neighbor_index);
				}
			}
		}

		for (TerrainVisibleComponent& component : candidate.components)
		{
			const uint32_t x = component.coord.x;
			const uint32_t z = component.coord.z;
			const uint32_t source_index = z * layout.component_count_x + x;
			auto add_edge_if_coarser = [&](int32_t neighbor_index, uint8_t edge)
			{
				if (neighbor_index >= 0 &&
					candidate.components[static_cast<size_t>(neighbor_index)].lod >
						component.lod)
				{
					component.neighbor_edge_mask = static_cast<uint8_t>(
						component.neighbor_edge_mask | edge);
				}
			};
			add_edge_if_coarser(
				x > 0u
					? visible_lookup[z * layout.component_count_x + x - 1u]
					: -1,
				TerrainNeighborEdgeWest);
			add_edge_if_coarser(
				x + 1u < layout.component_count_x
					? visible_lookup[z * layout.component_count_x + x + 1u]
					: -1,
				TerrainNeighborEdgeEast);
			add_edge_if_coarser(
				z > 0u
					? visible_lookup[(z - 1u) * layout.component_count_x + x]
					: -1,
				TerrainNeighborEdgeNorth);
			add_edge_if_coarser(
				z + 1u < layout.component_count_z
					? visible_lookup[(z + 1u) * layout.component_count_x + x]
					: -1,
				TerrainNeighborEdgeSouth);
			component.morph_factor = compute_morph_factor(
				prepared[source_index],
				component.lod,
				input.view,
				vertical_world_scale,
				projection_scale_pixels,
				input.max_screen_error_pixels);
		}

		for (uint8_t lod = 0u; lod < k_terrain_lod_count; ++lod)
		{
			TerrainLodBatch batch{};
			batch.lod = lod;
			batch.first_instance = 0u;
			for (const TerrainVisibleComponent& component : candidate.components)
			{
				if (component.lod != lod)
				{
					continue;
				}
				TerrainInstanceData instance{};
				instance.coord = component.coord;
				instance.lod = component.lod;
				instance.neighbor_edge_mask = component.neighbor_edge_mask;
				instance.morph_factor = component.morph_factor;
				batch.instances.push_back(instance);
			}
			if (!batch.instances.empty())
			{
				candidate.batches.push_back(std::move(batch));
			}
		}

		out_result = std::move(candidate);
		return true;
	}

	auto make_full_terrain_lod_test_input() -> TerrainLodInput
	{
		auto snapshot = std::make_shared<TerrainAssetSnapshot>();
		snapshot->asset_id = 1u;
		snapshot->layout = make_default_terrain_grid_layout();
		snapshot->height_mapping = { -512.0f, 1024.0f };
		snapshot->content_generation = 1u;
		const uint32_t component_count =
			snapshot->layout.component_count_x *
			snapshot->layout.component_count_z;
		snapshot->components.reserve(component_count);
		for (uint32_t z = 0u;
			z < snapshot->layout.component_count_z;
			++z)
		{
			for (uint32_t x = 0u;
				x < snapshot->layout.component_count_x;
				++x)
			{
				auto component = std::make_shared<TerrainComponentSnapshot>();
				component->coord = {
					static_cast<uint16_t>(x), static_cast<uint16_t>(z)
				};
				component->content_generation = 1u;
				component->sample_width = k_terrain_component_sample_count;
				component->sample_height = k_terrain_component_sample_count;
				component->min_max_levels = { { 0.0f, 0.0f } };
				component->min_max_level_offsets.fill(1u);
				component->lod_errors = {
					0.0f, 0.25f, 0.5f, 1.0f, 2.0f,
					4.0f, 8.0f, 16.0f, 32.0f
				};
				snapshot->components.push_back(std::move(component));
			}
		}

		TerrainLodInput input{};
		input.asset_snapshot = std::move(snapshot);
		input.world_transform = glm::mat4(1.0f);
		input.view.is_valid = true;
		input.view.desc.viewport_width = 2560u;
		input.view.desc.viewport_height = 1440u;
		input.view.projection = glm::mat4(1.0f);
		input.view.camera_position = { 4096.0f, 512.0f, 4096.0f };
		for (SceneFrustumPlane& plane : input.view.frustum_planes)
		{
			plane.normal = { 0.0f, 0.0f, 1.0f };
			plane.distance = 100000.0f;
		}
		input.max_screen_error_pixels = 2.0f;
		input.requested_lods.assign(
			component_count, k_terrain_lod_automatic);
		return input;
	}
}
