#include "Widgets/EditorTreeWidget.h"

#include "Function/Gui/UIContext.h"

#include <algorithm>
#include <string>

namespace AshEditor
{
	namespace
	{
		bool IsDropValid(
			const EditorTreeDropTargetDesc& refDesc,
			DragDropTransferId uTransferId,
			EditorTreeDropVisual eVisual)
		{
			return
				eVisual != EditorTreeDropVisual::None &&
				uTransferId != 0 &&
				(refDesc.pfnValidateDrop == nullptr || refDesc.pfnValidateDrop(uTransferId, eVisual, refDesc.pValidationUserData));
		}
	}

	void EditorTreeWidgetState::ResetDragState()
	{
		uHoverAutoExpandKey = 0;
		uPendingAutoExpandKey = 0;
		fHoverAutoExpandStartTimeSeconds = 0.0;
	}

	EditorTreeWidget::EditorTreeWidget(AshEngine::UIContext& refUi, EditorTreeWidgetState& refState, const EditorTreeWidgetStyle& refStyle)
		: _refUi(refUi)
		, _refState(refState)
		, _style(refStyle)
	{
	}

	EditorTreeWidget::ScopedTreeStyle::ScopedTreeStyle(AshEngine::UIContext& refUi, const EditorTreeWidgetStyle& refStyle)
		: _refUi(refUi)
	{
		const AshEngine::UIVec2 vecFramePadding = _refUi.get_style_frame_padding();
		const AshEngine::UIVec2 vecItemSpacing = _refUi.get_style_item_spacing();
		const float fMinPaddingY = std::max(refStyle.fRowPaddingY, 0.0f);
		const float fComputedPaddingY =
			refStyle.fRowHeight > 0.0f
			? std::max(fMinPaddingY, (refStyle.fRowHeight - _refUi.get_font_size()) * 0.5f)
			: fMinPaddingY;
		_refUi.push_style_var(
			AshEngine::UIStyleVarKind::FramePadding,
			{ vecFramePadding.x, fComputedPaddingY });
		_refUi.push_style_var(
			AshEngine::UIStyleVarKind::ItemSpacing,
			{ vecItemSpacing.x, refStyle.fRowSpacingY });
		_refUi.push_style_var(AshEngine::UIStyleVarKind::IndentSpacing, refStyle.fIndentSpacing);
		_bActive = true;
	}

	EditorTreeWidget::ScopedTreeStyle::~ScopedTreeStyle()
	{
		if (_bActive)
		{
			_refUi.pop_style_var(3);
		}
	}

	void EditorTreeWidget::ResetDragStateIfInactive()
	{
		if (!_refUi.has_drag_drop_payload())
		{
			_refState.ResetDragState();
		}
	}

	void EditorTreeWidget::PushLevel(bool bAncestorHasMoreSiblings)
	{
		_vecAncestorHasMoreSiblings.push_back(bAncestorHasMoreSiblings);
	}

	void EditorTreeWidget::PopLevel()
	{
		if (!_vecAncestorHasMoreSiblings.empty())
		{
			_vecAncestorHasMoreSiblings.pop_back();
		}
	}

	void EditorTreeWidget::TreePop()
	{
		_refUi.push_style_var(AshEngine::UIStyleVarKind::IndentSpacing, _style.fIndentSpacing);
		_refUi.tree_pop();
		_refUi.pop_style_var(1);
	}

