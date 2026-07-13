#include "Function/Gui/UINodeEditor.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <vector>

namespace ed = ax::NodeEditor;

namespace AshEngine
{
	// editor begin 修改原因：将 imgui-node-editor 适配为 AshEngine::UINodeEditor，保持 Editor 侧 UIContext 边界。
	namespace
	{
		ImVec2 to_im_vec2(const UIVec2& v)
		{
			return ImVec2(v.x, v.y);
		}

		UIVec2 to_ui_vec2(const ImVec2& v)
		{
			return UIVec2{ v.x, v.y };
		}

		ImVec4 to_im_vec4(const UIColor& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}

		ed::PinKind to_ed_pin_kind(UINodePinKind kind)
		{
			return kind == UINodePinKind::Output ? ed::PinKind::Output : ed::PinKind::Input;
		}
	}

	class UINodeEditor::Impl
	{
	public:
		Impl()
		{
			ed::Config config;
			// Disable on-disk persistence for now; the editor owns node state itself.
			config.SettingsFile = nullptr;
			context = ed::CreateEditor(&config);
		}

		~Impl()
		{
			if (context)
			{
				ed::DestroyEditor(context);
				context = nullptr;
			}
		}

		ed::EditorContext* context = nullptr;
	};

	UINodeEditor::UINodeEditor()
		: m_impl(new Impl())
	{
	}

	UINodeEditor::~UINodeEditor()
	{
		if (m_impl && m_impl->context && ed::GetCurrentEditor() == m_impl->context)
		{
			ed::SetCurrentEditor(nullptr);
		}
		delete m_impl;
		m_impl = nullptr;
	}

	bool UINodeEditor::begin(const char* str_id, const UIVec2& size)
	{
		if (!m_impl || !m_impl->context || !str_id)
		{
			return false;
		}
		ed::SetCurrentEditor(m_impl->context);
		ed::Begin(str_id, to_im_vec2(size));
		return true;
	}

	void UINodeEditor::end()
	{
		if (!m_impl || !m_impl->context)
		{
			return;
		}
		ed::End();
		ed::SetCurrentEditor(nullptr);
	}

	void UINodeEditor::begin_node(UINodeId id)
	{
		ed::BeginNode(ed::NodeId(static_cast<uintptr_t>(id)));
	}

	void UINodeEditor::end_node()
	{
		ed::EndNode();
	}

	void UINodeEditor::begin_pin(UIPinId id, UINodePinKind kind)
	{
		ed::BeginPin(ed::PinId(static_cast<uintptr_t>(id)), to_ed_pin_kind(kind));
	}

	void UINodeEditor::draw_pin_marker(UINodePinShape shape, const UIColor& color, float size)
	{
		if (size <= 0.0f)
		{
			return;
		}

		const ImVec2 marker_size(size, size);
		ImGui::Dummy(marker_size);

		const ImVec2 marker_min = ImGui::GetItemRectMin();
		const ImVec2 marker_max = ImGui::GetItemRectMax();
		const ImVec2 marker_center(
			(marker_min.x + marker_max.x) * 0.5f,
			(marker_min.y + marker_max.y) * 0.5f);
		const ImU32 marker_color = ImGui::ColorConvertFloat4ToU32(to_im_vec4(color));

		ImDrawList* pDrawList = ImGui::GetWindowDrawList();
		if (pDrawList)
		{
			if (shape == UINodePinShape::Square)
			{
				pDrawList->AddRectFilled(marker_min, marker_max, marker_color, 2.0f);
			}
			else
			{
				pDrawList->AddCircleFilled(marker_center, size * 0.5f, marker_color, 16);
			}
		}

		ed::PinRect(marker_min, marker_max);
		ed::PinPivotRect(marker_min, marker_max);
	}

	void UINodeEditor::end_pin()
	{
		ed::EndPin();
	}

	void UINodeEditor::link(UILinkId id, UIPinId start_pin, UIPinId end_pin, const UIColor& color, float thickness)
	{
		ed::Link(
			ed::LinkId(static_cast<uintptr_t>(id)),
			ed::PinId(static_cast<uintptr_t>(start_pin)),
			ed::PinId(static_cast<uintptr_t>(end_pin)),
			to_im_vec4(color),
			thickness);
	}

	bool UINodeEditor::begin_create()
	{
		const bool bHasCreateInteraction = ed::BeginCreate();
		if (!bHasCreateInteraction)
		{
			ed::EndCreate();
		}
		return bHasCreateInteraction;
	}

	bool UINodeEditor::query_new_link(UIPinId* out_start_pin, UIPinId* out_end_pin)
	{
		ed::PinId start_pin = 0;
		ed::PinId end_pin = 0;
		if (!ed::QueryNewLink(&start_pin, &end_pin))
		{
			return false;
		}
		if (out_start_pin)
		{
			*out_start_pin = static_cast<UIPinId>(start_pin.Get());
		}
		if (out_end_pin)
		{
			*out_end_pin = static_cast<UIPinId>(end_pin.Get());
		}
		return true;
	}

	bool UINodeEditor::query_new_node(UIPinId* out_pin)
	{
		ed::PinId pin = 0;
		if (!ed::QueryNewNode(&pin))
		{
			return false;
		}
		if (out_pin)
		{
			*out_pin = static_cast<UIPinId>(pin.Get());
		}
		return true;
	}

