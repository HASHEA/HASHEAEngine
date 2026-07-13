#include "Widgets/NodeGraph/NodeGraphLinkView.h"

namespace AshEditor
{
	NodeGraphLinkView::NodeGraphLinkView(AshEngine::UINodeEditor& refNodeEditor)
		: _refNodeEditor(refNodeEditor)
	{
	}

	void NodeGraphLinkView::DrawLinks(const AshEngine::UINodeGraphModel& refGraph)
	{
		for (const AshEngine::UINodeGraphLink& refLink : refGraph.links)
		{
			_refNodeEditor.link(refLink.id, refLink.startPin, refLink.endPin);
		}
	}
}
