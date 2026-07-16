#include "Function/Asset/VegetationChunk.h"
#include "Function/Asset/VegetationLayer.h"
#include "Function/Asset/VegetationSpecies.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
	void CheckEmptyCost(const AshEngine::VegetationLoadCost& cost)
	{
		CHECK(cost.file_bytes == 0);
		CHECK(cost.payload_bytes == 0);
		CHECK(cost.decoded_bytes == 0);
		CHECK(cost.palette_records == 0);
		CHECK(cost.tile_records == 0);
		CHECK(cost.instance_records == 0);
	}

	std::vector<uint8_t> MutateSpecies(
		const std::string& from,
		const std::string& to)
	{
		return VegetationTest::ReplaceJsonToken(
			VegetationTest::CanonicalGrassSpeciesJson(), from, to);
	}

	void CheckSpeciesRejected(
		const std::vector<uint8_t>& bytes,
		const AshEngine::VegetationLoadBudget& budget = VegetationTest::GenerousLoadBudget())
	{
		AshEngine::VegetationSpecies species{};
		species.name = "residue";
		AshEngine::VegetationLoadCost cost{ 1, 1, 1, 1, 1, 1 };
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_species(
			bytes, budget, species, &error, &cost));
		CHECK(species.name.empty());
		CheckEmptyCost(cost);
		CHECK_FALSE(error.empty());
	}

	void CheckLayerRejected(
		const std::vector<uint8_t>& bytes,
		const AshEngine::VegetationLoadBudget& budget = VegetationTest::GenerousLoadBudget())
	{
		AshEngine::VegetationLayerSnapshot layer{};
		layer.content_generation = 99;
		AshEngine::VegetationLoadCost cost{ 1, 1, 1, 1, 1, 1 };
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_layer(bytes, budget, layer, &error, &cost));
		CHECK(layer.content_generation == 0);
		CHECK(layer.palette.empty());
		CHECK(layer.tiles.empty());
		CheckEmptyCost(cost);
		CHECK_FALSE(error.empty());
	}

	void CheckChunkRejected(
		const std::vector<uint8_t>& bytes,
		const AshEngine::VegetationLoadBudget& budget = VegetationTest::GenerousLoadBudget())
	{
		AshEngine::VegetationChunk chunk{};
		chunk.instances.push_back({});
		AshEngine::VegetationLoadCost cost{ 1, 1, 1, 1, 1, 1 };
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_chunk(bytes, budget, chunk, &error, &cost));
		CHECK(chunk.species.empty());
		CHECK(chunk.instances.empty());
		CheckEmptyCost(cost);
		CHECK_FALSE(error.empty());
	}

	size_t LayerTileOffset(const std::vector<uint8_t>& bytes, const uint32_t palette_count = 1)
	{
		size_t offset = 80;
		for (uint32_t index = 0; index < palette_count; ++index)
		{
			offset += 52u + VegetationTest::ReadU16LE(bytes, offset + 48u);
		}
		return offset;
	}

	size_t NextLayerPlaneOffset(const std::vector<uint8_t>& bytes, const size_t plane_offset)
	{
		return plane_offset + 32u + VegetationTest::ReadU32LE(bytes, plane_offset + 24u);
	}

	size_t ChunkInstanceOffset(const std::vector<uint8_t>& bytes, const uint32_t species_count = 1)
	{
		size_t offset = 160;
		for (uint32_t index = 0; index < species_count; ++index)
		{
			offset += 52u + VegetationTest::ReadU16LE(bytes, offset + 48u);
		}
		return offset;
	}

	AshEngine::VegetationPaletteEntry IndexedPaletteEntry(const uint32_t index)
	{
		AshEngine::VegetationPaletteEntry entry{};
		entry.species_id[0] = static_cast<uint8_t>(index >> 8u);
		entry.species_id[1] = static_cast<uint8_t>(index);
		entry.species_id[15] = 1;
		entry.species_sha256.fill(0x5a);
		entry.species_sha256[0] = static_cast<uint8_t>(index >> 8u);
		entry.species_sha256[1] = static_cast<uint8_t>(index);
		entry.species_asset_path =
			"vegetation/preflight/" + std::to_string(index) + ".AshVegetation";
		return entry;
	}
}

TEST_CASE("Vegetation Species rejects scalar coercion and writes one canonical byte stream")
{
	const std::vector<uint8_t> canonical = VegetationTest::CanonicalGrassSpeciesJson();
	AshEngine::VegetationSpecies species{};
	AshEngine::VegetationLoadCost cost{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		canonical, VegetationTest::GenerousLoadBudget(), species, &error, &cost));
	CHECK(cost.file_bytes == canonical.size());
	CHECK(cost.payload_bytes == canonical.size());
	const uint64_t expected_decoded_bytes =
		70u + 4u + species.name.size() +
		species.mesh_lods[0].mesh_asset_path.size() +
		species.mesh_lods[0].material_asset_paths[0].size() +
		species.render.impostor_asset_path.size() +
		species.render.chunk_hlod_asset_path.size();
	CHECK(cost.decoded_bytes == expected_decoded_bytes);
	CHECK(cost.palette_records == 0);
	CHECK(cost.tile_records == 0);
	CHECK(cost.instance_records == 0);

	std::vector<uint8_t> rewritten{};
	REQUIRE(AshEngine::encode_vegetation_species(species, rewritten, &error));
	CHECK(rewritten == canonical);
	CHECK(rewritten.back() == '\n');

	CheckSpeciesRejected(MutateSpecies(
		"\"candidates_per_cell\":8", "\"candidates_per_cell\":[8]"));
	CheckSpeciesRejected(MutateSpecies(
		"\"align_to_normal\":true", "\"align_to_normal\":1"));
	CheckSpeciesRejected(MutateSpecies(
		"\"casts_shadow\":true", "\"casts_shadow\":\"true\""));
}

TEST_CASE("Vegetation Species reviewed fixtures are canonical and replacement-stable")
{
	const std::array<std::string, 2> paths{
		"project/src/tests/fixtures/vegetation/Phase2ManualSpecies.AshVegetation",
		"project/src/tests/fixtures/vegetation/Phase2ManualSpeciesReplacement.AshVegetation"
	};
	std::array<AshEngine::VegetationSpecies, 2> species{};
	std::array<AshEngine::VegetationSha256, 2> digests{};
	for (size_t index = 0; index < paths.size(); ++index)
	{
		const std::vector<uint8_t> bytes = VegetationTest::ReadFixtureBytes(paths[index]);
		REQUIRE_FALSE(bytes.empty());
		std::string error{};
		REQUIRE(AshEngine::decode_vegetation_species(
			bytes, VegetationTest::GenerousLoadBudget(), species[index], &error));
		std::vector<uint8_t> rewritten{};
		REQUIRE(AshEngine::encode_vegetation_species(species[index], rewritten, &error));
		CHECK(rewritten == bytes);
		digests[index] = AshEngine::vegetation_sha256(bytes.data(), bytes.size());
	}
	CHECK(species[0].species_id == species[1].species_id);
	CHECK(digests[0] != digests[1]);
}

