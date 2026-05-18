#pragma once

#include "Services/EditorGizmoTypesInternal.h"

namespace AshEditor
{
	class AssetDatabaseService;
	class SceneService;
	class SelectionService;
}

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class SelectionOverlayRenderer final
	{
	public:
		static void Draw(
			AshEngine::UIContext& refUi,
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService,
			const EditorGizmoInternal::ViewportContext& refViewportContext);
	};
}
