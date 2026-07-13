#include "Widgets/NodeGraph/NodeGraphNodeView.h"

#include "Widgets/NodeGraph/NodeGraphCanvasStyle.h"

#include "Function/Gui/UIContext.h"

#include <algorithm>
#include <string>

namespace AshEditor
{
	namespace
	{
		const AshEngine::UINodeGraphPin* FindNodePin(
			const AshEngine::UINodeGraphNode& refNode,
			AshEngine::UIPinId pinId)
		{
			for (const AshEngine::UINodeGraphPin& refPin : refNode.inputPins)
			{
				if (refPin.id == pinId)
				{
					return &refPin;
				}
			}
			for (const AshEngine::UINodeGraphPin& refPin : refNode.outputPins)
			{
				if (refPin.id == pinId)
				{
					return &refPin;
				}
			}
			return nullptr;
		}

		void AddMenuAction(
			NodeGraphCanvasResult& refResult,
			NodeGraphCanvasMenuAction kind,
			const AshEngine::UIVec2& position,
			AshEngine::UINodeId nodeId = 0,
			AshEngine::UILinkId linkId = 0)
		{
			NodeGraphCanvasAction action{};
			action.kind = kind;
			action.canvasPosition = position;
			action.nodeId = nodeId;
			action.linkId = linkId;
			refResult.vecMenuActions.push_back(action);
		}
	}

	NodeGraphNodeView::NodeGraphNodeView(AshEngine::UINodeEditor& refNodeEditor)
		: _refNodeEditor(refNodeEditor)
		, _portView(refNodeEditor)
	{
	}

	void NodeGraphNodeView::DrawNode(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		const NodeGraphCanvasOptions& refOptions,
		NodeGraphCanvasResult& refResult)
	{
		const float fNodeWidth = ResolveNodeWidth(refUi, refNode, refOptions);
		_refNodeEditor.begin_node(refNode.id);
		DrawNodeHeader(refUi, refNode, fNodeWidth, refResult);
		DrawNodeBody(refUi, refNode, refOptions, fNodeWidth, refResult);
		DrawBodyRows(refUi, refNode, refOptions, fNodeWidth, refResult);

		if (refNode.bodyRows.empty() && refNode.sections.empty() && !refNode.bCollapsed)
		{
			_portView.DrawLegacyPinRows(refUi, refNode, fNodeWidth);
		}

		_refNodeEditor.end_node();
	}

	float NodeGraphNodeView::ResolveNodeWidth(
		AshEngine::UIContext& refUi,
		const AshEngine::UINodeGraphNode& refNode,
		const NodeGraphCanvasOptions& refOptions) const
	{
		float fNodeWidth = refOptions.fNodeMinWidth;
		const AshEngine::UIVec2 titleSize = refUi.calc_text_size(refNode.title.c_str());
		fNodeWidth = std::max(fNodeWidth, titleSize.x + NodeGraphCanvasStyle::kNodePaddingX * 2.0f + 28.0f);
		for (const AshEngine::UINodeGraphBodyRow& refRow : refNode.bodyRows)
		{
			const AshEngine::UIVec2 labelSize = refUi.calc_text_size(refRow.label.c_str());
			const bool bHasColorSwatch = !NodeGraphCanvasStyle::IsColorUnset(refRow.defaultValueColor);
			const bool bHasValue = refRow.bShowDefaultValue && (bHasColorSwatch || !refRow.defaultValueText.empty());
			const float fValueBadgeWidth =
				bHasColorSwatch ? NodeGraphCanvasStyle::kNodeColorSwatchWidth : NodeGraphCanvasStyle::kNodeValueBadgeWidth;
			const float fValueWidth = bHasValue ? fValueBadgeWidth + NodeGraphCanvasStyle::kPinTextGap : 0.0f;
			fNodeWidth = std::max(
				fNodeWidth,
				labelSize.x
					+ fValueWidth
					+ NodeGraphCanvasStyle::kNodePaddingX * 2.0f
					+ NodeGraphCanvasStyle::kPinMarkerSize * 2.0f
					+ NodeGraphCanvasStyle::kPinTextGap * 3.0f);
		}
		for (const AshEngine::UINodeGraphSection& refSection : refNode.sections)
		{
			const AshEngine::UIVec2 sectionSize = refUi.calc_text_size(refSection.label.c_str());
			fNodeWidth = std::max(
				fNodeWidth,
				sectionSize.x + NodeGraphCanvasStyle::kNodePaddingX * 2.0f + 18.0f);
		}
		return fNodeWidth;
	}

