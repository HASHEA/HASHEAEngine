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
#include "Services/UndoRedoService.h"
#include "Shell/DockLayoutController.h"
#include "Shell/EditorCommandPaletteController.h"
#include "Shell/PanelManager.h"

#include <cstring>
#include <filesystem>
#include <string>

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

		uint64_t GetSelectedEntityId(const EditorActionCoordinatorContext& context)
		{
			const EditorSelection& refSelection = context.refSelectionService.GetSelection();
			return refSelection.eKind == EditorSelectionKind::Entity ? refSelection.uId : 0;
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

	bool EditorActionCoordinator::CanExecuteAction(std::string_view svActionId) const
	{
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
			svActionId == EditorActionIds::SelectionReparent ||
			svActionId == EditorActionIds::SelectionDuplicate ||
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
		SceneWorkflowContext sceneWorkflowContext = MakeSceneWorkflowContext(_context);
		_context.refSceneWorkflowCoordinator.ReloadActiveScene(sceneWorkflowContext);
	}

	void EditorActionCoordinator::HandleSaveScene()
	{
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
		if (_context.refSceneService.SaveScene(pathScene))
		{
			_context.refUndoRedoService.MarkSaved();
			_context.refSettingsService.GetSettings().strLastScenePath =
				MakeScenePathForSettings(_context.refSettingsService, pathScene);
			_context.refSettingsService.RecordRecentScenePath(pathScene);
			_context.refSettingsService.Save();
			_context.refEventBus.Publish(EditorDocumentOperationEvent{
				EditorDocumentOperationKind::SaveScene,
				EditorDocumentOperationResult::Succeeded,
				_context.refSceneService.GetActiveScene().get_name(),
				pathScene.generic_string()
			});
			Notify(_context, "Scene saved to " + pathScene.generic_string());
			return;
		}

		_context.refEventBus.Publish(EditorDocumentOperationEvent{
			EditorDocumentOperationKind::SaveScene,
			EditorDocumentOperationResult::Failed,
			_context.refSceneService.GetActiveScene().get_name(),
			pathScene.generic_string()
		});
		Notify(_context, "Failed to save scene.");
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
		return GetSelectedEntityId(_context) != 0;
	}
}
