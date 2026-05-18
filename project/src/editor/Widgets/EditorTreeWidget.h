#pragma once

#include "Services/DragDropTransferService.h"
#include "Function/Gui/UICommon.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	enum class EditorTreeDropVisual : uint8_t
	{
		None = 0,
		Before,
		After,
		Into
	};

	struct EditorTreeWidgetStyle
	{
		float fRowHeight = 0.0f;
		float fIndentSpacing = 14.0f;
		float fIconSize = 16.0f;
		float fIconTextSpacing = 3.0f;
		float fRowPaddingY = 5.0f;
		float fRowSpacingY = 3.0f;
		float fGuideLineThickness = 1.0f;
		float fGuideLinePaddingY = 1.0f;
		float fConnectorHorizontalPadding = 4.0f;
		float fDropZoneRatio = 0.25f;
		float fAutoExpandHoverDelaySeconds = 0.45f;
		AshEngine::UIColor colorGuideLine{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorDropAccent{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorRowHoverFill{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorRowHoverOutline{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorRowSelectedFill{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorRowSelectedOutline{ 0.0f, 0.0f, 0.0f, 0.0f };
	};

	struct EditorTreeWidgetState
	{
		uint64_t uHoverAutoExpandKey = 0;
		uint64_t uPendingAutoExpandKey = 0;
		double fHoverAutoExpandStartTimeSeconds = 0.0;

		void ResetDragState();
	};

	struct EditorTreeDragSourceDesc
	{
		const char* pPayloadType = nullptr;
		DragDropTransferId uTransferId = 0;
		const char* pPreviewText = nullptr;
	};

	struct EditorTreeDropTargetDesc
	{
		using ValidationCallback = bool (*)(DragDropTransferId uTransferId, EditorTreeDropVisual eVisual, void* pUserData);

		const char* pPayloadType = nullptr;
		bool bAllowBefore = false;
		bool bAllowAfter = false;
		bool bAllowInto = false;
		bool bAutoExpandOnIntoHover = false;
		ValidationCallback pfnValidateDrop = nullptr;
		void* pValidationUserData = nullptr;
	};

	struct EditorTreeItemDesc
	{
		std::string_view svUniqueId{};
		std::string_view svLabel{};
		AshEngine::UITextureHandle pIcon = nullptr;
		AshEngine::UITextureHandle pIconWhenOpen = nullptr;
		bool bIsSelected = false;
		bool bHasChildren = false;
		bool bIsDefaultOpen = false;
		bool bIsLastSibling = true;
		const EditorTreeDragSourceDesc* pDragSource = nullptr;
		const EditorTreeDropTargetDesc* pDropTarget = nullptr;
	};

	struct EditorTreeItemResult
	{
		bool bOpened = false;
		bool bClicked = false;
		bool bHovered = false;
		bool bDropHovered = false;
		bool bDropDelivered = false;
		EditorTreeDropVisual eDropVisual = EditorTreeDropVisual::None;
		DragDropTransferId uDropTransferId = 0;
		AshEngine::UIRect rectItem{};
	};

	struct EditorTreeDropSlotDesc
	{
		std::string_view svUniqueId{};
		float fHeight = 18.0f;
		bool bExpandToAvailableHeightWhileDragging = false;
		EditorTreeDropVisual ePreviewVisual = EditorTreeDropVisual::Before;
		const EditorTreeDropTargetDesc* pDropTarget = nullptr;
	};

	struct EditorTreeDropSlotResult
	{
		bool bDropHovered = false;
		bool bDropDelivered = false;
		EditorTreeDropVisual eDropVisual = EditorTreeDropVisual::None;
		DragDropTransferId uDropTransferId = 0;
		AshEngine::UIRect rectItem{};
	};

	class EditorTreeWidget final
	{
	public:
		EditorTreeWidget(AshEngine::UIContext& refUi, EditorTreeWidgetState& refState, const EditorTreeWidgetStyle& refStyle = {});
		~EditorTreeWidget() = default;

	public:
		void ResetDragStateIfInactive();
		void PushLevel(bool bAncestorHasMoreSiblings);
		void PopLevel();
		EditorTreeItemResult DrawItem(const EditorTreeItemDesc& refDesc);
		EditorTreeDropSlotResult DrawDropSlot(const EditorTreeDropSlotDesc& refDesc, bool bDraggingMatchingPayload);
		void TreePop();

	private:
		uint64_t MakeIdKey(std::string_view svUniqueId) const;
		float GetGuideColumnBaseX(float fRowStartX) const;
		EditorTreeDropVisual ResolveDropVisual(const EditorTreeDropTargetDesc& refDesc, const AshEngine::UIRect& refItemRect) const;
		EditorTreeDropVisual ResolveDropVisualFallback(const EditorTreeDropTargetDesc& refDesc, DragDropTransferId uTransferId, const AshEngine::UIRect& refItemRect) const;
		void UpdateAutoExpandHover(uint64_t uItemKey);
		void ClearAutoExpandHover(uint64_t uItemKey);
		void DrawRowBackground(const EditorTreeItemDesc& refDesc, const EditorTreeItemResult& refResult) const;
		void DrawDropPreview(EditorTreeDropVisual eVisual, const AshEngine::UIRect& refItemRect) const;
		void DrawGuides(const EditorTreeItemDesc& refDesc, const EditorTreeItemResult& refResult, float fRowStartX) const;
		void DrawItemContent(const EditorTreeItemDesc& refDesc, const EditorTreeItemResult& refResult, float fRowStartX) const;

	private:
		class ScopedTreeStyle final
		{
		public:
			ScopedTreeStyle(AshEngine::UIContext& refUi, const EditorTreeWidgetStyle& refStyle);
			~ScopedTreeStyle();

		private:
			AshEngine::UIContext& _refUi;
			bool _bActive = false;
		};

	private:
		AshEngine::UIContext& _refUi;
		EditorTreeWidgetState& _refState;
		EditorTreeWidgetStyle _style{};
		std::vector<bool> _vecAncestorHasMoreSiblings{};
	};
}
