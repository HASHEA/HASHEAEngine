#pragma once

#include "Core/EditorSelection.h"

#include <optional>
#include <string_view>
#include <vector>

namespace AshEngine
{
	class Entity;
	struct CameraComponent;
	struct EnvironmentComponent;
	struct LightComponent;
	struct MeshComponent;
	struct ParticleComponent;
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
	std::optional<AshEngine::EnvironmentComponent> GetEnvironmentComponentValue(const AshEngine::Entity& refEntity);
	std::optional<AshEngine::ParticleComponent> GetParticleComponentValue(const AshEngine::Entity& refEntity);

	bool ApplyCameraComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::CameraComponent>& optValue);
	bool ApplyLightComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::LightComponent>& optValue);
	bool ApplyMeshComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::MeshComponent>& optValue);
	bool ApplyEnvironmentComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::EnvironmentComponent>& optValue);
	bool ApplyParticleComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::ParticleComponent>& optValue);

	void DrawInspectorPanelIntro(
		AshEngine::UIContext& refUi,
		const char* pTitle,
		const char* pDescription);
	void DrawInspectorSelectionSummary(
		AshEngine::UIContext& refUi,
		const EditorSelection& refSelection,
		std::string_view svTooltipDescription = {});
	void DrawInspectorMultiSelectionSummary(
		AshEngine::UIContext& refUi,
		const std::vector<EditorSelection>& vecSelections,
		std::string_view svDescription = {});
	void DrawInspectorAssetInspector(
		AshEngine::UIContext& refUi,
		const EditorSelection& refSelection);
	void DrawInspectorEmptyState(AshEngine::UIContext& refUi);
	void DrawInspectorHierarchySection(
		AshEngine::UIContext& refUi,
		const AshEngine::Entity& refEntity);
}
