#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"

#include <array>
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
		InvalidData,
		// A cooperative writer currently owns the canonical commit lease. Readers
		// should debounce and retry instead of treating this as damaged metadata.
		Busy,
		// editor begin 修改原因：checked writer 必须把来源竞争与普通 I/O/内容错误分开上报。
		SourceChanged
		// editor end
	};

	struct TerrainContainerLoadReport
	{
		uint64_t loaded_generation = 0;
		bool recovered_previous_generation = false;
		// editor begin 修改原因：把被拒绝 descriptor 的 generation 与精确错误交给 Terrain 恢复工作流。
		uint64_t rejected_generation = 0;
		std::string recovery_detail{};
		// editor end
		uint32_t decoded_block_count = 0;
		// editor begin 修改原因：后续 checked writer 必须比较这次真实解码所绑定的物理 revision。
		TerrainContainerRevision source_revision{};
		// editor end
	};

	struct TerrainContainerSaveReport
	{
		uint64_t previous_generation = 0;
		uint64_t committed_generation = 0;
		uint64_t bytes_appended = 0;
		uint32_t blocks_written = 0;
		// editor begin 修改原因：保存完成结果直接携带本次 writer 实际提交的 revision。
		TerrainContainerRevision committed_revision{};
		// editor end
	};

	ASH_API auto load_terrain_container(
		const std::filesystem::path& path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainContainerLoadReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto inspect_terrain_container_revision(
		const std::filesystem::path& path,
		TerrainContainerRevision& out_revision,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	// Atomically publishes a validated staged container to a destination that must not exist.
	// The destination uses the same cooperative named commit lease as Save and Optimize.
	ASH_API auto publish_staged_terrain_container_new(
		const std::filesystem::path& destination,
		const std::filesystem::path& staged_path,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto save_terrain_container_incremental(
		const std::filesystem::path& path,
		const TerrainAssetSnapshot& snapshot,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		TerrainContainerSaveReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	// `expected_revision` 只保护遵守本 API/named commit lease 合同的 writer；它不声称能对
	// 任意绕过 lease 的外部程序提供绝对文件系统 CAS。
	ASH_API auto save_terrain_container_incremental(
		const std::filesystem::path& path,
		const TerrainAssetSnapshot& snapshot,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		const TerrainContainerRevision* expected_revision,
		TerrainContainerSaveReport* out_report,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto optimize_terrain_container(
		const std::filesystem::path& path,
		TerrainContainerSaveReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	ASH_API auto optimize_terrain_container(
		const std::filesystem::path& path,
		const TerrainContainerRevision* expected_revision,
		TerrainContainerSaveReport* out_report,
		std::string* out_error = nullptr) -> TerrainContainerResult;

	namespace TerrainContainerInternal
	{
		// Narrow final-commit seam shared by Optimize and deterministic contract tests.
		// Callers outside TerrainContainer should use optimize_terrain_container instead.
		ASH_API auto commit_staged_terrain_container_optimization(
			const std::filesystem::path& path,
			const std::filesystem::path& staged_path,
			const TerrainContainerRevision& expected_source_revision,
			const TerrainContainerRevision& expected_staged_revision,
			std::string* out_error = nullptr) -> TerrainContainerResult;
	}
}