	EditorTreeItemResult EditorTreeWidget::DrawItem(const EditorTreeItemDesc& refDesc)
	{
		const ScopedTreeStyle scopeTreeStyle(_refUi, _style);
		(void)scopeTreeStyle;

		EditorTreeItemResult result{};
		const uint64_t uItemKey = MakeIdKey(refDesc.svUniqueId);
		if (refDesc.bHasChildren && _refState.uPendingAutoExpandKey == uItemKey)
		{
			_refUi.set_next_item_open(true, AshEngine::UIConditionFlagBits::Always);
		}

		const float fRowStartX = _refUi.get_cursor_screen_pos().x;
		const std::string strUniqueId(refDesc.svUniqueId);
		_refUi.push_id(strUniqueId.c_str());

		AshEngine::UITreeNodeFlags flagsTreeNode =
			AshEngine::UITreeNodeFlagBits::OpenOnArrow |
			AshEngine::UITreeNodeFlagBits::SpanAvailWidth |
			AshEngine::UITreeNodeFlagBits::FramePadding;
		if (refDesc.bIsSelected)
		{
			flagsTreeNode |= AshEngine::UITreeNodeFlagBits::Selected;
		}
		if (!refDesc.bHasChildren)
		{
			flagsTreeNode |= AshEngine::UITreeNodeFlagBits::Leaf;
		}
		if (refDesc.bIsDefaultOpen)
		{
			flagsTreeNode |= AshEngine::UITreeNodeFlagBits::DefaultOpen;
		}

		result.bOpened = _refUi.tree_node("##tree_item", flagsTreeNode);
		result.bClicked = _refUi.is_item_clicked();
		result.bHovered = _refUi.is_item_hovered();
		result.rectItem = _refUi.get_item_rect();

		DrawRowBackground(refDesc, result);
		DrawGuides(refDesc, result, fRowStartX);
		DrawItemContent(refDesc, result, fRowStartX);

		if (refDesc.pDragSource &&
			refDesc.pDragSource->pPayloadType &&
			refDesc.pDragSource->uTransferId != 0 &&
			_refUi.begin_drag_drop_source(AshEngine::UIDragDropFlagBits::SourceAllowNullID))
		{
			_refUi.set_drag_drop_payload(
				refDesc.pDragSource->pPayloadType,
				&refDesc.pDragSource->uTransferId,
				sizeof(DragDropTransferId));
			if (refDesc.pDragSource->pPreviewText)
			{
				_refUi.text_unformatted(refDesc.pDragSource->pPreviewText);
			}
			_refUi.end_drag_drop_source();
		}

		if (refDesc.pDropTarget && refDesc.pDropTarget->pPayloadType && _refUi.begin_drag_drop_target())
		{
			const AshEngine::UIDragDropPayload payloadDrop = _refUi.accept_drag_drop_payload(
				refDesc.pDropTarget->pPayloadType,
				AshEngine::UIDragDropFlagBits::AcceptNoDrawDefaultRect | AshEngine::UIDragDropFlagBits::AcceptBeforeDelivery);
			if (payloadDrop.is_valid() && payloadDrop.data_size == sizeof(DragDropTransferId))
			{
				const DragDropTransferId uTransferId = *static_cast<const DragDropTransferId*>(payloadDrop.data);
				result.eDropVisual = ResolveDropVisual(*refDesc.pDropTarget, result.rectItem);
				bool bValidDrop = IsDropValid(*refDesc.pDropTarget, uTransferId, result.eDropVisual);

				// On delivery, if the resolved visual fails validation (mouse may have
				// shifted between preview and release), try fallback visuals.
				if (!bValidDrop && payloadDrop.is_delivery)
				{
					result.eDropVisual = ResolveDropVisualFallback(*refDesc.pDropTarget, uTransferId, result.rectItem);
					bValidDrop = result.eDropVisual != EditorTreeDropVisual::None;
				}

				if (bValidDrop)
				{
					result.bDropHovered = true;
					result.bDropDelivered = payloadDrop.is_delivery;
					result.uDropTransferId = uTransferId;
					DrawDropPreview(result.eDropVisual, result.rectItem);

					if (result.eDropVisual == EditorTreeDropVisual::Into &&
						refDesc.pDropTarget->bAutoExpandOnIntoHover &&
						refDesc.bHasChildren &&
						!result.bOpened)
					{
						UpdateAutoExpandHover(uItemKey);
					}
					else
					{
						ClearAutoExpandHover(uItemKey);
					}
				}
				else
				{
					ClearAutoExpandHover(uItemKey);
				}
			}
			else
			{
				ClearAutoExpandHover(uItemKey);
			}
			_refUi.end_drag_drop_target();
		}
		else
		{
			ClearAutoExpandHover(uItemKey);
		}

		if (result.bOpened && _refState.uPendingAutoExpandKey == uItemKey)
		{
			_refState.uPendingAutoExpandKey = 0;
			if (_refState.uHoverAutoExpandKey == uItemKey)
			{
				_refState.uHoverAutoExpandKey = 0;
				_refState.fHoverAutoExpandStartTimeSeconds = 0.0;
			}
		}

		_refUi.pop_id();
		return result;
	}

