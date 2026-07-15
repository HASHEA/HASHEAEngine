#pragma once

#include "Core/EditorSceneTypes.h"
#include "Function/Gui/UICommon.h"
#include "Function/Scene/SceneComponents.h"
#include "Panels/Inspector/InspectorPanelState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class AssetType : uint8_t;
	class UIContext;
}

namespace AshEditor
{
	class AssetDatabaseService;
	class IInspectorComponentHost;

	// Shared Inspector command ids and validation helpers used by split component editors.
	inline constexpr const char* kInspectorCameraComponentMenuId = "InspectorCameraComponentMenu";
	inline constexpr const char* kInspectorLightComponentMenuId = "InspectorLightComponentMenu";
	inline constexpr const char* kInspectorMeshComponentMenuId = "InspectorMeshComponentMenu";
	inline constexpr const char* kInspectorEnvironmentComponentMenuId = "InspectorEnvironmentComponentMenu";
	inline constexpr const char* kInspectorParticleComponentMenuId = "InspectorParticleComponentMenu";

	struct InspectorAssetPathValidationDesc
	{
		std::string strAssetPath{};
		std::string strOriginalAssetPath{};
		std::vector<AshEngine::AssetType> vecAllowedAssetTypes{};
		const char* pEmptyAssetPathMessage = nullptr;
		const char* pMissingAssetMessage = nullptr;
		const char* pUnsupportedAssetTypeMessage = nullptr;
		const char* pLoadStateProblemPrefix = nullptr;
		bool bValidateOnlyWhenChanged = false;
		bool bBlockWhenEmpty = false;
		bool bBlockWhenMissingAsset = true;
		bool bBlockWhenUnsupportedAssetType = true;
		bool bBlockWhenLoadStateProblem = true;
	};

	struct InspectorComponentActionRowDesc
	{
		const char* pResetLabel = nullptr;
		const char* pResetTooltipTitle = nullptr;
		const char* pResetDescription = nullptr;
		const char* pRestoreLabel = nullptr;
		const char* pRestoreTooltipTitle = nullptr;
		const char* pRestoreDescription = nullptr;
		const char* pRemoveIdSuffix = nullptr;
	};

	struct InspectorComponentActionRowResult
	{
		bool bResetRequested = false;
		bool bRestoreRequested = false;
		bool bRemoveRequested = false;
	};

	void LogInspectorDraftSanitized(const char* pComponentName, SceneEntityId uEntityId);
	void LogInspectorDraftReset(const char* pComponentName, const char* pTargetState, SceneEntityId uEntityId);
	InspectorComponentActionRowResult DrawInspectorComponentActionRow(
		AshEngine::UIContext& refUi,
		IInspectorComponentHost& refHost,
		const InspectorComponentActionRowDesc& refDesc);

	bool IsPerspectiveCamera(const AshEngine::CameraComponent& refCamera);
	bool IsOrthographicCamera(const AshEngine::CameraComponent& refCamera);
	bool HasCameraClampWarning(const AshEngine::CameraComponent& refCamera);
	bool SanitizeCameraComponent(AshEngine::CameraComponent& refComponent);
	bool SanitizeOptionalCameraComponent(std::optional<AshEngine::CameraComponent>& refValue);

	bool LightUsesRange(const AshEngine::LightComponent& refLight);
	bool LightUsesConeControls(const AshEngine::LightComponent& refLight);
	bool HasLightClampWarning(const AshEngine::LightComponent& refLight);
	bool SanitizeLightComponent(AshEngine::LightComponent& refComponent);
	bool SanitizeOptionalLightComponent(std::optional<AshEngine::LightComponent>& refValue);

	bool TryGetMeshAssetValidationMessage(
		const InspectorPanelState::MeshDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage);
	bool TryGetEnvironmentIblAssetValidationMessage(
		const InspectorPanelState::EnvironmentDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage);
	bool TryGetEnvironmentSourceTextureValidationMessage(
		const InspectorPanelState::EnvironmentDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage);
	bool TryGetParticleSpriteTextureValidationMessage(
		const InspectorPanelState::ParticleDraft& refDraft,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage);
	bool TryGetInspectorAssetPathValidationMessage(
		const InspectorAssetPathValidationDesc& refDesc,
		const AssetDatabaseService* pAssetDatabaseService,
		std::string& strOutMessage);
	bool MeshComponentHasValidAssetPath(const std::optional<AshEngine::MeshComponent>& optValue);
	bool SanitizeMeshComponent(AshEngine::MeshComponent& refComponent);
	bool SanitizeOptionalMeshComponent(std::optional<AshEngine::MeshComponent>& refValue);

	bool HasEnvironmentClampWarning(const AshEngine::EnvironmentComponent& refEnvironment);
	bool SanitizeEnvironmentComponent(AshEngine::EnvironmentComponent& refComponent);
	bool SanitizeOptionalEnvironmentComponent(std::optional<AshEngine::EnvironmentComponent>& refValue);

	bool SanitizeParticleComponent(AshEngine::ParticleComponent& refComponent);
	bool SanitizeOptionalParticleComponent(std::optional<AshEngine::ParticleComponent>& refValue);
}
