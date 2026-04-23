#include "Panels/AssetBrowserPanel.h"
#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Services/AssetDatabaseService.h"
#include "Services/CommandService.h"
#include "Services/EditorIconService.h"
#include "Services/EditorSettingsService.h"
#include "Services/SelectionService.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <vector>

namespace AshEditor
{
	namespace
	{
		struct AssetTypeFilterOption
		{
			const char* label = "";
			AshEngine::AssetType type = AshEngine::AssetType::Unknown;
			bool match_all = false;
		};

		constexpr std::array<AssetTypeFilterOption, 11> k_assetTypeFilters{ {
			{ "All", AshEngine::AssetType::Unknown, true },
			{ "Folder", AshEngine::AssetType::Directory, false },
			{ "Scene", AshEngine::AssetType::Scene, false },
			{ "Shader", AshEngine::AssetType::Shader, false },
			{ "Texture", AshEngine::AssetType::Texture, false },
			{ "Mesh", AshEngine::AssetType::Mesh, false },
			{ "Model", AshEngine::AssetType::Model, false },
			{ "Prefab", AshEngine::AssetType::Prefab, false },
			{ "Material", AshEngine::AssetType::Material, false },
			{ "Text", AshEngine::AssetType::Text, false },
			{ "Binary", AshEngine::AssetType::Binary, false },
		} };

		struct AssetDirectoryEntry
		{
			std::filesystem::path relative_path{};
			std::filesystem::path parent_path{};
			std::string label{};
			uint32_t child_count = 0;
		};

		constexpr float k_assetBrowserIconSize = 16.0f;
		constexpr float k_assetBrowserGridIconSize = 36.0f;
		constexpr float k_assetBrowserGridCellWidth = 112.0f;
		constexpr float k_assetBrowserGridCellHeight = 96.0f;
		constexpr float k_assetBrowserGridSpacing = 10.0f;
		constexpr float k_assetBrowserListIconTextSpacing = 6.0f;
		constexpr const char* k_assetItemContextPopupId = "AssetBrowserItemContextMenu";
		constexpr const char* k_assetContentContextPopupId = "AssetBrowserContentContextMenu";
		constexpr AshEngine::UIColor k_assetItemSelectedFillColor{ 0.32f, 0.47f, 0.60f, 0.30f };
		constexpr AshEngine::UIColor k_assetItemHoverFillColor{ 0.28f, 0.39f, 0.49f, 0.18f };
		constexpr AshEngine::UIColor k_assetItemHoverOutlineColor{ 0.38f, 0.56f, 0.74f, 0.40f };
		constexpr AshEngine::UIColor k_assetItemSelectedOutlineColor{ 0.43f, 0.64f, 0.85f, 0.92f };
		constexpr AshEngine::UIColor k_assetToolbarSelectedButtonColor{ 0.34f, 0.42f, 0.50f, 1.0f };
		constexpr AshEngine::UIColor k_assetToolbarSelectedButtonHoveredColor{ 0.38f, 0.46f, 0.55f, 1.0f };
		constexpr AshEngine::UIColor k_assetToolbarSelectedButtonActiveColor{ 0.31f, 0.39f, 0.47f, 1.0f };
		constexpr AshEngine::UIColor k_assetToolbarMutedTextColor{ 0.67f, 0.70f, 0.76f, 1.0f };

		auto get_action_shortcut(const CommandService* command_service, const char* action_id) -> const char*
		{
			if (!command_service || !action_id)
			{
				return nullptr;
			}

			const EditorAction* action = command_service->find_action(action_id);
			return action && !action->shortcut.empty() ? action->shortcut.c_str() : nullptr;
		}

