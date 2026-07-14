#pragma once

#include "Base/hcore.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace AshEngine
{
	using TerrainAssetId = uint64_t;

	static constexpr uint32_t k_terrain_sample_count = 8193u;
	static constexpr uint32_t k_terrain_component_count = 32u;
	static constexpr uint32_t k_terrain_component_quad_count = 256u;
	static constexpr uint32_t k_terrain_component_sample_count = 257u;
	static constexpr uint32_t k_terrain_material_layer_count = 8u;

	struct TerrainLayerId
	{
		std::array<uint8_t, 16> bytes{};
		ASH_API bool is_valid() const;
		friend bool operator==(const TerrainLayerId& lhs, const TerrainLayerId& rhs)
		{
			return lhs.bytes == rhs.bytes;
		}
		friend bool operator!=(const TerrainLayerId& lhs, const TerrainLayerId& rhs)
		{
			return !(lhs == rhs);
		}
	};

	struct TerrainComponentCoord
	{
		uint16_t x = 0;
		uint16_t z = 0;
		friend bool operator==(const TerrainComponentCoord& lhs, const TerrainComponentCoord& rhs)
		{
			return lhs.x == rhs.x && lhs.z == rhs.z;
		}
	};

	struct TerrainSampleRect
	{
		uint32_t min_x = 0;
		uint32_t min_z = 0;
		uint32_t max_x_exclusive = 0;
		uint32_t max_z_exclusive = 0;
		uint32_t width() const { return max_x_exclusive - min_x; }
		uint32_t height() const { return max_z_exclusive - min_z; }
		bool empty() const { return min_x >= max_x_exclusive || min_z >= max_z_exclusive; }
	};

	struct TerrainGridLayout
	{
		uint32_t sample_count_x = k_terrain_sample_count;
		uint32_t sample_count_z = k_terrain_sample_count;
		uint32_t component_count_x = k_terrain_component_count;
		uint32_t component_count_z = k_terrain_component_count;
		uint32_t component_quad_count = k_terrain_component_quad_count;
		float sample_spacing_meters = 1.0f;
	};

	struct TerrainHeightMapping
	{
		float height_offset = 0.0f;
		float height_range = 1024.0f;
	};

	enum class TerrainHeightBlendMode : uint8_t
	{
		Additive = 0,
		Alpha
	};

	struct TerrainMaterialLayerDesc
	{
		std::string name{};
		std::string base_color_asset_path{};
		std::string normal_asset_path{};
		std::string orm_asset_path{};
	};

	struct TerrainComponentSnapshot
	{
		TerrainComponentCoord coord{};
		uint64_t content_generation = 0;
		uint32_t sample_width = 0;
		uint32_t sample_height = 0;
		std::vector<float> heights{};
		std::vector<std::array<uint8_t, k_terrain_material_layer_count>> weights{};
		std::array<uint32_t, 10> min_max_level_offsets{};
		std::vector<glm::vec2> min_max_levels{};
		std::array<float, 9> lod_errors{};
	};

	struct TerrainSparseHeightBlock
	{
		TerrainComponentCoord owner{};
		TerrainSampleRect changed_rect{};
		std::vector<float> values{};
		std::vector<float> coverage{};
	};

	struct TerrainSparseWeightBlock
	{
		TerrainComponentCoord owner{};
		TerrainSampleRect changed_rect{};
		std::vector<std::array<float, k_terrain_material_layer_count>> values{};
		std::vector<float> coverage{};
	};

	struct TerrainEditLayer
	{
		TerrainLayerId id{};
		std::string name{};
		bool visible = true;
		bool locked = false;
		float strength = 1.0f;
		TerrainHeightBlendMode height_blend_mode = TerrainHeightBlendMode::Additive;
		std::vector<TerrainSparseHeightBlock> height_blocks{};
		std::vector<TerrainSparseWeightBlock> weight_blocks{};
	};

	// Transient container lineage carried with an immutable snapshot. These fields
	// are not serialized as Terrain content; they identify the exact header/file
	// revision that produced the snapshot for checked Editor save/reload decisions.
	struct TerrainContainerDescriptorRevision
	{
		uint64_t generation = 0;
		uint64_t index_offset = 0;
		uint64_t index_size = 0;
		uint32_t index_crc32 = 0;

		auto operator==(const TerrainContainerDescriptorRevision& other) const noexcept -> bool
		{
			return generation == other.generation &&
				index_offset == other.index_offset &&
				index_size == other.index_size &&
				index_crc32 == other.index_crc32;
		}

		auto operator!=(const TerrainContainerDescriptorRevision& other) const noexcept -> bool
		{
			return !(*this == other);
		}
	};

	struct TerrainContainerRevision
	{
		uint64_t file_size = 0;
		std::array<TerrainContainerDescriptorRevision, 2> descriptors{};

		auto is_valid() const noexcept -> bool
		{
			return file_size != 0u &&
				(descriptors[0].generation != 0u || descriptors[1].generation != 0u);
		}

		auto operator==(const TerrainContainerRevision& other) const noexcept -> bool
		{
			return file_size == other.file_size && descriptors == other.descriptors;
		}

		auto operator!=(const TerrainContainerRevision& other) const noexcept -> bool
		{
			return !(*this == other);
		}
	};

	struct TerrainAssetSnapshot
	{
		TerrainAssetId asset_id = 0;
		std::filesystem::path source_path{};
		TerrainGridLayout layout{};
		TerrainHeightMapping height_mapping{};
		std::array<TerrainMaterialLayerDesc, k_terrain_material_layer_count> material_layers{};
		uint64_t content_generation = 0;
		uint64_t residency_revision = 0;
		TerrainContainerRevision source_revision{};
		bool failed = false;
		// Transient cooperative-writer/revision races may be retried and must not
		// poison AssetDatabase's persistent failure cache or freeze Editor reload.
		bool retryable_failure = false;
		std::string failure_detail{};
		// editor begin 修改原因：让 Terrain 恢复 UI 区分已加载旧代与被拒绝的新代，并保持只读恢复语义。
		bool recovered_previous_generation = false;
		uint64_t rejected_content_generation = 0;
		std::string recovery_detail{};
		// editor end
		std::shared_ptr<const std::vector<uint16_t>> base_heights{};
		std::shared_ptr<const std::vector<TerrainEditLayer>> edit_layers{};
		std::vector<std::shared_ptr<const TerrainComponentSnapshot>> components{};
	};

	struct TerrainDirtyComponentPayload
	{
		TerrainComponentCoord coord{};
		uint64_t content_generation = 0;
		std::shared_ptr<const TerrainComponentSnapshot> component{};
	};

	// Trusted mutable editing state. Construct it with make_terrain_working_set(),
	// then mutate scalar/block data only through brush/patch operations that preserve
	// finite values, block shapes, ownership, and unique layer IDs.
	struct TerrainWorkingSet
	{
		TerrainAssetId asset_id = 0;
		std::filesystem::path source_path{};
		TerrainGridLayout layout{};
		TerrainHeightMapping height_mapping{};
		uint64_t content_generation = 0;
		uint64_t residency_revision = 0;
		std::vector<uint16_t> base_heights{};
		std::array<TerrainMaterialLayerDesc, k_terrain_material_layer_count> material_layers{};
		std::vector<TerrainEditLayer> edit_layers{};
		std::vector<std::shared_ptr<const TerrainComponentSnapshot>> components{};
		std::vector<TerrainComponentCoord> dirty_components{};
	};

	ASH_API auto make_default_terrain_grid_layout() -> TerrainGridLayout;
	ASH_API auto is_valid_terrain_grid_layout(const TerrainGridLayout& layout) -> bool;
	ASH_API auto get_terrain_sample_owner(
		const TerrainGridLayout& layout,
		uint32_t sample_x,
		uint32_t sample_z) -> TerrainComponentCoord;
	ASH_API auto get_terrain_component_owned_rect(
		const TerrainGridLayout& layout,
		TerrainComponentCoord coord) -> TerrainSampleRect;
	ASH_API auto get_terrain_component_snapshot_rect(
		const TerrainGridLayout& layout,
		TerrainComponentCoord coord) -> TerrainSampleRect;
	ASH_API auto collect_terrain_components_sharing_sample(
		const TerrainGridLayout& layout,
		uint32_t sample_x,
		uint32_t sample_z) -> std::vector<TerrainComponentCoord>;
	ASH_API auto encode_terrain_height_r16(
		float world_height,
		const TerrainHeightMapping& mapping) -> uint16_t;
	ASH_API auto decode_terrain_height_r16(
		uint16_t encoded_height,
		const TerrainHeightMapping& mapping) -> float;
	ASH_API auto create_flat_terrain_snapshot(
		TerrainAssetId asset_id,
		const TerrainGridLayout& layout,
		const TerrainHeightMapping& mapping,
		float world_height,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		std::string* out_error = nullptr) -> bool;
}
