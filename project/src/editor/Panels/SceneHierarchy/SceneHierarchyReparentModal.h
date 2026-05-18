#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"

namespace AshEditor
{
	class SceneHierarchyReparentModal
	{
	public:
		void BeginFromSelection(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps) const;
		void Draw(
			const EditorFrameContext& refFrameContext,
			const SceneHierarchyPanelDeps& refDeps,
			SceneHierarchyPanelState& refState) const;
	};
}
