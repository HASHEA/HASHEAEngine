#pragma once

#include "Function/Gui/UICommon.h"
#include "Function/Gui/UINodeGraph.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct NodeGraphCanvasStyle final
	{
		static constexpr float kPinMarkerSize = 12.0f;
		static constexpr float kPinTextGap = 6.0f;
		static constexpr float kPinTypeTextScale = 0.78f;
		static constexpr float kNodePaddingX = 6.0f;
		static constexpr float kNodeVisualBleed = 8.0f;
		static constexpr float kNodeHeaderTopBleed = 8.0f;
		static constexpr float kNodeRounding = 12.0f;
		static constexpr float kNodeHeaderHeight = 22.0f;
		static constexpr float kNodeSectionGap = 1.0f;
		static constexpr float kNodeRowHeight = 20.0f;
		static constexpr float kNodeValueBadgeHeight = 16.0f;
		static constexpr float kNodeValueBadgeWidth = 52.0f;
		static constexpr float kNodeColorSwatchWidth = 24.0f;
		static constexpr float kNodeChevronSize = 4.5f;

		static constexpr AshEngine::UIColor kInputPinColor{ 0.44f, 0.74f, 1.0f, 1.0f };
		static constexpr AshEngine::UIColor kOutputPinColor{ 1.0f, 0.72f, 0.35f, 1.0f };
		static constexpr AshEngine::UIColor kNodeBodyColor{ 0.14f, 0.14f, 0.14f, 0.90f };
		static constexpr AshEngine::UIColor kNodeRowColor{ 0.18f, 0.18f, 0.18f, 0.72f };
		static constexpr AshEngine::UIColor kNodeRowAltColor{ 0.16f, 0.16f, 0.16f, 0.72f };
		static constexpr AshEngine::UIColor kNodeSectionColor{ 0.12f, 0.12f, 0.12f, 0.72f };
		static constexpr AshEngine::UIColor kNodeMutedTextColor{ 0.70f, 0.72f, 0.76f, 1.0f };
		static constexpr AshEngine::UIColor kNodeBodyTextColor{ 0.86f, 0.87f, 0.89f, 1.0f };
		static constexpr AshEngine::UIColor kNodeHeaderTextColor{ 0.92f, 0.94f, 0.94f, 1.0f };
		static constexpr AshEngine::UIColor kTransparentColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		static constexpr AshEngine::UIColor kDefaultAccentColor{ 0.38f, 0.62f, 0.95f, 1.0f };

		static bool IsColorUnset(const AshEngine::UIColor& refColor);
		static AshEngine::UIColor ResolveAccentColor(const AshEngine::UINodeGraphNode& refNode);
		static AshEngine::UIColor ResolvePinColor(const AshEngine::UINodeGraphPin& refPin);
		static void PushTransparentControlStyle(AshEngine::UIContext& refUi);
		static void PopTransparentControlStyle(AshEngine::UIContext& refUi);
		static void PushTransparentButtonStyle(AshEngine::UIContext& refUi);
		static void PopTransparentButtonStyle(AshEngine::UIContext& refUi);
	};
}
