#pragma once

#include "Core/EditorCommand.h"
#include "Function/Asset/VegetationLayer.h"

#include <cstdint>
#include <memory>

namespace AshEditor
{
	class VegetationStrokeCommand final : public EditorCommand
	{
	public:
		VegetationStrokeCommand(
			EditorCommandDocumentKey documentKey,
			std::weak_ptr<AshEngine::VegetationLayerWorkingSet> workingSet,
			AshEngine::VegetationLayerPatch patch,
			uint64_t expectedCurrentGeneration);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		std::optional<EditorCommandDocumentKey> GetDocumentKey() const override;
		uint64_t GetExpectedCurrentGeneration() const;

	private:
		bool ApplyDirection(bool bForward);

	private:
		EditorCommandDocumentKey _documentKey{};
		std::weak_ptr<AshEngine::VegetationLayerWorkingSet> _workingSet{};
		AshEngine::VegetationLayerPatch _patch{};
		uint64_t _uExpectedCurrentGeneration = 0;
	};
}