TEST_CASE("Vegetation Species rejects malformed JSON shape paths ranges and noncanonical identity")
{
	std::vector<uint8_t> bom = VegetationTest::CanonicalGrassSpeciesJson();
	bom.insert(bom.begin(), { 0xef, 0xbb, 0xbf });
	CheckSpeciesRejected(bom);

	std::vector<uint8_t> trailing = VegetationTest::CanonicalGrassSpeciesJson();
	trailing.insert(trailing.end(), { 'x' });
	CheckSpeciesRejected(trailing);
	CheckSpeciesRejected(MutateSpecies(
		"\"schema_version\":1", "\"schema_version\":1,\"schema_version\":1"));
	CheckSpeciesRejected(MutateSpecies(
		"\"schema_version\":1", "\"schema_version\":1,\"unknown\":0"));
	CheckSpeciesRejected(MutateSpecies(
		"\"name\":\"Phase 2 Meadow Grass\",", ""));
	CheckSpeciesRejected(MutateSpecies(
		"\"mesh_asset_path\":\"models/vegetation/phase2_grass_lod0.fbx\",",
		"\"mesh_asset_path\":\"models/vegetation/phase2_grass_lod0.fbx\","
		"\"mesh_asset_path\":\"models/vegetation/phase2_grass_lod0.fbx\","));
	CheckSpeciesRejected(MutateSpecies(
		"\"bounds_mm\":{", "\"bounds_mm\":{\"unknown\":0,"));
	CheckSpeciesRejected(MutateSpecies(
		"\"placement\":{", "\"placement\":{\"unknown\":0,"));
	CheckSpeciesRejected(MutateSpecies(
		"00112233445566778899aabbccddeeff", "00112233445566778899AABBCCDDEEFF"));
	CheckSpeciesRejected(MutateSpecies(
		"00112233445566778899aabbccddeeff", "00000000000000000000000000000000"));
	CheckSpeciesRejected(MutateSpecies(
		"models/vegetation/phase2_grass_lod0.fbx", "../grass.fbx"));
	CheckSpeciesRejected(MutateSpecies(
		"models/vegetation/phase2_grass_lod0.fbx", "/absolute/grass.fbx"));
	CheckSpeciesRejected(MutateSpecies(
		"models/vegetation/phase2_grass_lod0.fbx", "models\\grass.fbx"));
	CheckSpeciesRejected(MutateSpecies(
		"models/vegetation/phase2_grass_lod0.fbx", "models/./grass.fbx"));
	CheckSpeciesRejected(MutateSpecies(
		"models/vegetation/phase2_grass_lod0.fbx", "models//grass.fbx"));
	CheckSpeciesRejected(MutateSpecies(
		"\"mesh_lods\":[{", "\"mesh_lods\":[] ,\"ignored_lods\":[{"));
	CheckSpeciesRejected(MutateSpecies(
		"\"material_asset_paths\":[\"materials/vegetation/phase2_grass.AshMaterial\"]",
		"\"material_asset_paths\":[]"));
	CheckSpeciesRejected(MutateSpecies(
		"\"screen_error_milli\":250", "\"screen_error_milli\":0"));
	CheckSpeciesRejected(MutateSpecies(
		"\"candidates_per_cell\":8", "\"candidates_per_cell\":257"));
	CheckSpeciesRejected(MutateSpecies(
		"\"min_scale_q12\":3277,\"max_scale_q12\":4915",
		"\"min_scale_q12\":4916,\"max_scale_q12\":4915"));
	CheckSpeciesRejected(MutateSpecies(
		"\"max_slope_milliradians\":785", "\"max_slope_milliradians\":1572"));
	CheckSpeciesRejected(MutateSpecies(
		"\"min\":[-500,0,-500],\"max\":[500,1500,500]",
		"\"min\":[500,0,-500],\"max\":[500,1500,500]"));
	CheckSpeciesRejected(MutateSpecies(
		"\"material_slot_min\":[0,0,0,0,0,0,0,0]",
		"\"material_slot_min\":[0,0,0,0,0,0,0]"));
	CheckSpeciesRejected(MutateSpecies(
		"\"material_slot_max\":[255,255,255,255,255,255,255,255]",
		"\"material_slot_max\":[255,255,255,255,255,255,255,256]"));
	CheckSpeciesRejected(MutateSpecies(
		"\"deformation\":\"Grass\"", "\"deformation\":\"Shrub\""));

	std::vector<uint8_t> invalid_utf8 = VegetationTest::CanonicalGrassSpeciesJson();
	const std::string marker = "Phase 2 Meadow Grass";
	const auto position = std::search(invalid_utf8.begin(), invalid_utf8.end(), marker.begin(), marker.end());
	REQUIRE(position != invalid_utf8.end());
	*position = 0xc0;
	CheckSpeciesRejected(invalid_utf8);
}

TEST_CASE("Vegetation Species encoder clears bytes on an invalid DTO")
{
	AshEngine::VegetationSpecies species{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		VegetationTest::CanonicalGrassSpeciesJson(),
		VegetationTest::GenerousLoadBudget(), species, &error));
	species.placement.candidates_per_cell = 0;
	std::vector<uint8_t> bytes{ 0xaa };
	CHECK_FALSE(AshEngine::encode_vegetation_species(species, bytes, &error));
	CHECK(bytes.empty());
	CHECK_FALSE(error.empty());

	species = {};
	REQUIRE(AshEngine::decode_vegetation_species(
		VegetationTest::CanonicalGrassSpeciesJson(),
		VegetationTest::GenerousLoadBudget(), species, &error));
	species.name.assign(1, static_cast<char>(0xc0));
	bytes = { 0xaa };
	bool encoded = true;
	CHECK_NOTHROW(encoded = AshEngine::encode_vegetation_species(species, bytes, &error));
	CHECK_FALSE(encoded);
	CHECK(bytes.empty());
	CHECK_FALSE(error.empty());
}

