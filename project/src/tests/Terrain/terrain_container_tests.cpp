#include "doctest.h"

#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/TerrainContainerFormat.h"
#include "Function/Asset/TerrainBlockCodec.h"
#include "Function/Asset/TerrainSpatialData.h"
#include "Terrain/TerrainCommitLeaseTestUtils.h"
#include "Terrain/TerrainTestUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
	using AshEngine::TerrainContainerFormat::BlockKind;
	using AshEngine::TerrainContainerFormat::BlockRecordDisk;
	using AshEngine::TerrainContainerFormat::FileHeaderDisk;

	auto TestDirectory() -> std::filesystem::path
	{
		const std::filesystem::path directory =
			"Intermediate/test-temp/tests/terrain-container";
		std::filesystem::create_directories(directory);
		return directory;
	}

	auto SmallContainerLayout() -> AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x = 9u;
		layout.sample_count_z = 5u;
		layout.component_count_x = 2u;
		layout.component_count_z = 1u;
		layout.component_quad_count = 4u;
		layout.sample_spacing_meters = 2.0f;
		return layout;
	}

	auto IncrementalContainerLayout() -> AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x = 13u;
		layout.sample_count_z = 9u;
		layout.component_count_x = 3u;
		layout.component_count_z = 2u;
		layout.component_quad_count = 4u;
		layout.sample_spacing_meters = 1.0f;
		return layout;
	}

	auto MakeContainerSnapshot(bool mixed_component_payloads = false)
		-> std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			91u,
			SmallContainerLayout(),
			{ -200.0f, 800.0f },
			0.0f,
			flat,
			&error));
		REQUIRE(error.empty());

		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
		snapshot->source_path = "not-persisted/runtime-source.AshTerrain";
		snapshot->residency_revision = 17u;
		snapshot->material_layers[0] = {
			"Rock", "textures/rock_base.ash", "textures/rock_normal.ash", "textures/rock_orm.ash"
		};
		snapshot->material_layers[1] = {
			"Mud", "textures/mud_base.ash", "textures/mud_normal.ash", "textures/mud_orm.ash"
		};

		if (mixed_component_payloads)
		{
			auto constant = std::make_shared<AshEngine::TerrainComponentSnapshot>(
				*snapshot->components[0u]);
			std::fill(constant->heights.begin(), constant->heights.end(), 0.0f);
			REQUIRE(AshEngine::build_terrain_component_spatial_data(
				*constant, constant->sample_width, constant->sample_height, &error));
			snapshot->components[0u] = std::move(constant);

			auto varied = std::make_shared<AshEngine::TerrainComponentSnapshot>(
				*snapshot->components[1u]);
			for (size_t index = 0u; index < varied->heights.size(); ++index)
			{
				varied->heights[index] =
					static_cast<float>((index * 37u + 11u) % 101u) * 0.1234567f;
			}
			REQUIRE(AshEngine::build_terrain_component_spatial_data(
				*varied, varied->sample_width, varied->sample_height, &error));
			snapshot->components[1u] = std::move(varied);
		}

		return snapshot;
	}

	auto ReadHeaderAndIndex(
		const std::filesystem::path& path,
		FileHeaderDisk& out_header,
		std::vector<BlockRecordDisk>& out_records) -> bool
	{
		std::ifstream input(path, std::ios::binary);
		if (!input.read(reinterpret_cast<char*>(&out_header), sizeof(out_header)))
		{
			return false;
		}
		const auto& descriptor = out_header.index_descriptors[0];
		if (descriptor.index_size_le == 0u ||
			descriptor.index_size_le % sizeof(BlockRecordDisk) != 0u)
		{
			return false;
		}
		out_records.resize(
			static_cast<size_t>(descriptor.index_size_le / sizeof(BlockRecordDisk)));
		input.seekg(static_cast<std::streamoff>(descriptor.index_offset_le));
		return static_cast<bool>(input.read(
			reinterpret_cast<char*>(out_records.data()),
			static_cast<std::streamsize>(descriptor.index_size_le)));
	}

	auto FlipPayloadByte(
		const std::filesystem::path& source,
		const std::filesystem::path& destination,
		const BlockRecordDisk& record) -> void
	{
		std::filesystem::copy_file(
			source, destination, std::filesystem::copy_options::overwrite_existing);
		std::fstream file(destination, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(file.is_open());
		const uint64_t byte_offset = record.offset_le + record.stored_size_le / 2u;
		file.seekg(static_cast<std::streamoff>(byte_offset));
		char value = 0;
		REQUIRE(file.read(&value, 1));
		value = static_cast<char>(static_cast<unsigned char>(value) ^ 0x5au);
		file.seekp(static_cast<std::streamoff>(byte_offset));
		REQUIRE(file.write(&value, 1));
	}

	auto TestCrc32(const uint8_t* bytes, size_t size) -> uint32_t
	{
		uint32_t crc = 0xffffffffu;
		for (size_t index = 0u; index < size; ++index)
		{
			crc ^= bytes[index];
			for (uint32_t bit = 0u; bit < 8u; ++bit)
			{
				const uint32_t mask = 0u - (crc & 1u);
				crc = (crc >> 1u) ^ (0xedb88320u & mask);
			}
		}
		return crc ^ 0xffffffffu;
	}

	auto RewriteIndex(
		const std::filesystem::path& path,
		FileHeaderDisk& header,
		const std::vector<BlockRecordDisk>& records) -> void
	{
		auto& descriptor = header.index_descriptors[0];
		descriptor.index_size_le = records.size() * sizeof(BlockRecordDisk);
		descriptor.index_crc32_le = TestCrc32(
			reinterpret_cast<const uint8_t*>(records.data()),
			static_cast<size_t>(descriptor.index_size_le));
		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(file.is_open());
		file.seekp(static_cast<std::streamoff>(descriptor.index_offset_le));
		REQUIRE(file.write(
			reinterpret_cast<const char*>(records.data()),
			static_cast<std::streamsize>(descriptor.index_size_le)));
		file.seekp(static_cast<std::streamoff>(
			offsetof(FileHeaderDisk, index_descriptors)));
		REQUIRE(file.write(
			reinterpret_cast<const char*>(&descriptor), sizeof(descriptor)));
	}

	auto RewriteMetadataPayload(
		const std::filesystem::path& path,
		const std::function<void(std::vector<uint8_t>&)>& rewrite) -> void
	{
		FileHeaderDisk header{};
		std::vector<BlockRecordDisk> records{};
		REQUIRE(ReadHeaderAndIndex(path, header, records));
		auto metadata = std::find_if(records.begin(), records.end(), [](const BlockRecordDisk& record)
		{
			return record.kind == static_cast<uint8_t>(BlockKind::Metadata);
		});
		REQUIRE(metadata != records.end());

		std::ifstream input(path, std::ios::binary);
		REQUIRE(input.is_open());
		std::vector<uint8_t> stored(static_cast<size_t>(metadata->stored_size_le));
		input.seekg(static_cast<std::streamoff>(metadata->offset_le));
		REQUIRE(input.read(
			reinterpret_cast<char*>(stored.data()),
			static_cast<std::streamsize>(stored.size())));
		input.close();

		std::vector<uint8_t> decoded{};
		if (metadata->codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::None))
		{
			REQUIRE(metadata->stored_size_le == metadata->decoded_size_le);
			decoded = std::move(stored);
		}
		else
		{
			REQUIRE(metadata->codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::Rle));
			REQUIRE(AshEngine::decode_terrain_rle(
				stored,
				static_cast<size_t>(metadata->decoded_size_le),
				decoded));
		}
		rewrite(decoded);
		REQUIRE_FALSE(decoded.empty());

		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
		REQUIRE(file.is_open());
		const std::streamoff appended_offset = file.tellp();
		REQUIRE(appended_offset >= 0);
		metadata->codec = static_cast<uint8_t>(AshEngine::TerrainBlockCodec::None);
		metadata->offset_le = static_cast<uint64_t>(appended_offset);
		metadata->stored_size_le = decoded.size();
		metadata->decoded_size_le = decoded.size();
		metadata->payload_crc32_le = TestCrc32(decoded.data(), decoded.size());
		auto& descriptor = header.index_descriptors[0];
		descriptor.index_offset_le =
			static_cast<uint64_t>(appended_offset) + decoded.size();
		descriptor.index_size_le = records.size() * sizeof(BlockRecordDisk);
		descriptor.index_crc32_le = TestCrc32(
			reinterpret_cast<const uint8_t*>(records.data()),
			static_cast<size_t>(descriptor.index_size_le));
		REQUIRE(file.write(
			reinterpret_cast<const char*>(decoded.data()),
			static_cast<std::streamsize>(decoded.size())));
		REQUIRE(file.write(
			reinterpret_cast<const char*>(records.data()),
			static_cast<std::streamsize>(descriptor.index_size_le)));
		file.seekp(static_cast<std::streamoff>(
			offsetof(FileHeaderDisk, index_descriptors)));
		REQUIRE(file.write(
			reinterpret_cast<const char*>(&descriptor), sizeof(descriptor)));
	}

	auto CheckMaterialEqual(
		const AshEngine::TerrainMaterialLayerDesc& lhs,
		const AshEngine::TerrainMaterialLayerDesc& rhs) -> void
	{
		CHECK(lhs.name == rhs.name);
		CHECK(lhs.base_color_asset_path == rhs.base_color_asset_path);
		CHECK(lhs.normal_asset_path == rhs.normal_asset_path);
		CHECK(lhs.orm_asset_path == rhs.orm_asset_path);
	}

	auto MakeIncrementalSnapshot()
		-> std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			73u,
			IncrementalContainerLayout(),
			{ -128.0f, 512.0f },
			0.0f,
			snapshot,
			&error));
		return snapshot;
	}

	struct AdvancedGeneration
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		AshEngine::TerrainDirtyComponentPayload dirty{};
	};

	auto AdvanceGeneration(
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& previous,
		uint64_t generation,
		float delta,
		size_t component_index = 0u) -> AdvancedGeneration
	{
		REQUIRE(component_index < previous->components.size());
		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*previous);
		snapshot->content_generation = generation;
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
			*snapshot->components[component_index]);
		component->content_generation = generation;
		component->heights[0] += delta;
		std::string error{};
		REQUIRE(AshEngine::build_terrain_component_spatial_data(
			*component, component->sample_width, component->sample_height, &error));
		snapshot->components[component_index] = component;
		return {
			snapshot,
			{ component->coord, generation, component }
		};
	}

	auto ReadAllBytes(const std::filesystem::path& path) -> std::vector<uint8_t>
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		REQUIRE(input.is_open());
		const std::streamsize size = input.tellg();
		REQUIRE(size >= 0);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		input.seekg(0);
		REQUIRE(input.read(reinterpret_cast<char*>(bytes.data()), size));
		return bytes;
	}

	auto ReadTextFile(const std::filesystem::path& path) -> std::string
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE(input.is_open());
		return std::string(
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	auto CountOccurrences(const std::string& text, const std::string& needle) -> size_t
	{
		size_t count = 0u;
		size_t offset = 0u;
		while ((offset = text.find(needle, offset)) != std::string::npos)
		{
			++count;
			offset += needle.size();
		}
		return count;
	}

	auto CheckSnapshotLogicalEqual(
		const AshEngine::TerrainAssetSnapshot& lhs,
		const AshEngine::TerrainAssetSnapshot& rhs) -> void
	{
		CHECK(lhs.content_generation == rhs.content_generation);
		CHECK(lhs.layout.sample_count_x == rhs.layout.sample_count_x);
		CHECK(lhs.layout.sample_count_z == rhs.layout.sample_count_z);
		REQUIRE(lhs.base_heights);
		REQUIRE(rhs.base_heights);
		CHECK(*lhs.base_heights == *rhs.base_heights);
		REQUIRE(lhs.edit_layers);
		REQUIRE(rhs.edit_layers);
		CHECK(lhs.edit_layers->size() == rhs.edit_layers->size());
		REQUIRE(lhs.components.size() == rhs.components.size());
		for (size_t index = 0u; index < lhs.components.size(); ++index)
		{
			REQUIRE(lhs.components[index]);
			REQUIRE(rhs.components[index]);
			CHECK(lhs.components[index]->content_generation ==
				rhs.components[index]->content_generation);
			CHECK(lhs.components[index]->heights == rhs.components[index]->heights);
			CHECK(lhs.components[index]->weights == rhs.components[index]->weights);
			CHECK(lhs.components[index]->min_max_level_offsets ==
				rhs.components[index]->min_max_level_offsets);
			CHECK(lhs.components[index]->min_max_levels ==
				rhs.components[index]->min_max_levels);
			CHECK(lhs.components[index]->lod_errors == rhs.components[index]->lod_errors);
		}
	}
}

