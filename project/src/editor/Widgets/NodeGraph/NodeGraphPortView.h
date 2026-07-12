#pragma once

#include "Function/Gui/UINodeEditor.h"
#include "Function/Gui/UINodeGraph.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class NodeGraphPortView final
	{
	public:
		explicit NodeGraphPortView(AshEngine::UINodeEditor& refNodeEditor);

		void DrawLegacyPinRows(AshEngine::UIContext& refUi, const AshEngine::UINodeGraphNode& refNode, float fNodeWidth);
		void DrawPinRow(AshEngine::UIContext& refUi, const AshEngine::UINodeGraphPin& refPin);
		void DrawPinMarker(const AshEngine::UINodeGraphPin& refPin);

	private:
		AshEngine::UINodeEditor& _refNodeEditor;
	};
}
