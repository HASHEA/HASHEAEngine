#pragma once

#include "Core/TerrainEditorSessionCore.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainContainer.h"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace AshEngine
{
	struct TerrainAssetSnapshot;
}

namespace AshEditor
{
	class IEditorCommandExecutor;

	enum class TerrainFileOperationKind : uint8_t
	{
		None = 0,
		Save,
		SaveAs,
		Optimize,
		Create,
		Import,
		Export
	};

	enum class TerrainFileOperationStatus : uint8_t
	{
		Idle = 0,
		AwaitingPublication,
		Running,
		PublishedAwaitingCatalog,
		Succeeded,
		Failed,
		Cancelled
	};

	enum class TerrainAssetReferenceMatch : uint8_t
	{
		Current = 0,
		Different,
		Unsafe
	};

	enum class TerrainExternalChangeStatus : uint8_t
	{
		None = 0,
		Reloading,
		Conflict,
		RecoveredReadOnly,
		Failed
	};

	struct TerrainExternalChangeState
	{
		TerrainExternalChangeStatus status = TerrainExternalChangeStatus::None;
		uint64_t serial = 0u;
		uint64_t local_generation = 0u;
		uint64_t disk_generation = 0u;
		std::string diagnostic{};
		bool read_only = false;
		bool can_repair = false;
		bool can_save_as = false;
	};

	struct TerrainFileOperationState
	{
		TerrainFileOperationKind kind = TerrainFileOperationKind::None;
		TerrainFileOperationStatus status = TerrainFileOperationStatus::Idle;
		uint64_t operation_serial = 0u;
		AshEngine::TerrainAssetId asset_id = 0u;
		uint64_t content_generation = 0u;
		std::filesystem::path path{};
		std::string error{};
		std::vector<std::string> warnings{};
	};

	struct TerrainFileJobResult
	{
		bool succeeded = false;
		std::string error{};
		std::optional<std::filesystem::file_time_type> source_write_time{};
		std::optional<AshEngine::TerrainContainerRevision> committed_revision{};
		bool source_changed = false;
		bool cancelled = false;
		std::vector<std::string> warnings{};
	};

	class TerrainEditorService final
	{
	public:
#if defined(ASH_TESTS)
		enum class FileJobTestPoint : uint8_t
		{
			BeforeCodec = 0,
			AfterPublishClaim
		};
		using FileJobTestHook = std::function<void(
			TerrainFileOperationKind,
			FileJobTestPoint)>;
		void SetFileJobTestHook(FileJobTestHook hook);
#endif

		bool Initialize(IEditorCommandExecutor& refCommands);
		bool Initialize(AshEngine::AssetDatabase& refAssets, IEditorCommandExecutor& refCommands);
		void Shutdown();
		void Update();
		bool SubmitIntent(const TerrainEditorIntent& refIntent);
		bool OpenSnapshotForAuthoring(const AshEngine::TerrainAssetSnapshot& refSnapshot);
		bool ApplyStrokePatches(
			AshEngine::TerrainAssetId assetId,
			AshEngine::TerrainLayerId layerId,
			const std::vector<AshEngine::TerrainEditPatch>& refPatches,
			AshEngine::TerrainEditPatchDirection eDirection,
			uint64_t sequence);
		bool ApplyLayerStackPatch(
			AshEngine::TerrainAssetId assetId,
			const AshEngine::TerrainLayerStackPatch& refPatch,
			AshEngine::TerrainEditPatchDirection eDirection,
			AshEngine::TerrainLayerId selectedLayerId,
			uint64_t sequence);

