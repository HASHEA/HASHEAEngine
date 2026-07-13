#include "Widgets/NodeGraph/NodeGraphCanvasWidget.h"

#include "Widgets/NodeGraph/NodeGraphLinkView.h"
#include "Widgets/NodeGraph/NodeGraphNodeView.h"

#include "Function/Gui/UIContext.h"

#include <algorithm>
#include <string>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kCreateNodePopupId = "NodeGraphCanvasCreateNode";
		constexpr const char* kContextPopupId = "NodeGraphCanvasContext";

		std::string BuildPopupId(const char* pCanvasId, const char* pPopupId)
		{
			std::string strId = pCanvasId ? pCanvasId : "##NodeGraphCanvas";
			strId += "/";
			strId += pPopupId;
			return strId;
		}

		void AddMenuAction(
			NodeGraphCanvasResult& refResult,
			NodeGraphCanvasMenuAction kind,
			const AshEngine::UIVec2& position,
			AshEngine::UINodeId nodeId = 0,
			AshEngine::UILinkId linkId = 0,
			const std::string& strTypeId = {})
		{
			NodeGraphCanvasAction action{};
			action.kind = kind;
			action.canvasPosition = position;
			action.nodeId = nodeId;
			action.linkId = linkId;
			action.strTypeId = strTypeId;
			refResult.vecMenuActions.push_back(action);
		}

		void RemoveSelectedNodeId(NodeGraphCanvasResult& refResult, AshEngine::UINodeId nodeId)
		{
			refResult.vecSelectedNodeIds.erase(
				std::remove(refResult.vecSelectedNodeIds.begin(), refResult.vecSelectedNodeIds.end(), nodeId),
				refResult.vecSelectedNodeIds.end());
		}

		void RemoveSelectedLinkId(NodeGraphCanvasResult& refResult, AshEngine::UILinkId linkId)
		{
			refResult.vecSelectedLinkIds.erase(
				std::remove(refResult.vecSelectedLinkIds.begin(), refResult.vecSelectedLinkIds.end(), linkId),
				refResult.vecSelectedLinkIds.end());
		}
	}

	NodeGraphCanvasResult NodeGraphCanvasWidget::Draw(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphModel& refGraph,
		const NodeGraphCanvasOptions& refOptions)
	{
		NodeGraphCanvasResult result{};
		if (!_nodeEditor.begin(refOptions.pCanvasId, refOptions.size))
		{
			return result;
		}
		_strCreateNodePopupId = BuildPopupId(refOptions.pCanvasId, kCreateNodePopupId);
		_strContextPopupId = BuildPopupId(refOptions.pCanvasId, kContextPopupId);

		result.bFocused = refUi.is_window_focused_with_children();
		SyncNodeLayout(refGraph);

		NodeGraphNodeView nodeView{ _nodeEditor };
		for (AshEngine::UINodeGraphNode& refNode : refGraph.nodes)
		{
			nodeView.DrawNode(refUi, refNode, refOptions, result);
		}

		NodeGraphLinkView linkView{ _nodeEditor };
		linkView.DrawLinks(refGraph);

		HandleCreateInteraction(refGraph, result);
		HandleDeleteInteraction(refGraph, result);
		CaptureSelection(result);

		for (AshEngine::UINodeGraphNode& refNode : refGraph.nodes)
		{
			refNode.position = _nodeEditor.get_node_position(refNode.id);
		}

		if (_bRequestDeleteSelection)
		{
			DeleteSelection(refGraph, result);
			_bRequestDeleteSelection = false;
		}

		if (_bRequestNavigateToContent)
		{
			_nodeEditor.navigate_to_content(0.2f);
			_bRequestNavigateToContent = false;
		}

		HandleContextMenus(refUi, refOptions, refGraph, result);

		_nodeEditor.end();
		return result;
	}

	void NodeGraphCanvasWidget::InvalidateLayout()
	{
		_setPositionedNodeIds.clear();
	}

	void NodeGraphCanvasWidget::RequestNavigateToContent()
	{
		_bRequestNavigateToContent = true;
	}

	void NodeGraphCanvasWidget::RequestDeleteSelection()
	{
		_bRequestDeleteSelection = true;
	}

	void NodeGraphCanvasWidget::SyncNodeLayout(AshEngine::UINodeGraphModel& refGraph)
	{
		for (const AshEngine::UINodeGraphNode& refNode : refGraph.nodes)
		{
			if (_setPositionedNodeIds.insert(refNode.id).second)
			{
				_nodeEditor.set_node_position(refNode.id, refNode.position);
			}
		}
	}

	void NodeGraphCanvasWidget::CaptureSelection(NodeGraphCanvasResult& refResult)
	{
		const int32_t iSelectedObjectCount = _nodeEditor.get_selected_object_count();
		if (iSelectedObjectCount <= 0)
		{
			return;
		}

		refResult.vecSelectedNodeIds.resize(static_cast<size_t>(iSelectedObjectCount));
		refResult.vecSelectedLinkIds.resize(static_cast<size_t>(iSelectedObjectCount));

		const int32_t iSelectedNodeCount =
			_nodeEditor.get_selected_nodes(refResult.vecSelectedNodeIds.data(), iSelectedObjectCount);
		const int32_t iSelectedLinkCount =
			_nodeEditor.get_selected_links(refResult.vecSelectedLinkIds.data(), iSelectedObjectCount);

		refResult.vecSelectedNodeIds.resize(static_cast<size_t>(iSelectedNodeCount));
		refResult.vecSelectedLinkIds.resize(static_cast<size_t>(iSelectedLinkCount));
	}

	void NodeGraphCanvasWidget::HandleCreateInteraction(
		AshEngine::UINodeGraphModel& refGraph,
		NodeGraphCanvasResult& refResult)
	{
		if (!_nodeEditor.begin_create())
		{
			return;
		}

		AshEngine::UIPinId uStartPin = 0;
		AshEngine::UIPinId uEndPin = 0;
		if (_nodeEditor.query_new_link(&uStartPin, &uEndPin))
		{
			AshEngine::UINodeGraphLinkPins pins{};
			if (refGraph.TryBuildLink(uStartPin, uEndPin, pins))
			{
				if (_nodeEditor.accept_new_item())
				{
					refResult.bCreateLinkRequested = true;
					refResult.createLinkPins = pins;
				}
			}
			else
			{
				_nodeEditor.reject_new_item();
			}
		}

		_nodeEditor.end_create();
	}

	void NodeGraphCanvasWidget::HandleDeleteInteraction(
		AshEngine::UINodeGraphModel& refGraph,
		NodeGraphCanvasResult& refResult)
	{
		if (!_nodeEditor.begin_delete())
		{
			return;
		}

		AshEngine::UILinkId uDeletedLink = 0;
		while (_nodeEditor.query_deleted_link(&uDeletedLink))
		{
			if (_nodeEditor.accept_deleted_item(false))
			{
				refResult.bGraphMutated = refGraph.RemoveLink(uDeletedLink) || refResult.bGraphMutated;
			}
		}

		AshEngine::UINodeId uDeletedNode = 0;
		while (_nodeEditor.query_deleted_node(&uDeletedNode))
		{
			if (_nodeEditor.accept_deleted_item(true))
			{
				refResult.bGraphMutated = refGraph.RemoveNode(uDeletedNode) || refResult.bGraphMutated;
				_setPositionedNodeIds.erase(uDeletedNode);
			}
		}

		_nodeEditor.end_delete();
	}

	void NodeGraphCanvasWidget::HandleBackgroundCreateMenu(
		AshEngine::UIContext& refUi,
		const NodeGraphCanvasOptions& refOptions,
		NodeGraphCanvasResult& refResult)
	{
		if (!refOptions.bEnableCreateMenu)
		{
			return;
		}

		_nodeEditor.suspend();
		if (_nodeEditor.show_background_context_menu())
		{
			_pendingCreateNodePosition = _nodeEditor.screen_to_canvas(refUi.get_mouse_pos());
			refUi.open_popup(_strCreateNodePopupId.c_str());
		}

		if (refUi.begin_popup(_strCreateNodePopupId.c_str()))
		{
			if (refOptions.pDrawCreateMenu)
			{
				std::string strTypeId{};
				if (refOptions.pDrawCreateMenu(refUi, _pendingCreateNodePosition, strTypeId, refOptions.pDrawCreateMenuUserData))
				{
					refResult.bCreateNodeRequested = true;
					refResult.createNodePosition = _pendingCreateNodePosition;
					refResult.strCreateNodeTypeId = strTypeId;
					AddMenuAction(
						refResult,
						NodeGraphCanvasMenuAction::AddNode,
						_pendingCreateNodePosition,
						0,
						0,
						strTypeId);
					refUi.close_current_popup();
				}
				refUi.separator();
			}
			else if (refUi.selectable("Add Node"))
			{
				refResult.bCreateNodeRequested = true;
				refResult.createNodePosition = _pendingCreateNodePosition;
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::AddNode, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			if (refUi.selectable("Delete Selected", false, AshEngine::UISelectableFlagBits::None))
			{
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::DeleteSelected, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			if (refUi.selectable("Break Links"))
			{
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::BreakLinks, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			refUi.separator();
			if (refUi.selectable("Reset View"))
			{
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::ResetView, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			if (refUi.selectable("Copy Selection"))
			{
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::CopySelection, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			if (refUi.selectable("Paste"))
			{
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::Paste, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			if (refUi.selectable("Add Comment"))
			{
				AddMenuAction(refResult, NodeGraphCanvasMenuAction::AddComment, _pendingCreateNodePosition);
				refUi.close_current_popup();
			}
			refUi.end_popup();
		}
		_nodeEditor.resume();
	}

	void NodeGraphCanvasWidget::HandleContextMenus(
		AshEngine::UIContext& refUi,
		const NodeGraphCanvasOptions& refOptions,
		AshEngine::UINodeGraphModel& refGraph,
		NodeGraphCanvasResult& refResult)
	{
		if (!refOptions.bEnableCreateMenu)
		{
			return;
		}

		_nodeEditor.suspend();
		AshEngine::UINodeId uContextNodeId = 0;
		AshEngine::UILinkId uContextLinkId = 0;
		if (_nodeEditor.show_node_context_menu(&uContextNodeId))
		{
			_uPendingContextNodeId = uContextNodeId;
			_uPendingContextLinkId = 0;
			_pendingContextPosition = _nodeEditor.screen_to_canvas(refUi.get_mouse_pos());
			refUi.open_popup(_strContextPopupId.c_str());
		}
		else if (_nodeEditor.show_link_context_menu(&uContextLinkId))
		{
			_uPendingContextNodeId = 0;
			_uPendingContextLinkId = uContextLinkId;
			_pendingContextPosition = _nodeEditor.screen_to_canvas(refUi.get_mouse_pos());
			refUi.open_popup(_strContextPopupId.c_str());
		}
		else if (_nodeEditor.show_background_context_menu())
		{
			_pendingCreateNodePosition = _nodeEditor.screen_to_canvas(refUi.get_mouse_pos());
			_uPendingContextNodeId = 0;
			_uPendingContextLinkId = 0;
			refUi.open_popup(_strCreateNodePopupId.c_str());
		}

		if (refUi.begin_popup(_strContextPopupId.c_str()))
		{
			if (_uPendingContextNodeId != 0)
			{
				AshEngine::UINodeGraphNode* pNode = refGraph.FindNode(_uPendingContextNodeId);
				const char* pCollapseLabel = pNode && pNode->bCollapsed ? "Expand Node" : "Collapse Node";
				if (refUi.selectable(pCollapseLabel))
				{
					if (pNode)
					{
						pNode->bCollapsed = !pNode->bCollapsed;
						refResult.bGraphMutated = true;
					}
					AddMenuAction(
						refResult,
						NodeGraphCanvasMenuAction::ToggleContextNodeCollapse,
						_pendingContextPosition,
						_uPendingContextNodeId);
					refUi.close_current_popup();
				}
				if (refUi.selectable("Break Node Links"))
				{
					refResult.bGraphMutated =
						refGraph.RemoveLinksConnectedToNode(_uPendingContextNodeId) || refResult.bGraphMutated;
					refResult.vecSelectedLinkIds.clear();
					AddMenuAction(
						refResult,
						NodeGraphCanvasMenuAction::BreakContextNodeLinks,
						_pendingContextPosition,
						_uPendingContextNodeId);
					refUi.close_current_popup();
				}
				if (refUi.selectable("Delete Node"))
				{
					refResult.bGraphMutated = refGraph.RemoveNode(_uPendingContextNodeId) || refResult.bGraphMutated;
					_setPositionedNodeIds.erase(_uPendingContextNodeId);
					RemoveSelectedNodeId(refResult, _uPendingContextNodeId);
					refResult.vecSelectedLinkIds.clear();
					AddMenuAction(
						refResult,
						NodeGraphCanvasMenuAction::DeleteContextNode,
						_pendingContextPosition,
						_uPendingContextNodeId);
					refUi.close_current_popup();
				}
			}
			else if (_uPendingContextLinkId != 0)
			{
				if (refUi.selectable("Delete Link"))
				{
					refResult.bGraphMutated = refGraph.RemoveLink(_uPendingContextLinkId) || refResult.bGraphMutated;
					RemoveSelectedLinkId(refResult, _uPendingContextLinkId);
					AddMenuAction(
						refResult,
						NodeGraphCanvasMenuAction::DeleteContextLink,
						_pendingContextPosition,
						0,
						_uPendingContextLinkId);
					refUi.close_current_popup();
				}
			}
			refUi.end_popup();
		}
		_nodeEditor.resume();

		HandleBackgroundCreateMenu(refUi, refOptions, refResult);
	}

	void NodeGraphCanvasWidget::DeleteSelection(
		AshEngine::UINodeGraphModel& refGraph,
		NodeGraphCanvasResult& refResult)
	{
		for (const AshEngine::UILinkId uLinkId : refResult.vecSelectedLinkIds)
		{
			refResult.bGraphMutated = refGraph.RemoveLink(uLinkId) || refResult.bGraphMutated;
		}
		for (const AshEngine::UINodeId uNodeId : refResult.vecSelectedNodeIds)
		{
			refResult.bGraphMutated = refGraph.RemoveNode(uNodeId) || refResult.bGraphMutated;
			_setPositionedNodeIds.erase(uNodeId);
		}
		refResult.vecSelectedLinkIds.clear();
		refResult.vecSelectedNodeIds.clear();
	}
}
