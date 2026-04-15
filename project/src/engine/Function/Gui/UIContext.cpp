#include "UIContext.h"
#include "ImGuiLayer.h"
#include "Function/Application.h"
#include "Function/Render/RenderDevice.h"
#include "Graphics/Texture.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdarg>

namespace AshEngine
{
	namespace
	{
		static auto to_imvec2(const UIVec2& value) -> ImVec2
		{
			return ImVec2(value.x, value.y);
		}

		static auto to_imvec4(const UIColor& value) -> ImVec4
		{
			return ImVec4(value.r, value.g, value.b, value.a);
		}

		static auto fit_size_into_region(const UIVec2& region, float source_width, float source_height) -> UIVec2
		{
			if (region.x <= 0.0f || region.y <= 0.0f || source_width <= 0.0f || source_height <= 0.0f)
			{
				return {};
			}

			const float width_ratio = region.x / source_width;
			const float height_ratio = region.y / source_height;
			const float scale = std::min(width_ratio, height_ratio);
			return { source_width * scale, source_height * scale };
		}

		static auto to_imgui_cond(UIConditionFlags flags) -> ImGuiCond
		{
			if (flags & UIConditionFlagBits::Always)
			{
				return ImGuiCond_Always;
			}
			if (flags & UIConditionFlagBits::Once)
			{
				return ImGuiCond_Once;
			}
			if (flags & UIConditionFlagBits::FirstUseEver)
			{
				return ImGuiCond_FirstUseEver;
			}
			if (flags & UIConditionFlagBits::Appearing)
			{
				return ImGuiCond_Appearing;
			}
			return ImGuiCond_None;
		}

		static auto to_imgui_window_flags(UIWindowFlags flags) -> ImGuiWindowFlags
		{
			ImGuiWindowFlags result = ImGuiWindowFlags_None;
			if (flags & UIWindowFlagBits::NoTitleBar) result |= ImGuiWindowFlags_NoTitleBar;
			if (flags & UIWindowFlagBits::NoResize) result |= ImGuiWindowFlags_NoResize;
			if (flags & UIWindowFlagBits::NoMove) result |= ImGuiWindowFlags_NoMove;
			if (flags & UIWindowFlagBits::NoScrollbar) result |= ImGuiWindowFlags_NoScrollbar;
			if (flags & UIWindowFlagBits::NoScrollWithMouse) result |= ImGuiWindowFlags_NoScrollWithMouse;
			if (flags & UIWindowFlagBits::NoCollapse) result |= ImGuiWindowFlags_NoCollapse;
			if (flags & UIWindowFlagBits::NoSavedSettings) result |= ImGuiWindowFlags_NoSavedSettings;
			if (flags & UIWindowFlagBits::NoInputs) result |= ImGuiWindowFlags_NoInputs;
			if (flags & UIWindowFlagBits::MenuBar) result |= ImGuiWindowFlags_MenuBar;
			if (flags & UIWindowFlagBits::NoDocking) result |= ImGuiWindowFlags_NoDocking;
			if (flags & UIWindowFlagBits::AlwaysAutoResize) result |= ImGuiWindowFlags_AlwaysAutoResize;
			if (flags & UIWindowFlagBits::NoBringToFrontOnFocus) result |= ImGuiWindowFlags_NoBringToFrontOnFocus;
			if (flags & UIWindowFlagBits::NoNavFocus) result |= ImGuiWindowFlags_NoNavFocus;
			return result;
		}

		static auto to_imgui_dock_node_flags(UIDockNodeFlags flags) -> ImGuiDockNodeFlags
		{
			ImGuiDockNodeFlags result = ImGuiDockNodeFlags_None;
			if (flags & UIDockNodeFlagBits::KeepAliveOnly) result |= ImGuiDockNodeFlags_KeepAliveOnly;
			if (flags & UIDockNodeFlagBits::NoDockingOverCentralNode) result |= ImGuiDockNodeFlags_NoDockingOverCentralNode;
			if (flags & UIDockNodeFlagBits::PassthruCentralNode) result |= ImGuiDockNodeFlags_PassthruCentralNode;
			if (flags & UIDockNodeFlagBits::NoDockingSplit) result |= ImGuiDockNodeFlags_NoDockingSplit;
			if (flags & UIDockNodeFlagBits::NoResize) result |= ImGuiDockNodeFlags_NoResize;
			if (flags & UIDockNodeFlagBits::AutoHideTabBar) result |= ImGuiDockNodeFlags_AutoHideTabBar;
			if (flags & UIDockNodeFlagBits::DockSpace) result |= ImGuiDockNodeFlags_DockSpace;
			return result;
		}

		static auto to_imgui_child_flags(UIChildFlags flags) -> ImGuiChildFlags
		{
			ImGuiChildFlags result = ImGuiChildFlags_None;
			if (flags & UIChildFlagBits::Border) result |= ImGuiChildFlags_Border;
			if (flags & UIChildFlagBits::AlwaysUseWindowPadding) result |= ImGuiChildFlags_AlwaysUseWindowPadding;
			if (flags & UIChildFlagBits::ResizeX) result |= ImGuiChildFlags_ResizeX;
			if (flags & UIChildFlagBits::ResizeY) result |= ImGuiChildFlags_ResizeY;
			if (flags & UIChildFlagBits::AutoResizeX) result |= ImGuiChildFlags_AutoResizeX;
			if (flags & UIChildFlagBits::AutoResizeY) result |= ImGuiChildFlags_AutoResizeY;
			return result;
		}

		static auto to_imgui_selectable_flags(UISelectableFlags flags) -> ImGuiSelectableFlags
		{
			ImGuiSelectableFlags result = ImGuiSelectableFlags_None;
			if (flags & UISelectableFlagBits::DontClosePopups) result |= ImGuiSelectableFlags_DontClosePopups;
			if (flags & UISelectableFlagBits::SpanAllColumns) result |= ImGuiSelectableFlags_SpanAllColumns;
			if (flags & UISelectableFlagBits::AllowDoubleClick) result |= ImGuiSelectableFlags_AllowDoubleClick;
			return result;
		}

