#include "Panels/Inspector/InspectorComponentEditorSupport.h"

#include "Base/hlog.h"
#include "Core/EditorComponentComparison.h"
#include "Services/AssetDatabaseService.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace AshEditor
{
	namespace
	{
		constexpr float kInspectorMinimumCameraNearPlane = 0.001f;
		constexpr float kInspectorMinimumCameraDepthGap = 0.001f;
		constexpr float kInspectorMaximumCameraFarPlane = 10000.0f;
		constexpr float kInspectorMinimumOrthoHeight = 0.1f;
		constexpr float kInspectorMaximumLightIntensity = 100000.0f;
		constexpr float kInspectorMaximumLightRange = 100000.0f;
		constexpr float kInspectorMinimumConeAngle = 0.0f;
		constexpr float kInspectorMaximumConeAngle = 180.0f;

		bool IsNearlyEqual(const float fLeft, const float fRight, const float fTolerance = 0.0001f)
		{
			return std::fabs(fLeft - fRight) <= fTolerance;
		}

		bool IsSupportedMeshAssetType(const AshEngine::AssetType eAssetType)
		{
			return eAssetType == AshEngine::AssetType::Mesh || eAssetType == AshEngine::AssetType::Model;
		}

		bool IsValidMeshMobility(const AshEngine::SceneMobility eMobility)
		{
			switch (eMobility)
			{
			case AshEngine::SceneMobility::Static:
			case AshEngine::SceneMobility::Stationary:
			case AshEngine::SceneMobility::Movable:
				return true;
			default:
				return false;
			}
		}

		bool HasMeshAssetPathChanged(const InspectorPanelState::MeshDraft& refDraft)
		{
			if (!refDraft.optCurrentValue.has_value())
			{
				return false;
			}

			const std::string_view svCurrentPath = refDraft.optCurrentValue->asset_path;
			const std::string_view svOriginalPath = refDraft.optOriginalValue.has_value()
				? std::string_view(refDraft.optOriginalValue->asset_path)
				: std::string_view{};
			return svCurrentPath != svOriginalPath;
		}

		float SanitizeFiniteScalar(const float fValue, const float fFallbackValue)
		{
			return std::isfinite(fValue) ? fValue : fFallbackValue;
		}

		float SanitizeClampedScalar(
			const float fValue,
			const float fFallbackValue,
			const float fMinValue,
			const float fMaxValue)
		{
			return std::clamp(SanitizeFiniteScalar(fValue, fFallbackValue), fMinValue, fMaxValue);
		}

		glm::vec3 SanitizeFiniteVec3(const glm::vec3& refValue, const glm::vec3& refFallbackValue)
		{
			return {
				SanitizeFiniteScalar(refValue.x, refFallbackValue.x),
				SanitizeFiniteScalar(refValue.y, refFallbackValue.y),
				SanitizeFiniteScalar(refValue.z, refFallbackValue.z)
			};
		}
	}

	void LogInspectorDraftSanitized(const char* pComponentName, const SceneEntityId uEntityId)
	{
		HLogTrace(
			"InspectorPanel sanitized {} draft values for entity {}.",
			pComponentName,
			static_cast<unsigned long long>(uEntityId));
	}

	void LogInspectorDraftReset(
		const char* pComponentName,
		const char* pTargetState,
		const SceneEntityId uEntityId)
	{
		HLogInfo(
			"InspectorPanel reset {} draft to {} for entity {}.",
			pComponentName,
			pTargetState,
			static_cast<unsigned long long>(uEntityId));
	}

	bool IsPerspectiveCamera(const AshEngine::CameraComponent& refCamera)
	{
		return refCamera.projection == AshEngine::CameraProjectionType::Perspective;
	}

	bool IsOrthographicCamera(const AshEngine::CameraComponent& refCamera)
	{
		return refCamera.projection == AshEngine::CameraProjectionType::Orthographic;
	}

	bool HasCameraClampWarning(const AshEngine::CameraComponent& refCamera)
	{
		return
			refCamera.near_plane <= kInspectorMinimumCameraNearPlane ||
			refCamera.far_plane >= kInspectorMaximumCameraFarPlane ||
			refCamera.orthographic_height <= kInspectorMinimumOrthoHeight ||
			refCamera.fov_y_degrees <= 1.0f ||
			refCamera.fov_y_degrees >= 179.0f ||
			(refCamera.far_plane - refCamera.near_plane) <= kInspectorMinimumCameraDepthGap;
	}

	bool SanitizeCameraComponent(AshEngine::CameraComponent& refComponent)
	{
		const AshEngine::CameraComponent originalValue = refComponent;
		refComponent.fov_y_degrees = SanitizeClampedScalar(refComponent.fov_y_degrees, 60.0f, 1.0f, 179.0f);
		refComponent.near_plane = SanitizeClampedScalar(
			refComponent.near_plane,
			0.1f,
			kInspectorMinimumCameraNearPlane,
			kInspectorMaximumCameraFarPlane);
		refComponent.far_plane = SanitizeClampedScalar(
			refComponent.far_plane,
			1000.0f,
			refComponent.near_plane + kInspectorMinimumCameraDepthGap,
			kInspectorMaximumCameraFarPlane);
		if (refComponent.far_plane <= refComponent.near_plane)
		{
			refComponent.far_plane = std::min(
				kInspectorMaximumCameraFarPlane,
				refComponent.near_plane + std::max(1.0f, kInspectorMinimumCameraDepthGap));
		}
		refComponent.orthographic_height = SanitizeClampedScalar(
			refComponent.orthographic_height,
			10.0f,
			kInspectorMinimumOrthoHeight,
			kInspectorMaximumCameraFarPlane);
		return !CameraComponentsEqual(refComponent, originalValue);
	}

	bool SanitizeOptionalCameraComponent(std::optional<AshEngine::CameraComponent>& refValue)
	{
		return refValue.has_value() && SanitizeCameraComponent(*refValue);
	}

	bool LightUsesRange(const AshEngine::LightComponent& refLight)
	{
		return refLight.type != AshEngine::LightType::Directional;
	}

	bool LightUsesConeControls(const AshEngine::LightComponent& refLight)
	{
		return refLight.type == AshEngine::LightType::Spot;
	}

	bool HasLightClampWarning(const AshEngine::LightComponent& refLight)
	{
		return
			refLight.intensity >= kInspectorMaximumLightIntensity ||
			refLight.range >= kInspectorMaximumLightRange ||
			refLight.inner_cone_angle_degrees <= kInspectorMinimumConeAngle ||
			refLight.outer_cone_angle_degrees >= kInspectorMaximumConeAngle ||
			IsNearlyEqual(refLight.outer_cone_angle_degrees, refLight.inner_cone_angle_degrees);
	}

	bool SanitizeLightComponent(AshEngine::LightComponent& refComponent)
	{
		const AshEngine::LightComponent originalValue = refComponent;
		refComponent.color = SanitizeFiniteVec3(refComponent.color, { 1.0f, 1.0f, 1.0f });
		refComponent.color.x = std::clamp(refComponent.color.x, 0.0f, 1.0f);
		refComponent.color.y = std::clamp(refComponent.color.y, 0.0f, 1.0f);
		refComponent.color.z = std::clamp(refComponent.color.z, 0.0f, 1.0f);
		refComponent.intensity = SanitizeClampedScalar(refComponent.intensity, 1.0f, 0.0f, kInspectorMaximumLightIntensity);
		refComponent.range = SanitizeClampedScalar(refComponent.range, 10.0f, 0.0f, kInspectorMaximumLightRange);
		refComponent.inner_cone_angle_degrees = SanitizeClampedScalar(
			refComponent.inner_cone_angle_degrees,
			30.0f,
			kInspectorMinimumConeAngle,
			kInspectorMaximumConeAngle);
		refComponent.outer_cone_angle_degrees = SanitizeClampedScalar(
			refComponent.outer_cone_angle_degrees,
			45.0f,
			refComponent.inner_cone_angle_degrees,
			kInspectorMaximumConeAngle);
		if (refComponent.outer_cone_angle_degrees < refComponent.inner_cone_angle_degrees)
		{
			refComponent.outer_cone_angle_degrees = refComponent.inner_cone_angle_degrees;
		}
		return !LightComponentsEqual(refComponent, originalValue);
	}

	bool SanitizeOptionalLightComponent(std::optional<AshEngine::LightComponent>& refValue)
	{
		return refValue.has_value() && SanitizeLightComponent(*refValue);
	}

	bool TryGetMeshAssetValidationMessage(
		const InspectorPanelState::MeshDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage)
	{
		strOutMessage.clear();
		if (!refDraft.optCurrentValue.has_value())
		{
			return false;
		}

		const std::string& strAssetPath = refDraft.optCurrentValue->asset_path;
		if (strAssetPath.empty())
		{
			strOutMessage = "Choose a mesh or model asset before this component is committed to the scene.";
			return true;
		}
		if (!HasMeshAssetPathChanged(refDraft) || !pAssetDatabaseService)
		{
			return false;
		}

		const AshEngine::AssetInfo* pAssetInfo = pAssetDatabaseService->FindByPath(strAssetPath);
		if (!pAssetInfo)
		{
			strOutMessage = "The typed asset path is not present in the current asset database.";
			return true;
		}
		if (!IsSupportedMeshAssetType(pAssetInfo->type))
		{
			strOutMessage = "The selected asset is not a mesh or model resource.";
			return true;
		}

		const AshEngine::AssetLoadState eLoadState = pAssetDatabaseService->GetLoadState(pAssetInfo->id);
		if (eLoadState == AshEngine::AssetLoadState::Missing || eLoadState == AshEngine::AssetLoadState::Failed)
		{
			strOutMessage = std::string("The selected asset is currently ") +
				AssetDatabaseService::GetLoadStateLabel(eLoadState) +
				" in the asset database.";
			return true;
		}

		return false;
	}

	bool MeshComponentHasValidAssetPath(const std::optional<AshEngine::MeshComponent>& optValue)
	{
		return optValue.has_value() && !optValue->asset_path.empty();
	}

	bool SanitizeMeshComponent(AshEngine::MeshComponent& refComponent)
	{
		const AshEngine::MeshComponent originalValue = refComponent;
		if (!IsValidMeshMobility(refComponent.mobility))
		{
			refComponent.mobility = AshEngine::SceneMobility::Static;
		}
		refComponent.layer_mask = refComponent.layer_mask == 0 ? AshEngine::k_default_scene_layer_mask : refComponent.layer_mask;
		return !MeshComponentsEqual(refComponent, originalValue);
	}

	bool SanitizeOptionalMeshComponent(std::optional<AshEngine::MeshComponent>& refValue)
	{
		return refValue.has_value() && SanitizeMeshComponent(*refValue);
	}
}