TEST_CASE("Vegetation Species accepts exact collection and UTF8 byte limits and rejects adjacent values")
{
	AshEngine::VegetationSpecies base{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		VegetationTest::CanonicalGrassSpeciesJson(),
		VegetationTest::GenerousLoadBudget(), base, &error));
	auto require_round_trip = [&](const AshEngine::VegetationSpecies& source)
	{
		std::vector<uint8_t> bytes{};
		REQUIRE(AshEngine::encode_vegetation_species(source, bytes, &error));
		AshEngine::VegetationSpecies decoded{};
		REQUIRE(AshEngine::decode_vegetation_species(
			bytes, VegetationTest::GenerousLoadBudget(), decoded, &error));
	};
	auto check_encode_rejected = [&](const AshEngine::VegetationSpecies& source)
	{
		std::vector<uint8_t> bytes{ 0xaa };
		bool encoded = true;
		CHECK_NOTHROW(encoded = AshEngine::encode_vegetation_species(source, bytes, &error));
		CHECK_FALSE(encoded);
		CHECK(bytes.empty());
		CHECK_FALSE(error.empty());
	};

	SUBCASE("name byte limits")
	{
		auto species = base; species.name = "n"; require_round_trip(species);
		species.name.assign(256, 'n'); require_round_trip(species);
		species.name.clear(); check_encode_rejected(species);
		species.name.assign(257, 'n'); check_encode_rejected(species);
	}
	SUBCASE("asset path byte limits")
	{
		auto species = base; species.mesh_lods[0].mesh_asset_path = "a"; require_round_trip(species);
		species.mesh_lods[0].mesh_asset_path = std::string(4092, 'a') + ".fbx";
		require_round_trip(species);
		species.mesh_lods[0].mesh_asset_path.clear(); check_encode_rejected(species);
		species.mesh_lods[0].mesh_asset_path = std::string(4093, 'a') + ".fbx";
		check_encode_rejected(species);
	}
	SUBCASE("material count limits")
	{
		auto species = base;
		species.mesh_lods[0].material_asset_paths.clear();
		for (size_t index = 0; index < 64; ++index)
			species.mesh_lods[0].material_asset_paths.push_back(
				"materials/limit_" + std::to_string(index) + ".AshMaterial");
		require_round_trip(species);
		species.mesh_lods[0].material_asset_paths.clear(); check_encode_rejected(species);
		for (size_t index = 0; index < 65; ++index)
			species.mesh_lods[0].material_asset_paths.push_back(
				"materials/overflow_" + std::to_string(index) + ".AshMaterial");
		check_encode_rejected(species);
	}
	SUBCASE("LOD count and strict error ordering")
	{
		auto species = base;
		species.mesh_lods.clear();
		for (uint32_t index = 0; index < 16; ++index)
		{
			auto lod = base.mesh_lods[0];
			lod.mesh_asset_path = "models/vegetation/limit_lod_" + std::to_string(index) + ".fbx";
			lod.screen_error_milli = index + 1u;
			species.mesh_lods.push_back(std::move(lod));
		}
		require_round_trip(species);
		species.mesh_lods.clear(); check_encode_rejected(species);
		species = base;
		for (uint32_t index = 1; index < 17; ++index)
		{
			auto lod = base.mesh_lods[0];
			lod.mesh_asset_path = "models/vegetation/overflow_lod_" + std::to_string(index) + ".fbx";
			lod.screen_error_milli = base.mesh_lods[0].screen_error_milli + index;
			species.mesh_lods.push_back(std::move(lod));
		}
		check_encode_rejected(species);

		species = base;
		auto second = species.mesh_lods[0];
		second.mesh_asset_path = "models/vegetation/second_lod.fbx";
		second.screen_error_milli += 1;
		species.mesh_lods.push_back(second);
		require_round_trip(species);
		species.mesh_lods[1].screen_error_milli = species.mesh_lods[0].screen_error_milli;
		check_encode_rejected(species);
		species.mesh_lods[1].screen_error_milli += 1;
		species.mesh_lods[1].mesh_asset_path = species.mesh_lods[0].mesh_asset_path;
		check_encode_rejected(species);
	}
}

TEST_CASE("Vegetation Species load budget fails closed for every wire-derived cost")
{
	const std::vector<uint8_t> bytes = VegetationTest::CanonicalGrassSpeciesJson();
	AshEngine::VegetationSpecies admitted{};
	AshEngine::VegetationLoadCost exact_cost{};
	std::string exact_error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		bytes, VegetationTest::GenerousLoadBudget(), admitted, &exact_error, &exact_cost));
	AshEngine::VegetationLoadBudget exact_budget{};
	exact_budget.max_file_bytes = exact_cost.file_bytes;
	exact_budget.max_payload_bytes = exact_cost.payload_bytes;
	exact_budget.max_decoded_bytes = exact_cost.decoded_bytes;
	REQUIRE(AshEngine::decode_vegetation_species(
		bytes, exact_budget, admitted, &exact_error));

	AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
	budget.max_file_bytes = bytes.size() - 1;
	AshEngine::VegetationSpecies species{};
	AshEngine::VegetationLoadCost cost{ 1, 1, 1, 1, 1, 1 };
	std::string error{};
	CHECK_FALSE(AshEngine::decode_vegetation_species(bytes, budget, species, &error, &cost));
	CHECK(species.name.empty());
	CheckEmptyCost(cost);

	budget = VegetationTest::GenerousLoadBudget();
	budget.max_payload_bytes = exact_cost.payload_bytes - 1;
	CHECK_FALSE(AshEngine::decode_vegetation_species(bytes, budget, species, &error, &cost));
	CheckEmptyCost(cost);
	budget = VegetationTest::GenerousLoadBudget();
	budget.max_decoded_bytes = exact_cost.decoded_bytes - 1;
	CHECK_FALSE(AshEngine::decode_vegetation_species(bytes, budget, species, &error, &cost));
	CheckEmptyCost(cost);
	CHECK_FALSE(AshEngine::decode_vegetation_species(
		bytes, AshEngine::VegetationLoadBudget{}, species, &error, &cost));
	CheckEmptyCost(cost);
}

