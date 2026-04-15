#include "AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <unordered_map>

namespace AshEngine
{
	namespace
	{
		struct AssetLoadInfo
		{
			AssetLoadState state = AssetLoadState::Unloaded;
			std::string error{};
		};

		static auto normalize_asset_key(const std::filesystem::path& path) -> std::string
		{
			std::string key = path.lexically_normal().generic_string();
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return key;
		}

		static bool is_path_outside_root(const std::filesystem::path& relative_path)
		{
			auto it = relative_path.begin();
			return it != relative_path.end() && *it == "..";
		}

		static auto hash_asset_key(std::string_view key) -> AssetId
		{
			constexpr uint64_t offset_basis = 14695981039346656037ull;
			constexpr uint64_t prime = 1099511628211ull;
			uint64_t hash = offset_basis;
			for (char c : key)
			{
				hash ^= static_cast<uint8_t>(c);
				hash *= prime;
			}
			return hash;
		}

		static auto detect_asset_type(const std::filesystem::path& path, bool is_directory) -> AssetType
		{
			if (is_directory)
			{
				return AssetType::Directory;
			}

			std::string ext = normalize_asset_key(path.extension());
			if (ext == ".scene" || ext == ".ashscene" || ext == ".json")
			{
				return AssetType::Scene;
			}
			if (ext == ".hlsl" || ext == ".hshader" || ext == ".glsl" || ext == ".spv")
			{
				return AssetType::Shader;
			}
			if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".dds" || ext == ".bmp" || ext == ".hdr")
			{
				return AssetType::Texture;
			}
			if (ext == ".mesh" || ext == ".ashmesh")
			{
				return AssetType::Mesh;
			}
			if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb")
			{
				return AssetType::Model;
			}
			if (ext == ".ashasset")
			{
				return AssetType::Prefab;
			}
			if (ext == ".mat" || ext == ".material")
			{
				return AssetType::Material;
			}
			if (ext == ".txt" || ext == ".md" || ext == ".ini" || ext == ".yaml" || ext == ".yml" || ext == ".csv")
			{
				return AssetType::Text;
			}
			return AssetType::Binary;
		}
	}

	class AssetDatabase::Impl
	{
	public:
		std::filesystem::path root_path{};
		std::vector<AssetInfo> assets{};
		std::unordered_map<AssetId, size_t> index_by_id{};
		std::unordered_map<std::string, size_t> index_by_key{};
		std::unordered_map<AssetId, AssetLoadInfo> load_info_by_id{};
		std::unordered_map<AssetId, std::shared_ptr<Mesh>> mesh_cache{};
		std::unordered_map<AssetId, std::shared_ptr<Model>> model_cache{};
		std::unordered_map<AssetId, std::shared_ptr<AshAsset>> ashasset_cache{};
		std::string last_error{};
	};

	AssetDatabase::AssetDatabase(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}

	AssetDatabase AssetDatabase::create(const std::filesystem::path& root_path)
	{
		AssetDatabase database(std::make_shared<Impl>());
		database.set_root_path(root_path);
		database.refresh();
		return database;
	}

	bool AssetDatabase::is_valid() const
	{
		return m_impl != nullptr;
	}

	bool AssetDatabase::set_root_path(const std::filesystem::path& root_path)
	{
		if (!m_impl)
		{
			m_impl = std::make_shared<Impl>();
		}

		std::error_code absolute_error{};
		const std::filesystem::path absolute_root = std::filesystem::absolute(root_path, absolute_error);
		m_impl->root_path = absolute_error ? root_path.lexically_normal() : absolute_root.lexically_normal();
		m_impl->last_error.clear();
		return true;
	}

	const std::filesystem::path& AssetDatabase::get_root_path() const
	{
		static const std::filesystem::path k_empty_path{};
		return m_impl ? m_impl->root_path : k_empty_path;
	}

	bool AssetDatabase::refresh()
	{
		if (!m_impl)
		{
			return false;
		}

		m_impl->assets.clear();
		m_impl->index_by_id.clear();
		m_impl->index_by_key.clear();
		m_impl->load_info_by_id.clear();
		m_impl->mesh_cache.clear();
		m_impl->model_cache.clear();
		m_impl->ashasset_cache.clear();
		m_impl->last_error.clear();

		std::error_code exists_error{};
		if (m_impl->root_path.empty() || !std::filesystem::exists(m_impl->root_path, exists_error))
		{
			m_impl->last_error = exists_error ? exists_error.message() : "Asset root path does not exist.";
			return false;
		}

		std::error_code iterate_error{};
		for (std::filesystem::recursive_directory_iterator it(m_impl->root_path, iterate_error), end; it != end; it.increment(iterate_error))
		{
			if (iterate_error)
			{
				m_impl->last_error = iterate_error.message();
				return false;
			}

			const std::filesystem::directory_entry& entry = *it;
			const std::filesystem::path relative_path = std::filesystem::relative(entry.path(), m_impl->root_path, iterate_error).lexically_normal();
			if (iterate_error)
			{
				m_impl->last_error = iterate_error.message();
				return false;
			}

			const std::string key = normalize_asset_key(relative_path);
			AssetInfo info{};
			info.id = hash_asset_key(key);
			info.name = entry.path().filename().string();
			info.relative_path = relative_path;
			info.parent_path = relative_path.parent_path();
			info.is_directory = entry.is_directory();
			info.type = detect_asset_type(relative_path, info.is_directory);

			std::error_code metadata_error{};
			info.last_write_time_ticks = static_cast<uint64_t>(entry.last_write_time(metadata_error).time_since_epoch().count());
			if (!info.is_directory)
			{
				info.file_size = entry.file_size(metadata_error);
			}

			m_impl->assets.push_back(std::move(info));
		}

		std::sort(m_impl->assets.begin(), m_impl->assets.end(), [](const AssetInfo& lhs, const AssetInfo& rhs)
		{
			return lhs.relative_path.generic_string() < rhs.relative_path.generic_string();
		});

		for (size_t index = 0; index < m_impl->assets.size(); ++index)
		{
			const AssetInfo& info = m_impl->assets[index];
			m_impl->index_by_id[info.id] = index;
			m_impl->index_by_key[normalize_asset_key(info.relative_path)] = index;
			if (!info.is_directory)
			{
				m_impl->load_info_by_id[info.id] = { AssetLoadState::Unloaded, {} };
			}
		}

		return true;
	}

	const std::vector<AssetInfo>& AssetDatabase::get_assets() const
	{
		static const std::vector<AssetInfo> k_empty_assets{};
		return m_impl ? m_impl->assets : k_empty_assets;
	}

	const AssetInfo* AssetDatabase::find_asset_by_id(AssetId id) const
	{
		if (!m_impl)
		{
			return nullptr;
		}

		auto it = m_impl->index_by_id.find(id);
		return it != m_impl->index_by_id.end() ? &m_impl->assets[it->second] : nullptr;
	}

	const AssetInfo* AssetDatabase::find_asset_by_path(const std::filesystem::path& path) const
	{
		if (!m_impl)
		{
			return nullptr;
		}

		std::filesystem::path normalized = path.lexically_normal();
		if (path.is_absolute())
		{
			std::error_code relative_error{};
			normalized = std::filesystem::relative(path, m_impl->root_path, relative_error).lexically_normal();
			if (relative_error || normalized.empty() || is_path_outside_root(normalized))
			{
				return nullptr;
			}
		}

		auto it = m_impl->index_by_key.find(normalize_asset_key(normalized));
		return it != m_impl->index_by_key.end() ? &m_impl->assets[it->second] : nullptr;
	}

	AssetLoadState AssetDatabase::get_asset_load_state(AssetId id) const
	{
		if (!m_impl)
		{
			return AssetLoadState::Unknown;
		}

		auto it = m_impl->load_info_by_id.find(id);
		return it != m_impl->load_info_by_id.end() ? it->second.state : AssetLoadState::Unknown;
	}

	std::string AssetDatabase::get_asset_last_error(AssetId id) const
	{
		if (!m_impl)
		{
			return {};
		}

		auto it = m_impl->load_info_by_id.find(id);
		return it != m_impl->load_info_by_id.end() ? it->second.error : std::string{};
	}

	std::string AssetDatabase::get_last_error() const
	{
		return m_impl ? m_impl->last_error : std::string{};
	}

	bool AssetDatabase::load_text_by_id(AssetId id, std::string& out_text)
	{
		const AssetInfo* info = find_asset_by_id(id);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->load_info_by_id[id] = { AssetLoadState::Missing, "Asset id was not found." };
			}
			return false;
		}
		return load_text_by_path(info->relative_path, out_text);
	}

	bool AssetDatabase::load_text_by_path(const std::filesystem::path& path, std::string& out_text)
	{
		const AssetInfo* info = find_asset_by_path(path);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->last_error = "Asset path was not found in the asset database.";
			}
			return false;
		}
		if (info->is_directory)
		{
			m_impl->last_error = "Cannot load a directory as text.";
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, "Cannot load a directory as text." };
			return false;
		}

		std::ifstream input(m_impl->root_path / info->relative_path, std::ios::binary);
		if (!input.is_open())
		{
			m_impl->last_error = "Failed to open text asset.";
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, "Failed to open text asset." };
			return false;
		}

		out_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		m_impl->last_error.clear();
		m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
		return true;
	}

	bool AssetDatabase::load_binary_by_id(AssetId id, std::vector<uint8_t>& out_bytes)
	{
		const AssetInfo* info = find_asset_by_id(id);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->load_info_by_id[id] = { AssetLoadState::Missing, "Asset id was not found." };
			}
			return false;
		}
		return load_binary_by_path(info->relative_path, out_bytes);
	}

	bool AssetDatabase::load_binary_by_path(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes)
	{
		const AssetInfo* info = find_asset_by_path(path);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->last_error = "Asset path was not found in the asset database.";
			}
			return false;
		}
		if (info->is_directory)
		{
			m_impl->last_error = "Cannot load a directory as binary.";
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, "Cannot load a directory as binary." };
			return false;
		}

		std::ifstream input(m_impl->root_path / info->relative_path, std::ios::binary);
		if (!input.is_open())
		{
			m_impl->last_error = "Failed to open binary asset.";
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, "Failed to open binary asset." };
			return false;
		}

		out_bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		m_impl->last_error.clear();
		m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
		return true;
	}

	bool AssetDatabase::load_mesh_by_id(AssetId id, std::shared_ptr<const Mesh>& out_mesh)
	{
		const AssetInfo* info = find_asset_by_id(id);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->load_info_by_id[id] = { AssetLoadState::Missing, "Asset id was not found." };
			}
			return false;
		}
		return load_mesh_by_path(info->relative_path, out_mesh);
	}

	bool AssetDatabase::load_mesh_by_path(const std::filesystem::path& path, std::shared_ptr<const Mesh>& out_mesh)
	{
		const AssetInfo* info = find_asset_by_path(path);
		if (!info || info->is_directory)
		{
			if (m_impl)
			{
				m_impl->last_error = "Asset path was not found in the asset database.";
			}
			return false;
		}

		if (const auto found = m_impl->mesh_cache.find(info->id); found != m_impl->mesh_cache.end())
		{
			out_mesh = found->second;
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
			return true;
		}

		auto mesh = std::make_shared<Mesh>();
		std::string error{};
		if (!load_mesh_from_file(m_impl->root_path / info->relative_path, *mesh, &error))
		{
			m_impl->last_error = error;
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, error };
			return false;
		}

		mesh->source_path = info->relative_path;
		m_impl->mesh_cache[info->id] = mesh;
		out_mesh = mesh;
		m_impl->last_error.clear();
		m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
		return true;
	}

	bool AssetDatabase::load_model_by_id(AssetId id, std::shared_ptr<const Model>& out_model)
	{
		const AssetInfo* info = find_asset_by_id(id);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->load_info_by_id[id] = { AssetLoadState::Missing, "Asset id was not found." };
			}
			return false;
		}
		return load_model_by_path(info->relative_path, out_model);
	}

	bool AssetDatabase::load_model_by_path(const std::filesystem::path& path, std::shared_ptr<const Model>& out_model)
	{
		const AssetInfo* info = find_asset_by_path(path);
		if (!info || info->is_directory)
		{
			if (m_impl)
			{
				m_impl->last_error = "Asset path was not found in the asset database.";
			}
			return false;
		}

		if (const auto found = m_impl->model_cache.find(info->id); found != m_impl->model_cache.end())
		{
			out_model = found->second;
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
			return true;
		}

		auto model = std::make_shared<Model>();
		std::string error{};
		if (!load_model_from_file(m_impl->root_path / info->relative_path, *model, &error))
		{
			m_impl->last_error = error;
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, error };
			return false;
		}

		model->source_path = info->relative_path;
		for (Mesh& mesh : model->meshes)
		{
			mesh.source_path = info->relative_path;
		}
		m_impl->model_cache[info->id] = model;
		out_model = model;
		m_impl->last_error.clear();
		m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
		return true;
	}

	bool AssetDatabase::load_ashasset_by_id(AssetId id, std::shared_ptr<const AshAsset>& out_asset)
	{
		const AssetInfo* info = find_asset_by_id(id);
		if (!info)
		{
			if (m_impl)
			{
				m_impl->load_info_by_id[id] = { AssetLoadState::Missing, "Asset id was not found." };
			}
			return false;
		}
		return load_ashasset_by_path(info->relative_path, out_asset);
	}

	bool AssetDatabase::load_ashasset_by_path(const std::filesystem::path& path, std::shared_ptr<const AshAsset>& out_asset)
	{
		const AssetInfo* info = find_asset_by_path(path);
		if (!info || info->is_directory)
		{
			if (m_impl)
			{
				m_impl->last_error = "Asset path was not found in the asset database.";
			}
			return false;
		}

		if (const auto found = m_impl->ashasset_cache.find(info->id); found != m_impl->ashasset_cache.end())
		{
			out_asset = found->second;
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
			return true;
		}

		auto asset = std::make_shared<AshAsset>();
		std::string error{};
		if (!load_ashasset_from_file(m_impl->root_path / info->relative_path, *asset, &error))
		{
			m_impl->last_error = error;
			m_impl->load_info_by_id[info->id] = { AssetLoadState::Failed, error };
			return false;
		}

		asset->source_path = info->relative_path;
		m_impl->ashasset_cache[info->id] = asset;
		out_asset = asset;
		m_impl->last_error.clear();
		m_impl->load_info_by_id[info->id] = { AssetLoadState::Loaded, {} };
		return true;
	}
}