		const TerrainEditorPreviewState& GetPreviewState() const;
		bool SetViewportPreview(
			AshEngine::TerrainAssetId assetId,
			const TerrainViewportPreviewState& refPreview);
		void ClearViewportPreview();
		const TerrainAuthoringConfig& GetAuthoringConfig() const;
		AshEngine::TerrainAssetId GetSelectedAssetId() const;
		AshEngine::TerrainLayerId GetSelectedLayerId() const;
		const AshEngine::TerrainWorkingSet* GetWorkingSet() const;
		TerrainAssetReferenceMatch ClassifyCurrentAssetReferences(
			const std::vector<std::filesystem::path>& refReferences) const;
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& GetPublishedSnapshot() const;
		bool HasDirtyAssets() const;
		bool HasPendingComposition() const;
		bool HasBlockingOperation() const;
		bool HasFileOperationInProgress() const;
		const TerrainFileOperationState& GetFileOperationState() const;
		const TerrainExternalChangeState& GetExternalChangeState() const;
		const std::string& GetLastError() const;
		bool PrepareForSceneChange();
		void CommitSceneChange();

	private:
		enum class CancellableFileJobPhase : uint8_t
		{
			Cancellable = 0,
			Cancelled,
			Publishing,
			Completed
		};

		struct ActiveStroke
		{
			AshEngine::TerrainAssetId asset_id = 0u;
			AshEngine::TerrainLayerId layer_id{};
			uint64_t sequence = 0u;
			AshEngine::TerrainBrushParameters parameters{};
			AshEngine::TerrainBrushMetric metric{};
			std::vector<AshEngine::TerrainStrokeSample> raw_samples{};
		};

		struct PendingComposition
		{
			AshEngine::TerrainAssetId asset_id = 0u;
			uint64_t source_sequence = 0u;
			uint64_t operation_serial = 0u;
			uint64_t content_generation = 0u;
			std::vector<AshEngine::TerrainComponentCoord> dirty_components{};
		};

		struct PendingLoadRollbackState
		{
			AshEngine::TerrainAssetId previous_asset_id = 0;
			TerrainExternalChangeState external_state{};
			AshEngine::TerrainQueryStatus query_status = AshEngine::TerrainQueryStatus::Outside;
			std::shared_ptr<const AshEngine::TerrainAssetSnapshot> external_candidate{};
			std::optional<std::filesystem::file_time_type> candidate_source_write_time{};
			std::optional<AshEngine::TerrainSnapshotPublicationToken>
				candidate_expected_publication{};
			bool conflict_save_as_pending = false;
			std::string last_error{};
		};

		enum class PendingLoadReason : uint8_t
		{
			None = 0,
			Selection,
			Reload,
			ConflictReload,
			Repair
		};

		bool SubmitSelectAssetIntent(const TerrainEditorIntent& refIntent);
		bool SubmitSelectLayerIntent(const TerrainEditorIntent& refIntent);
		bool SubmitConfigureAuthoringIntent(const TerrainEditorIntent& refIntent);
		bool BeginStroke(const TerrainEditorIntent& refIntent);
		bool AddStrokeSample(const TerrainEditorIntent& refIntent);
		bool EndStroke(const TerrainEditorIntent& refIntent);
		bool CancelStroke(const TerrainEditorIntent& refIntent);
		bool SubmitLayerAction(const TerrainEditorIntent& refIntent);
		bool SubmitReloadIntent(bool repair);
		bool ResolveKeepLocalConflict();
		bool ApplyExternalCandidate(AshEngine::TerrainAssetId historyAssetId);
		void FailPendingSelection(std::string error);
		bool BeginReload(PendingLoadReason reason);
		void PollExternalModification();
		void CaptureObservedSourceWriteTime();
		bool CapturePendingLoadSourceWriteTime(AshEngine::TerrainAssetId assetId);
		void ObserveLoadedSourceWriteTime(
			const std::optional<std::filesystem::file_time_type>& sourceWriteTime,
			const std::optional<AshEngine::TerrainContainerRevision>& sourceRevision = {}) noexcept;
		void SetExternalChangeState(
			TerrainExternalChangeStatus status,
			uint64_t localGeneration,
			uint64_t diskGeneration,
			std::string diagnostic,
			bool readOnly,
			bool canRepair,
			bool canSaveAs);
		void ResetExternalChangeState();
		void EnterExternalFailure(std::string error);
		bool SubmitFileOperation(const TerrainEditorIntent& refIntent);
		bool CancelFileOperation();
		bool TryStartFileOperation();
		bool TryBindPublishedFileOperation();
		void CompletePendingFileOperation();
		void FailFileOperation(std::string error);
		bool ResolveFileOperationPath(
			const TerrainEditorIntent& refIntent,
			std::filesystem::path& outPath,
			std::string& outError) const;
		void ScheduleComposition(
			uint64_t sourceSequence,
			std::vector<AshEngine::TerrainComponentCoord> dirtyComponents) noexcept;
		bool RollBackStroke(
			const ActiveStroke& refStroke,
			const std::vector<AshEngine::TerrainEditPatch>& refPatches);
		bool RollBackLayerAction(
			AshEngine::TerrainAssetId assetId,
			const AshEngine::TerrainLayerStackPatch& refPatch,
			AshEngine::TerrainLayerId selectedLayerId,
			uint64_t sequence);
		void CompletePendingLoad();
		void CompletePendingComposition();
		void SyncAuthoringLayerSelection();

