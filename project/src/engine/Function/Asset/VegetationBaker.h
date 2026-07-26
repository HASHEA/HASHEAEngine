#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationChunk.h"
#include "Function/Asset/VegetationChunkSet.h"
#include "Function/Asset/VegetationLayer.h"
#include "Function/Asset/VegetationSpecies.h"
#include "Function/Asset/VegetationSurface.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace AshEngine
{
	struct VegetationCounterHashKey
	{
		VegetationId layer_id{};
		VegetationChunkCoord chunk{};
		uint16_t cell_x = 0;
		uint16_t cell_z = 0;
		VegetationId species_id{};
		uint64_t layer_seed = 0;
		uint16_t candidate_ordinal = 0;
	};

	struct VegetationCounterHashResult
	{
		uint64_t state = 0;
		std::array<uint64_t, 5> random{};
	};

	struct VegetationChunkInputTileRecord
	{
		bool present = false;
		std::vector<uint8_t> canonical_record{};
	};

	struct VegetationChunkInputIdentity
	{
		uint32_t cooker_version = 1;
		VegetationId layer_id{};
		uint64_t layer_seed = 0;
		VegetationChunkCoord chunk{};
		VegetationSurfaceIdentity surface_identity{};
		std::array<VegetationChunkInputTileRecord, 64> logical_tiles{};
		std::vector<VegetationPaletteEntry> used_species{};
	};

	struct VegetationBakeInput
	{
		uint32_t cooker_version = 1;
		uint64_t operation_serial = 0;
		std::shared_ptr<const VegetationLayerSnapshot> layer_snapshot{};
		std::vector<std::shared_ptr<const VegetationSpecies>> species_snapshots{};
		std::shared_ptr<const IVegetationSurfaceSnapshot> surface_snapshot{};
		VegetationChunkSetSourceActiveIdentity source_active_identity{};
		std::shared_ptr<const VegetationActiveChunkSetSnapshot> active_chunk_set{};
		VegetationAuthoringDirtyEvidence dirty_evidence{};
	};

	enum class VegetationBakeStatus : uint8_t
	{
		Succeeded = 0,
		Cancelled,
		TimedOut,
		Failed
	};

	struct VegetationBakedChunk
	{
		VegetationChunkCoord coord{};
		VegetationSha256 input_digest{};
		VegetationSha256 object_sha256{};
		VegetationChunk chunk{};
		std::vector<uint8_t> object_bytes{};
	};

	struct VegetationBakeTransactionOutput
	{
		std::vector<VegetationBakedChunk> chunks{};
		std::vector<VegetationChunkCoord> removed_coords{};
		bool full_rebake_required = false;
		VegetationChunkSetManifest resulting_manifest{};
		VegetationChunkSetSourceActiveIdentity source_active_identity{};
		VegetationChunkSetExpectedIdentity expected_identity{};
	};

	struct VegetationBakeResult
	{
		VegetationBakeStatus status = VegetationBakeStatus::Failed;
		std::optional<VegetationBakeTransactionOutput> transaction{};
		std::string error{};
	};

	ASH_API VegetationCounterHashResult make_vegetation_counter_hash(
		const VegetationCounterHashKey& key,
		uint32_t cooker_version);
	ASH_API uint32_t vegetation_candidate_accept_limit(uint8_t effective_threshold);
	ASH_API VegetationSha256 build_vegetation_chunk_input_digest(
		const VegetationChunkInputIdentity& input,
		std::vector<uint8_t>* out_preimage);
	ASH_API VegetationBakeResult bake_vegetation_chunks(
		const VegetationBakeInput& input,
		VegetationOperationControl control);
}
