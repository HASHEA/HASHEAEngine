#include "Widgets/NodeGraph/NodeGraphCanvasStyle.h"

#include "Function/Gui/UIContext.h"

namespace AshEditor
{
	bool NodeGraphCanvasStyle::IsColorUnset(const AshEngine::UIColor& refColor)
	{
		return refColor.r == 0.0f && refColor.g == 0.0f && refColor.b == 0.0f && refColor.a == 0.0f;
	}

	AshEngine::UIColor NodeGraphCanvasStyle::ResolveAccentColor(const AshEngine::UINodeGraphNode& refNode)
	{
		return IsColorUnset(refNode.accentColor) ? kDefaultAccentColor : refNode.accentColor;
	}

	AshEngine::UIColor NodeGraphCanvasStyle::ResolvePinColor(const AshEngine::UINodeGraphPin& refPin)
	{
		if (!IsColorUnset(refPin.color))
		{
			return refPin.color;
		}

		switch (refPin.valueKind)
		{
		case AshEngine::UINodeGraphValueKind::Scalar:
			return AshEngine::UIColor{ 0.76f, 0.76f, 0.76f, 1.0f };
		case AshEngine::UINodeGraphValueKind::Vector:
			return AshEngine::UIColor{ 0.45f, 0.45f, 0.95f, 1.0f };
		case AshEngine::UINodeGraphValueKind::Color:
			return AshEngine::UIColor{ 0.92f, 0.86f, 0.18f, 1.0f };
		case AshEngine::UINodeGraphValueKind::Boolean:
			return AshEngine::UIColor{ 0.86f, 0.45f, 0.45f, 1.0f };
		case AshEngine::UINodeGraphValueKind::Texture:
			return AshEngine::UIColor{ 0.95f, 0.56f, 0.22f, 1.0f };
		case AshEngine::UINodeGraphValueKind::Shader:
			return AshEngine::UIColor{ 0.36f, 0.82f, 0.42f, 1.0f };
		case AshEngine::UINodeGraphValueKind::String:
			return AshEngine::UIColor{ 0.78f, 0.58f, 0.92f, 1.0f };
		case AshEngine::UINodeGraphValueKind::None:
		default:
			break;
		}

		return refPin.kind == AshEngine::UINodePinKind::Output ? kOutputPinColor : kInputPinColor;
	}

	void NodeGraphCanvasStyle::PushTransparentControlStyle(AshEngine::UIContext& refUi)
	{
		refUi.push_style_color(AshEngine::UIStyleColorKind::FrameBg, kTransparentColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::FrameBgHovered, kTransparentColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::FrameBgActive, kTransparentColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::Border, kTransparentColor);
		refUi.push_style_var(AshEngine::UIStyleVarKind::FramePadding, AshEngine::UIVec2{ 5.0f, 0.0f });
		refUi.push_style_var(AshEngine::UIStyleVarKind::FrameRounding, 3.0f);
		refUi.push_style_var(AshEngine::UIStyleVarKind::FrameBorderSize, 0.0f);
	}

	void NodeGraphCanvasStyle::PopTransparentControlStyle(AshEngine::UIContext& refUi)
	{
		refUi.pop_style_var(3);
		refUi.pop_style_color(4);
	}

	void NodeGraphCanvasStyle::PushTransparentButtonStyle(AshEngine::UIContext& refUi)
	{
		refUi.push_style_color(AshEngine::UIStyleColorKind::Button, kTransparentColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, kTransparentColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, kTransparentColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::Border, kTransparentColor);
		refUi.push_style_var(AshEngine::UIStyleVarKind::FramePadding, AshEngine::UIVec2{ 0.0f, 0.0f });
		refUi.push_style_var(AshEngine::UIStyleVarKind::FrameBorderSize, 0.0f);
	}

	void NodeGraphCanvasStyle::PopTransparentButtonStyle(AshEngine::UIContext& refUi)
	{
		refUi.pop_style_var(2);
		refUi.pop_style_color(4);
	}
}
