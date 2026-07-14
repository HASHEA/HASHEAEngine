#pragma once

#include "Core/TerrainEditorSessionCore.h"

#include <string>
#include <vector>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct TerrainExternalChangeState;
	struct TerrainFileOperationState;
	struct TerrainModeState;

	struct TerrainModeView
	{
		AshEngine::TerrainAssetId asset_id = 0u;
		const AshEngine::TerrainWorkingSet* p_working_set = nullptr;
		TerrainAuthoringConfig authoring_config{};
		TerrainEditorPreviewState preview{};
		bool dirty = false;
		bool pending_composition = false;
		bool blocking_operation = false;
		const TerrainFileOperationState* p_file_operation_state = nullptr;
		const TerrainExternalChangeState* p_external_change_state = nullptr;
		std::string last_error{};
	};

	struct TerrainModeDrawResult
	{
		std::vector<TerrainEditorIntent> intents{};
	};

	TerrainModeDrawResult DrawTerrainModeTabs(
		AshEngine::UIContext& refUi,
		const TerrainModeView& refView,
		TerrainModeState& refState);
}
