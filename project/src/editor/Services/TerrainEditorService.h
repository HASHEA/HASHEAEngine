#pragma once

#include "Core/TerrainEditorSessionCore.h"

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace AshEngine
{
	class AssetDatabase;
	struct TerrainAssetSnapshot;
}

namespace AshEditor
{
	class IEditorCommandExecutor;

	class TerrainEditorService final
	{
	public:
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
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& GetPublishedSnapshot() const;
		bool HasDirtyAssets() const;
		bool HasPendingComposition() const;
		bool HasBlockingOperation() const;
		const std::string& GetLastError() const;

	private:
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

		bool SubmitSelectAssetIntent(const TerrainEditorIntent& refIntent);
		bool SubmitSelectLayerIntent(const TerrainEditorIntent& refIntent);
		bool SubmitConfigureAuthoringIntent(const TerrainEditorIntent& refIntent);
		bool BeginStroke(const TerrainEditorIntent& refIntent);
		bool AddStrokeSample(const TerrainEditorIntent& refIntent);
		bool EndStroke(const TerrainEditorIntent& refIntent);
		bool CancelStroke(const TerrainEditorIntent& refIntent);
		bool SubmitLayerAction(const TerrainEditorIntent& refIntent);
		void ScheduleComposition(
			uint64_t sourceSequence,
			std::vector<AshEngine::TerrainComponentCoord> dirtyComponents);
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
		std::optional<ActiveStroke> _optActiveStroke{};
		std::optional<PendingComposition> _optPendingComposition{};
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> _publishedSnapshot{};
		uint64_t _nextStrokeSequence = 0u;
		uint64_t _nextLayerSequence = 0u;
		uint64_t _nextCompositionSerial = 0u;
		uint64_t _latestCompositionSourceSequence = 0u;
		bool _historyRollbackFailed = false;
		std::string _strLastError{};
	};
}
