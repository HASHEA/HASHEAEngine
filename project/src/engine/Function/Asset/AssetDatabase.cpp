#include "AssetDatabase.h"

#include "Base/hthreading.h"
#include "Function/Render/Material.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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
			if (ext == ".ashmat" || ext == ".ashmatins")
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
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			std::ifstream input(path, std::ios::binary);
			ASH_PROCESS_ERROR(input.is_open());
			out_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			out_error.clear();
			ASH_PROCESS_GUARD_END(bResult, false);
			if (!bResult)
			{
				out_error = "Failed to open text asset.";
			}
			return bResult;
		}

		static auto read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes, std::string& out_error) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			std::ifstream input(path, std::ios::binary);
			ASH_PROCESS_ERROR(input.is_open());
			out_bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			out_error.clear();
			ASH_PROCESS_GUARD_END(bResult, false);
			if (!bResult)
			{
				out_error = "Failed to open binary asset.";
			}
			return bResult;
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
		std::unordered_map<std::string, std::shared_ptr<MaterialInterface>> material_cache{};
		std::unordered_map<AssetId, std::shared_ptr<AshAsset>> ashasset_cache{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const Mesh>>> inflight_mesh_loads{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const Model>>> inflight_model_loads{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const MaterialInterface>>> inflight_material_loads{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const AshAsset>>> inflight_ashasset_loads{};
		std::string last_error{};
		mutable std::mutex mutex{};
	};

	namespace
	{
		static auto set_last_error_locked(AssetDatabase::Impl& impl, const std::string& error) -> void;
		static auto set_load_failed_locked(AssetDatabase::Impl& impl, AssetId id, const std::string& error) -> void;
		static auto set_load_loading_locked(AssetDatabase::Impl& impl, AssetId id) -> void;
		static auto set_load_success_locked(AssetDatabase::Impl& impl, AssetId id) -> void;
		static auto try_get_cached_load_failure_locked(AssetDatabase::Impl& impl, AssetId id, std::string& out_error) -> bool;
		static auto resolve_asset_by_path(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const std::filesystem::path& path,
			ResolvedAssetInfo& out_resolved,
			std::string& out_error) -> bool;

		static auto try_get_cached_material_locked(
			AssetDatabase::Impl& impl,
			std::string_view cache_key,
			std::shared_ptr<const MaterialInterface>& out_material) -> bool
		{
			const auto cached = impl.material_cache.find(std::string(cache_key));
			if (cached == impl.material_cache.end())
			{
				return false;
			}

			out_material = cached->second;
			return true;
		}

		static auto set_material_asset_path(
			const std::shared_ptr<MaterialInterface>& material,
			const std::filesystem::path& asset_path) -> void
		{
			if (!material)
			{
				return;
			}

			if (material->is_material_instance())
			{
				std::static_pointer_cast<MaterialInstance>(material)->set_asset_path(asset_path);
				std::static_pointer_cast<MaterialInstance>(material)->reset_change_version();
				return;
			}

			std::static_pointer_cast<Material>(material)->set_asset_path(asset_path);
			std::static_pointer_cast<Material>(material)->reset_change_version();
		}

		static auto try_load_builtin_material(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const std::filesystem::path& path,
			std::shared_ptr<const MaterialInterface>& out_material) -> bool
		{
			const std::string cache_key = normalize_asset_key(path);
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				if (try_get_cached_material_locked(*impl, cache_key, out_material))
				{
					set_last_error_locked(*impl, {});
					return true;
				}
			}

			std::shared_ptr<MaterialInterface> builtin_material = make_builtin_material(path.generic_string());
			if (!builtin_material)
			{
				return false;
			}

			set_material_asset_path(builtin_material, builtin_material->get_asset_path().empty() ? path.lexically_normal() : builtin_material->get_asset_path());

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				impl->material_cache[cache_key] = builtin_material;
				set_last_error_locked(*impl, {});
				out_material = builtin_material;
			}
			return true;
		}

		static auto load_material_by_path_recursive(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const std::filesystem::path& path,
			std::shared_ptr<const MaterialInterface>& out_material,
			std::unordered_set<std::string>& loading_keys,
			std::string& out_error) -> bool
		{
			const std::string cache_key = normalize_asset_key(path);
			if (cache_key.empty())
			{
				out_error = "Material path is empty.";
				return false;
			}

			ResolvedAssetInfo resolved{};
			if (!resolve_asset_by_path(impl, path, resolved, out_error))
			{
				if (try_load_builtin_material(impl, path, out_material))
				{
					out_error.clear();
					return true;
				}
				return false;
			}

			if (resolved.info.is_directory)
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				out_error = "Cannot load a directory as material.";
				set_load_failed_locked(*impl, resolved.info.id, out_error);
				return false;
			}

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				if (try_get_cached_material_locked(*impl, cache_key, out_material))
				{
					set_load_success_locked(*impl, resolved.info.id);
					out_error.clear();
					return true;
				}
				if (try_get_cached_load_failure_locked(*impl, resolved.info.id, out_error))
				{
					return false;
				}
			}

			if (!loading_keys.insert(cache_key).second)
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				out_error = "Detected cyclic material parent reference.";
				set_load_failed_locked(*impl, resolved.info.id, out_error);
				return false;
			}

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				set_load_loading_locked(*impl, resolved.info.id);
			}

			std::shared_ptr<MaterialInterface> material{};
			if (!load_material_from_file(resolved.absolute_path, material, &out_error))
			{
				loading_keys.erase(cache_key);
				std::scoped_lock<std::mutex> lock(impl->mutex);
				set_load_failed_locked(*impl, resolved.info.id, out_error);
				return false;
			}

			set_material_asset_path(material, resolved.info.relative_path);

			if (material && material->is_material_instance())
			{
				auto material_instance = std::static_pointer_cast<MaterialInstance>(material);
				if (!material_instance->get_parent() && !material_instance->get_parent_asset_path().empty())
				{
					std::shared_ptr<const MaterialInterface> parent_material{};
					if (!load_material_by_path_recursive(impl, material_instance->get_parent_asset_path(), parent_material, loading_keys, out_error))
					{
						loading_keys.erase(cache_key);
						std::scoped_lock<std::mutex> lock(impl->mutex);
						set_load_failed_locked(*impl, resolved.info.id, out_error);
						return false;
					}
					material_instance->set_parent(parent_material);
					material_instance->reset_change_version();
				}
			}

			loading_keys.erase(cache_key);

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				impl->material_cache[cache_key] = material;
				out_material = material;
				set_load_success_locked(*impl, resolved.info.id);
			}

			out_error.clear();
			return true;
		}
	}

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

		static auto set_load_failed_locked(AssetDatabase::Impl& impl, AssetId id, const std::string& error) -> void
		{
			set_last_error_locked(impl, error);
			set_load_info_locked(impl, id, AssetLoadState::Failed, error);
		}

		static auto set_load_loading_locked(AssetDatabase::Impl& impl, AssetId id) -> void
		{
			set_load_info_locked(impl, id, AssetLoadState::Loading, {});
		}

		static auto set_load_success_locked(AssetDatabase::Impl& impl, AssetId id) -> void
		{
			set_last_error_locked(impl, {});
			set_load_info_locked(impl, id, AssetLoadState::Loaded, {});
		}

		static auto try_get_cached_load_failure_locked(AssetDatabase::Impl& impl, AssetId id, std::string& out_error) -> bool
		{
			const auto found = impl.load_info_by_id.find(id);
			if (found == impl.load_info_by_id.end() || found->second.state != AssetLoadState::Failed)
			{
				return false;
			}

			out_error = found->second.error.empty() ? "Asset load failed previously." : found->second.error;
			set_last_error_locked(impl, out_error);
			return true;
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
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			std::scoped_lock<std::mutex> lock(impl->mutex);
			if (!resolve_asset_by_id_locked(impl, id, out_resolved))
			{
				out_error = "Asset id was not found.";
				set_load_info_locked(*impl, id, AssetLoadState::Missing, out_error);
				set_last_error_locked(*impl, out_error);
				ASH_PROCESS_ERROR(false);
			}
			out_error.clear();
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		static auto resolve_asset_by_path(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const std::filesystem::path& path,
			ResolvedAssetInfo& out_resolved,
			std::string& out_error) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			std::scoped_lock<std::mutex> lock(impl->mutex);
			if (!resolve_asset_by_path_locked(impl, path, out_resolved))
			{
				out_error = "Asset path was not found in the asset database.";
				set_last_error_locked(*impl, out_error);
				ASH_PROCESS_ERROR(false);
			}
			out_error.clear();
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
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
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				const auto cached = cache.find(resolved.info.id);
				if (cached != cache.end())
				{
					out_resource = cached->second;
					set_load_success_locked(*impl, resolved.info.id);
					break;
				}
				std::string cached_error{};
				if (try_get_cached_load_failure_locked(*impl, resolved.info.id, cached_error))
				{
					ASH_PROCESS_ERROR(false);
				}

				set_load_loading_locked(*impl, resolved.info.id);
			}

			auto resource = std::make_shared<TResource>();
			std::string error{};
			if (!loader(resolved.absolute_path, *resource, error))
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				set_load_failed_locked(*impl, resolved.info.id, error);
				ASH_PROCESS_ERROR(false);
			}

			finalize(*resource);

			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				cache[resolved.info.id] = resource;
				out_resource = resource;
				set_load_success_locked(*impl, resolved.info.id);
			}
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
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
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

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
			m_impl->material_cache.clear();
			m_impl->ashasset_cache.clear();
			m_impl->inflight_mesh_loads.clear();
			m_impl->inflight_model_loads.clear();
			m_impl->inflight_material_loads.clear();
			m_impl->inflight_ashasset_loads.clear();
			m_impl->last_error = exists_error ? exists_error.message() : "Asset root path does not exist.";
			ASH_PROCESS_ERROR(false);
		}

		std::vector<AssetInfo> assets{};
		std::unordered_map<AssetId, size_t> index_by_id{};
		std::unordered_map<std::string, size_t> index_by_key{};
		std::unordered_map<AssetId, AssetLoadInfo> load_info_by_id{};
		bool iterate_ok = true;

		std::error_code iterate_error{};
		for (std::filesystem::recursive_directory_iterator it(root_path, iterate_error), end; it != end; it.increment(iterate_error))
		{
			if (iterate_error)
			{
				std::scoped_lock<std::mutex> lock(m_impl->mutex);
				m_impl->last_error = iterate_error.message();
				iterate_ok = false;
				break;
			}

			const std::filesystem::directory_entry& entry = *it;
			const std::filesystem::path relative_path = std::filesystem::relative(entry.path(), root_path, iterate_error).lexically_normal();
			if (iterate_error)
			{
				std::scoped_lock<std::mutex> lock(m_impl->mutex);
				m_impl->last_error = iterate_error.message();
				iterate_ok = false;
				break;
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
		ASH_PROCESS_ERROR(iterate_ok);

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
		m_impl->material_cache.clear();
		m_impl->ashasset_cache.clear();
		m_impl->inflight_mesh_loads.clear();
		m_impl->inflight_model_loads.clear();
		m_impl->inflight_material_loads.clear();
		m_impl->inflight_ashasset_loads.clear();
		m_impl->last_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
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
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		bResult = load_text_by_path(resolved.info.relative_path, out_text);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_text_by_path(const std::filesystem::path& path, std::string& out_text)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as text.";
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_load_loading_locked(*m_impl, resolved.info.id);
		}

		if (!read_text_file(resolved.absolute_path, out_text, error))
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		set_load_success_locked(*m_impl, resolved.info.id);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_binary_by_id(AssetId id, std::vector<uint8_t>& out_bytes)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		bResult = load_binary_by_path(resolved.info.relative_path, out_bytes);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_binary_by_path(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as binary.";
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_load_loading_locked(*m_impl, resolved.info.id);
		}

		if (!read_binary_file(resolved.absolute_path, out_bytes, error))
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		set_load_success_locked(*m_impl, resolved.info.id);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_mesh_by_id(AssetId id, std::shared_ptr<const Mesh>& out_mesh)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		bResult = load_mesh_by_path(resolved.info.relative_path, out_mesh);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_mesh_by_path(const std::filesystem::path& path, std::shared_ptr<const Mesh>& out_mesh)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as mesh.";
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		ASH_PROCESS_ERROR(load_cached_resource<Mesh>(
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
			}));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_future<std::shared_ptr<const Mesh>> AssetDatabase::load_mesh_by_id_async(AssetId id)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const Mesh>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const Mesh>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		result = load_mesh_by_path_async(resolved.info.relative_path);
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	std::shared_future<std::shared_ptr<const Mesh>> AssetDatabase::load_mesh_by_path_async(const std::filesystem::path& path)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const Mesh>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const Mesh>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		std::shared_ptr<std::promise<std::shared_ptr<const Mesh>>> mesh_promise{};
		std::filesystem::path mesh_relative_path{};
		AssetId mesh_asset_id = 0;
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			const auto cached = m_impl->mesh_cache.find(resolved.info.id);
			if (cached != m_impl->mesh_cache.end())
			{
				set_load_success_locked(*m_impl, resolved.info.id);
				result = make_ready_future(std::shared_ptr<const Mesh>(cached->second));
				break;
			}

			std::string cached_error{};
			if (try_get_cached_load_failure_locked(*m_impl, resolved.info.id, cached_error))
			{
				result = empty_future;
				break;
			}

			const auto inflight = m_impl->inflight_mesh_loads.find(resolved.info.id);
			if (inflight != m_impl->inflight_mesh_loads.end())
			{
				result = inflight->second;
				break;
			}

			auto promise = std::make_shared<std::promise<std::shared_ptr<const Mesh>>>();
			result = promise->get_future().share();
			m_impl->inflight_mesh_loads[resolved.info.id] = result;
			set_load_loading_locked(*m_impl, resolved.info.id);
			mesh_promise = std::move(promise);
			mesh_relative_path = resolved.info.relative_path;
			mesh_asset_id = resolved.info.id;
		}

		if (mesh_promise)
		{
			AssetDatabase database(m_impl);
			dispatch_background_task("AssetDatabase::load_mesh_by_path_async", [database, relative_path = std::move(mesh_relative_path), impl = m_impl, asset_id = mesh_asset_id, promise = std::move(mesh_promise)]() mutable -> void
			{
				std::shared_ptr<const Mesh> mesh{};
				try
				{
					if (!database.load_mesh_by_path(relative_path, mesh))
					{
						mesh.reset();
					}
				}
				catch (...)
				{
					mesh.reset();
				}
				promise->set_value(mesh);

				std::scoped_lock<std::mutex> lock(impl->mutex);
				impl->inflight_mesh_loads.erase(asset_id);
			});
		}

		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	bool AssetDatabase::load_model_by_id(AssetId id, std::shared_ptr<const Model>& out_model)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		bResult = load_model_by_path(resolved.info.relative_path, out_model);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_model_by_path(const std::filesystem::path& path, std::shared_ptr<const Model>& out_model)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as model.";
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		ASH_PROCESS_ERROR(load_cached_resource<Model>(
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
			}));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_future<std::shared_ptr<const Model>> AssetDatabase::load_model_by_id_async(AssetId id)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const Model>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const Model>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		result = load_model_by_path_async(resolved.info.relative_path);
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	std::shared_future<std::shared_ptr<const Model>> AssetDatabase::load_model_by_path_async(const std::filesystem::path& path)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const Model>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const Model>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		std::shared_ptr<std::promise<std::shared_ptr<const Model>>> model_promise{};
		std::filesystem::path model_relative_path{};
		AssetId model_asset_id = 0;
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			const auto cached = m_impl->model_cache.find(resolved.info.id);
			if (cached != m_impl->model_cache.end())
			{
				set_load_success_locked(*m_impl, resolved.info.id);
				result = make_ready_future(std::shared_ptr<const Model>(cached->second));
				break;
			}

			std::string cached_error{};
			if (try_get_cached_load_failure_locked(*m_impl, resolved.info.id, cached_error))
			{
				result = empty_future;
				break;
			}

			const auto inflight = m_impl->inflight_model_loads.find(resolved.info.id);
			if (inflight != m_impl->inflight_model_loads.end())
			{
				result = inflight->second;
				break;
			}

			auto promise = std::make_shared<std::promise<std::shared_ptr<const Model>>>();
			result = promise->get_future().share();
			m_impl->inflight_model_loads[resolved.info.id] = result;
			set_load_loading_locked(*m_impl, resolved.info.id);
			model_promise = std::move(promise);
			model_relative_path = resolved.info.relative_path;
			model_asset_id = resolved.info.id;
		}

		if (model_promise)
		{
			AssetDatabase database(m_impl);
			dispatch_background_task("AssetDatabase::load_model_by_path_async", [database, relative_path = std::move(model_relative_path), impl = m_impl, asset_id = model_asset_id, promise = std::move(model_promise)]() mutable -> void
			{
				std::shared_ptr<const Model> model{};
				try
				{
					if (!database.load_model_by_path(relative_path, model))
					{
						model.reset();
					}
				}
				catch (...)
				{
					model.reset();
				}
				promise->set_value(model);

				std::scoped_lock<std::mutex> lock(impl->mutex);
				impl->inflight_model_loads.erase(asset_id);
			});
		}
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	bool AssetDatabase::load_material_by_id(AssetId id, std::shared_ptr<const MaterialInterface>& out_material)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		bResult = load_material_by_path(resolved.info.relative_path, out_material);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_material_by_path(const std::filesystem::path& path, std::shared_ptr<const MaterialInterface>& out_material)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		std::unordered_set<std::string> loading_keys{};
		std::string error{};
		ASH_PROCESS_ERROR(load_material_by_path_recursive(m_impl, path, out_material, loading_keys, error));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_future<std::shared_ptr<const MaterialInterface>> AssetDatabase::load_material_by_id_async(AssetId id)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const MaterialInterface>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const MaterialInterface>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		result = load_material_by_path_async(resolved.info.relative_path);
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	std::shared_future<std::shared_ptr<const MaterialInterface>> AssetDatabase::load_material_by_path_async(const std::filesystem::path& path)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const MaterialInterface>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const MaterialInterface>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			std::shared_ptr<const MaterialInterface> builtin_material{};
			if (try_load_builtin_material(m_impl, path, builtin_material))
			{
				result = make_ready_future(builtin_material);
				break;
			}
			ASH_PROCESS_ERROR(false);
		}

		const std::string cache_key = normalize_asset_key(resolved.info.relative_path);
		std::shared_ptr<const MaterialInterface> cached_material{};
		std::shared_ptr<std::promise<std::shared_ptr<const MaterialInterface>>> material_promise{};
		std::filesystem::path material_relative_path{};
		AssetId material_asset_id = 0;
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			if (try_get_cached_material_locked(*m_impl, cache_key, cached_material))
			{
				set_load_success_locked(*m_impl, resolved.info.id);
				result = make_ready_future(cached_material);
				break;
			}

			std::string cached_error{};
			if (try_get_cached_load_failure_locked(*m_impl, resolved.info.id, cached_error))
			{
				result = empty_future;
				break;
			}

			const auto inflight = m_impl->inflight_material_loads.find(resolved.info.id);
			if (inflight != m_impl->inflight_material_loads.end())
			{
				result = inflight->second;
				break;
			}

			auto promise = std::make_shared<std::promise<std::shared_ptr<const MaterialInterface>>>();
			result = promise->get_future().share();
			m_impl->inflight_material_loads[resolved.info.id] = result;
			set_load_loading_locked(*m_impl, resolved.info.id);
			material_promise = std::move(promise);
			material_relative_path = resolved.info.relative_path;
			material_asset_id = resolved.info.id;
		}

		if (material_promise)
		{
			AssetDatabase database(m_impl);
			dispatch_background_task("AssetDatabase::load_material_by_path_async", [database, relative_path = std::move(material_relative_path), impl = m_impl, asset_id = material_asset_id, promise = std::move(material_promise)]() mutable -> void
			{
				std::shared_ptr<const MaterialInterface> material{};
				try
				{
					if (!database.load_material_by_path(relative_path, material))
					{
						material.reset();
					}
				}
				catch (...)
				{
					material.reset();
				}
				promise->set_value(material);

				std::scoped_lock<std::mutex> lock(impl->mutex);
				impl->inflight_material_loads.erase(asset_id);
			});
		}
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	bool AssetDatabase::load_ashasset_by_id(AssetId id, std::shared_ptr<const AshAsset>& out_asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		bResult = load_ashasset_by_path(resolved.info.relative_path, out_asset);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AssetDatabase::load_ashasset_by_path(const std::filesystem::path& path, std::shared_ptr<const AshAsset>& out_asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		if (resolved.info.is_directory)
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			error = "Cannot load a directory as ashasset.";
			set_load_failed_locked(*m_impl, resolved.info.id, error);
			ASH_PROCESS_ERROR(false);
		}

		ASH_PROCESS_ERROR(load_cached_resource<AshAsset>(
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
			}));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_future<std::shared_ptr<const AshAsset>> AssetDatabase::load_ashasset_by_id_async(AssetId id)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const AshAsset>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const AshAsset>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_id(m_impl, id, resolved, error));
		result = load_ashasset_by_path_async(resolved.info.relative_path);
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}

	std::shared_future<std::shared_ptr<const AshAsset>> AssetDatabase::load_ashasset_by_path_async(const std::filesystem::path& path)
	{
		const auto empty_future = make_ready_future(std::shared_ptr<const AshAsset>{});
		ASH_PROCESS_GUARD_RETURN(std::shared_future<std::shared_ptr<const AshAsset>>, result, empty_future, empty_future);
		ASH_PROCESS_ERROR(m_impl);

		ResolvedAssetInfo resolved{};
		std::string error{};
		ASH_PROCESS_ERROR(resolve_asset_by_path(m_impl, path, resolved, error));

		std::shared_ptr<std::promise<std::shared_ptr<const AshAsset>>> ashasset_promise{};
		std::filesystem::path ashasset_relative_path{};
		AssetId ashasset_asset_id = 0;
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			const auto cached = m_impl->ashasset_cache.find(resolved.info.id);
			if (cached != m_impl->ashasset_cache.end())
			{
				set_load_success_locked(*m_impl, resolved.info.id);
				result = make_ready_future(std::shared_ptr<const AshAsset>(cached->second));
				break;
			}

			std::string cached_error{};
			if (try_get_cached_load_failure_locked(*m_impl, resolved.info.id, cached_error))
			{
				result = empty_future;
				break;
			}

			const auto inflight = m_impl->inflight_ashasset_loads.find(resolved.info.id);
			if (inflight != m_impl->inflight_ashasset_loads.end())
			{
				result = inflight->second;
				break;
			}

			auto promise = std::make_shared<std::promise<std::shared_ptr<const AshAsset>>>();
			result = promise->get_future().share();
			m_impl->inflight_ashasset_loads[resolved.info.id] = result;
			set_load_loading_locked(*m_impl, resolved.info.id);
			ashasset_promise = std::move(promise);
			ashasset_relative_path = resolved.info.relative_path;
			ashasset_asset_id = resolved.info.id;
		}

		if (ashasset_promise)
		{
			AssetDatabase database(m_impl);
			dispatch_background_task("AssetDatabase::load_ashasset_by_path_async", [database, relative_path = std::move(ashasset_relative_path), impl = m_impl, asset_id = ashasset_asset_id, promise = std::move(ashasset_promise)]() mutable -> void
			{
				std::shared_ptr<const AshAsset> asset{};
				try
				{
					if (!database.load_ashasset_by_path(relative_path, asset))
					{
						asset.reset();
					}
				}
				catch (...)
				{
					asset.reset();
				}
				promise->set_value(asset);

				std::scoped_lock<std::mutex> lock(impl->mutex);
				impl->inflight_ashasset_loads.erase(asset_id);
			});
		}
		ASH_PROCESS_GUARD_RETURN_END(result, empty_future);
	}
}
