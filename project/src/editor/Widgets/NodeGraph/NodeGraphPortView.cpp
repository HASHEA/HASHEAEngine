#include "Widgets/NodeGraph/NodeGraphPortView.h"

#include "Widgets/NodeGraph/NodeGraphCanvasStyle.h"

#include "Function/Gui/UIContext.h"

#include <algorithm>

namespace AshEditor
{
	NodeGraphPortView::NodeGraphPortView(AshEngine::UINodeEditor& refNodeEditor)
		: _refNodeEditor(refNodeEditor)
	{
	}

	void NodeGraphPortView::DrawLegacyPinRows(
		AshEngine::UIContext& refUi,
		const AshEngine::UINodeGraphNode& refNode,
		float fNodeWidth)
	{
		const size_t uPinRowCount = std::max(refNode.inputPins.size(), refNode.outputPins.size());
		for (size_t uPinRowIndex = 0; uPinRowIndex < uPinRowCount; ++uPinRowIndex)
		{
			const AshEngine::UIVec2 rowStart = refUi.get_cursor_pos();
			if (uPinRowIndex < refNode.inputPins.size())
			{
				refUi.set_cursor_pos(AshEngine::UIVec2{ rowStart.x + NodeGraphCanvasStyle::kNodePaddingX, rowStart.y });
				DrawPinRow(refUi, refNode.inputPins[uPinRowIndex]);
			}
			else
			{
				refUi.dummy(AshEngine::UIVec2{ NodeGraphCanvasStyle::kPinMarkerSize, NodeGraphCanvasStyle::kNodeRowHeight });
			}

			if (uPinRowIndex < refNode.outputPins.size())
			{
				const AshEngine::UINodeGraphPin& refOutputPin = refNode.outputPins[uPinRowIndex];
				const AshEngine::UIVec2 labelSize = refUi.calc_text_size(refOutputPin.label.c_str());
				const AshEngine::UIVec2 typeSize = refUi.calc_text_size(refOutputPin.typeLabel.c_str());
				const float fTypeWidth = refOutputPin.typeLabel.empty()
					? 0.0f
					: typeSize.x * NodeGraphCanvasStyle::kPinTypeTextScale + NodeGraphCanvasStyle::kPinTextGap;
				const float fOutputWidth =
					labelSize.x + fTypeWidth + NodeGraphCanvasStyle::kPinTextGap + NodeGraphCanvasStyle::kPinMarkerSize;
				const float fOutputX =
					rowStart.x + fNodeWidth - fOutputWidth - NodeGraphCanvasStyle::kNodePaddingX;
				refUi.same_line(0.0f, 0.0f);
				if (fOutputX > refUi.get_cursor_pos().x)
				{
					refUi.set_cursor_pos(AshEngine::UIVec2{ fOutputX, rowStart.y });
				}
				DrawPinRow(refUi, refOutputPin);
			}
			refUi.set_cursor_pos(AshEngine::UIVec2{ rowStart.x, rowStart.y + NodeGraphCanvasStyle::kNodeRowHeight });
		}
	}

	void NodeGraphPortView::DrawPinRow(AshEngine::UIContext& refUi, const AshEngine::UINodeGraphPin& refPin)
	{
		_refNodeEditor.begin_pin(refPin.id, refPin.kind);
		refUi.begin_group();
		if (refPin.kind == AshEngine::UINodePinKind::Output)
		{
			if (!refPin.typeLabel.empty())
			{
				refUi.text_colored_scaled(
					NodeGraphCanvasStyle::kPinTypeTextScale,
					NodeGraphCanvasStyle::kNodeMutedTextColor,
					"%s",
					refPin.typeLabel.c_str());
				refUi.same_line(0.0f, NodeGraphCanvasStyle::kPinTextGap);
			}
			refUi.text_unformatted(refPin.label.c_str());
			refUi.same_line(0.0f, NodeGraphCanvasStyle::kPinTextGap);
			_refNodeEditor.draw_pin_marker(
				refPin.shape,
				NodeGraphCanvasStyle::ResolvePinColor(refPin),
				NodeGraphCanvasStyle::kPinMarkerSize);
		}
		else
		{
			_refNodeEditor.draw_pin_marker(
				refPin.shape,
				NodeGraphCanvasStyle::ResolvePinColor(refPin),
				NodeGraphCanvasStyle::kPinMarkerSize);
			refUi.same_line(0.0f, NodeGraphCanvasStyle::kPinTextGap);
			refUi.text_unformatted(refPin.label.c_str());
			if (!refPin.typeLabel.empty())
			{
				refUi.same_line(0.0f, NodeGraphCanvasStyle::kPinTextGap);
				refUi.text_colored_scaled(
					NodeGraphCanvasStyle::kPinTypeTextScale,
					NodeGraphCanvasStyle::kNodeMutedTextColor,
					"%s",
					refPin.typeLabel.c_str());
			}
		}
		refUi.end_group();
		_refNodeEditor.end_pin();
	}

	void NodeGraphPortView::DrawPinMarker(const AshEngine::UINodeGraphPin& refPin)
	{
		_refNodeEditor.begin_pin(refPin.id, refPin.kind);
		_refNodeEditor.draw_pin_marker(
			refPin.shape,
			NodeGraphCanvasStyle::ResolvePinColor(refPin),
			NodeGraphCanvasStyle::kPinMarkerSize);
		_refNodeEditor.end_pin();
	}
}
