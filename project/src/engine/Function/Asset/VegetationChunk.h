#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationLayer.h"
#include "Function/Asset/VegetationSurface.h"
#include "Function/Asset/VegetationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	struct VegetationChunkInstance
	{
		uint16_t species_index = 0;
		uint16_t cell_x = 0;
		uint16_t cell_z = 0;
		uint16_t candidate_ordinal = 0;
		uint16_t cell_fraction_x_u16 = 0;
		uint16_t cell_fraction_z_u16 = 0;
		uint16_t yaw_turn_u16 = 0;
		uint16_t scale_q12 = 0;
		int16_t normal_oct_x = 0;
		int16_t normal_oct_y = 0;
		int32_t world_height_mm = 0;
	};

	struct VegetationChunk
	{
		uint32_t cooker_version = 1;
		VegetationId layer_id{};
		VegetationSha256 chunk_input_sha256{};
		VegetationChunkCoord chunk{};
		VegetationSurfaceIdentity surface_identity{};
		std::vector<VegetationPaletteEntry> species{};
		std::vector<VegetationChunkInstance> instances{};
		int32_t min_world_height_mm = 0;
		int32_t max_world_height_mm = 0;
	};

	ASH_API bool decode_vegetation_chunk(
		const std::vector<uint8_t>& bytes,
		const VegetationLoadBudget& budget,
		VegetationChunk& out_chunk,
		std::string* out_error,
		VegetationLoadCost* out_cost = nullptr);
	ASH_API bool encode_vegetation_chunk(
		const VegetationChunk& chunk,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error);
}
