#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	using AssetId = uint64_t;

	enum class AssetType : uint8_t
	{
		Unknown = 0,
		Directory,
		Scene,
		Shader,
		Texture,
		Mesh,
		Material,
		Text,
		Binary
	};

	enum class AssetLoadState : uint8_t
	{
		Unknown = 0,
		Unloaded,
		Loaded,
		Missing,
		Failed
	};

	struct AssetInfo
	{
		AssetId id = 0;
		AssetType type = AssetType::Unknown;
		std::string name{};
		std::filesystem::path relative_path{};
		std::filesystem::path parent_path{};
		bool is_directory = false;
		uint64_t file_size = 0;
		uint64_t last_write_time_ticks = 0;
	};

	class ASH_API AssetDatabase
	{
	public:
		class Impl;

	public:
		AssetDatabase() = default;

	public:
		static AssetDatabase create(const std::filesystem::path& root_path);

		bool is_valid() const;
		bool set_root_path(const std::filesystem::path& root_path);
		const std::filesystem::path& get_root_path() const;

		bool refresh();
		const std::vector<AssetInfo>& get_assets() const;
		const AssetInfo* find_asset_by_id(AssetId id) const;
		const AssetInfo* find_asset_by_path(const std::filesystem::path& path) const;

		AssetLoadState get_asset_load_state(AssetId id) const;
		std::string get_asset_last_error(AssetId id) const;
		std::string get_last_error() const;

		bool load_text_by_id(AssetId id, std::string& out_text);
		bool load_text_by_path(const std::filesystem::path& path, std::string& out_text);
		bool load_binary_by_id(AssetId id, std::vector<uint8_t>& out_bytes);
		bool load_binary_by_path(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes);

	private:
		std::shared_ptr<Impl> m_impl{};

	private:
		explicit AssetDatabase(std::shared_ptr<Impl> impl);
	};
}
