#include "Panels/Inspector/EnvironmentComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorAssetPathWidgets.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <string>

namespace AshEditor
{
	AshEngine::SceneComponentType EnvironmentComponentEditor::GetComponentType() const
	{
		return AshEngine::SceneComponentType::Environment;
	}

	const char* EnvironmentComponentEditor::GetDisplayName() const
	{
		return "Environment";
	}

	bool EnvironmentComponentEditor::CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const
	{
		(void)refHost;
		return AshEngine::can_add_scene_component(entity, GetComponentType());
	}

	bool EnvironmentComponentEditor::AddDefault(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		(void)refUi;
		refHost.ResetEnvironmentDraftToLive(entity);
		InspectorPanelState& refState = refHost.AccessInspectorState();
		refState.draftEnvironment.optCurrentValue = AshEngine::EnvironmentComponent{};
		SanitizeOptionalEnvironmentComponent(refState.draftEnvironment.optCurrentValue);
		return refHost.CommitEnvironmentDraft(entity);
	}

	bool EnvironmentComponentEditor::ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity)
	{
		refHost.SyncEnvironmentDraft(entity);
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		return
			refState.draftEnvironment.uEntityId == entity.get_id() &&
			refState.draftEnvironment.optCurrentValue.has_value();
	}

	void EnvironmentComponentEditor::Draw(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		if (!refState.draftEnvironment.optCurrentValue.has_value())
		{
			return;
		}

		const bool bOpen = refUi.collapsing_header("Environment", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (refHost.DrawComponentHeaderContextMenu(refUi, kInspectorEnvironmentComponentMenuId))
		{
			refState.draftEnvironment.optCurrentValue.reset();
			refHost.CommitEnvironmentDraft(entity);
			return;
		}
		if (!bOpen)
		{
			return;
		}

		AshEngine::EnvironmentComponent& environment = *refState.draftEnvironment.optCurrentValue;
		const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
		bool bCommitRequested = false;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"active",
				"Active",
				"Determines whether this environment participates in scene lighting.",
				"Enabled",
				"On / Off",
				"Only one active environment should be used by the scene."),
			"Active",
			environment.active) || bCommitRequested;
		InspectorAssetPathWidgetState iblPathState{};
		iblPathState.pVecRecentPaths = &refState.vecRecentEnvironmentIblPaths;
		iblPathState.pStrSearch = &refState.strEnvironmentIblAssetPickerSearch;
		InspectorAssetPathFieldDesc iblPathDesc = MakeInspectorSceneAssetPathFieldDesc(
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"ibl_asset_path",
				"IBL Asset",
				"Image-based lighting asset path.",
				"Empty",
				"Texture / Prefab asset path",
				"Type a relative asset path, browse, or drag-drop from the Asset Browser."),
			"IBL Asset",
			"EnvironmentIblAssetPickerPopup",
			"Select IBL Asset");
		bCommitRequested = DrawInspectorAssetPathField(
			refUi,
			environment.ibl_asset_path,
			iblPathDesc,
			iblPathState,
			refDeps.pAssetDatabaseService,
			refDeps.pDragDropTransferService) || bCommitRequested;
		std::string strIblAssetValidationMessage{};
		const bool bIblAssetBlocksCommit = TryGetEnvironmentIblAssetValidationMessage(
			refState.draftEnvironment,
			refDeps.pAssetDatabaseService,
			strIblAssetValidationMessage);
		if (!strIblAssetValidationMessage.empty())
		{
			refUi.text_colored(GetEditorWarningTextColor(refUi), "%s", strIblAssetValidationMessage.c_str());
		}

		InspectorAssetPathWidgetState sourceTexturePathState{};
		sourceTexturePathState.pVecRecentPaths = &refState.vecRecentEnvironmentTexturePaths;
		sourceTexturePathState.pStrSearch = &refState.strEnvironmentTextureAssetPickerSearch;
		InspectorAssetPathFieldDesc sourceTexturePathDesc = MakeInspectorSceneAssetPathFieldDesc(
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"source_texture_path",
				"Source Texture",
				"Source texture path used to build the environment.",
				"Empty",
				"Texture asset path",
				"Type a relative asset path, browse, or drag-drop from the Asset Browser."),
			"Source Texture",
			"EnvironmentSourceTextureAssetPickerPopup",
			"Select Source Texture");
		bCommitRequested = DrawInspectorAssetPathField(
			refUi,
			environment.source_texture_path,
			sourceTexturePathDesc,
			sourceTexturePathState,
			refDeps.pAssetDatabaseService,
			refDeps.pDragDropTransferService) || bCommitRequested;
		std::string strSourceTextureValidationMessage{};
		const bool bSourceTextureBlocksCommit = TryGetEnvironmentSourceTextureValidationMessage(
			refState.draftEnvironment,
			refDeps.pAssetDatabaseService,
			strSourceTextureValidationMessage);
		if (!strSourceTextureValidationMessage.empty())
		{
			refUi.text_colored(GetEditorWarningTextColor(refUi), "%s", strSourceTextureValidationMessage.c_str());
		}
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"intensity",
				"Intensity",
				"Overall environment intensity.",
				"1",
				{},
				"Values are clamped before commit."),
			"Intensity",
			environment.intensity,
			0.01f,
			0.0f,
			10.0f) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"lighting_intensity",
				"Lighting Intensity",
				"Diffuse and specular lighting contribution.",
				"1",
				{},
				"Values are clamped before commit."),
			"Lighting Intensity",
			environment.lighting_intensity,
			0.01f,
			0.0f,
			10.0f) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"background_intensity",
				"Background Intensity",
				"Skybox background contribution.",
				"1",
				{},
				"Values are clamped before commit."),
			"Background Intensity",
			environment.background_intensity,
			0.01f,
			0.0f,
			10.0f) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"rotation_degrees",
				"Rotation",
				"Environment rotation around the vertical axis.",
				"0",
				"[-360, 360] degrees",
				"Values outside the range are wrapped back into it."),
			"Rotation",
			environment.rotation_degrees,
			0.1f,
			-360.0f,
			360.0f) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"visible_background",
				"Visible Background",
				"Controls whether the environment is rendered as the scene background.",
				"Enabled",
				"On / Off",
				"Lighting can stay enabled even when the background is hidden."),
			"Visible Background",
			environment.visible_background) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Environment,
				"affect_lighting",
				"Affect Lighting",
				"Controls whether this environment contributes to scene lighting.",
				"Enabled",
				"On / Off",
				"Useful for keeping a visible background without environment lighting."),
			"Affect Lighting",
			environment.affect_lighting) || bCommitRequested;

		if (SanitizeEnvironmentComponent(environment))
		{
			LogInspectorDraftSanitized("Environment", entity.get_id());
			bCommitRequested = true;
		}
		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			refHost,
			{
				"Reset Environment",
				"Reset Environment",
				"Write the default environment settings back to the entity and keep the change undoable.",
				"Restore##Environment",
				"Restore Environment",
				"Discard the local environment draft and reload the current scene values without committing.",
				"Environment"
			});
		if (actionRowResult.bResetRequested)
		{
			refHost.ResetEnvironmentDraftToDefaults(entity);
			LogInspectorDraftReset("Environment", "defaults", entity.get_id());
			refHost.CommitEnvironmentDraft(entity);
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			refHost.ResetEnvironmentDraftToLive(entity);
			LogInspectorDraftReset("Environment", "live scene state", entity.get_id());
			return;
		}
		if (actionRowResult.bRemoveRequested)
		{
			refState.draftEnvironment.optCurrentValue.reset();
			refHost.CommitEnvironmentDraft(entity);
			return;
		}
		if (HasEnvironmentClampWarning(environment))
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"Environment values are touching editor safety limits.");
		}

		if (bCommitRequested)
		{
			if (!bIblAssetBlocksCommit && !bSourceTextureBlocksCommit)
			{
				refHost.CommitEnvironmentDraft(entity);
			}
		}
	}
}
