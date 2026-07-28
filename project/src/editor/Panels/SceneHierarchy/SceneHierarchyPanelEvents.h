#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorEvents.h"
#include "Function/Scene/Scene.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"

namespace AshEditor
{
	inline void BindSceneHierarchySceneLifetimeEvents(
		EditorEventBindings& refBindings,
		SceneHierarchyPanelState& refState)
	{
		refBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[&refState](const EditorActiveSceneChangedEvent&)
			{
				refState.ResetTransientState();
			});
		refBindings.Subscribe<EditorSceneChangedEvent>(
			[&refState](const EditorSceneChangedEvent& refEvent)
			{
				if (refEvent.eKind == AshEngine::SceneChangeKind::SceneReloaded ||
					refEvent.eKind == AshEngine::SceneChangeKind::SceneReplaced)
				{
					refState.ResetTransientState();
				}
			});
	}
}
