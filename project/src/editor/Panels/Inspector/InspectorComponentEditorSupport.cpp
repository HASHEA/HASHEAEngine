#include "Panels/Inspector/InspectorComponentEditorSupport.h"

#include "Base/hlog.h"
#include "Core/EditorComponentComparison.h"
#include "Function/Gui/UIContext.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Services/AssetDatabaseService.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <algorithm>
#include <cmath>
#include <string>
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
		constexpr float kInspectorMaximumEnvironmentIntensity = 10.0f;
		constexpr float kInspectorMinimumEnvironmentRotation = -360.0f;
		constexpr float kInspectorMaximumEnvironmentRotation = 360.0f;
		constexpr uint32_t kInspectorMinimumParticleCapacity = 1u;
		constexpr uint32_t kInspectorMaximumParticleCapacity = 65536u;
		constexpr float kInspectorMaximumParticleSpawnRate = 20000.0f;
		constexpr float kInspectorMinimumParticleLifetime = 0.01f;
		constexpr float kInspectorMaximumParticleLifetime = 60.0f;
		constexpr float kInspectorMaximumParticleLifetimeVariance = 30.0f;
		constexpr float kInspectorMaximumParticleInitialSpeed = 100.0f;
		constexpr float kInspectorMaximumParticleSpreadAngle = 90.0f;
		constexpr float kInspectorMaximumParticleSize = 10.0f;

		bool IsNearlyEqual(const float fLeft, const float fRight, const float fTolerance = 0.0001f)
		{
			return std::fabs(fLeft - fRight) <= fTolerance;
		}

		bool IsSupportedInspectorAssetType(
			const AshEngine::AssetType eAssetType,
			const std::vector<AshEngine::AssetType>& refAllowedAssetTypes)
		{
			if (refAllowedAssetTypes.empty())
			{
				return true;
			}

			return std::find(refAllowedAssetTypes.begin(), refAllowedAssetTypes.end(), eAssetType) !=
				refAllowedAssetTypes.end();
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

		std::string GetOriginalEnvironmentIblAssetPath(const InspectorPanelState::EnvironmentDraft& refDraft)
		{
			return refDraft.optOriginalValue.has_value() ? refDraft.optOriginalValue->ibl_asset_path : std::string{};
		}

		std::string GetOriginalEnvironmentSourceTexturePath(const InspectorPanelState::EnvironmentDraft& refDraft)
		{
			return refDraft.optOriginalValue.has_value() ? refDraft.optOriginalValue->source_texture_path : std::string{};
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

		std::string MakeVisibleInspectorLabel(const char* pLabel)
		{
			if (!pLabel)
			{
				return {};
			}

			const std::string_view svLabel{ pLabel };
			const size_t uHiddenIdOffset = svLabel.find("##");
			return uHiddenIdOffset == std::string_view::npos
				? std::string(svLabel)
				: std::string(svLabel.substr(0, uHiddenIdOffset));
		}

		float CalcInspectorSmallButtonWidth(AshEngine::UIContext& refUi, const char* pLabel)
		{
			const std::string strVisibleLabel = MakeVisibleInspectorLabel(pLabel);
			const char* pVisibleLabel = strVisibleLabel.empty() ? pLabel : strVisibleLabel.c_str();
			const float fTextWidth = pVisibleLabel ? refUi.calc_text_size(pVisibleLabel).x : 0.0f;
			return fTextWidth + refUi.get_style_frame_padding().x * 2.0f;
		}

		bool CanDrawInspectorActionRowInline(
			AshEngine::UIContext& refUi,
			const InspectorComponentActionRowDesc& refDesc)
		{
			const float fSpacing = refUi.get_style_item_spacing().x;
			float fRequiredWidth =
				CalcInspectorSmallButtonWidth(refUi, refDesc.pResetLabel) +
				fSpacing +
				CalcInspectorSmallButtonWidth(refUi, refDesc.pRestoreLabel);
			if (refDesc.pRemoveIdSuffix)
			{
				fRequiredWidth += fSpacing + CalcInspectorSmallButtonWidth(refUi, "Remove Component");
			}
			return refUi.get_content_region_avail().x >= fRequiredWidth;
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

	InspectorComponentActionRowResult DrawInspectorComponentActionRow(
		AshEngine::UIContext& refUi,
		IInspectorComponentHost& refHost,
		const InspectorComponentActionRowDesc& refDesc)
	{
		InspectorComponentActionRowResult result{};
		const bool bDrawInline = CanDrawInspectorActionRowInline(refUi, refDesc);
		if (DrawInspectorSmallActionButton(
			refUi,
			refDesc.pResetLabel,
			{
				refDesc.pResetTooltipTitle,
				refDesc.pResetDescription,
				{},
				{},
				"Immediate action"
			}))
		{
			result.bResetRequested = true;
		}
		if (bDrawInline)
		{
			refUi.same_line();
		}
		if (DrawInspectorSmallActionButton(
			refUi,
			refDesc.pRestoreLabel,
			{
				refDesc.pRestoreTooltipTitle,
				refDesc.pRestoreDescription,
				{},
				{},
				"Immediate action"
			}))
		{
			result.bRestoreRequested = true;
		}
		if (refDesc.pRemoveIdSuffix)
		{
			if (bDrawInline)
			{
				refUi.same_line();
			}
			result.bRemoveRequested = refHost.DrawComponentRemoveAction(refUi, refDesc.pRemoveIdSuffix);
		}
		return result;
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
		refComponent.shadow_distance = SanitizeClampedScalar(
			refComponent.shadow_distance,
			0.0f,
			0.0f,
			kInspectorMaximumLightRange);
		refComponent.near_shadow_distance = SanitizeClampedScalar(
			refComponent.near_shadow_distance,
			0.0f,
			0.0f,
			kInspectorMaximumLightRange);
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

		InspectorAssetPathValidationDesc desc{};
		desc.strAssetPath = refDraft.optCurrentValue->asset_path;
		desc.strOriginalAssetPath = refDraft.optOriginalValue.has_value()
			? refDraft.optOriginalValue->asset_path
			: std::string{};
		desc.vecAllowedAssetTypes = { AshEngine::AssetType::Mesh, AshEngine::AssetType::Model };
		desc.pEmptyAssetPathMessage = "Choose a mesh or model asset before this component is committed to the scene.";
		desc.pMissingAssetMessage = "The typed asset path is not present in the current asset database.";
		desc.pUnsupportedAssetTypeMessage = "The selected asset is not a mesh or model resource.";
		desc.pLoadStateProblemPrefix = "The selected asset is currently ";
		desc.bValidateOnlyWhenChanged = true;
		desc.bBlockWhenEmpty = true;
		return TryGetInspectorAssetPathValidationMessage(desc, pAssetDatabaseService, strOutMessage);
	}

	bool TryGetEnvironmentIblAssetValidationMessage(
		const InspectorPanelState::EnvironmentDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage)
	{
		strOutMessage.clear();
		if (!refDraft.optCurrentValue.has_value())
		{
			return false;
		}

		InspectorAssetPathValidationDesc desc{};
		desc.strAssetPath = refDraft.optCurrentValue->ibl_asset_path;
		desc.strOriginalAssetPath = GetOriginalEnvironmentIblAssetPath(refDraft);
		desc.vecAllowedAssetTypes = { AshEngine::AssetType::Texture, AshEngine::AssetType::Prefab };
		desc.pMissingAssetMessage = "The typed IBL asset path is not present in the current asset database.";
		desc.pUnsupportedAssetTypeMessage = "The selected IBL asset must be a texture or environment prefab.";
		desc.pLoadStateProblemPrefix = "The selected IBL asset is currently ";
		desc.bValidateOnlyWhenChanged = true;
		return TryGetInspectorAssetPathValidationMessage(desc, pAssetDatabaseService, strOutMessage);
	}

	bool TryGetEnvironmentSourceTextureValidationMessage(
		const InspectorPanelState::EnvironmentDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage)
	{
		strOutMessage.clear();
		if (!refDraft.optCurrentValue.has_value())
		{
			return false;
		}

		InspectorAssetPathValidationDesc desc{};
		desc.strAssetPath = refDraft.optCurrentValue->source_texture_path;
		desc.strOriginalAssetPath = GetOriginalEnvironmentSourceTexturePath(refDraft);
		desc.vecAllowedAssetTypes = { AshEngine::AssetType::Texture };
		desc.pMissingAssetMessage = "The typed source texture path is not present in the current asset database.";
		desc.pUnsupportedAssetTypeMessage = "The selected source texture must be a texture resource.";
		desc.pLoadStateProblemPrefix = "The selected source texture is currently ";
		desc.bValidateOnlyWhenChanged = true;
		return TryGetInspectorAssetPathValidationMessage(desc, pAssetDatabaseService, strOutMessage);
	}

	bool TryGetParticleSpriteTextureValidationMessage(
		const InspectorPanelState::ParticleDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage)
	{
		strOutMessage.clear();
		if (!refDraft.optCurrentValue.has_value())
		{
			return false;
		}

		InspectorAssetPathValidationDesc desc{};
		desc.strAssetPath = refDraft.optCurrentValue->sprite_texture_path;
		desc.strOriginalAssetPath = refDraft.optOriginalValue.has_value()
			? refDraft.optOriginalValue->sprite_texture_path
			: std::string{};
		desc.vecAllowedAssetTypes = { AshEngine::AssetType::Texture };
		desc.pMissingAssetMessage = "The particle sprite path is missing; runtime will use Default Particle Sprite.";
		desc.pUnsupportedAssetTypeMessage = "The particle sprite must be a texture resource.";
		desc.pLoadStateProblemPrefix = "The particle sprite is currently ";
		desc.bValidateOnlyWhenChanged = false;
		desc.bBlockWhenEmpty = false;
		desc.bBlockWhenMissingAsset = false;
		desc.bBlockWhenUnsupportedAssetType = true;
		desc.bBlockWhenLoadStateProblem = false;
		return TryGetInspectorAssetPathValidationMessage(desc, pAssetDatabaseService, strOutMessage);
	}

	bool TryGetInspectorAssetPathValidationMessage(
		const InspectorAssetPathValidationDesc& refDesc,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage)
	{
		strOutMessage.clear();
		if (refDesc.strAssetPath.empty())
		{
			if (refDesc.bBlockWhenEmpty && refDesc.pEmptyAssetPathMessage)
			{
				strOutMessage = refDesc.pEmptyAssetPathMessage;
				return true;
			}
			return false;
		}
		if (refDesc.bValidateOnlyWhenChanged && refDesc.strAssetPath == refDesc.strOriginalAssetPath)
		{
			return false;
		}
		if (!pAssetDatabaseService)
		{
			return false;
		}

		const AshEngine::AssetInfo* pAssetInfo = pAssetDatabaseService->FindByPath(refDesc.strAssetPath);
		if (!pAssetInfo)
		{
			strOutMessage = refDesc.pMissingAssetMessage
				? refDesc.pMissingAssetMessage
				: "The typed asset path is not present in the current asset database.";
			return refDesc.bBlockWhenMissingAsset;
		}
		if (!IsSupportedInspectorAssetType(pAssetInfo->type, refDesc.vecAllowedAssetTypes))
		{
			strOutMessage = refDesc.pUnsupportedAssetTypeMessage
				? refDesc.pUnsupportedAssetTypeMessage
				: "The selected asset type is not supported by this field.";
			return refDesc.bBlockWhenUnsupportedAssetType;
		}

		const AshEngine::AssetLoadState eLoadState = pAssetDatabaseService->GetLoadState(pAssetInfo->id);
		if (eLoadState == AshEngine::AssetLoadState::Missing || eLoadState == AshEngine::AssetLoadState::Failed)
		{
			strOutMessage = std::string(refDesc.pLoadStateProblemPrefix
				? refDesc.pLoadStateProblemPrefix
				: "The selected asset is currently ") +
				AssetDatabaseService::GetLoadStateLabel(eLoadState) +
				" in the asset database.";
			return refDesc.bBlockWhenLoadStateProblem;
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

	bool HasEnvironmentClampWarning(const AshEngine::EnvironmentComponent& refEnvironment)
	{
		return
			refEnvironment.intensity >= kInspectorMaximumEnvironmentIntensity ||
			refEnvironment.lighting_intensity >= kInspectorMaximumEnvironmentIntensity ||
			refEnvironment.background_intensity >= kInspectorMaximumEnvironmentIntensity ||
			refEnvironment.rotation_degrees <= kInspectorMinimumEnvironmentRotation ||
			refEnvironment.rotation_degrees >= kInspectorMaximumEnvironmentRotation;
	}

	bool SanitizeEnvironmentComponent(AshEngine::EnvironmentComponent& refComponent)
	{
		const AshEngine::EnvironmentComponent originalValue = refComponent;
		refComponent.intensity = SanitizeClampedScalar(
			refComponent.intensity,
			1.0f,
			0.0f,
			kInspectorMaximumEnvironmentIntensity);
		refComponent.lighting_intensity = SanitizeClampedScalar(
			refComponent.lighting_intensity,
			1.0f,
			0.0f,
			kInspectorMaximumEnvironmentIntensity);
		refComponent.background_intensity = SanitizeClampedScalar(
			refComponent.background_intensity,
			1.0f,
			0.0f,
			kInspectorMaximumEnvironmentIntensity);
		refComponent.rotation_degrees = SanitizeClampedScalar(
			refComponent.rotation_degrees,
			0.0f,
			kInspectorMinimumEnvironmentRotation,
			kInspectorMaximumEnvironmentRotation);
		return !EnvironmentComponentsEqual(refComponent, originalValue);
	}

	bool SanitizeOptionalEnvironmentComponent(std::optional<AshEngine::EnvironmentComponent>& refValue)
	{
		return refValue.has_value() && SanitizeEnvironmentComponent(*refValue);
	}

	bool SanitizeParticleComponent(AshEngine::ParticleComponent& refComponent)
	{
		const AshEngine::ParticleComponent originalValue = refComponent;
		const AshEngine::ParticleComponent defaultValue{};
		auto sanitizeClampedColor = [](glm::vec4& refColor, const glm::vec4& refFallbackColor)
		{
			refColor.x = SanitizeClampedScalar(refColor.x, refFallbackColor.x, 0.0f, 1.0f);
			refColor.y = SanitizeClampedScalar(refColor.y, refFallbackColor.y, 0.0f, 1.0f);
			refColor.z = SanitizeClampedScalar(refColor.z, refFallbackColor.z, 0.0f, 1.0f);
			refColor.w = SanitizeClampedScalar(refColor.w, refFallbackColor.w, 0.0f, 1.0f);
		};

		refComponent.max_particles = std::clamp(
			refComponent.max_particles,
			kInspectorMinimumParticleCapacity,
			kInspectorMaximumParticleCapacity);
		refComponent.spawn_rate = SanitizeClampedScalar(
			refComponent.spawn_rate,
			defaultValue.spawn_rate,
			0.0f,
			kInspectorMaximumParticleSpawnRate);
		refComponent.lifetime = SanitizeClampedScalar(
			refComponent.lifetime,
			defaultValue.lifetime,
			kInspectorMinimumParticleLifetime,
			kInspectorMaximumParticleLifetime);
		refComponent.lifetime_variance = SanitizeClampedScalar(
			refComponent.lifetime_variance,
			defaultValue.lifetime_variance,
			0.0f,
			kInspectorMaximumParticleLifetimeVariance);
		refComponent.initial_speed = SanitizeClampedScalar(
			refComponent.initial_speed,
			defaultValue.initial_speed,
			0.0f,
			kInspectorMaximumParticleInitialSpeed);
		refComponent.spread_angle_degrees = SanitizeClampedScalar(
			refComponent.spread_angle_degrees,
			defaultValue.spread_angle_degrees,
			0.0f,
			kInspectorMaximumParticleSpreadAngle);
		refComponent.constant_acceleration = SanitizeFiniteVec3(
			refComponent.constant_acceleration,
			defaultValue.constant_acceleration);
		refComponent.start_size = SanitizeClampedScalar(
			refComponent.start_size,
			defaultValue.start_size,
			0.0f,
			kInspectorMaximumParticleSize);
		refComponent.end_size = SanitizeClampedScalar(
			refComponent.end_size,
			defaultValue.end_size,
			0.0f,
			kInspectorMaximumParticleSize);
		sanitizeClampedColor(refComponent.start_color, defaultValue.start_color);
		sanitizeClampedColor(refComponent.end_color, defaultValue.end_color);
		refComponent.radial_falloff = SanitizeClampedScalar(
			refComponent.radial_falloff,
			defaultValue.radial_falloff,
			0.0f,
			1.0f);
		refComponent.radial_sharpness = SanitizeClampedScalar(
			refComponent.radial_sharpness,
			defaultValue.radial_sharpness,
			0.25f,
			8.0f);
		refComponent.soft_fade_distance = SanitizeClampedScalar(
			refComponent.soft_fade_distance,
			defaultValue.soft_fade_distance,
			0.001f,
			10.0f);
		if (refComponent.blend_mode != AshEngine::ParticleBlendMode::Additive &&
			refComponent.blend_mode != AshEngine::ParticleBlendMode::AlphaBlend)
		{
			refComponent.blend_mode = defaultValue.blend_mode;
		}
		return !ParticleComponentsEqual(refComponent, originalValue);
	}

	bool SanitizeOptionalParticleComponent(std::optional<AshEngine::ParticleComponent>& refValue)
	{
		return refValue.has_value() && SanitizeParticleComponent(*refValue);
	}
}
