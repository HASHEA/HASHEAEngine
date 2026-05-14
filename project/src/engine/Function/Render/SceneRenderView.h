#pragma once

#include "Function/Render/RenderDevice.h"
#include <memory>

namespace AshEngine
{
	struct ASH_API SceneRenderViewContext
	{
		const char* debug_name = nullptr;
		std::shared_ptr<RenderTarget> output_target = nullptr;
		bool has_viewport = false;
		RenderViewport viewport{};
		bool has_scissor = false;
		RenderScissor scissor{};
		RenderLoadAction color_load_action = RenderLoadAction::Clear;
		RenderColorValue color_clear_value{ 0.025f, 0.03f, 0.05f, 1.0f };
		RenderDepthStencilValue depth_clear_value{ 1.0f, 0u };
	};
}
