#include "Panels/Inspector/LightComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <cstdint>

namespace AshEditor
{
	AshEngine::SceneComponentType LightComponentEditor::GetComponentType() const
	{
		return AshEngine::SceneComponentType::Light;
	}

	const char* LightComponentEditor::GetDisplayName() const
	{
		return "Light";
	}

	bool LightComponentEditor::CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const
	{
		(void)refHost;
		return AshEngine::can_add_scene_component(entity, GetComponentType());
	}

	bool LightComponentEditor::AddDefault(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		(void)refUi;
		refHost.ResetLightDraftToLive(entity);
		InspectorPanelState& refState = refHost.AccessInspectorState();
		refState.draftLight.optCurrentValue = AshEngine::LightComponent{};
		SanitizeOptionalLightComponent(refState.draftLight.optCurrentValue);
		return refHost.CommitLightDraft(entity);
	}

	bool LightComponentEditor::ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity)
	{
		refHost.SyncLightDraft(entity);
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		return
			refState.draftLight.uEntityId == entity.get_id() &&
			refState.draftLight.optCurrentValue.has_value();
	}

	void LightComponentEditor::Draw(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		if (!refState.draftLight.optCurrentValue.has_value())
		{
			return;
		}

		const bool bOpen = refUi.collapsing_header("Light", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (refHost.DrawComponentHeaderContextMenu(refUi, kInspectorLightComponentMenuId))
		{
			refState.draftLight.optCurrentValue.reset();
			refHost.CommitLightDraft(entity);
			return;
		}
		if (!bOpen)
		{
			return;
		}

		AshEngine::LightComponent& light = *refState.draftLight.optCurrentValue;
		bool bCommitRequested = false;
		int32_t iLightType = static_cast<int32_t>(light.type);
		if (DrawInspectorSceneEnumField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"type",
				"Light Type",
				"Determines whether this light behaves as directional, point, or spot.",
				"Directional",
				"Directional / Point / Spot",
				"Applies immediately."),
			"Light Type",
			iLightType))
		{
			light.type = static_cast<AshEngine::LightType>(iLightType);
			bCommitRequested = true;
		}
		bCommitRequested = DrawInspectorSceneColor3Field(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"color",
				"Color",
				"Normalized light tint used for shading.",
				"(1, 1, 1)",
				"[0, 1] per channel",
				"Out-of-range values are clamped before commit."),
			"Color",
			light.color) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"intensity",
				"Intensity",
				"Overall brightness multiplier for the light contribution.",
				"1",
				"[0, 100000]",
				"Negative values are not allowed."),
			"Intensity",
			light.intensity,
			0.05f,
			0.0f,
			100000.0f) || bCommitRequested;
		const bool bUsesRangeControls = LightUsesRange(light);
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"range",
				"Range",
				"Approximate reach of point and spot lights.",
				"10",
				"[0, 100000]",
				"Disabled for Directional lights because their influence is not distance-bounded."),
			"Range",
			light.range,
			0.1f,
			0.0f,
			100000.0f,
			"%.3f",
			bUsesRangeControls) || bCommitRequested;
		const bool bUsesConeControls = LightUsesConeControls(light);
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"inner_cone_angle_degrees",
				"Inner Cone",
				"Fully lit spot angle before the outer falloff starts.",
				"30",
				"[0, 180] degrees",
				"Only active for Spot lights. Kept lower than or equal to the outer cone."),
			"Inner Cone",
			light.inner_cone_angle_degrees,
			0.1f,
			0.0f,
			180.0f,
			"%.3f",
			bUsesConeControls) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"outer_cone_angle_degrees",
				"Outer Cone",
				"Maximum spot angle where the light fades out completely.",
				"45",
				"[inner cone, 180] degrees",
				"Only active for Spot lights. Automatically widened if it would become narrower than the inner cone."),
			"Outer Cone",
			light.outer_cone_angle_degrees,
			0.1f,
			0.0f,
			180.0f,
			"%.3f",
			bUsesConeControls) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"casts_shadow",
				"Casts Shadow",
				"Controls whether this light contributes shadow maps.",
				"Enabled",
				"On / Off",
				"Applies immediately."),
			"Casts Shadow",
			light.casts_shadow) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"sunlight",
				"Sunlight",
				"Marks this light as the primary sun-style directional light.",
				"Disabled",
				"On / Off",
				"Scene systems may use this flag for directional atmosphere or cascaded shadows."),
			"Sunlight",
			light.sunlight) || bCommitRequested;
		if (DrawInspectorSceneUIntField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"shadow_priority",
				"Shadow Priority",
				"Priority used when the renderer chooses which shadow-casting lights to allocate first.",
				"128",
				"[0, +inf)",
				"Negative values are clamped to 0 before commit."),
			"Shadow Priority",
			light.shadow_priority))
		{
			bCommitRequested = true;
		}
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"shadow_distance",
				"Shadow Distance",
				"Maximum distance covered by this light's shadow setup.",
				"0",
				"[0, 100000]",
				"Zero lets the renderer use its default distance."),
			"Shadow Distance",
			light.shadow_distance,
			0.1f,
			0.0f,
			100000.0f) || bCommitRequested;
		if (DrawInspectorSceneUIntField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"shadow_cascade_count",
				"Shadow Cascades",
				"Requested cascade count for directional shadow rendering.",
				"0",
				"[0, +inf)",
				"Zero lets the renderer use its default cascade count."),
			"Shadow Cascades",
			light.shadow_cascade_count))
		{
			bCommitRequested = true;
		}
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Light,
				"near_shadow_distance",
				"Near Shadow Distance",
				"Distance reserved for near-field shadow detail.",
				"0",
				"[0, 100000]",
				"Zero disables the editor override."),
			"Near Shadow Distance",
			light.near_shadow_distance,
			0.1f,
			0.0f,
			100000.0f) || bCommitRequested;
		if (SanitizeLightComponent(light))
		{
			LogInspectorDraftSanitized("Light", entity.get_id());
			bCommitRequested = true;
		}
		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			refHost,
			{
				"Reset Light",
				"Reset Light",
				"Write the default light settings back to the entity and keep the change undoable.",
				"Restore##Light",
				"Restore Light",
				"Discard the local light draft and reload the current scene values without committing.",
				"Light"
			});
		if (actionRowResult.bResetRequested)
		{
			refHost.ResetLightDraftToDefaults(entity);
			LogInspectorDraftReset("Light", "defaults", entity.get_id());
			refHost.CommitLightDraft(entity);
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			refHost.ResetLightDraftToLive(entity);
			LogInspectorDraftReset("Light", "live scene state", entity.get_id());
			return;
		}
		if (actionRowResult.bRemoveRequested)
		{
			refState.draftLight.optCurrentValue.reset();
			refHost.CommitLightDraft(entity);
			return;
		}
		if (HasLightClampWarning(light))
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"Light values are touching safety limits or a hard cone boundary.");
		}

		if (bCommitRequested)
		{
			refHost.CommitLightDraft(entity);
		}
	}
}
