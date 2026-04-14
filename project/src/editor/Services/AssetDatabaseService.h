#pragma once
#include "Function/Asset/AssetDatabase.h"
#include <filesystem>
#include <string>

namespace AshEditor
{
	class AssetDatabaseService
	{
	public:
		void set_asset_root(std::filesystem::path asset_root);
		bool refresh();

		const std::filesystem::path& get_asset_root() const;
		const std::vector<AshEngine::AssetInfo>& get_items() const;
		const AshEngine::AssetInfo* find_by_id(uint64_t id) const;
		AshEngine::AssetLoadState get_load_state(uint64_t id) const;
		std::string get_last_error() const;
		std::string get_asset_last_error(uint64_t id) const;

		static const char* get_type_label(AshEngine::AssetType type);
		static const char* get_load_state_label(AshEngine::AssetLoadState state);

	private:
		std::filesystem::path m_assetRoot{};
		AshEngine::AssetDatabase m_database{};
	};
}