TEST_CASE("Terrain container generation one round-trips immutable source and caches")
{
	const std::filesystem::path path = TestDirectory() / "generation-one.AshTerrain";
	std::filesystem::remove(path);
	const auto source = MakeContainerSnapshot();

	AshEngine::TerrainContainerSaveReport save_report{};
	std::string error{};
	CHECK(AshEngine::save_terrain_container_incremental(
		path, *source, {}, &save_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(error.empty());
	CHECK(save_report.previous_generation == 0u);
	CHECK(save_report.committed_generation == source->content_generation);
	CHECK(save_report.blocks_written > source->components.size());
	CHECK(save_report.committed_revision.is_valid());

	std::ifstream bytes(path, std::ios::binary);
	std::array<uint8_t, 8> magic{};
	REQUIRE(bytes.read(reinterpret_cast<char*>(magic.data()), magic.size()));
	CHECK(magic == AshEngine::TerrainContainerFormat::k_magic);
	bytes.close();

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	AshEngine::TerrainContainerLoadReport load_report{};
	CHECK(AshEngine::load_terrain_container(
		path, loaded, &load_report, &error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(error.empty());
	CHECK(load_report.loaded_generation == source->content_generation);
	CHECK(load_report.source_revision == save_report.committed_revision);
	CHECK(loaded->asset_id == 0u);
	CHECK(loaded->source_path == path);
	CHECK(loaded->layout.sample_count_x == source->layout.sample_count_x);
	CHECK(loaded->layout.sample_count_z == source->layout.sample_count_z);
	CHECK(loaded->layout.component_count_x == source->layout.component_count_x);
	CHECK(loaded->layout.component_count_z == source->layout.component_count_z);
	CHECK(loaded->layout.component_quad_count == source->layout.component_quad_count);
	CHECK(loaded->layout.sample_spacing_meters == source->layout.sample_spacing_meters);
	CHECK(loaded->height_mapping.height_offset == source->height_mapping.height_offset);
	CHECK(loaded->height_mapping.height_range == source->height_mapping.height_range);
	CHECK(loaded->content_generation == source->content_generation);
	REQUIRE(loaded->base_heights);
	REQUIRE(source->base_heights);
	CHECK(*loaded->base_heights == *source->base_heights);
	REQUIRE(loaded->edit_layers);
	CHECK(loaded->edit_layers->empty());
	for (size_t index = 0u; index < source->material_layers.size(); ++index)
	{
		CheckMaterialEqual(loaded->material_layers[index], source->material_layers[index]);
	}
	REQUIRE(loaded->components.size() == source->components.size());
	for (size_t index = 0u; index < source->components.size(); ++index)
	{
		REQUIRE(loaded->components[index]);
		REQUIRE(source->components[index]);
		CHECK(loaded->components[index]->coord == source->components[index]->coord);
		CHECK(loaded->components[index]->content_generation == source->components[index]->content_generation);
		CHECK(loaded->components[index]->heights == source->components[index]->heights);
		CHECK(loaded->components[index]->weights.empty());
		CHECK(loaded->components[index]->min_max_level_offsets ==
			source->components[index]->min_max_level_offsets);
		CHECK(loaded->components[index]->min_max_levels == source->components[index]->min_max_levels);
		CHECK(loaded->components[index]->lod_errors == source->components[index]->lod_errors);
	}

	std::filesystem::remove(path);
}

TEST_CASE("Terrain container selects strictly smaller RLE and raw component payloads")
{
	const std::filesystem::path path = TestDirectory() / "codec-selection.AshTerrain";
	std::filesystem::remove(path);
	const auto source = MakeContainerSnapshot(true);
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *source, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);

	FileHeaderDisk header{};
	std::vector<BlockRecordDisk> records{};
	REQUIRE(ReadHeaderAndIndex(path, header, records));
	const BlockRecordDisk* rle_component = nullptr;
	const BlockRecordDisk* raw_component = nullptr;
	for (const BlockRecordDisk& record : records)
	{
		if (record.kind != static_cast<uint8_t>(BlockKind::ComposedComponent))
		{
			continue;
		}
		if (record.component_x_le == 0u)
		{
			rle_component = &record;
		}
		else if (record.component_x_le == 1u)
		{
			raw_component = &record;
		}
	}
	REQUIRE(rle_component != nullptr);
	REQUIRE(raw_component != nullptr);
	CHECK(rle_component->codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::Rle));
	CHECK(rle_component->stored_size_le < rle_component->decoded_size_le);
	CHECK(raw_component->codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::None));
	CHECK(raw_component->stored_size_le == raw_component->decoded_size_le);

	std::filesystem::remove(path);
}

TEST_CASE("Terrain container preserves ordered edit layer source blocks")
{
	const std::filesystem::path path = TestDirectory() / "edit-source.AshTerrain";
	std::filesystem::remove(path);
	const auto flat = MakeContainerSnapshot();
	const auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>();
	AshEngine::TerrainEditLayer layer{};
	layer.id.bytes[0] = 0x41u;
	layer.id.bytes[15] = 0x9au;
	layer.name = "Sculpt and Paint";
	layer.visible = false;
	layer.strength = 0.375f;
	layer.height_blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
	layer.height_blocks.push_back({
		{ 0u, 0u }, { 0u, 0u, 2u, 1u }, { 3.5f, -1.25f }, { 1.0f, 0.5f }
	});
	AshEngine::TerrainSparseWeightBlock weight{};
	weight.owner = { 1u, 0u };
	weight.changed_rect = { 4u, 0u, 5u, 1u };
	weight.values.resize(1u);
	weight.values[0][0] = 0.25f;
	weight.values[0][1] = 0.75f;
	weight.coverage = { 0.8f };
	layer.weight_blocks.push_back(weight);
	layers->push_back(layer);
	snapshot->edit_layers = layers;

	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *snapshot, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	REQUIRE(loaded->edit_layers->size() == 1u);
	const auto& actual = loaded->edit_layers->front();
	CHECK(actual.id == layer.id);
	CHECK(actual.name == layer.name);
	CHECK(actual.visible == layer.visible);
	CHECK(actual.strength == layer.strength);
	CHECK(actual.height_blend_mode == layer.height_blend_mode);
	REQUIRE(actual.height_blocks.size() == 1u);
	CHECK(actual.height_blocks[0].owner == layer.height_blocks[0].owner);
	CHECK(actual.height_blocks[0].changed_rect.min_x == layer.height_blocks[0].changed_rect.min_x);
	CHECK(actual.height_blocks[0].changed_rect.min_z == layer.height_blocks[0].changed_rect.min_z);
	CHECK(actual.height_blocks[0].changed_rect.max_x_exclusive ==
		layer.height_blocks[0].changed_rect.max_x_exclusive);
	CHECK(actual.height_blocks[0].changed_rect.max_z_exclusive ==
		layer.height_blocks[0].changed_rect.max_z_exclusive);
	CHECK(actual.height_blocks[0].values == layer.height_blocks[0].values);
	CHECK(actual.height_blocks[0].coverage == layer.height_blocks[0].coverage);
	REQUIRE(actual.weight_blocks.size() == 1u);
	CHECK(actual.weight_blocks[0].owner == layer.weight_blocks[0].owner);
	CHECK(actual.weight_blocks[0].changed_rect.min_x == layer.weight_blocks[0].changed_rect.min_x);
	CHECK(actual.weight_blocks[0].changed_rect.min_z == layer.weight_blocks[0].changed_rect.min_z);
	CHECK(actual.weight_blocks[0].changed_rect.max_x_exclusive ==
		layer.weight_blocks[0].changed_rect.max_x_exclusive);
	CHECK(actual.weight_blocks[0].changed_rect.max_z_exclusive ==
		layer.weight_blocks[0].changed_rect.max_z_exclusive);
	CHECK(actual.weight_blocks[0].values == layer.weight_blocks[0].values);
	CHECK(actual.weight_blocks[0].coverage == layer.weight_blocks[0].coverage);

	std::filesystem::remove(path);
}

TEST_CASE("Terrain container layer lock metadata round-trips and legacy v1 defaults unlocked")
{
	const std::filesystem::path current_path =
		TestDirectory() / "edit-layer-lock-current.AshTerrain";
	const std::filesystem::path legacy_path =
		TestDirectory() / "edit-layer-lock-legacy.AshTerrain";
	std::filesystem::remove(current_path);
	std::filesystem::remove(legacy_path);

	const auto flat = MakeContainerSnapshot();
	auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>();
	AshEngine::TerrainEditLayer layer{};
	layer.id.bytes[0] = 0x52u;
	layer.id.bytes[15] = 0xa7u;
	layer.name = "Locked authoring layer";
	layer.locked = true;
	layers->push_back(layer);
	snapshot->edit_layers = layers;

	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		current_path, *snapshot, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(current_path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	REQUIRE(loaded->edit_layers->size() == 1u);
	CHECK(loaded->edit_layers->front().locked);

	std::filesystem::copy_file(
		current_path, legacy_path, std::filesystem::copy_options::overwrite_existing);
	RewriteMetadataPayload(legacy_path, [](std::vector<uint8_t>& metadata)
	{
		const size_t extension_size =
			AshEngine::TerrainContainerFormat::k_layer_metadata_extension_magic.size() +
			2u + 4u + 1u;
		REQUIRE(metadata.size() > extension_size);
		const size_t extension_offset = metadata.size() - extension_size;
		CHECK(std::equal(
			AshEngine::TerrainContainerFormat::k_layer_metadata_extension_magic.begin(),
			AshEngine::TerrainContainerFormat::k_layer_metadata_extension_magic.end(),
			metadata.begin() + static_cast<std::ptrdiff_t>(extension_offset)));
		metadata.resize(extension_offset);
	});
	loaded.reset();
	const AshEngine::TerrainContainerResult legacy_result =
		AshEngine::load_terrain_container(legacy_path, loaded, nullptr, &error);
	INFO(error);
	REQUIRE(legacy_result == AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	REQUIRE(loaded->edit_layers->size() == 1u);
	CHECK_FALSE(loaded->edit_layers->front().locked);

	std::filesystem::remove(current_path);
	std::filesystem::remove(legacy_path);
}

TEST_CASE("Terrain container rejects an unsupported layer metadata revision")
{
	const std::filesystem::path path =
		TestDirectory() / "edit-layer-lock-future.AshTerrain";
	std::filesystem::remove(path);

	const auto flat = MakeContainerSnapshot();
	auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(1u);
	(*layers)[0].id.bytes[0] = 0x63u;
	(*layers)[0].name = "Future metadata";
	snapshot->edit_layers = layers;

	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *snapshot, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	RewriteMetadataPayload(path, [](std::vector<uint8_t>& metadata)
	{
		const size_t extension_size =
			AshEngine::TerrainContainerFormat::k_layer_metadata_extension_magic.size() +
			2u + 4u + 1u;
		REQUIRE(metadata.size() > extension_size);
		const size_t version_offset =
			metadata.size() - extension_size +
			AshEngine::TerrainContainerFormat::k_layer_metadata_extension_magic.size();
		metadata[version_offset] = 2u;
		metadata[version_offset + 1u] = 0u;
	});

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	const AshEngine::TerrainContainerResult future_result =
		AshEngine::load_terrain_container(path, loaded, nullptr, &error);
	INFO(error);
	CHECK(future_result == AshEngine::TerrainContainerResult::Corrupt);
	CHECK_FALSE(loaded);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container reports corrupt raw and RLE payload offsets")
{
	const std::filesystem::path source_path = TestDirectory() / "crc-source.AshTerrain";
	const std::filesystem::path raw_path = TestDirectory() / "crc-raw-corrupt.AshTerrain";
	const std::filesystem::path rle_path = TestDirectory() / "crc-rle-corrupt.AshTerrain";
	std::filesystem::remove(source_path);
	std::filesystem::remove(raw_path);
	std::filesystem::remove(rle_path);
	const auto source = MakeContainerSnapshot(true);
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		source_path, *source, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);

	FileHeaderDisk header{};
	std::vector<BlockRecordDisk> records{};
	REQUIRE(ReadHeaderAndIndex(source_path, header, records));
	const BlockRecordDisk* raw = nullptr;
	const BlockRecordDisk* rle = nullptr;
	for (const BlockRecordDisk& record : records)
	{
		if (record.kind != static_cast<uint8_t>(BlockKind::ComposedComponent))
		{
			continue;
		}
		if (record.codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::None))
		{
			raw = &record;
		}
		else if (record.codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::Rle))
		{
			rle = &record;
		}
	}
	REQUIRE(raw != nullptr);
	REQUIRE(rle != nullptr);
	FlipPayloadByte(source_path, raw_path, *raw);
	FlipPayloadByte(source_path, rle_path, *rle);

	for (const auto& [path, record] : {
		std::pair<std::filesystem::path, const BlockRecordDisk*>{ raw_path, raw },
		std::pair<std::filesystem::path, const BlockRecordDisk*>{ rle_path, rle } })
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
		error.clear();
		CHECK(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
			AshEngine::TerrainContainerResult::Corrupt);
		CHECK_FALSE(loaded);
		CHECK_FALSE(error.empty());
		CHECK(error.find(std::to_string(record->offset_le)) != std::string::npos);
	}

	std::filesystem::remove(source_path);
	std::filesystem::remove(raw_path);
	std::filesystem::remove(rle_path);
}

