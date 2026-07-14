#pragma once

#include "Core/TerrainViewportInputRouter.h"
#include "Function/Gui/UICommon.h"
#include "Panels/ViewportPanelState.h"

#include <string>

namespace AshEditor
{
	struct EditorViewportInputState;
	struct EditorViewportInstance;
	struct EditorViewportPresentation;
	struct ViewportPanelDeps;
	class TerrainEditorService;

	namespace ViewportPanelTerrainInteraction
	{
		bool IsAuthoringMode(const TerrainEditorService* pService);
		void CancelActiveStroke(TerrainEditorService& refService);
		TerrainViewportRouteResult Update(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const EditorViewportPresentation& refPresentation,
			const EditorViewportInputState& refInput,
			const AshEngine::UIRect& rectContent,
			bool bContentHovered,
			ViewportPanelTerrainInteractionState& refInteractionState);
	}
}