		void push_selected_toolbar_button_style(AshEngine::UIContext& ui)
		{
			ui.push_style_color(AshEngine::UIStyleColorKind::Button, k_assetToolbarSelectedButtonColor);
			ui.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, k_assetToolbarSelectedButtonHoveredColor);
			ui.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, k_assetToolbarSelectedButtonActiveColor);
		}

		void pop_selected_toolbar_button_style(AshEngine::UIContext& ui)
		{
			ui.pop_style_color(3);
		}

		void draw_list_item_icon(AshEngine::UITextureHandle icon_handle)
		{
			if (!icon_handle)
			{
				return;
			}

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const ImVec2 item_min = ImGui::GetItemRectMin();
			const ImVec2 item_max = ImGui::GetItemRectMax();
			const ImGuiStyle& style = ImGui::GetStyle();
			const float x = item_min.x + style.FramePadding.x + 2.0f;
			const float y = item_min.y + (item_max.y - item_min.y - k_assetBrowserIconSize) * 0.5f;
			draw_list->AddImage(
				icon_handle,
				ImVec2(x, y),
				ImVec2(x + k_assetBrowserIconSize, y + k_assetBrowserIconSize));
		}

		void draw_list_item_label(AshEngine::UITextureHandle icon_handle, std::string_view label)
		{
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			if (!draw_list)
			{
				return;
			}

			const ImVec2 item_min = ImGui::GetItemRectMin();
			const ImVec2 item_max = ImGui::GetItemRectMax();
			const ImGuiStyle& style = ImGui::GetStyle();
			float text_x = item_min.x + style.FramePadding.x;
			if (icon_handle)
			{
				text_x += k_assetBrowserIconSize + k_assetBrowserListIconTextSpacing;
			}

			const ImVec2 text_size = ImGui::CalcTextSize(label.data(), label.data() + label.size(), false);
			const float text_y = item_min.y + (item_max.y - item_min.y - text_size.y) * 0.5f;
			const ImVec2 clip_min(text_x, item_min.y);
			const ImVec2 clip_max(item_max.x, item_max.y);
			draw_list->PushClipRect(clip_min, clip_max, true);
			draw_list->AddText(
				ImVec2(text_x, text_y),
				ImGui::GetColorU32(ImGuiCol_Text),
				label.data(),
				label.data() + label.size());
			draw_list->PopClipRect();
		}

		void draw_item_feedback(bool selected, float rounding = 4.0f)
		{
			if (!selected && !ImGui::IsItemHovered())
			{
				return;
			}

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const ImVec2 item_min = ImGui::GetItemRectMin();
			const ImVec2 item_max = ImGui::GetItemRectMax();
			const ImU32 fill_color = ImGui::GetColorU32(
				ImVec4(
					selected ? k_assetItemSelectedFillColor.r : k_assetItemHoverFillColor.r,
					selected ? k_assetItemSelectedFillColor.g : k_assetItemHoverFillColor.g,
					selected ? k_assetItemSelectedFillColor.b : k_assetItemHoverFillColor.b,
					selected ? k_assetItemSelectedFillColor.a : k_assetItemHoverFillColor.a));
			draw_list->AddRectFilled(item_min, item_max, fill_color, rounding);

			const ImU32 outline_color = selected
				? ImGui::GetColorU32(ImVec4(
					k_assetItemSelectedOutlineColor.r,
					k_assetItemSelectedOutlineColor.g,
					k_assetItemSelectedOutlineColor.b,
					k_assetItemSelectedOutlineColor.a))
				: ImGui::GetColorU32(ImVec4(
					k_assetItemHoverOutlineColor.r,
					k_assetItemHoverOutlineColor.g,
					k_assetItemHoverOutlineColor.b,
					k_assetItemHoverOutlineColor.a));
			draw_list->AddRect(
				item_min,
				item_max,
				outline_color,
				rounding,
				0,
				1.0f);
		}

		auto get_asset_icon_id(const AshEngine::AssetInfo& item) -> EditorIconId
		{
			return item.is_directory ? EditorIconId::FolderClosed : EditorIconId::File;
		}

		auto to_lower_copy(std::string value) -> std::string
		{
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		auto asset_matches_filter(const AshEngine::AssetInfo& item, const std::string& search_text, const AssetTypeFilterOption& type_filter) -> bool
		{
			if (!type_filter.match_all && item.type != type_filter.type)
			{
				return false;
			}

			if (search_text.empty())
			{
				return true;
			}

			const std::string lowered_search = to_lower_copy(search_text);
			const std::string lowered_name = to_lower_copy(item.name);
			const std::string lowered_path = to_lower_copy(item.relative_path.generic_string());
			return lowered_name.find(lowered_search) != std::string::npos || lowered_path.find(lowered_search) != std::string::npos;
		}

		auto is_same_or_ancestor_path(
			const std::filesystem::path& ancestor_path,
			const std::filesystem::path& descendant_path) -> bool
		{
			if (ancestor_path.empty())
			{
				return true;
			}

			auto ancestor_it = ancestor_path.begin();
			auto descendant_it = descendant_path.begin();
			for (; ancestor_it != ancestor_path.end(); ++ancestor_it, ++descendant_it)
			{
				if (descendant_it == descendant_path.end() || *ancestor_it != *descendant_it)
				{
					return false;
				}
			}

			return true;
		}

		auto is_asset_in_directory(const AshEngine::AssetInfo& item, const std::filesystem::path& directory_path, bool include_descendants = false) -> bool
		{
			if (include_descendants)
			{
				return item.is_directory
					? is_same_or_ancestor_path(directory_path, item.relative_path)
					: is_same_or_ancestor_path(directory_path, item.parent_path);
			}

			return item.parent_path == directory_path;
		}

		auto build_directory_entries(const std::vector<AshEngine::AssetInfo>& items) -> std::vector<AssetDirectoryEntry>
		{
			std::vector<AssetDirectoryEntry> entries{};
			entries.push_back({ {}, {}, "All Assets", static_cast<uint32_t>(items.size()) });
			for (const AshEngine::AssetInfo& item : items)
			{
				if (!item.is_directory)
				{
					continue;
				}

				uint32_t child_count = 0;
				for (const AshEngine::AssetInfo& child : items)
				{
					if (child.parent_path == item.relative_path)
					{
						++child_count;
					}
				}

				entries.push_back({
					item.relative_path,
					item.parent_path,
					item.name.empty() ? item.relative_path.generic_string() : item.name,
					child_count });
			}

			std::sort(
				entries.begin() + 1,
				entries.end(),
				[](const AssetDirectoryEntry& lhs, const AssetDirectoryEntry& rhs) {
					return lhs.relative_path.generic_string() < rhs.relative_path.generic_string();
				});
			return entries;
		}

		auto get_selected_asset(const AssetDatabaseService& service, uint64_t selected_id) -> const AshEngine::AssetInfo*
		{
			return selected_id == 0 ? nullptr : service.find_by_id(selected_id);
		}

		auto should_preview_text(const AshEngine::AssetInfo& item) -> bool
		{
			switch (item.type)
			{
			case AshEngine::AssetType::Scene:
			case AshEngine::AssetType::Shader:
			case AshEngine::AssetType::Material:
			case AshEngine::AssetType::Text:
				return true;
			default:
				return false;
			}
		}

		auto get_asset_display_label(const AshEngine::AssetInfo& item) -> std::string
		{
			if (!item.name.empty())
			{
				return item.name;
			}

			const std::string filename = item.relative_path.filename().generic_string();
			return filename.empty() ? item.relative_path.generic_string() : filename;
		}

		auto directory_exists(const std::vector<AssetDirectoryEntry>& directories, const std::filesystem::path& directory_path) -> bool
		{
			if (directory_path.empty())
			{
				return true;
			}

			for (const AssetDirectoryEntry& entry : directories)
			{
				if (entry.relative_path == directory_path)
				{
					return true;
				}
			}

			return false;
		}

		auto should_include_asset_in_view(
			const AshEngine::AssetInfo& item,
			const std::filesystem::path& active_directory,
			bool search_active,
			const std::string& search_text,
			const AssetTypeFilterOption& type_filter) -> bool
		{
			if (!asset_matches_filter(item, search_text, type_filter))
			{
				return false;
			}

			return is_asset_in_directory(item, active_directory, search_active);
		}

		void sort_visible_assets(std::vector<const AshEngine::AssetInfo*>& visible_items)
		{
			std::stable_sort(
				visible_items.begin(),
				visible_items.end(),
				[](const AshEngine::AssetInfo* lhs, const AshEngine::AssetInfo* rhs)
				{
					if (lhs == rhs)
					{
						return false;
					}
					if (!lhs)
					{
						return false;
					}
					if (!rhs)
					{
						return true;
					}
					if (lhs->is_directory != rhs->is_directory)
					{
						return lhs->is_directory && !rhs->is_directory;
					}

					const std::string lhs_label = to_lower_copy(get_asset_display_label(*lhs));
					const std::string rhs_label = to_lower_copy(get_asset_display_label(*rhs));
					if (lhs_label != rhs_label)
					{
						return lhs_label < rhs_label;
					}

					return lhs->relative_path.generic_string() < rhs->relative_path.generic_string();
				});
		}

		auto get_asset_scope_label(const std::filesystem::path& directory_path) -> std::string
		{
			return directory_path.empty() ? std::string("All Assets") : directory_path.generic_string();
		}

		void clear_if_asset_selected(EditorContext& context)
		{
			if (context.selection_service && context.selection_service->get_selection().kind == EditorSelectionKind::Asset)
			{
				context.selection_service->clear();
			}
		}

		auto collect_child_directories(
			const std::vector<AssetDirectoryEntry>& directories,
			const std::filesystem::path& parent_path) -> std::vector<const AssetDirectoryEntry*>
		{
			std::vector<const AssetDirectoryEntry*> children{};
			for (const AssetDirectoryEntry& entry : directories)
			{
				if (!entry.relative_path.empty() && entry.parent_path == parent_path)
				{
					children.push_back(&entry);
				}
			}
			return children;
		}

		auto make_asset_browser_tree_style() -> EditorTreeWidgetStyle
		{
			EditorTreeWidgetStyle style{};
			style.row_height = 26.0f;
			style.indent_spacing = 12.0f;
			style.icon_size = k_assetBrowserIconSize;
			style.icon_text_spacing = 4.0f;
			style.row_padding_y = 6.0f;
			style.row_spacing_y = 3.0f;
			style.connector_horizontal_padding = 3.0f;
			style.guide_line_padding_y = 0.0f;
			style.guide_line_color = { 0.46f, 0.49f, 0.54f, 0.55f };
			style.row_hover_fill_color = { 0.28f, 0.39f, 0.49f, 0.16f };
			style.row_hover_outline_color = { 0.38f, 0.56f, 0.74f, 0.36f };
			style.row_selected_fill_color = { 0.32f, 0.47f, 0.60f, 0.28f };
			style.row_selected_outline_color = { 0.43f, 0.64f, 0.85f, 0.82f };
			return style;
		}

		void draw_directory_tree(
			EditorTreeWidget& tree_widget,
			AshEngine::UIContext& ui,
			EditorIconService* icon_service,
			const std::vector<AssetDirectoryEntry>& directories,
			const AssetDirectoryEntry& entry,
			const std::filesystem::path& active_directory,
			std::string& active_directory_path,
			bool is_last_sibling)
		{
			const bool selected = entry.relative_path == active_directory;
			const std::vector<const AssetDirectoryEntry*> child_directories = collect_child_directories(directories, entry.relative_path);
			const bool has_child_directories = !child_directories.empty();
			const std::string base_label = entry.label + " (" + std::to_string(entry.child_count) + ")";
			const std::string stable_id = entry.relative_path.empty()
				? std::string("__asset_browser_root__")
				: entry.relative_path.generic_string();
			EditorTreeItemDesc desc{};
			desc.unique_id = stable_id;
			desc.label = base_label;
			desc.icon = icon_service ? icon_service->get_icon(EditorIconId::FolderClosed, ui) : nullptr;
			desc.icon_when_open = icon_service ? icon_service->get_icon(EditorIconId::FolderOpen, ui) : nullptr;
			desc.selected = selected;
			desc.has_children = has_child_directories;
			desc.default_open = entry.relative_path.empty() || is_same_or_ancestor_path(entry.relative_path, active_directory);
			desc.is_last_sibling = is_last_sibling;
			const EditorTreeItemResult result = tree_widget.draw_item(desc);
			if (result.clicked)
			{
				active_directory_path = entry.relative_path.generic_string();
			}

			if (!result.opened)
			{
				return;
			}

			if (has_child_directories)
			{
				tree_widget.push_level(!is_last_sibling);
				for (size_t child_index = 0; child_index < child_directories.size(); ++child_index)
				{
					draw_directory_tree(
						tree_widget,
						ui,
						icon_service,
						directories,
						*child_directories[child_index],
						active_directory,
						active_directory_path,
						child_index + 1 == child_directories.size());
				}
				tree_widget.pop_level();
			}
			ImGui::TreePop();
		}

		auto is_selected_asset_visible(
			const AshEngine::AssetInfo* selected_asset,
			bool active_directory_exists,
			const std::filesystem::path& active_directory,
			const std::string& search_text,
			const AssetTypeFilterOption& type_filter) -> bool
		{
			return
				selected_asset &&
				active_directory_exists &&
				should_include_asset_in_view(
					*selected_asset,
					active_directory,
					!search_text.empty(),
					search_text,
					type_filter);
		}

		auto to_asset_browser_view_mode(int32_t value) -> AssetBrowserViewMode
		{
			return value == static_cast<int32_t>(AssetBrowserViewMode::Icons)
				? AssetBrowserViewMode::Icons
				: AssetBrowserViewMode::List;
		}
	}

	AssetBrowserPanel::AssetBrowserPanel()
		: EditorPanel("asset_browser", "Asset Browser")
	{
	}

	void AssetBrowserPanel::on_attach(EditorContext& context)
	{
		if (context.settings_service)
		{
			const EditorSettings& settings = context.settings_service->get_settings();
			m_searchText = settings.asset_browser_search_text;
			m_activeDirectoryPath = settings.asset_browser_active_directory;
			m_showDetails = settings.asset_browser_show_details;
			m_typeFilterIndex = settings.asset_browser_type_filter;
			m_viewMode = to_asset_browser_view_mode(settings.asset_browser_view_mode);
		}
		HLogInfo("AssetBrowserPanel attached.");
	}

	bool AssetBrowserPanel::has_active_filters() const
	{
		return !m_searchText.empty() || m_typeFilterIndex != 0 || !m_activeDirectoryPath.empty();
	}

	void AssetBrowserPanel::reset_filters()
	{
		m_searchText.clear();
		m_activeDirectoryPath.clear();
		m_typeFilterIndex = 0;
	}

	void AssetBrowserPanel::sync_settings(EditorContext& context) const
	{
		if (!context.settings_service)
		{
			return;
		}

		EditorSettings& settings = context.settings_service->get_settings();
		settings.asset_browser_search_text = m_searchText;
		settings.asset_browser_active_directory = m_activeDirectoryPath;
		settings.asset_browser_show_details = m_showDetails;
		settings.asset_browser_type_filter = m_typeFilterIndex;
		settings.asset_browser_view_mode = static_cast<int32_t>(m_viewMode);
	}

	void AssetBrowserPanel::select_asset(EditorContext& context, const AshEngine::AssetInfo& item)
	{
		m_selectedAssetId = item.id;
		if (context.selection_service)
		{
			context.selection_service->select({
				EditorSelectionKind::Asset,
				item.id,
				get_asset_display_label(item),
				item.relative_path.generic_string() });
		}
	}

	void AssetBrowserPanel::clear_asset_selection(EditorContext& context)
	{
		m_selectedAssetId = 0;
		clear_if_asset_selected(context);
	}

	void AssetBrowserPanel::activate_asset(EditorContext& context, const AshEngine::AssetInfo& item)
	{
		select_asset(context, item);
	}

	void AssetBrowserPanel::open_asset_item(EditorContext& context, const AshEngine::AssetInfo& item)
	{
		if (item.is_directory)
		{
			navigate_to_directory(item.relative_path);
			return;
		}

		activate_asset(context, item);
	}

	void AssetBrowserPanel::navigate_to_directory(const std::filesystem::path& directory_path)
	{
		m_activeDirectoryPath = directory_path.generic_string();
	}

	void AssetBrowserPanel::browse_to_asset_location(const AshEngine::AssetInfo& item)
	{
		navigate_to_directory(item.is_directory ? item.relative_path : item.parent_path);
	}

	void AssetBrowserPanel::handle_asset_item_interaction(
		EditorContext& context,
		const AshEngine::AssetInfo& item,
		bool primary_activated,
		bool double_clicked)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		if (primary_activated)
		{
			select_asset(context, item);
		}
		if (double_clicked)
		{
			open_asset_item(context, item);
		}

		if (ui.is_item_clicked(AshEngine::UIMouseButton::Right))
		{
			select_asset(context, item);
		}
	}

	void AssetBrowserPanel::draw_view_mode_toggle(AshEngine::UIContext& ui, const char* label, AssetBrowserViewMode mode)
	{
		const bool selected = m_viewMode == mode;
		if (selected)
		{
			push_selected_toolbar_button_style(ui);
		}

		if (ui.small_button(label))
		{
			m_viewMode = mode;
		}

		if (selected)
		{
			pop_selected_toolbar_button_style(ui);
		}
	}

	void AssetBrowserPanel::draw_breadcrumbs(AshEngine::UIContext& ui)
	{
		ui.text_colored(k_assetToolbarMutedTextColor, "Location");
		ui.same_line();
		const bool all_assets_selected = m_activeDirectoryPath.empty();
		if (all_assets_selected)
		{
			push_selected_toolbar_button_style(ui);
		}
		if (ui.small_button("All Assets"))
		{
			navigate_to_directory({});
		}
		if (all_assets_selected)
		{
			pop_selected_toolbar_button_style(ui);
		}

		if (m_activeDirectoryPath.empty())
		{
			return;
		}

		std::filesystem::path current_path(m_activeDirectoryPath);
		std::filesystem::path breadcrumb_path{};
		for (const std::filesystem::path& part : current_path)
		{
			breadcrumb_path /= part;
			ui.same_line();
			ui.text_unformatted("/");
			ui.same_line();
			const std::string label = part.generic_string();
			const std::string path_id = breadcrumb_path.generic_string();
			const bool is_current = path_id == m_activeDirectoryPath;
			ui.push_id(path_id.c_str());
			if (is_current)
			{
				push_selected_toolbar_button_style(ui);
			}
			if (ui.small_button(label.c_str()))
			{
				navigate_to_directory(breadcrumb_path);
			}
			if (is_current)
			{
				pop_selected_toolbar_button_style(ui);
			}
			ui.pop_id();
		}
	}

	void AssetBrowserPanel::draw_asset_list_view(
		EditorContext& context,
		const std::vector<const AshEngine::AssetInfo*>& visible_items,
		const AshEngine::AssetInfo* selected_asset)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		const AshEngine::UIVec2 available_region = ui.get_content_region_avail();
		const AshEngine::UIVec2 table_size = m_showDetails
			? AshEngine::UIVec2{ 0.0f, std::max(120.0f, available_region.y * 0.55f) }
			: AshEngine::UIVec2{};

		if (!ui.begin_table(
			"AssetBrowserTable",
			3,
			AshEngine::UITableFlagBits::RowBg |
				AshEngine::UITableFlagBits::BordersInner |
				AshEngine::UITableFlagBits::Resizable |
				AshEngine::UITableFlagBits::SizingStretchProp |
				AshEngine::UITableFlagBits::ScrollY,
			table_size))
		{
			return;
		}

		ui.table_setup_column("Name", AshEngine::UITableColumnFlagBits::WidthStretch);
		ui.table_setup_column("Type", AshEngine::UITableColumnFlagBits::WidthFixed, 110.0f);
		ui.table_setup_column("State", AshEngine::UITableColumnFlagBits::WidthFixed, 90.0f);
		ui.table_headers_row();
		for (const AshEngine::AssetInfo* item_ptr : visible_items)
		{
			const AshEngine::AssetInfo& item = *item_ptr;
			const bool selected = selected_asset && selected_asset->id == item.id;
			AshEngine::UITextureHandle icon_handle = nullptr;
			if (context.icon_service)
			{
				icon_handle = context.icon_service->get_icon(get_asset_icon_id(item), ui);
			}

			const std::string display_label = get_asset_display_label(item);

			ui.table_next_row();
			ui.table_next_column();
			const std::string item_id = std::to_string(item.id);
			ui.push_id(item_id.c_str());
			const bool primary_activated = ui.selectable(
				"##AssetListItem",
				selected,
				AshEngine::UISelectableFlagBits::SpanAllColumns);
			const bool double_clicked = ui.is_item_hovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			draw_item_feedback(selected);
			draw_list_item_icon(icon_handle);
			draw_list_item_label(icon_handle, display_label);
			handle_asset_item_interaction(context, item, primary_activated, double_clicked);
			draw_asset_item_context_menu(context, item);
			ui.pop_id();

			ui.table_next_column();
			ui.text_unformatted(AssetDatabaseService::get_type_label(item.type));
			ui.table_next_column();
			ui.text_unformatted(AssetDatabaseService::get_load_state_label(context.asset_database_service->get_load_state(item.id)));
		}

		ui.end_table();
	}

	void AssetBrowserPanel::draw_asset_icon_view(
		EditorContext& context,
		const std::vector<const AshEngine::AssetInfo*>& visible_items,
		const AshEngine::AssetInfo* selected_asset)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		const AshEngine::UIVec2 outer_region = ui.get_content_region_avail();
		const float grid_height = m_showDetails
			? std::max(140.0f, outer_region.y * 0.55f)
			: 0.0f;

		if (!ui.begin_child("AssetBrowserIconView", { 0.0f, grid_height }, AshEngine::UIChildFlagBits::Border))
		{
			ui.end_child();
			return;
		}

		const float available_width = std::max(1.0f, ui.get_content_region_avail().x);
		const float cell_span = k_assetBrowserGridCellWidth + k_assetBrowserGridSpacing;
		const int32_t column_count = std::max(1, static_cast<int32_t>((available_width + k_assetBrowserGridSpacing) / cell_span));
		int32_t column_index = 0;
		for (size_t item_index = 0; item_index < visible_items.size(); ++item_index)
		{
			const AshEngine::AssetInfo& item = *visible_items[item_index];
			const bool selected = selected_asset && selected_asset->id == item.id;
			AshEngine::UITextureHandle icon_handle = nullptr;
			if (context.icon_service)
			{
				icon_handle = context.icon_service->get_icon(get_asset_icon_id(item), ui);
			}

			const std::string display_label = get_asset_display_label(item);
			const std::string item_id = std::to_string(item.id);
			ui.push_id(item_id.c_str());
			const bool primary_activated = ui.selectable(
				"##AssetIconItem",
				selected,
				AshEngine::UISelectableFlagBits::None,
				{ k_assetBrowserGridCellWidth, k_assetBrowserGridCellHeight });
			const bool double_clicked = ui.is_item_hovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			draw_item_feedback(selected, 6.0f);

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const ImVec2 item_min = ImGui::GetItemRectMin();
			const ImVec2 item_max = ImGui::GetItemRectMax();
			const float icon_x = item_min.x + (k_assetBrowserGridCellWidth - k_assetBrowserGridIconSize) * 0.5f;
			const float icon_y = item_min.y + 10.0f;
			if (icon_handle)
			{
				draw_list->AddImage(
					icon_handle,
					ImVec2(icon_x, icon_y),
					ImVec2(icon_x + k_assetBrowserGridIconSize, icon_y + k_assetBrowserGridIconSize));
			}

			const ImVec2 text_min(item_min.x + 8.0f, icon_y + k_assetBrowserGridIconSize + 8.0f);
			const ImVec2 text_max(item_max.x - 8.0f, item_max.y - 8.0f);
			draw_list->PushClipRect(text_min, text_max, true);
			draw_list->AddText(
				nullptr,
				0.0f,
				text_min,
				ImGui::GetColorU32(ImGuiCol_Text),
				display_label.c_str(),
				nullptr,
				std::max(1.0f, text_max.x - text_min.x));
			draw_list->PopClipRect();

			handle_asset_item_interaction(context, item, primary_activated, double_clicked);
			draw_asset_item_context_menu(context, item);
			ui.pop_id();

			++column_index;
			const bool end_of_row = column_index >= column_count;
			const bool has_more_items = item_index + 1 < visible_items.size();
			if (!end_of_row && has_more_items)
			{
				ui.same_line(0.0f, k_assetBrowserGridSpacing);
			}
			else
			{
				column_index = 0;
			}
		}

		ui.end_child();
	}

	void AssetBrowserPanel::draw_asset_item_context_menu(EditorContext& context, const AshEngine::AssetInfo& item)
	{
		if (!ImGui::BeginPopupContextItem(k_assetItemContextPopupId, ImGuiPopupFlags_MouseButtonRight))
		{
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		select_asset(context, item);
		const bool can_browse = item.is_directory || !item.parent_path.empty();
		const bool can_refresh = context.command_service && context.command_service->has_action("assets.refresh");

		if (ui.menu_item("Select"))
		{
			select_asset(context, item);
			ui.close_current_popup();
		}
		if (ui.menu_item(item.is_directory ? "Open Folder" : "Open", nullptr, false, true))
		{
			open_asset_item(context, item);
			ui.close_current_popup();
		}
		if (ui.menu_item("Browse Location", nullptr, false, can_browse))
		{
			browse_to_asset_location(item);
			ui.close_current_popup();
		}
		ui.separator();
		ui.menu_item("Show Details", nullptr, &m_showDetails);
		if (ui.menu_item("Clear Selection"))
		{
			clear_asset_selection(context);
			ui.close_current_popup();
		}
		if (ui.menu_item("Refresh", get_action_shortcut(context.command_service, "assets.refresh"), false, can_refresh))
		{
			context.command_service->invoke("assets.refresh");
			ui.close_current_popup();
		}

		ImGui::EndPopup();
	}

	void AssetBrowserPanel::draw_content_context_menu(
		EditorContext& context,
		bool active_directory_exists,
		bool filters_active)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		if (!ui.begin_popup(k_assetContentContextPopupId))
		{
			return;
		}

		const bool can_go_up = active_directory_exists && !m_activeDirectoryPath.empty();
		const bool can_refresh = context.command_service && context.command_service->has_action("assets.refresh");

		if (ui.menu_item("Up", nullptr, false, can_go_up))
		{
			navigate_to_directory(std::filesystem::path(m_activeDirectoryPath).parent_path());
			ui.close_current_popup();
		}
		ui.separator();

		if (ui.menu_item("List View", nullptr, m_viewMode == AssetBrowserViewMode::List))
		{
			m_viewMode = AssetBrowserViewMode::List;
			ui.close_current_popup();
		}
		if (ui.menu_item("Icon View", nullptr, m_viewMode == AssetBrowserViewMode::Icons))
		{
			m_viewMode = AssetBrowserViewMode::Icons;
			ui.close_current_popup();
		}
		ui.menu_item("Show Details", nullptr, &m_showDetails);

		ui.separator();
		if (ui.menu_item("Reset Filters", nullptr, false, filters_active))
		{
			reset_filters();
			ui.close_current_popup();
		}
		if (ui.menu_item("Clear Selection"))
		{
			clear_asset_selection(context);
			ui.close_current_popup();
		}
		if (ui.menu_item("Refresh", get_action_shortcut(context.command_service, "assets.refresh"), false, can_refresh))
		{
			context.command_service->invoke("assets.refresh");
			ui.close_current_popup();
		}

		ui.end_popup();
	}

	void AssetBrowserPanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window(context))
		{
			end_panel_window(context);
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		if (!context.asset_database_service)
		{
			ui.text_unformatted("Asset database unavailable.");
			end_panel_window(context);
			return;
		}

		const auto& items = context.asset_database_service->get_items();
		m_typeFilterIndex = std::clamp(m_typeFilterIndex, 0, static_cast<int32_t>(k_assetTypeFilters.size() - 1));
		const AssetTypeFilterOption& type_filter = k_assetTypeFilters[m_typeFilterIndex];
		const std::vector<AssetDirectoryEntry> directories = build_directory_entries(items);
		const std::filesystem::path active_directory = m_activeDirectoryPath.empty() ? std::filesystem::path{} : std::filesystem::path(m_activeDirectoryPath);
		const bool active_directory_exists = directory_exists(directories, active_directory);
		const bool search_active = !m_searchText.empty();
		const AshEngine::AssetInfo* selected_asset = get_selected_asset(*context.asset_database_service, m_selectedAssetId);
		if (!selected_asset && context.selection_service && context.selection_service->get_selection().kind == EditorSelectionKind::Asset)
		{
			selected_asset = context.asset_database_service->find_by_id(context.selection_service->get_selection().id);
			m_selectedAssetId = selected_asset ? selected_asset->id : 0;
		}

		const std::vector<const char*> type_labels{
			"All", "Folder", "Scene", "Shader", "Texture", "Mesh", "Model", "Prefab", "Material", "Text", "Binary"
		};
		std::vector<const AshEngine::AssetInfo*> visible_items{};
		visible_items.reserve(items.size());
		for (const AshEngine::AssetInfo& item : items)
		{
			if (should_include_asset_in_view(item, active_directory, search_active, m_searchText, type_filter))
			{
				visible_items.push_back(&item);
			}
		}
		sort_visible_assets(visible_items);
		const uint32_t filtered_count = static_cast<uint32_t>(visible_items.size());

		bool selected_asset_visible = is_selected_asset_visible(
			selected_asset,
			active_directory_exists,
			active_directory,
			m_searchText,
			type_filter);

		ui.text("Root: %s", context.asset_database_service->get_asset_root().string().c_str());
		const std::string last_error = context.asset_database_service->get_last_error();
		if (!last_error.empty())
		{
			ui.text_wrapped("Last Error: %s", last_error.c_str());
		}
		ui.separator();
		ui.set_next_item_width(240.0f);
		ui.input_text("Search", m_searchText);
		ui.same_line();
		ui.set_next_item_width(140.0f);
		ui.combo("Type", m_typeFilterIndex, type_labels);
		ui.same_line();
		ui.checkbox("Details", m_showDetails);
		ui.same_line();
		ui.begin_disabled(m_activeDirectoryPath.empty());
		if (ui.small_button("Up"))
		{
			navigate_to_directory(std::filesystem::path(m_activeDirectoryPath).parent_path());
		}
		ui.end_disabled();
		ui.same_line();
		ui.text_unformatted("View");
		ui.same_line();
		draw_view_mode_toggle(ui, "List", AssetBrowserViewMode::List);
		ui.same_line();
		draw_view_mode_toggle(ui, "Icons", AssetBrowserViewMode::Icons);
		ui.same_line();
		ui.begin_disabled(!context.command_service || !context.command_service->has_action("assets.refresh"));
		if (ui.button("Refresh"))
		{
			context.command_service->invoke("assets.refresh");
		}
		ui.end_disabled();
		const bool filters_active = has_active_filters();
		ui.same_line();
		ui.begin_disabled(!filters_active);
		if (ui.button("Reset Filters"))
		{
			reset_filters();
		}
		ui.end_disabled();
		ui.separator();

		const AshEngine::UIVec2 region = ui.get_content_region_avail();
		const float left_width = std::max(180.0f, region.x * 0.28f);

		if (ui.begin_child("AssetBrowserDirectories", { left_width, 0.0f }, AshEngine::UIChildFlagBits::Border))
		{
			ui.text_unformatted("Directories");
			ui.separator();
			if (!directories.empty())
			{
				EditorTreeWidget tree_widget(ui, m_directoryTreeState, make_asset_browser_tree_style());
				tree_widget.reset_drag_state_if_inactive();
				draw_directory_tree(
					tree_widget,
					ui,
					context.icon_service,
					directories,
					directories.front(),
					active_directory,
					m_activeDirectoryPath,
					true);
			}
		}
		ui.end_child();
		ui.same_line();

		if (ui.begin_child("AssetBrowserContent", {}, AshEngine::UIChildFlagBits::Border))
		{
			draw_breadcrumbs(ui);
			ui.same_line();
			ui.text_colored(
				k_assetToolbarMutedTextColor,
				"| Showing %u of %u",
				filtered_count,
				static_cast<uint32_t>(items.size()));
			ui.separator();

			if (!active_directory_exists)
			{
				ui.text_unformatted("The saved directory is no longer available.");
				ui.text_unformatted("Reset the directory filter to return to the asset root.");
				if (ui.button("Reset Directory"))
				{
					m_activeDirectoryPath.clear();
				}
			}
			else if (items.empty())
			{
				if (!last_error.empty())
				{
					ui.text_unformatted("No assets are available because the last asset scan reported an error.");
					ui.text_unformatted("Review the error above, then refresh the asset database.");
				}
				else
				{
					ui.text_unformatted("No assets are indexed yet.");
					ui.text_unformatted("Refresh the asset database or confirm the asset root contains importable files.");
				}
			}
			else if (filtered_count == 0)
			{
				ui.text_unformatted("No assets match the current search, type filter, or directory.");
				if (!m_searchText.empty())
				{
					ui.text("Search: %s", m_searchText.c_str());
				}
				ui.text("Type Filter: %s", type_filter.label);
				const std::string scope_label = get_asset_scope_label(active_directory);
				ui.text("Directory: %s", scope_label.c_str());
				ui.begin_disabled(!filters_active);
				if (ui.button("Clear Search And Filters"))
				{
					reset_filters();
				}
				ui.end_disabled();
			}

			if (selected_asset && !selected_asset_visible)
			{
				ui.separator();
				ui.text_unformatted("The current asset selection is outside the visible browser scope.");
				if (ui.button("Reveal Selection"))
				{
					m_searchText.clear();
					m_typeFilterIndex = 0;
					browse_to_asset_location(*selected_asset);
				}
				ui.same_line();
				if (ui.button("Clear Selection"))
				{
					clear_asset_selection(context);
					selected_asset = nullptr;
				}
			}

			if (active_directory_exists && filtered_count > 0)
			{
				if (m_viewMode == AssetBrowserViewMode::Icons)
				{
					draw_asset_icon_view(context, visible_items, selected_asset);
				}
				else
				{
					draw_asset_list_view(context, visible_items, selected_asset);
				}
			}

			const bool open_content_menu =
				ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
				!ImGui::IsAnyItemHovered() &&
				!ImGui::IsAnyItemActive() &&
				ImGui::IsMouseReleased(ImGuiMouseButton_Right);
			const bool clear_content_selection =
				ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
				!ImGui::IsAnyItemHovered() &&
				!ImGui::IsAnyItemActive() &&
				ImGui::IsMouseReleased(ImGuiMouseButton_Left);
			if (open_content_menu)
			{
				ui.open_popup(k_assetContentContextPopupId);
			}
			if (clear_content_selection)
			{
				clear_asset_selection(context);
				selected_asset = nullptr;
			}

			const bool content_focused =
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				!ImGui::GetIO().WantTextInput;
			if (content_focused && selected_asset && selected_asset_visible && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
			{
				open_asset_item(context, *selected_asset);
			}
			if (content_focused && active_directory_exists && !m_activeDirectoryPath.empty() && ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
			{
				navigate_to_directory(std::filesystem::path(m_activeDirectoryPath).parent_path());
			}

			draw_content_context_menu(context, active_directory_exists, filters_active);

			selected_asset = get_selected_asset(*context.asset_database_service, m_selectedAssetId);
			selected_asset_visible = is_selected_asset_visible(
				selected_asset,
				active_directory_exists,
				active_directory,
				m_searchText,
				type_filter);
			if (m_showDetails && selected_asset_visible)
			{
				ui.separator();
				const std::string display_label = get_asset_display_label(*selected_asset);
				ui.text("Selected: %s", display_label.c_str());
				ui.text("Type: %s", AssetDatabaseService::get_type_label(selected_asset->type));
				ui.text("Path: %s", selected_asset->relative_path.generic_string().c_str());
				ui.text("Parent: %s", selected_asset->parent_path.generic_string().c_str());
				ui.text("Load State: %s", AssetDatabaseService::get_load_state_label(context.asset_database_service->get_load_state(selected_asset->id)));

				const bool can_activate = !selected_asset->is_directory;
				ui.begin_disabled(!can_activate);
				if (ui.button("Activate"))
				{
					activate_asset(context, *selected_asset);
				}
				ui.end_disabled();

				if (selected_asset->is_directory || !selected_asset->parent_path.empty())
				{
					ui.same_line();
					if (ui.button("Browse Location"))
					{
						browse_to_asset_location(*selected_asset);
					}
				}

				if (should_preview_text(*selected_asset))
				{
					std::string preview_text{};
					if (context.asset_database_service->load_text_by_id(selected_asset->id, preview_text))
					{
						if (preview_text.size() > 2048)
						{
							preview_text.resize(2048);
							preview_text += "\n...";
						}
						ui.separator();
						ui.text_unformatted("Preview");
						ui.input_text_multiline("##asset_preview", preview_text, { 0.0f, 160.0f }, AshEngine::UIInputTextFlagBits::ReadOnly);
					}
				}
			}
		}
		ui.end_child();

		sync_settings(context);
		end_panel_window(context);
	}
}
