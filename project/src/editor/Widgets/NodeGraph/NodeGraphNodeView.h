#pragma once

#include "Widgets/NodeGraph/NodeGraphCanvasWidget.h"
#include "Widgets/NodeGraph/NodeGraphPortView.h"

namespace AshEditor
{
	class NodeGraphNodeView final
	{
	public:
		explicit NodeGraphNodeView(AshEngine::UINodeEditor& refNodeEditor);

		void DrawNode(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			const NodeGraphCanvasOptions& refOptions,
			NodeGraphCanvasResult& refResult);
		float ResolveNodeWidth(
			AshEngine::UIContext& refUi,
			const AshEngine::UINodeGraphNode& refNode,
			const NodeGraphCanvasOptions& refOptions) const;

	private:
		void DrawNodeHeader(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			float fNodeWidth,
			NodeGraphCanvasResult& refResult);
		void DrawNodeBody(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			const NodeGraphCanvasOptions& refOptions,
			float fNodeWidth,
			NodeGraphCanvasResult& refResult);
		void DrawChevronIcon(
			AshEngine::UIContext& refUi,
			const AshEngine::UIVec2& screenCenter,
			bool bExpanded,
			const AshEngine::UIColor& color) const;
		void DrawBodyRows(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			const NodeGraphCanvasOptions& refOptions,
			float fNodeWidth,
			NodeGraphCanvasResult& refResult);
		void DrawBodyRow(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			AshEngine::UINodeGraphBodyRow& refRow,
			const NodeGraphCanvasOptions& refOptions,
			float fNodeWidth,
			uint32_t uRowIndex,
			NodeGraphCanvasResult& refResult);
		void DrawSectionRow(
			AshEngine::UIContext& refUi,
			const AshEngine::UINodeGraphSection& refSection,
			float fNodeWidth);

	private:
		AshEngine::UINodeEditor& _refNodeEditor;
		NodeGraphPortView _portView;
	};
}
