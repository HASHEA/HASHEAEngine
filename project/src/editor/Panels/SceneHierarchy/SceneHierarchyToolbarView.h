#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"

namespace AshEditor
{
	class SceneHierarchyToolbarView
	{
	public:
		void Draw(
			const EditorFrameContext& refFrameContext,
			const SceneHierarchyPanelDeps& refDeps,
			SceneHierarchyPanelState& refState) const;
	};
}
