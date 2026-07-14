#include "Panels/Terrain/TerrainModePanel.h"

#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/Terrain/TerrainModeWidgets.h"
#include "Services/AssetDatabaseService.h"
#include "Services/TerrainEditorService.h"

namespace AshEditor
{
	TerrainModePanel::TerrainModePanel(
		TerrainEditorService* pTerrainEditorService,
		AssetDatabaseService* pAssetDatabaseService)
		: EditorPanel(EditorPanelIds::TerrainMode, EditorWindowTitles::TerrainMode)
		, _pTerrainEditorService(pTerrainEditorService)
		, _pAssetDatabaseService(pAssetDatabaseService)
	{
	}

	void TerrainModePanel::BindEventBus(EditorEventBus* pEventBus)
	{
		if (_eventBindings.IsBoundTo(pEventBus))
		{
			return;
		}
		_eventBindings.Bind(pEventBus);
		_eventBindings.Subscribe<EditorSelectionChangedEvent>(
			[this](const EditorSelectionChangedEvent& refEvent)
			{
				if (!_pTerrainEditorService || !_pAssetDatabaseService ||
					refEvent.vecCurrentSelections.size() != 1u ||
					refEvent.currentSelection.eKind != EditorSelectionKind::Asset)
				{
					return;
				}

				const AshEngine::AssetInfo* pAsset =
					_pAssetDatabaseService->FindById(refEvent.currentSelection.uId);
				if (!pAsset || pAsset->type != AshEngine::AssetType::Terrain)
				{
					return;
				}

				TerrainEditorIntent select{};
				select.kind = TerrainEditorIntent::Kind::SelectAsset;
				select.asset_id = pAsset->id;
				_pTerrainEditorService->SubmitIntent(select);
			});
		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent&)
			{
				_state.ResetTransientDrafts();
			});
	}

	void TerrainModePanel::OnDetach()
	{
		_eventBindings.Clear();
		_pTerrainEditorService = nullptr;
		_pAssetDatabaseService = nullptr;
		_state.ResetTransientDrafts();
	}

	void TerrainModePanel::OnGui(const EditorFrameContext& refFrameContext)
	{
		if (!BeginPanelWindow(refFrameContext))
		{
			EndPanelWindow(refFrameContext);
			return;
		}
		if (!refFrameContext.pUiContext)
		{
			EndPanelWindow(refFrameContext);
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!_pTerrainEditorService)
		{
			refUi.text_wrapped("Terrain authoring service is unavailable.");
			EndPanelWindow(refFrameContext);
			return;
		}

		TerrainModeView view{};
		view.asset_id = _pTerrainEditorService->GetSelectedAssetId();
		view.p_working_set = _pTerrainEditorService->GetWorkingSet();
		view.authoring_config = _pTerrainEditorService->GetAuthoringConfig();
		view.preview = _pTerrainEditorService->GetPreviewState();
		view.dirty = _pTerrainEditorService->HasDirtyAssets();
		view.pending_composition = _pTerrainEditorService->HasPendingComposition();
		view.blocking_operation = _pTerrainEditorService->HasBlockingOperation();
		view.last_error = _pTerrainEditorService->GetLastError();

		TerrainModeDrawResult result = DrawTerrainModeTabs(refUi, view, _state);
		for (const TerrainEditorIntent& refIntent : result.intents)
		{
			_pTerrainEditorService->SubmitIntent(refIntent);
		}
		EndPanelWindow(refFrameContext);
	}
}
