#include "Panels/AssetBrowserPanel.h"
#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Services/AssetDatabaseService.h"
#include "Services/CommandService.h"
#include "Services/EditorSettingsService.h"
#include "Services/SelectionService.h"
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
			std::string label{};
			uint32_t child_count = 0;
		};

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

		auto is_asset_in_directory(const AshEngine::AssetInfo& item, const std::filesystem::path& directory_path) -> bool
		{
			return directory_path.empty() ? item.parent_path.empty() : item.parent_path == directory_path;
		}

		auto build_directory_entries(const std::vector<AshEngine::AssetInfo>& items) -> std::vector<AssetDirectoryEntry>
		{
			std::vector<AssetDirectoryEntry> entries{};
			entries.push_back({ {}, "All Assets", static_cast<uint32_t>(items.size()) });
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

				entries.push_back({ item.relative_path, item.name.empty() ? item.relative_path.generic_string() : item.name, child_count });
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
				is_asset_in_directory(*selected_asset, active_directory) &&
				asset_matches_filter(*selected_asset, search_text, type_filter);
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
		const AssetTypeFilterOption& type_filter = k_assetTypeFilters[std::clamp(m_typeFilterIndex, 0, static_cast<int32_t>(k_assetTypeFilters.size() - 1))];
		const std::vector<AssetDirectoryEntry> directories = build_directory_entries(items);
		const std::filesystem::path active_directory = m_activeDirectoryPath.empty() ? std::filesystem::path{} : std::filesystem::path(m_activeDirectoryPath);
		const bool active_directory_exists = directory_exists(directories, active_directory);
		const AshEngine::AssetInfo* selected_asset = get_selected_asset(*context.asset_database_service, m_selectedAssetId);
		if (!selected_asset && context.selection_service && context.selection_service->get_selection().kind == EditorSelectionKind::Asset)
		{
			selected_asset = context.asset_database_service->find_by_id(context.selection_service->get_selection().id);
			m_selectedAssetId = selected_asset ? selected_asset->id : 0;
		}

		const std::vector<const char*> type_labels{
			"All", "Folder", "Scene", "Shader", "Texture", "Mesh", "Model", "Prefab", "Material", "Text", "Binary"
		};
		uint32_t filtered_count = 0;
		for (const AshEngine::AssetInfo& item : items)
		{
			if (is_asset_in_directory(item, active_directory) && asset_matches_filter(item, m_searchText, type_filter))
			{
				++filtered_count;
			}
		}

		bool selected_asset_visible = is_selected_asset_visible(
			selected_asset,
			active_directory_exists,
			active_directory,
			m_searchText,
			type_filter);

		ui.text("Root: %s", context.asset_database_service->get_asset_root().string().c_str());
		ui.text("Items: %u / %u", filtered_count, static_cast<uint32_t>(items.size()));
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
			for (const AssetDirectoryEntry& entry : directories)
			{
				const bool selected_directory = entry.relative_path == active_directory;
				const std::string label = entry.label + " (" + std::to_string(entry.child_count) + ")";
				if (ui.selectable(label.c_str(), selected_directory))
				{
					m_activeDirectoryPath = entry.relative_path.generic_string();
				}
			}
		}
		ui.end_child();
		ui.same_line();

		if (ui.begin_child("AssetBrowserContent", {}, AshEngine::UIChildFlagBits::Border))
		{
			ui.text("Location: %s", m_activeDirectoryPath.empty() ? "All Assets" : m_activeDirectoryPath.c_str());
			ui.text("Visible Items: %u / %u", filtered_count, static_cast<uint32_t>(items.size()));
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
				ui.text("Directory: %s", m_activeDirectoryPath.empty() ? "All Assets" : m_activeDirectoryPath.c_str());
				ui.begin_disabled(!filters_active);
				if (ui.button("Clear Search And Filters"))
				{
					reset_filters();
				}
				ui.end_disabled();
			}

			if (active_directory_exists && filtered_count > 0 && ui.begin_table(
				"AssetBrowserTable",
				3,
				AshEngine::UITableFlagBits::RowBg |
					AshEngine::UITableFlagBits::BordersInner |
					AshEngine::UITableFlagBits::Resizable |
					AshEngine::UITableFlagBits::SizingStretchProp |
					AshEngine::UITableFlagBits::ScrollY,
				m_showDetails ? AshEngine::UIVec2{ 0.0f, region.y * 0.55f } : AshEngine::UIVec2{}))
			{
				ui.table_setup_column("Name", AshEngine::UITableColumnFlagBits::WidthStretch);
				ui.table_setup_column("Type", AshEngine::UITableColumnFlagBits::WidthFixed, 110.0f);
				ui.table_setup_column("State", AshEngine::UITableColumnFlagBits::WidthFixed, 90.0f);
				ui.table_headers_row();
				for (const AshEngine::AssetInfo& item : items)
				{
					if (!is_asset_in_directory(item, active_directory) || !asset_matches_filter(item, m_searchText, type_filter))
					{
						continue;
					}

					const bool selected = selected_asset && selected_asset->id == item.id;
					const std::string display_label = get_asset_display_label(item);

					ui.table_next_row();
					ui.table_next_column();
					if (ui.selectable(display_label.c_str(), selected, AshEngine::UISelectableFlagBits::SpanAllColumns))
					{
						m_selectedAssetId = item.id;
						if (context.selection_service)
						{
							context.selection_service->select({ EditorSelectionKind::Asset, item.id, display_label, item.relative_path.generic_string() });
						}
					}
					ui.table_next_column();
					ui.text_unformatted(AssetDatabaseService::get_type_label(item.type));
					ui.table_next_column();
					ui.text_unformatted(AssetDatabaseService::get_load_state_label(context.asset_database_service->get_load_state(item.id)));
				}

				ui.end_table();
			}

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
					if (context.selection_service)
					{
						context.selection_service->select({
							EditorSelectionKind::Asset,
							selected_asset->id,
							display_label,
							selected_asset->relative_path.generic_string() });
					}
				}
				ui.end_disabled();

				if (selected_asset->is_directory)
				{
					ui.same_line();
					if (ui.button("Open Folder"))
					{
						m_activeDirectoryPath = selected_asset->relative_path.generic_string();
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