	EditorTreeDropSlotResult EditorTreeWidget::DrawDropSlot(const EditorTreeDropSlotDesc& refDesc, bool bDraggingMatchingPayload)
	{
		const ScopedTreeStyle scopeTreeStyle(_refUi, _style);
		(void)scopeTreeStyle;

		EditorTreeDropSlotResult result{};
		float fHeight = refDesc.fHeight;
		if (bDraggingMatchingPayload && refDesc.bExpandToAvailableHeightWhileDragging)
		{
			fHeight = std::max(fHeight, _refUi.get_content_region_avail().y);
		}

		const std::string strUniqueId(refDesc.svUniqueId);
		_refUi.push_id(strUniqueId.c_str());
		_refUi.dummy({ std::max(_refUi.get_content_region_avail().x, 1.0f), std::max(fHeight, 1.0f) });
		result.rectItem = _refUi.get_item_rect();

		if (refDesc.pDropTarget && refDesc.pDropTarget->pPayloadType && _refUi.begin_drag_drop_target())
		{
			const AshEngine::UIDragDropPayload payloadDrop = _refUi.accept_drag_drop_payload(
				refDesc.pDropTarget->pPayloadType,
				AshEngine::UIDragDropFlagBits::AcceptNoDrawDefaultRect | AshEngine::UIDragDropFlagBits::AcceptBeforeDelivery);
			if (payloadDrop.is_valid() && payloadDrop.data_size == sizeof(DragDropTransferId))
			{
				const DragDropTransferId uTransferId = *static_cast<const DragDropTransferId*>(payloadDrop.data);
				if (IsDropValid(*refDesc.pDropTarget, uTransferId, refDesc.ePreviewVisual))
				{
					result.eDropVisual = refDesc.ePreviewVisual;
					result.bDropHovered = true;
					result.bDropDelivered = payloadDrop.is_delivery;
					result.uDropTransferId = uTransferId;
					DrawDropPreview(result.eDropVisual, result.rectItem);
				}
			}
			_refUi.end_drag_drop_target();
		}

		_refUi.pop_id();
		return result;
	}

	uint64_t EditorTreeWidget::MakeIdKey(std::string_view svUniqueId) const
	{
		return static_cast<uint64_t>(std::hash<std::string_view>{}(svUniqueId));
	}

	float EditorTreeWidget::GetGuideColumnBaseX(float fRowStartX) const
	{
		const float fArrowCenterOffsetX = std::max(0.0f, _refUi.get_tree_node_to_label_spacing() * 0.5f);
		return
			fRowStartX -
			static_cast<float>(_vecAncestorHasMoreSiblings.size()) * _style.fIndentSpacing +
			fArrowCenterOffsetX;
	}

	EditorTreeDropVisual EditorTreeWidget::ResolveDropVisual(
		const EditorTreeDropTargetDesc& refDesc,
		const AshEngine::UIRect& refItemRect) const
	{
		const float fItemHeight = std::max(refItemRect.height, 1.0f);
		const float fZonePadding = std::clamp(fItemHeight * _style.fDropZoneRatio, 4.0f, 10.0f);
		const float fMouseY = _refUi.get_mouse_pos().y;
		if (refDesc.bAllowBefore && fMouseY <= refItemRect.y + fZonePadding)
		{
			return EditorTreeDropVisual::Before;
		}
		if (refDesc.bAllowAfter && fMouseY >= refItemRect.y + refItemRect.height - fZonePadding)
		{
			return EditorTreeDropVisual::After;
		}
		return refDesc.bAllowInto ? EditorTreeDropVisual::Into : EditorTreeDropVisual::None;
	}

