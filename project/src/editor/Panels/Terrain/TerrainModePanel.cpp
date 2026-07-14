#include "Panels/Terrain/TerrainModePanel.h"

#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/Terrain/TerrainModeWidgets.h"
#include "Services/AssetDatabaseService.h"
#include "Services/TerrainEditorService.h"

namespace AshEditor
{
	namespace
	{
		constexpr char kTerrainExternalChangePopupId[] =
			"Terrain External Modification##terrain_external_change";

		void DrawTerrainExternalChangeModal(
			AshEngine::UIContext& refUi,
			TerrainEditorService& refService,
			TerrainModeState& refState)
		{
			const TerrainExternalChangeState& external = refService.GetExternalChangeState();
			const TerrainFileOperationState& fileOperation = refService.GetFileOperationState();
			if (refState.conflict_save_as_pending &&
				fileOperation.status == TerrainFileOperationStatus::Failed)
			{
				refState.conflict_save_as_pending = false;
			}
			if (refState.conflict_save_as_pending && external.status != TerrainExternalChangeStatus::Conflict)
			{
				if (refUi.begin_popup_modal(
						kTerrainExternalChangePopupId,
						nullptr,
						AshEngine::UIWindowFlagBits::AlwaysAutoResize))
				{
					refUi.close_current_popup();
					refUi.end_popup();
				}
				refState.conflict_save_as_pending = false;
				return;
			}
			if (external.status != TerrainExternalChangeStatus::Conflict)
			{
				return;
			}
			if (external.serial != 0u &&
				refState.last_external_change_serial != external.serial)
			{
				refState.last_external_change_serial = external.serial;
				refUi.open_popup(kTerrainExternalChangePopupId);
			}
			if (!refUi.begin_popup_modal(
					kTerrainExternalChangePopupId,
					nullptr,
					AshEngine::UIWindowFlagBits::AlwaysAutoResize))
			{
				return;
			}

			refUi.text_unformatted("Terrain changed on disk while local edits are unsaved.");
			refUi.text(
				"Local content generation: %llu",
				static_cast<unsigned long long>(external.local_generation));
			refUi.text(
				"Disk content generation: %llu",
				static_cast<unsigned long long>(external.disk_generation));
			if (!external.diagnostic.empty())
			{
				refUi.text_wrapped("%s", external.diagnostic.c_str());
			}
			refUi.separator();

			const bool fileOperationInProgress = refService.HasFileOperationInProgress();
			refUi.begin_disabled(fileOperationInProgress);
			if (refUi.button("Reload / Discard Local"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::Reload;
				if (refService.SubmitIntent(intent))
				{
					refUi.close_current_popup();
				}
			}
			refUi.same_line();
			if (refUi.button("Keep Local"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::KeepLocal;
				if (refService.SubmitIntent(intent))
				{
					refUi.close_current_popup();
				}
			}
			refUi.end_disabled();

			refUi.input_text("Save local copy as", refState.save_as_asset_path);
			const bool canSaveAs = external.can_save_as &&
				!refState.save_as_asset_path.empty() && !fileOperationInProgress;
			refUi.begin_disabled(!canSaveAs);
			if (refUi.button("Save Local Copy As"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::SaveAs;
				intent.asset_path = refState.save_as_asset_path;
				// Save As completes asynchronously. Keep the conflict modal open so an
				// I/O failure leaves Reload, Keep Local, and retry actions available.
				if (refService.SubmitIntent(intent))
				{
					refState.conflict_save_as_pending = true;
				}
			}
			refUi.end_disabled();
			refUi.end_popup();
		}
	}

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
		view.p_file_operation_state = &_pTerrainEditorService->GetFileOperationState();
		view.p_external_change_state = &_pTerrainEditorService->GetExternalChangeState();
		view.last_error = _pTerrainEditorService->GetLastError();

		TerrainModeDrawResult result = DrawTerrainModeTabs(refUi, view, _state);
		for (const TerrainEditorIntent& refIntent : result.intents)
		{
			_pTerrainEditorService->SubmitIntent(refIntent);
		}
		DrawTerrainExternalChangeModal(refUi, *_pTerrainEditorService, _state);
		EndPanelWindow(refFrameContext);
	}
}
