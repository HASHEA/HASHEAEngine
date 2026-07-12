#pragma once

#include "Base/hcore.h"
#include "Function/Gui/UICommon.h"
#include "Function/Gui/UINodeEditor.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	// editor begin 修改原因：为通用节点画布提供可测试的纯数据图模型，避免 Editor 面板持有散落的节点/连线规则。
	enum class UINodeGraphValueKind : uint8_t
	{
		None = 0,
		Scalar,
		Vector,
		Color,
		Boolean,
		Texture,
		Shader,
		String
	};

	enum class UINodeGraphInlineEditorKind : uint8_t
	{
		None = 0,
		Float,
		Color,
		Toggle,
		Text
	};

	struct UINodeGraphPin
	{
		UIPinId id = 0;
		UINodePinKind kind = UINodePinKind::Input;
		std::string label{};
		std::string typeLabel{};
		UINodeGraphValueKind valueKind = UINodeGraphValueKind::None;
		UINodePinShape shape = UINodePinShape::Circle;
		UIColor color{ 0.0f, 0.0f, 0.0f, 0.0f };
		std::string templateId{};
	};

	struct UINodeGraphLink
	{
		UILinkId id = 0;
		UIPinId startPin = 0;
		UIPinId endPin = 0;
	};

	struct UINodeGraphBodyRow
	{
		std::string label{};
		UIPinId inputPin = 0;
		UIPinId outputPin = 0;
		std::string defaultValueText{};
		UIColor defaultValueColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		bool bHighlighted = false;
		bool bShowDefaultValue = true;
		bool bEditableDefaultValue = false;
		std::string valueKey{};
		UINodeGraphValueKind valueKind = UINodeGraphValueKind::None;
		UINodeGraphInlineEditorKind editorKind = UINodeGraphInlineEditorKind::None;
	};

	struct UINodeGraphSection
	{
		std::string label{};
		bool bExpanded = false;
	};

	struct UINodeGraphNode
	{
		UINodeId id = 0;
		std::string title{};
		std::string subtitle{};
		std::string category{};
		std::string typeLabel{};
		std::string typeId{};
		bool bCollapsible = true;
		bool bCollapsed = false;
		UIColor accentColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		std::vector<std::string> bodyLines{};
		std::vector<UINodeGraphBodyRow> bodyRows{};
		std::vector<UINodeGraphSection> sections{};
		std::vector<UINodeGraphPin> inputPins{};
		std::vector<UINodeGraphPin> outputPins{};
		UIVec2 position{};
	};

	struct UINodeGraphLinkPins
	{
		UIPinId outputPin = 0;
		UIPinId inputPin = 0;
	};

	class UINodeGraphModel
	{
	public:
		const UINodeGraphPin* FindPin(UIPinId id) const
		{
			for (const UINodeGraphNode& refNode : nodes)
			{
				for (const UINodeGraphPin& refPin : refNode.inputPins)
				{
					if (refPin.id == id)
					{
						return &refPin;
					}
				}
				for (const UINodeGraphPin& refPin : refNode.outputPins)
				{
					if (refPin.id == id)
					{
						return &refPin;
					}
				}
			}
			return nullptr;
		}

		UINodeGraphPin* FindPin(UIPinId id)
		{
			return const_cast<UINodeGraphPin*>(static_cast<const UINodeGraphModel&>(*this).FindPin(id));
		}

		const UINodeGraphNode* FindNode(UINodeId id) const
		{
			const std::vector<UINodeGraphNode>::const_iterator it =
				std::find_if(
					nodes.begin(),
					nodes.end(),
					[id](const UINodeGraphNode& refNode) { return refNode.id == id; });
			return it != nodes.end() ? &(*it) : nullptr;
		}

		UINodeGraphNode* FindNode(UINodeId id)
		{
			return const_cast<UINodeGraphNode*>(static_cast<const UINodeGraphModel&>(*this).FindNode(id));
		}

		bool TryBuildLink(UIPinId startPin, UIPinId endPin, UINodeGraphLinkPins& outPins) const
		{
			outPins = {};
			if (startPin == 0 || endPin == 0 || startPin == endPin)
			{
				return false;
			}

			const UINodeGraphPin* pStartPin = FindPin(startPin);
			const UINodeGraphPin* pEndPin = FindPin(endPin);
			if (!pStartPin || !pEndPin || pStartPin->kind == pEndPin->kind)
			{
				return false;
			}

			outPins.outputPin = pStartPin->kind == UINodePinKind::Output ? startPin : endPin;
			outPins.inputPin = pStartPin->kind == UINodePinKind::Output ? endPin : startPin;
			return true;
		}

		bool ConnectPins(UILinkId linkId, UIPinId startPin, UIPinId endPin)
		{
			UINodeGraphLinkPins pins{};
			if (linkId == 0 || !TryBuildLink(startPin, endPin, pins))
			{
				return false;
			}

			const bool bDuplicateLink =
				std::find_if(
					links.begin(),
					links.end(),
					[&pins](const UINodeGraphLink& refLink)
					{
						return refLink.startPin == pins.outputPin && refLink.endPin == pins.inputPin;
					}) != links.end();
			if (bDuplicateLink)
			{
				return false;
			}

			links.erase(
				std::remove_if(
					links.begin(),
					links.end(),
					[&pins](const UINodeGraphLink& refLink) { return refLink.endPin == pins.inputPin; }),
				links.end());

			UINodeGraphLink link{};
			link.id = linkId;
			link.startPin = pins.outputPin;
			link.endPin = pins.inputPin;
			links.push_back(link);
			return true;
		}

		bool RemoveLink(UILinkId id)
		{
			const size_t oldSize = links.size();
			links.erase(
				std::remove_if(
					links.begin(),
					links.end(),
					[id](const UINodeGraphLink& refLink) { return refLink.id == id; }),
				links.end());
			return links.size() != oldSize;
		}

		bool RemoveLinksConnectedToNode(UINodeId id)
		{
			const UINodeGraphNode* pNode = FindNode(id);
			if (!pNode)
			{
				return false;
			}

			const size_t oldSize = links.size();
			links.erase(
				std::remove_if(
					links.begin(),
					links.end(),
					[pNode](const UINodeGraphLink& refLink)
					{
						for (const UINodeGraphPin& refPin : pNode->inputPins)
						{
							if (refLink.startPin == refPin.id || refLink.endPin == refPin.id)
							{
								return true;
							}
						}
						for (const UINodeGraphPin& refPin : pNode->outputPins)
						{
							if (refLink.startPin == refPin.id || refLink.endPin == refPin.id)
							{
								return true;
							}
						}
						return false;
					}),
				links.end());
			return links.size() != oldSize;
		}

		bool RemoveNode(UINodeId id)
		{
			const size_t oldSize = nodes.size();
			nodes.erase(
				std::remove_if(
					nodes.begin(),
					nodes.end(),
					[id](const UINodeGraphNode& refNode) { return refNode.id == id; }),
				nodes.end());
			if (nodes.size() == oldSize)
			{
				return false;
			}

			RemoveDanglingLinks();
			return true;
		}

		void RemoveDanglingLinks()
		{
			links.erase(
				std::remove_if(
					links.begin(),
					links.end(),
					[this](const UINodeGraphLink& refLink)
					{
						return !FindPin(refLink.startPin) || !FindPin(refLink.endPin);
					}),
				links.end());
		}

	public:
		std::vector<UINodeGraphNode> nodes{};
		std::vector<UINodeGraphLink> links{};
	};
	// editor end
}
