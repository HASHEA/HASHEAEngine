#include "Panels/AssetBrowserPanel.h"

#include "Base/hlog.h"
#include "Core/AssetPresentationUtils.h"
#include "Core/EditorEventBindings.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorPathUtils.h"
#include "Core/EditorSelection.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
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
#include "Services/DragDropTransferService.h"
#include "Services/SelectionService.h"
#include "Shell/PanelManager.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

namespace AshEditor
{
	namespace
	{
		constexpr const char* kCreateFolderModalId = "AssetBrowserCreateFolderModal";
		constexpr const char* kRenameAssetModalId = "AssetBrowserRenameAssetModal";
		constexpr const char* kDeleteAssetModalId = "AssetBrowserDeleteAssetModal";

		void ClearIfAssetSelected(SelectionService* pSelectionService)
		{
			if (pSelectionService && pSelectionService->GetSelection().eKind == EditorSelectionKind::Asset)
			{
				pSelectionService->Clear();
			}
		}

		bool OpenPathExternally(const std::filesystem::path& pathAbsolute)
		{
#if defined(_WIN32)
			const std::wstring strPath = pathAbsolute.wstring();
			const HINSTANCE hResult = ::ShellExecuteW(nullptr, L"open", strPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			return reinterpret_cast<intptr_t>(hResult) > 32;
#else
			(void)pathAbsolute;
			return false;
#endif
		}

		bool RevealPathInExplorer(const std::filesystem::path& pathAbsolute)
		{
#if defined(_WIN32)
			std::wstring strArgument = L"/select,\"" + pathAbsolute.wstring() + L"\"";
			const HINSTANCE hResult = ::ShellExecuteW(
				nullptr,
				L"open",
				L"explorer.exe",
				strArgument.c_str(),
				nullptr,
				SW_SHOWNORMAL);
			return reinterpret_cast<intptr_t>(hResult) > 32;
#else
			(void)pathAbsolute;
			return false;
#endif
		}

		void AppendUniqueAssetId(std::vector<uint64_t>& vecAssetIds, uint64_t uAssetId)
		{
			if (uAssetId == 0 ||
				std::find(vecAssetIds.begin(), vecAssetIds.end(), uAssetId) != vecAssetIds.end())
			{
				return;
			}

			vecAssetIds.push_back(uAssetId);
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
					SetAssetSelection(
						{ refEvent.currentSelection.uId },
						refEvent.currentSelection.uId,
						refEvent.currentSelection.uId,
						false);
				}
				else if (refEvent.currentSelection.IsEmpty() && refEvent.previousSelection.eKind == EditorSelectionKind::Asset)
				{
					ResetAssetSelectionState(false);
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
			HasSingleSelectedAsset() &&
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

	bool AssetBrowserPanel::CanExecuteCreateFolder() const
	{
		return _upImpl && _upImpl->deps.pAssetDatabaseService && _upImpl->state.bActiveDirectoryExistsThisFrame;
	}

	void AssetBrowserPanel::ExecuteCreateFolder()
	{
		if (!CanExecuteCreateFolder())
		{
			return;
		}

		RequestCreateFolderModal();
	}

	bool AssetBrowserPanel::CanExecuteInstantiateSelected() const
	{
		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset();
		return
			_upImpl &&
			_upImpl->deps.pAssetDatabaseService &&
			_upImpl->deps.pCommandExecutor &&
			HasSingleSelectedAsset() &&
			pSelectedAsset &&
			IsSceneInstantiableAssetType(pSelectedAsset->type);
	}

	void AssetBrowserPanel::ExecuteInstantiateSelected()
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService || !_upImpl->deps.pCommandExecutor)
		{
			return;
		}

		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset();
		if (!pSelectedAsset || !IsSceneInstantiableAssetType(pSelectedAsset->type))
		{
			return;
		}

		const bool bExecuted = _upImpl->deps.pCommandExecutor->ExecuteCommand(
			std::make_unique<InstantiateSceneAssetCommand>(
				_upImpl->deps.pAssetDatabaseService,
				pSelectedAsset->id,
				0,
				false,
				AshEngine::TransformComponent{},
				BuildSceneAssetEntityName(*pSelectedAsset)));
		if (bExecuted)
		{
			HLogInfo("Instantiated asset into scene: {}", pSelectedAsset->relative_path.generic_string());
			return;
		}

		HLogWarning("Failed to instantiate asset into scene: {}", pSelectedAsset->relative_path.generic_string());
	}

