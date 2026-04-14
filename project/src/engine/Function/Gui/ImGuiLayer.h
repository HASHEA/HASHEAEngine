#pragma once

#include "Function/Gui/UICommon.h"
#include "Graphics/RHIBackend.h"
#include <memory>
#include <vector>

namespace RHI
{
	class GraphicsContext;
}

namespace AshEngine
{
	class RenderDevice;
	class RenderTarget;
	class Window;
	struct WindowEvent;

	// Internal engine-only bridge to the active ImGui backend.
	// Higher layers should only depend on UIContext.
	class ImGuiLayer
	{
	public:
		virtual ~ImGuiLayer() = default;

	public:
		virtual bool init(Window* window, RHI::GraphicsContext* graphics_context, RenderDevice* render_device, const UIContextConfig& config = {}) = 0;
		virtual void shutdown() = 0;

		virtual bool is_initialized() const = 0;
		virtual bool begin_frame() = 0;
		virtual bool render(const std::vector<std::shared_ptr<RenderTarget>>& sampled_render_targets) = 0;
		virtual bool is_frame_active() const = 0;

		virtual void handle_window_event(const WindowEvent& event) = 0;

		virtual UITextureHandle register_render_target(const std::shared_ptr<RenderTarget>& render_target) = 0;
		virtual void unregister_render_target(const std::shared_ptr<RenderTarget>& render_target) = 0;
		virtual UITextureHandle get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target) = 0;

		virtual bool wants_capture_mouse() const = 0;
		virtual bool wants_capture_keyboard() const = 0;
		virtual bool wants_text_input() const = 0;
	};

	auto create_imgui_layer(RHI::Backend backend) -> std::unique_ptr<ImGuiLayer>;
}