TEST_CASE("Terrain container rejects logically incomplete generation one indexes")
{
	const std::filesystem::path source_path = TestDirectory() / "logical-source.AshTerrain";
	const std::filesystem::path generation_path = TestDirectory() / "logical-generation.AshTerrain";
	const std::filesystem::path global_key_path = TestDirectory() / "logical-global-key.AshTerrain";
	const std::filesystem::path missing_lod_path = TestDirectory() / "logical-missing-lod.AshTerrain";
	for (const std::filesystem::path& path : {
		source_path, generation_path, global_key_path, missing_lod_path })
	{
		std::filesystem::remove(path);
	}
	const auto source = MakeContainerSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		source_path, *source, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);

	FileHeaderDisk header{};
	std::vector<BlockRecordDisk> records{};
	REQUIRE(ReadHeaderAndIndex(source_path, header, records));

	std::filesystem::copy_file(source_path, generation_path);
	auto generation_header = header;
	++generation_header.index_descriptors[0].generation_le;
	RewriteIndex(generation_path, generation_header, records);

	std::filesystem::copy_file(source_path, global_key_path);
	auto global_header = header;
	auto global_records = records;
	REQUIRE(global_records.front().kind == static_cast<uint8_t>(BlockKind::Metadata));
	global_records.front().component_x_le = 1u;
	RewriteIndex(global_key_path, global_header, global_records);

	std::filesystem::copy_file(source_path, missing_lod_path);
	auto missing_header = header;
	auto missing_records = records;
	REQUIRE(missing_records.back().kind == static_cast<uint8_t>(BlockKind::LodError));
	missing_records.pop_back();
	RewriteIndex(missing_lod_path, missing_header, missing_records);

	for (const std::filesystem::path& path : {
		generation_path, global_key_path, missing_lod_path })
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
		error.clear();
		CHECK(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
			AshEngine::TerrainContainerResult::Corrupt);
		CHECK_FALSE(loaded);
		CHECK_FALSE(error.empty());
	}

	for (const std::filesystem::path& path : {
		source_path, generation_path, global_key_path, missing_lod_path })
	{
		std::filesystem::remove(path);
	}
}