	bool AssetBrowserPanel::CanExecuteRenameSelected() const
	{
		return HasSingleSelectedAsset() && GetSelectedAsset() != nullptr;
	}

	void AssetBrowserPanel::ExecuteRenameSelected()
	{
		if (!CanExecuteRenameSelected())
		{
			return;
		}

		RequestRenameSelectedModal();
	}

	bool AssetBrowserPanel::CanExecuteDeleteSelected() const
	{
		return HasAssetSelection();
	}

	void AssetBrowserPanel::ExecuteDeleteSelected()
	{
		if (!CanExecuteDeleteSelected())
		{
			return;
		}

		RequestDeleteSelectedModal();
	}

	bool AssetBrowserPanel::CanExecuteReimportSelected() const
	{
		return HasSingleSelectedAsset() && GetSelectedAsset() != nullptr;
	}

	void AssetBrowserPanel::ExecuteReimportSelected()
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset();
		if (!pSelectedAsset)
		{
			return;
		}

		std::string strError{};
		if (_upImpl->deps.pAssetDatabaseService->ReimportAsset(pSelectedAsset->relative_path, &strError))
		{
			SelectAssetByPath(pSelectedAsset->relative_path);
			HLogInfo("Asset reimported: {}", pSelectedAsset->relative_path.generic_string());
			return;
		}

		const std::string strAssetPath = pSelectedAsset->relative_path.generic_string();
		if (!strError.empty())
		{
			HLogError("Asset reimport failed for {}. {}", strAssetPath, strError);
		}
		else
		{
			HLogError("Asset reimport failed for {}.", strAssetPath);
		}
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

	const AshEngine::AssetInfo* AssetBrowserPanel::GetSelectedAsset() const
	{
		return
			_upImpl && _upImpl->deps.pAssetDatabaseService
			? AssetBrowserSupport::GetSelectedAsset(*_upImpl->deps.pAssetDatabaseService, _upImpl->state.uSelectedAssetId)
			: nullptr;
	}

	std::vector<const AshEngine::AssetInfo*> AssetBrowserPanel::GetSelectedAssets() const
	{
		std::vector<const AshEngine::AssetInfo*> vecSelectedAssets{};
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return vecSelectedAssets;
		}

		vecSelectedAssets.reserve(_upImpl->state.vecSelectedAssetIds.size());
		for (const uint64_t uAssetId : _upImpl->state.vecSelectedAssetIds)
		{
			const AshEngine::AssetInfo* pAsset =
				_upImpl->deps.pAssetDatabaseService->FindById(uAssetId);
			if (pAsset)
			{
				vecSelectedAssets.push_back(pAsset);
			}
		}

		return vecSelectedAssets;
	}

	std::vector<std::filesystem::path> AssetBrowserPanel::GetSelectedAssetPaths(bool bRemoveNestedDescendants) const
	{
		std::vector<std::filesystem::path> vecSelectedPaths{};
		for (const AshEngine::AssetInfo* pAsset : GetSelectedAssets())
		{
			if (pAsset)
			{
				vecSelectedPaths.push_back(pAsset->relative_path);
			}
		}

		if (bRemoveNestedDescendants)
		{
			vecSelectedPaths = EditorPathUtils::RemoveNestedDescendantPaths(std::move(vecSelectedPaths));
		}

		return vecSelectedPaths;
	}

