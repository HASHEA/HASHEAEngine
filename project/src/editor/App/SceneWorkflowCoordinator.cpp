#include "App/SceneWorkflowCoordinator.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorScenePathUtils.h"
#include "Core/INotificationSink.h"
#include "Services/EditorSettingsService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"

#include <vector>

namespace AshEditor
{
	void SceneWorkflowCoordinator::ResetEditorStateAfterSceneChange(SceneWorkflowContext& context) const
	{
		// Keep selection/undo reset centralized so reload/new/load all leave the editor in the same lifecycle state.
		context.refSelectionService.Clear();
		context.refUndoRedoService.Clear();
		SelectDefaultEntity(context);
	}

	void SceneWorkflowCoordinator::ActivateNewScene(SceneWorkflowContext& context, const std::string& strSceneName) const
	{
		context.refSceneService.NewScene(strSceneName);
		UpdateLastScenePathSetting(context, {});
		ResetEditorStateAfterSceneChange(context);
		PublishActiveSceneChanged(context);
		PublishDocumentOperation(context, EditorDocumentOperationKind::NewScene, EditorDocumentOperationResult::Succeeded, {});
	}

	bool SceneWorkflowCoordinator::LoadSceneIntoEditor(
		SceneWorkflowContext& context,
		const std::filesystem::path& pathScene) const
	{
		if (pathScene.empty())
		{
			PublishDocumentOperation(context, EditorDocumentOperationKind::LoadScene, EditorDocumentOperationResult::Skipped, pathScene);
			return false;
		}

		if (!context.refSceneService.LoadScene(pathScene))
		{
			PublishDocumentOperation(context, EditorDocumentOperationKind::LoadScene, EditorDocumentOperationResult::Failed, pathScene);
			return false;
		}

		UpdateLastScenePathSetting(context, pathScene);
		ResetEditorStateAfterSceneChange(context);
		PublishActiveSceneChanged(context);
		PublishDocumentOperation(context, EditorDocumentOperationKind::LoadScene, EditorDocumentOperationResult::Succeeded, pathScene);
		return true;
	}

	SceneReloadResult SceneWorkflowCoordinator::ReloadActiveScene(SceneWorkflowContext& context) const
	{
		const std::filesystem::path pathActiveScene = context.refSceneService.GetActiveScenePath();
		if (pathActiveScene.empty())
		{
			PublishDocumentOperation(context, EditorDocumentOperationKind::ReloadScene, EditorDocumentOperationResult::Skipped, pathActiveScene);
			HLogInfo("Scene reload skipped because there is no saved active scene path.");
			if (context.pNotificationSink)
			{
				context.pNotificationSink->Notify("Reload Scene skipped because the active scene has not been saved yet.");
			}
			return SceneReloadResult::Skipped;
		}

		const std::string strActiveScenePath = pathActiveScene.generic_string();
		if (LoadSceneIntoEditor(context, pathActiveScene))
		{
			HLogInfo("Scene reloaded from '{}'.", strActiveScenePath);
			if (context.pNotificationSink)
			{
				context.pNotificationSink->Notify("Scene reloaded from " + strActiveScenePath);
			}
			return SceneReloadResult::Reloaded;
		}

		ActivateNewScene(context, kUntitledSceneName);
		PublishDocumentOperation(
			context,
			EditorDocumentOperationKind::ReloadScene,
			EditorDocumentOperationResult::FallbackActivated,
			pathActiveScene);
		HLogWarning(
			"Failed to reload scene from '{}'. Editor activated a new default scene as fallback.",
			strActiveScenePath);
		if (context.pNotificationSink)
		{
			context.pNotificationSink->Notify(
				"Failed to reload scene from " +
				strActiveScenePath +
				". Editor fell back to a new default scene.");
		}
		return SceneReloadResult::FallbackActivated;
	}

	void SceneWorkflowCoordinator::UpdateLastScenePathSetting(
		SceneWorkflowContext& context,
		const std::filesystem::path& pathScene) const
	{
		EditorSettings& refSettings = context.refSettingsService.GetSettings();
		refSettings.strLastScenePath =
			MakeScenePathForSettings(context.refSettingsService, pathScene);
		context.refSettingsService.Save();
	}

	void SceneWorkflowCoordinator::SelectDefaultEntity(SceneWorkflowContext& context) const
	{
		const std::vector<AshEngine::Entity>& vecEntities = context.refSceneService.GetActiveScene().get_entities();
		if (vecEntities.empty())
		{
			context.refSelectionService.Clear();
			return;
		}

		const AshEngine::Entity& refEntity = vecEntities.front();
		context.refSelectionService.Select({
			EditorSelectionKind::Entity,
			refEntity.get_id(),
			refEntity.get_name(),
			{}
		});
	}

	void SceneWorkflowCoordinator::PublishActiveSceneChanged(SceneWorkflowContext& context) const
	{
		if (!context.pEventBus)
		{
			return;
		}

		EditorActiveSceneChangedEvent event{};
		event.strSceneName = context.refSceneService.GetActiveScene().get_name();
		event.strScenePath = context.refSceneService.GetActiveScenePath().generic_string();
		context.pEventBus->Publish(event);
	}

	void SceneWorkflowCoordinator::PublishDocumentOperation(
		SceneWorkflowContext& context,
		EditorDocumentOperationKind eKind,
		EditorDocumentOperationResult eResult,
		const std::filesystem::path& pathTarget) const
	{
		if (!context.pEventBus)
		{
			return;
		}

		EditorDocumentOperationEvent event{};
		event.eKind = eKind;
		event.eResult = eResult;
		event.strDocumentName = context.refSceneService.GetActiveScene().get_name();
		event.strDocumentPath = pathTarget.generic_string();
		context.pEventBus->Publish(event);
	}
}
