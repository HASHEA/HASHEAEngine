#pragma once

#include "Function/Render/RenderDevice.h"
#include "Function/Render/ScenePresentationHandles.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace AshEngine
{
	struct EnvironmentMapRuntimeResource;

	// editor begin 修改原因：P2 GPU ID buffer picking
	struct ASH_API ScenePickFrameState
	{
		bool request_active = false;
		int32_t request_x = 0;
		int32_t request_y = 0;
		bool result_ready = false;
		ScenePickResult result{};
	};
	// editor end

	struct ASH_API SceneRenderViewContext
	{
		uint64_t view_id = 0;
		const char* debug_name = nullptr;
		std::shared_ptr<RenderTarget> output_target = nullptr;
		std::shared_ptr<EnvironmentMapRuntimeResource> environment_resource = nullptr;
		// editor begin 修改原因：绑定 viewport-scoped scene overlay 到当前 view 的深度结果
		std::shared_ptr<const std::vector<SceneOverlayLine>> scene_overlay_lines = nullptr;
		ScenePickFrameState* pick_state = nullptr;
		Scene* scene = nullptr;
		// editor end
		bool has_viewport = false;
		RenderViewport viewport{};
		bool has_scissor = false;
		RenderScissor scissor{};
		RenderLoadAction color_load_action = RenderLoadAction::Clear;
		RenderColorValue color_clear_value{ 0.025f, 0.03f, 0.05f, 1.0f };
		RenderDepthStencilValue depth_clear_value{ 1.0f, 0u };
		bool reverse_z = false;
	};
}
