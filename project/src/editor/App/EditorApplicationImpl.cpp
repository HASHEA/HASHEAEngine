#include "App/EditorApplicationImpl.h"

#include "App/EditorActionCoordinator.h"
#include "App/PanelBootstrapper.h"
#include "App/SceneWorkflowCoordinator.h"
#include "App/ViewportLayoutPersistence.h"
#include "App/ViewportPanelStateBridge.h"
#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EditorEventBindings.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Application.h"
#include "Function/Gui/UICommon.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/ScenePresentationSubsystem.h"
#include "Services/AssetDatabaseService.h"
#include "Services/AssetPreviewService.h"
#include "Services/CommandService.h"
#include "Services/DragDropTransferService.h"
#include "Services/EditorIconService.h"
#include "Services/EditorGizmoService.h"
#include "Services/EditorSessionStateService.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorShortcutService.h"
#include "Services/EditorViewportCameraService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include "Shell/DockLayoutController.h"
#include "Shell/EditorCommandPaletteController.h"
#include "Shell/EditorStatusBarController.h"
#include "Shell/MainMenuController.h"
#include "Shell/PanelManager.h"

namespace AshEditor
{
	EditorApplicationImpl::EditorApplicationImpl()
		: _upEventBus(std::make_unique<EditorEventBus>())
		, _upEventBindings(std::make_unique<EditorEventBindings>())
		, _upSettingsService(std::make_unique<EditorSettingsService>())
		, _upSelectionService(std::make_unique<SelectionService>())
		, _upSceneService(std::make_unique<SceneService>())
		, _upAssetDatabaseService(std::make_unique<AssetDatabaseService>())
		, _upAssetPreviewService(std::make_unique<AssetPreviewService>())
		, _upViewportService(std::make_unique<EditorViewportService>())
		, _upViewportCameraService(std::make_unique<EditorViewportCameraService>())
		, _upCommandService(std::make_unique<CommandService>())
		, _upShortcutService(std::make_unique<EditorShortcutService>())
		, _upUndoRedoService(std::make_unique<UndoRedoService>())
		, _upDragDropTransferService(std::make_unique<DragDropTransferService>())
		, _upSessionStateService(std::make_unique<EditorSessionStateService>())
		, _upIconService(std::make_unique<EditorIconService>())
		, _upGizmoService(std::make_unique<EditorGizmoService>())
		, _upPanelManager(std::make_unique<PanelManager>())
		, _upDockLayoutController(std::make_unique<DockLayoutController>())
		, _upStatusBarController(std::make_unique<EditorStatusBarController>())
		, _upCommandPaletteController(std::make_unique<EditorCommandPaletteController>())
		, _upMainMenuController(std::make_unique<MainMenuController>())
		, _upSceneWorkflowCoordinator(std::make_unique<SceneWorkflowCoordinator>())
	{
		_upActionCoordinator = std::make_unique<EditorActionCoordinator>(EditorActionCoordinatorContext{
			*_upSceneService,
			*_upSelectionService,
			*_upAssetDatabaseService,
			*_upViewportService,
			*_upCommandService,
			*_upSettingsService,
			*_upUndoRedoService,
			*_upPanelManager,
			*_upDockLayoutController,
			*_upCommandPaletteController,
			*_upSceneWorkflowCoordinator,
			*_upEventBus,
			_editorContext,
			*this
		});
	}

	EditorApplicationImpl::~EditorApplicationImpl() = default;

	std::filesystem::path EditorApplicationImpl::ResolveStartupScenePath(const EditorSettings& refSettings) const
	{
		return refSettings.strLastScenePath.empty()
			? _upSettingsService->GetStartupScenePath()
			: _upSettingsService->ResolveWorkspacePath(refSettings.strLastScenePath);
	}