TEST_CASE("Terrain container bounds metadata block declarations by the live index")
{
	const std::filesystem::path path = TestDirectory() / "metadata-block-bounds.AshTerrain";
	std::filesystem::remove(path);
	const auto flat = MakeContainerSnapshot();
	const auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
	snapshot->material_layers = {};
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(1u);
	(*layers)[0].id.bytes[0] = 0x71u;
	(*layers)[0].name.resize(2048u);
	for (size_t index = 0u; index < (*layers)[0].name.size(); ++index)
	{
		(*layers)[0].name[index] = static_cast<char>((index * 73u + 19u) & 0xffu);
	}
	snapshot->edit_layers = layers;
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *snapshot, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);

	FileHeaderDisk header{};
	std::vector<BlockRecordDisk> records{};
	REQUIRE(ReadHeaderAndIndex(path, header, records));
	REQUIRE(records.front().kind == static_cast<uint8_t>(BlockKind::Metadata));
	REQUIRE(records.front().codec == static_cast<uint8_t>(AshEngine::TerrainBlockCodec::None));
	std::vector<uint8_t> metadata(static_cast<size_t>(records.front().stored_size_le));
	{
		std::ifstream input(path, std::ios::binary);
		input.seekg(static_cast<std::streamoff>(records.front().offset_le));
		REQUIRE(input.read(
			reinterpret_cast<char*>(metadata.data()),
			static_cast<std::streamsize>(metadata.size())));
	}
	const size_t height_count_offset = 202u + (*layers)[0].name.size();
	const size_t extension_offset = height_count_offset + 4u;
	const auto& extension_magic =
		AshEngine::TerrainContainerFormat::k_layer_metadata_extension_magic;
	REQUIRE(extension_offset + extension_magic.size() + 2u + 4u + layers->size() ==
		metadata.size());
	CHECK(std::equal(
		extension_magic.begin(),
		extension_magic.end(),
		metadata.begin() + static_cast<std::ptrdiff_t>(extension_offset)));
	metadata[height_count_offset] = 0xffu;
	metadata[height_count_offset + 1u] = 0xffu;
	metadata[height_count_offset + 2u] = 0xffu;
	metadata[height_count_offset + 3u] = 0xffu;
	records.front().payload_crc32_le = TestCrc32(metadata.data(), metadata.size());
	{
		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
		file.seekp(static_cast<std::streamoff>(records.front().offset_le));
		REQUIRE(file.write(
			reinterpret_cast<const char*>(metadata.data()),
			static_cast<std::streamsize>(metadata.size())));
	}
	RewriteIndex(path, header, records);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	CHECK(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Corrupt);
	CHECK_FALSE(loaded);
	CHECK(error.find(std::to_string(records.front().offset_le)) != std::string::npos);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container indexes many distinct edit layer IDs without ambiguity")
{
	const std::filesystem::path path = TestDirectory() / "many-edit-layers.AshTerrain";
	std::filesystem::remove(path);
	const auto flat = MakeContainerSnapshot();
	const auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
	constexpr uint32_t layer_count = 1024u;
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>();
	layers->reserve(layer_count);
	for (uint32_t index = 0u; index < layer_count; ++index)
	{
		AshEngine::TerrainEditLayer layer{};
		const uint32_t id = index + 1u;
		layer.id.bytes[0] = static_cast<uint8_t>(id);
		layer.id.bytes[1] = static_cast<uint8_t>(id >> 8u);
		layer.id.bytes[2] = static_cast<uint8_t>(id >> 16u);
		layer.id.bytes[3] = static_cast<uint8_t>(id >> 24u);
		layer.height_blocks.push_back({
			{ 0u, 0u }, { 0u, 0u, 1u, 1u },
			{ static_cast<float>(index) }, { 1.0f }
		});
		layers->push_back(std::move(layer));
	}
	snapshot->edit_layers = layers;
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *snapshot, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	REQUIRE(loaded->edit_layers->size() == layer_count);
	CHECK((*loaded->edit_layers)[0].id == (*layers)[0].id);
	CHECK((*loaded->edit_layers)[layer_count - 1u].id == (*layers)[layer_count - 1u].id);
	CHECK((*loaded->edit_layers)[0].height_blocks[0].values[0] == 0.0f);
	CHECK((*loaded->edit_layers)[layer_count - 1u].height_blocks[0].values[0] ==
		static_cast<float>(layer_count - 1u));
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container incremental save appends only one dirty component generation")
{
	const std::filesystem::path path = TestDirectory() / "incremental-size.AshTerrain";
	std::filesystem::remove(path);
	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	const uint64_t size_one = std::filesystem::file_size(path);

	const AdvancedGeneration generation_two =
		AdvanceGeneration(generation_one, 2u, 7.0f);
	AshEngine::TerrainContainerSaveReport report{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_two.snapshot, { generation_two.dirty }, &report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	const uint64_t size_two = std::filesystem::file_size(path);
	CHECK(report.previous_generation == 1u);
	CHECK(report.committed_generation == 2u);
	CHECK(report.blocks_written < generation_two.snapshot->components.size());
	CHECK(size_two > size_one);
	CHECK(size_two - size_one < size_one);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	const auto load_result =
		AshEngine::load_terrain_container(path, loaded, nullptr, &error);
	REQUIRE(load_result == AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(loaded->content_generation == 2u);
	CHECK(loaded->components[0]->content_generation == 2u);
	CHECK(loaded->components[0]->heights == generation_two.snapshot->components[0]->heights);
	CHECK(loaded->components[1]->content_generation == 1u);
	CHECK(loaded->components[1]->heights == generation_one->components[1]->heights);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container incremental save commits every component changed since disk generation")
{
	const std::filesystem::path path = TestDirectory() / "incremental-cumulative-dirty.AshTerrain";
	std::filesystem::remove(path);
	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);

	const AdvancedGeneration generation_two =
		AdvanceGeneration(generation_one, 2u, 7.0f, 0u);
	const AdvancedGeneration generation_three =
		AdvanceGeneration(generation_two.snapshot, 3u, 11.0f, 1u);
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path,
		*generation_three.snapshot,
		{ generation_two.dirty, generation_three.dirty },
		nullptr,
		&error) == AshEngine::TerrainContainerResult::Success);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(loaded->content_generation == 3u);
	CHECK(loaded->components[0u]->content_generation == 2u);
	CHECK(loaded->components[0u]->heights ==
		generation_two.snapshot->components[0u]->heights);
	CHECK(loaded->components[1u]->content_generation == 3u);
	CHECK(loaded->components[1u]->heights ==
		generation_three.snapshot->components[1u]->heights);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container rejects an incomplete cumulative dirty component set atomically")
{
	const std::filesystem::path path = TestDirectory() / "incremental-cumulative-missing.AshTerrain";
	std::filesystem::remove(path);
	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	const std::vector<uint8_t> before = ReadAllBytes(path);

	const AdvancedGeneration generation_two =
		AdvanceGeneration(generation_one, 2u, 7.0f, 0u);
	const AdvancedGeneration generation_three =
		AdvanceGeneration(generation_two.snapshot, 3u, 11.0f, 1u);
	CHECK(AshEngine::save_terrain_container_incremental(
		path,
		*generation_three.snapshot,
		{ generation_three.dirty },
		nullptr,
		&error) == AshEngine::TerrainContainerResult::InvalidData);
	CHECK(ReadAllBytes(path) == before);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container full save accepts immutable snapshots with mixed component generations")
{
	const std::filesystem::path path = TestDirectory() / "full-mixed-component-generations.AshTerrain";
	std::filesystem::remove(path);
	const auto generation_one = MakeIncrementalSnapshot();
	const AdvancedGeneration generation_two =
		AdvanceGeneration(generation_one, 2u, 7.0f, 0u);
	const AdvancedGeneration generation_three =
		AdvanceGeneration(generation_two.snapshot, 3u, 11.0f, 1u);
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_three.snapshot, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(loaded->components[0u]->content_generation == 2u);
	CHECK(loaded->components[1u]->content_generation == 3u);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container incremental source change cannot be hidden by a CRC32 collision")
{
	const std::filesystem::path path = TestDirectory() / "incremental-crc-collision.AshTerrain";
	std::filesystem::remove(path);
	const std::array<uint16_t, 16> first_values = {
		513u, 1027u, 1541u, 2055u, 2569u, 3083u, 3597u, 4111u,
		4625u, 5139u, 5653u, 6167u, 6681u, 7195u, 7709u, 8223u
	};
	const std::array<uint16_t, 16> colliding_values = {
		641u, 1027u, 1541u, 2055u, 2569u, 3083u, 3597u, 4111u,
		4625u, 5139u, 5653u, 6167u, 6681u, 7195u, 37021u, 64225u
	};

	auto generation_one = std::make_shared<AshEngine::TerrainAssetSnapshot>(
		*MakeIncrementalSnapshot());
	auto first_base = std::make_shared<std::vector<uint16_t>>(*generation_one->base_heights);
	std::copy(first_values.begin(), first_values.end(), first_base->begin());
	generation_one->base_heights = first_base;
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);

	const AdvancedGeneration advanced = AdvanceGeneration(generation_one, 2u, 5.0f);
	auto generation_two = std::make_shared<AshEngine::TerrainAssetSnapshot>(*advanced.snapshot);
	auto second_base = std::make_shared<std::vector<uint16_t>>(*first_base);
	std::copy(colliding_values.begin(), colliding_values.end(), second_base->begin());
	CHECK(TestCrc32(
		reinterpret_cast<const uint8_t*>(first_base->data()),
		first_base->size() * sizeof(uint16_t)) ==
		TestCrc32(
			reinterpret_cast<const uint8_t*>(second_base->data()),
			second_base->size() * sizeof(uint16_t)));
	generation_two->base_heights = second_base;
	AshEngine::TerrainContainerSaveReport report{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_two, { advanced.dirty }, &report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(report.blocks_written == 5u);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->base_heights);
	CHECK(*loaded->base_heights == *second_base);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container source contract streams full saves and builds only changed blocks")
{
	const std::string source = ReadTextFile(
		"project/src/engine/Function/Asset/TerrainContainer.cpp");
	CHECK(source.find("build_incremental_generation_blocks(") != std::string::npos);
	CHECK(CountOccurrences(
		source, "build_full_block_sources(snapshot, sources)") == 1u);
	CHECK(source.find("stream_terrain_rle(") != std::string::npos);
	CHECK(source.find("load_previous_terrain_metadata(") != std::string::npos);
	CHECK(source.find("previous_snapshot") == std::string::npos);
}

TEST_CASE("Terrain container recovery selects the previous valid descriptor")
{
	const std::filesystem::path source_path = TestDirectory() / "recovery-source.AshTerrain";
	const std::filesystem::path descriptor_path = TestDirectory() / "recovery-descriptor.AshTerrain";
	const std::filesystem::path truncate_path = TestDirectory() / "recovery-truncate.AshTerrain";
	const std::filesystem::path both_path = TestDirectory() / "recovery-both.AshTerrain";
	for (const auto& path : { source_path, descriptor_path, truncate_path, both_path })
	{
		std::filesystem::remove(path);
	}
	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		source_path, *generation_one, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	const AdvancedGeneration generation_two = AdvanceGeneration(generation_one, 2u, 3.0f);
	REQUIRE(AshEngine::save_terrain_container_incremental(
		source_path, *generation_two.snapshot, { generation_two.dirty }, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	FileHeaderDisk header{};
	{
		std::ifstream input(source_path, std::ios::binary);
		REQUIRE(input.read(reinterpret_cast<char*>(&header), sizeof(header)));
	}
	const size_t newer_slot = header.index_descriptors[1].generation_le >
		header.index_descriptors[0].generation_le ? 1u : 0u;
	REQUIRE(header.index_descriptors[newer_slot].generation_le == 2u);

	std::filesystem::copy_file(source_path, descriptor_path);
	{
		std::fstream file(descriptor_path, std::ios::binary | std::ios::in | std::ios::out);
		const uint64_t offset = offsetof(FileHeaderDisk, index_descriptors) +
			newer_slot * sizeof(AshEngine::TerrainContainerFormat::IndexDescriptorDisk) +
			offsetof(AshEngine::TerrainContainerFormat::IndexDescriptorDisk, index_crc32_le);
		file.seekg(static_cast<std::streamoff>(offset));
		char value = 0;
		REQUIRE(file.read(&value, 1));
		value ^= 0x5a;
		file.seekp(static_cast<std::streamoff>(offset));
		REQUIRE(file.write(&value, 1));
	}

	std::filesystem::copy_file(source_path, truncate_path);
	const auto& newer = header.index_descriptors[newer_slot];
	std::filesystem::resize_file(
		truncate_path, newer.index_offset_le + newer.index_size_le / 2u);

	std::filesystem::copy_file(source_path, both_path);
	{
		std::fstream file(both_path, std::ios::binary | std::ios::in | std::ios::out);
		for (size_t slot = 0u; slot < 2u; ++slot)
		{
			const uint64_t offset = offsetof(FileHeaderDisk, index_descriptors) +
				slot * sizeof(AshEngine::TerrainContainerFormat::IndexDescriptorDisk) +
				offsetof(AshEngine::TerrainContainerFormat::IndexDescriptorDisk, index_crc32_le);
			file.seekg(static_cast<std::streamoff>(offset));
			char value = 0;
			REQUIRE(file.read(&value, 1));
			value ^= static_cast<char>(0x33 + slot);
			file.seekp(static_cast<std::streamoff>(offset));
			REQUIRE(file.write(&value, 1));
		}
	}

	const std::array recovery_cases{
		std::pair{
			descriptor_path,
			std::string("Terrain generation 2 index CRC is invalid.") },
		std::pair{
			truncate_path,
			std::string("Terrain generation 2 index descriptor is invalid.") }
	};
	for (const auto& [path, expected_recovery_detail] : recovery_cases)
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
		AshEngine::TerrainContainerLoadReport report{};
		error.clear();
		CHECK(AshEngine::load_terrain_container(path, loaded, &report, &error) ==
			AshEngine::TerrainContainerResult::RecoveredPreviousGeneration);
		REQUIRE(loaded);
		CHECK(loaded->content_generation == 1u);
		CHECK(report.loaded_generation == 1u);
		CHECK(report.recovered_previous_generation);
		CHECK(report.rejected_generation == 2u);
		CHECK(report.recovery_detail == expected_recovery_detail);
		CHECK(error.empty());
	}
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded = generation_one;
		AshEngine::TerrainContainerLoadReport report{};
		report.loaded_generation = 99u;
		report.recovered_previous_generation = true;
		report.rejected_generation = 100u;
		report.recovery_detail = "sentinel";
		report.decoded_block_count = 17u;
		error.clear();
		CHECK(AshEngine::load_terrain_container(both_path, loaded, &report, &error) ==
			AshEngine::TerrainContainerResult::Corrupt);
		CHECK_FALSE(loaded);
		CHECK(report.loaded_generation == 0u);
		CHECK_FALSE(report.recovered_previous_generation);
		CHECK(report.rejected_generation == 0u);
		CHECK(report.recovery_detail.empty());
		CHECK(report.decoded_block_count == 0u);
		CHECK(error == "Terrain container has no valid index descriptor.");
	}

	for (const auto& path : { source_path, descriptor_path, truncate_path, both_path })
	{
		std::filesystem::remove(path);
	}
}

TEST_CASE("Terrain container refuses a corrupt newest payload instead of recovering")
{
	const std::filesystem::path source_path =
		TestDirectory() / "payload-recovery-source.AshTerrain";
	const std::filesystem::path corrupt_path =
		TestDirectory() / "payload-recovery-corrupt.AshTerrain";
	for (const auto& path : { source_path, corrupt_path })
	{
		std::filesystem::remove(path);
	}

	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		source_path, *generation_one, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	const AdvancedGeneration generation_two = AdvanceGeneration(generation_one, 2u, 3.0f);
	REQUIRE(AshEngine::save_terrain_container_incremental(
		source_path, *generation_two.snapshot, { generation_two.dirty }, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	FileHeaderDisk header{};
	std::ifstream input(source_path, std::ios::binary);
	REQUIRE(input.read(reinterpret_cast<char*>(&header), sizeof(header)));
	const size_t newest_slot = header.index_descriptors[1].generation_le >
		header.index_descriptors[0].generation_le ? 1u : 0u;
	const auto& newest = header.index_descriptors[newest_slot];
	REQUIRE(newest.generation_le == 2u);
	REQUIRE(newest.index_size_le % sizeof(BlockRecordDisk) == 0u);
	std::vector<BlockRecordDisk> newest_records(
		static_cast<size_t>(newest.index_size_le / sizeof(BlockRecordDisk)));
	input.seekg(static_cast<std::streamoff>(newest.index_offset_le));
	REQUIRE(input.read(
		reinterpret_cast<char*>(newest_records.data()),
		static_cast<std::streamsize>(newest.index_size_le)));
	REQUIRE(TestCrc32(
		reinterpret_cast<const uint8_t*>(newest_records.data()),
		static_cast<size_t>(newest.index_size_le)) == newest.index_crc32_le);
	const auto metadata = std::find_if(
		newest_records.begin(), newest_records.end(), [](const BlockRecordDisk& record)
		{
			return record.kind == static_cast<uint8_t>(BlockKind::Metadata);
		});
	REQUIRE(metadata != newest_records.end());
	input.close();
	FlipPayloadByte(source_path, corrupt_path, *metadata);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded = generation_one;
	AshEngine::TerrainContainerLoadReport report{};
	report.loaded_generation = 99u;
	report.recovered_previous_generation = true;
	report.rejected_generation = 100u;
	report.recovery_detail = "sentinel";
	report.decoded_block_count = 17u;
	error = "sentinel";
	CHECK(AshEngine::load_terrain_container(corrupt_path, loaded, &report, &error) ==
		AshEngine::TerrainContainerResult::Corrupt);
	CHECK_FALSE(loaded);
	CHECK(report.loaded_generation == 0u);
	CHECK_FALSE(report.recovered_previous_generation);
	CHECK(report.rejected_generation == 0u);
	CHECK(report.recovery_detail.empty());
	CHECK(report.decoded_block_count == 0u);
	CHECK(error == "Terrain block CRC mismatch at offset " +
		std::to_string(metadata->offset_le) + ".");

	for (const auto& path : { source_path, corrupt_path })
	{
		std::filesystem::remove(path);
	}
}

TEST_CASE("Terrain container incremental validation failure preserves committed bytes")
{
	const std::filesystem::path path = TestDirectory() / "incremental-preserve.AshTerrain";
	std::filesystem::remove(path);
	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	const std::vector<uint8_t> before = ReadAllBytes(path);
	const AdvancedGeneration generation_two = AdvanceGeneration(generation_one, 2u, 1.0f);
	auto invalid_dirty = generation_two.dirty;
	invalid_dirty.content_generation = 1u;
	CHECK(AshEngine::save_terrain_container_incremental(
		path, *generation_two.snapshot, { invalid_dirty }, nullptr, &error) ==
		AshEngine::TerrainContainerResult::InvalidData);
	CHECK(ReadAllBytes(path) == before);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	CHECK(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(loaded->content_generation == 1u);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container checked save rejects a changed source revision without touching its bytes")
{
	const std::filesystem::path path =
		TestDirectory() / "checked-save-source-changed.AshTerrain";
	std::filesystem::remove(path);
	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	AshEngine::TerrainContainerLoadReport original_report{};
	REQUIRE(AshEngine::load_terrain_container(
		path, loaded, &original_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(original_report.source_revision.is_valid());

	const AdvancedGeneration external_generation =
		AdvanceGeneration(generation_one, 2u, 17.0f);
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path,
		*external_generation.snapshot,
		{ external_generation.dirty },
		nullptr,
		&error) == AshEngine::TerrainContainerResult::Success);
	const std::vector<uint8_t> external_bytes = ReadAllBytes(path);

	const AdvancedGeneration local_generation =
		AdvanceGeneration(generation_one, 3u, 29.0f);
	AshEngine::TerrainContainerSaveReport save_report{};
	CHECK(AshEngine::save_terrain_container_incremental(
		path,
		*local_generation.snapshot,
		{ local_generation.dirty },
		&original_report.source_revision,
		&save_report,
		&error) == AshEngine::TerrainContainerResult::SourceChanged);
	CHECK_FALSE(error.empty());
	CHECK_FALSE(save_report.committed_revision.is_valid());
	CHECK(ReadAllBytes(path) == external_bytes);

	AshEngine::TerrainContainerLoadReport final_report{};
	REQUIRE(AshEngine::load_terrain_container(
		path, loaded, &final_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(final_report.loaded_generation == 2u);
	CHECK(final_report.source_revision != original_report.source_revision);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container optimize compacts generations atomically and idempotently")
{
	const std::filesystem::path path = TestDirectory() / "optimize.AshTerrain";
	std::filesystem::remove(path);
	std::filesystem::remove(path.string() + ".optimize.tmp");
	std::string error{};
	auto current = MakeIncrementalSnapshot();
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *current, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	for (uint64_t generation = 2u; generation <= 4u; ++generation)
	{
		const AdvancedGeneration next =
			AdvanceGeneration(current, generation, static_cast<float>(generation));
		REQUIRE(AshEngine::save_terrain_container_incremental(
			path, *next.snapshot, { next.dirty }, nullptr, &error) ==
			AshEngine::TerrainContainerResult::Success);
		current = next.snapshot;
	}
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> before{};
	REQUIRE(AshEngine::load_terrain_container(path, before, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	const uint64_t size_before = std::filesystem::file_size(path);
	AshEngine::TerrainContainerSaveReport report{};
	REQUIRE(AshEngine::optimize_terrain_container(path, &report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	const uint64_t size_after = std::filesystem::file_size(path);
	CHECK(size_after < size_before);
	CHECK(report.previous_generation == 4u);
	CHECK(report.committed_generation == 4u);
	CHECK(report.committed_revision.is_valid());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> after{};
	AshEngine::TerrainContainerLoadReport optimized_load_report{};
	REQUIRE(AshEngine::load_terrain_container(path, after, &optimized_load_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(after);
	CHECK(optimized_load_report.source_revision == report.committed_revision);
	CheckSnapshotLogicalEqual(*after, *before);
	REQUIRE(AshEngine::optimize_terrain_container(path, &report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(std::filesystem::file_size(path) <= size_after);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> second{};
	REQUIRE(AshEngine::load_terrain_container(path, second, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(second);
	CheckSnapshotLogicalEqual(*second, *before);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain container optimize commit preserves a source changed after staging")
{
	const std::filesystem::path path =
		TestDirectory() / "optimize-source-changed.AshTerrain";
	const std::filesystem::path staged =
		TestDirectory() / "optimize-source-changed.stage.AshTerrain";
	for (const auto& candidate : { path, staged })
	{
		std::filesystem::remove(candidate);
	}

	const auto generation_one = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *generation_one, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	AshEngine::TerrainContainerLoadReport original_report{};
	REQUIRE(AshEngine::load_terrain_container(
		path, loaded, &original_report, &error) ==
		AshEngine::TerrainContainerResult::Success);

	REQUIRE(AshEngine::save_terrain_container_incremental(
		staged, *generation_one, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	AshEngine::TerrainContainerLoadReport staged_report{};
	REQUIRE(AshEngine::load_terrain_container(
		staged, loaded, &staged_report, &error) ==
		AshEngine::TerrainContainerResult::Success);

	const AdvancedGeneration external_generation =
		AdvanceGeneration(generation_one, 2u, 41.0f);
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path,
		*external_generation.snapshot,
		{ external_generation.dirty },
		nullptr,
		&error) == AshEngine::TerrainContainerResult::Success);
	const std::vector<uint8_t> external_bytes = ReadAllBytes(path);

	CHECK(AshEngine::TerrainContainerInternal::commit_staged_terrain_container_optimization(
		path,
		staged,
		original_report.source_revision,
		staged_report.source_revision,
		&error) == AshEngine::TerrainContainerResult::SourceChanged);
	CHECK_FALSE(error.empty());
	CHECK(ReadAllBytes(path) == external_bytes);

	AshEngine::TerrainContainerLoadReport final_report{};
	REQUIRE(AshEngine::load_terrain_container(
		path, loaded, &final_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(final_report.loaded_generation == 2u);
	CHECK(final_report.source_revision != original_report.source_revision);

	for (const auto& candidate : { path, staged })
	{
		std::filesystem::remove(candidate);
	}
}

TEST_CASE("Terrain container revision probe rejects an invalid header")
{
	const std::filesystem::path path =
		TestDirectory() / "revision-probe-invalid-header.AshTerrain";
	std::filesystem::remove(path);
	const auto snapshot = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *snapshot, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	{
		std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(stream);
		std::array<uint8_t, 8> invalidMagic{};
		invalidMagic.fill(0x5au);
		stream.write(
			reinterpret_cast<const char*>(invalidMagic.data()),
			static_cast<std::streamsize>(invalidMagic.size()));
		REQUIRE(stream);
	}

	AshEngine::TerrainContainerRevision revision{};
	revision.file_size = 123u;
	CHECK(AshEngine::inspect_terrain_container_revision(path, revision, &error) ==
		AshEngine::TerrainContainerResult::Corrupt);
	CHECK_FALSE(revision.is_valid());
	CHECK(error.find("magic") != std::string::npos);
	std::filesystem::remove(path);
}

#if defined(_WIN32)
TEST_CASE("Terrain container commit lease reports Busy and recovers after release")
{
	const std::filesystem::path path =
		TestDirectory() / "revision-probe-busy.AshTerrain";
	std::filesystem::remove(path);
	const auto snapshot = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		path, *snapshot, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	{
		TerrainTests::ScopedTerrainCommitLeaseForTest lease(path);
		REQUIRE(lease.acquired());
		AshEngine::TerrainContainerRevision revision{};
		CHECK(AshEngine::inspect_terrain_container_revision(path, revision, &error) ==
			AshEngine::TerrainContainerResult::Busy);
		CHECK_FALSE(revision.is_valid());
		CHECK(error.find("lease") != std::string::npos);

		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded = snapshot;
		AshEngine::TerrainContainerLoadReport report{};
		report.loaded_generation = 99u;
		CHECK(AshEngine::load_terrain_container(path, loaded, &report, &error) ==
			AshEngine::TerrainContainerResult::Busy);
		CHECK_FALSE(loaded);
		CHECK(report.loaded_generation == 0u);
	}

	AshEngine::TerrainContainerRevision revision{};
	CHECK(AshEngine::inspect_terrain_container_revision(path, revision, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(revision.is_valid());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	CHECK(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	CHECK(loaded != nullptr);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain staged new publish shares the destination commit lease")
{
	const std::filesystem::path destination =
		TestDirectory() / "staged-publish-destination.AshTerrain";
	const std::filesystem::path staged =
		TestDirectory() / "staged-publish-source.AshTerrain";
	std::filesystem::remove(destination);
	std::filesystem::remove(staged);
	const auto snapshot = MakeIncrementalSnapshot();
	std::string error{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		staged, *snapshot, {}, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);

	{
		TerrainTests::ScopedTerrainCommitLeaseForTest lease(destination);
		REQUIRE(lease.acquired());
		CHECK(AshEngine::publish_staged_terrain_container_new(
			destination, staged, &error) == AshEngine::TerrainContainerResult::Busy);
		CHECK(std::filesystem::exists(staged));
		CHECK_FALSE(std::filesystem::exists(destination));
	}

	REQUIRE(AshEngine::publish_staged_terrain_container_new(
		destination, staged, &error) == AshEngine::TerrainContainerResult::Success);
	CHECK_FALSE(std::filesystem::exists(staged));
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(destination, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(loaded->content_generation == snapshot->content_generation);
	std::filesystem::remove(destination);
}
#endif