	void NodeGraphNodeView::DrawNodeHeader(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		float fNodeWidth,
		NodeGraphCanvasResult& refResult)
	{
		const AshEngine::UIVec2 localStart = refUi.get_cursor_pos();
		const AshEngine::UIVec2 screenStart = refUi.get_cursor_screen_pos();
		const AshEngine::UIColor accentColor = NodeGraphCanvasStyle::ResolveAccentColor(refNode);

		std::string strMeta = refNode.typeLabel;
		if (!refNode.category.empty())
		{
			strMeta = strMeta.empty() ? refNode.category : refNode.category + "  |  " + strMeta;
		}

		const AshEngine::UIVec2 titleSize = refUi.calc_text_size(refNode.title.c_str());
		const AshEngine::UIVec2 metaSize = refUi.calc_text_size(strMeta.c_str());
		const float fVisualWidth = fNodeWidth + NodeGraphCanvasStyle::kNodeVisualBleed * 2.0f;
		const float fVisualScreenX = screenStart.x - NodeGraphCanvasStyle::kNodeVisualBleed;
		const float fVisualLocalX = localStart.x - NodeGraphCanvasStyle::kNodeVisualBleed;
		const float fMetaX =
			fVisualLocalX + fVisualWidth - metaSize.x * NodeGraphCanvasStyle::kPinTypeTextScale - NodeGraphCanvasStyle::kNodePaddingX;
		const float fTitleEndX = fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX + titleSize.x + 16.0f;
		const bool bMetaOnSecondLine =
			!strMeta.empty() && fMetaX <= fTitleEndX + NodeGraphCanvasStyle::kNodePaddingX;
		const float fHeaderHeight =
			bMetaOnSecondLine ? NodeGraphCanvasStyle::kNodeHeaderHeight + 12.0f : NodeGraphCanvasStyle::kNodeHeaderHeight;
		const float fHeaderScreenY = screenStart.y - NodeGraphCanvasStyle::kNodeHeaderTopBleed;
		const float fHeaderVisualHeight = fHeaderHeight + NodeGraphCanvasStyle::kNodeHeaderTopBleed;

		refUi.draw_window_rect_filled(
			AshEngine::UIRect{ fVisualScreenX, fHeaderScreenY, fVisualWidth, fHeaderVisualHeight },
			accentColor,
			NodeGraphCanvasStyle::kNodeRounding);
		refUi.draw_window_rect_filled(
			AshEngine::UIRect{ fVisualScreenX, screenStart.y + fHeaderHeight * 0.5f, fVisualWidth, fHeaderHeight * 0.5f },
			accentColor,
			0.0f);
		refUi.draw_window_rect_filled(
			AshEngine::UIRect{ fVisualScreenX, screenStart.y + fHeaderHeight - 1.0f, fVisualWidth, 1.0f },
			AshEngine::UIColor{ 0.0f, 0.0f, 0.0f, 0.26f },
			0.0f);

		if (refNode.bCollapsible)
		{
			DrawChevronIcon(
				refUi,
				AshEngine::UIVec2{ fVisualScreenX + NodeGraphCanvasStyle::kNodePaddingX + 5.0f, screenStart.y + 11.0f },
				!refNode.bCollapsed,
				NodeGraphCanvasStyle::kNodeHeaderTextColor);
		}
		refUi.set_cursor_pos(
			AshEngine::UIVec2{
				fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX + (refNode.bCollapsible ? 15.0f : 0.0f),
				localStart.y + 3.0f });
		refUi.push_font(AshEngine::UIFontRole::Strong);
		refUi.text_unformatted(refNode.title.c_str());
		refUi.pop_font();

		if (!strMeta.empty())
		{
			if (!bMetaOnSecondLine)
			{
				refUi.set_cursor_pos(AshEngine::UIVec2{ fMetaX, localStart.y + 7.0f });
			}
			else
			{
				refUi.set_cursor_pos(
					AshEngine::UIVec2{ fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX, localStart.y + 20.0f });
			}
			refUi.text_colored_scaled(
				NodeGraphCanvasStyle::kPinTypeTextScale,
				NodeGraphCanvasStyle::kNodeMutedTextColor,
				"%s",
				strMeta.c_str());
		}

		refUi.draw_window_line(
			AshEngine::UIVec2{ fVisualScreenX + 4.0f, screenStart.y + fHeaderHeight },
			AshEngine::UIVec2{ fVisualScreenX + fVisualWidth - 4.0f, screenStart.y + fHeaderHeight },
			AshEngine::UIColor{ 1.0f, 1.0f, 1.0f, 0.08f },
			1.0f);

		const AshEngine::UIVec2 mousePos = refUi.get_mouse_pos();
		const AshEngine::UIRect collapseHitRect{ fVisualScreenX, screenStart.y, 28.0f, fHeaderHeight };
		const bool bCollapseHovered =
			mousePos.x >= collapseHitRect.x
			&& mousePos.x <= collapseHitRect.x + collapseHitRect.width
			&& mousePos.y >= collapseHitRect.y
			&& mousePos.y <= collapseHitRect.y + collapseHitRect.height;
		if (refNode.bCollapsible && bCollapseHovered && refUi.is_mouse_clicked(AshEngine::UIMouseButton::Left))
		{
			refNode.bCollapsed = !refNode.bCollapsed;
			refResult.bGraphMutated = true;
			AddMenuAction(
				refResult,
				NodeGraphCanvasMenuAction::ToggleContextNodeCollapse,
				refNode.position,
				refNode.id);
		}

		refUi.set_cursor_pos(AshEngine::UIVec2{ localStart.x, localStart.y + fHeaderHeight - 1.0f });
		refUi.dummy(AshEngine::UIVec2{ fNodeWidth, 1.0f });
		refUi.set_cursor_pos(
			AshEngine::UIVec2{ localStart.x, localStart.y + fHeaderHeight + NodeGraphCanvasStyle::kNodeSectionGap });
	}

