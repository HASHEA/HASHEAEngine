#include "Panels/SceneHierarchy/SceneHierarchyToolbarView.h"

#include "Function/Gui/UIContext.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"

#include <algorithm>
#include <array>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr float kSceneHierarchyPreferredSearchWidth = 220.0f;
		constexpr float kSceneHierarchyPreferredTypeFilterWidth = 110.0f;
		constexpr float kSceneHierarchyMinimumSearchWidth = 92.0f;
		constexpr float kSceneHierarchyMinimumTypeFilterWidth = 78.0f;
		constexpr float kSceneHierarchyControlSpacing = 6.0f;
	}

	void SceneHierarchyToolbarView::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		(void)refDeps;

		const AshEngine::UIVec2 vecAvailableSize = refUi.get_content_region_avail();
		const bool bShowReset = !refState.strSearchText.empty() || refState.iEntityTypeFilterIndex != 0;
		const float fResetWidth =
			bShowReset
			? std::max(54.0f, refUi.calc_text_size("Reset").x + 18.0f)
			: 0.0f;
		const float fReservedWidth =
			kSceneHierarchyMinimumTypeFilterWidth +
			(bShowReset ? (kSceneHierarchyControlSpacing + fResetWidth) : 0.0f);
		const float fSearchWidth = std::clamp(
			vecAvailableSize.x - fReservedWidth - kSceneHierarchyControlSpacing,
			kSceneHierarchyMinimumSearchWidth,
			kSceneHierarchyPreferredSearchWidth);
		const float fFilterWidth = std::clamp(
			vecAvailableSize.x - fSearchWidth - (bShowReset ? fResetWidth : 0.0f) -
				(bShowReset ? kSceneHierarchyControlSpacing * 2.0f : kSceneHierarchyControlSpacing),
			kSceneHierarchyMinimumTypeFilterWidth,
			kSceneHierarchyPreferredTypeFilterWidth);

		refUi.set_next_item_width(fSearchWidth);
		refUi.input_text("##SceneHierarchySearch", refState.strSearchText);
		refUi.same_line(0.0f, kSceneHierarchyControlSpacing);
		refUi.set_next_item_width(fFilterWidth);

		std::vector<const char*> vecFilterLabels{};
		const std::array<SceneHierarchyEntityFilterOption, 4>& arrFilters = GetSceneHierarchyEntityFilters();
		vecFilterLabels.reserve(arrFilters.size());
		for (const SceneHierarchyEntityFilterOption& refFilter : arrFilters)
		{
			vecFilterLabels.push_back(refFilter.pLabel);
		}
		refUi.combo("##SceneHierarchyTypeFilter", refState.iEntityTypeFilterIndex, vecFilterLabels);

		if (bShowReset)
		{
			refUi.same_line(0.0f, kSceneHierarchyControlSpacing);
			if (refUi.small_button("Reset"))
			{
				refState.strSearchText.clear();
				refState.iEntityTypeFilterIndex = 0;
			}
		}
	}
}