TEST_CASE("Vegetation codec preflight rejects tiny decoded budgets before DTO ownership")
{
	AshEngine::VegetationLoadBudget tiny = VegetationTest::GenerousLoadBudget();
	tiny.max_decoded_bytes = 1;
	std::string error{};

	SUBCASE("Species 4096-byte path")
	{
		AshEngine::VegetationSpecies species{};
		REQUIRE(AshEngine::decode_vegetation_species(
			VegetationTest::CanonicalGrassSpeciesJson(),
			VegetationTest::GenerousLoadBudget(), species, &error));
		species.mesh_lods[0].mesh_asset_path = std::string(4092, 'a') + ".fbx";
		std::vector<uint8_t> bytes{};
		REQUIRE(AshEngine::encode_vegetation_species(species, bytes, &error));
		AshEngine::VegetationSpecies admitted{};
		AshEngine::VegetationLoadCost cost{};
		REQUIRE(AshEngine::decode_vegetation_species(
			bytes, VegetationTest::GenerousLoadBudget(), admitted, &error, &cost));
		CHECK(cost.decoded_bytes > tiny.max_decoded_bytes);
		CheckSpeciesRejected(bytes, tiny);
	}

	SUBCASE("Layer large declared palette")
	{
		AshEngine::VegetationLayerSnapshot layer{};
		layer.layer_id = VegetationTest::SequentialId(33);
		layer.content_generation = 1;
		for (uint32_t index = 0; index < 512; ++index)
		{
			layer.palette.push_back(IndexedPaletteEntry(index));
		}
		std::vector<uint8_t> bytes{};
		REQUIRE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
		AshEngine::VegetationLayerSnapshot admitted{};
		AshEngine::VegetationLoadCost cost{};
		REQUIRE(AshEngine::decode_vegetation_layer(
			bytes, VegetationTest::GenerousLoadBudget(), admitted, &error, &cost));
		CHECK(cost.palette_records == 512);
		CHECK(cost.decoded_bytes > tiny.max_decoded_bytes);
		CheckLayerRejected(bytes, tiny);
	}

	SUBCASE("Chunk large declared species and instance counts")
	{
		auto chunk = VegetationTest::MinimalChunk();
		chunk.species.clear();
		chunk.instances.clear();
		for (uint32_t index = 0; index < 512; ++index)
		{
			chunk.species.push_back(IndexedPaletteEntry(index));
			AshEngine::VegetationChunkInstance instance{};
			instance.species_index = static_cast<uint16_t>(index);
			instance.scale_q12 = 4096;
			chunk.instances.push_back(instance);
		}
		chunk.min_world_height_mm = 0;
		chunk.max_world_height_mm = 0;
		std::vector<uint8_t> bytes{};
		REQUIRE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
		AshEngine::VegetationChunk admitted{};
		AshEngine::VegetationLoadCost cost{};
		REQUIRE(AshEngine::decode_vegetation_chunk(
			bytes, VegetationTest::GenerousLoadBudget(), admitted, &error, &cost));
		CHECK(cost.palette_records == 512);
		CHECK(cost.instance_records == 512);
		CHECK(cost.decoded_bytes > tiny.max_decoded_bytes);
		CheckChunkRejected(bytes, tiny);
	}
}

TEST_CASE("Vegetation Layer codec writes exact header and canonical round trip")
{
	const AshEngine::VegetationLayerSnapshot source = VegetationTest::MinimalLayerSnapshot();
	const std::vector<uint8_t> bytes = VegetationTest::MinimalLayerBytes();
	REQUIRE(bytes.size() > 80);
	CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "ASVL");
	CHECK(bytes[4] == 1);
	CHECK(bytes[6] == 80);

	AshEngine::VegetationLayerSnapshot decoded{};
	AshEngine::VegetationLoadCost cost{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_layer(
		bytes, VegetationTest::GenerousLoadBudget(), decoded, &error, &cost));
	CHECK(decoded.palette.size() == 1);
	CHECK(decoded.tiles.size() == 1);
	CHECK(decoded.tiles[0].planes.size() == 2);
	CHECK(cost.file_bytes == bytes.size());
	CHECK(cost.palette_records == 1);
	CHECK(cost.tile_records == 1);
	CHECK(cost.instance_records == 0);
	const uint64_t expected_decoded_bytes =
		32u + 48u + decoded.palette[0].species_asset_path.size() +
		16u + 2u * (17u + 1024u);
	CHECK(cost.decoded_bytes == expected_decoded_bytes);

	std::vector<uint8_t> rewritten{};
	REQUIRE(AshEngine::encode_vegetation_layer(decoded, rewritten, &error));
	CHECK(rewritten == bytes);
}

TEST_CASE("Vegetation Layer codec load budget rejects before publishing a partial object")
{
	const std::vector<uint8_t> bytes = VegetationTest::MinimalLayerBytes();
	AshEngine::VegetationLayerSnapshot admitted{};
	AshEngine::VegetationLoadCost exact_cost{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_layer(
		bytes, VegetationTest::GenerousLoadBudget(), admitted, &error, &exact_cost));
	AshEngine::VegetationLoadBudget exact_budget{};
	exact_budget.max_file_bytes = exact_cost.file_bytes;
	exact_budget.max_payload_bytes = exact_cost.payload_bytes;
	exact_budget.max_decoded_bytes = exact_cost.decoded_bytes;
	exact_budget.max_palette_records = exact_cost.palette_records;
	exact_budget.max_tile_records = exact_cost.tile_records;
	exact_budget.max_instance_records = 0;
	REQUIRE(AshEngine::decode_vegetation_layer(bytes, exact_budget, admitted, &error));
	for (size_t budget_case = 0; budget_case < 5; ++budget_case)
	{
		AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
		switch (budget_case)
		{
		case 0: budget.max_file_bytes = exact_cost.file_bytes - 1; break;
		case 1: budget.max_payload_bytes = exact_cost.payload_bytes - 1; break;
		case 2: budget.max_decoded_bytes = exact_cost.decoded_bytes - 1; break;
		case 3: budget.max_palette_records = exact_cost.palette_records - 1; break;
		case 4: budget.max_tile_records = exact_cost.tile_records - 1; break;
		}
		CheckLayerRejected(bytes, budget);
	}
	CheckLayerRejected(bytes, AshEngine::VegetationLoadBudget{});
}