	void NodeGraphNodeView::DrawNodeBody(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		const NodeGraphCanvasOptions& refOptions,
		float fNodeWidth,
		NodeGraphCanvasResult& refResult)
	{
		if (!refOptions.bEnableInlineBody)
		{
			return;
		}

		const AshEngine::UIVec2 bodyStart = refUi.get_cursor_screen_pos();
		const bool bHasTextBody = !refNode.subtitle.empty() || !refNode.bodyLines.empty();
		if (bHasTextBody)
		{
			refUi.draw_window_rect_filled(
				AshEngine::UIRect{ bodyStart.x, bodyStart.y, fNodeWidth, 1.0f },
				NodeGraphCanvasStyle::kNodeBodyColor,
				0.0f);
			refUi.set_cursor_pos(
				AshEngine::UIVec2{ refUi.get_cursor_pos().x + NodeGraphCanvasStyle::kNodePaddingX, refUi.get_cursor_pos().y });
			if (!refNode.subtitle.empty())
			{
				refUi.text_colored_scaled(
					0.86f,
					NodeGraphCanvasStyle::kNodeMutedTextColor,
					"%s",
					refNode.subtitle.c_str());
			}
			for (const std::string& refLine : refNode.bodyLines)
			{
				refUi.text_colored_scaled(
					0.86f,
					NodeGraphCanvasStyle::kNodeBodyTextColor,
					"%s",
					refLine.c_str());
			}
			const AshEngine::UIVec2 bodyEnd = refUi.get_cursor_pos();
			refUi.set_cursor_pos(AshEngine::UIVec2{ bodyEnd.x - NodeGraphCanvasStyle::kNodePaddingX, bodyEnd.y + 2.0f });
		}

		if (refOptions.pDrawNodeBody)
		{
			refUi.set_cursor_pos(
				AshEngine::UIVec2{ refUi.get_cursor_pos().x + NodeGraphCanvasStyle::kNodePaddingX, refUi.get_cursor_pos().y });
			const std::string strNodeId = std::to_string(refNode.id);
			refUi.push_id(strNodeId.c_str());
			NodeGraphCanvasStyle::PushTransparentControlStyle(refUi);
			refResult.bNodeBodyEdited =
				refOptions.pDrawNodeBody(refUi, refNode, refOptions.pDrawNodeBodyUserData) || refResult.bNodeBodyEdited;
			NodeGraphCanvasStyle::PopTransparentControlStyle(refUi);
			refUi.pop_id();
			refUi.set_cursor_pos(
				AshEngine::UIVec2{ refUi.get_cursor_pos().x - NodeGraphCanvasStyle::kNodePaddingX, refUi.get_cursor_pos().y + 2.0f });
		}

		if (bHasTextBody || refOptions.pDrawNodeBody)
		{
			refUi.dummy(AshEngine::UIVec2{ fNodeWidth, 2.0f });
		}
	}

