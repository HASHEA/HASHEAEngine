#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorPanel.h"
#include "Core/EditorSelection.h"
#include "Panels/Terrain/TerrainModeState.h"

#include <vector>

namespace AshEditor
{
	class AssetDatabaseService;
	class EditorEventBus;
	class SceneService;
	class SelectionService;
	class TerrainEditorService;

	class TerrainModePanel final : public EditorPanel
	{
	public:
		explicit TerrainModePanel(
			TerrainEditorService* pTerrainEditorService = nullptr,
			AssetDatabaseService* pAssetDatabaseService = nullptr,
			SceneService* pSceneService = nullptr,
			SelectionService* pSelectionService = nullptr);

		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;
		void BindEventBus(EditorEventBus* pEventBus);

	private:
		bool TryBindSelection(
			const EditorSelection& refSelection,
			const std::vector<EditorSelection>& refSelections);
		bool TryBindAssetSelection(
			const EditorSelection& refSelection,
			const std::vector<EditorSelection>& refSelections);

	private:
		TerrainEditorService* _pTerrainEditorService = nullptr;
		AssetDatabaseService* _pAssetDatabaseService = nullptr;
		SceneService* _pSceneService = nullptr;
		SelectionService* _pSelectionService = nullptr;
		EditorEventBindings _eventBindings{};
		TerrainModeState _state{};
		bool _selectionSynchronized = false;
	};
}