		static auto to_imgui_tree_node_flags(UITreeNodeFlags flags) -> ImGuiTreeNodeFlags
		{
			ImGuiTreeNodeFlags result = ImGuiTreeNodeFlags_None;
			if (flags & UITreeNodeFlagBits::Selected) result |= ImGuiTreeNodeFlags_Selected;
			if (flags & UITreeNodeFlagBits::Framed) result |= ImGuiTreeNodeFlags_Framed;
			if (flags & UITreeNodeFlagBits::DefaultOpen) result |= ImGuiTreeNodeFlags_DefaultOpen;
			if (flags & UITreeNodeFlagBits::OpenOnArrow) result |= ImGuiTreeNodeFlags_OpenOnArrow;
			if (flags & UITreeNodeFlagBits::SpanAvailWidth) result |= ImGuiTreeNodeFlags_SpanAvailWidth;
			if (flags & UITreeNodeFlagBits::Leaf) result |= ImGuiTreeNodeFlags_Leaf;
			return result;
		}

		static auto to_imgui_table_flags(UITableFlags flags) -> ImGuiTableFlags
		{
			ImGuiTableFlags result = ImGuiTableFlags_None;
			if (flags & UITableFlagBits::Resizable) result |= ImGuiTableFlags_Resizable;
			if (flags & UITableFlagBits::Reorderable) result |= ImGuiTableFlags_Reorderable;
			if (flags & UITableFlagBits::Hideable) result |= ImGuiTableFlags_Hideable;
			if (flags & UITableFlagBits::Sortable) result |= ImGuiTableFlags_Sortable;
			if (flags & UITableFlagBits::RowBg) result |= ImGuiTableFlags_RowBg;
			if (flags & UITableFlagBits::Borders) result |= ImGuiTableFlags_Borders;
			if (flags & UITableFlagBits::BordersOuter) result |= ImGuiTableFlags_BordersOuter;
			if (flags & UITableFlagBits::BordersInner) result |= ImGuiTableFlags_BordersInner;
			if (flags & UITableFlagBits::ScrollX) result |= ImGuiTableFlags_ScrollX;
			if (flags & UITableFlagBits::ScrollY) result |= ImGuiTableFlags_ScrollY;
			if (flags & UITableFlagBits::SizingStretchProp) result |= ImGuiTableFlags_SizingStretchProp;
			if (flags & UITableFlagBits::SizingFixedFit) result |= ImGuiTableFlags_SizingFixedFit;
			return result;
		}

		static auto to_imgui_table_column_flags(UITableColumnFlags flags) -> ImGuiTableColumnFlags
		{
			ImGuiTableColumnFlags result = ImGuiTableColumnFlags_None;
			if (flags & UITableColumnFlagBits::Disabled) result |= ImGuiTableColumnFlags_Disabled;
			if (flags & UITableColumnFlagBits::DefaultHide) result |= ImGuiTableColumnFlags_DefaultHide;
			if (flags & UITableColumnFlagBits::DefaultSort) result |= ImGuiTableColumnFlags_DefaultSort;
			if (flags & UITableColumnFlagBits::WidthStretch) result |= ImGuiTableColumnFlags_WidthStretch;
			if (flags & UITableColumnFlagBits::WidthFixed) result |= ImGuiTableColumnFlags_WidthFixed;
			if (flags & UITableColumnFlagBits::NoResize) result |= ImGuiTableColumnFlags_NoResize;
			if (flags & UITableColumnFlagBits::NoReorder) result |= ImGuiTableColumnFlags_NoReorder;
			if (flags & UITableColumnFlagBits::NoHide) result |= ImGuiTableColumnFlags_NoHide;
			if (flags & UITableColumnFlagBits::NoClip) result |= ImGuiTableColumnFlags_NoClip;
			if (flags & UITableColumnFlagBits::NoSort) result |= ImGuiTableColumnFlags_NoSort;
			if (flags & UITableColumnFlagBits::NoSortAscending) result |= ImGuiTableColumnFlags_NoSortAscending;
			if (flags & UITableColumnFlagBits::NoSortDescending) result |= ImGuiTableColumnFlags_NoSortDescending;
			if (flags & UITableColumnFlagBits::PreferSortAscending) result |= ImGuiTableColumnFlags_PreferSortAscending;
			if (flags & UITableColumnFlagBits::PreferSortDescending) result |= ImGuiTableColumnFlags_PreferSortDescending;
			if (flags & UITableColumnFlagBits::IndentEnable) result |= ImGuiTableColumnFlags_IndentEnable;
			if (flags & UITableColumnFlagBits::IndentDisable) result |= ImGuiTableColumnFlags_IndentDisable;
			return result;
		}

		static auto to_imgui_input_text_flags(UIInputTextFlags flags) -> ImGuiInputTextFlags
		{
			ImGuiInputTextFlags result = ImGuiInputTextFlags_None;
			if (flags & UIInputTextFlagBits::ReadOnly) result |= ImGuiInputTextFlags_ReadOnly;
			if (flags & UIInputTextFlagBits::Password) result |= ImGuiInputTextFlags_Password;
			if (flags & UIInputTextFlagBits::EnterReturnsTrue) result |= ImGuiInputTextFlags_EnterReturnsTrue;
			if (flags & UIInputTextFlagBits::AutoSelectAll) result |= ImGuiInputTextFlags_AutoSelectAll;
			if (flags & UIInputTextFlagBits::AllowTabInput) result |= ImGuiInputTextFlags_AllowTabInput;
			return result;
		}

		static auto to_imgui_tab_bar_flags(UITabBarFlags flags) -> ImGuiTabBarFlags
		{
			ImGuiTabBarFlags result = ImGuiTabBarFlags_None;
			if (flags & UITabBarFlagBits::Reorderable) result |= ImGuiTabBarFlags_Reorderable;
			if (flags & UITabBarFlagBits::AutoSelectNewTabs) result |= ImGuiTabBarFlags_AutoSelectNewTabs;
			if (flags & UITabBarFlagBits::FittingPolicyResizeDown) result |= ImGuiTabBarFlags_FittingPolicyResizeDown;
			if (flags & UITabBarFlagBits::FittingPolicyScroll) result |= ImGuiTabBarFlags_FittingPolicyScroll;
			return result;
		}

