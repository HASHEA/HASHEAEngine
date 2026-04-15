#include "AssetDatabase.h"

#include "Base/hthreading.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
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

		struct ResolvedAssetInfo
		{
			AssetInfo info{};
			std::filesystem::path absolute_path{};
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

		template <typename TValue>
		static auto make_ready_future(TValue value) -> std::shared_future<TValue>
		{
			std::promise<TValue> promise{};
			promise.set_value(std::move(value));
			return promise.get_future().share();
		}

		static auto read_text_file(const std::filesystem::path& path, std::string& out_text, std::string& out_error) -> bool
		{
			std::ifstream input(path, std::ios::binary);
			if (!input.is_open())
			{
				out_error = "Failed to open text asset.";
				return false;
			}

			out_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			out_error.clear();
			return true;
		}

		static auto read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes, std::string& out_error) -> bool
		{
			std::ifstream input(path, std::ios::binary);
			if (!input.is_open())
			{
				out_error = "Failed to open binary asset.";
				return false;
			}

			out_bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			out_error.clear();
			return true;
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
		mutable std::mutex mutex{};
	};

	namespace
	{
		static auto set_last_error_locked(AssetDatabase::Impl& impl, const std::string& error) -> void
		{
			impl.last_error = error;
		}

		static auto set_load_info_locked(AssetDatabase::Impl& impl, AssetId id, AssetLoadState state, const std::string& error) -> void
		{
			if (id == 0)
			{
				return;
			}
			impl.load_info_by_id[id] = { state, error };
		}

		static auto resolve_asset_by_id_locked(const std::shared_ptr<AssetDatabase::Impl>& impl, AssetId id, ResolvedAssetInfo& out_resolved) -> bool
		{
			const auto found = impl->index_by_id.find(id);
			if (found == impl->index_by_id.end())
			{
				return false;
			}

			out_resolved.info = impl->assets[found->second];
			out_resolved.absolute_path = impl->root_path / out_resolved.info.relative_path;
			return true;
		}

		static auto resolve_asset_by_path_locked(const std::shared_ptr<AssetDatabase::Impl>& impl, const std::filesystem::path& path, ResolvedAssetInfo& out_resolved) -> bool
		{
			std::filesystem::path normalized = path.lexically_normal();
			if (path.is_absolute())
			{
				std::error_code relative_error{};
				normalized = std::filesystem::relative(path, impl->root_path, relative_error).lexically_normal();
				if (relative_error || normalized.empty() || is_path_outside_root(normalized))
				{
					return false;
				}
			}

			const auto found = impl->index_by_key.find(normalize_asset_key(normalized));
			if (found == impl->index_by_key.end())
			{
				return false;
			}

			out_resolved.info = impl->assets[found->second];
			out_resolved.absolute_path = impl->root_path / out_resolved.info.relative_path;
			return true;
		}

		static auto resolve_asset_by_id(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			AssetId id,
			ResolvedAssetInfo& out_resolved,
			std::string& out_error) -> bool
		{
			std::scoped_lock<std::mutex> lock(impl->mutex);
			if (!resolve_asset_by_id_locked(impl, id, out_resolved))
			{
				out_error = "Asset id was not found.";
				set_load_info_locked(*impl, id, AssetLoadState::Missing, out_error);
				set_last_error_locked(*impl, out_error);
				return false;
			}
			out_error.clear();
			return true;
		}

		static auto resolve_asset_by_path(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const std::filesystem::path& path,
			ResolvedAssetInfo& out_resolved,
			std::string& out_error) -> bool
		{
			std::scoped_lock<std::mutex> lock(impl->mutex);
			if (!resolve_asset_by_path_locked(impl, path, out_resolved))
			{
				out_error = "Asset path was not found in the asset database.";
				set_last_error_locked(*impl, out_error);
				return false;
			}
			out_error.clear();
			return true;
		}

		template <typename TResource, typename TCacheMap, typename LoaderFn, typename FinalizeFn>
		static auto load_cached_resource(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const ResolvedAssetInfo& resolved,
			TCacheMap& cache,
			std::shared_ptr<const TResource>& out_resource,
			LoaderFn&& loader,
			FinalizeFn&& finalize) -> bool
		{
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				const auto cached = cache.find(resolved.info.id);
				if (cached != cache.end())
				{
					out_resource = cached->second;
					set_last_error_locked(*impl, {});
					set_load_info_locked(*impl, resolved.info.id, AssetLoadState::Loaded, {});
					return true;
				}

				set_load_info_locked(*impl, resolved.info.id, AssetLoadState::Loading, {});
			}

			auto resource = std::make_shared<TResource>();
			std::string error{};
			if (!loader(resolved.absolute_path, *resource, error))
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				set_last_error_locked(*impl, error);
				set_load_info_locked(*impl, resolved.info.id, AssetLoadState::Failed, error);
				return false;
			}

			finalize(*resource);

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				cache[resolved.info.id] = resource;
				out_resource = resource;
				set_last_error_locked(*impl, {});
				set_load_info_locked(*impl, resolved.info.id, AssetLoadState::Loaded, {});
			}
			return true;
		}
	}

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
		std::scoped_lock<std::mutex> lock(m_impl->mutex);
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

		std::error_code exists_error{};
		const std::filesystem::path root_path = get_root_path();
		if (root_path.empty() || !std::filesystem::exists(root_path, exists_error))
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			m_impl->assets.clear();
			m_impl->index_by_id.clear();
			m_impl->index_by_key.clear();
			m_impl->load_info_by_id.clear();
			m_impl->mesh_cache.clear();
			m_impl->model_cache.clear();
			m_impl->ashasset_cache.clear();
			m_impl->last_error = exists_error ? exists_error.message() : "Asset root path does not exist.";
			return false;
		}

		std::vector<AssetInfo> assets{};
		std::unordered_map<AssetId, size_t> index_by_id{};
		std::unordered_map<std::string, size_t> index_by_key{};
		std::unordered_map<AssetId, AssetLoadInfo> load_info_by_id{};

		std::error_code iterate_error{};
		for (std::filesystem::recursive_directory_iterator it(root_path, iterate_error), end; it != end; it.increment(iterate_error))
		{
			if (iterate_error)
			{
				std::scoped_lock<std::mutex> lock(m_impl->mutex);
				m_impl->last_error = iterate_error.message();
				return false;
			}

			const std::filesystem::directory_entry& entry = *it;
			const std::filesystem::path relative_path = std::filesystem::relative(entry.path(), root_path, iterate_error).lexically_normal();
			if (iterate_error)
			{
				std::scoped_lock<std::mutex> lock(m_impl->mutex);
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

			assets.push_back(std::move(info));
		}

		std::sort(assets.begin(), assets.end(), [](const AssetInfo& lhs, const AssetInfo& rhs)
		{
			return lhs.relative_path.generic_string() < rhs.relative_path.generic_string();
		});

		for (size_t index = 0; index < assets.size(); ++index)
		{
			const AssetInfo& info = assets[index];
			index_by_id[info.id] = index;
			index_by_key[normalize_asset_key(info.relative_path)] = index;
			if (!info.is_directory)
			{
				load_info_by_id[info.id] = { AssetLoadState::Unloaded, {} };
			}
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		m_impl->assets = std::move(assets);
		m_impl->index_by_id = std::move(index_by_id);
		m_impl->index_by_key = std::move(index_by_key);
		m_impl->load_info_by_id = std::move(load_info_by_id);
		m_impl->mesh_cache.clear();
		m_impl->model_cache.clear();
		m_impl->ashasset_cache.clear();
		m_impl->last_error.clear();
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

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const auto found = m_impl->index_by_id.find(id);
		return found != m_impl->index_by_id.end() ? &m_impl->assets[found->second] : nullptr;
	}

	const AssetInfo* AssetDatabase::find_asset_by_path(const std::filesystem::path& path) const
	{
		if (!m_impl)
		{
			return nullptr;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
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

		const auto found = m_impl->index_by_key.find(normalize_asset_key(normalized));
		return found != m_impl->index_by_key.end() ? &m_impl->assets[found->second] : nullptr;
	}

	AssetLoadState AssetDatabase::get_asset_load_state(AssetId id) const
	{
		if (!m_impl)
		{
			return AssetLoadState::Unknown;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const auto found = m_impl->load_info_by_id.find(id);
		return found != m_impl->load_info_by_id.end() ? found->second.state : AssetLoadState::Unknown;
	}

	std::string AssetDatabase::get_asset_last_error(AssetId id) const
	{
		if (!m_impl)
		{
			return {};
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const auto found = m_impl->load_info_by_id.find(id);
		return found != m_impl->load_info_by_id.end() ? found->second.error : std::string{};
	}

	std::string AssetDatabase::get_last_error() const
	{
		if (!m_impl)
		{
			return {};
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		return m_impl->last_error;
	}

	bool AssetDatabase::load_text_by_id(AssetId id, std::string& out_text)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return false;
		}
		return load_text_by_path(resolved.info.relative_path, out_text);
	}

	bool AssetDatabase::load_text_by_path(const std::filesystem::path& path, std::string& out_text)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return false;
		}

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as text.";
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loading, {});
		}

		if (!read_text_file(resolved.absolute_path, out_text, error))
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		set_last_error_locked(*m_impl, {});
		set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loaded, {});
		return true;
	}

	bool AssetDatabase::load_binary_by_id(AssetId id, std::vector<uint8_t>& out_bytes)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return false;
		}
		return load_binary_by_path(resolved.info.relative_path, out_bytes);
	}

	bool AssetDatabase::load_binary_by_path(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return false;
		}

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as binary.";
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loading, {});
		}

		if (!read_binary_file(resolved.absolute_path, out_bytes, error))
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		set_last_error_locked(*m_impl, {});
		set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loaded, {});
		return true;
	}

	bool AssetDatabase::load_mesh_by_id(AssetId id, std::shared_ptr<const Mesh>& out_mesh)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return false;
		}
		return load_mesh_by_path(resolved.info.relative_path, out_mesh);
	}

	bool AssetDatabase::load_mesh_by_path(const std::filesystem::path& path, std::shared_ptr<const Mesh>& out_mesh)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return false;
		}

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as mesh.";
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		return load_cached_resource<Mesh>(
			m_impl,
			resolved,
			m_impl->mesh_cache,
			out_mesh,
			[](const std::filesystem::path& absolute_path, Mesh& mesh, std::string& out_error) -> bool
			{
				return load_mesh_from_file(absolute_path, mesh, &out_error);
			},
			[&resolved](Mesh& mesh) -> void
			{
				mesh.source_path = resolved.info.relative_path;
			});
	}

	std::shared_future<std::shared_ptr<const Mesh>> AssetDatabase::load_mesh_by_id_async(AssetId id)
	{
		if (!m_impl)
		{
			return make_ready_future(std::shared_ptr<const Mesh>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return make_ready_future(std::shared_ptr<const Mesh>{});
		}
		return load_mesh_by_path_async(resolved.info.relative_path);
	}

	std::shared_future<std::shared_ptr<const Mesh>> AssetDatabase::load_mesh_by_path_async(const std::filesystem::path& path)
	{
		if (!m_impl)
		{
			return make_ready_future(std::shared_ptr<const Mesh>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return make_ready_future(std::shared_ptr<const Mesh>{});
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			const auto cached = m_impl->mesh_cache.find(resolved.info.id);
			if (cached != m_impl->mesh_cache.end())
			{
				set_last_error_locked(*m_impl, {});
				set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loaded, {});
				return make_ready_future(std::shared_ptr<const Mesh>(cached->second));
			}

			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loading, {});
		}

		AssetDatabase database(m_impl);
		const std::filesystem::path relative_path = resolved.info.relative_path;
		return dispatch_background_task("AssetDatabase::load_mesh_by_path_async", [database, relative_path]() mutable -> std::shared_ptr<const Mesh>
		{
			std::shared_ptr<const Mesh> mesh{};
			if (!database.load_mesh_by_path(relative_path, mesh))
			{
				return {};
			}
			return mesh;
		});
	}

	bool AssetDatabase::load_model_by_id(AssetId id, std::shared_ptr<const Model>& out_model)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return false;
		}
		return load_model_by_path(resolved.info.relative_path, out_model);
	}

	bool AssetDatabase::load_model_by_path(const std::filesystem::path& path, std::shared_ptr<const Model>& out_model)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return false;
		}

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as model.";
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		return load_cached_resource<Model>(
			m_impl,
			resolved,
			m_impl->model_cache,
			out_model,
			[](const std::filesystem::path& absolute_path, Model& model, std::string& out_error) -> bool
			{
				return load_model_from_file(absolute_path, model, &out_error);
			},
			[&resolved](Model& model) -> void
			{
				model.source_path = resolved.info.relative_path;
				for (Mesh& mesh : model.meshes)
				{
					mesh.source_path = resolved.info.relative_path;
				}
			});
	}

	std::shared_future<std::shared_ptr<const Model>> AssetDatabase::load_model_by_id_async(AssetId id)
	{
		if (!m_impl)
		{
			return make_ready_future(std::shared_ptr<const Model>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return make_ready_future(std::shared_ptr<const Model>{});
		}
		return load_model_by_path_async(resolved.info.relative_path);
	}

	std::shared_future<std::shared_ptr<const Model>> AssetDatabase::load_model_by_path_async(const std::filesystem::path& path)
	{
		if (!m_impl)
		{
			return make_ready_future(std::shared_ptr<const Model>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return make_ready_future(std::shared_ptr<const Model>{});
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			const auto cached = m_impl->model_cache.find(resolved.info.id);
			if (cached != m_impl->model_cache.end())
			{
				set_last_error_locked(*m_impl, {});
				set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loaded, {});
				return make_ready_future(std::shared_ptr<const Model>(cached->second));
			}

			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loading, {});
		}

		AssetDatabase database(m_impl);
		const std::filesystem::path relative_path = resolved.info.relative_path;
		return dispatch_background_task("AssetDatabase::load_model_by_path_async", [database, relative_path]() mutable -> std::shared_ptr<const Model>
		{
			std::shared_ptr<const Model> model{};
			if (!database.load_model_by_path(relative_path, model))
			{
				return {};
			}
			return model;
		});
	}

	bool AssetDatabase::load_ashasset_by_id(AssetId id, std::shared_ptr<const AshAsset>& out_asset)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return false;
		}
		return load_ashasset_by_path(resolved.info.relative_path, out_asset);
	}

	bool AssetDatabase::load_ashasset_by_path(const std::filesystem::path& path, std::shared_ptr<const AshAsset>& out_asset)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return false;
		}

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as ashasset.";
			set_last_error_locked(*m_impl, error);
			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Failed, error);
			return false;
		}

		return load_cached_resource<AshAsset>(
			m_impl,
			resolved,
			m_impl->ashasset_cache,
			out_asset,
			[](const std::filesystem::path& absolute_path, AshAsset& asset, std::string& out_error) -> bool
			{
				return load_ashasset_from_file(absolute_path, asset, &out_error);
			},
			[&resolved](AshAsset& asset) -> void
			{
				asset.source_path = resolved.info.relative_path;
			});
	}

	std::shared_future<std::shared_ptr<const AshAsset>> AssetDatabase::load_ashasset_by_id_async(AssetId id)
	{
		if (!m_impl)
		{
			return make_ready_future(std::shared_ptr<const AshAsset>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return make_ready_future(std::shared_ptr<const AshAsset>{});
		}
		return load_ashasset_by_path_async(resolved.info.relative_path);
	}

	std::shared_future<std::shared_ptr<const AshAsset>> AssetDatabase::load_ashasset_by_path_async(const std::filesystem::path& path)
	{
		if (!m_impl)
		{
			return make_ready_future(std::shared_ptr<const AshAsset>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return make_ready_future(std::shared_ptr<const AshAsset>{});
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			const auto cached = m_impl->ashasset_cache.find(resolved.info.id);
			if (cached != m_impl->ashasset_cache.end())
			{
				set_last_error_locked(*m_impl, {});
				set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loaded, {});
				return make_ready_future(std::shared_ptr<const AshAsset>(cached->second));
			}

			set_load_info_locked(*m_impl, resolved.info.id, AssetLoadState::Loading, {});
		}

		AssetDatabase database(m_impl);
		const std::filesystem::path relative_path = resolved.info.relative_path;
		return dispatch_background_task("AssetDatabase::load_ashasset_by_path_async", [database, relative_path]() mutable -> std::shared_ptr<const AshAsset>
		{
			std::shared_ptr<const AshAsset> asset{};
			if (!database.load_ashasset_by_path(relative_path, asset))
			{
				return {};
			}
			return asset;
		});
	}
}
