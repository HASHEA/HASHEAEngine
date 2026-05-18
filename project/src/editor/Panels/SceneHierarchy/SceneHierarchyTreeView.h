#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"

namespace AshEngine
{
	class Scene;
}

namespace AshEditor
{
	class SceneHierarchyTreeView
	{
	public:
		void Draw(
			const EditorFrameContext& refFrameContext,
			const SceneHierarchyPanelDeps& refDeps,
			SceneHierarchyPanelState& refState,
			AshEngine::Scene& refScene) const;
	};
}
