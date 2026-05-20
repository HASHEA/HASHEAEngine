#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	class GraphicsProgram;
	class Renderer;
	class RenderSampler;
	class UIContext;
	struct SceneRenderViewContext;

	enum class RenderDebugVisualization : uint8_t
	{
		Color = 0,
		LinearHDR,
		Depth,
		Normal,
		MotionVector,
		AO,
		Scalar
	};

	struct RenderDebugViewConfig
	{
		bool enabled = false;
		std::string selected = "Off";
	};

	struct RenderDebugViewItem
	{
		std::string name{};
		std::string display_name{};
		RenderGraphTextureRef texture{};
		RenderDebugVisualization visualization = RenderDebugVisualization::Color;
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	class RenderDebugViewFrameRegistry
	{
	public:
		void begin_frame();
		void register_item(const RenderDebugViewItem& item);
		const RenderDebugViewItem* find_item(const std::string& name) const;
		const std::vector<RenderDebugViewItem>& get_items() const;

	private:
		std::vector<RenderDebugViewItem> m_items{};
	};

	ASH_API const char* render_debug_visualization_name(RenderDebugVisualization visualization);
	ASH_API const char* render_debug_texture_format_name(RenderTextureFormat format);
	ASH_API RenderDebugViewConfig make_default_render_debug_view_config();
	ASH_API RenderDebugViewConfig load_runtime_render_debug_view_config(const char* config_path);
	ASH_API void set_runtime_render_debug_view_config(const RenderDebugViewConfig& config);
	ASH_API RenderDebugViewConfig get_runtime_render_debug_view_config();

	class RenderDebugView
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		void begin_frame();
		void register_item(const RenderDebugViewItem& item);
		bool add_pass(RenderGraphBuilder& graph, RenderGraphTextureRef output_target, const SceneRenderViewContext& view_context);
		void draw_ui(UIContext& ui_context);

		static bool should_bypass_debug_pass(const std::string& selected);
		static bool add_pass_for_tests(RenderGraphBuilder& graph, RenderGraphTextureRef selected, RenderGraphTextureRef output);

	private:
		bool create_resources(Renderer& renderer);
		bool add_pass_internal(
			RenderGraphBuilder& graph,
			const RenderDebugViewItem& item,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext* view_context);

	private:
		Renderer* m_renderer = nullptr;
		RenderDebugViewFrameRegistry m_registry{};
		std::unique_ptr<GraphicsProgram> m_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		bool m_last_selected_available = false;
		std::string m_last_selected{};
	};
}
