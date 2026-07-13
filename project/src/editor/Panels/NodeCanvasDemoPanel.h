#pragma once

#include "Core/EditorPanel.h"
#include "Function/Gui/UINodeGraph.h"
#include "Widgets/NodeGraph/NodeGraphCanvasWidget.h"
#include "Widgets/NodeGraph/NodeGraphNodeTypeRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorWidget.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace AshEditor
{
	class EditorEventBus;

	// Development-only showcase that drives the real imgui-node-editor library through the
	// engine-side AshEngine::UINodeEditor facade, and feeds the selected node's parameters
	// into the generic PropertyEditorWidget (node Details integration).
	class NodeCanvasDemoPanel final : public EditorPanel
	{
	public:
		explicit NodeCanvasDemoPanel(EditorEventBus* pEventBus = nullptr);

	public:
		void OnUpdate() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;

	private:
		// Editable parameters carried by a node, shown in the Details pane via PropertyEditorWidget.
		struct NodeParams
		{
			std::string strName{};
			bool bEnabled = true;
			float fIntensity = 1.0f;
			glm::vec3 v3Color{ 0.8f, 0.6f, 0.2f };
			int32_t iBlendMode = 0;
		};

		struct NodeParamsRecord
		{
			AshEngine::UINodeId id = 0;
			NodeParams params{};
		};

	private:
		void InitializeDemoGraph();
		void LoadNodeTypeRegistry();
		void RegisterBuiltInDemoNodeTypes();
		NodeParams* FindNodeParams(AshEngine::UINodeId id);
		void RemoveDanglingNodeParams();
		bool CreateNodeFromType(
			const std::string& strTypeId,
			AshEngine::UINodeId nodeId,
			const AshEngine::UIVec2& position,
			NodeGraphNodeCreateResult& outResult);
		void AddDemoNode(const AshEngine::UIVec2& position, const std::string& strTypeId);
		void ResetDemoLayout();
		void PublishShortcutScope(bool bActive);
		void SyncNodeDisplayValues();
		void DrawNodeDetails(AshEngine::UIContext& refUi, NodeParams& refParams);
		bool DrawCreateMenu(AshEngine::UIContext& refUi, const AshEngine::UIVec2& createNodePosition, std::string& strOutTypeId);
		bool DrawInlineNodeBody(AshEngine::UIContext& refUi, AshEngine::UINodeGraphNode& refNode);
		bool DrawInlineBodyRowValue(AshEngine::UIContext& refUi, AshEngine::UINodeGraphNode& refNode, AshEngine::UINodeGraphBodyRow& refRow, float fValueWidth);
		void HandleCanvasMenuActions(const NodeGraphCanvasResult& refCanvasResult);

	private:
		static bool DrawCreateMenuCallback(
			AshEngine::UIContext& refUi,
			const AshEngine::UIVec2& createNodePosition,
			std::string& strOutTypeId,
			void* pUserData);
		static bool DrawInlineNodeBodyCallback(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			void* pUserData);
		static bool DrawInlineBodyRowValueCallback(
			AshEngine::UIContext& refUi,
			AshEngine::UINodeGraphNode& refNode,
			AshEngine::UINodeGraphBodyRow& refRow,
			float fValueWidth,
			void* pUserData);

	private:
		AshEngine::UINodeGraphModel _graph{};
		NodeGraphCanvasWidget _canvas{};
		NodeGraphNodeTypeRegistry _nodeTypeRegistry{};
		std::vector<NodeParamsRecord> _vecNodeParams{};
		AshEngine::UINodeId _uNextNodeId = 1;
		AshEngine::UIPinId _uNextPinId = 1;
		AshEngine::UILinkId _uNextLinkId = 1;
		size_t _uSelectedNodeCount = 0;
		size_t _uSelectedLinkCount = 0;
		bool _bShortcutScopeActive = false;
		bool _bShowInlineNodeProperties = false;
		EditorEventBus* _pEventBus = nullptr;
		std::string _strLastCanvasAction{};

		PropertyEditorRegistry _registry{};
		PropertyEditorWidget _widget;
	};
}
