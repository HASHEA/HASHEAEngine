#include "Panels/Inspector/LightComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <vector>

namespace AshEditor
{
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
		bool bRestoreRequested = false;
		int iLightType = static_cast<int>(light.type);
		const std::vector<const char*> vecLightLabels{ "Directional", "Point", "Spot" };
		if (DrawInspectorComboField(
			refUi,
			"Light Type",
			iLightType,
			vecLightLabels,
			{
				"Light Type",
				"Determines whether this light behaves as directional, point, or spot.",
				"Directional",
				"Directional / Point / Spot",
				"Applies immediately."
			}))
		{
			light.type = static_cast<AshEngine::LightType>(iLightType);
			bCommitRequested = true;
		}
		bCommitRequested = DrawInspectorColor3Field(
			refUi,
			"Color",
			light.color,
			{
				"Color",
				"Normalized light tint used for shading.",
				"(1, 1, 1)",
				"[0, 1] per channel",
				"Out-of-range values are clamped before commit."
			}) || bCommitRequested;
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Intensity",
			light.intensity,
			0.05f,
			0.0f,
			100000.0f,
			{
				"Intensity",
				"Overall brightness multiplier for the light contribution.",
				"1",
				"[0, 100000]",
				"Negative values are not allowed."
			}) || bCommitRequested;
		const bool bUsesRangeControls = LightUsesRange(light);
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Range",
			light.range,
			0.1f,
			0.0f,
			100000.0f,
			{
				"Range",
				"Approximate reach of point and spot lights.",
				"10",
				"[0, 100000]",
				"Disabled for Directional lights because their influence is not distance-bounded."
			},
			"%.3f",
			bUsesRangeControls) || bCommitRequested;
		const bool bUsesConeControls = LightUsesConeControls(light);
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Inner Cone",
			light.inner_cone_angle_degrees,
			0.1f,
			0.0f,
			180.0f,
			{
				"Inner Cone",
				"Fully lit spot angle before the outer falloff starts.",
				"30",
				"[0, 180] degrees",
				"Only active for Spot lights. Kept lower than or equal to the outer cone."
			},
			"%.3f",
			bUsesConeControls) || bCommitRequested;
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Outer Cone",
			light.outer_cone_angle_degrees,
			0.1f,
			0.0f,
			180.0f,
			{
				"Outer Cone",
				"Maximum spot angle where the light fades out completely.",
				"45",
				"[inner cone, 180] degrees",
				"Only active for Spot lights. Automatically widened if it would become narrower than the inner cone."
			},
			"%.3f",
			bUsesConeControls) || bCommitRequested;
		if (SanitizeLightComponent(light))
		{
			LogInspectorDraftSanitized("Light", entity.get_id());
			bCommitRequested = true;
		}
		if (DrawInspectorSmallActionButton(
			refUi,
			"Reset Light",
			{
				"Reset Light",
				"Write the default light settings back to the entity and keep the change undoable.",
				{},
				{},
				"Immediate action"
			}))
		{
			refHost.ResetLightDraftToDefaults(entity);
			LogInspectorDraftReset("Light", "defaults", entity.get_id());
			bCommitRequested = true;
		}
		refUi.same_line();
		if (DrawInspectorSmallActionButton(
			refUi,
			"Restore##Light",
			{
				"Restore Light",
				"Discard the local light draft and reload the current scene values without committing.",
				{},
				{},
				"Immediate action"
			}))
		{
			refHost.ResetLightDraftToLive(entity);
			LogInspectorDraftReset("Light", "live scene state", entity.get_id());
			bRestoreRequested = true;
		}
		if (HasLightClampWarning(light))
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"Light values are touching safety limits or a hard cone boundary.");
		}

		if (!bRestoreRequested && bCommitRequested)
		{
			refHost.CommitLightDraft(entity);
		}
		if (refHost.DrawComponentRemoveAction(refUi, "Light"))
		{
			refState.draftLight.optCurrentValue.reset();
			refHost.CommitLightDraft(entity);
		}
	}
}
