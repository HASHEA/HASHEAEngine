#pragma once

#include "Function/Gui/UICommon.h"
#include "Graphics/RHIBackend.h"
#include <memory>
#include <string_view>
#include <vector>

namespace RHI
{
	class GraphicsContext;
	class TextureView;
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
		virtual UITextureHandle register_texture_view(const std::shared_ptr<RHI::TextureView>& texture_view) = 0;
		virtual void unregister_texture_view(const std::shared_ptr<RHI::TextureView>& texture_view) = 0;
		virtual UITextureHandle get_texture_view_texture_id(const std::shared_ptr<RHI::TextureView>& texture_view) = 0;

		virtual bool wants_capture_mouse() const = 0;
		virtual bool wants_capture_keyboard() const = 0;
		virtual bool wants_text_input() const = 0;
		// editor begin 修改原因：暴露字体栈操作给 UIContext，供编辑器实现标题、强调信息等分层排版。
		virtual void push_font(UIFontRole role) = 0;
		virtual void pop_font() = 0;
		// editor end
		virtual void apply_theme_preset(UIThemePreset preset) = 0;
		virtual UIThemePreset get_theme_preset() const = 0;
		virtual bool apply_theme_definition(std::string_view svThemeId, std::string_view svThemeDefinition) = 0;
		virtual std::string get_theme_id() const = 0;
	};

	auto create_imgui_layer(RHI::Backend backend) -> std::unique_ptr<ImGuiLayer>;
}
