#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include "Function/Gui/UICommon.h"
#include <memory>
#include <string>
#include <vector>

namespace RHI
{
	class GraphicsContext;
}

namespace AshEngine
{
	class ImGuiLayer;
	class RenderDevice;
	class RenderTarget;
	class Window;
	struct WindowEvent;

	// Engine-facing developer UI facade.
	// Reusable by editor, game, and client tooling. Editor-specific workspace abstractions should sit above this layer.
	class ASH_API UIContext
	{
	public:
		UIContext();
		~UIContext();

		UIContext(const UIContext&) = delete;
		UIContext& operator=(const UIContext&) = delete;

	public:
		// Engine-owned lifecycle hooks. Editor side should use the drawing APIs below.
		bool init(Window* window, RHI::GraphicsContext* graphics_context, RenderDevice* render_device, const UIContextConfig& config = {});
		void shutdown();
		bool begin_frame();
		bool render();
		void handle_window_event(const WindowEvent& event);

	public:
		bool is_initialized() const;
		bool is_frame_active() const;

		bool wants_capture_mouse() const;
		bool wants_capture_keyboard() const;
		bool wants_text_input() const;

	public:
		void show_demo_window(bool* open = nullptr);

		bool begin_window(const char* name, bool* open = nullptr, UIWindowFlags flags = UIWindowFlagBits::None);
		bool begin_dockspace_host_window(const char* name, bool* open = nullptr, UIWindowFlags flags = UIWindowFlagBits::None);
		void end_window();
		bool begin_child(const char* str_id, const UIVec2& size = {}, UIChildFlags child_flags = UIChildFlagBits::None, UIWindowFlags window_flags = UIWindowFlagBits::None);
		void end_child();

		UIDockNodeId dock_space(const char* str_id, const UIVec2& size = {}, UIDockNodeFlags flags = UIDockNodeFlagBits::None);
		UIDockNodeId dock_space(UIDockNodeId dockspace_id, const UIVec2& size = {}, UIDockNodeFlags flags = UIDockNodeFlagBits::None);
		void dock_builder_remove_node(UIDockNodeId node_id);
		void dock_builder_add_node(UIDockNodeId node_id, UIDockNodeFlags flags = UIDockNodeFlagBits::None);
		void dock_builder_set_node_size(UIDockNodeId node_id, const UIVec2& size);
		UIDockNodeId dock_builder_split_node(UIDockNodeId node_id, UIDirection direction, float size_ratio_for_node_at_dir, UIDockNodeId* out_id_at_dir = nullptr, UIDockNodeId* out_id_at_opposite_dir = nullptr);
		void dock_builder_dock_window(const char* window_name, UIDockNodeId node_id);
		void dock_builder_finish(UIDockNodeId node_id);

		void set_next_window_position(const UIVec2& position, UIConditionFlags cond = UIConditionFlagBits::None, const UIVec2& pivot = {});
		void set_next_window_size(const UIVec2& size, UIConditionFlags cond = UIConditionFlagBits::None);
		void set_next_window_viewport(UIViewportId viewport_id);
		void set_next_window_collapsed(bool collapsed, UIConditionFlags cond = UIConditionFlagBits::None);
		void set_next_item_width(float width);

		void same_line(float offset_from_start_x = 0.0f, float spacing = -1.0f);
		void separator();
		void spacing();
		void dummy(const UIVec2& size);
		void begin_group();
		void end_group();
		void begin_disabled(bool disabled = true);
		void end_disabled();

		void push_id(const char* str_id);
		void push_id(int32_t int_id);
		void pop_id();

		void push_style_color(UIStyleColorKind kind, const UIColor& color);
		void pop_style_color(int count = 1);
		void push_style_var(UIStyleVarKind kind, float value);
		void push_style_var(UIStyleVarKind kind, const UIVec2& value);
		void pop_style_var(int count = 1);

		void text_unformatted(const char* text);
		void text(const char* format, ...);
		void text_wrapped(const char* format, ...);
		void text_colored(const UIColor& color, const char* format, ...);
		void bullet_text(const char* format, ...);

		bool button(const char* label, const UIVec2& size = {});
		bool small_button(const char* label);
		bool checkbox(const char* label, bool& value);
		bool selectable(const char* label, bool selected = false, UISelectableFlags flags = UISelectableFlagBits::None, const UIVec2& size = {});
		bool collapsing_header(const char* label, UITreeNodeFlags flags = UITreeNodeFlagBits::None);
		bool tree_node(const char* label, UITreeNodeFlags flags = UITreeNodeFlagBits::None);
		bool tree_node(const void* stable_id, const char* label, UITreeNodeFlags flags = UITreeNodeFlagBits::None);
		void tree_pop();