	bool EditorApplicationImpl::Initialize()
	{
		if (_bInitialized)
		{
			return true;
		}

		const std::filesystem::path pathWorkspaceRoot = DiscoverEditorWorkspaceRoot();
		_upSettingsService->Initialize(pathWorkspaceRoot);

		const EditorSettings& refSettings = _upSettingsService->GetSettings();
		_upViewportCameraService->SetDefaultMoveSpeed(refSettings.fSceneViewportCameraSpeed);
		const std::filesystem::path pathStartupScene = ResolveStartupScenePath(refSettings);
		const bool bStartupSceneLoaded = CreateServices(pathWorkspaceRoot, pathStartupScene);

		WireServices();
		CreateViewports();
		LoadPersistentState();
		CreatePanels();
		PublishInitialSessionState();
		RegisterActions();
		PublishInitialNotifications(bStartupSceneLoaded, pathStartupScene);

		_bInitialized = true;
		return true;
	}

	void EditorApplicationImpl::Shutdown()
	{
		if (!_bInitialized)
		{
			return;
		}

		_upViewportService->DestroyScenePresentations(AshEngine::Application::get_scene_presentation());
		ShutdownPanels();
		SavePersistentState();
		_upViewportCameraService->Reset();
		_upViewportService->Clear();
		_upUndoRedoService->Clear();
		_upSessionStateService->Clear();
		_upEventBindings->Clear();
		_upEventBus->Clear();
		_upIconService->Shutdown(_editorContext.pUiContext);
		_upSettingsService->Save();
		_bInitialized = false;
	}

	void EditorApplicationImpl::Update()
	{
		RefreshUiContext();
		_upPanelManager->Update();

		const bool bDragging = _editorContext.pUiContext && _editorContext.pUiContext->has_drag_drop_payload();
		_upDragDropTransferService->GarbageCollect(bDragging);
	}

	void EditorApplicationImpl::DrawGui()
	{
		if (!_editorContext.bGuiRendererReady || !_editorContext.pUiContext || !_editorContext.pUiContext->is_frame_active())
		{
			return;
		}

		HandleGlobalShortcuts();
		MainMenuControllerContext mainMenuContext{
			*_editorContext.pUiContext,
			*_upCommandService,
			*_upSessionStateService,
			*_upSettingsService,
			_upPanelManager->GetPanels(),
			this,
			_upActionCoordinator.get(),
			_upPanelManager.get(),
			this
		};
		_upMainMenuController->Draw(mainMenuContext);

		const float fStatusBarHeight = _upStatusBarController->GetPreferredHeight(*_editorContext.pUiContext);
		_upDockLayoutController->DrawWorkspaceHost(*_editorContext.pUiContext, fStatusBarHeight);
		_upPanelManager->DrawGui(MakeEditorFrameContext(_editorContext));

		EditorStatusBarContext statusBarContext{
			*_editorContext.pUiContext,
			*_upSessionStateService
		};
		_upStatusBarController->Draw(statusBarContext);

		EditorCommandPaletteContext commandPaletteContext{
			*_editorContext.pUiContext,
			*_upCommandService
		};
		_upCommandPaletteController->Draw(commandPaletteContext);
	}

	void EditorApplicationImpl::SyncRuntimeScenePresentations()
	{
		AshEngine::Renderer* pRenderer = AshEngine::Application::get_renderer();
		AshEngine::ScenePresentationSubsystem* pScenePresentation = AshEngine::Application::get_scene_presentation();
		if (!pRenderer || !pScenePresentation)
		{
			return;
		}

		AshEngine::AssetDatabase& refAssetDatabase = _upAssetDatabaseService->GetDatabase();
		AshEngine::Application::get()->get_render_asset_manager().initialize(&refAssetDatabase, pRenderer);
		_upViewportCameraService->SyncFromScene(*_upSceneService, *_upAssetDatabaseService);
		if (!_upViewportService->SyncScenePresentations(
			*pScenePresentation,
			_upSceneService->GetActiveScene(),
			_upViewportCameraService.get()))
		{
			HLogError("Editor failed to synchronize scene viewport presentation bindings.");
		}
	}

