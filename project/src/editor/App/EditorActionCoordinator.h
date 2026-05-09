#pragma once

#include "Core/IAssetBrowserActionTarget.h"
#include "Core/IEditorActionHandler.h"
#include "Core/ISceneHierarchyActionTarget.h"

#include <cstdint>
#include <string_view>

namespace AshEditor
{
	class AssetDatabaseService;
	class CommandService;
	class DockLayoutController;
	class EditorCommandPaletteController;
	struct EditorContext;
	class EditorEventBus;
	class EditorSettingsService;
	class EditorViewportService;
	class INotificationSink;
	class PanelManager;
	class SceneService;
	class SceneWorkflowCoordinator;
	class SelectionService;
	class UndoRedoService;

	struct EditorActionCoordinatorContext
	{
		SceneService& refSceneService;
		SelectionService& refSelectionService;
		AssetDatabaseService& refAssetDatabaseService;
		EditorViewportService& refViewportService;
		CommandService& refCommandService;
		EditorSettingsService& refSettingsService;
		UndoRedoService& refUndoRedoService;
		PanelManager& refPanelManager;
		DockLayoutController& refDockLayoutController;
		EditorCommandPaletteController& refCommandPaletteController;
		SceneWorkflowCoordinator& refSceneWorkflowCoordinator;
		EditorEventBus& refEventBus;
		EditorContext& refEditorContext;
		INotificationSink& refNotificationSink;
	};

	class EditorActionCoordinator final : public IEditorActionHandler
	{
	public:
		explicit EditorActionCoordinator(EditorActionCoordinatorContext context);

		void BindPanelActionTargets(
			IAssetBrowserActionTarget* pAssetBrowserActionTarget,
			ISceneHierarchyActionTarget* pSceneHierarchyActionTarget);
		void RegisterActions();

		bool CanExecuteAction(std::string_view svActionId) const override;
		void ExecuteAction(std::string_view svActionId) override;

	private:
		void HandleNewScene();
		void HandleReloadScene();
		void HandleSaveScene();
		void HandleRefreshAssets();
		void HandleResetLayout();
		void HandleUndo();
		void HandleRedo();

		bool HasSelectedEntity() const;

	private:
		EditorActionCoordinatorContext _context;
		IAssetBrowserActionTarget* _pAssetBrowserActionTarget = nullptr;
		ISceneHierarchyActionTarget* _pSceneHierarchyActionTarget = nullptr;
	};
}
