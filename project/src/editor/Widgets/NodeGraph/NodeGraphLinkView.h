#pragma once

#include "Function/Gui/UINodeEditor.h"
#include "Function/Gui/UINodeGraph.h"

namespace AshEditor
{
	class NodeGraphLinkView final
	{
	public:
		explicit NodeGraphLinkView(AshEngine::UINodeEditor& refNodeEditor);

		void DrawLinks(const AshEngine::UINodeGraphModel& refGraph);

	private:
		AshEngine::UINodeEditor& _refNodeEditor;
	};
}
