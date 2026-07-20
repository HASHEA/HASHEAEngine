#include "Core/VegetationCommands.h"

#include "Core/EditorContext.h"

#include <utility>

namespace AshEditor
{
	VegetationStrokeCommand::VegetationStrokeCommand(
		EditorCommandDocumentKey documentKey,
		std::weak_ptr<AshEngine::VegetationLayerWorkingSet> workingSet,
		AshEngine::VegetationLayerPatch patch,
		const uint64_t expectedCurrentGeneration)
		: _documentKey(std::move(documentKey))
		, _workingSet(std::move(workingSet))
		, _patch(std::move(patch))
		, _uExpectedCurrentGeneration(expectedCurrentGeneration)
	{
	}

	const char* VegetationStrokeCommand::GetLabel() const
	{
		return "Vegetation Stroke";
	}

	bool VegetationStrokeCommand::Execute(EditorContext& refContext)
	{
		(void)refContext;
		return ApplyDirection(true);
	}

	bool VegetationStrokeCommand::Undo(EditorContext& refContext)
	{
		(void)refContext;
		return ApplyDirection(false);
	}

	std::optional<EditorCommandDocumentKey> VegetationStrokeCommand::GetDocumentKey() const
	{
		return _documentKey;
	}

	uint64_t VegetationStrokeCommand::GetExpectedCurrentGeneration() const
	{
		return _uExpectedCurrentGeneration;
	}

	bool VegetationStrokeCommand::ApplyDirection(const bool bForward)
	{
		const std::shared_ptr<AshEngine::VegetationLayerWorkingSet> workingSet =
			_workingSet.lock();
		if (!workingSet)
		{
			return false;
		}

		const AshEngine::VegetationPatchApplyStatus status = bForward
			? AshEngine::apply_vegetation_layer_patch(
				*workingSet, _patch, _uExpectedCurrentGeneration)
			: AshEngine::revert_vegetation_layer_patch(
				*workingSet, _patch, _uExpectedCurrentGeneration);
		if (status != AshEngine::VegetationPatchApplyStatus::Applied)
		{
			return false;
		}

		_uExpectedCurrentGeneration = workingSet->content_generation();
		return true;
	}
}
