#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"
#include "Widgets/EditorTreeWidget.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	class Entity;
	class Scene;
	class UIContext;
}

namespace AshEditor
{
	class SceneService;

	enum class SceneEntityCreatePreset : uint8_t
	{
		Empty = 0,
		Mesh,
		DirectionalLight,
		PointLight,
		SpotLight,
		Camera
	};

	struct SceneHierarchyEntityFilterOption
	{
		const char* pLabel = "";
		bool (*pfnMatches)(const AshEngine::Entity& refEntity) = nullptr;
	};

	const std::array<SceneHierarchyEntityFilterOption, 4>& GetSceneHierarchyEntityFilters();
	SceneEntityId GetSelectedSceneEntityId(const SceneHierarchyPanelDeps& refDeps);
	SceneEntityId GetEntityParentId(const AshEngine::Entity& refEntity);
	uint32_t GetEntitySiblingIndex(const AshEngine::Entity& refEntity, const SceneService& refSceneService);
	uint32_t GetParentChildCount(const SceneService& refSceneService, SceneEntityId uParentId);
	std::vector<SceneEntityId> BuildVisibleEntityIds(const AshEngine::Scene& refScene);
	std::string BuildEntityPathLabel(const AshEngine::Entity& refEntity);
	void BuildReparentCandidateLists(
		const SceneService& refSceneService,
		SceneEntityId uSceneEntityId,
		std::vector<SceneEntityId>& vecOutIds,
		std::vector<std::string>& vecOutLabels);
	EditorTreeWidgetStyle MakeSceneTreeStyle();
	bool IsSceneEntityDragActive(const AshEngine::UIContext* pUiContext);

	void ExecuteCreateRoot(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps);
	void ExecuteCreateChildFromSelection(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps);
	void ExecuteCopySelection(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps);
	void ExecutePasteSelection(const SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps);
	void ExecuteDuplicateSelection(const SceneHierarchyPanelDeps& refDeps);
	bool CanPasteSelection(const SceneHierarchyPanelState& refState);

	bool CreateEntity(
		const SceneHierarchyPanelDeps& refDeps,
		SceneEntityId uParentId,
		SceneEntityCreatePreset ePreset = SceneEntityCreatePreset::Empty);
	bool DrawCreateEntityMenuItems(
		AshEngine::UIContext& refUi,
		const SceneHierarchyPanelDeps& refDeps,
		SceneEntityId uParentId);
	void DestroySelectedEntities(
		const SceneHierarchyPanelDeps& refDeps,
		const std::vector<SceneEntityId>& vecPendingDeleteEntityIds);

	void SelectEntityFromHierarchy(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState,
		const AshEngine::Entity& refEntity,
		const std::vector<SceneEntityId>& vecVisibleEntityIds);

	void DrawEntityContextMenu(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState,
		const AshEngine::Entity& refEntity);

	void DrawContentContextMenu(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneEntityId uSelectedSceneEntityId);
}