	EditorTreeDropVisual EditorTreeWidget::ResolveDropVisualFallback(
		const EditorTreeDropTargetDesc& refDesc,
		DragDropTransferId uTransferId,
		const AshEngine::UIRect& refItemRect) const
	{
		// Try each allowed visual in proximity order (closest zone to mouse first).
		const float fMouseY = _refUi.get_mouse_pos().y;
		const float fCenterY = refItemRect.y + refItemRect.height * 0.5f;
		const bool bAboveCenter = fMouseY < fCenterY;

		const EditorTreeDropVisual eFallbacks[] = {
			bAboveCenter ? EditorTreeDropVisual::Before : EditorTreeDropVisual::After,
			bAboveCenter ? EditorTreeDropVisual::After : EditorTreeDropVisual::Before,
			EditorTreeDropVisual::Into
		};

		for (EditorTreeDropVisual eCandidate : eFallbacks)
		{
			if (IsDropValid(refDesc, uTransferId, eCandidate))
			{
				return eCandidate;
			}
		}
		return EditorTreeDropVisual::None;
	}

	void EditorTreeWidget::UpdateAutoExpandHover(uint64_t uItemKey)
	{
		if (_refState.uHoverAutoExpandKey != uItemKey)
		{
			_refState.uHoverAutoExpandKey = uItemKey;
			_refState.fHoverAutoExpandStartTimeSeconds = _refUi.get_time_seconds();
			return;
		}

		if (_refUi.get_time_seconds() - _refState.fHoverAutoExpandStartTimeSeconds >= _style.fAutoExpandHoverDelaySeconds)
		{
			_refState.uPendingAutoExpandKey = uItemKey;
		}
	}

	void EditorTreeWidget::ClearAutoExpandHover(uint64_t uItemKey)
	{
		if (_refState.uHoverAutoExpandKey == uItemKey)
		{
			_refState.uHoverAutoExpandKey = 0;
			_refState.fHoverAutoExpandStartTimeSeconds = 0.0;
		}
	}

	void EditorTreeWidget::DrawRowBackground(const EditorTreeItemDesc& refDesc, const EditorTreeItemResult& refResult) const
	{
		if (!refDesc.bIsSelected && !refResult.bHovered)
		{
			return;
		}

		const AshEngine::UIRect rectRowFill{
			refResult.rectItem.x + 1.0f,
			refResult.rectItem.y + 1.0f,
			std::max(0.0f, refResult.rectItem.width - 2.0f),
			std::max(0.0f, refResult.rectItem.height - 2.0f)
		};

		if (refDesc.bIsSelected)
		{
			_refUi.draw_window_rect_filled(rectRowFill, _style.colorRowSelectedFill, 4.0f);
			_refUi.draw_window_rect(rectRowFill, _style.colorRowSelectedOutline, 4.0f, 1.0f);
			return;
		}

		_refUi.draw_window_rect_filled(rectRowFill, _style.colorRowHoverFill, 4.0f);
		_refUi.draw_window_rect(rectRowFill, _style.colorRowHoverOutline, 4.0f, 1.0f);
	}