		static auto to_imgui_tab_item_flags(UITabItemFlags flags) -> ImGuiTabItemFlags
		{
			ImGuiTabItemFlags result = ImGuiTabItemFlags_None;
			if (flags & UITabItemFlagBits::UnsavedDocument) result |= ImGuiTabItemFlags_UnsavedDocument;
			if (flags & UITabItemFlagBits::SetSelected) result |= ImGuiTabItemFlags_SetSelected;
			if (flags & UITabItemFlagBits::NoCloseWithMiddleMouseButton) result |= ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;
			return result;
		}

		static auto to_imgui_dir(UIDirection direction) -> ImGuiDir
		{
			switch (direction)
			{
			case UIDirection::Left: return ImGuiDir_Left;
			case UIDirection::Right: return ImGuiDir_Right;
			case UIDirection::Up: return ImGuiDir_Up;
			case UIDirection::Down: return ImGuiDir_Down;
			default: return ImGuiDir_None;
			}
		}

		static auto to_imgui_style_color(UIStyleColorKind kind) -> ImGuiCol
		{
			switch (kind)
			{
			case UIStyleColorKind::Text: return ImGuiCol_Text;
			case UIStyleColorKind::WindowBg: return ImGuiCol_WindowBg;
			case UIStyleColorKind::ChildBg: return ImGuiCol_ChildBg;
			case UIStyleColorKind::PopupBg: return ImGuiCol_PopupBg;
			case UIStyleColorKind::Border: return ImGuiCol_Border;
			case UIStyleColorKind::FrameBg: return ImGuiCol_FrameBg;
			case UIStyleColorKind::FrameBgHovered: return ImGuiCol_FrameBgHovered;
			case UIStyleColorKind::FrameBgActive: return ImGuiCol_FrameBgActive;
			case UIStyleColorKind::TitleBg: return ImGuiCol_TitleBg;
			case UIStyleColorKind::TitleBgActive: return ImGuiCol_TitleBgActive;
			case UIStyleColorKind::MenuBarBg: return ImGuiCol_MenuBarBg;
			case UIStyleColorKind::Button: return ImGuiCol_Button;
			case UIStyleColorKind::ButtonHovered: return ImGuiCol_ButtonHovered;
			case UIStyleColorKind::ButtonActive: return ImGuiCol_ButtonActive;
			case UIStyleColorKind::Header: return ImGuiCol_Header;
			case UIStyleColorKind::HeaderHovered: return ImGuiCol_HeaderHovered;
			case UIStyleColorKind::HeaderActive: return ImGuiCol_HeaderActive;
			case UIStyleColorKind::Separator: return ImGuiCol_Separator;
			case UIStyleColorKind::SeparatorHovered: return ImGuiCol_SeparatorHovered;
			case UIStyleColorKind::SeparatorActive: return ImGuiCol_SeparatorActive;
			case UIStyleColorKind::Tab: return ImGuiCol_Tab;
			case UIStyleColorKind::TabHovered: return ImGuiCol_TabHovered;
			case UIStyleColorKind::TabSelected: return ImGuiCol_TabActive;
			case UIStyleColorKind::TableRowBg: return ImGuiCol_TableRowBg;
			case UIStyleColorKind::TableRowBgAlt: return ImGuiCol_TableRowBgAlt;
			default: return ImGuiCol_Text;
			}
		}

		static auto to_imgui_style_var(UIStyleVarKind kind) -> ImGuiStyleVar
		{
			switch (kind)
			{
			case UIStyleVarKind::Alpha: return ImGuiStyleVar_Alpha;
			case UIStyleVarKind::WindowPadding: return ImGuiStyleVar_WindowPadding;
			case UIStyleVarKind::WindowRounding: return ImGuiStyleVar_WindowRounding;
			case UIStyleVarKind::WindowBorderSize: return ImGuiStyleVar_WindowBorderSize;
			case UIStyleVarKind::FramePadding: return ImGuiStyleVar_FramePadding;
			case UIStyleVarKind::FrameRounding: return ImGuiStyleVar_FrameRounding;
			case UIStyleVarKind::FrameBorderSize: return ImGuiStyleVar_FrameBorderSize;
			case UIStyleVarKind::ItemSpacing: return ImGuiStyleVar_ItemSpacing;
			case UIStyleVarKind::ItemInnerSpacing: return ImGuiStyleVar_ItemInnerSpacing;
			case UIStyleVarKind::IndentSpacing: return ImGuiStyleVar_IndentSpacing;
			case UIStyleVarKind::ScrollbarSize: return ImGuiStyleVar_ScrollbarSize;
			case UIStyleVarKind::GrabMinSize: return ImGuiStyleVar_GrabMinSize;
			default: return ImGuiStyleVar_Alpha;
			}
		}

		struct InputTextCallbackUserData
		{
			std::string* value = nullptr;
		};

		static int input_text_resize_callback(ImGuiInputTextCallbackData* data)
		{
			if (!data || data->EventFlag != ImGuiInputTextFlags_CallbackResize)
			{
				return 0;
			}

			auto* user_data = static_cast<InputTextCallbackUserData*>(data->UserData);
			if (!user_data || !user_data->value)
			{
				return 0;
			}

			std::string& value = *user_data->value;
			value.resize(static_cast<size_t>(data->BufTextLen));
			data->Buf = value.data();
			return 0;
		}
	}

	class UIContext::Impl
	{
	public:
		Window* window = nullptr;
		RHI::GraphicsContext* graphics_context = nullptr;
		RenderDevice* render_device = nullptr;
		std::unique_ptr<ImGuiLayer> layer{};
		std::vector<std::shared_ptr<RenderTarget>> frame_render_targets{};
		std::vector<const RenderTarget*> frame_render_target_keys{};
		bool initialized = false;
	};

