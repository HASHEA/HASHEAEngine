#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorPanel.h"
#include "Panels/Terrain/TerrainModeState.h"

namespace AshEditor
{
	class AssetDatabaseService;
	class EditorEventBus;
	class TerrainEditorService;

	class TerrainModePanel final : public EditorPanel
	{
	public:
		explicit TerrainModePanel(
			TerrainEditorService* pTerrainEditorService = nullptr,
			AssetDatabaseService* pAssetDatabaseService = nullptr);

		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;
		void BindEventBus(EditorEventBus* pEventBus);

	private:
		TerrainEditorService* _pTerrainEditorService = nullptr;
		AssetDatabaseService* _pAssetDatabaseService = nullptr;
		EditorEventBindings _eventBindings{};
		TerrainModeState _state{};
	};
}
