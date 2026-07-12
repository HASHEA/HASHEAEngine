#include "Panels/NodeCanvasDemoPanel.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Widgets/NodeGraph/NodeGraphCanvasStyle.h"
#include "Widgets/PropertyEditor/EnumTypeRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorTypes.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UINodeId kNodeA = 1;
		constexpr AshEngine::UINodeId kNodeB = 2;
		constexpr AshEngine::UINodeId kFirstDynamicNodeId = 3;
		constexpr AshEngine::UIPinId kFirstPinId = 1;
		constexpr AshEngine::UILinkId kFirstLinkId = 1000;
		constexpr float kDetailsWidth = 300.0f;
		constexpr const char* kDemoNodeTypeFile = "product/config/editor/node_types/demo_node_types.json";
		constexpr const char* kColorParameterTypeId = "material.parameter.color";
		constexpr const char* kSurfaceOutputTypeId = "material.output.surface";
		constexpr const char* kMultiplyTypeId = "math.multiply";

		void EnsureNodeBlendModeRegistered()
		{
			if (EnumTypeRegistry::Instance().Find("NodeBlendMode"))
			{
				return;
			}
			EnumTypeDesc desc{};
			desc.strTypeName = "NodeBlendMode";
			desc.vecValues = {
				{ "Add", 0 },
				{ "Multiply", 1 },
				{ "Mix", 2 },
			};
			EnumTypeRegistry::Instance().Register(std::move(desc));
		}

		NodeGraphNodeTypeDesc BuildColorParameterNodeTypeDesc()
		{
			NodeGraphNodeTypeDesc desc{};
			desc.strId = kColorParameterTypeId;
			desc.strTitle = "BaseColor";
			desc.strSubtitle = "Blender-like rows use generic node metadata.";
			desc.strTypeLabel = "Parameter";
			desc.accentColor = AshEngine::UIColor{ 0.23f, 0.40f, 0.18f, 1.0f };
			desc.vecOutputs.push_back(NodeGraphNodeTypePinDesc{
				"value",
				"BaseColor",
				"Color",
				AshEngine::UINodeGraphValueKind::Color,
				AshEngine::UINodePinShape::Circle,
				AshEngine::UIColor{ 0.92f, 0.86f, 0.18f, 1.0f } });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{ "BaseColor", "", "value", "", {}, false, false, false });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{
				"SkinColor",
				"",
				"",
				"",
				AshEngine::UIColor{ 1.0f, 0.78f, 0.72f, 1.0f },
				false,
				true,
				true,
				"color",
				AshEngine::UINodeGraphValueKind::Color,
				AshEngine::UINodeGraphInlineEditorKind::Color });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{
				"ShadowSide",
				"",
				"",
				"0.144",
				{},
				false,
				true,
				true,
				"intensity",
				AshEngine::UINodeGraphValueKind::Scalar,
				AshEngine::UINodeGraphInlineEditorKind::Float });
			return desc;
		}

		NodeGraphNodeTypeDesc BuildSurfaceOutputNodeTypeDesc()
		{
			NodeGraphNodeTypeDesc desc{};
			desc.strId = kSurfaceOutputTypeId;
			desc.strTitle = "Material Output";
			desc.strSubtitle = "Use Details for the full property table.";
			desc.strTypeLabel = "Surface";
			desc.bCollapsible = false;
			desc.accentColor = AshEngine::UIColor{ 0.38f, 0.10f, 0.16f, 1.0f };
			desc.vecInputs.push_back(NodeGraphNodeTypePinDesc{
				"surface",
				"Surface",
				"Shader",
				AshEngine::UINodeGraphValueKind::Shader,
				AshEngine::UINodePinShape::Circle,
				AshEngine::UIColor{ 0.36f, 0.82f, 0.42f, 1.0f } });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{ "Surface", "surface", "", "", {}, false, false, false });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{ "Volume", "", "", "", {}, false, false, false });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{ "Displacement", "", "", "", {}, false, false, false });
			return desc;
		}

		NodeGraphNodeTypeDesc BuildMultiplyNodeTypeDesc()
		{
			NodeGraphNodeTypeDesc desc{};
			desc.strId = kMultiplyTypeId;
			desc.strTitle = "Math";
			desc.strSubtitle = "Right-click the canvas for more actions.";
			desc.strTypeLabel = "Multiply";
			desc.accentColor = AshEngine::UIColor{ 0.45f, 0.43f, 0.08f, 1.0f };
			desc.vecInputs.push_back(NodeGraphNodeTypePinDesc{
				"value",
				"Value",
				"Float",
				AshEngine::UINodeGraphValueKind::Scalar,
				AshEngine::UINodePinShape::Square,
				AshEngine::UIColor{ 0.76f, 0.76f, 0.76f, 1.0f } });
			desc.vecOutputs.push_back(NodeGraphNodeTypePinDesc{
				"result",
				"Result",
				"Float",
				AshEngine::UINodeGraphValueKind::Scalar,
				AshEngine::UINodePinShape::Circle,
				AshEngine::UIColor{ 0.76f, 0.76f, 0.76f, 1.0f } });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{
				"Value",
				"value",
				"result",
				"1.000",
				{},
				false,
				true,
				true,
				"intensity",
				AshEngine::UINodeGraphValueKind::Scalar,
				AshEngine::UINodeGraphInlineEditorKind::Float });
			desc.vecRows.push_back(NodeGraphNodeTypeRowDesc{
				"Clamp",
				"",
				"",
				"On",
				{},
				false,
				true,
				true,
				"enabled",
				AshEngine::UINodeGraphValueKind::Boolean,
				AshEngine::UINodeGraphInlineEditorKind::Toggle });
			return desc;
		}
	}

	NodeCanvasDemoPanel::NodeCanvasDemoPanel(EditorEventBus* pEventBus)
		: EditorPanel(EditorPanelIds::NodeCanvasDemo, EditorWindowTitles::NodeCanvasDemo)
		, _pEventBus(pEventBus)
		, _widget(_registry)
	{
		RegisterDefaultPropertyTypeEditors(_registry);
		EnsureNodeBlendModeRegistered();
		LoadNodeTypeRegistry();
		InitializeDemoGraph();
		SetOpen(false);
	}

	void NodeCanvasDemoPanel::OnUpdate()
	{
		if (!IsOpen() && _bShortcutScopeActive)
		{
			PublishShortcutScope(false);
		}
	}

	void NodeCanvasDemoPanel::InitializeDemoGraph()
	{
		_graph.nodes.clear();
		_graph.links.clear();
		_vecNodeParams.clear();
		_uSelectedNodeCount = 0;
		_uSelectedLinkCount = 0;
		_uNextNodeId = kFirstDynamicNodeId;
		_uNextPinId = kFirstPinId;
		_uNextLinkId = kFirstLinkId;

		NodeGraphNodeCreateResult colorNodeResult{};
		if (!CreateNodeFromType(kColorParameterTypeId, kNodeA, AshEngine::UIVec2{ 40.0f, 40.0f }, colorNodeResult))
		{
			HLogWarning("Node canvas demo could not create node type '{}'.", kColorParameterTypeId);
			return;
		}
		const AshEngine::UIPinId uColorOutputPin = colorNodeResult.FindPinId("value");
		_graph.nodes.push_back(std::move(colorNodeResult.node));

		NodeParamsRecord paramsA{};
		paramsA.id = kNodeA;
		paramsA.params.strName = "BaseColor";
		paramsA.params.fIntensity = 0.144f;
		paramsA.params.v3Color = glm::vec3{ 1.0f, 0.78f, 0.72f };
		_vecNodeParams.push_back(std::move(paramsA));

		NodeGraphNodeCreateResult outputNodeResult{};
		if (!CreateNodeFromType(kSurfaceOutputTypeId, kNodeB, AshEngine::UIVec2{ 320.0f, 140.0f }, outputNodeResult))
		{
			HLogWarning("Node canvas demo could not create node type '{}'.", kSurfaceOutputTypeId);
			return;
		}
		const AshEngine::UIPinId uSurfaceInputPin = outputNodeResult.FindPinId("surface");
		_graph.nodes.push_back(std::move(outputNodeResult.node));

		NodeParamsRecord paramsB{};
		paramsB.id = kNodeB;
		paramsB.params.strName = "Material Output";
		paramsB.params.v3Color = glm::vec3{ 0.2f, 0.6f, 0.9f };
		paramsB.params.iBlendMode = 2;
		_vecNodeParams.push_back(std::move(paramsB));
		if (uColorOutputPin != 0 && uSurfaceInputPin != 0)
		{
			_graph.ConnectPins(_uNextLinkId++, uColorOutputPin, uSurfaceInputPin);
		}

		_canvas.InvalidateLayout();
		_canvas.RequestNavigateToContent();
	}

	void NodeCanvasDemoPanel::LoadNodeTypeRegistry()
	{
		std::string strError{};
		if (_nodeTypeRegistry.LoadFromFile(kDemoNodeTypeFile, strError))
		{
			return;
		}

		HLogWarning("Node canvas demo failed to load node type JSON: {}", strError);
		RegisterBuiltInDemoNodeTypes();
	}

	void NodeCanvasDemoPanel::RegisterBuiltInDemoNodeTypes()
	{
		_nodeTypeRegistry.Clear();
		_nodeTypeRegistry.RegisterType(BuildColorParameterNodeTypeDesc());
		_nodeTypeRegistry.RegisterType(BuildSurfaceOutputNodeTypeDesc());
		_nodeTypeRegistry.RegisterType(BuildMultiplyNodeTypeDesc());
	}

	NodeCanvasDemoPanel::NodeParams* NodeCanvasDemoPanel::FindNodeParams(AshEngine::UINodeId id)
	{
		for (NodeParamsRecord& refRecord : _vecNodeParams)
		{
			if (refRecord.id == id)
			{
				return &refRecord.params;
			}
		}
		return nullptr;
	}

	void NodeCanvasDemoPanel::RemoveDanglingNodeParams()
	{
		_vecNodeParams.erase(
			std::remove_if(
				_vecNodeParams.begin(),
				_vecNodeParams.end(),
				[this](const NodeParamsRecord& refRecord) { return !_graph.FindNode(refRecord.id); }),
			_vecNodeParams.end());
	}

	bool NodeCanvasDemoPanel::CreateNodeFromType(
		const std::string& strTypeId,
		AshEngine::UINodeId nodeId,
		const AshEngine::UIVec2& position,
		NodeGraphNodeCreateResult& outResult)
	{
		NodeGraphNodeCreateDesc desc{};
		desc.strTypeId = strTypeId;
		desc.nodeId = nodeId;
		desc.firstPinId = _uNextPinId;
		desc.position = position;
		if (!_nodeTypeRegistry.CreateNode(desc, outResult))
		{
			return false;
		}

		_uNextPinId = outResult.nextPinId;
		return true;
	}

	void NodeCanvasDemoPanel::AddDemoNode(const AshEngine::UIVec2& position, const std::string& strTypeId)
	{
		const AshEngine::UINodeId uNodeId = _uNextNodeId;
		NodeGraphNodeCreateResult createResult{};
		const std::string strResolvedTypeId = strTypeId.empty() ? kMultiplyTypeId : strTypeId;
		if (!CreateNodeFromType(strResolvedTypeId, uNodeId, position, createResult))
		{
			HLogWarning("Node canvas demo could not create node type '{}'.", strResolvedTypeId);
			return;
		}
		++_uNextNodeId;
		if (strResolvedTypeId == kMultiplyTypeId)
		{
			createResult.node.title = "Math " + std::to_string(uNodeId);
		}
		else
		{
			createResult.node.title += " " + std::to_string(uNodeId);
		}
		_graph.nodes.push_back(std::move(createResult.node));

		NodeParamsRecord params{};
		params.id = uNodeId;
		params.params.strName = _graph.nodes.back().title;
		params.params.fIntensity = 1.0f;
		_vecNodeParams.push_back(std::move(params));
	}

	void NodeCanvasDemoPanel::ResetDemoLayout()
	{
		for (size_t uIndex = 0; uIndex < _graph.nodes.size(); ++uIndex)
		{
			AshEngine::UINodeGraphNode& refNode = _graph.nodes[uIndex];
			const float fColumn = static_cast<float>(uIndex % 3);
			const float fRow = static_cast<float>(uIndex / 3);
			refNode.position = AshEngine::UIVec2{ 60.0f + fColumn * 280.0f, 60.0f + fRow * 180.0f };
		}
		_canvas.InvalidateLayout();
		_canvas.RequestNavigateToContent();
	}

	void NodeCanvasDemoPanel::SyncNodeDisplayValues()
	{
		char szValue[32] = {};
		for (AshEngine::UINodeGraphNode& refNode : _graph.nodes)
		{
			NodeParams* pParams = FindNodeParams(refNode.id);
			if (!pParams)
			{
				continue;
			}

			refNode.title = pParams->strName;
			for (AshEngine::UINodeGraphBodyRow& refRow : refNode.bodyRows)
			{
				if (refRow.valueKey == "color")
				{
					refRow.defaultValueColor = AshEngine::UIColor{ pParams->v3Color.x, pParams->v3Color.y, pParams->v3Color.z, 1.0f };
				}
				else if (refRow.valueKey == "intensity")
				{
					std::snprintf(szValue, sizeof(szValue), "%.3f", pParams->fIntensity);
					refRow.defaultValueText = szValue;
				}
				else if (refRow.valueKey == "enabled")
				{
					refRow.defaultValueText = pParams->bEnabled ? "On" : "Off";
				}
			}
		}
	}

	void NodeCanvasDemoPanel::PublishShortcutScope(bool bActive)
	{
		if (!_pEventBus || _bShortcutScopeActive == bActive)
		{
			return;
		}

		_bShortcutScopeActive = bActive;
		EditorShortcutScopeChangedEvent event{};
		event.eScope = bActive ? EditorShortcutScope::NodeCanvasContent : EditorShortcutScope::Global;
		event.strOwnerPanelId = EditorPanelIds::NodeCanvasDemo;
		_pEventBus->Publish(event);
	}

	void NodeCanvasDemoPanel::DrawNodeDetails(AshEngine::UIContext& refUi, NodeParams& refParams)
	{
		PropertyEditorContext ctx{};
		ctx.pUi = &refUi;

		std::vector<PropertyDesc> vecDescs{};

		PropertyDesc descName{};
		descName.strLabel = "Name";
		descName.strTypeId = "string";
		descName.pValue = &refParams.strName;
		vecDescs.push_back(descName);

		PropertyDesc descEnabled{};
		descEnabled.strLabel = "Enabled";
		descEnabled.strTypeId = "bool";
		descEnabled.pValue = &refParams.bEnabled;
		vecDescs.push_back(descEnabled);

		PropertyDesc descIntensity{};
		descIntensity.strLabel = "Intensity";
		descIntensity.strTypeId = "float";
		descIntensity.pValue = &refParams.fIntensity;
		descIntensity.meta.fSpeed = 0.01f;
		descIntensity.meta.fMin = 0.0f;
		descIntensity.meta.fMax = 10.0f;
		vecDescs.push_back(descIntensity);

		PropertyDesc descColor{};
		descColor.strLabel = "Color";
		descColor.strTypeId = "color3";
		descColor.pValue = &refParams.v3Color;
		vecDescs.push_back(descColor);

		PropertyDesc descBlend{};
		descBlend.strLabel = "Blend Mode";
		descBlend.strTypeId = "enum:NodeBlendMode";
		descBlend.pValue = &refParams.iBlendMode;
		vecDescs.push_back(descBlend);

		_widget.DrawFields(vecDescs, ctx);
	}

	bool NodeCanvasDemoPanel::DrawCreateMenu(
		AshEngine::UIContext& refUi,
		const AshEngine::UIVec2& createNodePosition,
		std::string& strOutTypeId)
	{
		(void)createNodePosition;

		refUi.text_unformatted("Add Node");
		refUi.separator();
		const std::vector<std::string>& vecTypeIds = _nodeTypeRegistry.GetTypeIds();
		for (const std::string& strTypeId : vecTypeIds)
		{
			const NodeGraphNodeTypeDesc* pDesc = _nodeTypeRegistry.FindType(strTypeId);
			const char* pLabel = pDesc && !pDesc->strTitle.empty() ? pDesc->strTitle.c_str() : strTypeId.c_str();
			const std::string strSelectableId = std::string{ pLabel } + "##" + strTypeId;
			if (refUi.selectable(strSelectableId.c_str()))
			{
				strOutTypeId = strTypeId;
				return true;
			}
		}
		if (vecTypeIds.empty())
		{
			refUi.text_unformatted("No node types loaded.");
		}
		return false;
	}

	bool NodeCanvasDemoPanel::DrawInlineNodeBody(AshEngine::UIContext& refUi, AshEngine::UINodeGraphNode& refNode)
	{
		NodeParams* pParams = FindNodeParams(refNode.id);
		if (!pParams)
		{
			return false;
		}

		bool bChanged = false;
		refUi.set_next_item_width(150.0f);
		if (refUi.input_text("Name", pParams->strName))
		{
			refNode.title = pParams->strName;
			bChanged = true;
		}
		refUi.set_next_item_width(150.0f);
		bChanged = refUi.drag_float("Intensity", pParams->fIntensity, 0.01f, 0.0f, 10.0f, "%.2f") || bChanged;
		bChanged = refUi.checkbox("Enabled", pParams->bEnabled) || bChanged;
		return bChanged;
	}

	bool NodeCanvasDemoPanel::DrawInlineBodyRowValue(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		AshEngine::UINodeGraphBodyRow& refRow,
		float fValueWidth)
	{
		NodeParams* pParams = FindNodeParams(refNode.id);
		if (!pParams)
		{
			return false;
		}

		if (refRow.editorKind == AshEngine::UINodeGraphInlineEditorKind::Color && refRow.valueKey == "color")
		{
			const AshEngine::UIColor swatchColor{ pParams->v3Color.x, pParams->v3Color.y, pParams->v3Color.z, 1.0f };
			const AshEngine::UIVec2 swatchPos = refUi.get_cursor_screen_pos();
			refUi.draw_window_rect_filled(
				AshEngine::UIRect{ swatchPos.x + 2.0f, swatchPos.y + 2.0f, fValueWidth - 4.0f, 12.0f },
				swatchColor,
				2.0f);
			NodeGraphCanvasStyle::PushTransparentButtonStyle(refUi);
			if (refUi.button("##ColorSwatch", AshEngine::UIVec2{ fValueWidth, 16.0f }))
			{
				refUi.open_popup("Color");
			}
			NodeGraphCanvasStyle::PopTransparentButtonStyle(refUi);

			if (refUi.begin_popup("Color"))
			{
				const bool bChanged = refUi.color_edit3("##Color", &pParams->v3Color.x);
				refUi.end_popup();
				return bChanged;
			}
			return false;
		}

		if (refRow.editorKind == AshEngine::UINodeGraphInlineEditorKind::Float && refRow.valueKey == "intensity")
		{
			refUi.set_next_item_width(fValueWidth);
			return refUi.drag_float("##Float", pParams->fIntensity, 0.01f, 0.0f, 10.0f, "%.3f");
		}

		if (refRow.editorKind == AshEngine::UINodeGraphInlineEditorKind::Toggle && refRow.valueKey == "enabled")
		{
			const char* pLabel = pParams->bEnabled ? "On" : "Off";
			NodeGraphCanvasStyle::PushTransparentButtonStyle(refUi);
			if (refUi.button(pLabel, AshEngine::UIVec2{ fValueWidth, 16.0f }))
			{
				pParams->bEnabled = !pParams->bEnabled;
				NodeGraphCanvasStyle::PopTransparentButtonStyle(refUi);
				return true;
			}
			NodeGraphCanvasStyle::PopTransparentButtonStyle(refUi);
		}

		return false;
	}

	bool NodeCanvasDemoPanel::DrawCreateMenuCallback(
		AshEngine::UIContext& refUi,
		const AshEngine::UIVec2& createNodePosition,
		std::string& strOutTypeId,
		void* pUserData)
	{
		NodeCanvasDemoPanel* pPanel = static_cast<NodeCanvasDemoPanel*>(pUserData);
		return pPanel ? pPanel->DrawCreateMenu(refUi, createNodePosition, strOutTypeId) : false;
	}

	bool NodeCanvasDemoPanel::DrawInlineNodeBodyCallback(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		void* pUserData)
	{
		NodeCanvasDemoPanel* pPanel = static_cast<NodeCanvasDemoPanel*>(pUserData);
		return pPanel ? pPanel->DrawInlineNodeBody(refUi, refNode) : false;
	}

	bool NodeCanvasDemoPanel::DrawInlineBodyRowValueCallback(
		AshEngine::UIContext& refUi,
		AshEngine::UINodeGraphNode& refNode,
		AshEngine::UINodeGraphBodyRow& refRow,
		float fValueWidth,
		void* pUserData)
	{
		NodeCanvasDemoPanel* pPanel = static_cast<NodeCanvasDemoPanel*>(pUserData);
		return pPanel ? pPanel->DrawInlineBodyRowValue(refUi, refNode, refRow, fValueWidth) : false;
	}

	void NodeCanvasDemoPanel::HandleCanvasMenuActions(const NodeGraphCanvasResult& refCanvasResult)
	{
		for (const NodeGraphCanvasAction& refAction : refCanvasResult.vecMenuActions)
		{
			switch (refAction.kind)
			{
			case NodeGraphCanvasMenuAction::DeleteSelected:
				for (const AshEngine::UILinkId uLinkId : refCanvasResult.vecSelectedLinkIds)
				{
					_graph.RemoveLink(uLinkId);
				}
				for (const AshEngine::UINodeId uNodeId : refCanvasResult.vecSelectedNodeIds)
				{
					_graph.RemoveNode(uNodeId);
				}
				RemoveDanglingNodeParams();
				_strLastCanvasAction = "Deleted selected graph item(s).";
				break;
			case NodeGraphCanvasMenuAction::DeleteContextNode:
				RemoveDanglingNodeParams();
				_strLastCanvasAction = "Deleted context node.";
				break;
			case NodeGraphCanvasMenuAction::DeleteContextLink:
				_strLastCanvasAction = "Deleted context link.";
				break;
			case NodeGraphCanvasMenuAction::BreakLinks:
				for (const AshEngine::UILinkId uLinkId : refCanvasResult.vecSelectedLinkIds)
				{
					_graph.RemoveLink(uLinkId);
				}
				for (const AshEngine::UINodeId uNodeId : refCanvasResult.vecSelectedNodeIds)
				{
					_graph.RemoveLinksConnectedToNode(uNodeId);
				}
				_strLastCanvasAction = "Broke selected link connection(s).";
				break;
			case NodeGraphCanvasMenuAction::BreakContextNodeLinks:
				_strLastCanvasAction = "Broke context node links.";
				break;
			case NodeGraphCanvasMenuAction::ToggleContextNodeCollapse:
				_strLastCanvasAction = "Toggled node collapse.";
				break;
			case NodeGraphCanvasMenuAction::ResetView:
				_canvas.RequestNavigateToContent();
				_strLastCanvasAction = "Requested canvas view reset.";
				break;
			case NodeGraphCanvasMenuAction::CopySelection:
				_strLastCanvasAction = "Copy Selection is reserved for graph clipboard.";
				break;
			case NodeGraphCanvasMenuAction::Paste:
				_strLastCanvasAction = "Paste is reserved for graph clipboard.";
				break;
			case NodeGraphCanvasMenuAction::AddComment:
				_strLastCanvasAction = "Add Comment is reserved for graph comment boxes.";
				break;
			case NodeGraphCanvasMenuAction::AddNode:
			case NodeGraphCanvasMenuAction::None:
			default:
				break;
			}
		}
	}

	void NodeCanvasDemoPanel::OnGui(const EditorFrameContext& refFrameContext)
	{
		if (!BeginPanelWindow(refFrameContext))
		{
			EndPanelWindow(refFrameContext);
			return;
		}
		if (!refFrameContext.pUiContext)
		{
			EndPanelWindow(refFrameContext);
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		refUi.text("Nodes: %zu  Links: %zu", _graph.nodes.size(), _graph.links.size());
		refUi.same_line();
		if (refUi.button("Add Node"))
		{
			const float fNodeIndex = static_cast<float>(_graph.nodes.size());
			AddDemoNode(
				AshEngine::UIVec2{ 160.0f + fNodeIndex * 80.0f, 80.0f + fNodeIndex * 40.0f },
				kMultiplyTypeId);
		}
		refUi.same_line();
		if (refUi.button("Reset Layout"))
		{
			ResetDemoLayout();
		}
		refUi.same_line();
		if (refUi.button("Zoom To Content"))
		{
			_canvas.RequestNavigateToContent();
		}
		refUi.same_line();
		refUi.checkbox("Inline Properties", _bShowInlineNodeProperties);
		refUi.same_line();
		refUi.begin_disabled(_uSelectedNodeCount == 0 && _uSelectedLinkCount == 0);
		if (refUi.button("Delete Selected"))
		{
			_canvas.RequestDeleteSelection();
		}
		refUi.end_disabled();
		refUi.same_line();
		refUi.text_unformatted("Drag pins to link. Right-click canvas to add. Del deletes graph selection only.");
		refUi.separator();
		SyncNodeDisplayValues();

		const AshEngine::UIVec2 avail = refUi.get_content_region_avail();
		float fCanvasWidth = avail.x - kDetailsWidth - 8.0f;
		if (fCanvasWidth < 200.0f)
		{
			fCanvasWidth = avail.x * 0.6f;
		}

		NodeGraphCanvasOptions canvasOptions{};
		canvasOptions.pCanvasId = "##NodeCanvasDemo";
		canvasOptions.size = AshEngine::UIVec2{ fCanvasWidth, avail.y };
		canvasOptions.bEnableInlineBody = _bShowInlineNodeProperties;
		canvasOptions.pDrawNodeBody = &NodeCanvasDemoPanel::DrawInlineNodeBodyCallback;
		canvasOptions.pDrawNodeBodyUserData = this;
		canvasOptions.pDrawCreateMenu = &NodeCanvasDemoPanel::DrawCreateMenuCallback;
		canvasOptions.pDrawCreateMenuUserData = this;
		if (_bShowInlineNodeProperties)
		{
			canvasOptions.pDrawBodyRowValue = &NodeCanvasDemoPanel::DrawInlineBodyRowValueCallback;
			canvasOptions.pDrawBodyRowValueUserData = this;
		}
		NodeGraphCanvasResult canvasResult = _canvas.Draw(refUi, _graph, canvasOptions);
		PublishShortcutScope(canvasResult.bFocused);

		if (canvasResult.bCreateLinkRequested)
		{
			_graph.ConnectPins(_uNextLinkId++, canvasResult.createLinkPins.outputPin, canvasResult.createLinkPins.inputPin);
		}
		if (canvasResult.bCreateNodeRequested)
		{
			AddDemoNode(canvasResult.createNodePosition, canvasResult.strCreateNodeTypeId);
		}
		if (canvasResult.bGraphMutated)
		{
			RemoveDanglingNodeParams();
		}
		HandleCanvasMenuActions(canvasResult);
		_uSelectedNodeCount = canvasResult.vecSelectedNodeIds.size();
		_uSelectedLinkCount = canvasResult.vecSelectedLinkIds.size();

		refUi.same_line();

		if (refUi.begin_child("##NodeDetails", AshEngine::UIVec2{ kDetailsWidth, avail.y }, AshEngine::UIChildFlagBits::Border))
		{
			refUi.text_unformatted("Details");
			refUi.separator();
			if (canvasResult.vecSelectedNodeIds.size() == 1)
			{
				const AshEngine::UINodeId uSelectedNodeId = canvasResult.vecSelectedNodeIds[0];
				NodeParams* pParams = FindNodeParams(uSelectedNodeId);
				if (pParams)
				{
					DrawNodeDetails(refUi, *pParams);
					AshEngine::UINodeGraphNode* pNode = _graph.FindNode(uSelectedNodeId);
					if (pNode)
					{
						pNode->title = pParams->strName;
					}
				}
			}
			else if (canvasResult.vecSelectedNodeIds.size() > 1 || canvasResult.vecSelectedLinkIds.size() > 0)
			{
				refUi.text(
					"Selected: %zu node(s), %zu link(s)",
					canvasResult.vecSelectedNodeIds.size(),
					canvasResult.vecSelectedLinkIds.size());
			}
			else
			{
				refUi.text_unformatted("Select a node to edit, or right-click the canvas to add one.");
			}
			if (!_strLastCanvasAction.empty())
			{
				refUi.separator();
				refUi.text_colored_scaled(0.86f, AshEngine::UIColor{ 0.72f, 0.78f, 0.88f, 1.0f }, "%s", _strLastCanvasAction.c_str());
			}
		}
		refUi.end_child();

		EndPanelWindow(refFrameContext);
	}
}
