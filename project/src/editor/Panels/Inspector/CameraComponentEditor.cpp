#include "Panels/Inspector/CameraComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <vector>

namespace AshEditor
{
	bool CameraComponentEditor::ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity)
	{
		refHost.SyncCameraDraft(entity);
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		return
			refState.draftCamera.uEntityId == entity.get_id() &&
			refState.draftCamera.optCurrentValue.has_value();
	}

	void CameraComponentEditor::Draw(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		if (!refState.draftCamera.optCurrentValue.has_value())
		{
			return;
		}

		const bool bOpen = refUi.collapsing_header("Camera", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (refHost.DrawComponentHeaderContextMenu(refUi, kInspectorCameraComponentMenuId))
		{
			refState.draftCamera.optCurrentValue.reset();
			refHost.CommitCameraDraft(entity);
			return;
		}
		if (!bOpen)
		{
			return;
		}

		AshEngine::CameraComponent& camera = *refState.draftCamera.optCurrentValue;
		bool bCommitRequested = false;
		bool bRestoreRequested = false;
		bCommitRequested = DrawInspectorCheckboxField(
			refUi,
			"Primary",
			camera.primary,
			{
				"Primary",
				"Marks this camera as the default scene view camera when the runtime asks for one.",
				"Enabled",
				"On / Off",
				"Applies immediately."
			}) || bCommitRequested;

		int iProjection = static_cast<int>(camera.projection);
		const std::vector<const char*> vecProjectionLabels{ "Perspective", "Orthographic" };
		if (DrawInspectorComboField(
			refUi,
			"Projection",
			iProjection,
			vecProjectionLabels,
			{
				"Projection",
				"Chooses whether this camera uses perspective or orthographic projection.",
				"Perspective",
				"Perspective / Orthographic",
				"Applies immediately."
			}))
		{
			camera.projection = static_cast<AshEngine::CameraProjectionType>(iProjection);
			bCommitRequested = true;
		}

		const bool bUsesPerspectiveControls = IsPerspectiveCamera(camera);
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"FOV Y",
			camera.fov_y_degrees,
			0.1f,
			1.0f,
			179.0f,
			{
				"FOV Y",
				"Vertical field of view used by perspective cameras.",
				"60",
				"[1, 179] degrees",
				"Only active for Perspective cameras. Clamped to avoid degenerate projection matrices."
			},
			"%.3f",
			bUsesPerspectiveControls) || bCommitRequested;
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Near Plane",
			camera.near_plane,
			0.01f,
			0.001f,
			camera.far_plane,
			{
				"Near Plane",
				"Closest visible depth from the camera. Smaller values reduce depth precision.",
				"0.1",
				"[0.001, far plane)",
				"Kept safely above zero and below the far plane."
			}) || bCommitRequested;
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Far Plane",
			camera.far_plane,
			1.0f,
			camera.near_plane,
			10000.0f,
			{
				"Far Plane",
				"Farthest visible depth from the camera.",
				"1000",
				"(near plane, 10000]",
				"Clamped so the near/far pair always stays valid."
			}) || bCommitRequested;
		const bool bUsesOrthoControls = IsOrthographicCamera(camera);
		bCommitRequested = DrawInspectorDragFloatField(
			refUi,
			"Ortho Height",
			camera.orthographic_height,
			0.1f,
			0.1f,
			1000.0f,
			{
				"Ortho Height",
				"Vertical size of the orthographic frustum when Projection is set to Orthographic.",
				"10",
				"[0.1, 10000]",
				"Only active for Orthographic cameras. Kept positive to avoid a collapsed orthographic view."
			},
			"%.3f",
			bUsesOrthoControls) || bCommitRequested;
		if (SanitizeCameraComponent(camera))
		{
			LogInspectorDraftSanitized("Camera", entity.get_id());
			bCommitRequested = true;
		}
		if (DrawInspectorSmallActionButton(
			refUi,
			"Reset Camera",
			{
				"Reset Camera",
				"Write the default camera settings back to the entity and keep the change undoable.",
				{},
				{},
				"Immediate action"
			}))
		{
			refHost.ResetCameraDraftToDefaults(entity);
			LogInspectorDraftReset("Camera", "defaults", entity.get_id());
			bCommitRequested = true;
		}
		refUi.same_line();
		if (DrawInspectorSmallActionButton(
			refUi,
			"Restore##Camera",
			{
				"Restore Camera",
				"Discard the local camera draft and reload the current scene values without committing.",
				{},
				{},
				"Immediate action"
			}))
		{
			refHost.ResetCameraDraftToLive(entity);
			LogInspectorDraftReset("Camera", "live scene state", entity.get_id());
			bRestoreRequested = true;
		}
		if (HasCameraClampWarning(camera))
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"One or more camera values are sitting on editor safety limits.");
		}

		if (!bRestoreRequested && bCommitRequested)
		{
			refHost.CommitCameraDraft(entity);
		}
		if (refHost.DrawComponentRemoveAction(refUi, "Camera"))
		{
			refState.draftCamera.optCurrentValue.reset();
			refHost.CommitCameraDraft(entity);
		}
	}
}