	private:
		AshEngine::AssetDatabase* _pAssets = nullptr;
		IEditorCommandExecutor* _pCommands = nullptr;
		TerrainEditorSessionCore _core{};
		TerrainAuthoringConfig _authoringConfig{};
		std::shared_future<std::shared_ptr<const AshEngine::TerrainAssetSnapshot>> _pendingLoad{};
		AshEngine::TerrainAssetId _pendingLoadAssetId = 0;
		PendingLoadReason _pendingLoadReason = PendingLoadReason::None;
		std::optional<std::filesystem::file_time_type> _pendingLoadSourceWriteTime{};
		std::optional<AshEngine::TerrainSnapshotPublicationToken>
			_pendingLoadExpectedPublication{};
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> _externalCandidate{};
		std::optional<std::filesystem::file_time_type> _externalCandidateSourceWriteTime{};
		std::optional<AshEngine::TerrainSnapshotPublicationToken>
			_externalCandidateExpectedPublication{};
		std::optional<PendingLoadRollbackState> _pendingLoadRollback{};
		std::optional<ActiveStroke> _optActiveStroke{};
		std::optional<PendingComposition> _optPendingComposition{};
		std::shared_future<TerrainFileJobResult> _pendingFileOperation{};
		std::shared_ptr<std::atomic<CancellableFileJobPhase>>
			_pendingFileOperationPhase{};
		TerrainFileOperationState _fileOperationState{};
		std::optional<TerrainCreateAssetDesc> _pendingCreateDesc{};
		std::optional<AshEngine::TerrainHeightImportDesc> _pendingImportDesc{};
		std::optional<AshEngine::TerrainHeightExportDesc> _pendingExportDesc{};
		TerrainExternalChangeState _externalChangeState{};
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> _publishedSnapshot{};
		uint64_t _nextStrokeSequence = 0u;
		uint64_t _nextLayerSequence = 0u;
		uint64_t _nextCompositionSerial = 0u;
		uint64_t _latestCompositionSourceSequence = 0u;
		uint64_t _nextFileOperationSerial = 0u;
		uint64_t _pendingPublishedBindOperationSerial = 0u;
		AshEngine::TerrainAssetId _pendingPublishedBindAssetId = 0u;
		uint64_t _nextExternalStateSerial = 0u;
		uint64_t _nextExternalCompositionSequence = 0u;
		std::filesystem::file_time_type _observedSourceWriteTime{};
		std::optional<AshEngine::TerrainContainerRevision> _observedSourceRevision{};
		std::chrono::steady_clock::time_point _nextExternalPollTime{};
		std::chrono::steady_clock::time_point _nextPublishedCatalogRetryTime{};
		uint32_t _sourceWriteTimeErrorCount = 0u;
		bool _hasObservedSourceWriteTime = false;
		bool _conflictSaveAsPending = false;
		bool _historyRollbackFailed = false;
		std::string _strLastError{};
#if defined(ASH_TESTS)
		FileJobTestHook _fileJobTestHook{};
#endif
	};
}
