#include "App/EditorActionCoordinator.h"

#include "App/EditorActionRegistrar.h"
#include "App/SceneWorkflowCoordinator.h"
#include "App/ViewportPanelStateBridge.h"
#include "Base/hlog.h"
#include "Core/EditorContext.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorScenePathUtils.h"
#include "Core/INotificationSink.h"
#include "Core/PlatformFileDialog.h"
#include "Services/AssetDatabaseService.h"
#include "Services/CommandService.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/TerrainEditorService.h"
#include "Services/UndoRedoService.h"
#include "Shell/DockLayoutController.h"
#include "Shell/EditorCommandPaletteController.h"
#include "Shell/PanelManager.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace AshEditor
{
	namespace
	{
		static constexpr wchar_t kSceneFileDialogFilter[] =
			L"Scene Files (*.scene.json)\0*.scene.json\0"
			L"JSON Files (*.json)\0*.json\0"
			L"All Files (*.*)\0*.*\0\0";

		std::filesystem::path ResolveSceneOpenInitialDirectory(const EditorActionCoordinatorContext& context)
		{
			const std::filesystem::path pathActiveScene = context.refSceneService.GetActiveScenePath();
			if (!pathActiveScene.empty())
			{
				return pathActiveScene.parent_path();
			}

			const std::filesystem::path pathSceneDirectory =
				context.refSettingsService.GetAssetsRootPath() / "scenes";
			std::error_code errorCode{};
			if (std::filesystem::exists(pathSceneDirectory, errorCode) && !errorCode)
			{
				return pathSceneDirectory;
			}

			return context.refSettingsService.GetAssetsRootPath();
		}

		SceneWorkflowContext MakeSceneWorkflowContext(const EditorActionCoordinatorContext& context)
		{
			return SceneWorkflowContext{
				context.refSceneService,
				context.refSelectionService,
				context.refUndoRedoService,
				context.refSettingsService,
				&context.refEventBus,
				&context.refNotificationSink
			};
		}

		void Notify(const EditorActionCoordinatorContext& context, const std::string& strMessage)
		{
			context.refNotificationSink.Notify(strMessage, "Editor");
		}

		size_t GetSelectedEntityCount(const EditorActionCoordinatorContext& context)
		{
			return context.refSelectionService.GetSelectedIds(EditorSelectionKind::Entity).size();
		}
	}

	EditorActionCoordinator::EditorActionCoordinator(EditorActionCoordinatorContext context)
		: _context(context)
	{
	}

	void EditorActionCoordinator::BindPanelActionTargets(
		IAssetBrowserActionTarget* pAssetBrowserActionTarget,
		ISceneHierarchyActionTarget* pSceneHierarchyActionTarget)
	{
		_pAssetBrowserActionTarget = pAssetBrowserActionTarget;
		_pSceneHierarchyActionTarget = pSceneHierarchyActionTarget;
	}

	void EditorActionCoordinator::RegisterActions()
	{
		EditorActionRegistrarContext context{
			_context.refCommandService,
			this
		};
		EditorActionRegistrar::Register(context);
	}

	void EditorActionCoordinator::Update()
	{
		if (!_optPendingSceneSave)
		{
			return;
		}

		const PendingSceneSave pending = *_optPendingSceneSave;
		if (_context.refSceneService.GetActiveScene().get_content_epoch() !=
			pending.scene_content_epoch)
		{
			_optPendingSceneSave.reset();
			FailSceneSave(
				pending.path,
				"Scene save canceled because the active Scene changed while Terrain data was saving.");
			return;
		}

		TerrainEditorService* pTerrainService = _context.refEditorContext.pTerrainEditorService;
		if (!pTerrainService)
		{
			_optPendingSceneSave.reset();
			FailSceneSave(pending.path, "Scene save failed because Terrain authoring became unavailable.");
			return;
		}

		const TerrainFileOperationState& fileState = pTerrainService->GetFileOperationState();
		if (fileState.operation_serial != pending.terrain_operation_serial)
		{
			_optPendingSceneSave.reset();
			FailSceneSave(pending.path, "Scene save lost its referenced Terrain save operation.");
			return;
		}
		if (fileState.status == TerrainFileOperationStatus::AwaitingPublication ||
			fileState.status == TerrainFileOperationStatus::Running)
		{
			return;
		}

		_optPendingSceneSave.reset();
		if (fileState.status == TerrainFileOperationStatus::Failed)
		{
			const std::string detail = fileState.error.empty()
				? pTerrainService->GetLastError()
				: fileState.error;
			FailSceneSave(
				pending.path,
				"Scene save stopped because referenced Terrain '" +
				fileState.path.generic_string() + "' failed: " +
				(detail.empty() ? "unknown Terrain save error" : detail));
			return;
		}
		if (fileState.status != TerrainFileOperationStatus::Succeeded)
		{
			FailSceneSave(pending.path, "Scene save observed an invalid Terrain operation state.");
			return;
		}

		SaveSceneNow(
			pending.path,
			pending.scene_name,
			pending.history_state_id);
	}

	bool EditorActionCoordinator::CanExecuteAction(std::string_view svActionId) const
	{
		if (svActionId == EditorActionIds::FileSaveScene)
		{
			return !_optPendingSceneSave.has_value();
		}
		if (svActionId == EditorActionIds::FileReloadScene)
		{
			return !_context.refSceneService.GetActiveScenePath().empty();
		}
		if (svActionId == EditorActionIds::AssetsOpenSelected)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteOpenSelected();
		}
		if (svActionId == EditorActionIds::AssetsNavigateUp)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteNavigateUp();
		}
		if (svActionId == EditorActionIds::AssetsCreateFolder)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteCreateFolder();
		}
		if (svActionId == EditorActionIds::AssetsInstantiateSelected)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteInstantiateSelected();
		}
		if (svActionId == EditorActionIds::AssetsRenameSelected)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteRenameSelected();
		}
		if (svActionId == EditorActionIds::AssetsDeleteSelected)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteDeleteSelected();
		}
		if (svActionId == EditorActionIds::AssetsReimportSelected)
		{
			return _pAssetBrowserActionTarget && _pAssetBrowserActionTarget->CanExecuteReimportSelected();
		}
		if (svActionId == EditorActionIds::EditUndo)
		{
			return _context.refUndoRedoService.CanUndo();
		}
		if (svActionId == EditorActionIds::EditRedo)
		{
			return _context.refUndoRedoService.CanRedo();
		}
		if (svActionId == EditorActionIds::EditCopy)
		{
			return HasSelectedEntity();
		}
		if (svActionId == EditorActionIds::EditPaste)
		{
			return _pSceneHierarchyActionTarget && _pSceneHierarchyActionTarget->CanPasteSelection();
		}
		if (svActionId == EditorActionIds::SceneCreateChild ||
			svActionId == EditorActionIds::SelectionRename ||
			svActionId == EditorActionIds::SelectionReparent)
		{
			return HasSingleSelectedEntity();
		}
		if (svActionId == EditorActionIds::SelectionDuplicate ||
			svActionId == EditorActionIds::SelectionDelete)
		{
			return HasSelectedEntity();
		}

		// Default: always enabled (FileNewScene, FileSaveScene, AssetsRefresh,
		// WindowResetLayout, WindowCommandPalette, SceneCreateRoot)
		return true;
	}

	void EditorActionCoordinator::ExecuteAction(std::string_view svActionId)
	{
		if (svActionId == EditorActionIds::FileNewScene)
		{
			HandleNewScene();
		}
		else if (svActionId == EditorActionIds::FileOpenScene)
		{
			HandleOpenScene();
		}
		else if (svActionId == EditorActionIds::FileReloadScene)
		{
			HandleReloadScene();
		}
		else if (svActionId == EditorActionIds::FileSaveScene)
		{
			HandleSaveScene();
		}
		else if (svActionId == EditorActionIds::AssetsRefresh)
		{
			HandleRefreshAssets();
		}
		else if (svActionId == EditorActionIds::AssetsOpenSelected)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteOpenSelected();
			}
		}
		else if (svActionId == EditorActionIds::AssetsNavigateUp)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteNavigateUp();
			}
		}
		else if (svActionId == EditorActionIds::AssetsCreateFolder)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteCreateFolder();
			}
		}
		else if (svActionId == EditorActionIds::AssetsInstantiateSelected)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteInstantiateSelected();
			}
		}
		else if (svActionId == EditorActionIds::AssetsRenameSelected)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteRenameSelected();
			}
		}
		else if (svActionId == EditorActionIds::AssetsDeleteSelected)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteDeleteSelected();
			}
		}
		else if (svActionId == EditorActionIds::AssetsReimportSelected)
		{
			if (_pAssetBrowserActionTarget)
			{
				_pAssetBrowserActionTarget->ExecuteReimportSelected();
			}
		}
		else if (svActionId == EditorActionIds::WindowResetLayout)
		{
			HandleResetLayout();
		}
		else if (svActionId == EditorActionIds::WindowCommandPalette)
		{
			_context.refCommandPaletteController.RequestOpen();
		}
		else if (svActionId == EditorActionIds::EditUndo)
		{
			HandleUndo();
		}
		else if (svActionId == EditorActionIds::EditRedo)
		{
			HandleRedo();
		}
		else if (svActionId == EditorActionIds::EditCopy)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->ExecuteCopySelection();
			}
		}
		else if (svActionId == EditorActionIds::EditPaste)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->ExecutePasteSelection();
			}
		}
		else if (svActionId == EditorActionIds::SceneCreateRoot)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->ExecuteCreateRoot();
			}
		}
		else if (svActionId == EditorActionIds::SceneCreateChild)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->ExecuteCreateChildFromSelection();
			}
		}
		else if (svActionId == EditorActionIds::SelectionRename)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->RequestRenameSelected(_context.refEditorContext.pUiContext);
			}
		}
		else if (svActionId == EditorActionIds::SelectionReparent)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->RequestReparentSelected(_context.refEditorContext.pUiContext);
			}
		}
		else if (svActionId == EditorActionIds::SelectionDuplicate)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->ExecuteDuplicateSelection();
			}
		}
		else if (svActionId == EditorActionIds::SelectionDelete)
		{
			if (_pSceneHierarchyActionTarget)
			{
				_pSceneHierarchyActionTarget->RequestDeleteSelected(_context.refEditorContext.pUiContext);
			}
		}
	}

	void EditorActionCoordinator::HandleNewScene()
	{
		CancelPendingSceneSave("a new Scene was requested");
		SceneWorkflowContext sceneWorkflowContext = MakeSceneWorkflowContext(_context);
		const std::filesystem::path pathNewScene =
			_context.refSceneWorkflowCoordinator.CreateNewSceneFromStartupTemplate(
				sceneWorkflowContext,
				kUntitledSceneName);
		if (!pathNewScene.empty())
		{
			Notify(_context, "Created a new scene from template: " + pathNewScene.generic_string());
			return;
		}

		_context.refSceneWorkflowCoordinator.ActivateNewScene(sceneWorkflowContext, kUntitledSceneName);
		Notify(_context, "Failed to copy the startup scene template. Created a new default scene instead.");
	}

	bool EditorActionCoordinator::OpenSceneFromDialog(const char* pSource)
	{
		OpenFileDialogOptions options{};
		options.strTitle = L"Open Scene";
		options.pFilter = kSceneFileDialogFilter;
		options.strDefaultExtension = L"json";
		options.pathInitialDirectory = ResolveSceneOpenInitialDirectory(_context);

		const std::filesystem::path pathSelectedScene = OpenSingleFileDialog(options);
		if (pathSelectedScene.empty())
		{
			return false;
		}

		return OpenSceneFromPath(pathSelectedScene, pSource);
	}

	bool EditorActionCoordinator::OpenSceneFromPath(const std::filesystem::path& pathScene, const char* pSource)
	{
		(void)pSource;
		if (pathScene.empty())
		{
			return false;
		}
		CancelPendingSceneSave("another Scene was opened");

		std::error_code errorCode{};
		if (!std::filesystem::exists(pathScene, errorCode) || errorCode)
		{
			if (_context.refSettingsService.RemoveRecentScenePath(pathScene))
			{
				_context.refSettingsService.Save();
			}
			Notify(_context, "Scene file was not found: " + pathScene.generic_string());
			return false;
		}

		SceneWorkflowContext sceneWorkflowContext = MakeSceneWorkflowContext(_context);
		if (_context.refSceneWorkflowCoordinator.LoadSceneIntoEditor(sceneWorkflowContext, pathScene))
		{
			Notify(_context, "Opened scene: " + pathScene.generic_string());
			return true;
		}

		Notify(_context, "Failed to open scene: " + pathScene.generic_string());
		return false;
	}

	void EditorActionCoordinator::HandleOpenScene()
	{
		OpenSceneFromDialog("action");
	}

	void EditorActionCoordinator::HandleReloadScene()
	{
		CancelPendingSceneSave("the active Scene was reloaded");
		SceneWorkflowContext sceneWorkflowContext = MakeSceneWorkflowContext(_context);
		_context.refSceneWorkflowCoordinator.ReloadActiveScene(sceneWorkflowContext);
	}

	EditorActionCoordinator::DirtyTerrainSaveStartResult
		EditorActionCoordinator::SaveDirtyReferencedTerrains(PendingSceneSave& refPending)
	{
		TerrainEditorService* pTerrainService = _context.refEditorContext.pTerrainEditorService;
		if (!pTerrainService || !pTerrainService->HasDirtyAssets())
		{
			return DirtyTerrainSaveStartResult::NotRequired;
		}

		const AshEngine::TerrainAssetId dirtyAssetId = pTerrainService->GetSelectedAssetId();
		const AshEngine::TerrainWorkingSet* pWorkingSet = pTerrainService->GetWorkingSet();
		bool referenced = false;
		std::vector<std::filesystem::path> unresolvedTerrainReferences{};
		for (const AshEngine::Entity& entity :
			_context.refSceneService.GetActiveScene().get_entities_with_component(
				AshEngine::SceneComponentType::Terrain))
		{
			if (!entity.is_valid() || !entity.has_terrain_component())
			{
				continue;
			}
			const std::filesystem::path pathReference =
				entity.get_terrain_component().asset_path;
			const AshEngine::AssetInfo* pAsset =
				_context.refAssetDatabaseService.FindByPath(pathReference);
			if (!pAsset)
			{
				unresolvedTerrainReferences.push_back(pathReference);
				continue;
			}
			if (pAsset->id == dirtyAssetId)
			{
				referenced = true;
				break;
			}
		}
		if (!referenced && !unresolvedTerrainReferences.empty())
		{
			const TerrainAssetReferenceMatch match =
				pTerrainService->ClassifyCurrentAssetReferences(unresolvedTerrainReferences);
			if (match == TerrainAssetReferenceMatch::Current)
			{
				referenced = true;
			}
			else if (match == TerrainAssetReferenceMatch::Unsafe)
			{
				FailSceneSave(
					refPending.path,
					"Scene save cannot safely identify an unresolved Terrain reference inside the asset root.");
				return DirtyTerrainSaveStartResult::Failed;
			}
		}
		if (!referenced)
		{
			return DirtyTerrainSaveStartResult::NotRequired;
		}

		const TerrainFileOperationState& existing = pTerrainService->GetFileOperationState();
		if (pTerrainService->HasFileOperationInProgress())
		{
			if (existing.kind != TerrainFileOperationKind::Save ||
				existing.asset_id != dirtyAssetId)
			{
				FailSceneSave(
					refPending.path,
					"Scene save cannot wait on an unrelated Terrain file operation.");
				return DirtyTerrainSaveStartResult::Failed;
			}
			if (!pWorkingSet ||
				existing.content_generation != pWorkingSet->content_generation)
			{
				FailSceneSave(
					refPending.path,
					"Scene save cannot attach to an older Terrain generation; wait for the current Terrain save and retry.");
				return DirtyTerrainSaveStartResult::Failed;
			}
			refPending.terrain_operation_serial = existing.operation_serial;
			return DirtyTerrainSaveStartResult::Started;
		}

		TerrainEditorIntent save{};
		save.kind = TerrainEditorIntent::Kind::Save;
		if (!pTerrainService->SubmitIntent(save))
		{
			FailSceneSave(
				refPending.path,
				"Scene save could not start referenced Terrain '" +
				std::to_string(dirtyAssetId) + "': " + pTerrainService->GetLastError());
			return DirtyTerrainSaveStartResult::Failed;
		}

		refPending.terrain_operation_serial =
			pTerrainService->GetFileOperationState().operation_serial;
		return refPending.terrain_operation_serial != 0u
			? DirtyTerrainSaveStartResult::Started
			: DirtyTerrainSaveStartResult::Failed;
	}

	void EditorActionCoordinator::HandleSaveScene()
	{
		if (_optPendingSceneSave)
		{
			Notify(_context, "Scene save is already waiting for referenced Terrain data.");
			return;
		}
		const std::filesystem::path pathActiveScene = _context.refSceneService.GetActiveScenePath();
		const std::string& strSceneName = _context.refSceneService.GetActiveScene().get_name();
		const std::filesystem::path pathScene = pathActiveScene.empty()
			? MakeUniqueSceneAssetPath(
				_context.refSettingsService,
				strSceneName.empty() ? kUntitledSceneName : strSceneName)
			: pathActiveScene;
		if (pathScene.empty())
		{
			_context.refEventBus.Publish(EditorDocumentOperationEvent{
				EditorDocumentOperationKind::SaveScene,
				EditorDocumentOperationResult::Failed,
				_context.refSceneService.GetActiveScene().get_name(),
				{}
			});
			Notify(_context, "Failed to choose a save path for the scene.");
			return;
		}

		PendingSceneSave pending{};
		pending.path = pathScene;
		pending.scene_name = strSceneName;
		pending.scene_content_epoch =
			_context.refSceneService.GetActiveScene().get_content_epoch();
		pending.history_state_id =
			_context.refUndoRedoService.GetCurrentHistoryStateId();
		const DirtyTerrainSaveStartResult terrainStart = SaveDirtyReferencedTerrains(pending);
		if (terrainStart == DirtyTerrainSaveStartResult::Failed)
		{
			return;
		}
		if (terrainStart == DirtyTerrainSaveStartResult::Started)
		{
			_optPendingSceneSave = std::move(pending);
			Notify(_context, "Saving referenced Terrain data before the Scene...");
			return;
		}
		SaveSceneNow(pathScene, strSceneName);
	}

	bool EditorActionCoordinator::SaveSceneNow(
		const std::filesystem::path& pathScene,
		const std::string& strSceneName,
		const std::optional<uint64_t> optCapturedHistoryState)
	{
		if (_context.refSceneService.SaveScene(pathScene))
		{
			const bool canMarkCurrentHistorySaved =
				!optCapturedHistoryState.has_value() ||
				_context.refUndoRedoService.GetCurrentHistoryStateId() ==
					*optCapturedHistoryState;
			if (canMarkCurrentHistorySaved)
			{
				_context.refUndoRedoService.MarkSaved();
			}
			_context.refSettingsService.GetSettings().strLastScenePath =
				MakeScenePathForSettings(_context.refSettingsService, pathScene);
			_context.refSettingsService.RecordRecentScenePath(pathScene);
			_context.refSettingsService.Save();
			_context.refEventBus.Publish(EditorDocumentOperationEvent{
				EditorDocumentOperationKind::SaveScene,
				EditorDocumentOperationResult::Succeeded,
				strSceneName,
				pathScene.generic_string()
			});
			Notify(_context, "Scene saved to " + pathScene.generic_string());
			if (!canMarkCurrentHistorySaved)
			{
				Notify(_context, "Scene saved, but newer Editor commands remain unsaved.");
			}
			return true;
		}

		FailSceneSave(pathScene, "Failed to save scene.");
		return false;
	}

	void EditorActionCoordinator::FailSceneSave(
		const std::filesystem::path& pathScene,
		const std::string& message)
	{
		_context.refEventBus.Publish(EditorDocumentOperationEvent{
			EditorDocumentOperationKind::SaveScene,
			EditorDocumentOperationResult::Failed,
			_context.refSceneService.GetActiveScene().get_name(),
			pathScene.generic_string()
		});
		Notify(_context, message);
	}

	void EditorActionCoordinator::CancelPendingSceneSave(const char* reason)
	{
		if (!_optPendingSceneSave)
		{
			return;
		}
		const std::filesystem::path path = _optPendingSceneSave->path;
		_optPendingSceneSave.reset();
		FailSceneSave(
			path,
			std::string("Scene save canceled because ") +
				(reason ? reason : "the active document changed") + ".");
	}

	void EditorActionCoordinator::HandleRefreshAssets()
	{
		if (_context.refAssetDatabaseService.Refresh())
		{
			Notify(_context, "Asset database refreshed.");
			return;
		}

		Notify(_context, "Asset refresh skipped because the asset root was not found.");
	}

	void EditorActionCoordinator::HandleResetLayout()
	{
		_context.refDockLayoutController.RequestLayoutReset();
		_context.refViewportService.ResetPresentations();
		ViewportPanelStateBridge::ApplyViewportOpenStateToPanels(_context.refViewportService, _context.refPanelManager);
		_context.refEventBus.Publish(EditorViewportLayoutResetEvent{});
		Notify(_context, "Dockspace layout reset requested.");
	}

	void EditorActionCoordinator::HandleUndo()
	{
		if (_context.refUndoRedoService.Undo(_context.refEditorContext))
		{
			Notify(_context, "Undo executed.");
			return;
		}

		Notify(_context, "Undo failed.");
	}

	void EditorActionCoordinator::HandleRedo()
	{
		if (_context.refUndoRedoService.Redo(_context.refEditorContext))
		{
			Notify(_context, "Redo executed.");
			return;
		}

		Notify(_context, "Redo failed.");
	}

	bool EditorActionCoordinator::HasSelectedEntity() const
	{
		return GetSelectedEntityCount(_context) > 0u;
	}

	bool EditorActionCoordinator::HasSingleSelectedEntity() const
	{
		return GetSelectedEntityCount(_context) == 1u;
	}
}
