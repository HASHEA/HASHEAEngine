#include "Panels/SceneHierarchy/SceneHierarchyToolbarView.h"

#include "Function/Gui/UIContext.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"

#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr float kSceneHierarchySearchWidth = 220.0f;
	}

	void SceneHierarchyToolbarView::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		(void)refDeps;

		refUi.set_next_item_width(kSceneHierarchySearchWidth);
		refUi.input_text("##SceneHierarchySearch", refState.strSearchText);
		refUi.same_line();
		refUi.set_next_item_width(110.0f);

		std::vector<const char*> vecFilterLabels{};
		const std::array<SceneHierarchyEntityFilterOption, 4>& arrFilters = GetSceneHierarchyEntityFilters();
		vecFilterLabels.reserve(arrFilters.size());
		for (const SceneHierarchyEntityFilterOption& refFilter : arrFilters)
		{
			vecFilterLabels.push_back(refFilter.pLabel);
		}
		refUi.combo("##SceneHierarchyTypeFilter", refState.iEntityTypeFilterIndex, vecFilterLabels);

		if (!refState.strSearchText.empty() || refState.iEntityTypeFilterIndex != 0)
		{
			refUi.same_line();
			if (refUi.small_button("Reset"))
			{
				refState.strSearchText.clear();
				refState.iEntityTypeFilterIndex = 0;
			}
		}
	}
}
