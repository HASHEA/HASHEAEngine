#include "Panels/Inspector/CameraComponentEditor.h"

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
	AshEngine::SceneComponentType CameraComponentEditor::GetComponentType() const
	{
		return AshEngine::SceneComponentType::Camera;
	}

	const char* CameraComponentEditor::GetDisplayName() const
	{
		return "Camera";
	}

	bool CameraComponentEditor::CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const
	{
		(void)refHost;
		return AshEngine::can_add_scene_component(entity, GetComponentType());
	}

	bool CameraComponentEditor::AddDefault(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		(void)refUi;
		refHost.ResetCameraDraftToLive(entity);
		InspectorPanelState& refState = refHost.AccessInspectorState();
		refState.draftCamera.optCurrentValue = AshEngine::CameraComponent{};
		SanitizeOptionalCameraComponent(refState.draftCamera.optCurrentValue);
		return refHost.CommitCameraDraft(entity);
	}

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
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"primary",
				"Primary",
				"Marks this camera as the default scene view camera when the runtime asks for one.",
				"Enabled",
				"On / Off",
				"Applies immediately."),
			"Primary",
			camera.primary) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"reverse_z",
				"Reverse Z",
				"Uses reversed depth mapping for this camera to improve far-distance depth precision.",
				"Enabled",
				"On / Off",
				"Camera-local setting. Applies immediately and is serialized as reverse_z."),
			"Reverse Z",
			camera.reverse_z) || bCommitRequested;

		int32_t iProjection = static_cast<int32_t>(camera.projection);
		if (DrawInspectorSceneEnumField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"projection",
				"Projection",
				"Chooses whether this camera uses perspective or orthographic projection.",
				"Perspective",
				"Perspective / Orthographic",
				"Applies immediately."),
			"Projection",
			iProjection))
		{
			camera.projection = static_cast<AshEngine::CameraProjectionType>(iProjection);
			bCommitRequested = true;
		}

		const bool bUsesPerspectiveControls = IsPerspectiveCamera(camera);
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"fov_y_degrees",
				"FOV Y",
				"Vertical field of view used by perspective cameras.",
				"60",
				"[1, 179] degrees",
				"Only active for Perspective cameras. Clamped to avoid degenerate projection matrices."),
			"FOV Y",
			camera.fov_y_degrees,
			0.1f,
			1.0f,
			179.0f,
			"%.3f",
			bUsesPerspectiveControls) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"near_plane",
				"Near Plane",
				"Closest visible depth from the camera. Smaller values reduce depth precision.",
				"0.1",
				"[0.001, far plane)",
				"Kept safely above zero and below the far plane."),
			"Near Plane",
			camera.near_plane,
			0.01f,
			0.001f,
			camera.far_plane) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"far_plane",
				"Far Plane",
				"Farthest visible depth from the camera.",
				"1000",
				"(near plane, 10000]",
				"Clamped so the near/far pair always stays valid."),
			"Far Plane",
			camera.far_plane,
			1.0f,
			camera.near_plane,
			10000.0f) || bCommitRequested;
		const bool bUsesOrthoControls = IsOrthographicCamera(camera);
		bCommitRequested = DrawInspectorSceneDragFloatField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Camera,
				"orthographic_height",
				"Ortho Height",
				"Vertical size of the orthographic frustum when Projection is set to Orthographic.",
				"10",
				"[0.1, 10000]",
				"Only active for Orthographic cameras. Kept positive to avoid a collapsed orthographic view."),
			"Ortho Height",
			camera.orthographic_height,
			0.1f,
			0.1f,
			1000.0f,
			"%.3f",
			bUsesOrthoControls) || bCommitRequested;
		if (SanitizeCameraComponent(camera))
		{
			LogInspectorDraftSanitized("Camera", entity.get_id());
			bCommitRequested = true;
		}
		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			refHost,
			{
				"Reset Camera",
				"Reset Camera",
				"Write the default camera settings back to the entity and keep the change undoable.",
				"Restore##Camera",
				"Restore Camera",
				"Discard the local camera draft and reload the current scene values without committing.",
				"Camera"
			});
		if (actionRowResult.bResetRequested)
		{
			refHost.ResetCameraDraftToDefaults(entity);
			LogInspectorDraftReset("Camera", "defaults", entity.get_id());
			refHost.CommitCameraDraft(entity);
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			refHost.ResetCameraDraftToLive(entity);
			LogInspectorDraftReset("Camera", "live scene state", entity.get_id());
			return;
		}
		if (actionRowResult.bRemoveRequested)
		{
			refState.draftCamera.optCurrentValue.reset();
			refHost.CommitCameraDraft(entity);
			return;
		}
		if (HasCameraClampWarning(camera))
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"One or more camera values are sitting on editor safety limits.");
		}

		if (bCommitRequested)
		{
			refHost.CommitCameraDraft(entity);
		}
	}
}
