#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class TerrainContainerResult : uint8_t
	{
		Success = 0,
		RecoveredPreviousGeneration,
		NotFound,
		UnsupportedVersion,
		Corrupt,
		IoFailure,
		InvalidData
	};

	struct TerrainContainerLoadReport
	{
		uint64_t loaded_generation = 0;
		bool recovered_previous_generation = false;
		uint32_t decoded_block_count = 0;
	};

	struct TerrainContainerSaveReport
	{
		uint64_t previous_generation = 0;
		uint64_t committed_generation = 0;
		uint64_t bytes_appended = 0;
		uint32_t blocks_written = 0;
	};

	ASH_API auto load_terrain_container(
		const std::filesystem::path& path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainContainerLoadReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto save_terrain_container_incremental(
		const std::filesystem::path& path,
		const TerrainAssetSnapshot& snapshot,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		TerrainContainerSaveReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto optimize_terrain_container(
		const std::filesystem::path& path,
		TerrainContainerSaveReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;
}
