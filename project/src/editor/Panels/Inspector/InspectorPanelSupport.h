#pragma once

#include "Core/EditorSelection.h"

#include <optional>
#include <string_view>

namespace AshEngine
{
	class Entity;
	struct CameraComponent;
	struct LightComponent;
	struct MeshComponent;
	struct TransformComponent;
	class UIContext;
}

namespace AshEditor
{
	bool HasTransformClampWarning(const AshEngine::TransformComponent& refTransform);
	bool SanitizeTransformComponent(AshEngine::TransformComponent& refComponent);

	std::optional<AshEngine::CameraComponent> GetCameraComponentValue(const AshEngine::Entity& refEntity);
	std::optional<AshEngine::LightComponent> GetLightComponentValue(const AshEngine::Entity& refEntity);
	std::optional<AshEngine::MeshComponent> GetMeshComponentValue(const AshEngine::Entity& refEntity);

	bool CanAddCameraComponent(const AshEngine::Entity& refEntity);
	bool CanAddLightComponent(const AshEngine::Entity& refEntity);
	bool CanAddMeshComponent(const AshEngine::Entity& refEntity);

	bool ApplyCameraComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::CameraComponent>& optValue);
	bool ApplyLightComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::LightComponent>& optValue);
	bool ApplyMeshComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::MeshComponent>& optValue);

	void DrawInspectorPanelIntro(
		AshEngine::UIContext& refUi,
		const char* pTitle,
		const char* pDescription);
	void DrawInspectorSelectionSummary(
		AshEngine::UIContext& refUi,
		const EditorSelection& refSelection,
		std::string_view svTooltipDescription = {});
	void DrawInspectorAssetInspector(
		AshEngine::UIContext& refUi,
		const EditorSelection& refSelection);
	void DrawInspectorEmptyState(AshEngine::UIContext& refUi);
	void DrawInspectorHierarchySection(
		AshEngine::UIContext& refUi,
		const AshEngine::Entity& refEntity);
}
