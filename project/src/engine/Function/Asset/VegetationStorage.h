#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationFileOps.h"
#include "Function/Asset/VegetationLayer.h"
#include "Function/Asset/VegetationSurface.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace AshEngine
{
	struct VegetationFileRevision
	{
		uint64_t file_size = 0;
		VegetationSha256 sha256{};

		friend constexpr bool operator==(
			const VegetationFileRevision& lhs,
			const VegetationFileRevision& rhs) noexcept
		{
			return lhs.file_size == rhs.file_size && lhs.sha256 == rhs.sha256;
		}

		friend constexpr bool operator!=(
			const VegetationFileRevision& lhs,
			const VegetationFileRevision& rhs) noexcept
		{
			return !(lhs == rhs);
		}
	};

	enum class VegetationStorageStatus : uint8_t
	{
		Succeeded = 0,
		Prepared,
		SourceChanged,
		AlreadyExists,
		NotFound,
		InvalidPath,
		Cancelled,
		TimedOut,
		RecoveryRequired,
		Failed
	};

	struct VegetationLayerReadResult
	{
		VegetationStorageStatus status = VegetationStorageStatus::Failed;
		std::shared_ptr<const VegetationLayerSnapshot> snapshot{};
		VegetationFileRevision revision{};
		std::filesystem::path canonical_relative_path{};
		std::filesystem::path resolved_absolute_path{};
		std::string canonical_identity{};
		std::string error{};
	};

	enum class VegetationPreparedLayerWriteKind : uint8_t
	{
		CheckedSave = 0,
		CopyAsCreateNew
	};

	class VegetationStorageAccess;

	class VegetationPreparedLayerWrite
	{
	public:
		VegetationPreparedLayerWrite() = default;
		VegetationPreparedLayerWrite(const VegetationPreparedLayerWrite&) = delete;
		VegetationPreparedLayerWrite& operator=(const VegetationPreparedLayerWrite&) = delete;
		ASH_API VegetationPreparedLayerWrite(VegetationPreparedLayerWrite&& other) noexcept;
		VegetationPreparedLayerWrite& operator=(VegetationPreparedLayerWrite&&) = delete;

		VegetationStorageStatus status() const noexcept { return m_data.status; }
		VegetationPreparedLayerWriteKind kind() const noexcept { return m_data.kind; }
		const std::filesystem::path& asset_root() const noexcept { return m_data.asset_root; }
		const std::filesystem::path& canonical_relative_path() const noexcept
		{
			return m_data.canonical_relative_path;
		}
		const std::filesystem::path& resolved_absolute_path() const noexcept
		{
			return m_data.resolved_absolute_path;
		}
		const std::string& canonical_identity() const noexcept
		{
			return m_data.canonical_identity;
		}
		const std::filesystem::path& stage_path() const noexcept { return m_data.stage_path; }
		const std::optional<VegetationFileRevision>& expected_revision() const noexcept
		{
			return m_data.expected_revision;
		}
		const VegetationFileRevision& staged_revision() const noexcept
		{
			return m_data.staged_revision;
		}
		uint64_t operation_serial() const noexcept { return m_data.operation_serial; }
		const std::string& error() const noexcept { return m_data.error; }

	private:
		struct Data
		{
			VegetationStorageStatus status = VegetationStorageStatus::Failed;
			VegetationPreparedLayerWriteKind kind =
				VegetationPreparedLayerWriteKind::CheckedSave;
			std::filesystem::path asset_root{};
			std::filesystem::path canonical_relative_path{};
			std::filesystem::path resolved_absolute_path{};
			std::string canonical_identity{};
			std::filesystem::path stage_path{};
			std::optional<VegetationFileRevision> expected_revision{};
			VegetationFileRevision staged_revision{};
			uint64_t operation_serial = 0;
			const VegetationOwnedStageCleanupRegistry* cleanup_registry = nullptr;
			std::string error{};
		};

		Data m_data{};
		friend class VegetationStorageAccess;
	};

	struct VegetationStorageResult
	{
		VegetationStorageStatus status = VegetationStorageStatus::Failed;
		std::optional<VegetationFileRevision> resulting_revision{};
		std::filesystem::path recovery_path{};
		std::string error{};
	};

	ASH_API VegetationLayerReadResult read_vegetation_layer_snapshot(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& layer_path,
		const VegetationLoadBudget& budget,
		IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());

	ASH_API VegetationPreparedLayerWrite prepare_vegetation_layer_write(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& target,
		std::optional<VegetationFileRevision> expected_revision,
		const VegetationLayerSnapshot& snapshot,
		uint64_t operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());

	ASH_API VegetationStorageResult commit_vegetation_layer_write(
		const VegetationPreparedLayerWrite& prepared,
		uint64_t current_operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationCommitFileOps& file_ops = get_default_vegetation_file_ops());

	ASH_API VegetationPreparedLayerWrite prepare_vegetation_layer_copy_as(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& destination,
		const VegetationLayerSnapshot& snapshot,
		uint64_t operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());

	ASH_API VegetationStorageResult commit_vegetation_layer_copy_as(
		const VegetationPreparedLayerWrite& prepared,
		uint64_t current_operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationCommitFileOps& file_ops = get_default_vegetation_file_ops());
}