	UIContext::UIContext()
		: m_impl(new Impl())
	{
	}

	UIContext::~UIContext()
	{
		shutdown();
		delete m_impl;
		m_impl = nullptr;
	}

	bool UIContext::init(Window* window, RHI::GraphicsContext* graphics_context, RenderDevice* render_device, const UIContextConfig& config)
	{
		if (!m_impl)
		{
			return false;
		}

		if (m_impl->initialized)
		{
			return true;
		}
		if (!window || !graphics_context || !render_device)
		{
			return false;
		}

		m_impl->window = window;
		m_impl->graphics_context = graphics_context;
		m_impl->render_device = render_device;
		m_impl->layer = create_imgui_layer(Application::get_rhi_backend());
		if (!m_impl->layer || !m_impl->layer->init(window, graphics_context, render_device, config))
		{
			m_impl->layer.reset();
			m_impl->window = nullptr;
			m_impl->graphics_context = nullptr;
			m_impl->render_device = nullptr;
			return false;
		}

		m_impl->initialized = true;
		return true;
	}

	void UIContext::shutdown()
	{
		if (!m_impl)
		{
			return;
		}

		m_impl->frame_render_targets.clear();
		m_impl->frame_render_target_keys.clear();
		if (m_impl->layer)
		{
			m_impl->layer->shutdown();
			m_impl->layer.reset();
		}
		m_impl->window = nullptr;
		m_impl->graphics_context = nullptr;
		m_impl->render_device = nullptr;
		m_impl->initialized = false;
	}

	bool UIContext::begin_frame()
	{
		if (!m_impl)
		{
			return false;
		}

		m_impl->frame_render_targets.clear();
		m_impl->frame_render_target_keys.clear();
		return m_impl->layer ? m_impl->layer->begin_frame() : false;
	}

	bool UIContext::render()
	{
		bool result = true;
		if (m_impl && m_impl->layer)
		{
			result = m_impl->layer->render(m_impl->frame_render_targets);
		}
		if (m_impl)
		{
			m_impl->frame_render_targets.clear();
			m_impl->frame_render_target_keys.clear();
		}
		return result;
	}

	void UIContext::handle_window_event(const WindowEvent& event)
	{
		if (m_impl && m_impl->layer)
		{
			m_impl->layer->handle_window_event(event);
		}
	}

	bool UIContext::is_initialized() const
	{
		return m_impl && m_impl->initialized && m_impl->layer && m_impl->layer->is_initialized();
	}

	bool UIContext::is_frame_active() const
	{
		return m_impl && m_impl->layer && m_impl->layer->is_frame_active();
	}

	bool UIContext::wants_capture_mouse() const
	{
		return m_impl && m_impl->layer && m_impl->layer->wants_capture_mouse();
	}

	bool UIContext::wants_capture_keyboard() const
	{
		return m_impl && m_impl->layer && m_impl->layer->wants_capture_keyboard();
	}

	bool UIContext::wants_text_input() const
	{
		return m_impl && m_impl->layer && m_impl->layer->wants_text_input();
	}

	void UIContext::show_demo_window(bool* open)
	{
		if (is_frame_active())
		{
			ImGui::ShowDemoWindow(open);
		}
	}

	bool UIContext::begin_window(const char* name, bool* open, UIWindowFlags flags)
	{
		return is_frame_active() && name ? ImGui::Begin(name, open, to_imgui_window_flags(flags)) : false;
	}

	bool UIContext::begin_dockspace_host_window(const char* name, bool* open, UIWindowFlags flags)
	{
		if (!is_frame_active() || !name)
		{
			return false;
		}

		const UIWindowFlags resolved_flags =
			flags |
			UIWindowFlagBits::NoDocking |
			UIWindowFlagBits::NoTitleBar |
			UIWindowFlagBits::NoCollapse |
			UIWindowFlagBits::NoResize |
			UIWindowFlagBits::NoMove |
			UIWindowFlagBits::NoBringToFrontOnFocus |
			UIWindowFlagBits::NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		const bool opened = ImGui::Begin(name, open, to_imgui_window_flags(resolved_flags));
		ImGui::PopStyleVar(2);
		return opened;
	}

	void UIContext::end_window()
	{
		if (is_frame_active())
		{
			ImGui::End();
		}
	}

	bool UIContext::begin_child(const char* str_id, const UIVec2& size, UIChildFlags child_flags, UIWindowFlags window_flags)
	{
		return is_frame_active() && str_id ?
			ImGui::BeginChild(str_id, to_imvec2(size), to_imgui_child_flags(child_flags), to_imgui_window_flags(window_flags)) :
			false;
	}

	void UIContext::end_child()
	{
		if (is_frame_active())
		{
			ImGui::EndChild();
		}
	}

	UIDockNodeId UIContext::dock_space(const char* str_id, const UIVec2& size, UIDockNodeFlags flags)
	{
		if (!is_frame_active() || !str_id)
		{
			return 0u;
		}

		return dock_space(static_cast<UIDockNodeId>(ImGui::GetID(str_id)), size, flags);
	}

	UIDockNodeId UIContext::dock_space(UIDockNodeId dockspace_id, const UIVec2& size, UIDockNodeFlags flags)
	{
		if (!is_frame_active() || dockspace_id == 0u)
		{
			return 0u;
		}

		ImGui::DockSpace(static_cast<ImGuiID>(dockspace_id), to_imvec2(size), to_imgui_dock_node_flags(flags));
		return dockspace_id;
	}

	void UIContext::dock_builder_remove_node(UIDockNodeId node_id)
	{
		if (is_frame_active() && node_id != 0u)
		{
			ImGui::DockBuilderRemoveNode(static_cast<ImGuiID>(node_id));
		}
	}

	void UIContext::dock_builder_add_node(UIDockNodeId node_id, UIDockNodeFlags flags)
	{
		if (is_frame_active() && node_id != 0u)
		{
			ImGui::DockBuilderAddNode(static_cast<ImGuiID>(node_id), to_imgui_dock_node_flags(flags));
		}
	}

