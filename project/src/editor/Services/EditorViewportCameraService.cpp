#include "Services/EditorViewportCameraService.h"

#include "Core/EditorIds.h"
#include "Core/SceneSnapshotUtils.h"
#include "Services/SceneService.h"

#include "Base/hlog.h"
#include "Base/input/Input.h"
#include "Function/Scene/SceneComponents.h"

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace AshEditor
{
	namespace
	{
		constexpr float kMinMoveSpeed = 0.25f;
		constexpr float kMaxMoveSpeed = 256.0f;
		constexpr float kMouseSensitivity = 0.25f;
		constexpr float kShiftMultiplier = 4.0f;
		constexpr float kDefaultFocusDistance = 4.5f;
		constexpr float kScrollDollySpeed = 1.5f;

		bool IsPointInRect(const AshEngine::UIRect& refRect, const AshEngine::UIVec2& refPoint)
		{
			return
				refPoint.x >= refRect.x &&
				refPoint.y >= refRect.y &&
				refPoint.x <= (refRect.x + refRect.width) &&
				refPoint.y <= (refRect.y + refRect.height);
		}

		glm::vec3 ComputeForwardVector(const glm::vec3& refRotationEulerDegrees)
		{
			const glm::mat4 rotationMatrix = glm::yawPitchRoll(
				glm::radians(refRotationEulerDegrees.y),
				glm::radians(refRotationEulerDegrees.x),
				glm::radians(refRotationEulerDegrees.z));
			return glm::normalize(glm::vec3(rotationMatrix * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
		}

		glm::vec3 ComputeRightVector(const glm::vec3& refRotationEulerDegrees)
		{
			const glm::mat4 rotationMatrix = glm::yawPitchRoll(
				glm::radians(refRotationEulerDegrees.y),
				glm::radians(refRotationEulerDegrees.x),
				glm::radians(refRotationEulerDegrees.z));
			return glm::normalize(glm::vec3(rotationMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		}

		glm::vec3 ComputeLookRotationDegrees(const glm::vec3& refDirection)
		{
			const glm::vec3 direction = glm::normalize(refDirection);
			const float fYaw = glm::degrees(std::atan2(direction.x, direction.z));
			const float fHorizontalLength = std::sqrt(direction.x * direction.x + direction.z * direction.z);
			// Negate direction.y: in yawPitchRoll convention, positive pitch rotates
			// the forward vector downward (forward.y = -sin(pitch)), so looking down
			// (direction.y < 0) must produce positive pitch.
			const float fPitch = glm::degrees(std::atan2(-direction.y, std::max(fHorizontalLength, 0.0001f)));
			return { std::clamp(fPitch, -89.0f, 89.0f), fYaw, 0.0f };
		}

		bool TryComputeSceneFocusBounds(
			const AshEngine::Scene& refScene,
			glm::vec3& outMinBounds,
			glm::vec3& outMaxBounds)
		{
			bool bHasBounds = false;
			const auto AccumulateEntityPosition =
				[&refScene, &bHasBounds, &outMinBounds, &outMaxBounds](const AshEngine::Entity& refEntity)
			{
				if (!refEntity.is_valid())
				{
					return;
				}

				const glm::mat4 matWorldTransform = refScene.get_entity_world_transform(refEntity.get_id());
				const glm::vec3 vecWorldPosition = glm::vec3(matWorldTransform[3]);
				if (!bHasBounds)
				{
					outMinBounds = vecWorldPosition;
					outMaxBounds = vecWorldPosition;
					bHasBounds = true;
					return;
				}

				outMinBounds = glm::min(outMinBounds, vecWorldPosition);
				outMaxBounds = glm::max(outMaxBounds, vecWorldPosition);
			};

			for (const AshEngine::Entity& refEntity : refScene.get_entities_with_component(AshEngine::SceneComponentType::Mesh))
			{
				AccumulateEntityPosition(refEntity);
			}

			if (bHasBounds)
			{
				return true;
			}

			for (const AshEngine::Entity& refEntity : refScene.get_entities())
			{
				AccumulateEntityPosition(refEntity);
			}

			return bHasBounds;
		}
	}

	void EditorViewportCameraService::SyncFromScene(const SceneService& refSceneService)
	{
		if (!refSceneService.GetActiveScene().is_valid())
		{
			return;
		}

		ViewportCameraState& refState = EnsureState(EditorViewportIds::Scene);
		SyncPreviewScene(refSceneService, EditorViewportIds::Scene, refState);
	}

	void EditorViewportCameraService::UpdateViewportInput(
		const SceneService& refSceneService,
		const AshEngine::InputState& refInput,
		const double dTimeSeconds,
		const EditorViewportCameraInputContext& refContext)
	{
		if (!IsSupportedSceneViewport(refContext.strViewportId) || !refContext.bAcceptsInput)
		{
			return;
		}

		ViewportCameraState& refState = EnsureState(refContext.strViewportId);
		SyncPreviewScene(refSceneService, refContext.strViewportId, refState);
		if (!refState.previewScene.is_valid() || refState.uCameraEntityId == 0)
		{
			return;
		}

		const double dDeltaSecondsRaw =
			refState.dLastUpdateTimeSeconds >= 0.0
			? (dTimeSeconds - refState.dLastUpdateTimeSeconds)
			: 0.0;
		refState.dLastUpdateTimeSeconds = dTimeSeconds;
		const float fDeltaSeconds = static_cast<float>(std::clamp(dDeltaSecondsRaw, 0.0, 0.1));

		const AshEngine::UIVec2 vecMousePos{
			static_cast<float>(refInput.get_mouse_x()),
			static_cast<float>(refInput.get_mouse_y())
		};
		const bool bMouseInContent = IsPointInRect(refContext.rectContent, vecMousePos);
		const bool bViewportInteractive =
			refContext.bViewportFocused ||
			refContext.bViewportHovered ||
			refState.bMouseLookActive;

		if (!bViewportInteractive)
		{
			refState.bMouseLookActive = false;
			refState.bHasLastMousePosition = false;
			return;
		}

		if (bMouseInContent && std::abs(refInput.get_scroll_y()) > 0.0)
		{
			const glm::vec3 vecForward = ComputeForwardVector(refState.vecRotationEulerDegrees);
			refState.vecPosition += vecForward * static_cast<float>(refInput.get_scroll_y()) * kScrollDollySpeed;
		}

		const bool bRightMouseDown = refInput.is_mouse_button_down(GLFW_MOUSE_BUTTON_RIGHT);
		if (!bRightMouseDown)
		{
			refState.bMouseLookActive = false;
			refState.bHasLastMousePosition = false;
		}
		else if (!refState.bMouseLookActive && bMouseInContent)
		{
			refState.bMouseLookActive = true;
			refState.bHasLastMousePosition = false;
		}

		if (refInput.was_key_pressed(GLFW_KEY_F) && (bMouseInContent || refContext.bViewportFocused))
		{
			FocusEntity(refSceneService, refContext.strViewportId, refState, refContext.uFocusEntityId);
		}

		if (refState.bMouseLookActive)
		{
			if (!refState.bHasLastMousePosition || refInput.was_mouse_button_pressed(GLFW_MOUSE_BUTTON_RIGHT))
			{
				refState.dLastMouseX = refInput.get_mouse_x();
				refState.dLastMouseY = refInput.get_mouse_y();
				refState.bHasLastMousePosition = true;
			}

			const double dMouseDeltaX = refInput.get_mouse_x() - refState.dLastMouseX;
			const double dMouseDeltaY = refInput.get_mouse_y() - refState.dLastMouseY;
			refState.dLastMouseX = refInput.get_mouse_x();
			refState.dLastMouseY = refInput.get_mouse_y();

			refState.vecRotationEulerDegrees.y += static_cast<float>(dMouseDeltaX * kMouseSensitivity);
			refState.vecRotationEulerDegrees.x += static_cast<float>(dMouseDeltaY * kMouseSensitivity);
			refState.vecRotationEulerDegrees.x = std::clamp(refState.vecRotationEulerDegrees.x, -89.0f, 89.0f);
		}

		const bool bTranslationActive =
			refContext.bViewportFocused ||
			(refContext.bViewportHovered && bMouseInContent) ||
			refState.bMouseLookActive;

		glm::vec3 vecMoveDirection{ 0.0f, 0.0f, 0.0f };
		if (bTranslationActive)
		{
			const glm::vec3 vecForward = ComputeForwardVector(refState.vecRotationEulerDegrees);
			const glm::vec3 vecRight = ComputeRightVector(refState.vecRotationEulerDegrees);
			const glm::vec3 vecUp{ 0.0f, 1.0f, 0.0f };

			if (refInput.is_key_down(GLFW_KEY_W))
			{
				vecMoveDirection += vecForward;
			}
			if (refInput.is_key_down(GLFW_KEY_S))
			{
				vecMoveDirection -= vecForward;
			}
			if (refInput.is_key_down(GLFW_KEY_D))
			{
				vecMoveDirection += vecRight;
			}
			if (refInput.is_key_down(GLFW_KEY_A))
			{
				vecMoveDirection -= vecRight;
			}
			if (refInput.is_key_down(GLFW_KEY_E))
			{
				vecMoveDirection += vecUp;
			}
			if (refInput.is_key_down(GLFW_KEY_Q))
			{
				vecMoveDirection -= vecUp;
			}
		}

		const float fMoveLength = glm::length(vecMoveDirection);
		if (fMoveLength > 0.0f && fDeltaSeconds > 0.0f)
		{
			vecMoveDirection /= fMoveLength;
			float fResolvedSpeed = refState.fMoveSpeed;
			if (refInput.is_key_down(GLFW_KEY_LEFT_SHIFT) || refInput.is_key_down(GLFW_KEY_RIGHT_SHIFT))
			{
				fResolvedSpeed *= kShiftMultiplier;
			}
			refState.vecPosition += vecMoveDirection * fResolvedSpeed * fDeltaSeconds;
		}

		ApplyCameraToPreview(refState);
	}

	bool EditorViewportCameraService::TryResolveViewportBinding(
		const std::string& strViewportId,
		EditorViewportBindingOverride& outOverride) const
	{
		const ViewportCameraState* pState = FindState(strViewportId);
		if (!pState || !pState->previewScene.is_valid() || pState->uCameraEntityId == 0)
		{
			return false;
		}

		outOverride.pScene = const_cast<AshEngine::Scene*>(&pState->previewScene);
		outOverride.camera.source = AshEngine::SceneCameraSource::EntityId;
		outOverride.camera.entity_id = pState->uCameraEntityId;
		return true;
	}

	void EditorViewportCameraService::Reset()
	{
		_mapStates.clear();
	}

	EditorViewportCameraService::ViewportCameraState& EditorViewportCameraService::EnsureState(const std::string& strViewportId)
	{
		return _mapStates.try_emplace(strViewportId).first->second;
	}

	const EditorViewportCameraService::ViewportCameraState* EditorViewportCameraService::FindState(const std::string& strViewportId) const
	{
		const std::unordered_map<std::string, ViewportCameraState>::const_iterator itState = _mapStates.find(strViewportId);
		return itState != _mapStates.end() ? &itState->second : nullptr;
	}

	void EditorViewportCameraService::SyncPreviewScene(
		const SceneService& refSceneService,
		const std::string& strViewportId,
		ViewportCameraState& refState)
	{
		if (!IsSupportedSceneViewport(strViewportId))
		{
			return;
		}

		const AshEngine::Scene& refActiveScene = refSceneService.GetActiveScene();
		if (!refActiveScene.is_valid())
		{
			return;
		}

		const std::string strScenePath = refSceneService.GetActiveScenePath().generic_string();
		const bool bNeedsFullReseed =
			!refState.previewScene.is_valid() ||
			refState.strSourceSceneName != refActiveScene.get_name() ||
			refState.strSourceScenePath != strScenePath;
		const bool bNeedsResync =
			bNeedsFullReseed ||
			refState.uSourceSceneChangeVersion != refActiveScene.get_change_version();
		if (!bNeedsResync)
		{
			return;
		}

		if (bNeedsFullReseed)
		{
			SeedCameraFromSceneContent(refActiveScene, refState);
		}

		refState.previewScene = SceneSnapshotUtils::CloneScene(refActiveScene);
		refState.uSourceSceneChangeVersion = refActiveScene.get_change_version();
		refState.strSourceSceneName = refActiveScene.get_name();
		refState.strSourceScenePath = strScenePath;
		refState.uCameraEntityId = 0;

		if (!refState.previewScene.is_valid())
		{
			HLogError("EditorViewportCameraService failed to clone the active scene preview.");
			return;
		}

		AshEngine::Entity previewCameraEntity =
			refState.previewScene.create_entity_with_id(kEditorCameraEntityId, "EditorCamera");
		if (!previewCameraEntity.is_valid())
		{
			HLogError("EditorViewportCameraService failed to create the preview camera entity.");
			return;
		}

		AshEngine::CameraComponent cameraComponent{};
		cameraComponent.primary = false;
		cameraComponent.projection = AshEngine::CameraProjectionType::Perspective;
		cameraComponent.fov_y_degrees = 60.0f;
		cameraComponent.near_plane = 0.03f;
		cameraComponent.far_plane = 2000.0f;
		previewCameraEntity.add_camera_component(cameraComponent);

		refState.uCameraEntityId = previewCameraEntity.get_id();
		ApplyCameraToPreview(refState);
	}

	void EditorViewportCameraService::SeedCameraFromSceneContent(
		const AshEngine::Scene& refScene,
		ViewportCameraState& refState) const
	{
		glm::vec3 vecMinBounds{ 0.0f, 0.0f, 0.0f };
		glm::vec3 vecMaxBounds{ 0.0f, 0.0f, 0.0f };
		if (TryComputeSceneFocusBounds(refScene, vecMinBounds, vecMaxBounds))
		{
			const glm::vec3 vecCenter = (vecMinBounds + vecMaxBounds) * 0.5f;
			const glm::vec3 vecExtent = glm::max(vecMaxBounds - vecMinBounds, glm::vec3(0.001f, 0.001f, 0.001f));
			const float fMaxExtent = std::max(std::max(vecExtent.x, vecExtent.y), vecExtent.z);
			const float fDistance = std::max(4.5f, fMaxExtent * 2.25f);
			const glm::vec3 vecTarget = vecCenter + glm::vec3(0.0f, vecExtent.y * 0.2f, 0.0f);
			const glm::vec3 vecViewDirection = glm::normalize(glm::vec3(-0.55f, 0.38f, -1.0f));
			refState.vecPosition = vecTarget + vecViewDirection * fDistance;
			refState.vecRotationEulerDegrees = ComputeLookRotationDegrees(vecTarget - refState.vecPosition);
			return;
		}

		const glm::vec3 vecFallbackTarget{ 0.0f, 0.9f, 0.0f };
		refState.vecPosition = { -3.0f, 2.2f, -6.5f };
		refState.vecRotationEulerDegrees = ComputeLookRotationDegrees(vecFallbackTarget - refState.vecPosition);
	}

	void EditorViewportCameraService::ApplyCameraToPreview(ViewportCameraState& refState) const
	{
		if (!refState.previewScene.is_valid() || refState.uCameraEntityId == 0)
		{
			return;
		}

		AshEngine::Entity previewCameraEntity = refState.previewScene.find_entity(refState.uCameraEntityId);
		if (!previewCameraEntity.is_valid())
		{
			return;
		}

		AshEngine::TransformComponent transformComponent = previewCameraEntity.get_transform_component();
		transformComponent.position = refState.vecPosition;
		transformComponent.rotation_euler_degrees = refState.vecRotationEulerDegrees;
		previewCameraEntity.set_transform_component_silent(transformComponent);
	}

	void EditorViewportCameraService::FocusEntity(
		const SceneService& refSceneService,
		const std::string& strViewportId,
		ViewportCameraState& refState,
		const SceneEntityId uEntityId)
	{
		if (!IsSupportedSceneViewport(strViewportId) || uEntityId == 0)
		{
			return;
		}

		const AshEngine::Entity entity = refSceneService.FindEntity(uEntityId);
		if (!entity.is_valid())
		{
			return;
		}

		const glm::mat4 matWorldTransform = refSceneService.GetActiveScene().get_entity_world_transform(uEntityId);
		const glm::vec3 vecTarget = glm::vec3(matWorldTransform[3]);
		const glm::vec3 vecForward = ComputeForwardVector(refState.vecRotationEulerDegrees);
		refState.vecPosition = vecTarget - vecForward * kDefaultFocusDistance + glm::vec3(0.0f, 0.75f, 0.0f);
		refState.vecRotationEulerDegrees = ComputeLookRotationDegrees(vecTarget - refState.vecPosition);
		ApplyCameraToPreview(refState);
	}

	bool EditorViewportCameraService::IsSupportedSceneViewport(const std::string& strViewportId)
	{
		return strViewportId == EditorViewportIds::Scene;
	}
}
