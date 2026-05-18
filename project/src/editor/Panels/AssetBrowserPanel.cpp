#include "Panels/AssetBrowserPanel.h"

#include "Base/hlog.h"
#include "Core/AssetPresentationUtils.h"
#include "Core/EditorEventBindings.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/AssetBrowserBreadcrumbsView.h"
#include "Panels/AssetBrowser/AssetBrowserContentView.h"
#include "Panels/AssetBrowser/AssetBrowserContextMenus.h"
#include "Panels/AssetBrowser/AssetBrowserDirectoryTreeView.h"
#include "Panels/AssetBrowser/AssetBrowserSupport.h"
#include "Panels/AssetBrowser/AssetBrowserToolbarView.h"
#include "Services/AssetDatabaseService.h"
#include "Services/AssetPreviewService.h"
#include "Services/CommandService.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorShortcutService.h"
#include "Services/SelectionService.h"
#include "Shell/PanelManager.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace AshEditor
{
	namespace
	{
		void ClearIfAssetSelected(SelectionService* pSelectionService)
		{
			if (pSelectionService && pSelectionService->GetSelection().eKind == EditorSelectionKind::Asset)
			{
				pSelectionService->Clear();
			}
		}
	}

	struct AssetBrowserPanel::Impl
	{
		EditorEventBus* pEventBus = nullptr;
		AssetBrowserPanelDeps deps{};
		EditorEventBindings eventBindings{};
		AssetBrowserPanelState state{};
		AssetBrowserToolbarView toolbarView{};
		AssetBrowserBreadcrumbsView breadcrumbsView{};
		AssetBrowserDirectoryTreeView directoryTreeView{};
		AssetBrowserContentView contentView{};
		AssetBrowserContextMenus contextMenus{};
	};

	AssetBrowserPanel::AssetBrowserPanel(AssetBrowserPanelDeps deps)
		: EditorPanel(EditorPanelIds::AssetBrowser, EditorWindowTitles::AssetBrowser)
		, _upImpl(std::make_unique<Impl>())
	{
		_upImpl->deps = deps;
	}

	AssetBrowserPanel::~AssetBrowserPanel() = default;

	void AssetBrowserPanel::BindEventBus(EditorEventBus* pEventBus)
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->pEventBus = pEventBus;
		if (_upImpl->eventBindings.IsBoundTo(pEventBus))
		{
			return;
		}

		_upImpl->eventBindings.Bind(pEventBus);
		if (!pEventBus)
		{
			return;
		}

		_upImpl->eventBindings.Subscribe<EditorSelectionChangedEvent>(
			[this](const EditorSelectionChangedEvent& refEvent)
			{
				if (refEvent.currentSelection.eKind == EditorSelectionKind::Asset)
				{
					_upImpl->state.uSelectedAssetId = refEvent.currentSelection.uId;
				}
				else if (refEvent.currentSelection.IsEmpty() && refEvent.previousSelection.eKind == EditorSelectionKind::Asset)
				{
					_upImpl->state.uSelectedAssetId = 0;
				}
			});
	}

	void AssetBrowserPanel::OnAttach()
	{
		if (!_upImpl)
		{
			return;
		}

		if (_upImpl->deps.pSettingsService)
		{
			const EditorSettings& settings = _upImpl->deps.pSettingsService->GetSettings();
			_upImpl->state.strSearchText = settings.strAssetBrowserSearchText;
			_upImpl->state.strActiveDirectoryPath = settings.strAssetBrowserActiveDirectory;
			_upImpl->state.iTypeFilterIndex = settings.iAssetBrowserTypeFilter;
			_upImpl->state.eViewMode = AssetBrowserSupport::ToAssetBrowserViewMode(settings.iAssetBrowserViewMode);
		}
		ResetDirectoryHistory();
		HLogInfo("AssetBrowserPanel attached.");
	}

	void AssetBrowserPanel::OnDetach()
	{
		if (!_upImpl)
		{
			return;
		}

		PublishContentShortcutScope(false);
		UnsubscribeEvents();
		_upImpl->pEventBus = nullptr;
		ClearDeps();
	}

	void AssetBrowserPanel::ClearDeps()
	{
		if (_upImpl)
		{
			_upImpl->deps = {};
		}
	}

	void AssetBrowserPanel::UnsubscribeEvents()
	{
		if (_upImpl)
		{
			_upImpl->eventBindings.Clear();
		}
	}

	bool AssetBrowserPanel::CanExecuteOpenSelected() const
	{
		return
			_upImpl &&
			_upImpl->deps.pAssetDatabaseService &&
			AssetBrowserSupport::GetSelectedAsset(*_upImpl->deps.pAssetDatabaseService, _upImpl->state.uSelectedAssetId) &&
			_upImpl->state.bSelectedAssetVisibleThisFrame;
	}

	void AssetBrowserPanel::ExecuteOpenSelected()
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		const AshEngine::AssetInfo* pSelectedAsset =
			AssetBrowserSupport::GetSelectedAsset(*_upImpl->deps.pAssetDatabaseService, _upImpl->state.uSelectedAssetId);
		if (!pSelectedAsset)
		{
			return;
		}

		OpenAssetItem(*pSelectedAsset);
	}

	bool AssetBrowserPanel::CanExecuteNavigateUp() const
	{
		return
			_upImpl &&
			_upImpl->state.bActiveDirectoryExistsThisFrame &&
			!_upImpl->state.strActiveDirectoryPath.empty();
	}

	void AssetBrowserPanel::ExecuteNavigateUp()
	{
		if (!CanExecuteNavigateUp())
		{
			return;
		}

		NavigateToDirectory(std::filesystem::path(_upImpl->state.strActiveDirectoryPath).parent_path());
	}

	void AssetBrowserPanel::DispatchContentShortcuts(
		const EditorFrameContext& refFrameContext,
		bool bContentFocused)
	{
		if (!_upImpl ||
			!bContentFocused ||
			!_upImpl->deps.pCommandService ||
			!_upImpl->deps.pShortcutService ||
			!refFrameContext.pUiContext)
		{
			return;
		}

		_upImpl->deps.pShortcutService->DispatchScope(
			*_upImpl->deps.pCommandService,
			EditorActionScope::AssetBrowserContent,
			*refFrameContext.pUiContext);
	}

	void AssetBrowserPanel::PublishContentShortcutScope(bool bContentFocused)
	{
		if (!_upImpl || _upImpl->state.bContentShortcutScopeActive == bContentFocused)
		{
			return;
		}

		_upImpl->state.bContentShortcutScopeActive = bContentFocused;
		if (!_upImpl->pEventBus)
		{
			return;
		}

		EditorShortcutScopeChangedEvent event{};
		event.eScope = bContentFocused ? EditorShortcutScope::AssetBrowserContent : EditorShortcutScope::Global;
		event.strOwnerPanelId = EditorPanelIds::AssetBrowser;
		_upImpl->pEventBus->Publish(event);
	}

	void AssetBrowserPanel::SelectAsset(const AshEngine::AssetInfo& refItem)
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.uSelectedAssetId = refItem.id;
		if (_upImpl->deps.pSelectionService)
		{
			_upImpl->deps.pSelectionService->Select({
				EditorSelectionKind::Asset,
				refItem.id,
				GetAssetDisplayLabel(refItem),
				refItem.relative_path.generic_string() });
		}
	}

	void AssetBrowserPanel::ClearAssetSelection()
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.uSelectedAssetId = 0u;
		ClearIfAssetSelected(_upImpl->deps.pSelectionService);
	}

	void AssetBrowserPanel::OpenAssetItem(const AshEngine::AssetInfo& refItem)
	{
		if (refItem.is_directory)
		{
			NavigateToDirectory(refItem.relative_path);
			return;
		}

		SelectAsset(refItem);
	}

	void AssetBrowserPanel::OpenAssetPreview(const AshEngine::AssetInfo& refItem)
	{
		if (!_upImpl)
		{
			return;
		}

		SelectAsset(refItem);
		if (_upImpl->deps.pAssetPreviewService)
		{
			_upImpl->deps.pAssetPreviewService->SetPreviewAsset({
				EditorSelectionKind::Asset,
				refItem.id,
				GetAssetDisplayLabel(refItem),
				refItem.relative_path.generic_string() });
		}

		if (!_upImpl->deps.pPanelManager)
		{
			HLogWarning(
				"AssetBrowserPanel cannot open Asset Preview because PanelManager is unavailable. Asset={}.",
				refItem.relative_path.generic_string());
			return;
		}

		_upImpl->deps.pPanelManager->SetPanelOpen(EditorPanelIds::AssetPreview, true);
	}

	void AssetBrowserPanel::NavigateToDirectory(const std::filesystem::path& refDirectoryPath)
	{
		NavigateToDirectoryInternal(refDirectoryPath, true);
	}

	void AssetBrowserPanel::NavigateToDirectoryInternal(
		const std::filesystem::path& refDirectoryPath,
		bool bRecordHistory)
	{
		if (!_upImpl)
		{
			return;
		}

		const std::string strNewPath = refDirectoryPath.generic_string();
		if (strNewPath == _upImpl->state.strActiveDirectoryPath)
		{
			return;
		}

		if (bRecordHistory)
		{
			if (_upImpl->state.iDirectoryHistoryIndex >= 0 &&
				_upImpl->state.iDirectoryHistoryIndex + 1 < static_cast<int32_t>(_upImpl->state.vecDirectoryHistory.size()))
			{
				_upImpl->state.vecDirectoryHistory.erase(
					_upImpl->state.vecDirectoryHistory.begin() + (_upImpl->state.iDirectoryHistoryIndex + 1),
					_upImpl->state.vecDirectoryHistory.end());
			}

			if (_upImpl->state.vecDirectoryHistory.empty() || _upImpl->state.vecDirectoryHistory.back() != strNewPath)
			{
				_upImpl->state.vecDirectoryHistory.push_back(strNewPath);
				_upImpl->state.iDirectoryHistoryIndex = static_cast<int32_t>(_upImpl->state.vecDirectoryHistory.size() - 1);
			}
		}

		_upImpl->state.strActiveDirectoryPath = strNewPath;
	}

	bool AssetBrowserPanel::CanNavigateDirectoryBack() const
	{
		return _upImpl && _upImpl->state.iDirectoryHistoryIndex > 0 && !_upImpl->state.vecDirectoryHistory.empty();
	}

	bool AssetBrowserPanel::CanNavigateDirectoryForward() const
	{
		if (!_upImpl)
		{
			return false;
		}

		return
			_upImpl->state.iDirectoryHistoryIndex >= 0 &&
			_upImpl->state.iDirectoryHistoryIndex + 1 < static_cast<int32_t>(_upImpl->state.vecDirectoryHistory.size());
	}

	void AssetBrowserPanel::NavigateDirectoryBack()
	{
		if (!CanNavigateDirectoryBack())
		{
			return;
		}

		--_upImpl->state.iDirectoryHistoryIndex;
		NavigateToDirectoryInternal(
			std::filesystem::path(_upImpl->state.vecDirectoryHistory[_upImpl->state.iDirectoryHistoryIndex]),
			false);
	}

	void AssetBrowserPanel::NavigateDirectoryForward()
	{
		if (!CanNavigateDirectoryForward())
		{
			return;
		}

		++_upImpl->state.iDirectoryHistoryIndex;
		NavigateToDirectoryInternal(
			std::filesystem::path(_upImpl->state.vecDirectoryHistory[_upImpl->state.iDirectoryHistoryIndex]),
			false);
	}

	void AssetBrowserPanel::ResetDirectoryHistory()
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.vecDirectoryHistory.clear();
		_upImpl->state.iDirectoryHistoryIndex = 0;
		_upImpl->state.vecDirectoryHistory.push_back(_upImpl->state.strActiveDirectoryPath);
	}

	void AssetBrowserPanel::ResetFilters()
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.strSearchText.clear();
		_upImpl->state.strActiveDirectoryPath.clear();
		_upImpl->state.iTypeFilterIndex = 0;
	}

	void AssetBrowserPanel::BrowseToAssetLocation(const AshEngine::AssetInfo& refItem)
	{
		NavigateToDirectory(refItem.is_directory ? refItem.relative_path : refItem.parent_path);
	}

	void AssetBrowserPanel::OnGui(const EditorFrameContext& frameContext)
	{
		if (!BeginPanelWindow(frameContext))
		{
			EndPanelWindow(frameContext);
			return;
		}

		_upImpl->state.bActiveDirectoryExistsThisFrame = false;
		_upImpl->state.bSelectedAssetVisibleThisFrame = false;
		if (!frameContext.pUiContext)
		{
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			refUi.text_unformatted("Asset database unavailable.");
			EndPanelWindow(frameContext);
			return;
		}

		AssetBrowserFrameData frameData = AssetBrowserSupport::BuildFrameData(_upImpl->deps, _upImpl->state);
		_upImpl->state.bActiveDirectoryExistsThisFrame = frameData.bActiveDirectoryExists;
		_upImpl->state.bSelectedAssetVisibleThisFrame = frameData.bSelectedAssetVisible;

		AssetBrowserViewContext viewContext{ frameContext, _upImpl->deps, _upImpl->state, *this };

		_upImpl->toolbarView.Draw(viewContext, frameData);
		refUi.separator();

		const AshEngine::UIVec2 vecAvailRegion = refUi.get_content_region_avail();
		const float fLeftWidth = std::max(180.0f, vecAvailRegion.x * 0.28f);

		if (refUi.begin_child("AssetBrowserDirectories", { fLeftWidth, 0.0f }, AshEngine::UIChildFlagBits::Border))
		{
			refUi.text_unformatted("Directories");
			refUi.separator();
			_upImpl->directoryTreeView.Draw(viewContext, frameData);
		}
		refUi.end_child();
		refUi.same_line();

		if (refUi.begin_child("AssetBrowserContent", {}, AshEngine::UIChildFlagBits::Border))
		{
			_upImpl->breadcrumbsView.Draw(refUi, _upImpl->state, *this);
			refUi.same_line();
			refUi.text_colored(
				{ 0.67f, 0.70f, 0.76f, 1.0f },
				"| %u / %u",
				frameData.uFilteredCount,
				static_cast<uint32_t>(_upImpl->deps.pAssetDatabaseService->GetItems().size()));
			refUi.separator();

			const AssetBrowserContentDrawResult drawResult =
				_upImpl->contentView.Draw(viewContext, frameData, _upImpl->contextMenus);

			if (drawResult.bOpenContentMenu)
			{
				refUi.open_popup(AssetBrowserSupport::GetAssetContentContextPopupId());
			}
			if (drawResult.bClearContentSelection)
			{
				ClearAssetSelection();
			}

			PublishContentShortcutScope(drawResult.bContentFocused);
			DispatchContentShortcuts(frameContext, drawResult.bContentFocused);
			_upImpl->contextMenus.DrawContentMenu(viewContext, frameData);

			const AshEngine::AssetInfo* pSelectedAsset =
				AssetBrowserSupport::GetSelectedAsset(*_upImpl->deps.pAssetDatabaseService, _upImpl->state.uSelectedAssetId);
			_upImpl->state.bSelectedAssetVisibleThisFrame = AssetBrowserSupport::IsSelectedAssetVisible(
				pSelectedAsset,
				frameData.bActiveDirectoryExists,
				frameData.pathActiveDirectory,
				_upImpl->state.strSearchText,
				*frameData.pTypeFilter);
		}
		refUi.end_child();

		AssetBrowserSupport::SyncSettings(_upImpl->deps, _upImpl->state);
		EndPanelWindow(frameContext);
	}
}