	void UIContext::dock_builder_set_node_size(UIDockNodeId node_id, const UIVec2& size)
	{
		if (is_frame_active() && node_id != 0u)
		{
			ImGui::DockBuilderSetNodeSize(static_cast<ImGuiID>(node_id), to_imvec2(size));
		}
	}

	UIDockNodeId UIContext::dock_builder_split_node(UIDockNodeId node_id, UIDirection direction, float size_ratio_for_node_at_dir, UIDockNodeId* out_id_at_dir, UIDockNodeId* out_id_at_opposite_dir)
	{
		if (!is_frame_active() || node_id == 0u)
		{
			return 0u;
		}

		const ImGuiDir imgui_direction = to_imgui_dir(direction);
		if (imgui_direction == ImGuiDir_None)
		{
			return 0u;
		}

		ImGuiID imgui_id_at_dir = 0u;
		ImGuiID imgui_id_at_opposite_dir = 0u;
		const ImGuiID split_id = ImGui::DockBuilderSplitNode(
			static_cast<ImGuiID>(node_id),
			imgui_direction,
			size_ratio_for_node_at_dir,
			out_id_at_dir ? &imgui_id_at_dir : nullptr,
			out_id_at_opposite_dir ? &imgui_id_at_opposite_dir : nullptr);

		if (out_id_at_dir)
		{
			*out_id_at_dir = static_cast<UIDockNodeId>(imgui_id_at_dir);
		}
		if (out_id_at_opposite_dir)
		{
			*out_id_at_opposite_dir = static_cast<UIDockNodeId>(imgui_id_at_opposite_dir);
		}
		return static_cast<UIDockNodeId>(split_id);
	}

	void UIContext::dock_builder_dock_window(const char* window_name, UIDockNodeId node_id)
	{
		if (is_frame_active() && window_name && node_id != 0u)
		{
			ImGui::DockBuilderDockWindow(window_name, static_cast<ImGuiID>(node_id));
		}
	}

	void UIContext::dock_builder_finish(UIDockNodeId node_id)
	{
		if (is_frame_active() && node_id != 0u)
		{
			ImGui::DockBuilderFinish(static_cast<ImGuiID>(node_id));
		}
	}

	void UIContext::set_next_window_position(const UIVec2& position, UIConditionFlags cond, const UIVec2& pivot)
	{
		if (is_frame_active())
		{
			ImGui::SetNextWindowPos(to_imvec2(position), to_imgui_cond(cond), to_imvec2(pivot));
		}
	}

	void UIContext::set_next_window_size(const UIVec2& size, UIConditionFlags cond)
	{
		if (is_frame_active())
		{
			ImGui::SetNextWindowSize(to_imvec2(size), to_imgui_cond(cond));
		}
	}

	void UIContext::set_next_window_viewport(UIViewportId viewport_id)
	{
		if (is_frame_active() && viewport_id != 0u)
		{
			ImGui::SetNextWindowViewport(static_cast<ImGuiID>(viewport_id));
		}
	}

	void UIContext::set_next_window_collapsed(bool collapsed, UIConditionFlags cond)
	{
		if (is_frame_active())
		{
			ImGui::SetNextWindowCollapsed(collapsed, to_imgui_cond(cond));
		}
	}

	void UIContext::set_next_item_width(float width)
	{
		if (is_frame_active())
		{
			ImGui::SetNextItemWidth(width);
		}
	}

	void UIContext::same_line(float offset_from_start_x, float spacing)
	{
		if (is_frame_active())
		{
			ImGui::SameLine(offset_from_start_x, spacing);
		}
	}

	void UIContext::separator()
	{
		if (is_frame_active())
		{
			ImGui::Separator();
		}
	}

	void UIContext::spacing()
	{
		if (is_frame_active())
		{
			ImGui::Spacing();
		}
	}

	void UIContext::dummy(const UIVec2& size)
	{
		if (is_frame_active())
		{
			ImGui::Dummy(to_imvec2(size));
		}
	}

	void UIContext::begin_group()
	{
		if (is_frame_active())
		{
			ImGui::BeginGroup();
		}
	}

	void UIContext::end_group()
	{
		if (is_frame_active())
		{
			ImGui::EndGroup();
		}
	}

	void UIContext::begin_disabled(bool disabled)
	{
		if (is_frame_active())
		{
			ImGui::BeginDisabled(disabled);
		}
	}

	void UIContext::end_disabled()
	{
		if (is_frame_active())
		{
			ImGui::EndDisabled();
		}
	}

	void UIContext::push_id(const char* str_id)
	{
		if (is_frame_active() && str_id)
		{
			ImGui::PushID(str_id);
		}
	}

	void UIContext::push_id(int32_t int_id)
	{
		if (is_frame_active())
		{
			ImGui::PushID(int_id);
		}
	}

	void UIContext::pop_id()
	{
		if (is_frame_active())
		{
			ImGui::PopID();
		}
	}

	void UIContext::push_style_color(UIStyleColorKind kind, const UIColor& color)
	{
		if (is_frame_active())
		{
			ImGui::PushStyleColor(to_imgui_style_color(kind), to_imvec4(color));
		}
	}

	void UIContext::pop_style_color(int count)
	{
		if (is_frame_active())
		{
			ImGui::PopStyleColor(count);
		}
	}

	void UIContext::push_style_var(UIStyleVarKind kind, float value)
	{
		if (!is_frame_active())
		{
			return;
		}

		switch (kind)
		{
		case UIStyleVarKind::Alpha:
		case UIStyleVarKind::WindowRounding:
		case UIStyleVarKind::WindowBorderSize:
		case UIStyleVarKind::FrameRounding:
		case UIStyleVarKind::FrameBorderSize:
		case UIStyleVarKind::IndentSpacing:
		case UIStyleVarKind::ScrollbarSize:
		case UIStyleVarKind::GrabMinSize:
			ImGui::PushStyleVar(to_imgui_style_var(kind), value);
			break;
		default:
			break;
		}
	}