	void NodeGraphNodeView::DrawChevronIcon(
		AshEngine::UIContext& refUi,
		const AshEngine::UIVec2& screenCenter,
		bool bExpanded,
		const AshEngine::UIColor& color) const
	{
		if (bExpanded)
		{
			refUi.draw_window_line(
				AshEngine::UIVec2{ screenCenter.x - NodeGraphCanvasStyle::kNodeChevronSize, screenCenter.y - 1.5f },
				AshEngine::UIVec2{ screenCenter.x, screenCenter.y + NodeGraphCanvasStyle::kNodeChevronSize - 1.0f },
				color,
				1.4f);
			refUi.draw_window_line(
				AshEngine::UIVec2{ screenCenter.x, screenCenter.y + NodeGraphCanvasStyle::kNodeChevronSize - 1.0f },
				AshEngine::UIVec2{ screenCenter.x + NodeGraphCanvasStyle::kNodeChevronSize, screenCenter.y - 1.5f },
				color,
				1.4f);
			return;
		}

		refUi.draw_window_line(
			AshEngine::UIVec2{ screenCenter.x - 1.5f, screenCenter.y - NodeGraphCanvasStyle::kNodeChevronSize },
			AshEngine::UIVec2{ screenCenter.x + NodeGraphCanvasStyle::kNodeChevronSize - 1.0f, screenCenter.y },
			color,
			1.4f);
		refUi.draw_window_line(
			AshEngine::UIVec2{ screenCenter.x + NodeGraphCanvasStyle::kNodeChevronSize - 1.0f, screenCenter.y },
			AshEngine::UIVec2{ screenCenter.x - 1.5f, screenCenter.y + NodeGraphCanvasStyle::kNodeChevronSize },
			color,
			1.4f);
	}

	void NodeGraphNodeView::DrawBodyRows(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		const NodeGraphCanvasOptions& refOptions,
		float fNodeWidth,
		NodeGraphCanvasResult& refResult)
	{
		if (refNode.bCollapsed)
		{
			return;
		}

		uint32_t uRowIndex = 0;
		for (AshEngine::UINodeGraphBodyRow& refRow : refNode.bodyRows)
		{
			DrawBodyRow(refUi, refNode, refRow, refOptions, fNodeWidth, uRowIndex, refResult);
			++uRowIndex;
		}
		for (const AshEngine::UINodeGraphSection& refSection : refNode.sections)
		{
			DrawSectionRow(refUi, refSection, fNodeWidth);
		}
	}

