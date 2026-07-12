#pragma once

#include "Function/Gui/UINodeEditor.h"
#include "Function/Gui/UINodeGraph.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	enum class NodeGraphCanvasMenuAction : uint8_t
	{
		None = 0,
		AddNode,
		DeleteSelected,
		DeleteContextNode,
		DeleteContextLink,
		ResetView,
		BreakLinks,
		BreakContextNodeLinks,
		ToggleContextNodeCollapse,
		CopySelection,
		Paste,
		AddComment
	};

	struct NodeGraphCanvasAction
	{
		NodeGraphCanvasMenuAction kind = NodeGraphCanvasMenuAction::None;
		AshEngine::UIVec2 canvasPosition{};
		AshEngine::UINodeId nodeId = 0;
		AshEngine::UILinkId linkId = 0;
		std::string strTypeId{};
	};

	using NodeGraphDrawNodeBodyCallback = bool (*)(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		void* pUserData);

	using NodeGraphDrawBodyRowValueCallback = bool (*)(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		AshEngine::UINodeGraphBodyRow& refRow,
		float fValueWidth,
		void* pUserData);

	using NodeGraphDrawCreateMenuCallback = bool (*)(
		AshEngine::UIContext& refUi,
		const AshEngine::UIVec2& createNodePosition,
		std::string& strOutTypeId,
		void* pUserData);

	struct NodeGraphCanvasOptions
	{
		const char* pCanvasId = "##NodeGraphCanvas";
		AshEngine::UIVec2 size{};
		float fNodeMinWidth = 160.0f;
		bool bEnableCreateMenu = true;
		bool bEnableInlineBody = false;
		NodeGraphDrawNodeBodyCallback pDrawNodeBody = nullptr;
		void* pDrawNodeBodyUserData = nullptr;
		NodeGraphDrawBodyRowValueCallback pDrawBodyRowValue = nullptr;
		void* pDrawBodyRowValueUserData = nullptr;
		NodeGraphDrawCreateMenuCallback pDrawCreateMenu = nullptr;
		void* pDrawCreateMenuUserData = nullptr;
	};

	struct NodeGraphCanvasResult
	{
		bool bFocused = false;
		bool bGraphMutated = false;
		bool bCreateNodeRequested = false;
		bool bCreateLinkRequested = false;
		bool bNodeBodyEdited = false;
		AshEngine::UIVec2 createNodePosition{};
		std::string strCreateNodeTypeId{};
		AshEngine::UINodeGraphLinkPins createLinkPins{};
		std::vector<AshEngine::UINodeId> vecSelectedNodeIds{};
		std::vector<AshEngine::UILinkId> vecSelectedLinkIds{};
		std::vector<NodeGraphCanvasAction> vecMenuActions{};
	};

	class NodeGraphCanvasWidget
	{
	public:
		NodeGraphCanvasResult Draw(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphModel& refGraph,
			const NodeGraphCanvasOptions& refOptions);

		void InvalidateLayout();
		void RequestNavigateToContent();
		void RequestDeleteSelection();

	private:
		void SyncNodeLayout(AshEngine::UINodeGraphModel& refGraph);
		void CaptureSelection(NodeGraphCanvasResult& refResult);
		void HandleCreateInteraction(AshEngine::UINodeGraphModel& refGraph, NodeGraphCanvasResult& refResult);
		void HandleDeleteInteraction(AshEngine::UINodeGraphModel& refGraph, NodeGraphCanvasResult& refResult);
		void HandleBackgroundCreateMenu(AshEngine::UIContext& refUi, const NodeGraphCanvasOptions& refOptions, NodeGraphCanvasResult& refResult);
		void HandleContextMenus(AshEngine::UIContext& refUi, const NodeGraphCanvasOptions& refOptions, AshEngine::UINodeGraphModel& refGraph, NodeGraphCanvasResult& refResult);
		void DeleteSelection(AshEngine::UINodeGraphModel& refGraph, NodeGraphCanvasResult& refResult);

	private:
		AshEngine::UINodeEditor _nodeEditor{};
		std::unordered_set<AshEngine::UINodeId> _setPositionedNodeIds{};
		bool _bRequestNavigateToContent = false;
		bool _bRequestDeleteSelection = false;
		AshEngine::UIVec2 _pendingCreateNodePosition{};
		AshEngine::UIVec2 _pendingContextPosition{};
		AshEngine::UINodeId _uPendingContextNodeId = 0;
		AshEngine::UILinkId _uPendingContextLinkId = 0;
		std::string _strCreateNodePopupId{};
		std::string _strContextPopupId{};
	};
}
