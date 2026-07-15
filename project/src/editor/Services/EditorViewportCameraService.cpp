#include "Services/EditorViewportCameraService.h"

#include "Core/EditorIds.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"

#include "Function/Scene/SceneQuery.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace AshEditor
{
	namespace
	{
		constexpr float kOrbitMouseSensitivity = 0.25f;
		constexpr float kDefaultFocusDistance = 4.5f;
		constexpr float kScrollDollySpeed = 0.12f;
		constexpr float kDragDollySpeed = 0.015f;
		constexpr float kMinOrbitDistance = 0.1f;
		constexpr float kDefaultMaxOrbitDistance = 20000.0f;
		constexpr float kCameraFramingPaddingScale = 1.15f;
		constexpr float kOrbitDistanceHeadroomScale = 2.0f;
		constexpr bool kEditorCameraReverseZ = true;

		bool IsPointInRect(const AshEngine::UIRect& refRect, const AshEngine::UIVec2& refPoint)
		{
			return
				refPoint.x >= refRect.x &&
				refPoint.y >= refRect.y &&
				refPoint.x <= (refRect.x + refRect.width) &&
				refPoint.y <= (refRect.y + refRect.height);
		}

		glm::mat4 ComputeRotationMatrix(const glm::vec3& refRotationEulerDegrees)
		{
			return glm::yawPitchRoll(
				glm::radians(refRotationEulerDegrees.y),
				glm::radians(refRotationEulerDegrees.x),
				glm::radians(refRotationEulerDegrees.z));
		}

		glm::mat4 ComputeViewMatrix(
			const glm::vec3& refPosition,
			const glm::vec3& refRotationEulerDegrees)
		{
			const glm::mat4 translation = glm::translate(glm::mat4(1.0f), refPosition);
			return glm::inverse(translation * ComputeRotationMatrix(refRotationEulerDegrees));
		}

		glm::vec3 ComputeForwardVector(const glm::vec3& refRotationEulerDegrees)
		{
			return glm::normalize(
				glm::vec3(ComputeRotationMatrix(refRotationEulerDegrees) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
		}

		glm::vec3 ComputeRightVector(const glm::vec3& refRotationEulerDegrees)
		{
			return glm::normalize(
				glm::vec3(ComputeRotationMatrix(refRotationEulerDegrees) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		}

		glm::vec3 ComputeUpVector(const glm::vec3& refRotationEulerDegrees)
		{
			return glm::normalize(
				glm::vec3(ComputeRotationMatrix(refRotationEulerDegrees) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
		}

		glm::mat4 ComputePerspectiveProjection(
			const float fViewportWidth,
			const float fViewportHeight,
			const float fFovYDegrees,
			const float fNearPlane,
			const float fFarPlane)
		{
			const float fResolvedWidth = std::max(fViewportWidth, 1.0f);
			const float fResolvedHeight = std::max(fViewportHeight, 1.0f);
			const float fAspect = fResolvedWidth / fResolvedHeight;
			float fResolvedNearPlane = std::max(fNearPlane, 0.001f);
			float fResolvedFarPlane = std::max(fFarPlane, fNearPlane + 0.001f);
			if (kEditorCameraReverseZ)
			{
				std::swap(fResolvedNearPlane, fResolvedFarPlane);
			}
			return glm::perspectiveLH_ZO(
				glm::radians(std::clamp(fFovYDegrees, 1.0f, 179.0f)),
				fAspect,
				fResolvedNearPlane,
				fResolvedFarPlane);
		}

		glm::vec3 ComputeLookRotationDegrees(const glm::vec3& refDirection)
		{
			if (glm::length(refDirection) <= 0.0001f)
			{
				return { 0.0f, 0.0f, 0.0f };
			}

			const glm::vec3 direction = glm::normalize(refDirection);
			const float fYaw = glm::degrees(std::atan2(direction.x, direction.z));
			const float fHorizontalLength = std::sqrt(direction.x * direction.x + direction.z * direction.z);
			const float fPitch = glm::degrees(std::atan2(-direction.y, std::max(fHorizontalLength, 0.0001f)));
			return { std::clamp(fPitch, -89.0f, 89.0f), fYaw, 0.0f };
		}

		bool IsAltDown(const EditorViewportInputState& refInput)
		{
			return refInput.IsModifierDown(AshEngine::UIModifierFlagBits::Alt);
		}

		bool TryComputeSceneFocusBounds(
			const AshEngine::Scene& refScene,
			AshEngine::AssetDatabase& refAssetDatabase,
			AshEngine::SceneWorldBounds& outBounds,
			bool& outBoundsComplete)
		{
			outBounds = {};
			outBoundsComplete = true;
			for (const AshEngine::Entity& refRoot : refScene.get_root_entities())
			{
				if (!refRoot.is_valid())
				{
					continue;
				}

				AshEngine::SceneWorldBounds subtreeBounds{};
				if (!AshEngine::get_entity_subtree_world_bounds(refScene, refAssetDatabase, refRoot.get_id(), subtreeBounds))
				{
					continue;
				}

				AshEngine::merge_scene_world_bounds(outBounds, subtreeBounds);
			}

			for (const AshEngine::Entity& refTerrain :
				refScene.get_entities_with_component(AshEngine::SceneComponentType::Terrain))
			{
				if (!refTerrain.is_valid())
				{
					continue;
				}
				const AshEngine::AssetInfo* pAsset = refAssetDatabase.find_asset_by_path(
					refTerrain.get_terrain_component().asset_path);
				if (!pAsset ||
					refAssetDatabase.get_asset_load_state(pAsset->id) !=
						AshEngine::AssetLoadState::Loaded)
				{
					outBoundsComplete = false;
				}
			}

			return outBounds.is_valid;
		}
	}

	void EditorViewportCameraService::SetDefaultMoveSpeed(const float fMoveSpeed)
	{
		_fDefaultMoveSpeed = ClampMoveSpeed(fMoveSpeed);
		for (auto& [strViewportId, refState] : _mapStates)
		{
			if (!IsSupportedSceneViewport(strViewportId))
			{
				continue;
			}

			refState.fMoveSpeed = _fDefaultMoveSpeed;
		}
	}

	float EditorViewportCameraService::GetMoveSpeed(const std::string& strViewportId) const
	{
		if (const ViewportCameraState* pState = FindState(strViewportId))
		{
			return pState->fMoveSpeed;
		}

		return _fDefaultMoveSpeed;
	}

	void EditorViewportCameraService::SetMoveSpeed(const std::string& strViewportId, const float fMoveSpeed)
	{
		if (!IsSupportedSceneViewport(strViewportId))
		{
			return;
		}

		ViewportCameraState& refState = EnsureState(strViewportId);
		refState.fMoveSpeed = ClampMoveSpeed(fMoveSpeed);
		_fDefaultMoveSpeed = refState.fMoveSpeed;
	}

	void EditorViewportCameraService::SyncFromScene(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService)
	{
		const AshEngine::Scene& refActiveScene = refSceneService.GetActiveScene();
		if (!refActiveScene.is_valid())
		{
			return;
		}

		++_uSceneSyncSequence;
		if (_uSceneSyncSequence == 0)
		{
			++_uSceneSyncSequence;
		}
		ViewportCameraState& refState = EnsureState(EditorViewportIds::Scene);
		SyncCameraState(refSceneService, refAssetDatabaseService, EditorViewportIds::Scene, refState);
		refState.uLastSceneSyncSequence = _uSceneSyncSequence;
		refState.uLastSceneContentEpoch = refActiveScene.get_content_epoch();
	}

	void EditorViewportCameraService::UpdateViewportInput(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const EditorViewportInputState& refInput,
		const double /*dTimeSeconds*/,
		const EditorViewportCameraInputContext& refContext)
	{
		if (!IsSupportedSceneViewport(refContext.strViewportId))
		{
			return;
		}

		ViewportCameraState& refState = EnsureState(refContext.strViewportId);
		const AshEngine::Scene& refActiveScene = refSceneService.GetActiveScene();
		const bool bSceneChangedSinceSync =
			!refActiveScene.is_valid() ||
			refState.pScene != &refActiveScene ||
			refState.uLastSceneContentEpoch != refActiveScene.get_content_epoch();
		if (_uSceneSyncSequence == 0 ||
			refState.uLastSceneSyncSequence != _uSceneSyncSequence ||
			bSceneChangedSinceSync)
		{
			SyncCameraState(
				refSceneService,
				refAssetDatabaseService,
				refContext.strViewportId,
				refState);
			refState.uLastSceneSyncSequence = _uSceneSyncSequence;
			refState.uLastSceneContentEpoch = refActiveScene.is_valid()
				? refActiveScene.get_content_epoch()
				: 0;
		}
		if (!refState.bInitialized)
		{
			return;
		}

		UpdateViewportExtent(refContext.rectContent, refState);
		if (!refContext.bAcceptsInput)
		{
			refState.eDragMode = CameraDragMode::None;
			refState.bHasLastMousePosition = false;
			RefreshCameraOverride(refState);
			return;
		}

		const AshEngine::UIVec2 vecMousePos = refInput.vecMouseScreenPosition;
		const bool bMouseInContent = IsPointInRect(refContext.rectContent, vecMousePos);
		const bool bViewportInteractive =
			refContext.bViewportFocused ||
			refContext.bViewportHovered ||
			refState.eDragMode != CameraDragMode::None;
		if (!bViewportInteractive)
		{
			refState.eDragMode = CameraDragMode::None;
			refState.bHasLastMousePosition = false;
			RefreshCameraOverride(refState);
			return;
		}

		if (bMouseInContent && std::abs(refInput.vecMouseWheelDelta.y) > 0.0f)
		{
			refState.bHasUserNavigated = true;
			const float fSpeedScale = std::max(0.2f, refState.fMoveSpeed / 8.0f);
			const float fDistanceDelta =
				std::max(refState.fOrbitDistance * kScrollDollySpeed, kMinOrbitDistance) *
				refInput.vecMouseWheelDelta.y *
				fSpeedScale;
			refState.fOrbitDistance = ClampOrbitDistance(
				refState.fOrbitDistance - fDistanceDelta,
				refState.fMaxOrbitDistance);
			UpdatePositionFromOrbit(refState);
		}

		const bool bAltDown = IsAltDown(refInput);
		const bool bMiddleMouseDown = refInput.IsMouseDown(AshEngine::UIMouseButton::Middle);
		const bool bRightMouseDown = refInput.IsMouseDown(AshEngine::UIMouseButton::Right);
		const bool bLeftMouseDown = refInput.IsMouseDown(AshEngine::UIMouseButton::Left);

		if (refInput.WasMousePressed(AshEngine::UIMouseButton::Middle) && bMouseInContent)
		{
			refState.bHasUserNavigated = true;
			refState.eDragMode = CameraDragMode::Pan;
			refState.bHasLastMousePosition = false;
		}
		else if (bAltDown &&
			refInput.WasMousePressed(AshEngine::UIMouseButton::Left) &&
			bMouseInContent)
		{
			refState.bHasUserNavigated = true;
			refState.eDragMode = CameraDragMode::Orbit;
			refState.bHasLastMousePosition = false;
		}
		else if (bAltDown &&
			refInput.WasMousePressed(AshEngine::UIMouseButton::Right) &&
			bMouseInContent)
		{
			refState.bHasUserNavigated = true;
			refState.eDragMode = CameraDragMode::Dolly;
			refState.bHasLastMousePosition = false;
		}

		switch (refState.eDragMode)
		{
		case CameraDragMode::Orbit:
			if (!bAltDown || !bLeftMouseDown)
			{
				refState.eDragMode = CameraDragMode::None;
				refState.bHasLastMousePosition = false;
			}
			break;
		case CameraDragMode::Pan:
			if (!bMiddleMouseDown)
			{
				refState.eDragMode = CameraDragMode::None;
				refState.bHasLastMousePosition = false;
			}
			break;
		case CameraDragMode::Dolly:
			if (!bAltDown || !bRightMouseDown)
			{
				refState.eDragMode = CameraDragMode::None;
				refState.bHasLastMousePosition = false;
			}
			break;
		default:
			break;
		}

		if (refInput.WasKeyPressed(AshEngine::UIKey::F) && (bMouseInContent || refContext.bViewportFocused))
		{
			if (FocusEntity(
				refSceneService,
				refAssetDatabaseService,
				refContext.strViewportId,
				refState,
				refContext.uFocusEntityId))
			{
				refState.bHasUserNavigated = true;
			}
		}

		if (refState.eDragMode != CameraDragMode::None)
		{
			if (!refState.bHasLastMousePosition)
			{
				refState.dLastMouseX = refInput.vecMouseScreenPosition.x;
				refState.dLastMouseY = refInput.vecMouseScreenPosition.y;
				refState.bHasLastMousePosition = true;
			}
			else
			{
				const double dMouseDeltaX = refInput.vecMouseScreenPosition.x - refState.dLastMouseX;
				const double dMouseDeltaY = refInput.vecMouseScreenPosition.y - refState.dLastMouseY;
				refState.dLastMouseX = refInput.vecMouseScreenPosition.x;
				refState.dLastMouseY = refInput.vecMouseScreenPosition.y;

				switch (refState.eDragMode)
				{
				case CameraDragMode::Orbit:
					refState.vecRotationEulerDegrees.y += static_cast<float>(dMouseDeltaX * kOrbitMouseSensitivity);
					refState.vecRotationEulerDegrees.x += static_cast<float>(dMouseDeltaY * kOrbitMouseSensitivity);
					refState.vecRotationEulerDegrees.x =
						std::clamp(refState.vecRotationEulerDegrees.x, -89.0f, 89.0f);
					UpdatePositionFromOrbit(refState);
					break;
				case CameraDragMode::Pan:
				{
					const float fViewportHeight = std::max(1.0f, static_cast<float>(refState.uViewportHeight));
					const float fPanUnitsPerPixel =
						(2.0f * std::tan(glm::radians(refState.fFovYDegrees) * 0.5f) *
							std::max(refState.fOrbitDistance, kMinOrbitDistance)) /
						fViewportHeight;
					const float fPanScale = fPanUnitsPerPixel * std::max(0.25f, refState.fMoveSpeed / 8.0f);
					const glm::vec3 vecRight = ComputeRightVector(refState.vecRotationEulerDegrees);
					const glm::vec3 vecUp = ComputeUpVector(refState.vecRotationEulerDegrees);
					const glm::vec3 vecPanDelta =
						(-vecRight * static_cast<float>(dMouseDeltaX) + vecUp * static_cast<float>(dMouseDeltaY)) *
						fPanScale;
					refState.vecOrbitTarget += vecPanDelta;
					refState.vecPosition += vecPanDelta;
					break;
				}
				case CameraDragMode::Dolly:
				{
					const float fSpeedScale = std::max(0.25f, refState.fMoveSpeed / 8.0f);
					const float fDistanceDelta =
						std::max(refState.fOrbitDistance * kDragDollySpeed, kMinOrbitDistance) *
						static_cast<float>(-dMouseDeltaY) *
						fSpeedScale;
					refState.fOrbitDistance = ClampOrbitDistance(
						refState.fOrbitDistance - fDistanceDelta,
						refState.fMaxOrbitDistance);
					UpdatePositionFromOrbit(refState);
					break;
				}
				default:
					break;
				}
			}
		}

		RefreshCameraOverride(refState);
	}

	bool EditorViewportCameraService::TryResolveViewportBinding(
		const std::string& strViewportId,
		EditorViewportBindingOverride& outOverride) const
	{
		const ViewportCameraState* pState = FindState(strViewportId);
		if (!pState || !pState->pScene || !pState->cameraOverride.enabled)
		{
			return false;
		}

		outOverride.pScene = pState->pScene;
		outOverride.camera.source = AshEngine::SceneCameraSource::Override;
		outOverride.camera.override_view = pState->cameraOverride;
		outOverride.camera.entity_id = 0;
		return true;
	}

	bool EditorViewportCameraService::TryBuildViewportRay(
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent,
		const AshEngine::UIVec2& vecMousePosition,
		AshEngine::SceneRay& outRay) const
	{
		const ViewportCameraState* pState = FindState(strViewportId);
		if (!pState || !pState->cameraOverride.enabled || rectContent.width <= 0.0f || rectContent.height <= 0.0f)
		{
			return false;
		}

		const float fLocalX = vecMousePosition.x - rectContent.x;
		const float fLocalY = vecMousePosition.y - rectContent.y;
		outRay = AshEngine::screen_to_world_ray(
			fLocalX,
			fLocalY,
			rectContent.width,
			rectContent.height,
			pState->cameraOverride.view,
			pState->cameraOverride.projection);
		return true;
	}

	void EditorViewportCameraService::Reset()
	{
		_mapStates.clear();
		_uSceneSyncSequence = 0;
	}

	EditorViewportCameraService::ViewportCameraState& EditorViewportCameraService::EnsureState(const std::string& strViewportId)
	{
		auto [itState, bInserted] = _mapStates.try_emplace(strViewportId);
		if (bInserted)
		{
			itState->second.fMoveSpeed = _fDefaultMoveSpeed;
		}
		return itState->second;
	}

	const EditorViewportCameraService::ViewportCameraState* EditorViewportCameraService::FindState(const std::string& strViewportId) const
	{
		const auto itState = _mapStates.find(strViewportId);
		return itState != _mapStates.end() ? &itState->second : nullptr;
	}

	float EditorViewportCameraService::ClampMoveSpeed(const float fMoveSpeed)
	{
		return std::clamp(fMoveSpeed, kMinMoveSpeed, kMaxMoveSpeed);
	}

	float EditorViewportCameraService::ClampOrbitDistance(
		const float fOrbitDistance,
		const float fMaxOrbitDistance)
	{
		return std::clamp(
			fOrbitDistance,
			kMinOrbitDistance,
			std::max(kMinOrbitDistance, fMaxOrbitDistance));
	}

	void EditorViewportCameraService::SyncCameraState(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
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

		refState.pScene = const_cast<AshEngine::Scene*>(&refActiveScene);
		const std::string strScenePath = refSceneService.GetActiveScenePath().generic_string();
		const bool bNeedsReseed =
			!refState.bInitialized ||
			refState.strSourceSceneName != refActiveScene.get_name() ||
			refState.strSourceScenePath != strScenePath;
		if (bNeedsReseed)
		{
			SeedCameraFromSceneContent(refActiveScene, refAssetDatabaseService, refState);
			refState.strSourceSceneName = refActiveScene.get_name();
			refState.strSourceScenePath = strScenePath;
			refState.eDragMode = CameraDragMode::None;
			refState.bHasLastMousePosition = false;
			refState.bInitialized = true;
			RefreshCameraOverride(refState);
			return;
		}

		const bool bBoundsComplete = RefreshSceneBounds(
			refActiveScene, refAssetDatabaseService, refState);
		if (refState.bAwaitingInitialSceneBounds && bBoundsComplete)
		{
			refState.bAwaitingInitialSceneBounds = false;
			if (!refState.bHasUserNavigated && refState.sceneBounds.is_valid)
			{
				SeedCameraFromBounds(refState.sceneBounds, refState);
			}
		}
		RefreshCameraOverride(refState);
	}

	void EditorViewportCameraService::SeedCameraFromSceneContent(
		const AshEngine::Scene& refScene,
		const AssetDatabaseService& refAssetDatabaseService,
		ViewportCameraState& refState) const
	{
		AshEngine::SceneWorldBounds sceneBounds{};
		AshEngine::AssetDatabase& refAssetDatabase =
			const_cast<AshEngine::AssetDatabase&>(refAssetDatabaseService.GetDatabase());
		bool bBoundsComplete = false;
		refState.lastFocusBounds = {};
		refState.bHasUserNavigated = false;
		refState.fMaxOrbitDistance = kDefaultMaxOrbitDistance;
		if (TryComputeSceneFocusBounds(
				refScene, refAssetDatabase, sceneBounds, bBoundsComplete))
		{
			refState.sceneBounds = sceneBounds;
			refState.bAwaitingInitialSceneBounds = !bBoundsComplete;
			SeedCameraFromBounds(sceneBounds, refState);
			return;
		}

		refState.sceneBounds = {};
		refState.bAwaitingInitialSceneBounds = !bBoundsComplete;
		const glm::vec3 vecFallbackTarget{ 0.0f, 0.9f, 0.0f };
		refState.vecOrbitTarget = vecFallbackTarget;
		refState.vecPosition = { -3.0f, 2.2f, -6.5f };
		refState.vecRotationEulerDegrees = ComputeLookRotationDegrees(vecFallbackTarget - refState.vecPosition);
		refState.fOrbitDistance = ClampOrbitDistance(
			glm::length(refState.vecOrbitTarget - refState.vecPosition),
			refState.fMaxOrbitDistance);
		UpdatePositionFromOrbit(refState);
	}

	void EditorViewportCameraService::SeedCameraFromBounds(
		const AshEngine::SceneWorldBounds& refBounds,
		ViewportCameraState& refState) const
	{
		float fFocusDistance = kDefaultFocusDistance;
		float fFramingDistance = 0.0f;
		if (AshEngine::compute_camera_bounds_framing_distance(
				refBounds,
				refState.fFovYDegrees,
				kCameraFramingPaddingScale,
				fFramingDistance))
		{
			fFocusDistance = std::max(fFocusDistance, fFramingDistance);
		}
		refState.fMaxOrbitDistance = std::max(refState.fMaxOrbitDistance, fFocusDistance);
		float fNavigationLimit = 0.0f;
		if (AshEngine::compute_camera_bounds_framing_distance(
				refBounds,
				refState.fFovYDegrees,
				kCameraFramingPaddingScale * kOrbitDistanceHeadroomScale,
				fNavigationLimit))
		{
			refState.fMaxOrbitDistance = std::max(refState.fMaxOrbitDistance, fNavigationLimit);
		}
		const glm::vec3 vecTarget = refBounds.center;
		const glm::vec3 vecViewDirection = glm::normalize(glm::vec3(-0.55f, 0.38f, -1.0f));
		refState.vecOrbitTarget = vecTarget;
		refState.vecPosition = vecTarget + vecViewDirection * fFocusDistance;
		refState.vecRotationEulerDegrees = ComputeLookRotationDegrees(
			vecTarget - refState.vecPosition);
		refState.fOrbitDistance = ClampOrbitDistance(
			glm::length(refState.vecOrbitTarget - refState.vecPosition),
			refState.fMaxOrbitDistance);
		UpdatePositionFromOrbit(refState);
	}

	bool EditorViewportCameraService::RefreshSceneBounds(
		const AshEngine::Scene& refScene,
		const AssetDatabaseService& refAssetDatabaseService,
		ViewportCameraState& refState) const
	{
		AshEngine::SceneWorldBounds sceneBounds{};
		AshEngine::AssetDatabase& refAssetDatabase =
			const_cast<AshEngine::AssetDatabase&>(refAssetDatabaseService.GetDatabase());
		bool bBoundsComplete = false;
		const bool bHasBounds = TryComputeSceneFocusBounds(
			refScene, refAssetDatabase, sceneBounds, bBoundsComplete);
		if (bBoundsComplete || !refState.sceneBounds.is_valid)
		{
			refState.sceneBounds = bHasBounds
				? sceneBounds
				: AshEngine::SceneWorldBounds{};
		}
		else if (bHasBounds)
		{
			AshEngine::merge_scene_world_bounds(refState.sceneBounds, sceneBounds);
		}

		if (refState.sceneBounds.is_valid)
		{
			float fNavigationLimit = 0.0f;
			if (AshEngine::compute_camera_bounds_framing_distance(
					refState.sceneBounds,
					refState.fFovYDegrees,
					kCameraFramingPaddingScale * kOrbitDistanceHeadroomScale,
					fNavigationLimit))
			{
				refState.fMaxOrbitDistance = std::max(refState.fMaxOrbitDistance, fNavigationLimit);
			}
		}
		return bBoundsComplete;
	}

	void EditorViewportCameraService::RefreshCameraOverride(ViewportCameraState& refState) const
	{
		refState.cameraOverride.view = ComputeViewMatrix(refState.vecPosition, refState.vecRotationEulerDegrees);
		const AshEngine::SceneWorldBounds& refClipBounds = refState.sceneBounds.is_valid
			? refState.sceneBounds
			: refState.lastFocusBounds;
		AshEngine::SceneCameraClipRange clipRange{};
		if (AshEngine::compute_camera_clip_range(
				refClipBounds,
				refState.cameraOverride.view,
				0.03f,
				clipRange))
		{
			refState.fNearPlane = clipRange.near_plane;
			refState.fFarPlane = clipRange.far_plane;
		}
		refState.cameraOverride.projection = ComputePerspectiveProjection(
			static_cast<float>(refState.uViewportWidth),
			static_cast<float>(refState.uViewportHeight),
			refState.fFovYDegrees,
			refState.fNearPlane,
			refState.fFarPlane);
		refState.cameraOverride.camera_position = refState.vecPosition;
		refState.cameraOverride.reverse_z = kEditorCameraReverseZ;
		refState.cameraOverride.enabled = true;
	}

	void EditorViewportCameraService::UpdateViewportExtent(
		const AshEngine::UIRect& rectContent,
		ViewportCameraState& refState) const
	{
		const uint32_t uNewWidth = std::max(1u, static_cast<uint32_t>(std::round(std::max(rectContent.width, 1.0f))));
		const uint32_t uNewHeight = std::max(1u, static_cast<uint32_t>(std::round(std::max(rectContent.height, 1.0f))));
		if (refState.uViewportWidth == uNewWidth && refState.uViewportHeight == uNewHeight)
		{
			return;
		}

		refState.uViewportWidth = uNewWidth;
		refState.uViewportHeight = uNewHeight;
		RefreshCameraOverride(refState);
	}

	void EditorViewportCameraService::UpdatePositionFromOrbit(ViewportCameraState& refState) const
	{
		refState.fOrbitDistance = ClampOrbitDistance(
			refState.fOrbitDistance,
			refState.fMaxOrbitDistance);
		const glm::vec3 vecForward = ComputeForwardVector(refState.vecRotationEulerDegrees);
		refState.vecPosition = refState.vecOrbitTarget - vecForward * refState.fOrbitDistance;
	}

	bool EditorViewportCameraService::FocusEntity(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const std::string& strViewportId,
		ViewportCameraState& refState,
		const SceneEntityId uEntityId)
	{
		if (!IsSupportedSceneViewport(strViewportId) || uEntityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = refSceneService.FindEntity(uEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		AshEngine::SceneWorldBounds bounds{};
		AshEngine::AssetDatabase& refAssetDatabase =
			const_cast<AshEngine::AssetDatabase&>(refAssetDatabaseService.GetDatabase());
		const bool bHasBounds = AshEngine::get_entity_subtree_world_bounds(
			refSceneService.GetActiveScene(),
			refAssetDatabase,
			uEntityId,
			bounds);

		glm::vec3 vecTarget{};
		float fFocusDistance = kDefaultFocusDistance;
		if (bHasBounds)
		{
			refState.lastFocusBounds = bounds;
			vecTarget = bounds.center;
			float fFramingDistance = 0.0f;
			if (AshEngine::compute_camera_bounds_framing_distance(
					bounds,
					refState.fFovYDegrees,
					kCameraFramingPaddingScale,
					fFramingDistance))
			{
				fFocusDistance = std::max(fFocusDistance, fFramingDistance);
			}
			refState.fMaxOrbitDistance = std::max(refState.fMaxOrbitDistance, fFocusDistance);
			float fNavigationLimit = 0.0f;
			if (AshEngine::compute_camera_bounds_framing_distance(
					bounds,
					refState.fFovYDegrees,
					kCameraFramingPaddingScale * kOrbitDistanceHeadroomScale,
					fNavigationLimit))
			{
				refState.fMaxOrbitDistance = std::max(refState.fMaxOrbitDistance, fNavigationLimit);
			}
		}
		else
		{
			const glm::mat4 matWorldTransform = refSceneService.GetActiveScene().get_entity_world_transform(uEntityId);
			vecTarget = glm::vec3(matWorldTransform[3]);
			refState.lastFocusBounds.is_valid = true;
			refState.lastFocusBounds.min = vecTarget;
			refState.lastFocusBounds.max = vecTarget;
			refState.lastFocusBounds.center = vecTarget;
			refState.lastFocusBounds.extents = glm::vec3(0.0f);
		}

		const glm::vec3 vecForward = ComputeForwardVector(refState.vecRotationEulerDegrees);
		refState.vecOrbitTarget = vecTarget;
		refState.fOrbitDistance = ClampOrbitDistance(
			fFocusDistance,
			refState.fMaxOrbitDistance);
		refState.vecPosition = vecTarget - vecForward * refState.fOrbitDistance;
		refState.vecRotationEulerDegrees = ComputeLookRotationDegrees(vecTarget - refState.vecPosition);
		UpdatePositionFromOrbit(refState);
		RefreshCameraOverride(refState);
		return true;
	}

	bool EditorViewportCameraService::IsSupportedSceneViewport(const std::string& strViewportId)
	{
		return strViewportId == EditorViewportIds::Scene;
	}
}
