#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class VegetationDeformation : uint8_t
	{
		None = 0,
		Grass,
		Tree
	};

	struct VegetationMeshLod
	{
		std::string mesh_asset_path{};
		std::vector<std::string> material_asset_paths{};
		uint32_t screen_error_milli = 0;
	};

	struct VegetationBoundsMm
	{
		std::array<int32_t, 3> min{};
		std::array<int32_t, 3> max{};
	};

	struct VegetationPlacement
	{
		uint16_t candidates_per_cell = 0;
		uint16_t min_scale_q12 = 0;
		uint16_t max_scale_q12 = 0;
		uint16_t min_slope_milliradians = 0;
		uint16_t max_slope_milliradians = 0;
		std::array<uint8_t, 8> material_slot_min{};
		std::array<uint8_t, 8> material_slot_max{};
		bool align_to_normal = false;
	};

	struct VegetationRender
	{
		bool casts_shadow = false;
		bool two_sided = false;
		VegetationDeformation deformation = VegetationDeformation::None;
		std::string impostor_asset_path{};
		std::string chunk_hlod_asset_path{};
	};

	struct VegetationSpecies
	{
		VegetationId species_id{};
		std::string name{};
		std::vector<VegetationMeshLod> mesh_lods{};
		VegetationBoundsMm bounds_mm{};
		VegetationPlacement placement{};
		VegetationRender render{};
	};

	ASH_API bool decode_vegetation_species(
		const std::vector<uint8_t>& bytes,
		const VegetationLoadBudget& budget,
		VegetationSpecies& out_species,
		std::string* out_error,
		VegetationLoadCost* out_cost = nullptr);
	ASH_API bool encode_vegetation_species(
		const VegetationSpecies& species,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error);
}
