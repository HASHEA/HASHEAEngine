#pragma once

#include "Core/EditorCommand.h"
#include "Function/Asset/TerrainBrush.h"
#include "Function/Asset/TerrainLayerStack.h"

#include <cstdint>
#include <vector>

namespace AshEditor
{
	class TerrainStrokeCommand final : public EditorCommand
	{
	public:
		TerrainStrokeCommand(
			AshEngine::TerrainAssetId assetId,
			AshEngine::TerrainLayerId layerId,
			uint64_t sequence,
			std::vector<AshEngine::TerrainEditPatch> patches);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;

	private:
		bool Replay(EditorContext& refContext, AshEngine::TerrainEditPatchDirection eDirection);

	private:
		AshEngine::TerrainAssetId _assetId = 0;
		AshEngine::TerrainLayerId _layerId{};
		uint64_t _sequence = 0;
		std::vector<AshEngine::TerrainEditPatch> _patches{};
	};

	class TerrainLayerCommand final : public EditorCommand
	{
	public:
		TerrainLayerCommand(
			AshEngine::TerrainAssetId assetId,
			uint64_t sequence,
			AshEngine::TerrainLayerStackPatch patch,
			AshEngine::TerrainLayerId selectedBefore,
			AshEngine::TerrainLayerId selectedAfter);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;

	private:
		bool Replay(EditorContext& refContext, AshEngine::TerrainEditPatchDirection eDirection);

	private:
		AshEngine::TerrainAssetId _assetId = 0;
		uint64_t _sequence = 0;
		AshEngine::TerrainLayerStackPatch _patch{};
		AshEngine::TerrainLayerId _selectedBefore{};
		AshEngine::TerrainLayerId _selectedAfter{};
	};
}
