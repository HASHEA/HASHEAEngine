#include "Function/Asset/TerrainLayerStack.h"

#include "Function/Asset/TerrainComposition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace AshEngine
{
	namespace
	{
		constexpr size_t k_max_layer_name_size = 1024u * 1024u;

		static_assert(std::is_nothrow_move_constructible_v<TerrainEditLayer>);
		static_assert(std::is_nothrow_move_assignable_v<TerrainEditLayer>);
		static_assert(std::is_nothrow_move_assignable_v<TerrainLayerStackPatch>);

		auto fail(std::string* out_error, const char* message) -> bool
		{
			if (out_error != nullptr)
			{
				*out_error = message;
			}
			return false;
		}

		auto valid_kind(const TerrainLayerStackEditKind kind) -> bool
		{
			return kind >= TerrainLayerStackEditKind::Add &&
				kind <= TerrainLayerStackEditKind::SetLocked;
		}

		auto valid_blend_mode(const TerrainHeightBlendMode mode) -> bool
		{
			return mode == TerrainHeightBlendMode::Additive ||
				mode == TerrainHeightBlendMode::Alpha;
		}

		auto component_coord_less(
			const TerrainComponentCoord lhs,
			const TerrainComponentCoord rhs) -> bool
		{
			return lhs.z != rhs.z ? lhs.z < rhs.z : lhs.x < rhs.x;
		}

		auto rect_equal(const TerrainSampleRect& lhs, const TerrainSampleRect& rhs) -> bool
		{
			return lhs.min_x == rhs.min_x && lhs.min_z == rhs.min_z &&
				lhs.max_x_exclusive == rhs.max_x_exclusive &&
				lhs.max_z_exclusive == rhs.max_z_exclusive;
		}

		auto rect_inside(const TerrainSampleRect& outer, const TerrainSampleRect& inner) -> bool
		{
			return !inner.empty() && inner.min_x >= outer.min_x && inner.min_z >= outer.min_z &&
				inner.max_x_exclusive <= outer.max_x_exclusive &&
				inner.max_z_exclusive <= outer.max_z_exclusive;
		}

		auto height_block_equal(
			const TerrainSparseHeightBlock& lhs,
			const TerrainSparseHeightBlock& rhs) -> bool
		{
			return lhs.owner == rhs.owner && rect_equal(lhs.changed_rect, rhs.changed_rect) &&
				lhs.values == rhs.values && lhs.coverage == rhs.coverage;
		}

		auto weight_block_equal(
			const TerrainSparseWeightBlock& lhs,
			const TerrainSparseWeightBlock& rhs) -> bool
		{
			return lhs.owner == rhs.owner && rect_equal(lhs.changed_rect, rhs.changed_rect) &&
				lhs.values == rhs.values && lhs.coverage == rhs.coverage;
		}

		auto metadata_from_layer(const TerrainEditLayer& layer) -> TerrainLayerMetadata
		{
			return {
				layer.id,
				layer.name,
				layer.visible,
				layer.locked,
				layer.strength,
				layer.height_blend_mode
			};
		}

		auto metadata_equal(
			const TerrainLayerMetadata& lhs,
			const TerrainLayerMetadata& rhs) -> bool
		{
			return lhs.id == rhs.id && lhs.name == rhs.name && lhs.visible == rhs.visible &&
				lhs.locked == rhs.locked && lhs.strength == rhs.strength &&
				lhs.height_blend_mode == rhs.height_blend_mode;
		}

		auto valid_metadata(const TerrainLayerMetadata& metadata) -> bool
		{
			return metadata.id.is_valid() && metadata.name.size() <= k_max_layer_name_size &&
				std::isfinite(metadata.strength) && valid_blend_mode(metadata.height_blend_mode);
		}

		auto metadata_matches_layer(
			const TerrainEditLayer& layer,
			const TerrainLayerMetadata& metadata) -> bool
		{
			return metadata_equal(metadata_from_layer(layer), metadata);
		}

		auto layer_equal(const TerrainEditLayer& lhs, const TerrainEditLayer& rhs) -> bool
		{
			if (!metadata_matches_layer(lhs, metadata_from_layer(rhs)) ||
				lhs.height_blocks.size() != rhs.height_blocks.size() ||
				lhs.weight_blocks.size() != rhs.weight_blocks.size())
			{
				return false;
			}
			for (size_t index = 0u; index < lhs.height_blocks.size(); ++index)
			{
				if (!height_block_equal(lhs.height_blocks[index], rhs.height_blocks[index]))
				{
					return false;
				}
			}
			for (size_t index = 0u; index < lhs.weight_blocks.size(); ++index)
			{
				if (!weight_block_equal(lhs.weight_blocks[index], rhs.weight_blocks[index]))
				{
					return false;
				}
			}
			return true;
		}

		auto find_layer_index(
			const std::vector<TerrainEditLayer>& layers,
			const TerrainLayerId id,
			size_t& out_index) -> bool
		{
			for (size_t index = 0u; index < layers.size(); ++index)
			{
				if (layers[index].id == id)
				{
					out_index = index;
					return true;
				}
			}
			return false;
		}

		auto find_id_index(
			const std::vector<TerrainLayerId>& order,
			const TerrainLayerId id,
			size_t& out_index) -> bool
		{
			for (size_t index = 0u; index < order.size(); ++index)
			{
				if (order[index] == id)
				{
					out_index = index;
					return true;
				}
			}
			return false;
		}

		auto make_order(const std::vector<TerrainEditLayer>& layers) -> std::vector<TerrainLayerId>
		{
			std::vector<TerrainLayerId> order{};
			order.reserve(layers.size());
			for (const TerrainEditLayer& layer : layers)
			{
				order.push_back(layer.id);
			}
			return order;
		}

		auto order_matches(
			const std::vector<TerrainEditLayer>& layers,
			const std::vector<TerrainLayerId>& order) -> bool
		{
			if (layers.size() != order.size())
			{
				return false;
			}
			for (size_t index = 0u; index < layers.size(); ++index)
			{
				if (layers[index].id != order[index])
				{
					return false;
				}
			}
			return true;
		}

		auto validate_order(const std::vector<TerrainLayerId>& order) -> bool
		{
			for (size_t index = 0u; index < order.size(); ++index)
			{
				if (!order[index].is_valid() ||
					std::find(order.begin(), order.begin() + index, order[index]) !=
						order.begin() + index)
				{
					return false;
				}
			}
			return true;
		}

		auto validate_block_shape(
			const TerrainWorkingSet& working_set,
			const TerrainComponentCoord owner,
			const TerrainSampleRect& rect,
			const size_t value_count,
			const size_t coverage_count) -> bool
		{
			if (owner.x >= working_set.layout.component_count_x ||
				owner.z >= working_set.layout.component_count_z || rect.empty())
			{
				return false;
			}
			const TerrainSampleRect owned_rect =
				get_terrain_component_owned_rect(working_set.layout, owner);
			const uint64_t area =
				static_cast<uint64_t>(rect.width()) * static_cast<uint64_t>(rect.height());
			return rect_inside(owned_rect, rect) && area <= std::numeric_limits<size_t>::max() &&
				value_count == static_cast<size_t>(area) && coverage_count == static_cast<size_t>(area);
		}

		auto validate_canonical_coverage(
			const TerrainSampleRect& rect,
			const std::vector<float>& coverage) -> bool
		{
			bool has_non_zero = false;
			bool touches_min_x = false;
			bool touches_max_x = false;
			bool touches_min_z = false;
			bool touches_max_z = false;
			for (uint32_t local_z = 0u; local_z < rect.height(); ++local_z)
			{
				for (uint32_t local_x = 0u; local_x < rect.width(); ++local_x)
				{
					const float value = coverage[
						static_cast<size_t>(local_z) * rect.width() + local_x];
					if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
					{
						return false;
					}
					if (value == 0.0f)
					{
						continue;
					}
					has_non_zero = true;
					touches_min_x = touches_min_x || local_x == 0u;
					touches_max_x = touches_max_x || local_x + 1u == rect.width();
					touches_min_z = touches_min_z || local_z == 0u;
					touches_max_z = touches_max_z || local_z + 1u == rect.height();
				}
			}
			return has_non_zero && touches_min_x && touches_max_x &&
				touches_min_z && touches_max_z;
		}

		auto validate_layer_contents(
			const TerrainWorkingSet& working_set,
			const TerrainEditLayer& layer,
			std::string* out_error) -> bool
		{
			if (!layer.id.is_valid() || layer.name.size() > k_max_layer_name_size ||
				!std::isfinite(layer.strength) || !valid_blend_mode(layer.height_blend_mode))
			{
				return fail(out_error, "Terrain layer stack metadata is invalid.");
			}
			for (size_t block_index = 0u; block_index < layer.height_blocks.size(); ++block_index)
			{
				const TerrainSparseHeightBlock& block = layer.height_blocks[block_index];
				if (!validate_block_shape(
						working_set, block.owner, block.changed_rect,
						block.values.size(), block.coverage.size()) ||
					!std::all_of(block.values.begin(), block.values.end(),
						[](const float value) { return std::isfinite(value); }) ||
					!validate_canonical_coverage(block.changed_rect, block.coverage))
				{
					return fail(out_error, "Terrain layer stack Height block shape is invalid.");
				}
				for (size_t previous = 0u; previous < block_index; ++previous)
				{
					if (layer.height_blocks[previous].owner == block.owner)
					{
						return fail(out_error, "Terrain layer stack Height owners must be unique.");
					}
				}
			}
			for (size_t block_index = 0u; block_index < layer.weight_blocks.size(); ++block_index)
			{
				const TerrainSparseWeightBlock& block = layer.weight_blocks[block_index];
				if (!validate_block_shape(
						working_set, block.owner, block.changed_rect,
						block.values.size(), block.coverage.size()) ||
					!std::all_of(block.values.begin(), block.values.end(),
						[](const std::array<float, k_terrain_material_layer_count>& values)
						{
							return std::all_of(values.begin(), values.end(),
								[](const float value) { return std::isfinite(value); });
						}) ||
					!validate_canonical_coverage(block.changed_rect, block.coverage))
				{
					return fail(out_error, "Terrain layer stack Weight block shape is invalid.");
				}
				for (size_t previous = 0u; previous < block_index; ++previous)
				{
					if (layer.weight_blocks[previous].owner == block.owner)
					{
						return fail(out_error, "Terrain layer stack Weight owners must be unique.");
					}
				}
			}
			return true;
		}

		auto validate_working_set(const TerrainWorkingSet& working_set, std::string* out_error) -> bool
		{
			if (working_set.asset_id == 0u || working_set.content_generation == 0u ||
				!is_valid_terrain_grid_layout(working_set.layout))
			{
				return fail(out_error, "Terrain layer stack working-set header is invalid.");
			}
			for (size_t layer_index = 0u; layer_index < working_set.edit_layers.size(); ++layer_index)
			{
				const TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				if (!validate_layer_contents(working_set, layer, out_error))
				{
					return false;
				}
				for (size_t previous = 0u; previous < layer_index; ++previous)
				{
					if (working_set.edit_layers[previous].id == layer.id)
					{
						return fail(out_error, "Terrain layer stack IDs must be unique.");
					}
				}
			}
			for (const TerrainComponentCoord coord : working_set.dirty_components)
			{
				if (coord.x >= working_set.layout.component_count_x ||
					coord.z >= working_set.layout.component_count_z)
				{
					return fail(out_error, "Terrain layer stack dirty component is invalid.");
				}
			}
			return true;
		}

		void move_layer_id(
			std::vector<TerrainLayerId>& order,
			const size_t source_index,
			const size_t destination_index)
		{
			if (source_index < destination_index)
			{
				std::rotate(
					order.begin() + source_index,
					order.begin() + source_index + 1u,
					order.begin() + destination_index + 1u);
			}
			else if (source_index > destination_index)
			{
				std::rotate(
					order.begin() + destination_index,
					order.begin() + source_index,
					order.begin() + source_index + 1u);
			}
		}

		void move_layer(
			std::vector<TerrainEditLayer>& layers,
			const size_t source_index,
			const size_t destination_index)
		{
			if (source_index < destination_index)
			{
				std::rotate(
					layers.begin() + source_index,
					layers.begin() + source_index + 1u,
					layers.begin() + destination_index + 1u);
			}
			else if (source_index > destination_index)
			{
				std::rotate(
					layers.begin() + destination_index,
					layers.begin() + source_index,
					layers.begin() + source_index + 1u);
			}
		}

		auto splitmix64(uint64_t value) -> uint64_t
		{
			value += 0x9e3779b97f4a7c15ull;
			value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
			value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
			return value ^ (value >> 31u);
		}

		auto generate_layer_id(const TerrainWorkingSet& working_set) -> TerrainLayerId
		{
			for (uint64_t salt = 0u;; ++salt)
			{
				const uint64_t low = splitmix64(
					working_set.asset_id ^ working_set.content_generation ^
					(static_cast<uint64_t>(working_set.edit_layers.size()) << 32u) ^ salt);
				const uint64_t high = splitmix64(low ^ 0xa0761d6478bd642full);
				TerrainLayerId candidate{};
				for (size_t byte_index = 0u; byte_index < 8u; ++byte_index)
				{
					candidate.bytes[byte_index] =
						static_cast<uint8_t>(low >> (byte_index * 8u));
					candidate.bytes[byte_index + 8u] =
						static_cast<uint8_t>(high >> (byte_index * 8u));
				}
				size_t ignored = 0u;
				if (candidate.is_valid() &&
					!find_layer_index(working_set.edit_layers, candidate, ignored))
				{
					return candidate;
				}
			}
		}

		auto append_layer_dirty(
			const TerrainWorkingSet& working_set,
			const TerrainEditLayer& layer,
			std::vector<TerrainComponentCoord>& dirty,
			std::string* out_error) -> bool
		{
			for (const TerrainSparseHeightBlock& block : layer.height_blocks)
			{
				const std::vector<TerrainComponentCoord> block_dirty =
					collect_dirty_terrain_components(working_set.layout, block.changed_rect);
				if (block_dirty.empty())
				{
					return fail(out_error, "Terrain layer stack Height dirty halo is invalid.");
				}
				dirty.insert(dirty.end(), block_dirty.begin(), block_dirty.end());
			}
			for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
			{
				const std::vector<TerrainComponentCoord> block_dirty =
					collect_dirty_terrain_components(working_set.layout, block.changed_rect);
				if (block_dirty.empty())
				{
					return fail(out_error, "Terrain layer stack Weight dirty halo is invalid.");
				}
				dirty.insert(dirty.end(), block_dirty.begin(), block_dirty.end());
			}
			return true;
		}

		auto make_dirty_union(
			const TerrainWorkingSet& working_set,
			const std::vector<const TerrainEditLayer*>& affected_layers,
			std::vector<TerrainComponentCoord>& out_authoritative,
			std::vector<TerrainComponentCoord>& out_result,
			std::string* out_error) -> bool
		{
			std::vector<TerrainComponentCoord> dirty = working_set.dirty_components;
			for (const TerrainEditLayer* layer : affected_layers)
			{
				if (layer == nullptr || !append_layer_dirty(working_set, *layer, dirty, out_error))
				{
					return layer != nullptr ? false :
						fail(out_error, "Terrain layer stack affected layer is missing.");
				}
			}
			std::sort(dirty.begin(), dirty.end(), component_coord_less);
			dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
			std::vector<TerrainComponentCoord> result = dirty;
			out_authoritative.swap(dirty);
			out_result.swap(result);
			return true;
		}

		auto finish_no_change(
			const TerrainWorkingSet& working_set,
			TerrainLayerStackPatch& out_patch,
			std::vector<TerrainComponentCoord>& out_dirty_components) -> bool
		{
			TerrainLayerStackPatch empty_patch{};
			std::vector<TerrainComponentCoord> current_dirty = working_set.dirty_components;
			out_patch = std::move(empty_patch);
			out_dirty_components.swap(current_dirty);
			return true;
		}

		auto validate_patch_shape(const TerrainLayerStackPatch& patch, std::string* out_error) -> bool
		{
			if (patch.asset_id == 0u || !patch.layer_id.is_valid() || !valid_kind(patch.kind) ||
				!validate_order(patch.before_order) || !validate_order(patch.after_order))
			{
				return fail(out_error, "Terrain layer stack patch identity is invalid.");
			}
			size_t before_index = 0u;
			size_t after_index = 0u;
			const bool in_before = find_id_index(patch.before_order, patch.layer_id, before_index);
			const bool in_after = find_id_index(patch.after_order, patch.layer_id, after_index);

			switch (patch.kind)
			{
			case TerrainLayerStackEditKind::Add:
			case TerrainLayerStackEditKind::Duplicate:
				if (in_before || !in_after ||
					patch.after_order.size() != patch.before_order.size() + 1u ||
					!patch.retained_layer || patch.retained_layer->id != patch.layer_id ||
					patch.before_metadata.has_value() || !patch.after_metadata.has_value() ||
					!valid_metadata(*patch.after_metadata) ||
					!metadata_matches_layer(*patch.retained_layer, *patch.after_metadata) ||
					(patch.kind == TerrainLayerStackEditKind::Add &&
						(!patch.retained_layer->height_blocks.empty() ||
							!patch.retained_layer->weight_blocks.empty())))
				{
					return fail(out_error, "Terrain layer stack insertion patch is invalid.");
				}
				{
					std::vector<TerrainLayerId> expected = patch.before_order;
					expected.insert(expected.begin() + after_index, patch.layer_id);
					if (expected != patch.after_order)
					{
						return fail(out_error, "Terrain layer stack insertion order is invalid.");
					}
				}
				break;
			case TerrainLayerStackEditKind::Delete:
				if (!in_before || in_after ||
					patch.before_order.size() != patch.after_order.size() + 1u ||
					!patch.retained_layer || patch.retained_layer->id != patch.layer_id ||
					!patch.before_metadata.has_value() || patch.after_metadata.has_value() ||
					!valid_metadata(*patch.before_metadata) ||
					!metadata_matches_layer(*patch.retained_layer, *patch.before_metadata))
				{
					return fail(out_error, "Terrain layer stack deletion patch is invalid.");
				}
				{
					std::vector<TerrainLayerId> expected = patch.before_order;
					expected.erase(expected.begin() + before_index);
					if (expected != patch.after_order)
					{
						return fail(out_error, "Terrain layer stack deletion order is invalid.");
					}
				}
				break;
			case TerrainLayerStackEditKind::Move:
				if (!in_before || !in_after || patch.before_order.size() != patch.after_order.size() ||
					patch.before_order == patch.after_order || patch.before_metadata.has_value() ||
					patch.after_metadata.has_value() || patch.retained_layer)
				{
					return fail(out_error, "Terrain layer stack move patch is invalid.");
				}
				{
					std::vector<TerrainLayerId> expected = patch.before_order;
					move_layer_id(expected, before_index, after_index);
					if (expected != patch.after_order)
					{
						return fail(out_error, "Terrain layer stack move order is invalid.");
					}
				}
				break;
			case TerrainLayerStackEditKind::Rename:
			case TerrainLayerStackEditKind::SetVisible:
			case TerrainLayerStackEditKind::SetOpacity:
			case TerrainLayerStackEditKind::SetLocked:
				if (!in_before || !in_after || patch.before_order != patch.after_order ||
					!patch.before_metadata.has_value() || !patch.after_metadata.has_value() ||
					patch.before_metadata->id != patch.layer_id ||
					patch.after_metadata->id != patch.layer_id || patch.retained_layer)
				{
					return fail(out_error, "Terrain layer stack metadata patch is invalid.");
				}
				if (!valid_metadata(*patch.before_metadata) ||
					!valid_metadata(*patch.after_metadata))
				{
					return fail(out_error, "Terrain layer stack patch metadata value is invalid.");
				}
				{
					const TerrainLayerMetadata& before = *patch.before_metadata;
					const TerrainLayerMetadata& after = *patch.after_metadata;
					const bool same_name = before.name == after.name;
					const bool same_visible = before.visible == after.visible;
					const bool same_locked = before.locked == after.locked;
					const bool same_strength = before.strength == after.strength;
					const bool same_blend = before.height_blend_mode == after.height_blend_mode;
					bool valid_transition = false;
					switch (patch.kind)
					{
					case TerrainLayerStackEditKind::Rename:
						valid_transition = !same_name && same_visible && same_locked &&
							same_strength && same_blend;
						break;
					case TerrainLayerStackEditKind::SetVisible:
						valid_transition = same_name && !same_visible && same_locked &&
							same_strength && same_blend;
						break;
					case TerrainLayerStackEditKind::SetOpacity:
						valid_transition = same_name && same_visible && same_locked &&
							!same_strength && same_blend &&
							after.strength >= 0.0f && after.strength <= 1.0f;
						break;
					case TerrainLayerStackEditKind::SetLocked:
						valid_transition = same_name && same_visible && !same_locked &&
							same_strength && same_blend;
						break;
					default:
						break;
					}
					if (!valid_transition)
					{
						return fail(out_error, "Terrain layer stack metadata transition is invalid.");
					}
				}
				break;
			}
			return true;
		}
	}

	auto apply_terrain_layer_stack_edit(
		TerrainWorkingSet& working_set,
		const TerrainLayerStackEdit& edit,
		TerrainLayerStackPatch& out_patch,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error) -> bool
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		try
		{
			if (!valid_kind(edit.kind) || !validate_working_set(working_set, out_error))
			{
				return valid_kind(edit.kind) ? false :
					fail(out_error, "Terrain layer stack edit kind is invalid.");
			}

			TerrainLayerStackPatch patch{};
			patch.asset_id = working_set.asset_id;
			patch.kind = edit.kind;
			patch.before_order = make_order(working_set.edit_layers);
			patch.after_order = patch.before_order;
			TerrainEditLayer candidate{};
			std::vector<const TerrainEditLayer*> affected_layers{};
			size_t layer_index = 0u;
			bool has_layer = find_layer_index(working_set.edit_layers, edit.layer_id, layer_index);

			switch (edit.kind)
			{
			case TerrainLayerStackEditKind::Add:
			{
				if (edit.destination_index > working_set.edit_layers.size() ||
					edit.name.size() > k_max_layer_name_size || !valid_blend_mode(edit.blend_mode))
				{
					return fail(out_error, "Terrain layer stack add parameters are invalid.");
				}
				const TerrainLayerId new_id = edit.new_layer_id.is_valid()
					? edit.new_layer_id : generate_layer_id(working_set);
				size_t ignored = 0u;
				if (find_layer_index(working_set.edit_layers, new_id, ignored))
				{
					return fail(out_error, "Terrain layer stack add ID already exists.");
				}
				candidate.id = new_id;
				candidate.name = edit.name;
				candidate.height_blend_mode = edit.blend_mode;
				patch.layer_id = new_id;
				patch.after_order.insert(
					patch.after_order.begin() + edit.destination_index, new_id);
				patch.after_metadata = metadata_from_layer(candidate);
				patch.retained_layer = std::make_shared<TerrainEditLayer>(candidate);
				break;
			}
			case TerrainLayerStackEditKind::Delete:
				if (!has_layer)
				{
					return fail(out_error, "Terrain layer stack delete target is missing.");
				}
				patch.layer_id = edit.layer_id;
				patch.before_metadata = metadata_from_layer(working_set.edit_layers[layer_index]);
				patch.retained_layer =
					std::make_shared<TerrainEditLayer>(working_set.edit_layers[layer_index]);
				patch.after_order.erase(patch.after_order.begin() + layer_index);
				affected_layers.push_back(&working_set.edit_layers[layer_index]);
				break;
			case TerrainLayerStackEditKind::Duplicate:
			{
				if (!has_layer || edit.destination_index > working_set.edit_layers.size() ||
					edit.name.size() > k_max_layer_name_size)
				{
					return fail(out_error, "Terrain layer stack duplicate parameters are invalid.");
				}
				const TerrainLayerId new_id = edit.new_layer_id.is_valid()
					? edit.new_layer_id : generate_layer_id(working_set);
				size_t ignored = 0u;
				if (find_layer_index(working_set.edit_layers, new_id, ignored))
				{
					return fail(out_error, "Terrain layer stack duplicate ID already exists.");
				}
				candidate = working_set.edit_layers[layer_index];
				candidate.id = new_id;
				if (!edit.name.empty())
				{
					candidate.name = edit.name;
				}
				patch.layer_id = new_id;
				patch.after_order.insert(
					patch.after_order.begin() + edit.destination_index, new_id);
				patch.after_metadata = metadata_from_layer(candidate);
				patch.retained_layer = std::make_shared<TerrainEditLayer>(candidate);
				affected_layers.push_back(&working_set.edit_layers[layer_index]);
				break;
			}
			case TerrainLayerStackEditKind::Rename:
				if (!has_layer || edit.name.size() > k_max_layer_name_size)
				{
					return fail(out_error, "Terrain layer stack rename parameters are invalid.");
				}
				if (working_set.edit_layers[layer_index].name == edit.name)
				{
					return finish_no_change(working_set, out_patch, out_dirty_components);
				}
				patch.layer_id = edit.layer_id;
				patch.before_metadata = metadata_from_layer(working_set.edit_layers[layer_index]);
				patch.after_metadata = *patch.before_metadata;
				patch.after_metadata->name = edit.name;
				break;
			case TerrainLayerStackEditKind::Move:
				if (!has_layer || edit.destination_index >= working_set.edit_layers.size())
				{
					return fail(out_error, "Terrain layer stack move parameters are invalid.");
				}
				if (layer_index == edit.destination_index)
				{
					return finish_no_change(working_set, out_patch, out_dirty_components);
				}
				patch.layer_id = edit.layer_id;
				for (size_t index = std::min(layer_index, edit.destination_index);
					index <= std::max(layer_index, edit.destination_index); ++index)
				{
					affected_layers.push_back(&working_set.edit_layers[index]);
				}
				move_layer_id(patch.after_order, layer_index, edit.destination_index);
				break;
			case TerrainLayerStackEditKind::SetVisible:
				if (!has_layer)
				{
					return fail(out_error, "Terrain layer stack visibility target is missing.");
				}
				if (working_set.edit_layers[layer_index].visible == edit.flag_value)
				{
					return finish_no_change(working_set, out_patch, out_dirty_components);
				}
				patch.layer_id = edit.layer_id;
				patch.before_metadata = metadata_from_layer(working_set.edit_layers[layer_index]);
				patch.after_metadata = *patch.before_metadata;
				patch.after_metadata->visible = edit.flag_value;
				affected_layers.push_back(&working_set.edit_layers[layer_index]);
				break;
			case TerrainLayerStackEditKind::SetOpacity:
				if (!has_layer || !std::isfinite(edit.opacity) ||
					edit.opacity < 0.0f || edit.opacity > 1.0f)
				{
					return fail(out_error, "Terrain layer stack opacity parameters are invalid.");
				}
				if (working_set.edit_layers[layer_index].strength == edit.opacity)
				{
					return finish_no_change(working_set, out_patch, out_dirty_components);
				}
				patch.layer_id = edit.layer_id;
				patch.before_metadata = metadata_from_layer(working_set.edit_layers[layer_index]);
				patch.after_metadata = *patch.before_metadata;
				patch.after_metadata->strength = edit.opacity;
				affected_layers.push_back(&working_set.edit_layers[layer_index]);
				break;
			case TerrainLayerStackEditKind::SetLocked:
				if (!has_layer)
				{
					return fail(out_error, "Terrain layer stack lock target is missing.");
				}
				if (working_set.edit_layers[layer_index].locked == edit.flag_value)
				{
					return finish_no_change(working_set, out_patch, out_dirty_components);
				}
				patch.layer_id = edit.layer_id;
				patch.before_metadata = metadata_from_layer(working_set.edit_layers[layer_index]);
				patch.after_metadata = *patch.before_metadata;
				patch.after_metadata->locked = edit.flag_value;
				break;
			}

			if (working_set.content_generation == std::numeric_limits<uint64_t>::max())
			{
				return fail(out_error, "Terrain layer stack content generation overflowed.");
			}
			std::vector<TerrainComponentCoord> authoritative_dirty{};
			std::vector<TerrainComponentCoord> dirty_output{};
			if (!make_dirty_union(
					working_set, affected_layers, authoritative_dirty, dirty_output, out_error))
			{
				return false;
			}

			if (edit.kind == TerrainLayerStackEditKind::Add ||
				edit.kind == TerrainLayerStackEditKind::Duplicate)
			{
				working_set.edit_layers.reserve(working_set.edit_layers.size() + 1u);
				working_set.edit_layers.insert(
					working_set.edit_layers.begin() + edit.destination_index,
					std::move(candidate));
			}
			else if (edit.kind == TerrainLayerStackEditKind::Delete)
			{
				working_set.edit_layers.erase(working_set.edit_layers.begin() + layer_index);
			}
			else if (edit.kind == TerrainLayerStackEditKind::Move)
			{
				move_layer(working_set.edit_layers, layer_index, edit.destination_index);
			}
			else
			{
				TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				std::string prepared_name = patch.after_metadata->name;
				layer.name.swap(prepared_name);
				layer.visible = patch.after_metadata->visible;
				layer.locked = patch.after_metadata->locked;
				layer.strength = patch.after_metadata->strength;
				layer.height_blend_mode = patch.after_metadata->height_blend_mode;
			}

			working_set.content_generation += 1u;
			working_set.dirty_components.swap(authoritative_dirty);
			out_patch = std::move(patch);
			out_dirty_components.swap(dirty_output);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain layer stack edit allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain layer stack edit size is unsupported.");
		}
	}

	auto apply_terrain_layer_stack_patch(
		TerrainWorkingSet& working_set,
		const TerrainLayerStackPatch& patch,
		const TerrainEditPatchDirection direction,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error) -> bool
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		try
		{
			if ((direction != TerrainEditPatchDirection::Undo &&
				direction != TerrainEditPatchDirection::Redo) ||
				!validate_working_set(working_set, out_error) ||
				!validate_patch_shape(patch, out_error))
			{
				return direction == TerrainEditPatchDirection::Undo ||
					direction == TerrainEditPatchDirection::Redo
					? false : fail(out_error, "Terrain layer stack patch direction is invalid.");
			}
			if (patch.asset_id != working_set.asset_id)
			{
				return fail(out_error, "Terrain layer stack patch asset does not match.");
			}
			if (working_set.content_generation == std::numeric_limits<uint64_t>::max())
			{
				return fail(out_error, "Terrain layer stack patch content generation overflowed.");
			}

			const std::vector<TerrainLayerId>& source_order =
				direction == TerrainEditPatchDirection::Undo
				? patch.after_order : patch.before_order;
			const std::vector<TerrainLayerId>& target_order =
				direction == TerrainEditPatchDirection::Undo
				? patch.before_order : patch.after_order;
			if (!order_matches(working_set.edit_layers, source_order))
			{
				return fail(out_error, "Terrain layer stack patch source order does not match.");
			}

			size_t source_index = 0u;
			size_t target_index = 0u;
			const bool source_has_layer = find_id_index(source_order, patch.layer_id, source_index);
			const bool target_has_layer = find_id_index(target_order, patch.layer_id, target_index);
			if (patch.retained_layer &&
				!validate_layer_contents(working_set, *patch.retained_layer, out_error))
			{
				return false;
			}
			const TerrainLayerMetadata* source_metadata = direction == TerrainEditPatchDirection::Undo
				? (patch.after_metadata ? &*patch.after_metadata : nullptr)
				: (patch.before_metadata ? &*patch.before_metadata : nullptr);
			const TerrainLayerMetadata* target_metadata = direction == TerrainEditPatchDirection::Undo
				? (patch.before_metadata ? &*patch.before_metadata : nullptr)
				: (patch.after_metadata ? &*patch.after_metadata : nullptr);
			if (source_has_layer && source_metadata != nullptr &&
				!metadata_matches_layer(working_set.edit_layers[source_index], *source_metadata))
			{
				return fail(out_error, "Terrain layer stack patch source metadata does not match.");
			}
			if (source_has_layer && !target_has_layer && patch.retained_layer &&
				!layer_equal(working_set.edit_layers[source_index], *patch.retained_layer))
			{
				return fail(out_error, "Terrain layer stack patch retained layer does not match.");
			}

			std::vector<const TerrainEditLayer*> affected_layers{};
			if (patch.kind == TerrainLayerStackEditKind::Delete ||
				patch.kind == TerrainLayerStackEditKind::Duplicate)
			{
				affected_layers.push_back(patch.retained_layer.get());
			}
			else if (patch.kind == TerrainLayerStackEditKind::SetVisible ||
				patch.kind == TerrainLayerStackEditKind::SetOpacity)
			{
				affected_layers.push_back(&working_set.edit_layers[source_index]);
			}
			else if (patch.kind == TerrainLayerStackEditKind::Move)
			{
				for (size_t index = 0u; index < source_order.size(); ++index)
				{
					size_t new_index = 0u;
					if (!find_id_index(target_order, source_order[index], new_index))
					{
						return fail(out_error, "Terrain layer stack move patch order is not a permutation.");
					}
					if (index != new_index)
					{
						affected_layers.push_back(&working_set.edit_layers[index]);
					}
				}
			}

			std::vector<TerrainComponentCoord> authoritative_dirty{};
			std::vector<TerrainComponentCoord> dirty_output{};
			if (!make_dirty_union(
					working_set, affected_layers, authoritative_dirty, dirty_output, out_error))
			{
				return false;
			}
			TerrainEditLayer insertion_candidate{};
			if (!source_has_layer && target_has_layer)
			{
				insertion_candidate = *patch.retained_layer;
				working_set.edit_layers.reserve(working_set.edit_layers.size() + 1u);
			}
			std::string prepared_name{};
			if (source_has_layer && target_has_layer && target_metadata != nullptr)
			{
				if (target_metadata->name.size() > k_max_layer_name_size ||
					!std::isfinite(target_metadata->strength) ||
					!valid_blend_mode(target_metadata->height_blend_mode))
				{
					return fail(out_error, "Terrain layer stack patch target metadata is invalid.");
				}
				prepared_name = target_metadata->name;
			}

			if (!source_has_layer && target_has_layer)
			{
				working_set.edit_layers.insert(
					working_set.edit_layers.begin() + target_index,
					std::move(insertion_candidate));
			}
			else if (source_has_layer && !target_has_layer)
			{
				working_set.edit_layers.erase(working_set.edit_layers.begin() + source_index);
			}
			else if (patch.kind == TerrainLayerStackEditKind::Move)
			{
				move_layer(working_set.edit_layers, source_index, target_index);
			}
			else if (target_metadata != nullptr)
			{
				TerrainEditLayer& layer = working_set.edit_layers[source_index];
				layer.name.swap(prepared_name);
				layer.visible = target_metadata->visible;
				layer.locked = target_metadata->locked;
				layer.strength = target_metadata->strength;
				layer.height_blend_mode = target_metadata->height_blend_mode;
			}
			else
			{
				return fail(out_error, "Terrain layer stack patch transition is invalid.");
			}

			working_set.content_generation += 1u;
			working_set.dirty_components.swap(authoritative_dirty);
			out_dirty_components.swap(dirty_output);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain layer stack patch allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain layer stack patch size is unsupported.");
		}
	}
}
