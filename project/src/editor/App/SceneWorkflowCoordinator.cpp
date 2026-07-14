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
#include "Services/TerrainEditorService.h"
#include "Services/UndoRedoService.h"

#include <fstream>
#include <json.hpp>
#include <system_error>
#include <vector>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;

		std::string MakeSceneDisplayNameFromPath(const std::filesystem::path& pathScene)
		{
			const std::filesystem::path pathFirstStem = pathScene.stem();
			const std::filesystem::path pathSecondStem = pathFirstStem.stem();
			const std::string strDisplayName = pathSecondStem.empty()
				? pathFirstStem.string()
				: pathSecondStem.string();
			return strDisplayName.empty() ? kUntitledSceneName : strDisplayName;
		}

		bool WriteSceneTemplateCopy(
			const std::filesystem::path& pathTemplateScene,
			const std::filesystem::path& pathDestinationScene)
		{
			std::ifstream input(pathTemplateScene);
			if (!input.is_open())
			{
				return false;
			}

			json root = json::parse(input, nullptr, false);
			if (root.is_discarded())
			{
				return false;
			}

			root["name"] = MakeSceneDisplayNameFromPath(pathDestinationScene);

			std::error_code errorCode{};
			std::filesystem::create_directories(pathDestinationScene.parent_path(), errorCode);
			if (errorCode)
			{
				return false;
			}

			std::ofstream output(pathDestinationScene, std::ios::out | std::ios::trunc);
			if (!output.is_open())
			{
				return false;
			}

			output << root.dump(2);
			return output.good();
		}
	}

	bool SceneWorkflowCoordinator::PrepareTerrainForSceneChange(
		SceneWorkflowContext& context) const
	{
		return !context.pTerrainEditorService ||
			context.pTerrainEditorService->PrepareForSceneChange();
	}

	void SceneWorkflowCoordinator::CommitTerrainSceneChange(
		SceneWorkflowContext& context) const
	{
		if (context.pTerrainEditorService)
		{
			context.pTerrainEditorService->CommitSceneChange();
		}
	}

	void SceneWorkflowCoordinator::ResetEditorStateAfterSceneChange(SceneWorkflowContext& context) const
	{
		// Terrain commands must lose their mutable session before the shared Scene history is cleared.
		CommitTerrainSceneChange(context);
		context.refSelectionService.Clear();
		context.refUndoRedoService.Clear();
		SelectDefaultEntity(context);
	}

	void SceneWorkflowCoordinator::ActivateNewScenePrepared(
		SceneWorkflowContext& context,
		const std::string& strSceneName) const
	{
		context.refSceneService.NewScene(strSceneName);
		UpdateLastScenePathSetting(context, {});
		ResetEditorStateAfterSceneChange(context);
		PublishActiveSceneChanged(context);
		PublishDocumentOperation(context, EditorDocumentOperationKind::NewScene, EditorDocumentOperationResult::Succeeded, {});
	}

	bool SceneWorkflowCoordinator::ActivateNewScene(
		SceneWorkflowContext& context,
		const std::string& strSceneName) const
	{
		if (!PrepareTerrainForSceneChange(context))
		{
			PublishDocumentOperation(
				context,
				EditorDocumentOperationKind::NewScene,
				EditorDocumentOperationResult::Skipped,
				{});
			if (context.pNotificationSink)
			{
				context.pNotificationSink->Notify(
					context.pTerrainEditorService && !context.pTerrainEditorService->GetLastError().empty()
						? context.pTerrainEditorService->GetLastError()
						: "New Scene is blocked by the active Terrain authoring session.");
			}
			return false;
		}

		ActivateNewScenePrepared(context, strSceneName);
		return true;
	}

	std::filesystem::path SceneWorkflowCoordinator::CreateNewSceneFromStartupTemplate(
		SceneWorkflowContext& context,
		std::string_view svSceneName) const
	{
		if (!PrepareTerrainForSceneChange(context))
		{
			PublishDocumentOperation(
				context,
				EditorDocumentOperationKind::NewScene,
				EditorDocumentOperationResult::Skipped,
				{});
			return {};
		}
		const std::filesystem::path pathTemplateScene = context.refSettingsService.GetStartupScenePath();
		if (pathTemplateScene.empty())
		{
			return {};
		}

		std::error_code errorCode{};
		if (!std::filesystem::exists(pathTemplateScene, errorCode) || errorCode)
		{
			return {};
		}

		const std::filesystem::path pathScene = MakeUniqueSceneAssetPath(context.refSettingsService, svSceneName);
		if (pathScene.empty())
		{
			return {};
		}

		if (!WriteSceneTemplateCopy(pathTemplateScene, pathScene))
		{
			return {};
		}

		if (!LoadSceneIntoEditorPrepared(context, pathScene, EditorDocumentOperationKind::NewScene))
		{
			std::filesystem::remove(pathScene, errorCode);
			return {};
		}

		return pathScene;
	}

	bool SceneWorkflowCoordinator::LoadSceneIntoEditor(
		SceneWorkflowContext& context,
		const std::filesystem::path& pathScene) const
	{
		if (pathScene.empty())
		{
			PublishDocumentOperation(
				context,
				EditorDocumentOperationKind::LoadScene,
				EditorDocumentOperationResult::Skipped,
				pathScene);
			return false;
		}
		if (!PrepareTerrainForSceneChange(context))
		{
			PublishDocumentOperation(
				context,
				EditorDocumentOperationKind::LoadScene,
				EditorDocumentOperationResult::Skipped,
				pathScene);
			if (context.pNotificationSink)
			{
				context.pNotificationSink->Notify(
					context.pTerrainEditorService && !context.pTerrainEditorService->GetLastError().empty()
						? context.pTerrainEditorService->GetLastError()
						: "Open Scene is blocked by the active Terrain authoring session.");
			}
			return false;
		}
		return LoadSceneIntoEditorPrepared(
			context,
			pathScene,
			EditorDocumentOperationKind::LoadScene);
	}

	bool SceneWorkflowCoordinator::LoadSceneIntoEditorPrepared(
		SceneWorkflowContext& context,
		const std::filesystem::path& pathScene,
		EditorDocumentOperationKind eDocumentOperationKind) const
	{
		if (pathScene.empty())
		{
			PublishDocumentOperation(context, eDocumentOperationKind, EditorDocumentOperationResult::Skipped, pathScene);
			return false;
		}

		if (!context.refSceneService.LoadScene(pathScene))
		{
			PublishDocumentOperation(context, eDocumentOperationKind, EditorDocumentOperationResult::Failed, pathScene);
			return false;
		}

		UpdateLastScenePathSetting(context, pathScene);
		ResetEditorStateAfterSceneChange(context);
		PublishActiveSceneChanged(context);
		PublishDocumentOperation(context, eDocumentOperationKind, EditorDocumentOperationResult::Succeeded, pathScene);
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
		if (!PrepareTerrainForSceneChange(context))
		{
			PublishDocumentOperation(
				context,
				EditorDocumentOperationKind::ReloadScene,
				EditorDocumentOperationResult::Skipped,
				pathActiveScene);
			if (context.pNotificationSink)
			{
				context.pNotificationSink->Notify(
					context.pTerrainEditorService && !context.pTerrainEditorService->GetLastError().empty()
						? context.pTerrainEditorService->GetLastError()
						: "Reload Scene is blocked by the active Terrain authoring session.");
			}
			return SceneReloadResult::Skipped;
		}

		const std::string strActiveScenePath = pathActiveScene.generic_string();
		if (LoadSceneIntoEditorPrepared(
				context,
				pathActiveScene,
				EditorDocumentOperationKind::ReloadScene))
		{
			HLogInfo("Scene reloaded from '{}'.", strActiveScenePath);
			if (context.pNotificationSink)
			{
				context.pNotificationSink->Notify("Scene reloaded from " + strActiveScenePath);
			}
			return SceneReloadResult::Reloaded;
		}

		HLogWarning(
			"Failed to reload scene from '{}'. The current in-memory scene was preserved.",
			strActiveScenePath);
		if (context.pNotificationSink)
		{
			context.pNotificationSink->Notify(
				"Failed to reload scene from " +
				strActiveScenePath +
				". The current in-memory scene was preserved.");
		}
		return SceneReloadResult::Failed;
	}

	void SceneWorkflowCoordinator::UpdateLastScenePathSetting(
		SceneWorkflowContext& context,
		const std::filesystem::path& pathScene) const
	{
		EditorSettings& refSettings = context.refSettingsService.GetSettings();
		refSettings.strLastScenePath =
			MakeScenePathForSettings(context.refSettingsService, pathScene);
		context.refSettingsService.RecordRecentScenePath(pathScene);
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
