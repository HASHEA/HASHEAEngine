#pragma once

#include "Core/EditorContext.h"
#include "Core/EditorGizmoTypes.h"
#include "Core/IActionInvoker.h"
#include "Core/IEditorCommandExecutor.h"
#include "Core/INotificationSink.h"
#include "Core/IThemeApplier.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace AshEngine
{
	enum class UIThemePreset : uint8_t;
}

namespace AshEditor
{
	class AssetDatabaseService;
	class AssetPreviewService;
	class CommandService;
	class DragDropTransferService;
	class EditorActionCoordinator;
	class EditorEventBindings;
	class EditorEventBus;
	class DockLayoutController;
	class EditorCommandPaletteController;
	class EditorCommand;
	class IEditorIconService;
	class EditorSessionStateService;
	struct EditorSettings;
	class EditorSettingsService;
	class EditorShortcutService;
	class EditorViewportCameraService;
	class EditorStatusBarController;
	struct EditorViewportInstance;
	class EditorViewportService;
	class MainMenuController;
	class PanelManager;
	class SceneService;
	class SceneWorkflowCoordinator;
	class SelectionService;
	class UndoRedoService;

	class EditorApplicationImpl final
		: public IActionInvoker
		, public IEditorCommandExecutor
		, public INotificationSink
		, public IThemeApplier
	{
	public:
		EditorApplicationImpl();
		~EditorApplicationImpl();

		EditorApplicationImpl(const EditorApplicationImpl&) = delete;
		EditorApplicationImpl& operator=(const EditorApplicationImpl&) = delete;

		bool Initialize();
		void Shutdown();

		void Update();
		void DrawGui();
		void SyncRuntimeScenePresentations();
		EditorViewportInstance* GetPrimaryViewport();
		const EditorViewportInstance* GetPrimaryViewport() const;
		EditorViewportService& GetViewportService();
		const EditorViewportService& GetViewportService() const;

	private:
		std::filesystem::path ResolveStartupScenePath(const EditorSettings& refSettings) const;
		bool CreateServices(const std::filesystem::path& pathWorkspaceRoot, const std::filesystem::path& pathStartupScene);
		void WireServices();
		void CreateViewports();
		void LoadPersistentState();
		void SavePersistentState();
		void CreatePanels();
		void ShutdownPanels();
		void RegisterActions();
		void RefreshUiContext();
		void PublishInitialSessionState();
		void PublishInitialNotifications(bool bStartupSceneLoaded, const std::filesystem::path& pathStartupScene);
		void Notify(const std::string& strMessage, const char* pSource = "Editor") override;
		bool InvokeAction(const char* pActionId, const char* pSource) override;
		bool ExecuteCommand(std::unique_ptr<EditorCommand> upCommand) override;
		void HandleGlobalShortcuts();
		void ApplyThemePreset(AshEngine::UIThemePreset preset) override;

	private:
		std::unique_ptr<EditorEventBus> _upEventBus{};
		std::unique_ptr<EditorEventBindings> _upEventBindings{};
		std::unique_ptr<EditorSettingsService> _upSettingsService{};
		std::unique_ptr<SelectionService> _upSelectionService{};
		std::unique_ptr<SceneService> _upSceneService{};
		std::unique_ptr<AssetDatabaseService> _upAssetDatabaseService{};
		std::unique_ptr<AssetPreviewService> _upAssetPreviewService{};
		std::unique_ptr<EditorViewportService> _upViewportService{};
		std::unique_ptr<EditorViewportCameraService> _upViewportCameraService{};
		std::unique_ptr<CommandService> _upCommandService{};
		std::unique_ptr<EditorShortcutService> _upShortcutService{};
		std::unique_ptr<UndoRedoService> _upUndoRedoService{};
		std::unique_ptr<DragDropTransferService> _upDragDropTransferService{};
		std::unique_ptr<EditorSessionStateService> _upSessionStateService{};
		std::unique_ptr<IEditorIconService> _upIconService{};
		std::unique_ptr<PanelManager> _upPanelManager{};
		std::unique_ptr<DockLayoutController> _upDockLayoutController{};
		std::unique_ptr<EditorStatusBarController> _upStatusBarController{};
		std::unique_ptr<EditorCommandPaletteController> _upCommandPaletteController{};
		std::unique_ptr<MainMenuController> _upMainMenuController{};
		std::unique_ptr<SceneWorkflowCoordinator> _upSceneWorkflowCoordinator{};
		std::unique_ptr<EditorActionCoordinator> _upActionCoordinator{};
		EditorContext _editorContext{};
		EditorGizmoState _gizmoState{};
		bool _bInitialized = false;
	};
}