	void AssetBrowserPanel::SetAssetSelection(
		std::vector<uint64_t> vecSelectedAssetIds,
		uint64_t uPrimaryAssetId,
		uint64_t uAnchorAssetId,
		bool bSyncGlobalSelection)
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.vecSelectedAssetIds.clear();
		for (const uint64_t uAssetId : vecSelectedAssetIds)
		{
			AppendUniqueAssetId(_upImpl->state.vecSelectedAssetIds, uAssetId);
		}

		if (_upImpl->state.vecSelectedAssetIds.empty())
		{
			ResetAssetSelectionState(bSyncGlobalSelection);
			return;
		}

		const auto itPrimary = std::find(
			_upImpl->state.vecSelectedAssetIds.begin(),
			_upImpl->state.vecSelectedAssetIds.end(),
			uPrimaryAssetId);
		const auto itAnchor = std::find(
			_upImpl->state.vecSelectedAssetIds.begin(),
			_upImpl->state.vecSelectedAssetIds.end(),
			uAnchorAssetId);

		_upImpl->state.uSelectedAssetId =
			itPrimary != _upImpl->state.vecSelectedAssetIds.end()
			? uPrimaryAssetId
			: _upImpl->state.vecSelectedAssetIds.back();
		_upImpl->state.uSelectionAnchorAssetId =
			itAnchor != _upImpl->state.vecSelectedAssetIds.end()
			? uAnchorAssetId
			: _upImpl->state.uSelectedAssetId;

