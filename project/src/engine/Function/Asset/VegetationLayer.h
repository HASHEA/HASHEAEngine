#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationTypes.h"

#include <array>
#include <cstdint>
#include <memory>
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

	using VegetationPaletteRecord = VegetationPaletteEntry;

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

	enum class VegetationLayerMutationAccess : uint8_t
	{
		Editable = 0,
		ReadOnly
	};

	struct VegetationLayerPatchEntry
	{
		int64_t tile_x = 0;
		int64_t tile_z = 0;
		VegetationLayerPlaneKind plane_kind = VegetationLayerPlaneKind::Density;
		VegetationId species_id{};
		std::vector<uint8_t> before_bytes{};
		std::vector<uint8_t> after_bytes{};
	};

	struct VegetationLayerPatch
	{
		std::vector<VegetationLayerPatchEntry> entries{};
		bool has_palette_change = false;
		std::vector<VegetationPaletteEntry> before_palette{};
		std::vector<VegetationPaletteEntry> after_palette{};
	};

	struct VegetationAuthoringSpeciesDirtyEvidence
	{
		VegetationId species_id{};
		std::vector<VegetationChunkCoord> before_coords{};
		std::vector<VegetationChunkCoord> after_coords{};
	};

	struct VegetationAuthoringDirtyEvidence
	{
		uint64_t generation = 0;
		std::vector<VegetationChunkCoord> density_coords{};
		std::vector<VegetationAuthoringSpeciesDirtyEvidence> species_coords{};
	};

	enum class VegetationMutationStatus : uint8_t
	{
		Applied = 0,
		NoChange,
		Rejected
	};

	struct VegetationMutationResult
	{
		VegetationMutationStatus status = VegetationMutationStatus::Rejected;
		VegetationLayerPatch patch{};
		uint64_t new_generation = 0;
	};

	enum class VegetationPatchApplyStatus : uint8_t
	{
		Applied = 0,
		GenerationMismatch,
		GenerationExhausted,
		ReadOnly,
		InvalidPatch,
		SourceMismatch
	};

	struct VegetationBrushStroke;
	struct VegetationPaletteEdit;
	class VegetationLayerWorkingSet;

	ASH_API VegetationMutationResult apply_vegetation_brush_stroke(
		VegetationLayerWorkingSet& working_set,
		const VegetationBrushStroke& stroke);
	ASH_API VegetationMutationResult apply_vegetation_palette_edit(
		VegetationLayerWorkingSet& working_set,
		const VegetationPaletteEdit& edit);
	ASH_API VegetationPatchApplyStatus apply_vegetation_layer_patch(
		VegetationLayerWorkingSet& working_set,
		const VegetationLayerPatch& patch,
		uint64_t expected_current_generation);
	ASH_API VegetationPatchApplyStatus revert_vegetation_layer_patch(
		VegetationLayerWorkingSet& working_set,
		const VegetationLayerPatch& patch,
		uint64_t expected_current_generation);

	class ASH_API VegetationLayerWorkingSet
	{
	public:
		explicit VegetationLayerWorkingSet(
			std::shared_ptr<const VegetationLayerSnapshot> snapshot,
			VegetationLayerMutationAccess access = VegetationLayerMutationAccess::Editable);

		std::shared_ptr<const VegetationLayerSnapshot> publish_snapshot() const;
		VegetationAuthoringDirtyEvidence snapshot_bake_dirty_evidence() const;
		bool acknowledge_bake_dirty_evidence(uint64_t captured_generation);
		uint64_t content_generation() const;

	private:
		friend struct VegetationLayerMutationInternals;

		std::shared_ptr<const VegetationLayerSnapshot> m_snapshot{};
		VegetationLayerMutationAccess m_access = VegetationLayerMutationAccess::Editable;
		VegetationAuthoringDirtyEvidence m_dirty_evidence{};
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
