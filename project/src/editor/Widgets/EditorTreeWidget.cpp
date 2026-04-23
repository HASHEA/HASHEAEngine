#include "Widgets/EditorTreeWidget.h"

#include "Function/Gui/UIContext.h"

#include <algorithm>
#include <functional>

namespace AshEditor
{
	namespace
	{
		auto to_im_color(const AshEngine::UIColor& color) -> ImU32
		{
			return ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, color.a));
		}

		auto is_drop_valid(
			const EditorTreeDropTargetDesc& desc,
			const ImGuiPayload* payload,
			EditorTreeDropVisual visual) -> bool
		{
			return
				visual != EditorTreeDropVisual::None &&
				(desc.validate_drop == nullptr || desc.validate_drop(payload, visual, desc.validation_user_data));
		}
	}

	void EditorTreeWidgetState::reset_drag_state()
	{
		hover_auto_expand_key = 0;
		pending_auto_expand_key = 0;
		hover_auto_expand_start_time = 0.0;
	}

	EditorTreeWidget::EditorTreeWidget(AshEngine::UIContext& ui, EditorTreeWidgetState& state, const EditorTreeWidgetStyle& style)
		: m_ui(ui)
		, m_state(state)
		, m_style(style)
		, m_treeStartX(ImGui::GetCursorScreenPos().x)
	{
		const ImGuiStyle& imgui_style = ImGui::GetStyle();
		const float min_padding_y = std::max(m_style.row_padding_y, 0.0f);
		const float computed_padding_y =
			m_style.row_height > 0.0f
			? std::max(min_padding_y, (m_style.row_height - ImGui::GetFontSize()) * 0.5f)
			: min_padding_y;
		m_ui.push_style_var(
			AshEngine::UIStyleVarKind::FramePadding,
			{ imgui_style.FramePadding.x, computed_padding_y });
		m_ui.push_style_var(
			AshEngine::UIStyleVarKind::ItemSpacing,
			{ imgui_style.ItemSpacing.x, m_style.row_spacing_y });
		m_ui.push_style_var(AshEngine::UIStyleVarKind::IndentSpacing, m_style.indent_spacing);
	}

	EditorTreeWidget::~EditorTreeWidget()
	{
		m_ui.pop_style_var(3);
	}

	void EditorTreeWidget::reset_drag_state_if_inactive()
	{
		const ImGuiPayload* payload = ImGui::GetDragDropPayload();
		if (!payload)
		{
			m_state.reset_drag_state();
		}
	}

	void EditorTreeWidget::push_level(bool ancestor_has_more_siblings)
	{
		m_ancestorHasMoreSiblings.push_back(ancestor_has_more_siblings);
	}

	void EditorTreeWidget::pop_level()
	{
		if (!m_ancestorHasMoreSiblings.empty())
		{
			m_ancestorHasMoreSiblings.pop_back();
		}
	}

	EditorTreeItemResult EditorTreeWidget::draw_item(const EditorTreeItemDesc& desc)
	{
		EditorTreeItemResult result{};
		const uint64_t item_key = make_id_key(desc.unique_id);
		if (desc.has_children && m_state.pending_auto_expand_key == item_key)
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}

		float row_start_x = ImGui::GetCursorScreenPos().x;
		ImGui::PushID(desc.unique_id.data(), desc.unique_id.data() + desc.unique_id.size());

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_FramePadding;
		if (desc.selected)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (!desc.has_children)
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (desc.default_open)
		{
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}

		result.opened = ImGui::TreeNodeEx("##tree_item", flags);
		result.clicked = ImGui::IsItemClicked();
		result.hovered = ImGui::IsItemHovered();
		result.item_min = ImGui::GetItemRectMin();
		result.item_max = ImGui::GetItemRectMax();

		draw_row_background(desc, result);
		draw_guides(desc, result, row_start_x);
		draw_item_content(desc, result, row_start_x);

		if (desc.drag_source &&
			desc.drag_source->payload_type &&
			desc.drag_source->payload_data &&
			desc.drag_source->payload_size > 0 &&
			ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload(
				desc.drag_source->payload_type,
				desc.drag_source->payload_data,
				static_cast<size_t>(desc.drag_source->payload_size));
			if (desc.drag_source->preview_text)
			{
				ImGui::TextUnformatted(desc.drag_source->preview_text);
			}
			ImGui::EndDragDropSource();
		}

		if (desc.drop_target && desc.drop_target->payload_type && ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				desc.drop_target->payload_type,
				ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
			if (payload)
			{
				result.drop_visual = resolve_drop_visual(*desc.drop_target, result.item_min, result.item_max);
				if (is_drop_valid(*desc.drop_target, payload, result.drop_visual))
				{
					result.drop_hovered = true;
					result.drop_delivered = payload->IsDelivery();
					result.accepted_payload = payload;
					draw_drop_preview(result.drop_visual, result.item_min, result.item_max);

					if (result.drop_visual == EditorTreeDropVisual::Into &&
						desc.drop_target->auto_expand_on_into_hover &&
						desc.has_children &&
						!result.opened)
					{
						update_auto_expand_hover(item_key);
					}
					else
					{
						clear_auto_expand_hover(item_key);
					}
				}
				else
				{
					clear_auto_expand_hover(item_key);
				}
			}
			else
			{
				clear_auto_expand_hover(item_key);
			}
			ImGui::EndDragDropTarget();
		}
		else
		{
			clear_auto_expand_hover(item_key);
		}

		if (result.opened && m_state.pending_auto_expand_key == item_key)
		{
			m_state.pending_auto_expand_key = 0;
			if (m_state.hover_auto_expand_key == item_key)
			{
				m_state.hover_auto_expand_key = 0;
				m_state.hover_auto_expand_start_time = 0.0;
			}
		}

		ImGui::PopID();
		return result;
	}

	EditorTreeDropSlotResult EditorTreeWidget::draw_drop_slot(const EditorTreeDropSlotDesc& desc, bool dragging_matching_payload)
	{
		EditorTreeDropSlotResult result{};
		float height = desc.height;
		if (dragging_matching_payload && desc.expand_to_available_height_while_dragging)
		{
			height = std::max(height, m_ui.get_content_region_avail().y);
		}

		ImGui::PushID(desc.unique_id.data(), desc.unique_id.data() + desc.unique_id.size());
		m_ui.dummy({ std::max(m_ui.get_content_region_avail().x, 1.0f), std::max(height, 1.0f) });
		result.item_min = ImGui::GetItemRectMin();
		result.item_max = ImGui::GetItemRectMax();

		if (desc.drop_target && desc.drop_target->payload_type && ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				desc.drop_target->payload_type,
				ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
			if (payload && is_drop_valid(*desc.drop_target, payload, desc.preview_visual))
			{
				result.drop_visual = desc.preview_visual;
				result.drop_hovered = true;
				result.drop_delivered = payload->IsDelivery();
				result.accepted_payload = payload;
				draw_drop_preview(result.drop_visual, result.item_min, result.item_max);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::PopID();
		return result;
	}

	uint64_t EditorTreeWidget::make_id_key(std::string_view unique_id) const
	{
		return static_cast<uint64_t>(std::hash<std::string_view>{}(unique_id));
	}

	EditorTreeDropVisual EditorTreeWidget::resolve_drop_visual(
		const EditorTreeDropTargetDesc& desc,
		const ImVec2& item_min,
		const ImVec2& item_max) const
	{
		const float item_height = std::max(item_max.y - item_min.y, 1.0f);
		const float zone_padding = std::clamp(item_height * m_style.drop_zone_ratio, 4.0f, 10.0f);
		const float mouse_y = ImGui::GetMousePos().y;
		if (desc.allow_before && mouse_y <= item_min.y + zone_padding)
		{
			return EditorTreeDropVisual::Before;
		}
		if (desc.allow_after && mouse_y >= item_max.y - zone_padding)
		{
			return EditorTreeDropVisual::After;
		}
		return desc.allow_into ? EditorTreeDropVisual::Into : EditorTreeDropVisual::None;
	}

	void EditorTreeWidget::update_auto_expand_hover(uint64_t item_key)
	{
		if (m_state.hover_auto_expand_key != item_key)
		{
			m_state.hover_auto_expand_key = item_key;
			m_state.hover_auto_expand_start_time = ImGui::GetTime();
			return;
		}

		if (ImGui::GetTime() - m_state.hover_auto_expand_start_time >= m_style.auto_expand_hover_delay_seconds)
		{
			m_state.pending_auto_expand_key = item_key;
		}
	}

	void EditorTreeWidget::clear_auto_expand_hover(uint64_t item_key)
	{
		if (m_state.hover_auto_expand_key == item_key)
		{
			m_state.hover_auto_expand_key = 0;
			m_state.hover_auto_expand_start_time = 0.0;
		}
	}

	void EditorTreeWidget::draw_row_background(const EditorTreeItemDesc& desc, const EditorTreeItemResult& result) const
	{
		if (!desc.selected && !result.hovered)
		{
			return;
		}

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		if (!draw_list)
		{
			return;
		}

		const ImVec2 fill_min(result.item_min.x + 1.0f, result.item_min.y + 1.0f);
		const ImVec2 fill_max(result.item_max.x - 1.0f, result.item_max.y - 1.0f);
		if (desc.selected)
		{
			draw_list->AddRectFilled(
				fill_min,
				fill_max,
				to_im_color(m_style.row_selected_fill_color),
				4.0f);
			draw_list->AddRect(
				fill_min,
				fill_max,
				to_im_color(m_style.row_selected_outline_color),
				4.0f,
				0,
				1.0f);
			return;
		}

		draw_list->AddRectFilled(
			fill_min,
			fill_max,
			to_im_color(m_style.row_hover_fill_color),
			4.0f);
		draw_list->AddRect(
			fill_min,
			fill_max,
			to_im_color(m_style.row_hover_outline_color),
			4.0f,
			0,
			1.0f);
	}

	void EditorTreeWidget::draw_drop_preview(EditorTreeDropVisual visual, const ImVec2& item_min, const ImVec2& item_max) const
	{
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		if (!draw_list)
		{
			return;
		}

		const ImU32 accent = to_im_color(m_style.drop_accent_color);
		switch (visual)
		{
		case EditorTreeDropVisual::Before:
			draw_list->AddLine(ImVec2(item_min.x, item_min.y), ImVec2(item_max.x, item_min.y), accent, 2.0f);
			break;
		case EditorTreeDropVisual::After:
			draw_list->AddLine(ImVec2(item_min.x, item_max.y), ImVec2(item_max.x, item_max.y), accent, 2.0f);
			break;
		case EditorTreeDropVisual::Into:
			draw_list->AddRect(item_min, item_max, accent, 3.0f, 0, 2.0f);
			break;
		default:
			break;
		}
	}

	void EditorTreeWidget::draw_guides(const EditorTreeItemDesc& desc, const EditorTreeItemResult& result, float row_start_x) const
	{
		if (m_ancestorHasMoreSiblings.empty() && desc.is_last_sibling)
		{
			return;
		}

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		if (!draw_list)
		{
			return;
		}

		const ImU32 color = to_im_color(m_style.guide_line_color);
		const float y0 = result.item_min.y + m_style.guide_line_padding_y;
		const float y1 = result.item_max.y - m_style.guide_line_padding_y;
		const float center_y = (result.item_min.y + result.item_max.y) * 0.5f;

		for (size_t index = 0; index < m_ancestorHasMoreSiblings.size(); ++index)
		{
			if (!m_ancestorHasMoreSiblings[index])
			{
				continue;
			}

			const float x = m_treeStartX + (static_cast<float>(index) + 0.5f) * m_style.indent_spacing;
			draw_list->AddLine(ImVec2(x, y0), ImVec2(x, y1), color, m_style.guide_line_thickness);
		}

		const size_t depth = m_ancestorHasMoreSiblings.size();
		if (depth == 0)
		{
			return;
		}

		const float connector_x = m_treeStartX + (static_cast<float>(depth) - 0.5f) * m_style.indent_spacing;
		draw_list->AddLine(
			ImVec2(connector_x, y0),
			ImVec2(connector_x, desc.is_last_sibling ? center_y : y1),
			color,
			m_style.guide_line_thickness);

		const float horizontal_end_x = row_start_x - m_style.connector_horizontal_padding;
		if (horizontal_end_x > connector_x)
		{
			draw_list->AddLine(
				ImVec2(connector_x, center_y),
				ImVec2(horizontal_end_x, center_y),
				color,
				m_style.guide_line_thickness);
		}
	}

	void EditorTreeWidget::draw_item_content(const EditorTreeItemDesc& desc, const EditorTreeItemResult& result, float row_start_x) const
	{
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		if (!draw_list)
		{
			return;
		}

		const float label_start_x = row_start_x + ImGui::GetTreeNodeToLabelSpacing();
		const float center_y = (result.item_min.y + result.item_max.y) * 0.5f;
		float text_x = label_start_x;
		AshEngine::UITextureHandle icon = result.opened && desc.icon_when_open != nullptr ? desc.icon_when_open : desc.icon;
		if (icon != nullptr)
		{
			const float icon_y = center_y - m_style.icon_size * 0.5f;
			draw_list->AddImage(
				icon,
				ImVec2(label_start_x, icon_y),
				ImVec2(label_start_x + m_style.icon_size, icon_y + m_style.icon_size));
			text_x += m_style.icon_size + m_style.icon_text_spacing;
		}

		const ImVec2 text_size = ImGui::CalcTextSize(desc.label.data(), desc.label.data() + desc.label.size(), false);
		const float text_y = center_y - text_size.y * 0.5f;
		draw_list->AddText(
			ImVec2(text_x, text_y),
			ImGui::GetColorU32(ImGuiCol_Text),
			desc.label.data(),
			desc.label.data() + desc.label.size());
	}
}
