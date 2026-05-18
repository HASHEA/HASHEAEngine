#include "Services/EditorGizmoSelectionUtils.h"

#include "Core/EditorSelection.h"
#include "Function/Scene/Scene.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <algorithm>
#include <cstdint>

namespace AshEditor::EditorGizmoSelectionUtils
{
	std::vector<SceneEntityId> BuildSelectedTopLevelEntityIds(
		const SceneService& refSceneService,
		const SelectionService& refSelectionService)
	{
		const std::vector<uint64_t> vecSelectedIds = refSelectionService.GetSelectedIds(EditorSelectionKind::Entity);
		return refSceneService.BuildTopLevelEntityIds(vecSelectedIds);
	}

	bool CaptureEntityTransforms(
		const SceneService& refSceneService,
		const std::vector<SceneEntityId>& vecEntityIds,
		std::vector<AshEngine::TransformComponent>& outTransforms)
	{
		outTransforms.clear();
		outTransforms.reserve(vecEntityIds.size());
		for (const SceneEntityId uEntityId : vecEntityIds)
		{
			const AshEngine::Entity entity = refSceneService.FindEntity(uEntityId);
			if (!entity.is_valid())
			{
				outTransforms.clear();
				return false;
			}
			outTransforms.push_back(entity.get_transform_component());
		}
		return !outTransforms.empty();
	}

	void CaptureDragEntityTransforms(
		const SceneService& refSceneService,
		const SelectionService& refSelectionService,
		const SceneEntityId uPrimaryEntityId,
		const AshEngine::TransformComponent& refPrimaryTransform,
		std::vector<SceneEntityId>& outEntityIds,
		std::vector<AshEngine::TransformComponent>& outTransforms)
	{
		outEntityIds = BuildSelectedTopLevelEntityIds(refSceneService, refSelectionService);
		if (std::find(outEntityIds.begin(), outEntityIds.end(), uPrimaryEntityId) == outEntityIds.end())
		{
			outEntityIds.clear();
			outEntityIds.push_back(uPrimaryEntityId);
		}
		if (!CaptureEntityTransforms(refSceneService, outEntityIds, outTransforms))
		{
			outEntityIds.clear();
			outTransforms.clear();
			outEntityIds.push_back(uPrimaryEntityId);
			outTransforms.push_back(refPrimaryTransform);
		}
	}
}
