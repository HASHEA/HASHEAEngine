#pragma once

#include "Base/hcore.h"
#include "Function/Gui/UICommon.h"

#include <cstdint>

namespace AshEngine
{
	// editor begin 修改原因：为编辑器材质图与通用节点面板提供 Engine 侧节点画布门面，避免 Editor 直接触碰 ImGui/node-editor。
	using UINodeId = uint64_t;
	using UIPinId = uint64_t;
	using UILinkId = uint64_t;

	enum class UINodePinKind : uint8_t
	{
		Input = 0,
		Output = 1
	};

	enum class UINodePinShape : uint8_t
	{
		Circle = 0,
		Square = 1
	};

	// Engine-side facade over imgui-node-editor (ax::NodeEditor), mirroring the UIContext style.
	// Hides all ImGui / node-editor types behind house types so the editor never touches raw ImGui.
	// One instance owns one node-editor canvas context; submit nodes/pins/links between begin()/end().
	class ASH_API UINodeEditor
	{
	public:
		UINodeEditor();
		~UINodeEditor();

		UINodeEditor(const UINodeEditor&) = delete;
		UINodeEditor& operator=(const UINodeEditor&) = delete;

	public:
		bool begin(const char* str_id, const UIVec2& size = {});
		void end();

		void begin_node(UINodeId id);
		void end_node();

		void begin_pin(UIPinId id, UINodePinKind kind);
		void draw_pin_marker(UINodePinShape shape = UINodePinShape::Circle, const UIColor& color = { 0.35f, 0.68f, 1.0f, 1.0f }, float size = 12.0f);
		void end_pin();

		void link(UILinkId id, UIPinId start_pin, UIPinId end_pin, const UIColor& color = { 1.0f, 1.0f, 1.0f, 1.0f }, float thickness = 2.0f);

		// Link creation interaction. Wrap the per-frame query in begin_create()/end_create().
		bool begin_create();
		bool query_new_link(UIPinId* out_start_pin, UIPinId* out_end_pin);
		bool query_new_node(UIPinId* out_pin);
		bool accept_new_item();
		void reject_new_item();
		void end_create();

		// Deletion interaction. Wrap the per-frame query in begin_delete()/end_delete().
		bool begin_delete();
		bool query_deleted_link(UILinkId* out_link, UIPinId* out_start_pin = nullptr, UIPinId* out_end_pin = nullptr);
		bool query_deleted_node(UINodeId* out_node);
		bool accept_deleted_item(bool delete_dependencies = true);
		void reject_deleted_item();
		void end_delete();

		void set_node_position(UINodeId id, const UIVec2& position);
		UIVec2 get_node_position(UINodeId id);
		void navigate_to_content(float duration = -1.0f);
		UIVec2 screen_to_canvas(const UIVec2& screen_position);
		bool is_node_selected(UINodeId id);
		bool is_link_selected(UILinkId id);
		bool has_selection_changed();
		int32_t get_selected_object_count();
		int32_t get_selected_nodes(UINodeId* out_nodes, int32_t max_count);
		int32_t get_selected_links(UILinkId* out_links, int32_t max_count);
		bool show_node_context_menu(UINodeId* out_node);
		bool show_link_context_menu(UILinkId* out_link);
		bool show_background_context_menu();

		void suspend();
		void resume();

	private:
		class Impl;
		Impl* m_impl = nullptr;
	};
	// editor end
}