	void EditorTreeWidget::DrawDropPreview(EditorTreeDropVisual eVisual, const AshEngine::UIRect& refItemRect) const
	{
		switch (eVisual)
		{
		case EditorTreeDropVisual::Before:
			_refUi.draw_window_line(
				{ refItemRect.x, refItemRect.y },
				{ refItemRect.x + refItemRect.width, refItemRect.y },
				_style.colorDropAccent,
				2.0f);
			break;
		case EditorTreeDropVisual::After:
			_refUi.draw_window_line(
				{ refItemRect.x, refItemRect.y + refItemRect.height },
				{ refItemRect.x + refItemRect.width, refItemRect.y + refItemRect.height },
				_style.colorDropAccent,
				2.0f);
			break;
		case EditorTreeDropVisual::Into:
			_refUi.draw_window_rect(refItemRect, _style.colorDropAccent, 3.0f, 2.0f);
			break;
		default:
			break;
		}
	}

	void EditorTreeWidget::DrawGuides(const EditorTreeItemDesc& refDesc, const EditorTreeItemResult& refResult, float fRowStartX) const
	{
		const float fGuideColumnBaseX = GetGuideColumnBaseX(fRowStartX);
		if (_vecAncestorHasMoreSiblings.empty() && refDesc.bIsLastSibling)
		{
			return;
		}

		const float fY0 = refResult.rectItem.y + _style.fGuideLinePaddingY;
		const float fY1 = refResult.rectItem.y + refResult.rectItem.height - _style.fGuideLinePaddingY;
		const float fCenterY = refResult.rectItem.y + refResult.rectItem.height * 0.5f;

		const size_t uDepth = _vecAncestorHasMoreSiblings.size();
		for (size_t uIndex = 0; uIndex + 1 < uDepth; ++uIndex)
		{
			if (!_vecAncestorHasMoreSiblings[uIndex])
			{
				continue;
			}

			const float fX = fGuideColumnBaseX + static_cast<float>(uIndex) * _style.fIndentSpacing;
			_refUi.draw_window_line(
				{ fX, fY0 },
				{ fX, fY1 },
				_style.colorGuideLine,
				_style.fGuideLineThickness);
		}

		if (uDepth == 0)
		{
			return;
		}

		const float fConnectorX = fGuideColumnBaseX + static_cast<float>(uDepth - 1) * _style.fIndentSpacing;
		_refUi.draw_window_line(
			{ fConnectorX, fY0 },
			{ fConnectorX, refDesc.bIsLastSibling ? fCenterY : fY1 },
			_style.colorGuideLine,
			_style.fGuideLineThickness);

		const float fHorizontalEndX = fRowStartX - _style.fConnectorHorizontalPadding;
		if (fHorizontalEndX > fConnectorX)
		{
			_refUi.draw_window_line(
				{ fConnectorX, fCenterY },
				{ fHorizontalEndX, fCenterY },
				_style.colorGuideLine,
				_style.fGuideLineThickness);
		}
	}

	void EditorTreeWidget::DrawItemContent(const EditorTreeItemDesc& refDesc, const EditorTreeItemResult& refResult, float fRowStartX) const
	{
		const float fLabelStartX = fRowStartX + _refUi.get_tree_node_to_label_spacing();
		const float fCenterY = refResult.rectItem.y + refResult.rectItem.height * 0.5f;
		float fTextX = fLabelStartX;
		AshEngine::UITextureHandle pIcon = refResult.bOpened && refDesc.pIconWhenOpen != nullptr ? refDesc.pIconWhenOpen : refDesc.pIcon;
		if (pIcon != nullptr)
		{
			const float fIconY = fCenterY - _style.fIconSize * 0.5f;
			_refUi.draw_window_image(pIcon, { fLabelStartX, fIconY, _style.fIconSize, _style.fIconSize });
			fTextX += _style.fIconSize + _style.fIconTextSpacing;
		}

		const std::string strLabel(refDesc.svLabel);
		const AshEngine::UIVec2 vecTextSize = _refUi.calc_text_size(strLabel.c_str());
		const float fTextY = fCenterY - vecTextSize.y * 0.5f;
		_refUi.draw_window_text(
			{ fTextX, fTextY },
			_refUi.get_style_color(AshEngine::UIStyleColorKind::Text),
			strLabel.c_str());
	}
}
