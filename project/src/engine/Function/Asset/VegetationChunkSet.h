#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationFileOps.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	class VegetationAssetResolverSnapshot;
	struct VegetationBakeResult;

	struct VegetationChunkSetManifestEntry
	{
		VegetationChunkCoord coord{};
		VegetationSha256 object_sha256{};
		VegetationSha256 input_sha256{};
	};

	struct VegetationChunkSetManifest
	{
		VegetationId layer_id{};
		uint64_t layer_generation = 0;
		VegetationSurfaceIdentity surface_identity{};
		std::vector<VegetationChunkSetManifestEntry> entries{};
	};

	struct VegetationChunkSetActivePointer
	{
		VegetationSha256 manifest_sha256{};
	};

	struct VegetationChunkSetLoadBudget
	{
		VegetationLoadBudget per_file{};
		uint32_t max_manifest_entries = 0;
		uint64_t max_total_inspected_bytes = 0;
		uint64_t max_summary_bytes = 0;
	};

	struct VegetationActiveChunkSetEntrySummary
	{
		VegetationChunkCoord coord{};
		VegetationSha256 object_sha256{};
		VegetationSha256 input_sha256{};
		std::vector<VegetationId> referenced_species_ids{};
	};

	struct VegetationActiveChunkSetSnapshot
	{
		VegetationId layer_id{};
		uint64_t layer_generation = 0;
		VegetationSurfaceIdentity surface_identity{};
		VegetationSha256 manifest_sha256{};
		std::vector<VegetationActiveChunkSetEntrySummary> entries{};
	};

	enum class VegetationActiveChunkSetReadStatus : uint8_t
	{
		Succeeded = 0,
		NoActive,
		Invalid,
		Cancelled,
		TimedOut,
		Failed
	};

	struct VegetationActiveChunkSetReadResult
	{
		VegetationActiveChunkSetReadStatus status =
			VegetationActiveChunkSetReadStatus::Failed;
		std::shared_ptr<const VegetationActiveChunkSetSnapshot> snapshot{};
		std::filesystem::path store_relative_path{};
		std::filesystem::path active_relative_path{};
		std::string error{};
	};

	enum class VegetationChunkSetSourceActiveState : uint8_t
	{
		Invalid = 0,
		NoActive,
		Existing
	};

	struct VegetationChunkSetSourceActiveIdentity
	{
		VegetationChunkSetSourceActiveState state =
			VegetationChunkSetSourceActiveState::Invalid;
		VegetationSha256 manifest_sha256{};

		friend bool operator==(
			const VegetationChunkSetSourceActiveIdentity& lhs,
			const VegetationChunkSetSourceActiveIdentity& rhs)
		{
			return lhs.state == rhs.state &&
				lhs.manifest_sha256 == rhs.manifest_sha256;
		}

		friend bool operator!=(
			const VegetationChunkSetSourceActiveIdentity& lhs,
			const VegetationChunkSetSourceActiveIdentity& rhs)
		{
			return !(lhs == rhs);
		}
	};

	struct VegetationChunkSetSpeciesIdentity
	{
		VegetationId species_id{};
		VegetationSha256 canonical_sha256{};

		friend bool operator==(
			const VegetationChunkSetSpeciesIdentity& lhs,
			const VegetationChunkSetSpeciesIdentity& rhs)
		{
			return lhs.species_id == rhs.species_id &&
				lhs.canonical_sha256 == rhs.canonical_sha256;
		}

		friend bool operator!=(
			const VegetationChunkSetSpeciesIdentity& lhs,
			const VegetationChunkSetSpeciesIdentity& rhs)
		{
			return !(lhs == rhs);
		}
	};

	struct VegetationChunkSetExpectedIdentity
	{
		uint64_t operation_serial = 0;
		uint32_t cooker_version = 0;
		uint32_t format_version = 1;
		VegetationId layer_id{};
		uint64_t layer_generation = 0;
		VegetationSurfaceIdentity surface_identity{};
		std::vector<VegetationChunkSetSpeciesIdentity> species_identities{};
		std::vector<VegetationChunkCoord> target_coords{};

		friend bool operator==(
			const VegetationChunkSetExpectedIdentity& lhs,
			const VegetationChunkSetExpectedIdentity& rhs)
		{
			return lhs.operation_serial == rhs.operation_serial &&
				lhs.cooker_version == rhs.cooker_version &&
				lhs.format_version == rhs.format_version &&
				lhs.layer_id == rhs.layer_id &&
				lhs.layer_generation == rhs.layer_generation &&
				lhs.surface_identity.surface_id == rhs.surface_identity.surface_id &&
				lhs.surface_identity.content_revision ==
					rhs.surface_identity.content_revision &&
				lhs.surface_identity.residency_revision ==
					rhs.surface_identity.residency_revision &&
				lhs.surface_identity.transform_revision ==
					rhs.surface_identity.transform_revision &&
				lhs.species_identities == rhs.species_identities &&
				lhs.target_coords.size() == rhs.target_coords.size() &&
				std::equal(lhs.target_coords.begin(), lhs.target_coords.end(),
					rhs.target_coords.begin(),
					[](const VegetationChunkCoord& lhs_coord,
						const VegetationChunkCoord& rhs_coord)
					{
						return lhs_coord.x == rhs_coord.x && lhs_coord.z == rhs_coord.z;
					});
		}

		friend bool operator!=(
			const VegetationChunkSetExpectedIdentity& lhs,
			const VegetationChunkSetExpectedIdentity& rhs)
		{
			return !(lhs == rhs);
		}
	};

	enum class VegetationChunkSetPrepareStatus : uint8_t
	{
		Prepared = 0,
		InvalidPath,
		Cancelled,
		TimedOut,
		RecoveryRequired,
		Failed
	};

	class VegetationChunkSetAccess;

	class VegetationPreparedChunkSet
	{
	public:
		VegetationPreparedChunkSet() = default;
		VegetationPreparedChunkSet(const VegetationPreparedChunkSet&) = delete;
		VegetationPreparedChunkSet& operator=(const VegetationPreparedChunkSet&) = delete;
		ASH_API VegetationPreparedChunkSet(VegetationPreparedChunkSet&& other) noexcept;
		VegetationPreparedChunkSet& operator=(VegetationPreparedChunkSet&&) = delete;

		VegetationChunkSetPrepareStatus status() const noexcept { return m_data.status; }
		const std::filesystem::path& asset_root() const noexcept { return m_data.asset_root; }
		const std::filesystem::path& layer_canonical_relative_path() const noexcept
		{
			return m_data.layer_canonical_relative_path;
		}
		const std::filesystem::path& layer_resolved_absolute_path() const noexcept
		{
			return m_data.layer_resolved_absolute_path;
		}
		const std::string& layer_canonical_identity() const noexcept
		{
			return m_data.layer_canonical_identity;
		}
		const std::filesystem::path& store_canonical_relative_path() const noexcept
		{
			return m_data.store_canonical_relative_path;
		}
		const std::filesystem::path& store_resolved_absolute_path() const noexcept
		{
			return m_data.store_resolved_absolute_path;
		}
		const std::string& store_canonical_identity() const noexcept
		{
			return m_data.store_canonical_identity;
		}
		const std::filesystem::path& active_canonical_relative_path() const noexcept
		{
			return m_data.active_canonical_relative_path;
		}
		const std::filesystem::path& active_resolved_absolute_path() const noexcept
		{
			return m_data.active_resolved_absolute_path;
		}
		const std::string& active_canonical_identity() const noexcept
		{
			return m_data.active_canonical_identity;
		}
		const VegetationChunkSetSourceActiveIdentity& source_active_identity() const noexcept
		{
			return m_data.source_active_identity;
		}
		const VegetationChunkSetExpectedIdentity& expected_identity() const noexcept
		{
			return m_data.expected_identity;
		}
		const VegetationSha256& manifest_sha256() const noexcept
		{
			return m_data.manifest_sha256;
		}
		const std::filesystem::path& stage_path() const noexcept { return m_data.stage_path; }
		const VegetationFileIdentity& stage_file_identity() const noexcept
		{
			return m_data.stage_file_identity;
		}
		uint64_t active_stage_size() const noexcept { return m_data.active_stage_size; }
		const VegetationSha256& active_stage_sha256() const noexcept
		{
			return m_data.active_stage_sha256;
		}
		const std::string& error() const noexcept { return m_data.error; }

	private:
		struct Data
		{
			VegetationChunkSetPrepareStatus status =
				VegetationChunkSetPrepareStatus::Failed;
			std::filesystem::path asset_root{};
			std::filesystem::path layer_canonical_relative_path{};
			std::filesystem::path layer_resolved_absolute_path{};
			std::string layer_canonical_identity{};
			VegetationFileIdentity layer_file_identity{};
			std::filesystem::path store_canonical_relative_path{};
			std::filesystem::path store_resolved_absolute_path{};
			std::string store_canonical_identity{};
			VegetationFileIdentity store_file_identity{};
			std::filesystem::path active_canonical_relative_path{};
			std::filesystem::path active_resolved_absolute_path{};
			std::string active_canonical_identity{};
			VegetationChunkSetSourceActiveIdentity source_active_identity{};
			VegetationFileIdentity source_active_file_identity{};
			VegetationChunkSetExpectedIdentity expected_identity{};
			VegetationSha256 manifest_sha256{};
			std::filesystem::path stage_path{};
			VegetationFileIdentity stage_file_identity{};
			uint64_t active_stage_size = 0;
			VegetationSha256 active_stage_sha256{};
			const VegetationOwnedStageCleanupRegistry* cleanup_registry = nullptr;
			std::string error{};
		};

		Data m_data{};
		friend class VegetationChunkSetAccess;
	};

	enum class VegetationChunkSetCommitStatus : uint8_t
	{
		Succeeded = 0,
		SourceChanged,
		AlreadyExists,
		Cancelled,
		TimedOut,
		RecoveryRequired,
		Failed
	};

	struct VegetationChunkSetCommitResult
	{
		VegetationChunkSetCommitStatus status = VegetationChunkSetCommitStatus::Failed;
		std::filesystem::path recovery_path{};
		std::string error{};
	};

	ASH_API bool encode_vegetation_chunk_set_manifest(
		const VegetationChunkSetManifest& manifest,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error);
	ASH_API bool decode_vegetation_chunk_set_manifest(
		const std::vector<uint8_t>& bytes,
		uint32_t max_entries,
		VegetationChunkSetManifest& out_manifest,
		std::string* out_error);
	ASH_API bool encode_vegetation_chunk_set_active_pointer(
		const VegetationChunkSetActivePointer& pointer,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error);
	ASH_API bool decode_vegetation_chunk_set_active_pointer(
		const std::vector<uint8_t>& bytes,
		VegetationChunkSetActivePointer& out_pointer,
		std::string* out_error);

	ASH_API VegetationActiveChunkSetReadResult read_active_vegetation_chunk_set(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& layer_path,
		const VegetationAssetResolverSnapshot& resolver,
		const VegetationChunkSetLoadBudget& budget,
		VegetationOperationControl control,
		IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());

	ASH_API VegetationPreparedChunkSet prepare_vegetation_chunk_set(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& layer_path,
		const VegetationBakeResult& bake,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationImmutablePublishFileOps& file_ops =
			get_default_vegetation_file_ops());

	ASH_API VegetationChunkSetCommitResult commit_vegetation_chunk_set(
		const VegetationPreparedChunkSet& prepared,
		const VegetationChunkSetExpectedIdentity& current_identity,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationCommitFileOps& file_ops = get_default_vegetation_file_ops());
}
