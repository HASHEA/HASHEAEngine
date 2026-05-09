#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace AshEditor
{
	enum class EditorDocumentOperationKind : uint8_t;
	enum class EditorDocumentOperationResult : uint8_t;
	class EditorEventBus;
	class EditorSettingsService;
	class INotificationSink;
	class SceneService;
	class SelectionService;
	class UndoRedoService;

	struct SceneWorkflowContext
	{
		SceneService& refSceneService;
		SelectionService& refSelectionService;
		UndoRedoService& refUndoRedoService;
		EditorSettingsService& refSettingsService;
		EditorEventBus* pEventBus = nullptr;
		INotificationSink* pNotificationSink = nullptr;
	};

	enum class SceneReloadResult : uint8_t
	{
		Skipped = 0,
		Reloaded,
		// Reload failed, but the editor already recovered into a usable default scene.
		FallbackActivated
	};

	class SceneWorkflowCoordinator final
	{
	public:
		// Scene switches must reset selection and undo/redo through one shared path.
		void ResetEditorStateAfterSceneChange(SceneWorkflowContext& context) const;
		void ActivateNewScene(SceneWorkflowContext& context, const std::string& strSceneName) const;
		bool LoadSceneIntoEditor(SceneWorkflowContext& context, const std::filesystem::path& pathScene) const;
		// Callers must branch on SceneReloadResult instead of assuming bool-style success/failure semantics.
		SceneReloadResult ReloadActiveScene(SceneWorkflowContext& context) const;

	private:
		void UpdateLastScenePathSetting(SceneWorkflowContext& context, const std::filesystem::path& pathScene) const;
		void SelectDefaultEntity(SceneWorkflowContext& context) const;
		void PublishActiveSceneChanged(SceneWorkflowContext& context) const;
		void PublishDocumentOperation(
			SceneWorkflowContext& context,
			EditorDocumentOperationKind eKind,
			EditorDocumentOperationResult eResult,
			const std::filesystem::path& pathTarget) const;
	};
}
