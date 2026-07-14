#pragma once

#include "Core/IAssetBrowserActionTarget.h"
#include "Core/IEditorActionHandler.h"
#include "Core/ISceneFileActionHandler.h"
#include "Core/ISceneHierarchyActionTarget.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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

	class EditorActionCoordinator final
		: public IEditorActionHandler
		, public ISceneFileActionHandler
	{
	public:
		explicit EditorActionCoordinator(EditorActionCoordinatorContext context);

		void BindPanelActionTargets(
			IAssetBrowserActionTarget* pAssetBrowserActionTarget,
			ISceneHierarchyActionTarget* pSceneHierarchyActionTarget);
		void RegisterActions();
		void Update();

		bool CanExecuteAction(std::string_view svActionId) const override;
		void ExecuteAction(std::string_view svActionId) override;
		bool OpenSceneFromDialog(const char* pSource) override;
		bool OpenSceneFromPath(const std::filesystem::path& pathScene, const char* pSource) override;

	private:
		enum class DirtyTerrainSaveStartResult : uint8_t
		{
			NotRequired = 0,
			Started,
			Failed
		};

		struct PendingSceneSave
		{
			std::filesystem::path path{};
			std::string scene_name{};
			uint64_t scene_content_epoch = 0u;
			uint64_t history_state_id = 0u;
			uint64_t terrain_operation_serial = 0u;
		};

		void HandleNewScene();
		void HandleOpenScene();
		void HandleReloadScene();
		void HandleSaveScene();
		void HandleRefreshAssets();
		void HandleResetLayout();
		void HandleUndo();
		void HandleRedo();
		DirtyTerrainSaveStartResult SaveDirtyReferencedTerrains(PendingSceneSave& refPending);
		bool SaveSceneNow(
			const std::filesystem::path& pathScene,
			const std::string& strSceneName,
			std::optional<uint64_t> optCapturedHistoryState = std::nullopt);
		void FailSceneSave(const std::filesystem::path& pathScene, const std::string& message);
		void CancelPendingSceneSave(const char* reason);

		bool HasSelectedEntity() const;
		bool HasSingleSelectedEntity() const;

	private:
		EditorActionCoordinatorContext _context;
		IAssetBrowserActionTarget* _pAssetBrowserActionTarget = nullptr;
		ISceneHierarchyActionTarget* _pSceneHierarchyActionTarget = nullptr;
		std::optional<PendingSceneSave> _optPendingSceneSave{};
	};
}
