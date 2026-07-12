#pragma once

#include "Function/Gui/UINodeGraph.h"

#include <filesystem>
#include <string>
#include <vector>

namespace AshEditor
{
	struct NodeGraphNodeTypePinDesc
	{
		std::string strId{};
		std::string strLabel{};
		std::string strTypeLabel{};
		AshEngine::UINodeGraphValueKind eValueKind = AshEngine::UINodeGraphValueKind::None;
		AshEngine::UINodePinShape eShape = AshEngine::UINodePinShape::Circle;
		AshEngine::UIColor color{ 0.0f, 0.0f, 0.0f, 0.0f };
	};

	struct NodeGraphNodeTypeRowDesc
	{
		std::string strLabel{};
		std::string strInputPinId{};
		std::string strOutputPinId{};
		std::string strDefaultValueText{};
		AshEngine::UIColor defaultValueColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		bool bHighlighted = false;
		bool bShowDefaultValue = true;
		bool bEditableDefaultValue = false;
		std::string strValueKey{};
		AshEngine::UINodeGraphValueKind eValueKind = AshEngine::UINodeGraphValueKind::None;
		AshEngine::UINodeGraphInlineEditorKind eEditorKind = AshEngine::UINodeGraphInlineEditorKind::None;
	};

	struct NodeGraphNodeTypeSectionDesc
	{
		std::string strLabel{};
		bool bExpanded = false;
	};

	struct NodeGraphNodeTypeDesc
	{
		std::string strId{};
		std::string strTitle{};
		std::string strSubtitle{};
		std::string strCategory{};
		std::string strTypeLabel{};
		bool bCollapsible = true;
		bool bDefaultCollapsed = false;
		AshEngine::UIColor accentColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		std::vector<NodeGraphNodeTypePinDesc> vecInputs{};
		std::vector<NodeGraphNodeTypePinDesc> vecOutputs{};
		std::vector<NodeGraphNodeTypeRowDesc> vecRows{};
		std::vector<NodeGraphNodeTypeSectionDesc> vecSections{};
	};

	struct NodeGraphNodePinMapping
	{
		std::string strTemplatePinId{};
		AshEngine::UIPinId runtimePinId = 0;
		AshEngine::UINodePinKind eKind = AshEngine::UINodePinKind::Input;
	};

	struct NodeGraphNodeCreateDesc
	{
		std::string strTypeId{};
		AshEngine::UINodeId nodeId = 0;
		AshEngine::UIPinId firstPinId = 0;
		AshEngine::UIVec2 position{};
	};

	struct NodeGraphNodeCreateResult
	{
		AshEngine::UINodeGraphNode node{};
		AshEngine::UIPinId nextPinId = 0;
		std::vector<NodeGraphNodePinMapping> vecPinMappings{};

		AshEngine::UIPinId FindPinId(const std::string& strTemplatePinId) const;
	};

	class NodeGraphNodeTypeRegistry final
	{
	public:
		bool LoadFromFile(const std::filesystem::path& pathFile, std::string& strOutError);
		void Clear();
		bool RegisterType(const NodeGraphNodeTypeDesc& refDesc);
		const NodeGraphNodeTypeDesc* FindType(const std::string& strTypeId) const;
		const std::vector<std::string>& GetTypeIds() const;

		bool CreateNode(
			const NodeGraphNodeCreateDesc& refDesc,
			NodeGraphNodeCreateResult& outResult) const;

	private:
		std::vector<NodeGraphNodeTypeDesc> _vecTypes{};
		std::vector<std::string> _vecTypeIds{};
	};
}
