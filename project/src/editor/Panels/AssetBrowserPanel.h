#pragma once
#include "Core/EditorPanel.h"
#include "Widgets/EditorTreeWidget.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AshEngine
{
	struct AssetInfo;
	class UIContext;
}

namespace AshEditor
{
	enum class AssetBrowserViewMode : uint8_t
	{
		List = 0,
		Icons
	};

	class AssetBrowserPanel final : public EditorPanel
	{
	public:
		AssetBrowserPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		void select_asset(EditorContext& context, const AshEngine::AssetInfo& item);
		void clear_asset_selection(EditorContext& context);
		void activate_asset(EditorContext& context, const AshEngine::AssetInfo& item);
		void open_asset_item(EditorContext& context, const AshEngine::AssetInfo& item);
		void navigate_to_directory(const std::filesystem::path& directory_path);
		void browse_to_asset_location(const AshEngine::AssetInfo& item);
		void handle_asset_item_interaction(EditorContext& context, const AshEngine::AssetInfo& item, bool primary_activated, bool double_clicked);
		void draw_view_mode_toggle(AshEngine::UIContext& ui, const char* label, AssetBrowserViewMode mode);
		void draw_breadcrumbs(AshEngine::UIContext& ui);
		void draw_asset_list_view(
			EditorContext& context,
			const std::vector<const AshEngine::AssetInfo*>& visible_items,
			const AshEngine::AssetInfo* selected_asset);
		void draw_asset_icon_view(
			EditorContext& context,
			const std::vector<const AshEngine::AssetInfo*>& visible_items,
			const AshEngine::AssetInfo* selected_asset);
		void draw_asset_item_context_menu(EditorContext& context, const AshEngine::AssetInfo& item);
		void draw_content_context_menu(EditorContext& context, bool active_directory_exists, bool filters_active);

		bool has_active_filters() const;
		void reset_filters();
		void sync_settings(EditorContext& context) const;

	private:
		std::string m_searchText{};
		std::string m_activeDirectoryPath{};
		uint64_t m_selectedAssetId = 0;
		bool m_showDetails = true;
		int32_t m_typeFilterIndex = 0;
		AssetBrowserViewMode m_viewMode = AssetBrowserViewMode::List;
		EditorTreeWidgetState m_directoryTreeState{};
	};
}
