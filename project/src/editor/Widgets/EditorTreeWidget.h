#pragma once

#include "Function/Gui/UICommon.h"
#include "imgui.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	enum class EditorTreeDropVisual : uint8_t
	{
		None = 0,
		Before,
		After,
		Into
	};

	struct EditorTreeWidgetStyle
	{
		float row_height = 0.0f;
		float indent_spacing = 14.0f;
		float icon_size = 16.0f;
		float icon_text_spacing = 3.0f;
		float row_padding_y = 5.0f;
		float row_spacing_y = 3.0f;
		float guide_line_thickness = 1.0f;
		float guide_line_padding_y = 1.0f;
		float connector_horizontal_padding = 4.0f;
		float drop_zone_ratio = 0.25f;
		float auto_expand_hover_delay_seconds = 0.45f;
		AshEngine::UIColor guide_line_color{ 0.55f, 0.58f, 0.63f, 0.60f };
		AshEngine::UIColor drop_accent_color{ 0.35f, 0.65f, 1.0f, 1.0f };
		AshEngine::UIColor row_hover_fill_color{ 0.28f, 0.39f, 0.49f, 0.18f };
		AshEngine::UIColor row_hover_outline_color{ 0.38f, 0.56f, 0.74f, 0.42f };
		AshEngine::UIColor row_selected_fill_color{ 0.31f, 0.46f, 0.58f, 0.28f };
		AshEngine::UIColor row_selected_outline_color{ 0.43f, 0.64f, 0.85f, 0.80f };
	};

	struct EditorTreeWidgetState
	{
		uint64_t hover_auto_expand_key = 0;
		uint64_t pending_auto_expand_key = 0;
		double hover_auto_expand_start_time = 0.0;

		void reset_drag_state();
	};

	struct EditorTreeDragSourceDesc
	{
		const char* payload_type = nullptr;
		const void* payload_data = nullptr;
		int32_t payload_size = 0;
		const char* preview_text = nullptr;
	};

	struct EditorTreeDropTargetDesc
	{
		using ValidationCallback = bool (*)(const ImGuiPayload* payload, EditorTreeDropVisual visual, void* user_data);

		const char* payload_type = nullptr;
		bool allow_before = false;
		bool allow_after = false;
		bool allow_into = false;
		bool auto_expand_on_into_hover = false;
		ValidationCallback validate_drop = nullptr;
		void* validation_user_data = nullptr;
	};

	struct EditorTreeItemDesc
	{
		std::string_view unique_id{};
		std::string_view label{};
		AshEngine::UITextureHandle icon = nullptr;
		AshEngine::UITextureHandle icon_when_open = nullptr;
		bool selected = false;
		bool has_children = false;
		bool default_open = false;
		bool is_last_sibling = true;
		const EditorTreeDragSourceDesc* drag_source = nullptr;
		const EditorTreeDropTargetDesc* drop_target = nullptr;
	};

	struct EditorTreeItemResult
	{
		bool opened = false;
		bool clicked = false;
		bool hovered = false;
		bool drop_hovered = false;
		bool drop_delivered = false;
		EditorTreeDropVisual drop_visual = EditorTreeDropVisual::None;
		const ImGuiPayload* accepted_payload = nullptr;
		ImVec2 item_min{};
		ImVec2 item_max{};
	};

	struct EditorTreeDropSlotDesc
	{
		std::string_view unique_id{};
		float height = 18.0f;
		bool expand_to_available_height_while_dragging = false;
		EditorTreeDropVisual preview_visual = EditorTreeDropVisual::Before;
		const EditorTreeDropTargetDesc* drop_target = nullptr;
	};

	struct EditorTreeDropSlotResult
	{
		bool drop_hovered = false;
		bool drop_delivered = false;
		EditorTreeDropVisual drop_visual = EditorTreeDropVisual::None;
		const ImGuiPayload* accepted_payload = nullptr;
		ImVec2 item_min{};
		ImVec2 item_max{};
	};

	class EditorTreeWidget final
	{
	public:
		EditorTreeWidget(AshEngine::UIContext& ui, EditorTreeWidgetState& state, const EditorTreeWidgetStyle& style = {});
		~EditorTreeWidget();

	public:
		void reset_drag_state_if_inactive();
		void push_level(bool ancestor_has_more_siblings);
		void pop_level();
		EditorTreeItemResult draw_item(const EditorTreeItemDesc& desc);
		EditorTreeDropSlotResult draw_drop_slot(const EditorTreeDropSlotDesc& desc, bool dragging_matching_payload);

	private:
		uint64_t make_id_key(std::string_view unique_id) const;
		EditorTreeDropVisual resolve_drop_visual(const EditorTreeDropTargetDesc& desc, const ImVec2& item_min, const ImVec2& item_max) const;
		void update_auto_expand_hover(uint64_t item_key);
		void clear_auto_expand_hover(uint64_t item_key);
		void draw_row_background(const EditorTreeItemDesc& desc, const EditorTreeItemResult& result) const;
		void draw_drop_preview(EditorTreeDropVisual visual, const ImVec2& item_min, const ImVec2& item_max) const;
		void draw_guides(const EditorTreeItemDesc& desc, const EditorTreeItemResult& result, float row_start_x) const;
		void draw_item_content(const EditorTreeItemDesc& desc, const EditorTreeItemResult& result, float row_start_x) const;

	private:
		AshEngine::UIContext& m_ui;
		EditorTreeWidgetState& m_state;
		EditorTreeWidgetStyle m_style{};
		float m_treeStartX = 0.0f;
		std::vector<bool> m_ancestorHasMoreSiblings{};
	};
}