	void NodeGraphNodeView::DrawBodyRow(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		AshEngine::UINodeGraphBodyRow& refRow,
		const NodeGraphCanvasOptions& refOptions,
		float fNodeWidth,
		uint32_t uRowIndex,
		NodeGraphCanvasResult& refResult)
	{
		const AshEngine::UIVec2 localStart = refUi.get_cursor_pos();
		const AshEngine::UIVec2 screenStart = refUi.get_cursor_screen_pos();
		const float fVisualWidth = fNodeWidth + NodeGraphCanvasStyle::kNodeVisualBleed * 2.0f;
		const float fVisualScreenX = screenStart.x - NodeGraphCanvasStyle::kNodeVisualBleed;
		const float fVisualLocalX = localStart.x - NodeGraphCanvasStyle::kNodeVisualBleed;
		const AshEngine::UIColor rowColor =
			uRowIndex % 2u == 0u ? NodeGraphCanvasStyle::kNodeRowColor : NodeGraphCanvasStyle::kNodeRowAltColor;
		refUi.draw_window_rect_filled(
			AshEngine::UIRect{ fVisualScreenX, screenStart.y, fVisualWidth, NodeGraphCanvasStyle::kNodeRowHeight },
			rowColor,
			0.0f);

		const AshEngine::UINodeGraphPin* pInputPin = FindNodePin(refNode, refRow.inputPin);
		const AshEngine::UINodeGraphPin* pOutputPin = FindNodePin(refNode, refRow.outputPin);
		const float fInputTextX = pInputPin
			? fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX + NodeGraphCanvasStyle::kPinMarkerSize + NodeGraphCanvasStyle::kPinTextGap
			: fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX;
		const float fOutputReserve =
			pOutputPin ? NodeGraphCanvasStyle::kPinMarkerSize + NodeGraphCanvasStyle::kPinTextGap : 0.0f;
		const bool bHasColorSwatch = !NodeGraphCanvasStyle::IsColorUnset(refRow.defaultValueColor);
		const bool bHasValue = refRow.bShowDefaultValue && (bHasColorSwatch || !refRow.defaultValueText.empty());
		const float fValueBadgeWidth =
			bHasColorSwatch ? NodeGraphCanvasStyle::kNodeColorSwatchWidth : NodeGraphCanvasStyle::kNodeValueBadgeWidth;
		const float fValueWidth = bHasValue ? fValueBadgeWidth : 0.0f;
		const float fValueX =
			fVisualLocalX + fVisualWidth - NodeGraphCanvasStyle::kNodePaddingX - fOutputReserve - fValueWidth;
		const float fTextMaxWidth = std::max(24.0f, fValueX - fInputTextX - NodeGraphCanvasStyle::kPinTextGap);

		if (pInputPin)
		{
			refUi.set_cursor_pos(
				AshEngine::UIVec2{ fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX, localStart.y + 4.0f });
			_portView.DrawPinMarker(*pInputPin);
		}
		if (pOutputPin)
		{
			refUi.set_cursor_pos(
				AshEngine::UIVec2{
					fVisualLocalX + fVisualWidth - NodeGraphCanvasStyle::kNodePaddingX - NodeGraphCanvasStyle::kPinMarkerSize,
					localStart.y + 4.0f });
			_portView.DrawPinMarker(*pOutputPin);
		}

		const AshEngine::UIVec2 labelSize = refUi.calc_text_size(refRow.label.c_str());
		const bool bOutputOnlyRow = !pInputPin && pOutputPin && !bHasValue;
		const float fLabelX = bOutputOnlyRow
			? std::max(
				fVisualLocalX + NodeGraphCanvasStyle::kNodePaddingX,
				fVisualLocalX
					+ fVisualWidth
					- NodeGraphCanvasStyle::kNodePaddingX
					- NodeGraphCanvasStyle::kPinMarkerSize
					- NodeGraphCanvasStyle::kPinTextGap
					- labelSize.x)
			: fInputTextX;
		const float fLabelMaxWidth = bOutputOnlyRow
			? std::max(
				24.0f,
				fVisualLocalX
					+ fVisualWidth
					- NodeGraphCanvasStyle::kNodePaddingX
					- NodeGraphCanvasStyle::kPinMarkerSize
					- NodeGraphCanvasStyle::kPinTextGap
					- fLabelX)
			: fTextMaxWidth;
		refUi.draw_window_text(
			AshEngine::UIVec2{ screenStart.x + fLabelX - localStart.x, screenStart.y + 3.0f },
			NodeGraphCanvasStyle::kNodeBodyTextColor,
			refRow.label.c_str(),
			fLabelMaxWidth);

		if (bHasValue && refRow.bEditableDefaultValue && refOptions.pDrawBodyRowValue)
		{
			const std::string strNodeId = std::to_string(refNode.id);
			refUi.set_cursor_pos(AshEngine::UIVec2{ fValueX, localStart.y + 1.0f });
			refUi.push_id(strNodeId.c_str());
			refUi.push_id(static_cast<int32_t>(uRowIndex));
			NodeGraphCanvasStyle::PushTransparentControlStyle(refUi);
			refUi.set_next_item_width(fValueWidth);
			refResult.bNodeBodyEdited =
				refOptions.pDrawBodyRowValue(refUi, refNode, refRow, fValueWidth, refOptions.pDrawBodyRowValueUserData)
				|| refResult.bNodeBodyEdited;
			NodeGraphCanvasStyle::PopTransparentControlStyle(refUi);
			refUi.pop_id();
			refUi.pop_id();
		}
		else if (bHasValue)
		{
			const AshEngine::UIVec2 valueScreenPos{ screenStart.x + fValueX - localStart.x, screenStart.y + 2.5f };
			if (bHasColorSwatch)
			{
				refUi.draw_window_rect_filled(
					AshEngine::UIRect{
						valueScreenPos.x,
						valueScreenPos.y,
						NodeGraphCanvasStyle::kNodeColorSwatchWidth,
						NodeGraphCanvasStyle::kNodeValueBadgeHeight },
					refRow.defaultValueColor,
					3.0f);
			}
			else
			{
				const AshEngine::UIVec2 valueTextSize = refUi.calc_text_size(refRow.defaultValueText.c_str());
				const float fValueTextX =
					valueScreenPos.x + std::max(0.0f, NodeGraphCanvasStyle::kNodeValueBadgeWidth - valueTextSize.x);
				refUi.draw_window_text(
					AshEngine::UIVec2{ fValueTextX, valueScreenPos.y + 1.0f },
					NodeGraphCanvasStyle::kNodeBodyTextColor,
					refRow.defaultValueText.c_str(),
					NodeGraphCanvasStyle::kNodeValueBadgeWidth);
			}
		}

		refUi.set_cursor_pos(AshEngine::UIVec2{ localStart.x, localStart.y + NodeGraphCanvasStyle::kNodeRowHeight });
		refUi.dummy(AshEngine::UIVec2{ fNodeWidth, 0.5f });
	}

