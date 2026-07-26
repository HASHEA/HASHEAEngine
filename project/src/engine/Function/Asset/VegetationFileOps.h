#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationSurface.h"
#include "Function/Asset/VegetationTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace AshEngine
{
	enum class VegetationFileOpKind : uint8_t
	{
		InspectPath,
		ReadAllBytes,
		EnsureDirectoryTree,
		CreateUniqueSiblingStageFile,
		CreateUniqueStageTree,
		CreateOwnedStageFile,
		WriteBlock,
		FlushAndClose,
		PublishImmutableFromStage,
		AcquireNamedLease,
		AtomicReplace,
		CreateNewFromStage,
		RemoveOwnedStageFile,
		RemoveOwnedStageTree
	};

	enum class VegetationCreateNewStatus : uint8_t
	{
		Created,
		AlreadyExists,
		Failed
	};

	enum class VegetationFileResultStatus : uint8_t
	{
		Succeeded,
		NotFound,
		InvalidPath,
		LimitExceeded,
		Failed
	};

	struct VegetationFileIdentity
	{
		// Captured from the same stable Windows handle that established the
		// corresponding successful inspection or stage-writer capability.
		bool available = false;
		uint64_t volume_serial_number = 0;
		uint64_t file_index = 0;
	};

	struct VegetationFileInspection
	{
		VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
		std::filesystem::path canonical_relative_path{};
		std::filesystem::path resolved_absolute_path{};
		std::string canonical_identity{};
		VegetationFileIdentity file_identity{};
		bool exists = false;
		bool is_regular_file = false;
		std::string error{};
	};

	struct VegetationFileBytesResult
	{
		VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
		std::vector<uint8_t> bytes{};
		std::string error{};
	};

	class IVegetationStageFileWriter;

	struct VegetationStageFileResult
	{
		// Succeeded carries one path, one available identity and one writer.
		// Every non-success status clears all three payloads.
		VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
		std::filesystem::path owned_stage_file{};
		VegetationFileIdentity file_identity{};
		std::unique_ptr<IVegetationStageFileWriter> writer{};
		std::string error{};
	};

	struct VegetationStageTreeResult
	{
		// Succeeded carries one root and the identity captured from the stable
		// creation handle. Every non-success status clears both payloads.
		VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
		std::filesystem::path owned_stage_root{};
		VegetationFileIdentity file_identity{};
		std::string error{};
	};

	class ASH_API IVegetationFileLease
	{
	public:
		virtual ~IVegetationFileLease() = default;
	};

	enum class VegetationFileLeaseStatus : uint8_t
	{
		Acquired,
		Cancelled,
		TimedOut,
		Failed
	};

	struct VegetationFileLeaseResult
	{
		VegetationFileLeaseStatus status = VegetationFileLeaseStatus::Failed;
		std::unique_ptr<IVegetationFileLease> lease{};
		std::string error{};
	};

	struct VegetationByteSpan
	{
		const uint8_t* data = nullptr;
		size_t size = 0;
	};

	enum class VegetationAtomicReplaceStatus : uint8_t
	{
		Replaced,
		TargetPreserved,
		RecoveryRequired
	};

	enum class VegetationStageFilePublishResolution : uint8_t
	{
		TargetPreserved,
		Consumed,
		RecoveryRequired
	};

	// Pure production policy for the documented ReplaceFileW partial-failure
	// outcomes. Keeping this decision separate makes every probe state mechanically
	// testable without destructive filesystem fault injection.
	enum class VegetationReplacePathState : uint8_t
	{
		PresentRegular,
		Missing,
		ProbeFailed,
		Invalid
	};

	enum class VegetationFailedReplaceAction : uint8_t
	{
		TargetPreserved,
		RestoreBackup,
		RetainBackup,
		RetainStage
	};

	ASH_API VegetationFailedReplaceAction select_vegetation_failed_replace_action(
		uint32_t replace_error,
		VegetationReplacePathState target_state,
		VegetationReplacePathState backup_state) noexcept;

	struct VegetationAtomicReplaceResult
	{
		VegetationAtomicReplaceStatus status =
			VegetationAtomicReplaceStatus::RecoveryRequired;
		std::filesystem::path recovery_path{};
		std::string error{};
	};

	class ASH_API IVegetationStageFileWriter
	{
	public:
		virtual ~IVegetationStageFileWriter() = default;
		virtual bool WriteBlock(uint64_t offset, VegetationByteSpan bytes) = 0;
		virtual bool FlushAndClose() = 0;
	};

	class ASH_API IVegetationStageFileOps
	{
	public:
		virtual ~IVegetationStageFileOps() = default;
		virtual VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) = 0;
		virtual VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			uint64_t max_bytes) = 0;
		virtual VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) = 0;
		virtual VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			uint64_t operation_serial) = 0;
		virtual VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			uint64_t operation_serial) = 0;
		virtual VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) = 0;
		virtual bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const VegetationFileIdentity& expected_identity) = 0;
		virtual bool RemoveOwnedStageTree(
			const std::filesystem::path& stage_root,
			const VegetationFileIdentity& expected_identity) = 0;
	};

	class ASH_API IVegetationImmutablePublishFileOps :
		public virtual IVegetationStageFileOps
	{
	public:
		virtual VegetationCreateNewStatus PublishImmutableFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& content_addressed_target) = 0;
	};

	class VegetationOwnedStageCleanupRegistry;

	class ASH_API IVegetationCommitFileOps :
		public virtual IVegetationStageFileOps
	{
	public:
		virtual VegetationFileLeaseResult AcquireNamedLease(
			std::string_view canonical_identity,
			const VegetationOperationControl& control) = 0;
		virtual VegetationAtomicReplaceResult AtomicReplace(
			const std::filesystem::path& stage,
			const std::filesystem::path& target,
			VegetationOwnedStageCleanupRegistry& cleanup_registry) = 0;
		virtual VegetationCreateNewStatus CreateNewFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& target) = 0;
	};

	class ASH_API IVegetationFileOps :
		public IVegetationImmutablePublishFileOps,
		public IVegetationCommitFileOps
	{
	public:
		~IVegetationFileOps() override = default;
	};

	ASH_API IVegetationFileOps& get_default_vegetation_file_ops();

	struct VegetationOwnedStageCleanupStatus
	{
		bool all_removed = true;
		std::vector<std::filesystem::path> retained_stage_files{};
		std::vector<std::filesystem::path> retained_recovery_stage_files{};
		std::vector<std::filesystem::path> retained_stage_trees{};
	};

	class VegetationOwnedStageCleanupRegistry
	{
	public:
		ASH_API VegetationOwnedStageCleanupRegistry();
		ASH_API ~VegetationOwnedStageCleanupRegistry();

		VegetationOwnedStageCleanupRegistry(
			const VegetationOwnedStageCleanupRegistry&) = delete;
		VegetationOwnedStageCleanupRegistry& operator=(
			const VegetationOwnedStageCleanupRegistry&) = delete;

		ASH_API bool TrackStageFile(
			std::filesystem::path owned_stage_file,
			VegetationFileIdentity expected_identity);
		ASH_API bool TrackStageTree(
			std::filesystem::path owned_stage_root,
			VegetationFileIdentity expected_identity);
		ASH_API bool CleanupStageFile(
			const std::filesystem::path& owned_stage_file,
			IVegetationStageFileOps& file_ops);
		ASH_API bool CleanupStageTree(
			const std::filesystem::path& owned_stage_root,
			IVegetationStageFileOps& file_ops);
		ASH_API bool OwnsStageFile(
			const std::filesystem::path& owned_stage_file) const noexcept;
		ASH_API bool OwnsStageTree(
			const std::filesystem::path& owned_stage_root) const noexcept;
		// Pins one exact, normally owned stage before a publication call. Cleanup
		// cannot consume the path until ResolveStageFilePublish atomically records
		// the publication outcome. The source/target pair is retained as the
		// provenance for any recovery artifact produced by that exact publication.
		ASH_API bool BeginStageFilePublish(
			const std::filesystem::path& owned_stage_file,
			const std::filesystem::path& target) noexcept;
		ASH_API bool ResolveStageFilePublish(
			const std::filesystem::path& owned_stage_file,
			VegetationStageFilePublishResolution resolution) noexcept;
		// Recovery files are exact owned stage paths that ordinary targeted cleanup
		// and RetryAll must retain until an explicit recovery decision releases them.
		// An unresolved publish pin is also a fail-closed recovery state: AtomicReplace
		// may surface that exact path if its terminal registry transition cannot be
		// confirmed, after which the caller can explicitly release it through the same
		// recovery API. Legitimate callers never release a pin while AtomicReplace runs.
		// Registers a newly created, independently verified stage directly as Recovery.
		// Unlike RetainStageFileForRecovery, this is insert-only and cannot take over an
		// exact path already owned by another operation.
		ASH_API bool TrackNewRecoveryStageFile(
			std::filesystem::path owned_stage_file,
			VegetationFileIdentity expected_identity);
		ASH_API bool RetainStageFileForRecovery(
			std::filesystem::path owned_stage_file);
		// Reserves one legal sibling ReplaceFile backup for the currently pinned
		// source/target operation. This association is the only authority consumers
		// may use to accept an AtomicReplace recovery result. The recovery query also
		// requires the native identity from the consumer's stable inspection to match
		// the identity captured when this reservation was created.
		ASH_API bool RetainStageFileForAtomicReplaceRecovery(
			std::filesystem::path recovery_stage_file,
			const std::filesystem::path& source_stage_file,
			const std::filesystem::path& target,
			VegetationFileIdentity expected_identity);
		ASH_API bool ReleaseRecoveryStageFile(
			const std::filesystem::path& owned_stage_file);
		ASH_API bool IsRecoveryStageFile(
			const std::filesystem::path& owned_stage_file) const noexcept;
		ASH_API bool IsAtomicReplaceRecoveryStageFile(
			const std::filesystem::path& recovery_stage_file,
			const std::filesystem::path& source_stage_file,
			const std::filesystem::path& target,
			const VegetationFileIdentity& inspected_recovery_identity) const noexcept;
		// Publish already consumed the filesystem object. Forget is deliberately
		// idempotent so a concurrent exact cleanup cannot turn a committed publish
		// into a reported failure merely because the registry entry is already gone.
		ASH_API bool ForgetConsumedStageFile(
			const std::filesystem::path& owned_stage_file);
		// A consumer that has independently proved an exact path is missing or now
		// names a different native object must drop cleanup authority without
		// deleting by path. The captured identity prevents another operation from
		// abandoning an unrelated registry entry.
		ASH_API bool AbandonStageFileOwnershipAfterIdentityDrift(
			const std::filesystem::path& owned_stage_file,
			const VegetationFileIdentity& expected_identity) noexcept;
		// A synchronous publish may consume the filesystem object before its registry
		// terminal transition is confirmed. This reconciliation first proves the exact
		// path is absent through FileOps, then may erase an Owned/Publishing entry. It
		// never erases an explicit Recovery entry or a still-existing stage.
		ASH_API bool ReconcileConsumedStageFileAfterPublish(
			const std::filesystem::path& owned_stage_file,
			IVegetationStageFileOps& file_ops);
		ASH_API bool ForgetConsumedStageTree(
			const std::filesystem::path& owned_stage_root);
		ASH_API VegetationOwnedStageCleanupStatus RetryAll(
			IVegetationStageFileOps& file_ops);
		ASH_API bool empty() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl{};
	};
}
