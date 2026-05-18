#include "Panels/SceneHierarchy/SceneHierarchySearchResultsView.h"

#include "Core/EditorStringUtils.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <string>
#include <vector>

namespace AshEditor
{
	void SceneHierarchySearchResultsView::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState,
		AshEngine::Scene& refScene) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		const std::string strLoweredSearch = ToLowerCopy(refState.strSearchText);

		const std::array<SceneHierarchyEntityFilterOption, 4>& arrFilters = GetSceneHierarchyEntityFilters();
		refState.iEntityTypeFilterIndex = std::clamp(
			refState.iEntityTypeFilterIndex,
			0,
			static_cast<int32_t>(arrFilters.size() - 1));
		const SceneHierarchyEntityFilterOption& refFilter = arrFilters[static_cast<size_t>(refState.iEntityTypeFilterIndex)];

		std::vector<AshEngine::Entity> vecMatches{};
		for (const AshEngine::Entity& refEntity : refScene.get_entities())
		{
			const std::string strName = refEntity.get_name();
			if (refFilter.pfnMatches && !refFilter.pfnMatches(refEntity))
			{
				continue;
			}
			if (!strLoweredSearch.empty() && ToLowerCopy(strName).find(strLoweredSearch) == std::string::npos)
			{
				continue;
			}
			vecMatches.push_back(refEntity);
		}

		std::vector<SceneEntityId> vecVisibleEntityIds{};
		vecVisibleEntityIds.reserve(vecMatches.size());
		for (const AshEngine::Entity& refEntity : vecMatches)
		{
			if (refEntity.is_valid())
			{
				vecVisibleEntityIds.push_back(refEntity.get_id());
			}
		}

		refUi.text_colored(
			GetEditorMutedTextColor(refUi),
			"Matches: %u | Filter: %s",
			static_cast<uint32_t>(vecMatches.size()),
			refFilter.pLabel);
		refUi.separator();

		if (vecMatches.empty())
		{
			refUi.text_unformatted("No entities match the current search/filter.");
			return;
		}

		if (!refUi.begin_table(
			"SceneHierarchySearchResults",
			2,
			AshEngine::UITableFlagBits::RowBg |
				AshEngine::UITableFlagBits::BordersInner |
				AshEngine::UITableFlagBits::SizingStretchProp |
				AshEngine::UITableFlagBits::ScrollY))
		{
			return;
		}

		refUi.table_setup_column("Name", AshEngine::UITableColumnFlagBits::WidthStretch);
		refUi.table_setup_column("Path", AshEngine::UITableColumnFlagBits::WidthStretch);
		refUi.table_headers_row();
		for (const AshEngine::Entity& refEntity : vecMatches)
		{
			if (!refEntity.is_valid())
			{
				continue;
			}

			const SceneEntityId uEntityId = refEntity.get_id();
			const bool bSelected =
				refDeps.pSelectionService &&
				refDeps.pSelectionService->IsSelected(EditorSelectionKind::Entity, uEntityId);
			const std::string strName = refEntity.get_name();
			const std::string strPath = BuildEntityPathLabel(refEntity);

			refUi.table_next_row();
			refUi.table_next_column();
			const std::string strId = std::to_string(uEntityId);
			refUi.push_id(strId.c_str());
			if (refUi.selectable(strName.c_str(), bSelected, AshEngine::UISelectableFlagBits::SpanAllColumns))
			{
				SelectEntityFromHierarchy(refFrameContext, refDeps, refState, refEntity, vecVisibleEntityIds);
			}
			DrawEntityContextMenu(refFrameContext, refDeps, refState, refEntity);
			refUi.table_next_column();
			refUi.text_unformatted(strPath.c_str());
			refUi.pop_id();
		}

		refUi.end_table();
	}
}