	void NodeGraphNodeView::DrawSectionRow(
		AshEngine::UIContext& refUi,
		const AshEngine::UINodeGraphSection& refSection,
		float fNodeWidth)
	{
		const AshEngine::UIVec2 localStart = refUi.get_cursor_pos();
		const AshEngine::UIVec2 screenStart = refUi.get_cursor_screen_pos();
		refUi.draw_window_rect_filled(
			AshEngine::UIRect{ screenStart.x, screenStart.y, fNodeWidth, NodeGraphCanvasStyle::kNodeRowHeight },
			NodeGraphCanvasStyle::kNodeSectionColor,
			2.0f);
		DrawChevronIcon(
			refUi,
			AshEngine::UIVec2{ screenStart.x + NodeGraphCanvasStyle::kNodePaddingX + 6.0f, screenStart.y + 11.0f },
			refSection.bExpanded,
			NodeGraphCanvasStyle::kNodeMutedTextColor);
		refUi.draw_window_text(
			AshEngine::UIVec2{ screenStart.x + NodeGraphCanvasStyle::kNodePaddingX + 18.0f, screenStart.y + 3.0f },
			NodeGraphCanvasStyle::kNodeMutedTextColor,
			refSection.label.c_str(),
			fNodeWidth - NodeGraphCanvasStyle::kNodePaddingX * 2.0f - 18.0f);
		refUi.set_cursor_pos(AshEngine::UIVec2{ localStart.x, localStart.y + NodeGraphCanvasStyle::kNodeRowHeight });
		refUi.dummy(AshEngine::UIVec2{ fNodeWidth, 0.5f });
	}
}
