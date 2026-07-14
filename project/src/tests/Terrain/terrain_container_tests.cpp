#include "doctest.h"

#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/TerrainContainerFormat.h"
#include "Function/Asset/TerrainBlockCodec.h"
#include "Function/Asset/TerrainSpatialData.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

	auto CheckMaterialEqual(
		const AshEngine::TerrainMaterialLayerDesc& lhs,
		const AshEngine::TerrainMaterialLayerDesc& rhs) -> void
	{
		CHECK(lhs.name == rhs.name);
		CHECK(lhs.base_color_asset_path == rhs.base_color_asset_path);
		CHECK(lhs.normal_asset_path == rhs.normal_asset_path);
		CHECK(lhs.orm_asset_path == rhs.orm_asset_path);
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
	REQUIRE(height_count_offset + 4u == metadata.size());
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