TEST_CASE("Vegetation Layer codec rejects noncanonical RLE and writes Raw when RLE is not smaller")
{
	std::vector<uint8_t> noncanonical = VegetationTest::MinimalLayerBytes();
	const size_t palette_path_length = VegetationTest::ReadU16LE(noncanonical, 80 + 48);
	const size_t tile_offset = 80 + 52 + palette_path_length;
	const size_t plane_offset = tile_offset + 24;
	REQUIRE(noncanonical[plane_offset + 1] ==
		static_cast<uint8_t>(AshEngine::VegetationLayerPlaneCodec::Rle));
	REQUIRE(VegetationTest::ReadU32LE(noncanonical, plane_offset + 24) == 6);
	const size_t data_offset = plane_offset + 32;
	noncanonical.erase(noncanonical.begin() + data_offset,
		noncanonical.begin() + data_offset + 6);
	const std::array<uint8_t, 9> split_runs{ 1, 0, 255, 1, 0, 0, 0xfe, 0x03, 0 };
	noncanonical.insert(noncanonical.begin() + data_offset,
		split_runs.begin(), split_runs.end());
	VegetationTest::WriteU32LE(noncanonical, plane_offset + 24, 9);
	VegetationTest::WriteU32LE(noncanonical, tile_offset + 20,
		VegetationTest::ReadU32LE(noncanonical, tile_offset + 20) + 3);
	VegetationTest::WriteU64LE(noncanonical, 60,
		VegetationTest::ReadU64LE(noncanonical, 60) + 3);
	VegetationTest::RepairLayerCrcs(noncanonical);
	CheckLayerRejected(noncanonical);

	auto rle_layer = VegetationTest::MinimalLayerSnapshot();
	size_t cursor = 0;
	for (size_t run = 0; run < 341; ++run)
	{
		const size_t run_length = run == 340 ? 4 : 3;
		std::fill_n(rle_layer.tiles[0].planes[0].values.begin() +
			static_cast<std::ptrdiff_t>(cursor), run_length, static_cast<uint8_t>(run & 1u));
		cursor += run_length;
	}
	std::vector<uint8_t> rle_bytes{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_layer(rle_layer, rle_bytes, &error));
	const size_t rle_path_length = VegetationTest::ReadU16LE(rle_bytes, 80 + 48);
	const size_t rle_plane_offset = 80 + 52 + rle_path_length + 24;
	CHECK(rle_bytes[rle_plane_offset + 1] ==
		static_cast<uint8_t>(AshEngine::VegetationLayerPlaneCodec::Rle));
	CHECK(VegetationTest::ReadU32LE(rle_bytes, rle_plane_offset + 24) == 1023);

	auto raw_layer = VegetationTest::MinimalLayerSnapshot();
	cursor = 0;
	for (size_t run = 0; run < 342; ++run)
	{
		const size_t run_length = run == 341 ? 1 : 3;
		std::fill_n(raw_layer.tiles[0].planes[0].values.begin() +
			static_cast<std::ptrdiff_t>(cursor), run_length, static_cast<uint8_t>(run & 1u));
		cursor += run_length;
	}
	std::vector<uint8_t> raw_bytes{};
	REQUIRE(AshEngine::encode_vegetation_layer(raw_layer, raw_bytes, &error));
	const size_t raw_path_length = VegetationTest::ReadU16LE(raw_bytes, 80 + 48);
	const size_t raw_plane_offset = 80 + 52 + raw_path_length + 24;
	CHECK(raw_bytes[raw_plane_offset + 1] ==
		static_cast<uint8_t>(AshEngine::VegetationLayerPlaneCodec::Raw));
	CHECK(VegetationTest::ReadU32LE(raw_bytes, raw_plane_offset + 24) == 1024);

	SUBCASE("reader rejects Raw when canonical RLE is shorter")
	{
		auto forced_raw = VegetationTest::MinimalLayerBytes();
		const size_t tile = LayerTileOffset(forced_raw);
		const size_t plane = tile + 24u;
		const uint32_t old_size = VegetationTest::ReadU32LE(forced_raw, plane + 24u);
		REQUIRE(old_size == 6u);
		const auto values = VegetationTest::MinimalLayerSnapshot().tiles[0].planes[0].values;
		forced_raw.erase(forced_raw.begin() + static_cast<std::ptrdiff_t>(plane + 32u),
			forced_raw.begin() + static_cast<std::ptrdiff_t>(plane + 32u + old_size));
		forced_raw.insert(forced_raw.begin() + static_cast<std::ptrdiff_t>(plane + 32u),
			values.begin(), values.end());
		forced_raw[plane + 1u] = static_cast<uint8_t>(AshEngine::VegetationLayerPlaneCodec::Raw);
		VegetationTest::WriteU32LE(forced_raw, plane + 24u, 1024u);
		VegetationTest::WriteU32LE(forced_raw, tile + 20u,
			VegetationTest::ReadU32LE(forced_raw, tile + 20u) + 1024u - old_size);
		VegetationTest::WriteU64LE(forced_raw, 60u,
			VegetationTest::ReadU64LE(forced_raw, 60u) + 1024u - old_size);
		VegetationTest::RepairLayerCrcs(forced_raw);
		CheckLayerRejected(forced_raw);
	}
}