	void UIContext::push_style_var(UIStyleVarKind kind, const UIVec2& value)
	{
		if (!is_frame_active())
		{
			return;
		}

		switch (kind)
		{
		case UIStyleVarKind::WindowPadding:
		case UIStyleVarKind::FramePadding:
		case UIStyleVarKind::ItemSpacing:
		case UIStyleVarKind::ItemInnerSpacing:
			ImGui::PushStyleVar(to_imgui_style_var(kind), to_imvec2(value));
			break;
		default:
			break;
		}
	}

	void UIContext::pop_style_var(int count)
	{
		if (is_frame_active())
		{
			ImGui::PopStyleVar(count);
		}
	}

	void UIContext::text_unformatted(const char* text)
	{
		if (is_frame_active() && text)
		{
			ImGui::TextUnformatted(text);
		}
	}

	void UIContext::text(const char* format, ...)
	{
		if (!is_frame_active() || !format)
		{
			return;
		}

		va_list args;
		va_start(args, format);
		ImGui::TextV(format, args);
		va_end(args);
	}

	void UIContext::text_wrapped(const char* format, ...)
	{
		if (!is_frame_active() || !format)
		{
			return;
		}

		va_list args;
		va_start(args, format);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextV(format, args);
		ImGui::PopTextWrapPos();
		va_end(args);
	}

	void UIContext::text_colored(const UIColor& color, const char* format, ...)
	{
		if (!is_frame_active() || !format)
		{
			return;
		}

		va_list args;
		va_start(args, format);
		ImGui::TextColoredV(to_imvec4(color), format, args);
		va_end(args);
	}

	void UIContext::bullet_text(const char* format, ...)
	{
		if (!is_frame_active() || !format)
		{
			return;
		}

		va_list args;
		va_start(args, format);
		ImGui::BulletTextV(format, args);
		va_end(args);
	}

	bool UIContext::button(const char* label, const UIVec2& size)
	{
		return is_frame_active() && label ? ImGui::Button(label, to_imvec2(size)) : false;
	}

	bool UIContext::small_button(const char* label)
	{
		return is_frame_active() && label ? ImGui::SmallButton(label) : false;
	}

	bool UIContext::checkbox(const char* label, bool& value)
	{
		return is_frame_active() && label ? ImGui::Checkbox(label, &value) : false;
	}

	bool UIContext::selectable(const char* label, bool selected, UISelectableFlags flags, const UIVec2& size)
	{
		return is_frame_active() && label ? ImGui::Selectable(label, selected, to_imgui_selectable_flags(flags), to_imvec2(size)) : false;
	}

	bool UIContext::collapsing_header(const char* label, UITreeNodeFlags flags)
	{
		return is_frame_active() && label ? ImGui::CollapsingHeader(label, to_imgui_tree_node_flags(flags)) : false;
	}

	bool UIContext::tree_node(const char* label, UITreeNodeFlags flags)
	{
		return is_frame_active() && label ? ImGui::TreeNodeEx(label, to_imgui_tree_node_flags(flags)) : false;
	}

	bool UIContext::tree_node(const void* stable_id, const char* label, UITreeNodeFlags flags)
	{
		return is_frame_active() && stable_id && label ?
			ImGui::TreeNodeEx(stable_id, to_imgui_tree_node_flags(flags), "%s", label) :
			false;
	}

	void UIContext::tree_pop()
	{
		if (is_frame_active())
		{
			ImGui::TreePop();
		}
	}

	bool UIContext::input_text(const char* label, std::string& value, UIInputTextFlags flags)
	{
		if (!is_frame_active() || !label)
		{
			return false;
		}

		InputTextCallbackUserData user_data{ &value };
		ImGuiInputTextFlags imgui_flags = to_imgui_input_text_flags(flags) | ImGuiInputTextFlags_CallbackResize;
		if (value.capacity() == 0u)
		{
			value.reserve(32u);
		}
		return ImGui::InputText(label, const_cast<char*>(value.c_str()), value.capacity() + 1u, imgui_flags, input_text_resize_callback, &user_data);
	}

	bool UIContext::input_text_multiline(const char* label, std::string& value, const UIVec2& size, UIInputTextFlags flags)
	{
		if (!is_frame_active() || !label)
		{
			return false;
		}

		InputTextCallbackUserData user_data{ &value };
		ImGuiInputTextFlags imgui_flags = to_imgui_input_text_flags(flags) | ImGuiInputTextFlags_CallbackResize;
		if (value.capacity() == 0u)
		{
			value.reserve(64u);
		}
		return ImGui::InputTextMultiline(label, const_cast<char*>(value.c_str()), value.capacity() + 1u, to_imvec2(size), imgui_flags, input_text_resize_callback, &user_data);
	}

	bool UIContext::input_int(const char* label, int32_t& value, int32_t step, int32_t step_fast)
	{
		return is_frame_active() && label ? ImGui::InputInt(label, &value, step, step_fast) : false;
	}

	bool UIContext::input_float(const char* label, float& value, float step, float step_fast, const char* format)
	{
		return is_frame_active() && label ? ImGui::InputFloat(label, &value, step, step_fast, format ? format : "%.3f") : false;
	}

	bool UIContext::input_float2(const char* label, float value[2], const char* format, UIInputTextFlags flags)
	{
		return is_frame_active() && label && value ?
			ImGui::InputFloat2(label, value, format ? format : "%.3f", to_imgui_input_text_flags(flags)) :
			false;
	}

	bool UIContext::input_float3(const char* label, float value[3], const char* format, UIInputTextFlags flags)
	{
		return is_frame_active() && label && value ?
			ImGui::InputFloat3(label, value, format ? format : "%.3f", to_imgui_input_text_flags(flags)) :
			false;
	}

	bool UIContext::input_float4(const char* label, float value[4], const char* format, UIInputTextFlags flags)
	{
		return is_frame_active() && label && value ?
			ImGui::InputFloat4(label, value, format ? format : "%.3f", to_imgui_input_text_flags(flags)) :
			false;
	}