	EditorViewportInstance* EditorApplicationImpl::GetPrimaryViewport()
	{
		return _upViewportService->GetPrimaryViewport();
	}

	const EditorViewportInstance* EditorApplicationImpl::GetPrimaryViewport() const
	{
		return _upViewportService->GetPrimaryViewport();
	}

	EditorViewportService& EditorApplicationImpl::GetViewportService()
	{
		return *_upViewportService;
	}

	const EditorViewportService& EditorApplicationImpl::GetViewportService() const
	{
		return *_upViewportService;
	}

	bool EditorApplicationImpl::CreateServices(
		const std::filesystem::path& pathWorkspaceRoot,
		const std::filesystem::path& pathStartupScene)
	{
		const bool bStartupSceneLoaded = _upSceneService->Initialize(pathStartupScene);
		_upAssetDatabaseService->SetAssetRoot(_upSettingsService->GetAssetsRootPath());
		_upAssetDatabaseService->Refresh();
		_upIconService->Initialize(pathWorkspaceRoot);
		return bStartupSceneLoaded;
	}

	void EditorApplicationImpl::WireServices()
	{
		_upSelectionService->SetEventBus(_upEventBus.get());
		_upCommandService->SetEventBus(_upEventBus.get());
		_upUndoRedoService->SetEventBus(_upEventBus.get());
		_upViewportService->SetEventBus(_upEventBus.get());
		_upSessionStateService->BindEventBus(_upEventBus.get());
		_upPanelManager->BindEventBus(_upEventBus.get());

		_editorContext.pSelectionService = _upSelectionService.get();
		_editorContext.pSceneService = _upSceneService.get();
		RefreshUiContext();

		SceneWorkflowContext sceneWorkflowContext{
			*_upSceneService,
			*_upSelectionService,
			*_upUndoRedoService,
			*_upSettingsService,
			_upEventBus.get(),
			this
		};
		_upSceneWorkflowCoordinator->ResetEditorStateAfterSceneChange(sceneWorkflowContext);
		if (_editorContext.pUiContext)
		{
			_editorContext.pUiContext->apply_theme(_upSettingsService->GetSettings().strUiThemePreset);
		}
	}

	void EditorApplicationImpl::CreateViewports()
	{
		_upViewportService->EnsureViewport(EditorViewportIds::Scene, EditorWindowTitles::Scene);
		_upViewportService->EnsureViewport(EditorViewportIds::Game, EditorWindowTitles::Game);
		_upViewportService->SetPrimaryViewport(EditorViewportIds::Scene);
	}

	void EditorApplicationImpl::LoadPersistentState()
	{
		ViewportLayoutPersistence::Load(
			*_upSettingsService,
			*_upViewportService,
			this);
	}

	void EditorApplicationImpl::SavePersistentState()
	{
		ViewportLayoutPersistence::Save(*_upSettingsService, *_upViewportService);
	}

	void EditorApplicationImpl::CreatePanels()
	{
		const PanelBootstrapContext bootstrapContext{
			_upSelectionService.get(),
			_upSceneService.get(),
			_upAssetDatabaseService.get(),
			_upAssetPreviewService.get(),
			_upCommandService.get(),
			this,
			_upSettingsService.get(),
			_upIconService.get(),
			_upShortcutService.get(),
			_upViewportService.get(),
			_upViewportCameraService.get(),
			_upDragDropTransferService.get(),
			_upGizmoService.get(),
			&_gizmoState
		};
		const PanelBootstrapResult bootstrapResult =
			PanelBootstrapper::CreateDefaultPanels(*_upPanelManager, bootstrapContext, *_upEventBus);
		_upActionCoordinator->BindPanelActionTargets(
			bootstrapResult.pAssetBrowserActionTarget,
			bootstrapResult.pSceneHierarchyActionTarget);
		ViewportPanelStateBridge::Bind(*_upEventBindings, *_upEventBus, *_upViewportService);
		ViewportPanelStateBridge::ApplyViewportOpenStateToPanels(*_upViewportService, *_upPanelManager);
	}

