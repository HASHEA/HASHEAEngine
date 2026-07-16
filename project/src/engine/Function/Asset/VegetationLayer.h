#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	struct VegetationPaletteEntry
	{
		VegetationId species_id{};
		VegetationSha256 species_sha256{};
		std::string species_asset_path{};
	};

	enum class VegetationLayerPlaneKind : uint8_t
	{
		Density = 0,
		SpeciesWeight = 1
	};

	enum class VegetationLayerPlaneCodec : uint8_t
	{
		Raw = 0,
		Rle = 1
	};

	struct VegetationLayerPlane
	{
		VegetationLayerPlaneKind kind = VegetationLayerPlaneKind::Density;
		VegetationId species_id{};
		std::array<uint8_t, 1024> values{};
	};

	struct VegetationLayerTile
	{
		int64_t tile_x = 0;
		int64_t tile_z = 0;
		std::vector<VegetationLayerPlane> planes{};
	};

	struct VegetationLayerSnapshot
	{
		VegetationId layer_id{};
		uint64_t content_generation = 0;
		uint64_t layer_seed = 0;
		std::vector<VegetationPaletteEntry> palette{};
		std::vector<VegetationLayerTile> tiles{};
	};

	ASH_API bool decode_vegetation_layer(
		const std::vector<uint8_t>& bytes,
		const VegetationLoadBudget& budget,
		VegetationLayerSnapshot& out_layer,
		std::string* out_error,
		VegetationLoadCost* out_cost = nullptr);
	ASH_API bool encode_vegetation_layer(
		const VegetationLayerSnapshot& layer,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error);
}