TEST_CASE("Vegetation Layer codec rejects header payload ordering and shape corruption")
{
	const std::vector<uint8_t> canonical = VegetationTest::MinimalLayerBytes();
	const size_t tile_offset = LayerTileOffset(canonical);
	const size_t density_offset = tile_offset + 24u;
	const size_t weight_offset = NextLayerPlaneOffset(canonical, density_offset);
	SUBCASE("bad magic")
	{
		auto bytes = canonical; bytes[0] = 'X'; CheckLayerRejected(bytes);
	}
	SUBCASE("unknown schema")
	{
		auto bytes = canonical; VegetationTest::WriteU16LE(bytes, 4, 2);
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
	}
	SUBCASE("wrong header size")
	{
		auto bytes = canonical; VegetationTest::WriteU16LE(bytes, 6, 79);
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
	}
	SUBCASE("nonzero flags")
	{
		auto bytes = canonical; VegetationTest::WriteU32LE(bytes, 8, 1);
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
	}
	SUBCASE("wrong tile constants")
	{
		auto bytes = canonical; VegetationTest::WriteU32LE(bytes, 12, 31);
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
		bytes = canonical; VegetationTest::WriteU32LE(bytes, 16, 3199);
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
	}
	SUBCASE("reserved header")
	{
		auto bytes = canonical; bytes[76] = 1; VegetationTest::RepairHeaderCrc(bytes, 80, 72);
		CheckLayerRejected(bytes);
	}
	SUBCASE("payload CRC")
	{
		auto bytes = canonical; bytes.back() ^= 1; CheckLayerRejected(bytes);
	}
	SUBCASE("trailing byte")
	{
		auto bytes = canonical; bytes.push_back(0); CheckLayerRejected(bytes);
	}
	SUBCASE("zero generation")
	{
		auto bytes = canonical;
		for (size_t index = 24; index < 32; ++index) bytes[index] = 0;
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
	}
	SUBCASE("zero layer id")
	{
		auto bytes = canonical; std::fill(bytes.begin() + 40, bytes.begin() + 56, 0);
		VegetationTest::RepairHeaderCrc(bytes, 80, 72); CheckLayerRejected(bytes);
	}
	SUBCASE("palette reserved")
	{
		auto bytes = canonical; bytes[80 + 50] = 1;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("tile reserved")
	{
		auto bytes = canonical; bytes[tile_offset + 18] = 1;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("tile record size")
	{
		auto bytes = canonical;
		VegetationTest::WriteU32LE(bytes, tile_offset + 20,
			VegetationTest::ReadU32LE(bytes, tile_offset + 20) - 1);
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("plane reserved kind codec and decoded size")
	{
		auto bytes = canonical; bytes[density_offset + 2] = 1;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
		bytes = canonical; bytes[density_offset] = 2;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
		bytes = canonical; bytes[density_offset + 1] = 2;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
		bytes = canonical; VegetationTest::WriteU32LE(bytes, density_offset + 20, 1023);
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("decoded plane CRC")
	{
		auto bytes = canonical; bytes[density_offset + 28] ^= 1;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("RLE zero run")
	{
		auto bytes = canonical; VegetationTest::WriteU16LE(bytes, density_offset + 32, 0);
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("RLE sum is not 1024")
	{
		auto bytes = canonical; VegetationTest::WriteU16LE(bytes, density_offset + 35, 1022);
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("weight references unknown species")
	{
		auto bytes = canonical; bytes[weight_offset + 4] ^= 0x80;
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("density is not first")
	{
		auto bytes = canonical;
		const size_t density_size = NextLayerPlaneOffset(bytes, density_offset) - density_offset;
		const size_t weight_size = NextLayerPlaneOffset(bytes, weight_offset) - weight_offset;
		REQUIRE(density_size == weight_size);
		std::vector<uint8_t> density(bytes.begin() + static_cast<std::ptrdiff_t>(density_offset),
			bytes.begin() + static_cast<std::ptrdiff_t>(density_offset + density_size));
		std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(weight_offset),
			bytes.begin() + static_cast<std::ptrdiff_t>(weight_offset + weight_size),
			bytes.begin() + static_cast<std::ptrdiff_t>(density_offset));
		std::copy(density.begin(), density.end(),
			bytes.begin() + static_cast<std::ptrdiff_t>(weight_offset));
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("canonical all-zero density plane")
	{
		auto bytes = canonical;
		const uint32_t old_size = VegetationTest::ReadU32LE(bytes, density_offset + 24);
		REQUIRE(old_size == 6u);
		const std::array<uint8_t, 3> zero_run{ 0x00, 0x04, 0x00 };
		bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(density_offset + 32),
			bytes.begin() + static_cast<std::ptrdiff_t>(density_offset + 32 + old_size));
		bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(density_offset + 32),
			zero_run.begin(), zero_run.end());
		VegetationTest::WriteU32LE(bytes, density_offset + 24, 3);
		std::array<uint8_t, 1024> zeros{};
		VegetationTest::WriteU32LE(bytes, density_offset + 28,
			AshEngine::vegetation_crc32(zeros.data(), zeros.size()));
		VegetationTest::WriteU32LE(bytes, tile_offset + 20,
			VegetationTest::ReadU32LE(bytes, tile_offset + 20) - 3);
		VegetationTest::WriteU64LE(bytes, 60,
			VegetationTest::ReadU64LE(bytes, 60) - 3);
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("duplicate tile coordinates")
	{
		auto layer = VegetationTest::MinimalLayerSnapshot();
		auto second = layer.tiles[0]; second.tile_x = -1; layer.tiles.push_back(second);
		std::vector<uint8_t> bytes{}; std::string error{};
		REQUIRE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
		const size_t first = LayerTileOffset(bytes);
		const size_t second_offset = first + 24u + VegetationTest::ReadU32LE(bytes, first + 20u);
		std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(first),
			bytes.begin() + static_cast<std::ptrdiff_t>(first + 16u),
			bytes.begin() + static_cast<std::ptrdiff_t>(second_offset));
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
	SUBCASE("duplicate palette and unsorted weight planes")
	{
		auto layer = VegetationTest::MinimalLayerSnapshot();
		auto second = layer.palette[0];
		second.species_id = VegetationTest::SequentialId(17);
		second.species_asset_path = "vegetation/Phase2OtherSpecies.AshVegetation";
		layer.palette.push_back(second);
		auto second_weight = layer.tiles[0].planes[1];
		second_weight.species_id = second.species_id;
		layer.tiles[0].planes.push_back(second_weight);
		std::vector<uint8_t> bytes{}; std::string error{};
		REQUIRE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
		const size_t second_palette = 80u + 52u + VegetationTest::ReadU16LE(bytes, 128u);
		auto duplicate = bytes;
		std::copy(duplicate.begin() + 80, duplicate.begin() + 96,
			duplicate.begin() + static_cast<std::ptrdiff_t>(second_palette));
		VegetationTest::RepairLayerCrcs(duplicate); CheckLayerRejected(duplicate);

		const size_t tile = LayerTileOffset(bytes, 2);
		const size_t density = tile + 24u;
		const size_t first_weight = NextLayerPlaneOffset(bytes, density);
		const size_t second_weight_offset = NextLayerPlaneOffset(bytes, first_weight);
		std::array<uint8_t, 16> first_id{};
		std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(first_weight + 4u), 16,
			first_id.begin());
		std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(second_weight_offset + 4u), 16,
			bytes.begin() + static_cast<std::ptrdiff_t>(first_weight + 4u));
		std::copy(first_id.begin(), first_id.end(),
			bytes.begin() + static_cast<std::ptrdiff_t>(second_weight_offset + 4u));
		VegetationTest::RepairLayerCrcs(bytes); CheckLayerRejected(bytes);
	}
}

TEST_CASE("Vegetation Layer codec writer rejects zero planes duplicate palette and noncanonical order")
{
	std::string error{};
	std::vector<uint8_t> bytes{};
	SUBCASE("zero density plane")
	{
		auto layer = VegetationTest::MinimalLayerSnapshot();
		layer.tiles[0].planes[0].values.fill(0);
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
		CHECK(bytes.empty());
	}
	SUBCASE("duplicate palette")
	{
		auto layer = VegetationTest::MinimalLayerSnapshot();
		layer.palette.push_back(layer.palette[0]);
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
		CHECK(bytes.empty());
	}
	SUBCASE("weight before density")
	{
		auto layer = VegetationTest::MinimalLayerSnapshot();
		std::swap(layer.tiles[0].planes[0], layer.tiles[0].planes[1]);
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
		CHECK(bytes.empty());
	}
}

TEST_CASE("Vegetation Layer codec palette ceiling accepts zero and 65534 and rejects 65535")
{
	AshEngine::VegetationLayerSnapshot layer{};
	layer.layer_id = VegetationTest::SequentialId(33);
	layer.content_generation = 1;
	std::vector<uint8_t> bytes{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
	AshEngine::VegetationLayerSnapshot decoded{};
	AshEngine::VegetationLoadCost cost{};
	REQUIRE(AshEngine::decode_vegetation_layer(
		bytes, VegetationTest::GenerousLoadBudget(), decoded, &error, &cost));
	CHECK(decoded.palette.empty());
	CHECK(cost.palette_records == 0);
	CHECK(cost.decoded_bytes == 32);

	layer.palette.reserve(65534);
	for (uint32_t index = 0; index < 65534; ++index)
		layer.palette.push_back(IndexedPaletteEntry(index));
	REQUIRE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
	REQUIRE(AshEngine::decode_vegetation_layer(
		bytes, VegetationTest::GenerousLoadBudget(), decoded, &error, &cost));
	CHECK(decoded.palette.size() == 65534);
	CHECK(cost.palette_records == 65534);

	layer.palette.push_back(layer.palette.back());
	bytes.assign(1, 0xaa);
	CHECK_FALSE(AshEngine::encode_vegetation_layer(layer, bytes, &error));
	CHECK(bytes.empty());
}

TEST_CASE("Vegetation Chunk codec writes exact header record and canonical round trip")
{
	const std::vector<uint8_t> bytes = VegetationTest::MinimalChunkBytes();
	REQUIRE(bytes.size() > 160);
	CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "ASVC");
	CHECK(bytes[4] == 1);
	CHECK(bytes[6] == 160);

	AshEngine::VegetationChunk chunk{};
	AshEngine::VegetationLoadCost cost{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_chunk(
		bytes, VegetationTest::GenerousLoadBudget(), chunk, &error, &cost));
	REQUIRE(chunk.instances.size() == 1);
	CHECK(chunk.instances[0].cell_fraction_x_u16 == 65535);
	CHECK(chunk.instances[0].yaw_turn_u16 == 32768);
	CHECK(chunk.instances[0].scale_q12 == 4096);
	CHECK(chunk.instances[0].normal_oct_x == 0);
	CHECK(chunk.instances[0].normal_oct_y == 0);
	CHECK(chunk.min_world_height_mm == 1250);
	CHECK(chunk.max_world_height_mm == 1250);
	CHECK(cost.palette_records == 1);
	CHECK(cost.instance_records == 1);
	CHECK(cost.tile_records == 0);
	CHECK(cost.decoded_bytes ==
		112u + 48u + chunk.species[0].species_asset_path.size() + 28u);

	std::vector<uint8_t> rewritten{};
	REQUIRE(AshEngine::encode_vegetation_chunk(chunk, rewritten, &error));
	CHECK(rewritten == bytes);
}

TEST_CASE("Vegetation Chunk codec rejects zero instances unreferenced species and loose extrema")
{
	const std::vector<uint8_t> canonical = VegetationTest::MinimalChunkBytes();
	SUBCASE("zero instances")
	{
		auto bytes = canonical;
		VegetationTest::WriteU32LE(bytes, 124, 0);
		VegetationTest::RepairChunkHeaderCrc(bytes);
		CheckChunkRejected(bytes);
	}
	SUBCASE("loose minimum")
	{
		auto bytes = canonical;
		VegetationTest::WriteU32LE(bytes, 128, 1249);
		VegetationTest::RepairChunkHeaderCrc(bytes);
		CheckChunkRejected(bytes);
	}
	SUBCASE("unreferenced species")
	{
		auto chunk = VegetationTest::MinimalChunk();
		auto second = chunk.species[0];
		second.species_id = VegetationTest::SequentialId(17);
		second.species_asset_path = "vegetation/Other.AshVegetation";
		chunk.species.push_back(second);
		std::vector<uint8_t> bytes{};
		std::string error{};
		CHECK_FALSE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
		CHECK(bytes.empty());
	}
}

TEST_CASE("Vegetation Chunk codec rejects corrupt shape ordering and every budget overrun")
{
	const std::vector<uint8_t> canonical = VegetationTest::MinimalChunkBytes();
	const size_t instance_offset = ChunkInstanceOffset(canonical);
	AshEngine::VegetationChunk admitted{};
	AshEngine::VegetationLoadCost exact_cost{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_chunk(
		canonical, VegetationTest::GenerousLoadBudget(), admitted, &error, &exact_cost));
	AshEngine::VegetationLoadBudget exact_budget{};
	exact_budget.max_file_bytes = exact_cost.file_bytes;
	exact_budget.max_payload_bytes = exact_cost.payload_bytes;
	exact_budget.max_decoded_bytes = exact_cost.decoded_bytes;
	exact_budget.max_palette_records = exact_cost.palette_records;
	exact_budget.max_tile_records = 0;
	exact_budget.max_instance_records = exact_cost.instance_records;
	REQUIRE(AshEngine::decode_vegetation_chunk(canonical, exact_budget, admitted, &error));
	SUBCASE("payload corruption")
	{
		auto bytes = canonical; bytes.back() ^= 1; CheckChunkRejected(bytes);
	}
	SUBCASE("header reserved")
	{
		auto bytes = canonical; bytes[152] = 1; VegetationTest::RepairChunkHeaderCrc(bytes);
		CheckChunkRejected(bytes);
	}
	SUBCASE("fixed header fields")
	{
		auto bytes = canonical; VegetationTest::WriteU16LE(bytes, 4, 2);
		VegetationTest::RepairChunkHeaderCrc(bytes); CheckChunkRejected(bytes);
		bytes = canonical; VegetationTest::WriteU16LE(bytes, 6, 159);
		VegetationTest::RepairChunkHeaderCrc(bytes); CheckChunkRejected(bytes);
		bytes = canonical; VegetationTest::WriteU32LE(bytes, 8, 2);
		VegetationTest::RepairChunkHeaderCrc(bytes); CheckChunkRejected(bytes);
		bytes = canonical; VegetationTest::WriteU32LE(bytes, 12, 1);
		VegetationTest::RepairChunkHeaderCrc(bytes); CheckChunkRejected(bytes);
	}
	SUBCASE("palette reserved")
	{
		auto bytes = canonical; bytes[160 + 50] = 1;
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
	}
	SUBCASE("instance reserved")
	{
		auto bytes = canonical; bytes[instance_offset + 24] = 1;
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
	}
	SUBCASE("species index cell and ordinal ranges")
	{
		auto bytes = canonical; VegetationTest::WriteU16LE(bytes, instance_offset, 1);
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
		bytes = canonical; VegetationTest::WriteU16LE(bytes, instance_offset + 2, 256);
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
		bytes = canonical; VegetationTest::WriteU16LE(bytes, instance_offset + 4, 256);
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
		bytes = canonical; VegetationTest::WriteU16LE(bytes, instance_offset + 6, 256);
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
	}
	SUBCASE("duplicate and descending total record key")
	{
		auto chunk = VegetationTest::MinimalChunk();
		auto second = chunk.instances[0]; second.candidate_ordinal = 6;
		chunk.instances.push_back(second);
		std::vector<uint8_t> bytes{}; std::string local_error{};
		REQUIRE(AshEngine::encode_vegetation_chunk(chunk, bytes, &local_error));
		const size_t first = ChunkInstanceOffset(bytes);
		VegetationTest::WriteU16LE(bytes, first + 28u + 6u, 5);
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
		REQUIRE(AshEngine::encode_vegetation_chunk(chunk, bytes, &local_error));
		VegetationTest::WriteU16LE(bytes, first + 28u + 6u, 4);
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
	}
	SUBCASE("unused species and duplicate palette")
	{
		auto chunk = VegetationTest::MinimalChunk();
		auto second_species = chunk.species[0];
		second_species.species_id = VegetationTest::SequentialId(17);
		second_species.species_asset_path = "vegetation/Phase2OtherSpecies.AshVegetation";
		chunk.species.push_back(second_species);
		auto second_instance = chunk.instances[0];
		second_instance.species_index = 1;
		second_instance.cell_x = 18;
		second_instance.candidate_ordinal = 0;
		chunk.instances.push_back(second_instance);
		std::vector<uint8_t> bytes{}; std::string local_error{};
		REQUIRE(AshEngine::encode_vegetation_chunk(chunk, bytes, &local_error));
		const size_t first_instance = ChunkInstanceOffset(bytes, 2);
		auto unused = bytes;
		VegetationTest::WriteU16LE(unused, first_instance + 28u, 0);
		VegetationTest::RepairChunkCrcs(unused); CheckChunkRejected(unused);

		const size_t second_palette = 160u + 52u + VegetationTest::ReadU16LE(bytes, 208u);
		std::copy(bytes.begin() + 160, bytes.begin() + 176,
			bytes.begin() + static_cast<std::ptrdiff_t>(second_palette));
		VegetationTest::RepairChunkCrcs(bytes); CheckChunkRejected(bytes);
	}
	SUBCASE("tail")
	{
		auto bytes = canonical; bytes.push_back(0); CheckChunkRejected(bytes);
	}

	for (size_t budget_case = 0; budget_case < 5; ++budget_case)
	{
		AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
		switch (budget_case)
		{
		case 0: budget.max_file_bytes = exact_cost.file_bytes - 1; break;
		case 1: budget.max_payload_bytes = exact_cost.payload_bytes - 1; break;
		case 2: budget.max_decoded_bytes = exact_cost.decoded_bytes - 1; break;
		case 3: budget.max_palette_records = exact_cost.palette_records - 1; break;
		case 4: budget.max_instance_records = exact_cost.instance_records - 1; break;
		}
		CheckChunkRejected(canonical, budget);
	}
	CheckChunkRejected(canonical, AshEngine::VegetationLoadBudget{});
}

TEST_CASE("Vegetation Chunk codec rejects zero identities scale and forbidden oct endpoint")
{
	const std::vector<uint8_t> canonical = VegetationTest::MinimalChunkBytes();
	SUBCASE("zero layer id")
	{
		auto bytes = canonical;
		std::fill(bytes.begin() + 16, bytes.begin() + 32, 0);
		VegetationTest::RepairChunkHeaderCrc(bytes);
		CheckChunkRejected(bytes);
	}
	SUBCASE("zero input digest")
	{
		auto bytes = canonical;
		std::fill(bytes.begin() + 32, bytes.begin() + 64, 0);
		VegetationTest::RepairChunkHeaderCrc(bytes);
		CheckChunkRejected(bytes);
	}
	SUBCASE("zero surface id")
	{
		auto bytes = canonical;
		std::fill(bytes.begin() + 80, bytes.begin() + 96, 0);
		VegetationTest::RepairChunkHeaderCrc(bytes);
		CheckChunkRejected(bytes);
	}

	const size_t path_length = VegetationTest::ReadU16LE(canonical, 160 + 48);
	const size_t instance_offset = 160 + 52 + path_length;
	SUBCASE("zero scale")
	{
		auto bytes = canonical;
		VegetationTest::WriteU16LE(bytes, instance_offset + 14, 0);
		VegetationTest::RepairChunkCrcs(bytes);
		CheckChunkRejected(bytes);
	}
	SUBCASE("negative 32768 oct endpoint")
	{
		auto bytes = canonical;
		VegetationTest::WriteU16LE(bytes, instance_offset + 16, 0x8000);
		VegetationTest::RepairChunkCrcs(bytes);
		CheckChunkRejected(bytes);
	}
}

TEST_CASE("Vegetation Chunk codec writer enforces record ordering indices cells ordinals and exact extrema")
{
	std::vector<uint8_t> bytes{};
	std::string error{};
	SUBCASE("species index")
	{
		auto chunk = VegetationTest::MinimalChunk(); chunk.instances[0].species_index = 1;
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
		CHECK(bytes.empty());
	}
	SUBCASE("candidate ordinal")
	{
		auto chunk = VegetationTest::MinimalChunk(); chunk.instances[0].candidate_ordinal = 256;
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
		CHECK(bytes.empty());
	}
	SUBCASE("loose extrema")
	{
		auto chunk = VegetationTest::MinimalChunk(); chunk.min_world_height_mm = 1249;
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
		CHECK(bytes.empty());
	}
	SUBCASE("duplicate total key")
	{
		auto chunk = VegetationTest::MinimalChunk(); chunk.instances.push_back(chunk.instances[0]);
		bytes.assign(1, 0xaa);
		CHECK_FALSE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
		CHECK(bytes.empty());
	}
}

TEST_CASE("Vegetation Chunk codec standalone species ceiling accepts 65534 and rejects 65535")
{
	AshEngine::VegetationChunk chunk = VegetationTest::MinimalChunk();
	chunk.species.clear();
	chunk.instances.clear();
	chunk.min_world_height_mm = 0;
	chunk.max_world_height_mm = 0;
	chunk.species.reserve(65534);
	chunk.instances.reserve(65534);
	for (uint32_t index = 0; index < 65534; ++index)
	{
		AshEngine::VegetationPaletteEntry entry{};
		entry.species_id[0] = static_cast<uint8_t>(index >> 8);
		entry.species_id[1] = static_cast<uint8_t>(index);
		entry.species_id[15] = 1;
		entry.species_sha256.fill(0x5a);
		entry.species_sha256[0] = static_cast<uint8_t>(index >> 8);
		entry.species_sha256[1] = static_cast<uint8_t>(index);
		entry.species_asset_path = "vegetation/species/" + std::to_string(index) + ".AshVegetation";
		chunk.species.push_back(entry);
		AshEngine::VegetationChunkInstance instance{};
		instance.species_index = static_cast<uint16_t>(index);
		instance.candidate_ordinal = 0;
		instance.scale_q12 = 4096;
		chunk.instances.push_back(instance);
	}
	std::vector<uint8_t> bytes{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
	AshEngine::VegetationChunk decoded{};
	REQUIRE(AshEngine::decode_vegetation_chunk(
		bytes, VegetationTest::GenerousLoadBudget(), decoded, &error));
	CHECK(decoded.species.size() == 65534);

	chunk.species.push_back(chunk.species.back());
	CHECK_FALSE(AshEngine::encode_vegetation_chunk(chunk, bytes, &error));
}
