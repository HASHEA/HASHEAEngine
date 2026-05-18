#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class SceneHierarchyRenameModal
	{
	public:
		void BeginFromSelection(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps) const;
		void Draw(
			const EditorFrameContext& refFrameContext,
			const SceneHierarchyPanelDeps& refDeps,
			SceneHierarchyPanelState& refState) const;
	};
}
