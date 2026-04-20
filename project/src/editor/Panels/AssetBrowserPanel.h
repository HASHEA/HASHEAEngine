#pragma once
#include "Core/EditorPanel.h"
#include <cstdint>
#include <string>

namespace AshEditor
{
	class AssetBrowserPanel final : public EditorPanel
	{
	public:
		AssetBrowserPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		bool has_active_filters() const;
		void reset_filters();
		void sync_settings(EditorContext& context) const;

	private:
		std::string m_searchText{};
		std::string m_activeDirectoryPath{};
		uint64_t m_selectedAssetId = 0;
		bool m_showDetails = true;
		int32_t m_typeFilterIndex = 0;
	};
}
