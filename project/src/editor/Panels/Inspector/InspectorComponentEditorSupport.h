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

	// Shared Inspector visuals and validation helpers used by split component editors.
	inline constexpr AshEngine::UIColor kInspectorAccentColor{ 0.67f, 0.78f, 0.92f, 1.0f };
	inline constexpr AshEngine::UIColor kInspectorMutedColor{ 0.67f, 0.70f, 0.76f, 1.0f };
	inline constexpr AshEngine::UIColor kInspectorWarningColor{ 0.95f, 0.80f, 0.48f, 1.0f };
	inline constexpr AshEngine::UIColor kInspectorDropZoneFillColor{ 0.24f, 0.31f, 0.39f, 0.38f };
	inline constexpr AshEngine::UIColor kInspectorDropZoneHoverColor{ 0.30f, 0.42f, 0.54f, 0.52f };
	inline constexpr AshEngine::UIColor kInspectorDropZoneActiveColor{ 0.36f, 0.50f, 0.64f, 0.68f };
	inline constexpr AshEngine::UIColor kInspectorDropZoneBorderColor{ 0.47f, 0.60f, 0.75f, 0.62f };

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