	void EditorApplicationImpl::ShutdownPanels()
	{
		_upPanelManager->Shutdown();
		_upDockLayoutController->ClearRuntimeState();
	}

	void EditorApplicationImpl::RegisterActions()
	{
		_upActionCoordinator->RegisterActions();
	}

	void EditorApplicationImpl::RefreshUiContext()
	{
		_editorContext.pUiContext = AshEngine::Application::get_ui_context();
		_editorContext.bGuiRendererReady = _editorContext.pUiContext != nullptr && _editorContext.pUiContext->is_initialized();
	}

	void EditorApplicationImpl::PublishInitialSessionState()
	{
		_upSessionStateService->SyncFromServices(*_upSceneService, *_upUndoRedoService);
		_upSessionStateService->SyncFromPanelManager(*_upPanelManager);
		_upEventBus->Publish(EditorActiveSceneChangedEvent{
			_upSceneService->GetActiveScene().get_name(),
			_upSceneService->GetActiveScenePath().generic_string()
		});
	}

	void EditorApplicationImpl::PublishInitialNotifications(bool bStartupSceneLoaded, const std::filesystem::path& pathStartupScene)
	{
		if (!bStartupSceneLoaded && !pathStartupScene.empty())
		{
			Notify("Failed to load startup scene. Editor fell back to a new default scene.");
		}

		if (!_editorContext.bGuiRendererReady)
		{
			Notify("Engine UIContext is not ready. Editor panels will stay idle until the runtime UI layer is available.");
		}

		Notify("Scene loaded: " + _upSceneService->GetActiveScene().get_name());
		Notify("Asset scan complete: " + std::to_string(_upAssetDatabaseService->GetItems().size()) + " items.");
	}

	void EditorApplicationImpl::Notify(const std::string& strMessage, const char* pSource)
	{
		if (!strMessage.empty())
		{
			_upEventBus->Publish(EditorNotificationEvent{ strMessage, pSource ? pSource : "Editor" });
		}
	}

	bool EditorApplicationImpl::InvokeAction(const char* pActionId, const char* pSource)
	{
		return pActionId ? _upCommandService->Invoke(pActionId, pSource ? pSource : "unknown") : false;
	}

	bool EditorApplicationImpl::ExecuteCommand(std::unique_ptr<EditorCommand> upCommand)
	{
		return _upUndoRedoService->Execute(std::move(upCommand), _editorContext);
	}

	bool EditorApplicationImpl::BeginCommandTransaction(const char* pLabel)
	{
		return _upUndoRedoService->BeginTransaction(pLabel ? pLabel : "Editor Transaction");
	}

	bool EditorApplicationImpl::CommitCommandTransaction()
	{
		return _upUndoRedoService->CommitTransaction();
	}

	void EditorApplicationImpl::CancelCommandTransaction()
	{
		_upUndoRedoService->CancelTransaction(_editorContext);
	}

	void EditorApplicationImpl::HandleGlobalShortcuts()
	{
		if (!_editorContext.pUiContext || !_editorContext.pUiContext->is_frame_active())
		{
			return;
		}

		_upShortcutService->DispatchScope(*_upCommandService, EditorActionScope::Global, *_editorContext.pUiContext);
	}

	void EditorApplicationImpl::ApplyTheme(std::string_view svThemeId, std::string_view svThemeLabel)
	{
		if (!_editorContext.pUiContext || svThemeId.empty())
		{
			return;
		}

		if (!_editorContext.pUiContext->apply_theme(svThemeId))
		{
			Notify(std::string("Failed to switch theme to ") + std::string(svThemeId) + ".");
			return;
		}

		EditorSettings& settings = _upSettingsService->GetSettings();
		settings.strUiThemePreset = std::string(svThemeId);
		_upSettingsService->Save();
		const std::string strThemeLabel = svThemeLabel.empty() ? std::string(svThemeId) : std::string(svThemeLabel);
		Notify(std::string("Theme switched to ") + strThemeLabel + ".");
	}
}