	bool UINodeEditor::accept_new_item()
	{
		return ed::AcceptNewItem();
	}

	void UINodeEditor::reject_new_item()
	{
		ed::RejectNewItem();
	}

	void UINodeEditor::end_create()
	{
		ed::EndCreate();
	}

	bool UINodeEditor::begin_delete()
	{
		return ed::BeginDelete();
	}

	bool UINodeEditor::query_deleted_link(UILinkId* out_link, UIPinId* out_start_pin, UIPinId* out_end_pin)
	{
		ed::LinkId link = 0;
		ed::PinId start_pin = 0;
		ed::PinId end_pin = 0;
		if (!ed::QueryDeletedLink(&link, &start_pin, &end_pin))
		{
			return false;
		}
		if (out_link)
		{
			*out_link = static_cast<UILinkId>(link.Get());
		}
		if (out_start_pin)
		{
			*out_start_pin = static_cast<UIPinId>(start_pin.Get());
		}
		if (out_end_pin)
		{
			*out_end_pin = static_cast<UIPinId>(end_pin.Get());
		}
		return true;
	}

	bool UINodeEditor::query_deleted_node(UINodeId* out_node)
	{
		ed::NodeId node = 0;
		if (!ed::QueryDeletedNode(&node))
		{
			return false;
		}
		if (out_node)
		{
			*out_node = static_cast<UINodeId>(node.Get());
		}
		return true;
	}

	bool UINodeEditor::accept_deleted_item(bool delete_dependencies)
	{
		return ed::AcceptDeletedItem(delete_dependencies);
	}

	void UINodeEditor::reject_deleted_item()
	{
		ed::RejectDeletedItem();
	}

	void UINodeEditor::end_delete()
	{
		ed::EndDelete();
	}

	void UINodeEditor::set_node_position(UINodeId id, const UIVec2& position)
	{
		ed::SetNodePosition(ed::NodeId(static_cast<uintptr_t>(id)), to_im_vec2(position));
	}

	UIVec2 UINodeEditor::get_node_position(UINodeId id)
	{
		return to_ui_vec2(ed::GetNodePosition(ed::NodeId(static_cast<uintptr_t>(id))));
	}

	void UINodeEditor::navigate_to_content(float duration)
	{
		ed::NavigateToContent(duration);
	}

	UIVec2 UINodeEditor::screen_to_canvas(const UIVec2& screen_position)
	{
		return to_ui_vec2(ed::ScreenToCanvas(to_im_vec2(screen_position)));
	}

	bool UINodeEditor::is_node_selected(UINodeId id)
	{
		return ed::IsNodeSelected(ed::NodeId(static_cast<uintptr_t>(id)));
	}

	bool UINodeEditor::is_link_selected(UILinkId id)
	{
		return ed::IsLinkSelected(ed::LinkId(static_cast<uintptr_t>(id)));
	}

	bool UINodeEditor::has_selection_changed()
	{
		return ed::HasSelectionChanged();
	}

	int32_t UINodeEditor::get_selected_object_count()
	{
		return ed::GetSelectedObjectCount();
	}

	int32_t UINodeEditor::get_selected_nodes(UINodeId* out_nodes, int32_t max_count)
	{
		if (!out_nodes || max_count <= 0)
		{
			return 0;
		}

		std::vector<ed::NodeId> vecNodes(static_cast<size_t>(max_count));
		const int32_t count = ed::GetSelectedNodes(vecNodes.data(), max_count);
		for (int32_t iIndex = 0; iIndex < count; ++iIndex)
		{
			out_nodes[iIndex] = static_cast<UINodeId>(vecNodes[static_cast<size_t>(iIndex)].Get());
		}
		return count;
	}

	int32_t UINodeEditor::get_selected_links(UILinkId* out_links, int32_t max_count)
	{
		if (!out_links || max_count <= 0)
		{
			return 0;
		}

		std::vector<ed::LinkId> vecLinks(static_cast<size_t>(max_count));
		const int32_t count = ed::GetSelectedLinks(vecLinks.data(), max_count);
		for (int32_t iIndex = 0; iIndex < count; ++iIndex)
		{
			out_links[iIndex] = static_cast<UILinkId>(vecLinks[static_cast<size_t>(iIndex)].Get());
		}
		return count;
	}

	bool UINodeEditor::show_node_context_menu(UINodeId* out_node)
	{
		ed::NodeId node = 0;
		if (!ed::ShowNodeContextMenu(&node))
		{
			return false;
		}
		if (out_node)
		{
			*out_node = static_cast<UINodeId>(node.Get());
		}
		return true;
	}

	bool UINodeEditor::show_link_context_menu(UILinkId* out_link)
	{
		ed::LinkId link = 0;
		if (!ed::ShowLinkContextMenu(&link))
		{
			return false;
		}
		if (out_link)
		{
			*out_link = static_cast<UILinkId>(link.Get());
		}
		return true;
	}

	bool UINodeEditor::show_background_context_menu()
	{
		return ed::ShowBackgroundContextMenu();
	}

	void UINodeEditor::suspend()
	{
		ed::Suspend();
	}

	void UINodeEditor::resume()
	{
		ed::Resume();
	}
	// editor end
}
