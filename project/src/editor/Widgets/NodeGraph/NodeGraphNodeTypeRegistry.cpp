#include "Widgets/NodeGraph/NodeGraphNodeTypeRegistry.h"

#include <fstream>
#include <json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;

		AshEngine::UIColor ParseColor(const json& value)
		{
			if (!value.is_array() || value.size() < 3)
			{
				return AshEngine::UIColor{ 0.0f, 0.0f, 0.0f, 0.0f };
			}

			AshEngine::UIColor color{};
			color.r = value.at(0).get<float>();
			color.g = value.at(1).get<float>();
			color.b = value.at(2).get<float>();
			color.a = value.size() >= 4 ? value.at(3).get<float>() : 1.0f;
			return color;
		}

		AshEngine::UINodeGraphValueKind ParseValueKind(const std::string& strValue)
		{
			if (strValue == "scalar" || strValue == "float")
			{
				return AshEngine::UINodeGraphValueKind::Scalar;
			}
			if (strValue == "vector")
			{
				return AshEngine::UINodeGraphValueKind::Vector;
			}
			if (strValue == "color")
			{
				return AshEngine::UINodeGraphValueKind::Color;
			}
			if (strValue == "bool" || strValue == "boolean")
			{
				return AshEngine::UINodeGraphValueKind::Boolean;
			}
			if (strValue == "texture")
			{
				return AshEngine::UINodeGraphValueKind::Texture;
			}
			if (strValue == "shader")
			{
				return AshEngine::UINodeGraphValueKind::Shader;
			}
			if (strValue == "string" || strValue == "text")
			{
				return AshEngine::UINodeGraphValueKind::String;
			}
			return AshEngine::UINodeGraphValueKind::None;
		}

		AshEngine::UINodeGraphInlineEditorKind ParseEditorKind(const std::string& strValue)
		{
			if (strValue == "float" || strValue == "scalar")
			{
				return AshEngine::UINodeGraphInlineEditorKind::Float;
			}
			if (strValue == "color")
			{
				return AshEngine::UINodeGraphInlineEditorKind::Color;
			}
			if (strValue == "toggle" || strValue == "bool" || strValue == "boolean")
			{
				return AshEngine::UINodeGraphInlineEditorKind::Toggle;
			}
			if (strValue == "text" || strValue == "string")
			{
				return AshEngine::UINodeGraphInlineEditorKind::Text;
			}
			return AshEngine::UINodeGraphInlineEditorKind::None;
		}

		AshEngine::UINodePinShape ParsePinShape(const std::string& strValue)
		{
			if (strValue == "square")
			{
				return AshEngine::UINodePinShape::Square;
			}
			return AshEngine::UINodePinShape::Circle;
		}

		NodeGraphNodeTypePinDesc ParsePinDesc(const json& entry)
		{
			NodeGraphNodeTypePinDesc desc{};
			desc.strId = entry.value("id", std::string{});
			desc.strLabel = entry.value("label", desc.strId);
			desc.strTypeLabel = entry.value("typeLabel", std::string{});
			desc.eValueKind = ParseValueKind(entry.value("valueKind", std::string{}));
			desc.eShape = ParsePinShape(entry.value("shape", std::string{}));
			if (entry.contains("color"))
			{
				desc.color = ParseColor(entry["color"]);
			}
			return desc;
		}

		NodeGraphNodeTypeRowDesc ParseRowDesc(const json& entry)
		{
			NodeGraphNodeTypeRowDesc desc{};
			desc.strLabel = entry.value("label", std::string{});
			desc.strInputPinId = entry.value("input", std::string{});
			desc.strOutputPinId = entry.value("output", std::string{});
			desc.strDefaultValueText = entry.value("defaultText", std::string{});
			if (entry.contains("defaultColor"))
			{
				desc.defaultValueColor = ParseColor(entry["defaultColor"]);
			}
			desc.bHighlighted = entry.value("highlighted", false);
			desc.bShowDefaultValue = entry.value("showDefault", true);
			desc.bEditableDefaultValue = entry.value("editable", false);
			desc.strValueKey = entry.value("valueKey", std::string{});
			desc.eValueKind = ParseValueKind(entry.value("valueKind", std::string{}));
			desc.eEditorKind = ParseEditorKind(entry.value("editor", std::string{}));
			return desc;
		}

		bool ParseNodeType(const json& entry, NodeGraphNodeTypeDesc& outDesc)
		{
			if (!entry.is_object())
			{
				return false;
			}

			outDesc = {};
			outDesc.strId = entry.value("id", std::string{});
			if (outDesc.strId.empty())
			{
				return false;
			}

			outDesc.strTitle = entry.value("title", outDesc.strId);
			outDesc.strSubtitle = entry.value("subtitle", std::string{});
			outDesc.strCategory = entry.value("category", std::string{});
			outDesc.strTypeLabel = entry.value("typeLabel", std::string{});
			outDesc.bCollapsible = entry.value("collapsible", true);
			outDesc.bDefaultCollapsed = entry.value("defaultCollapsed", false);
			if (entry.contains("accentColor"))
			{
				outDesc.accentColor = ParseColor(entry["accentColor"]);
			}

			if (entry.contains("inputs") && entry["inputs"].is_array())
			{
				for (const json& pinEntry : entry["inputs"])
				{
					NodeGraphNodeTypePinDesc pin = ParsePinDesc(pinEntry);
					if (!pin.strId.empty())
					{
						outDesc.vecInputs.push_back(std::move(pin));
					}
				}
			}
			if (entry.contains("outputs") && entry["outputs"].is_array())
			{
				for (const json& pinEntry : entry["outputs"])
				{
					NodeGraphNodeTypePinDesc pin = ParsePinDesc(pinEntry);
					if (!pin.strId.empty())
					{
						outDesc.vecOutputs.push_back(std::move(pin));
					}
				}
			}
			if (entry.contains("rows") && entry["rows"].is_array())
			{
				for (const json& rowEntry : entry["rows"])
				{
					NodeGraphNodeTypeRowDesc row = ParseRowDesc(rowEntry);
					if (!row.strLabel.empty())
					{
						outDesc.vecRows.push_back(std::move(row));
					}
				}
			}
			if (entry.contains("sections") && entry["sections"].is_array())
			{
				for (const json& sectionEntry : entry["sections"])
				{
					NodeGraphNodeTypeSectionDesc section{};
					section.strLabel = sectionEntry.value("label", std::string{});
					section.bExpanded = sectionEntry.value("expanded", false);
					if (!section.strLabel.empty())
					{
						outDesc.vecSections.push_back(std::move(section));
					}
				}
			}

			return true;
		}

		AshEngine::UIPinId FindMappedPinId(
			const std::unordered_map<std::string, AshEngine::UIPinId>& mapPinIds,
			const std::string& strTemplatePinId)
		{
			if (strTemplatePinId.empty())
			{
				return 0;
			}

			const std::unordered_map<std::string, AshEngine::UIPinId>::const_iterator it = mapPinIds.find(strTemplatePinId);
			return it != mapPinIds.end() ? it->second : 0;
		}

		void AddPinMapping(
			NodeGraphNodeCreateResult& refResult,
			const std::string& strTemplatePinId,
			AshEngine::UIPinId runtimePinId,
			AshEngine::UINodePinKind eKind)
		{
			NodeGraphNodePinMapping mapping{};
			mapping.strTemplatePinId = strTemplatePinId;
			mapping.runtimePinId = runtimePinId;
			mapping.eKind = eKind;
			refResult.vecPinMappings.push_back(std::move(mapping));
		}

		bool ValidateNodeTypeDesc(const NodeGraphNodeTypeDesc& refDesc)
		{
			if (refDesc.strId.empty())
			{
				return false;
			}

			std::unordered_set<std::string> setInputPinIds{};
			std::unordered_set<std::string> setOutputPinIds{};
			std::unordered_set<std::string> setAllPinIds{};
			for (const NodeGraphNodeTypePinDesc& refPinDesc : refDesc.vecInputs)
			{
				if (refPinDesc.strId.empty() || !setAllPinIds.insert(refPinDesc.strId).second)
				{
					return false;
				}
				setInputPinIds.insert(refPinDesc.strId);
			}
			for (const NodeGraphNodeTypePinDesc& refPinDesc : refDesc.vecOutputs)
			{
				if (refPinDesc.strId.empty() || !setAllPinIds.insert(refPinDesc.strId).second)
				{
					return false;
				}
				setOutputPinIds.insert(refPinDesc.strId);
			}
			for (const NodeGraphNodeTypeRowDesc& refRowDesc : refDesc.vecRows)
			{
				if (!refRowDesc.strInputPinId.empty() && setInputPinIds.find(refRowDesc.strInputPinId) == setInputPinIds.end())
				{
					return false;
				}
				if (!refRowDesc.strOutputPinId.empty() && setOutputPinIds.find(refRowDesc.strOutputPinId) == setOutputPinIds.end())
				{
					return false;
				}
			}
			return true;
		}
	}

	AshEngine::UIPinId NodeGraphNodeCreateResult::FindPinId(const std::string& strTemplatePinId) const
	{
		for (const NodeGraphNodePinMapping& refMapping : vecPinMappings)
		{
			if (refMapping.strTemplatePinId == strTemplatePinId)
			{
				return refMapping.runtimePinId;
			}
		}
		return 0;
	}

	bool NodeGraphNodeTypeRegistry::LoadFromFile(const std::filesystem::path& pathFile, std::string& strOutError)
	{
		strOutError.clear();
		std::ifstream input(pathFile);
		if (!input.is_open())
		{
			strOutError = "Failed to open node type JSON file: " + pathFile.generic_string();
			return false;
		}

		json root{};
		try
		{
			input >> root;
		}
		catch (const json::exception& refException)
		{
			strOutError = std::string{ "Invalid node type JSON: " } + refException.what();
			return false;
		}

		if (!root.is_object() || !root.contains("nodeTypes") || !root["nodeTypes"].is_array())
		{
			strOutError = "Node type JSON root must contain a nodeTypes array.";
			return false;
		}

		NodeGraphNodeTypeRegistry loadedRegistry{};
		for (const json& entry : root["nodeTypes"])
		{
			NodeGraphNodeTypeDesc desc{};
			try
			{
				if (ParseNodeType(entry, desc))
				{
					loadedRegistry.RegisterType(desc);
				}
			}
			catch (const json::exception&)
			{
				continue;
			}
		}

		if (loadedRegistry._vecTypes.empty())
		{
			strOutError = "Node type JSON did not contain any valid nodeTypes entries.";
			return false;
		}

		_vecTypes = std::move(loadedRegistry._vecTypes);
		_vecTypeIds = std::move(loadedRegistry._vecTypeIds);
		return true;
	}

	void NodeGraphNodeTypeRegistry::Clear()
	{
		_vecTypes.clear();
		_vecTypeIds.clear();
	}

	bool NodeGraphNodeTypeRegistry::RegisterType(const NodeGraphNodeTypeDesc& refDesc)
	{
		if (!ValidateNodeTypeDesc(refDesc) || FindType(refDesc.strId))
		{
			return false;
		}

		_vecTypes.push_back(refDesc);
		_vecTypeIds.push_back(refDesc.strId);
		return true;
	}

	const NodeGraphNodeTypeDesc* NodeGraphNodeTypeRegistry::FindType(const std::string& strTypeId) const
	{
		for (const NodeGraphNodeTypeDesc& refDesc : _vecTypes)
		{
			if (refDesc.strId == strTypeId)
			{
				return &refDesc;
			}
		}
		return nullptr;
	}

	const std::vector<std::string>& NodeGraphNodeTypeRegistry::GetTypeIds() const
	{
		return _vecTypeIds;
	}

	bool NodeGraphNodeTypeRegistry::CreateNode(
		const NodeGraphNodeCreateDesc& refDesc,
		NodeGraphNodeCreateResult& outResult) const
	{
		outResult = {};
		outResult.nextPinId = refDesc.firstPinId;

		const NodeGraphNodeTypeDesc* pDesc = FindType(refDesc.strTypeId);
		if (!pDesc || refDesc.nodeId == 0 || refDesc.firstPinId == 0)
		{
			return false;
		}

		std::unordered_map<std::string, AshEngine::UIPinId> mapPinIds{};
		AshEngine::UIPinId nextPinId = refDesc.firstPinId;
		outResult.node.id = refDesc.nodeId;
		outResult.node.position = refDesc.position;
		outResult.node.title = pDesc->strTitle;
		outResult.node.subtitle = pDesc->strSubtitle;
		outResult.node.category = pDesc->strCategory;
		outResult.node.typeLabel = pDesc->strTypeLabel;
		outResult.node.bCollapsible = pDesc->bCollapsible;
		outResult.node.bCollapsed = pDesc->bDefaultCollapsed;
		outResult.node.accentColor = pDesc->accentColor;
		outResult.node.typeId = pDesc->strId;

		for (const NodeGraphNodeTypePinDesc& refPinDesc : pDesc->vecInputs)
		{
			AshEngine::UINodeGraphPin pin{};
			pin.id = nextPinId++;
			pin.kind = AshEngine::UINodePinKind::Input;
			pin.label = refPinDesc.strLabel;
			pin.typeLabel = refPinDesc.strTypeLabel;
			pin.valueKind = refPinDesc.eValueKind;
			pin.shape = refPinDesc.eShape;
			pin.color = refPinDesc.color;
			pin.templateId = refPinDesc.strId;
			mapPinIds[refPinDesc.strId] = pin.id;
			AddPinMapping(outResult, refPinDesc.strId, pin.id, pin.kind);
			outResult.node.inputPins.push_back(std::move(pin));
		}

		for (const NodeGraphNodeTypePinDesc& refPinDesc : pDesc->vecOutputs)
		{
			AshEngine::UINodeGraphPin pin{};
			pin.id = nextPinId++;
			pin.kind = AshEngine::UINodePinKind::Output;
			pin.label = refPinDesc.strLabel;
			pin.typeLabel = refPinDesc.strTypeLabel;
			pin.valueKind = refPinDesc.eValueKind;
			pin.shape = refPinDesc.eShape;
			pin.color = refPinDesc.color;
			pin.templateId = refPinDesc.strId;
			mapPinIds[refPinDesc.strId] = pin.id;
			AddPinMapping(outResult, refPinDesc.strId, pin.id, pin.kind);
			outResult.node.outputPins.push_back(std::move(pin));
		}

		for (const NodeGraphNodeTypeRowDesc& refRowDesc : pDesc->vecRows)
		{
			AshEngine::UINodeGraphBodyRow row{};
			row.label = refRowDesc.strLabel;
			row.inputPin = FindMappedPinId(mapPinIds, refRowDesc.strInputPinId);
			row.outputPin = FindMappedPinId(mapPinIds, refRowDesc.strOutputPinId);
			row.defaultValueText = refRowDesc.strDefaultValueText;
			row.defaultValueColor = refRowDesc.defaultValueColor;
			row.bHighlighted = refRowDesc.bHighlighted;
			row.bShowDefaultValue = refRowDesc.bShowDefaultValue;
			row.bEditableDefaultValue = refRowDesc.bEditableDefaultValue;
			row.valueKey = refRowDesc.strValueKey;
			row.valueKind = refRowDesc.eValueKind;
			row.editorKind = refRowDesc.eEditorKind;
			outResult.node.bodyRows.push_back(std::move(row));
		}

		for (const NodeGraphNodeTypeSectionDesc& refSectionDesc : pDesc->vecSections)
		{
			AshEngine::UINodeGraphSection section{};
			section.label = refSectionDesc.strLabel;
			section.bExpanded = refSectionDesc.bExpanded;
			outResult.node.sections.push_back(std::move(section));
		}

		outResult.nextPinId = nextPinId;
		return true;
	}
}