		if (bSyncGlobalSelection)
		{
			SyncPrimaryAssetSelection();
		}
	}

	void AssetBrowserPanel::ResetAssetSelectionState(bool bClearGlobalSelection)
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.vecSelectedAssetIds.clear();
		_upImpl->state.uSelectedAssetId = 0u;
		_upImpl->state.uSelectionAnchorAssetId = 0u;
		if (bClearGlobalSelection)
		{
			ClearIfAssetSelected(_upImpl->deps.pSelectionService);
		}
	}

	void AssetBrowserPanel::SelectAssetsByPaths(const std::vector<std::filesystem::path>& vecRelativePaths)
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		std::vector<uint64_t> vecSelectedAssetIds{};
		vecSelectedAssetIds.reserve(vecRelativePaths.size());
		for (const std::filesystem::path& pathRelative : vecRelativePaths)
		{
			const AshEngine::AssetInfo* pAsset = _upImpl->deps.pAssetDatabaseService->FindByPath(pathRelative);
			if (pAsset)
			{
				AppendUniqueAssetId(vecSelectedAssetIds, pAsset->id);
			}
		}

		const uint64_t uPrimaryAssetId = vecSelectedAssetIds.empty() ? 0 : vecSelectedAssetIds.back();
		SetAssetSelection(vecSelectedAssetIds, uPrimaryAssetId, uPrimaryAssetId);
	}

	void AssetBrowserPanel::SelectAsset(const AshEngine::AssetInfo& refItem)
	{
		if (!_upImpl)
		{
			return;
		}

		SetAssetSelection({ refItem.id }, refItem.id, refItem.id);
	}

	void AssetBrowserPanel::FocusAssetSelection(const AshEngine::AssetInfo& refItem)
	{
		if (!_upImpl)
		{
			return;
		}

		if (!AssetBrowserSupport::IsAssetSelected(_upImpl->state, refItem.id))
		{
			SelectAsset(refItem);
			return;
		}

		SetAssetSelection(_upImpl->state.vecSelectedAssetIds, refItem.id, refItem.id);
	}

	void AssetBrowserPanel::ToggleAssetSelection(const AshEngine::AssetInfo& refAsset)
	{
		if (!_upImpl)
		{
			return;
		}

		std::vector<uint64_t>& vecSelectedAssetIds = _upImpl->state.vecSelectedAssetIds;
		const auto itSelected =
			std::find(vecSelectedAssetIds.begin(), vecSelectedAssetIds.end(), refAsset.id);
		if (itSelected == vecSelectedAssetIds.end())
		{
			vecSelectedAssetIds.push_back(refAsset.id);
			SetAssetSelection(vecSelectedAssetIds, refAsset.id, refAsset.id);
			return;
		}

		vecSelectedAssetIds.erase(itSelected);
		const uint64_t uPrimaryAssetId =
			_upImpl->state.uSelectedAssetId == refAsset.id && !vecSelectedAssetIds.empty()
			? vecSelectedAssetIds.back()
			: _upImpl->state.uSelectedAssetId;
		const uint64_t uAnchorAssetId =
			_upImpl->state.uSelectionAnchorAssetId == refAsset.id && !vecSelectedAssetIds.empty()
			? vecSelectedAssetIds.front()
			: _upImpl->state.uSelectionAnchorAssetId;
		SetAssetSelection(vecSelectedAssetIds, uPrimaryAssetId, uAnchorAssetId);
	}

	void AssetBrowserPanel::SelectAssetRange(
		const AshEngine::AssetInfo& refAsset,
		const AssetBrowserFrameData& refFrameData)
	{
		if (!_upImpl || refFrameData.vecVisibleItems.empty())
		{
			SelectAsset(refAsset);
			return;
		}

		const uint64_t uAnchorAssetId =
			_upImpl->state.uSelectionAnchorAssetId != 0
			? _upImpl->state.uSelectionAnchorAssetId
			: (_upImpl->state.uSelectedAssetId != 0 ? _upImpl->state.uSelectedAssetId : refAsset.id);

		size_t uAnchorIndex = refFrameData.vecVisibleItems.size();
		size_t uTargetIndex = refFrameData.vecVisibleItems.size();
		for (size_t uIndex = 0; uIndex < refFrameData.vecVisibleItems.size(); ++uIndex)
		{
			const AshEngine::AssetInfo* pVisibleAsset = refFrameData.vecVisibleItems[uIndex].pAsset;
			if (!pVisibleAsset)
			{
				continue;
			}

			if (pVisibleAsset->id == uAnchorAssetId)
			{
				uAnchorIndex = uIndex;
			}
			if (pVisibleAsset->id == refAsset.id)
			{
				uTargetIndex = uIndex;
			}
		}

		if (uAnchorIndex >= refFrameData.vecVisibleItems.size() ||
			uTargetIndex >= refFrameData.vecVisibleItems.size())
		{
			SelectAsset(refAsset);
			return;
		}

		const size_t uBegin = std::min(uAnchorIndex, uTargetIndex);
		const size_t uEnd = std::max(uAnchorIndex, uTargetIndex);
		std::vector<uint64_t> vecSelectedAssetIds{};
		vecSelectedAssetIds.reserve(uEnd - uBegin + 1u);
		for (size_t uIndex = uBegin; uIndex <= uEnd; ++uIndex)
		{
			const AshEngine::AssetInfo* pVisibleAsset = refFrameData.vecVisibleItems[uIndex].pAsset;
			if (pVisibleAsset)
			{
				AppendUniqueAssetId(vecSelectedAssetIds, pVisibleAsset->id);
			}
		}

		SetAssetSelection(vecSelectedAssetIds, refAsset.id, uAnchorAssetId);
	}

	bool AssetBrowserPanel::IsAssetSelected(uint64_t uAssetId) const
	{
		return _upImpl && AssetBrowserSupport::IsAssetSelected(_upImpl->state, uAssetId);
	}

	void AssetBrowserPanel::SelectAssetByPath(const std::filesystem::path& refRelativePath)
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		const AshEngine::AssetInfo* pAsset = _upImpl->deps.pAssetDatabaseService->FindByPath(refRelativePath);
		if (pAsset)
		{
			SelectAsset(*pAsset);
			return;
		}

		ClearAssetSelection();
	}

	void AssetBrowserPanel::ClearAssetSelection()
	{
		if (!_upImpl)
		{
			return;
		}

		ResetAssetSelectionState(true);
	}

	void AssetBrowserPanel::OpenAssetItem(const AshEngine::AssetInfo& refItem)
	{
		if (refItem.is_directory)
		{
			NavigateToDirectory(refItem.relative_path);
			return;
		}

		SelectAsset(refItem);
		OpenAssetPreview(refItem);
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

	void AssetBrowserPanel::OpenAssetExternally(const AshEngine::AssetInfo& refItem)
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		const std::filesystem::path pathAbsolute = _upImpl->deps.pAssetDatabaseService->ResolveAssetPath(refItem.relative_path);
		if (pathAbsolute.empty() || !OpenPathExternally(pathAbsolute))
		{
			HLogError("Failed to open asset externally: {}", refItem.relative_path.generic_string());
			return;
		}

		HLogInfo("Opened asset externally: {}", refItem.relative_path.generic_string());
	}

	void AssetBrowserPanel::RevealAssetInExplorer(const AshEngine::AssetInfo& refItem)
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		const std::filesystem::path pathAbsolute = _upImpl->deps.pAssetDatabaseService->ResolveAssetPath(refItem.relative_path);
		if (pathAbsolute.empty() || !RevealPathInExplorer(pathAbsolute))
		{
			HLogError("Failed to reveal asset in explorer: {}", refItem.relative_path.generic_string());
			return;
		}

		HLogInfo("Revealed asset in explorer: {}", refItem.relative_path.generic_string());
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

	bool AssetBrowserPanel::HandleAssetDropToDirectory(
		const std::filesystem::path& refTargetDirectoryPath,
		uint64_t uTransferId)
	{
		if (!_upImpl ||
			!_upImpl->deps.pAssetDatabaseService ||
			!_upImpl->deps.pDragDropTransferService)
		{
			return false;
		}

		const DragDropTransferData* pTransfer =
			_upImpl->deps.pDragDropTransferService->Resolve(uTransferId);
		if (!pTransfer ||
			pTransfer->strPayloadType != EditorDragPayloadTypes::Asset ||
			pTransfer->vecEntityIds.empty())
		{
			return false;
		}

		std::vector<std::filesystem::path> vecSourcePaths{};
		vecSourcePaths.reserve(pTransfer->vecEntityIds.size());
		for (const uint64_t uAssetId : pTransfer->vecEntityIds)
		{
			const AshEngine::AssetInfo* pSourceAsset =
				_upImpl->deps.pAssetDatabaseService->FindById(uAssetId);
			if (pSourceAsset)
			{
				vecSourcePaths.push_back(pSourceAsset->relative_path);
			}
		}
		if (vecSourcePaths.empty())
		{
			return false;
		}

		std::vector<std::filesystem::path> vecMovedRelativePaths{};
		std::string strError{};
		if (_upImpl->deps.pAssetDatabaseService->MoveAssets(
			vecSourcePaths,
			refTargetDirectoryPath,
			&vecMovedRelativePaths,
			&strError))
		{
			NavigateToDirectory(refTargetDirectoryPath);
			SelectAssetsByPaths(vecMovedRelativePaths);
			HLogInfo(
				"Moved {} asset item(s) to {}.",
				static_cast<uint32_t>(vecMovedRelativePaths.size()),
				refTargetDirectoryPath.empty() ? std::string("<root>") : refTargetDirectoryPath.generic_string());
			return true;
		}

		if (!strError.empty())
		{
			HLogWarning(
				"Failed to move {} selected asset item(s) to {}. {}",
				static_cast<uint32_t>(vecSourcePaths.size()),
				refTargetDirectoryPath.empty() ? std::string("<root>") : refTargetDirectoryPath.generic_string(),
				strError);
		}
		return false;
	}

	void AssetBrowserPanel::RequestCreateFolderModal()
	{
		if (!_upImpl)
		{
			return;
		}

		_upImpl->state.bOpenCreateFolderModal = true;
		_upImpl->state.strCreateFolderParentPath = _upImpl->state.strActiveDirectoryPath;
		_upImpl->state.strCreateFolderName = "New Folder";
		ClearWorkflowError();
	}

	bool AssetBrowserPanel::HasAssetSelection() const
	{
		return _upImpl && !_upImpl->state.vecSelectedAssetIds.empty();
	}

	bool AssetBrowserPanel::HasSingleSelectedAsset() const
	{
		return _upImpl && _upImpl->state.vecSelectedAssetIds.size() == 1u;
	}

	void AssetBrowserPanel::SyncPrimaryAssetSelection()
	{
		if (!_upImpl)
		{
			return;
		}

		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset();
		if (!pSelectedAsset)
		{
			ClearIfAssetSelected(_upImpl->deps.pSelectionService);
			return;
		}

		if (_upImpl->deps.pSelectionService)
		{
			_upImpl->deps.pSelectionService->Select({
				EditorSelectionKind::Asset,
				pSelectedAsset->id,
				GetAssetDisplayLabel(*pSelectedAsset),
				pSelectedAsset->relative_path.generic_string() });
		}
	}

	void AssetBrowserPanel::RequestRenameSelectedModal()
	{
		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset();
		if (!_upImpl || !pSelectedAsset)
		{
			return;
		}

		_upImpl->state.bOpenRenameModal = true;
		_upImpl->state.strRenameTargetPath = pSelectedAsset->relative_path.generic_string();
		_upImpl->state.strRenameValue = pSelectedAsset->name;
		ClearWorkflowError();
	}

	void AssetBrowserPanel::RequestDeleteSelectedModal()
	{
		if (!_upImpl)
		{
			return;
		}

		const std::vector<std::filesystem::path> vecDeleteTargetPaths =
			GetSelectedAssetPaths(true);
		if (vecDeleteTargetPaths.empty())
		{
			return;
		}

		_upImpl->state.bOpenDeleteModal = true;
		_upImpl->state.vecDeleteTargetPaths = vecDeleteTargetPaths;

		if (vecDeleteTargetPaths.size() == 1u)
		{
			_upImpl->state.strDeleteTargetPath = vecDeleteTargetPaths.front().generic_string();
			if (_upImpl->deps.pAssetDatabaseService)
			{
				const AshEngine::AssetInfo* pSingleAsset =
					_upImpl->deps.pAssetDatabaseService->FindByPath(vecDeleteTargetPaths.front());
				_upImpl->state.strDeleteTargetLabel =
					pSingleAsset ? GetAssetDisplayLabel(*pSingleAsset) : vecDeleteTargetPaths.front().filename().generic_string();
			}
			else
			{
				_upImpl->state.strDeleteTargetLabel = vecDeleteTargetPaths.front().filename().generic_string();
			}
		}
		else
		{
			_upImpl->state.strDeleteTargetPath.clear();
			_upImpl->state.strDeleteTargetLabel =
				std::to_string(vecDeleteTargetPaths.size()) + " selected assets";
		}
		ClearWorkflowError();
	}

	void AssetBrowserPanel::ClearWorkflowError()
	{
		if (_upImpl)
		{
			_upImpl->state.strWorkflowError.clear();
		}
	}

	void AssetBrowserPanel::DrawWorkflowModals(AshEngine::UIContext& refUi)
	{
		if (!_upImpl || !_upImpl->deps.pAssetDatabaseService)
		{
			return;
		}

		AssetBrowserPanelState& refState = _upImpl->state;
		if (refState.bOpenCreateFolderModal)
		{
			refUi.open_popup(kCreateFolderModalId);
			refState.bOpenCreateFolderModal = false;
		}
		if (refState.bOpenRenameModal)
		{
			refUi.open_popup(kRenameAssetModalId);
			refState.bOpenRenameModal = false;
		}
		if (refState.bOpenDeleteModal)
		{
			refUi.open_popup(kDeleteAssetModalId);
			refState.bOpenDeleteModal = false;
		}

		if (refUi.begin_popup_modal(kCreateFolderModalId, nullptr, AshEngine::UIWindowFlagBits::AlwaysAutoResize))
		{
			refUi.text_unformatted("Create a folder in the current asset directory.");
			refUi.text_colored(
				GetEditorMutedTextColor(refUi),
				"Directory: %s",
				refState.strCreateFolderParentPath.empty() ? "<root>" : refState.strCreateFolderParentPath.c_str());
			refUi.separator();
			refUi.set_next_item_width(320.0f);
			const bool bSubmitByEnter = refUi.input_text(
				"Name",
				refState.strCreateFolderName,
				AshEngine::UIInputTextFlagBits::EnterReturnsTrue |
				AshEngine::UIInputTextFlagBits::AutoSelectAll);
			if (!refState.strWorkflowError.empty())
			{
				refUi.text_colored(GetEditorErrorTextColor(refUi), "%s", refState.strWorkflowError.c_str());
			}
			refUi.separator();

			bool bSubmit = bSubmitByEnter;
			if (refUi.button("Create"))
			{
				bSubmit = true;
			}
			refUi.same_line();
			if (refUi.button("Cancel"))
			{
				refUi.close_current_popup();
				ClearWorkflowError();
			}

			if (bSubmit)
			{
				std::filesystem::path pathCreatedRelative{};
				std::string strError{};
				if (_upImpl->deps.pAssetDatabaseService->CreateDirectory(
					std::filesystem::path(refState.strCreateFolderParentPath),
					refState.strCreateFolderName,
					&pathCreatedRelative,
					&strError))
				{
					NavigateToDirectory(std::filesystem::path(refState.strCreateFolderParentPath));
					SelectAssetByPath(pathCreatedRelative);
					HLogInfo("Created asset folder: {}", pathCreatedRelative.generic_string());
					refUi.close_current_popup();
					ClearWorkflowError();
				}
				else
				{
					refState.strWorkflowError =
						strError.empty() ? std::string("Failed to create folder.") : std::move(strError);
				}
			}

			refUi.end_popup();
		}

		if (refUi.begin_popup_modal(kRenameAssetModalId, nullptr, AshEngine::UIWindowFlagBits::AlwaysAutoResize))
		{
			refUi.text_unformatted("Rename the selected asset or folder.");
			refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", refState.strRenameTargetPath.c_str());
			refUi.separator();
			refUi.set_next_item_width(320.0f);
			const bool bSubmitByEnter = refUi.input_text(
				"New Name",
				refState.strRenameValue,
				AshEngine::UIInputTextFlagBits::EnterReturnsTrue |
				AshEngine::UIInputTextFlagBits::AutoSelectAll);
			if (!refState.strWorkflowError.empty())
			{
				refUi.text_colored(GetEditorErrorTextColor(refUi), "%s", refState.strWorkflowError.c_str());
			}
			refUi.separator();

			bool bSubmit = bSubmitByEnter;
			if (refUi.button("Rename"))
			{
				bSubmit = true;
			}
			refUi.same_line();
			if (refUi.button("Cancel"))
			{
				refUi.close_current_popup();
				ClearWorkflowError();
			}

			if (bSubmit)
			{
				std::filesystem::path pathRenamedRelative{};
				std::string strError{};
				if (_upImpl->deps.pAssetDatabaseService->RenameAsset(
					std::filesystem::path(refState.strRenameTargetPath),
					refState.strRenameValue,
					&pathRenamedRelative,
					&strError))
				{
					SelectAssetByPath(pathRenamedRelative);
					HLogInfo("Renamed asset to {}", pathRenamedRelative.generic_string());
					refUi.close_current_popup();
					ClearWorkflowError();
				}
				else
				{
					refState.strWorkflowError =
						strError.empty() ? std::string("Failed to rename asset.") : std::move(strError);
				}
			}

			refUi.end_popup();
		}

		if (refUi.begin_popup_modal(kDeleteAssetModalId, nullptr, AshEngine::UIWindowFlagBits::AlwaysAutoResize))
		{
			const std::vector<std::filesystem::path>& vecDeleteTargets = _upImpl->state.vecDeleteTargetPaths;
			const bool bBatchDelete = vecDeleteTargets.size() > 1u;

			refUi.text_unformatted(
				bBatchDelete
				? "Delete the selected assets from disk?"
				: "Delete the selected asset from disk?");
			refUi.text_colored(GetEditorWarningTextColor(refUi), "%s", refState.strDeleteTargetLabel.c_str());
			if (!refState.strDeleteTargetPath.empty())
			{
				refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", refState.strDeleteTargetPath.c_str());
			}
			else if (bBatchDelete)
			{
				const size_t uPreviewCount = std::min<size_t>(5u, vecDeleteTargets.size());
				for (size_t uIndex = 0; uIndex < uPreviewCount; ++uIndex)
				{
					refUi.text_colored(
						GetEditorMutedTextColor(refUi),
						"- %s",
						vecDeleteTargets[uIndex].generic_string().c_str());
				}
				if (uPreviewCount < vecDeleteTargets.size())
				{
					refUi.text_colored(
						GetEditorMutedTextColor(refUi),
						"... and %zu more",
						vecDeleteTargets.size() - uPreviewCount);
				}
			}
			if (!refState.strWorkflowError.empty())
			{
				refUi.separator();
				refUi.text_colored(GetEditorErrorTextColor(refUi), "%s", refState.strWorkflowError.c_str());
			}
			refUi.separator();
			if (refUi.button("Delete"))
			{
				std::string strError{};
				std::vector<std::filesystem::path> vecDeletedPaths{};
				if (_upImpl->deps.pAssetDatabaseService->DeleteAssets(
					vecDeleteTargets,
					&vecDeletedPaths,
					&strError))
				{
					const std::filesystem::path pathActiveDirectory(refState.strActiveDirectoryPath);
					for (const std::filesystem::path& pathDeletedRelative : vecDeletedPaths)
					{
						if (!pathActiveDirectory.empty() &&
							AssetBrowserSupport::IsSameOrAncestorPath(pathDeletedRelative, pathActiveDirectory))
						{
							NavigateToDirectory(pathDeletedRelative.parent_path());
							break;
						}
					}

					ClearAssetSelection();
					HLogInfo(
						"Deleted {} asset item(s) from the browser selection.",
						static_cast<uint32_t>(vecDeletedPaths.size()));
					refUi.close_current_popup();
					ClearWorkflowError();
				}
				else
				{
					refState.strWorkflowError =
						strError.empty() ? std::string("Failed to delete asset.") : std::move(strError);
				}
			}
			refUi.same_line();
			if (refUi.button("Cancel"))
			{
				refUi.close_current_popup();
				ClearWorkflowError();
			}

			refUi.end_popup();
		}
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
				GetEditorMutedTextColor(refUi),
				"| %u / %u",
				frameData.uFilteredCount,
				static_cast<uint32_t>(_upImpl->deps.pAssetDatabaseService->GetItems().size()));
			if (frameData.uSelectedCount > 1u)
			{
				refUi.same_line();
				refUi.text_colored(
					GetEditorMutedTextColor(refUi),
					"| %u selected",
					frameData.uSelectedCount);
			}
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

		DrawWorkflowModals(refUi);
		AssetBrowserSupport::SyncSettings(_upImpl->deps, _upImpl->state);
		EndPanelWindow(frameContext);
	}
}
