#pragma once

#include "Core/EditorSceneTypes.h"
#include "Function/Gui/UICommon.h"
#include "Function/Scene/SceneComponents.h"
#include "Panels/Inspector/InspectorPanelState.h"

#include <optional>
#include <string>

namespace AshEditor
{
	class AssetDatabaseService;

	// Shared Inspector command ids and validation helpers used by split component editors.
	inline constexpr const char* kInspectorCameraComponentMenuId = "InspectorCameraComponentMenu";
	inline constexpr const char* kInspectorLightComponentMenuId = "InspectorLightComponentMenu";
	inline constexpr const char* kInspectorMeshComponentMenuId = "InspectorMeshComponentMenu";

	void LogInspectorDraftSanitized(const char* pComponentName, SceneEntityId uEntityId);
	void LogInspectorDraftReset(const char* pComponentName, const char* pTargetState, SceneEntityId uEntityId);

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
	bool MeshComponentHasValidAssetPath(const std::optional<AshEngine::MeshComponent>& optValue);
	bool SanitizeMeshComponent(AshEngine::MeshComponent& refComponent);
	bool SanitizeOptionalMeshComponent(std::optional<AshEngine::MeshComponent>& refValue);
}