	bool UIContext::drag_float(const char* label, float& value, float speed, float min_value, float max_value, const char* format)
	{
		return is_frame_active() && label ? ImGui::DragFloat(label, &value, speed, min_value, max_value, format ? format : "%.3f") : false;
	}

	bool UIContext::drag_float2(const char* label, float value[2], float speed, float min_value, float max_value, const char* format)
	{
		return is_frame_active() && label && value ?
			ImGui::DragFloat2(label, value, speed, min_value, max_value, format ? format : "%.3f") :
			false;
	}

	bool UIContext::drag_float3(const char* label, float value[3], float speed, float min_value, float max_value, const char* format)
	{
		return is_frame_active() && label && value ?
			ImGui::DragFloat3(label, value, speed, min_value, max_value, format ? format : "%.3f") :
			false;
	}

	bool UIContext::drag_float4(const char* label, float value[4], float speed, float min_value, float max_value, const char* format)
	{
		return is_frame_active() && label && value ?
			ImGui::DragFloat4(label, value, speed, min_value, max_value, format ? format : "%.3f") :
			false;
	}

	bool UIContext::slider_float(const char* label, float& value, float min_value, float max_value, const char* format)
	{
		return is_frame_active() && label ? ImGui::SliderFloat(label, &value, min_value, max_value, format ? format : "%.3f") : false;
	}

	bool UIContext::color_edit3(const char* label, float value[3])
	{
		return is_frame_active() && label && value ? ImGui::ColorEdit3(label, value) : false;
	}

	bool UIContext::color_edit3(const char* label, UIColor& value)
	{
		return color_edit3(label, &value.r);
	}

	bool UIContext::color_edit4(const char* label, float value[4])
	{
		return is_frame_active() && label && value ? ImGui::ColorEdit4(label, value) : false;
	}

	bool UIContext::color_edit4(const char* label, UIColor& value)
	{
		return color_edit4(label, &value.r);
	}

	bool UIContext::combo(const char* label, int32_t& current_index, const std::vector<const char*>& items, int32_t popup_max_height_in_items)
	{
		if (!is_frame_active() || !label || items.empty())
		{
			return false;
		}
		return ImGui::Combo(label, &current_index, items.data(), static_cast<int32_t>(items.size()), popup_max_height_in_items);
	}

	bool UIContext::combo(const char* label, int32_t& current_index, const std::vector<std::string>& items, int32_t popup_max_height_in_items)
	{
		std::vector<const char*> item_ptrs;
		item_ptrs.reserve(items.size());
		for (const std::string& item : items)
		{
			item_ptrs.push_back(item.c_str());
		}
		return combo(label, current_index, item_ptrs, popup_max_height_in_items);
	}

	bool UIContext::begin_main_menu_bar()
	{
		return is_frame_active() && ImGui::BeginMainMenuBar();
	}

	void UIContext::end_main_menu_bar()
	{
		if (is_frame_active())
		{
			ImGui::EndMainMenuBar();
		}
	}

	bool UIContext::begin_menu_bar()
	{
		return is_frame_active() && ImGui::BeginMenuBar();
	}

	void UIContext::end_menu_bar()
	{
		if (is_frame_active())
		{
			ImGui::EndMenuBar();
		}
	}

	bool UIContext::begin_menu(const char* label, bool enabled)
	{
		return is_frame_active() && label ? ImGui::BeginMenu(label, enabled) : false;
	}

	void UIContext::end_menu()
	{
		if (is_frame_active())
		{
			ImGui::EndMenu();
		}
	}

	bool UIContext::menu_item(const char* label, const char* shortcut, bool selected, bool enabled)
	{
		return is_frame_active() && label ? ImGui::MenuItem(label, shortcut, selected, enabled) : false;
	}

	bool UIContext::menu_item(const char* label, const char* shortcut, bool* selected, bool enabled)
	{
		return is_frame_active() && label && selected ? ImGui::MenuItem(label, shortcut, selected, enabled) : false;
	}

	bool UIContext::begin_tab_bar(const char* str_id, UITabBarFlags flags)
	{
		return is_frame_active() && str_id ? ImGui::BeginTabBar(str_id, to_imgui_tab_bar_flags(flags)) : false;
	}

	void UIContext::end_tab_bar()
	{
		if (is_frame_active())
		{
			ImGui::EndTabBar();
		}
	}

	bool UIContext::begin_tab_item(const char* label, bool* open, UITabItemFlags flags)
	{
		return is_frame_active() && label ? ImGui::BeginTabItem(label, open, to_imgui_tab_item_flags(flags)) : false;
	}

	void UIContext::end_tab_item()
	{
		if (is_frame_active())
		{
			ImGui::EndTabItem();
		}
	}

	bool UIContext::begin_table(const char* str_id, int32_t column_count, UITableFlags flags, const UIVec2& outer_size, float inner_width)
	{
		return is_frame_active() && str_id && column_count > 0 ?
			ImGui::BeginTable(str_id, column_count, to_imgui_table_flags(flags), to_imvec2(outer_size), inner_width) :
			false;
	}

	void UIContext::end_table()
	{
		if (is_frame_active())
		{
			ImGui::EndTable();
		}
	}

	void UIContext::table_next_row()
	{
		if (is_frame_active())
		{
			ImGui::TableNextRow();
		}
	}

	bool UIContext::table_next_column()
	{
		return is_frame_active() && ImGui::TableNextColumn();
	}

