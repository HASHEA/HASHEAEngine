#pragma once

#include "Function/Gui/UICommon.h"
#include "Function/Scene/Scene.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace AshEngine
{
	struct SceneOutputHandle
	{
		uint32_t value = 0;

		bool is_valid() const
		{
			return value != 0;
		}
	};

	struct SceneViewBindingHandle
	{
		uint32_t value = 0;

		bool is_valid() const
		{
			return value != 0;
		}
	};

	// editor begin 修改原因：Scene Overlay per-viewport / depth 语义
	enum class SceneOverlayDepthMode : uint8_t
	{
		AlwaysOnTop = 0,
		DepthTest,
		DepthTestNoWrite,
		XRay
	};

	struct ASH_API SceneOverlayLine
	{
		glm::vec3 start{ 0.0f };
		glm::vec3 end{ 0.0f };
		glm::vec4 color{ 1.0f };
		float thickness = 1.0f;
		float depth_bias = 0.0f;
		SceneOverlayDepthMode depth_mode = SceneOverlayDepthMode::DepthTestNoWrite;
	};

	struct ASH_API SceneOverlayBatchDesc
	{
		const SceneOverlayLine* lines = nullptr;
		uint32_t line_count = 0;
	};

	ASH_API bool submit_scene_overlay(
		SceneViewBindingHandle binding,
		const SceneOverlayBatchDesc& desc);

	ASH_API bool clear_scene_overlay(SceneViewBindingHandle binding);
	// editor end

	// editor begin 修改原因：P2 GPU ID buffer picking facade
	struct ASH_API ScenePickResult
	{
		EntityId entity_id = 0;
		float depth = 0.0f;
		glm::vec3 world_position{ 0.0f };
		glm::vec3 world_normal{ 0.0f, 1.0f, 0.0f };
		bool hit = false;
	};

	ASH_API bool request_scene_entity_pick(SceneViewBindingHandle binding, int32_t x, int32_t y);
	ASH_API bool poll_scene_entity_pick_result(SceneViewBindingHandle binding, ScenePickResult& out_result);
	// editor end

	// editor begin 修改原因：P3 viewport stats facade
	struct ASH_API SceneViewStats
	{
		uint32_t output_width = 0;
		uint32_t output_height = 0;
		uint32_t allocated_output_width = 0;
		uint32_t allocated_output_height = 0;
		const char* rhi_backend_name = nullptr;
		uint32_t draw_call_count = 0;
		uint32_t graphics_pass_count = 0;
		uint32_t compute_dispatch_count = 0;
		double cpu_frame_time_ms = 0.0;
		double instantaneous_fps = 0.0;
		double average_fps = 0.0;
		bool output_allocated = false;
		bool valid = false;
	};

	ASH_API bool get_scene_view_stats(SceneViewBindingHandle binding, SceneViewStats& out_stats);
	// editor end
}