		bool input_text(const char* label, std::string& value, UIInputTextFlags flags = UIInputTextFlagBits::None);
		bool input_text_multiline(const char* label, std::string& value, const UIVec2& size = {}, UIInputTextFlags flags = UIInputTextFlagBits::None);
		bool input_int(const char* label, int32_t& value, int32_t step = 1, int32_t step_fast = 100);
		bool input_float(const char* label, float& value, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f");
		bool input_float2(const char* label, float value[2], const char* format = "%.3f", UIInputTextFlags flags = UIInputTextFlagBits::None);
		bool input_float3(const char* label, float value[3], const char* format = "%.3f", UIInputTextFlags flags = UIInputTextFlagBits::None);
		bool input_float4(const char* label, float value[4], const char* format = "%.3f", UIInputTextFlags flags = UIInputTextFlagBits::None);
		bool drag_float(const char* label, float& value, float speed = 1.0f, float min_value = 0.0f, float max_value = 0.0f, const char* format = "%.3f");
		bool drag_float2(const char* label, float value[2], float speed = 1.0f, float min_value = 0.0f, float max_value = 0.0f, const char* format = "%.3f");
		bool drag_float3(const char* label, float value[3], float speed = 1.0f, float min_value = 0.0f, float max_value = 0.0f, const char* format = "%.3f");
		bool drag_float4(const char* label, float value[4], float speed = 1.0f, float min_value = 0.0f, float max_value = 0.0f, const char* format = "%.3f");
		bool slider_float(const char* label, float& value, float min_value, float max_value, const char* format = "%.3f");
		bool color_edit3(const char* label, float value[3]);
		bool color_edit3(const char* label, UIColor& value);
		bool color_edit4(const char* label, float value[4]);
		bool color_edit4(const char* label, UIColor& value);
		bool combo(const char* label, int32_t& current_index, const std::vector<const char*>& items, int32_t popup_max_height_in_items = -1);
		bool combo(const char* label, int32_t& current_index, const std::vector<std::string>& items, int32_t popup_max_height_in_items = -1);

		bool begin_main_menu_bar();
		void end_main_menu_bar();
		bool begin_menu_bar();
		void end_menu_bar();
		bool begin_menu(const char* label, bool enabled = true);
		void end_menu();
		bool menu_item(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
		bool menu_item(const char* label, const char* shortcut, bool* selected, bool enabled = true);

		bool begin_tab_bar(const char* str_id, UITabBarFlags flags = UITabBarFlagBits::None);
		void end_tab_bar();
		bool begin_tab_item(const char* label, bool* open = nullptr, UITabItemFlags flags = UITabItemFlagBits::None);
		void end_tab_item();

		bool begin_table(const char* str_id, int32_t column_count, UITableFlags flags = UITableFlagBits::None, const UIVec2& outer_size = {}, float inner_width = 0.0f);
		void end_table();
		void table_next_row();
		bool table_next_column();
		void table_setup_column(const char* label, float init_width_or_weight = 0.0f);
		void table_setup_column(const char* label, UITableColumnFlags flags, float init_width_or_weight = 0.0f);
		void table_headers_row();

		void open_popup(const char* str_id);
		bool begin_popup(const char* str_id, UIWindowFlags flags = UIWindowFlagBits::None);
		bool begin_popup_modal(const char* name, bool* open = nullptr, UIWindowFlags flags = UIWindowFlagBits::None);
		void end_popup();
		void close_current_popup();

		bool is_item_hovered() const;
		bool is_item_clicked(UIMouseButton button = UIMouseButton::Left) const;
		bool is_window_focused() const;
		bool is_window_hovered() const;

		UIVec2 get_content_region_avail() const;
		UIRect get_main_viewport_rect() const;
		UIViewportId get_main_viewport_id() const;
		UIVec2 get_cursor_pos() const;
		void set_cursor_pos(const UIVec2& position);
		float get_window_width() const;
		float get_window_height() const;

	public:
		UITextureHandle register_render_target(const std::shared_ptr<RenderTarget>& render_target);
		void unregister_render_target(const std::shared_ptr<RenderTarget>& render_target);
		UITextureHandle get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target);
		void image(const std::shared_ptr<RenderTarget>& render_target, const UIVec2& size, const UIVec2& uv0 = { 0.0f, 0.0f }, const UIVec2& uv1 = { 1.0f, 1.0f }, const UIColor& tint = { 1.0f, 1.0f, 1.0f, 1.0f }, const UIColor& border = { 0.0f, 0.0f, 0.0f, 0.0f });
		void image(UITextureHandle texture, const UIVec2& size, const UIVec2& uv0 = { 0.0f, 0.0f }, const UIVec2& uv1 = { 1.0f, 1.0f }, const UIColor& tint = { 1.0f, 1.0f, 1.0f, 1.0f }, const UIColor& border = { 0.0f, 0.0f, 0.0f, 0.0f });
		void draw_render_target(const std::shared_ptr<RenderTarget>& render_target, const UIVec2& size);
		void draw_render_target_fill_available(const std::shared_ptr<RenderTarget>& render_target, bool preserve_aspect = false, const UIColor& tint = { 1.0f, 1.0f, 1.0f, 1.0f }, const UIColor& border = { 0.0f, 0.0f, 0.0f, 0.0f });

	private:
		void track_render_target_usage(const std::shared_ptr<RenderTarget>& render_target);

	private:
		class Impl;
		Impl* m_impl = nullptr;
	};
}