	void UIContext::table_setup_column(const char* label, float init_width_or_weight)
	{
		if (is_frame_active() && label)
		{
			ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_None, init_width_or_weight);
		}
	}

	void UIContext::table_setup_column(const char* label, UITableColumnFlags flags, float init_width_or_weight)
	{
		if (is_frame_active() && label)
		{
			ImGui::TableSetupColumn(label, to_imgui_table_column_flags(flags), init_width_or_weight);
		}
	}

	void UIContext::table_headers_row()
	{
		if (is_frame_active())
		{
			ImGui::TableHeadersRow();
		}
	}

	void UIContext::open_popup(const char* str_id)
	{
		if (is_frame_active() && str_id)
		{
			ImGui::OpenPopup(str_id);
		}
	}

	bool UIContext::begin_popup(const char* str_id, UIWindowFlags flags)
	{
		return is_frame_active() && str_id ? ImGui::BeginPopup(str_id, to_imgui_window_flags(flags)) : false;
	}

	bool UIContext::begin_popup_modal(const char* name, bool* open, UIWindowFlags flags)
	{
		return is_frame_active() && name ? ImGui::BeginPopupModal(name, open, to_imgui_window_flags(flags)) : false;
	}

	void UIContext::end_popup()
	{
		if (is_frame_active())
		{
			ImGui::EndPopup();
		}
	}

	void UIContext::close_current_popup()
	{
		if (is_frame_active())
		{
			ImGui::CloseCurrentPopup();
		}
	}

	bool UIContext::is_item_hovered() const
	{
		return is_frame_active() && ImGui::IsItemHovered();
	}

	bool UIContext::is_item_clicked(UIMouseButton button) const
	{
		return is_frame_active() && ImGui::IsItemClicked(static_cast<ImGuiMouseButton>(button));
	}

	bool UIContext::is_window_focused() const
	{
		return is_frame_active() && ImGui::IsWindowFocused();
	}

	bool UIContext::is_window_hovered() const
	{
		return is_frame_active() && ImGui::IsWindowHovered();
	}

	UIVec2 UIContext::get_content_region_avail() const
	{
		if (!is_frame_active())
		{
			return {};
		}
		const ImVec2 value = ImGui::GetContentRegionAvail();
		return { value.x, value.y };
	}

	UIRect UIContext::get_main_viewport_rect() const
	{
		if (!is_frame_active())
		{
			return {};
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (!viewport)
		{
			return {};
		}

		return { viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y };
	}

	UIViewportId UIContext::get_main_viewport_id() const
	{
		if (!is_frame_active())
		{
			return 0u;
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		return viewport ? static_cast<UIViewportId>(viewport->ID) : 0u;
	}

	UIVec2 UIContext::get_cursor_pos() const
	{
		if (!is_frame_active())
		{
			return {};
		}
		const ImVec2 value = ImGui::GetCursorPos();
		return { value.x, value.y };
	}

	void UIContext::set_cursor_pos(const UIVec2& position)
	{
		if (is_frame_active())
		{
			ImGui::SetCursorPos(to_imvec2(position));
		}
	}

	float UIContext::get_window_width() const
	{
		return is_frame_active() ? ImGui::GetWindowWidth() : 0.0f;
	}

	float UIContext::get_window_height() const
	{
		return is_frame_active() ? ImGui::GetWindowHeight() : 0.0f;
	}

	UITextureHandle UIContext::register_render_target(const std::shared_ptr<RenderTarget>& render_target)
	{
		return m_impl && m_impl->layer ? m_impl->layer->register_render_target(render_target) : nullptr;
	}

	void UIContext::unregister_render_target(const std::shared_ptr<RenderTarget>& render_target)
	{
		if (m_impl && m_impl->layer)
		{
			m_impl->layer->unregister_render_target(render_target);
		}
	}

	UITextureHandle UIContext::get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target)
	{
		if (!m_impl || !m_impl->layer)
		{
			return nullptr;
		}

		UITextureHandle texture_handle = m_impl->layer->get_render_target_texture_id(render_target);
		if (texture_handle)
		{
			track_render_target_usage(render_target);
		}
		return texture_handle;
	}

	void UIContext::image(const std::shared_ptr<RenderTarget>& render_target, const UIVec2& size, const UIVec2& uv0, const UIVec2& uv1, const UIColor& tint, const UIColor& border)
	{
		UITextureHandle texture = get_render_target_texture_id(render_target);
		image(texture, size, uv0, uv1, tint, border);
	}

	void UIContext::image(UITextureHandle texture, const UIVec2& size, const UIVec2& uv0, const UIVec2& uv1, const UIColor& tint, const UIColor& border)
	{
		if (!is_frame_active() || !texture)
		{
			return;
		}
		ImGui::Image(texture, to_imvec2(size), to_imvec2(uv0), to_imvec2(uv1), to_imvec4(tint), to_imvec4(border));
	}

	void UIContext::draw_render_target(const std::shared_ptr<RenderTarget>& render_target, const UIVec2& size)
	{
		image(render_target, size);
	}

	void UIContext::draw_render_target_fill_available(const std::shared_ptr<RenderTarget>& render_target, bool preserve_aspect, const UIColor& tint, const UIColor& border)
	{
		if (!is_frame_active() || !render_target)
		{
			return;
		}

		UIVec2 draw_size = get_content_region_avail();
		if (draw_size.x <= 0.0f || draw_size.y <= 0.0f)
		{
			return;
		}

		if (preserve_aspect)
		{
			const UIVec2 fitted_size = fit_size_into_region(
				draw_size,
				static_cast<float>(render_target->get_width()),
				static_cast<float>(render_target->get_height()));
			if (fitted_size.x > 0.0f && fitted_size.y > 0.0f)
			{
				const UIVec2 cursor = get_cursor_pos();
				set_cursor_pos({
					cursor.x + (draw_size.x - fitted_size.x) * 0.5f,
					cursor.y + (draw_size.y - fitted_size.y) * 0.5f
				});
				draw_size = fitted_size;
			}
		}

		image(render_target, draw_size, { 0.0f, 0.0f }, { 1.0f, 1.0f }, tint, border);
	}

	void UIContext::track_render_target_usage(const std::shared_ptr<RenderTarget>& render_target)
	{
		if (!m_impl || !render_target)
		{
			return;
		}

		if (std::find(m_impl->frame_render_target_keys.begin(), m_impl->frame_render_target_keys.end(), render_target.get()) != m_impl->frame_render_target_keys.end())
		{
			return;
		}

		m_impl->frame_render_target_keys.push_back(render_target.get());
		m_impl->frame_render_targets.push_back(render_target);
	}
}
