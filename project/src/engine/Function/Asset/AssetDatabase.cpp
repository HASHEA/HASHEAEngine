#include "AssetDatabase.h"

#include "Base/hthreading.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/VegetationCodec.h"
#include "Function/Render/Material.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>
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
			uint64_t catalog_generation = 0;
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
			if (ext == ".ashterrain")
			{
				return AssetType::Terrain;
			}
			if (ext == ".ashvegetation")
			{
				return AssetType::Species;
			}
			if (ext == ".ashvegetationlayer")
			{
				return AssetType::Layer;
			}
			if (ext == ".ashvegetationchunk")
			{
				return AssetType::Chunk;
			}
			return AssetType::Binary;
		}

		static auto normalize_vegetation_diagnostic(std::string error) -> std::string
		{
			auto is_ascii_space = [](const unsigned char value) -> bool
			{
				return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
					value == '\f' || value == '\v';
			};
			size_t first = 0;
			while (first < error.size() && is_ascii_space(static_cast<unsigned char>(error[first])))
			{
				++first;
			}
			size_t last = error.size();
			while (last > first && is_ascii_space(static_cast<unsigned char>(error[last - 1])))
			{
				--last;
			}
			error = error.substr(first, last - first);
			std::replace(error.begin(), error.end(), '\\', '/');
			return error.empty() ? std::string("Vegetation asset load failed.") : error;
		}

		static auto vegetation_budget_equal(
			const VegetationLoadBudget& lhs,
			const VegetationLoadBudget& rhs) -> bool
		{
			return lhs.max_file_bytes == rhs.max_file_bytes &&
				lhs.max_payload_bytes == rhs.max_payload_bytes &&
				lhs.max_decoded_bytes == rhs.max_decoded_bytes &&
				lhs.max_palette_records == rhs.max_palette_records &&
				lhs.max_tile_records == rhs.max_tile_records &&
				lhs.max_instance_records == rhs.max_instance_records;
		}

		static auto vegetation_failure_rank(VegetationAssetLoadFailure failure) -> uint8_t
		{
			switch (failure)
			{
			case VegetationAssetLoadFailure::InvalidData:
				return 3;
			case VegetationAssetLoadFailure::Io:
				return 2;
			case VegetationAssetLoadFailure::Missing:
				return 1;
			default:
				return 0;
			}
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

	VegetationAssetInFlightAdmissionDecision decide_vegetation_asset_in_flight_admission(
		const VegetationAssetInFlightAdmissionInput& input)
	{
		if (!input.has_existing ||
			input.requested_type != input.existing_type ||
			input.requested_id != input.existing_id ||
			input.requested_epoch != input.existing_epoch ||
			!vegetation_budget_equal(input.requested_budget, input.existing_budget))
		{
			return VegetationAssetInFlightAdmissionDecision::LaunchNew;
		}
		return VegetationAssetInFlightAdmissionDecision::JoinExisting;
	}

	VegetationCatalogPublicationDecision decide_vegetation_catalog_publication(
		const VegetationCatalogPublicationInput& input)
	{
		if (input.captured_epoch != input.current_epoch ||
			input.captured_root.lexically_normal() != input.current_root.lexically_normal())
		{
			return VegetationCatalogPublicationDecision::DiscardStale;
		}
		switch (input.scan_outcome)
		{
		case VegetationCatalogScanOutcome::Succeeded:
			return VegetationCatalogPublicationDecision::PublishReplacement;
		case VegetationCatalogScanOutcome::InvalidRoot:
			return VegetationCatalogPublicationDecision::ResetInvalidRoot;
		case VegetationCatalogScanOutcome::Failed:
		default:
			return VegetationCatalogPublicationDecision::KeepLastKnownGood;
		}
	}

	VegetationAssetCompletionPublicationDecision decide_vegetation_asset_completion_publication(
		const VegetationAssetCompletionPublicationInput& input)
	{
		VegetationAssetCompletionPublicationDecision decision{};
		if (input.captured_epoch != input.current_epoch)
		{
			return decision;
		}

		decision.erase_matching_in_flight =
			input.captured_request_token == input.current_in_flight_token;
		decision.publish_cache = input.completion_has_asset &&
			input.completion_failure == VegetationAssetLoadFailure::None;

		if (decision.publish_cache)
		{
			decision.publish_global_state = input.current_global_state != AssetLoadState::Loaded ||
				input.current_global_failure != VegetationAssetLoadFailure::None ||
				!input.current_global_error.empty();
			decision.global_state = AssetLoadState::Loaded;
			decision.global_failure = VegetationAssetLoadFailure::None;
			decision.global_error.clear();
			return decision;
		}

		const uint8_t completion_rank = vegetation_failure_rank(input.completion_failure);
		if (completion_rank == 0 || input.current_global_state == AssetLoadState::Loaded)
		{
			return decision;
		}

		const uint8_t current_rank = vegetation_failure_rank(input.current_global_failure);
		const std::string completion_error = normalize_vegetation_diagnostic(input.completion_error);
		const std::string current_error = input.current_global_error.empty() ?
			std::string{} : normalize_vegetation_diagnostic(input.current_global_error);
		if (completion_rank < current_rank ||
			(completion_rank == current_rank && !current_error.empty() && completion_error >= current_error))
		{
			return decision;
		}

		decision.publish_global_state = true;
		decision.global_state = input.completion_failure == VegetationAssetLoadFailure::Missing ?
			AssetLoadState::Missing : AssetLoadState::Failed;
		decision.global_failure = input.completion_failure;
		decision.global_error = completion_error;
		return decision;
	}

	namespace Detail
	{
		bool read_vegetation_bounded_stream_snapshot(
			std::istream& input,
			const uint64_t max_file_bytes,
			std::vector<uint8_t>& out_bytes,
			std::string* out_error)
		{
			auto fail_snapshot = [&](const char* message) -> bool
			{
				if (out_error)
				{
					*out_error = message ? message : "Vegetation bounded stream read failed.";
				}
				return false;
			};

			try
			{
				constexpr size_t k_read_chunk_bytes = 64u * 1024u;
				std::array<char, k_read_chunk_bytes> scratch{};
				std::vector<uint8_t> bytes{};
				const uint64_t reserve_bytes = std::min<uint64_t>(max_file_bytes, k_read_chunk_bytes);
				bytes.reserve(static_cast<size_t>(reserve_bytes));

				uint64_t total = 0;
				while (total < max_file_bytes)
				{
					const uint64_t remaining = max_file_bytes - total;
					const std::streamsize request = static_cast<std::streamsize>(
						std::min<uint64_t>(remaining, k_read_chunk_bytes));
					input.read(scratch.data(), request);
					const std::streamsize read_count = input.gcount();
					if (read_count < 0 ||
						static_cast<uint64_t>(read_count) > remaining ||
						static_cast<size_t>(read_count) > bytes.max_size() - bytes.size())
					{
						return fail_snapshot("Vegetation bounded stream size overflow.");
					}
					bytes.insert(bytes.end(), scratch.data(), scratch.data() + read_count);
					total += static_cast<uint64_t>(read_count);

					if (read_count < request)
					{
						if (input.bad())
						{
							return fail_snapshot("Vegetation bounded stream I/O failure.");
						}
						if (!input.eof())
						{
							return fail_snapshot("Vegetation bounded stream ended short without EOF.");
						}

						// basic_istream::read sets eofbit/failbit on every short bulk read. Clear
						// those automatic flags, then probe the same stream before publishing.
						input.clear();
						char extra = 0;
						input.get(extra);
						if (input.gcount() == 1)
						{
							return fail_snapshot("Vegetation bounded stream ended short without EOF.");
						}
						if (input.bad() || !input.eof())
						{
							return fail_snapshot("Vegetation bounded stream I/O failure after short read.");
						}

						out_bytes = std::move(bytes);
						if (out_error) out_error->clear();
						return true;
					}
				}

				char extra = 0;
				input.get(extra);
				if (input.gcount() == 1)
				{
					return fail_snapshot("Vegetation bounded stream exceeds maximum byte count.");
				}
				if (input.bad() || !input.eof())
				{
					return fail_snapshot("Vegetation bounded stream I/O failure after byte limit.");
				}

				out_bytes = std::move(bytes);
				if (out_error) out_error->clear();
				return true;
			}
			catch (const std::exception& exception)
			{
				if (out_error)
				{
					*out_error = std::string("Vegetation bounded stream read failed: ") + exception.what();
				}
				return false;
			}
			catch (...)
			{
				return fail_snapshot("Vegetation bounded stream read failed with an unknown exception.");
			}
		}
	}

	namespace
	{
		struct VegetationInFlightKey
		{
			AssetType type = AssetType::Unknown;
			AssetId id = 0;
			uint64_t epoch = 0;
			VegetationLoadBudget budget{};

			bool operator==(const VegetationInFlightKey& other) const
			{
				return type == other.type && id == other.id && epoch == other.epoch &&
					vegetation_budget_equal(budget, other.budget);
			}
		};

		struct VegetationInFlightKeyHash
		{
			size_t operator()(const VegetationInFlightKey& key) const
			{
				auto combine = [](size_t seed, const uint64_t value) -> size_t
				{
					return seed ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
						(seed << 6u) + (seed >> 2u));
				};
				size_t hash = combine(0, static_cast<uint8_t>(key.type));
				hash = combine(hash, key.id);
				hash = combine(hash, key.epoch);
				hash = combine(hash, key.budget.max_file_bytes);
				hash = combine(hash, key.budget.max_payload_bytes);
				hash = combine(hash, key.budget.max_decoded_bytes);
				hash = combine(hash, key.budget.max_palette_records);
				hash = combine(hash, key.budget.max_tile_records);
				return combine(hash, key.budget.max_instance_records);
			}
		};

		struct VegetationResolvedDependency
		{
			std::filesystem::path path{};
			VegetationId id{};
			VegetationSha256 digest{};
			std::shared_ptr<const VegetationSpecies> asset{};
			VegetationLoadCost cost{};
		};

		template<typename T>
		struct VegetationSuccessCacheEntry
		{
			std::shared_ptr<const T> asset{};
			VegetationLoadCost cost{};
			std::vector<VegetationResolvedDependency> dependencies{};
		};

		template<typename T>
		struct VegetationInFlightEntry
		{
			uint64_t request_token = 0;
			std::shared_future<VegetationAssetLoadResult<T>> future{};
		};

		template<typename T>
		struct VegetationLoadedValue
		{
			VegetationAssetLoadResult<T> result{};
			std::vector<VegetationResolvedDependency> dependencies{};
		};

		template<typename T>
		static auto set_vegetation_io_failure_noexcept(
			VegetationLoadedValue<T>& loaded,
			const char* diagnostic = nullptr) noexcept -> void
		{
			loaded.dependencies.clear();
			loaded.result.state = AssetLoadState::Failed;
			loaded.result.failure = VegetationAssetLoadFailure::Io;
			loaded.result.asset.reset();
			loaded.result.cost = {};
			try
			{
				loaded.result.error = diagnostic && diagnostic[0] != '\0' ?
					normalize_vegetation_diagnostic(
						std::string("Vegetation worker I/O failed: ") + diagnostic) :
					std::string("Vegetation worker I/O failed.");
			}
			catch (...)
			{
				// Keep the emergency diagnostic short enough for the string's small buffer.
				// Failure publication must never expose an empty diagnostic.
				try
				{
					loaded.result.error = "I/O failed.";
				}
				catch (...)
				{
				}
			}
		}

		static auto vegetation_cost_fits_budget(
			const VegetationLoadCost& cost,
			const VegetationLoadBudget& budget) -> bool
		{
			return cost.file_bytes <= budget.max_file_bytes &&
				cost.payload_bytes <= budget.max_payload_bytes &&
				cost.decoded_bytes <= budget.max_decoded_bytes &&
				cost.palette_records <= budget.max_palette_records &&
				cost.tile_records <= budget.max_tile_records &&
				cost.instance_records <= budget.max_instance_records;
		}

		static auto is_vegetation_asset_type(const AssetType type) -> bool
		{
			return type == AssetType::Species || type == AssetType::Layer || type == AssetType::Chunk;
		}

		static auto is_opaque_vegetation_asset_type(const AssetType type) -> bool
		{
			return type == AssetType::Layer || type == AssetType::Chunk;
		}

		static auto error_indicates_budget_failure(const std::string& error) -> bool
		{
			return error == "Vegetation bounded stream exceeds maximum byte count.";
		}

		static auto is_explicit_vegetation_codec_budget_rejection(
			const std::string& error) -> bool
		{
			return error == "Vegetation Species file size exceeds its budget." ||
				error == "Vegetation Species decoded budget exceeded before DTO ownership." ||
				error == "Vegetation Layer file budget exceeded before ownership." ||
				error == "Vegetation Layer count or payload budget exceeded before ownership." ||
				error == "Vegetation Layer decoded budget exceeded before ownership." ||
				error == "Vegetation Layer decoded budget exceeded." ||
				error == "Vegetation Chunk file budget exceeded before ownership." ||
				error == "Vegetation Chunk count or payload budget exceeded before ownership." ||
				error == "Vegetation Chunk decoded budget exceeded before ownership." ||
				error == "Vegetation Chunk decoded budget exceeded.";
		}

		template<typename T>
		static auto make_vegetation_failure_result(
			const VegetationAssetLoadFailure failure,
			std::string error) -> VegetationAssetLoadResult<T>
		{
			VegetationAssetLoadResult<T> result{};
			result.failure = failure;
			result.state = failure == VegetationAssetLoadFailure::Missing ?
				AssetLoadState::Missing : AssetLoadState::Failed;
			result.error = normalize_vegetation_diagnostic(std::move(error));
			return result;
		}

		template<typename T>
		static auto make_vegetation_loaded_result(
			std::shared_ptr<const T> asset,
			const VegetationLoadCost& cost) -> VegetationAssetLoadResult<T>
		{
			VegetationAssetLoadResult<T> result{};
			result.state = AssetLoadState::Loaded;
			result.failure = VegetationAssetLoadFailure::None;
			result.asset = std::move(asset);
			result.cost = cost;
			return result;
		}

		template<typename T>
		static auto read_cached_vegetation_result(
			const VegetationSuccessCacheEntry<T>& cached,
			const VegetationLoadBudget& budget) -> VegetationAssetLoadResult<T>
		{
			if (!vegetation_cost_fits_budget(cached.cost, budget))
			{
				return make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::BudgetExceeded,
					"Vegetation outer asset exceeds the caller load budget.");
			}
			for (const VegetationResolvedDependency& dependency : cached.dependencies)
			{
				if (!vegetation_cost_fits_budget(dependency.cost, budget))
				{
					return make_vegetation_failure_result<T>(
						VegetationAssetLoadFailure::BudgetExceeded,
						"Vegetation Species dependency exceeds the caller load budget.");
				}
			}
			return make_vegetation_loaded_result<T>(cached.asset, cached.cost);
		}
	}

	class AssetDatabase::Impl
	{
	public:
		struct InflightTerrainLoad
		{
			uint64_t serial = 0;
			std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> future{};
		};

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
		std::unordered_map<TerrainAssetId, std::shared_ptr<const TerrainAssetSnapshot>> terrain_cache{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const Mesh>>> inflight_mesh_loads{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const Model>>> inflight_model_loads{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const MaterialInterface>>> inflight_material_loads{};
		std::unordered_map<AssetId, std::shared_future<std::shared_ptr<const AshAsset>>> inflight_ashasset_loads{};
		std::unordered_map<TerrainAssetId, InflightTerrainLoad> inflight_terrain_loads{};
		std::unordered_map<TerrainAssetId, uint64_t> terrain_load_serial_by_id{};
		uint64_t next_terrain_load_serial = 0;
		uint64_t catalog_generation = 0;
		uint64_t vegetation_catalog_epoch = 0;
		uint64_t next_vegetation_request_token = 0;
		std::unordered_map<AssetId, VegetationAssetLoadFailure> vegetation_global_failure_by_id{};
		std::unordered_map<AssetId, VegetationSuccessCacheEntry<VegetationSpecies>> vegetation_species_cache{};
		std::unordered_map<AssetId, VegetationSuccessCacheEntry<VegetationLayerSnapshot>> vegetation_layer_cache{};
		std::unordered_map<AssetId, VegetationSuccessCacheEntry<VegetationChunk>> vegetation_chunk_cache{};
		std::unordered_map<VegetationInFlightKey, VegetationInFlightEntry<VegetationSpecies>, VegetationInFlightKeyHash> inflight_vegetation_species_loads{};
		std::unordered_map<VegetationInFlightKey, VegetationInFlightEntry<VegetationLayerSnapshot>, VegetationInFlightKeyHash> inflight_vegetation_layer_loads{};
		std::unordered_map<VegetationInFlightKey, VegetationInFlightEntry<VegetationChunk>, VegetationInFlightKeyHash> inflight_vegetation_chunk_loads{};
		std::string last_error{};
		mutable std::mutex mutex{};
	};

	class VegetationAssetResolverSnapshot::Impl
	{
	public:
		std::filesystem::path root_path{};
		std::unordered_map<std::string, AssetInfo> catalog_by_key{};
		std::unordered_map<std::string, VegetationSuccessCacheEntry<VegetationSpecies>> species_cache{};
		mutable std::mutex mutex{};
	};

	namespace
	{
		enum class VegetationSnapshotReadOutcome : uint8_t
		{
			Succeeded = 0,
			Missing,
			BudgetExceeded,
			Io
		};

		static auto clear_vegetation_shared_state_locked(AssetDatabase::Impl& impl) -> void
		{
			impl.vegetation_species_cache.clear();
			impl.vegetation_layer_cache.clear();
			impl.vegetation_chunk_cache.clear();
			impl.inflight_vegetation_species_loads.clear();
			impl.inflight_vegetation_layer_loads.clear();
			impl.inflight_vegetation_chunk_loads.clear();
			impl.vegetation_global_failure_by_id.clear();
		}

		static auto clear_all_asset_shared_state_locked(AssetDatabase::Impl& impl) -> void
		{
			impl.load_info_by_id.clear();
			impl.mesh_cache.clear();
			impl.model_cache.clear();
			impl.material_cache.clear();
			impl.ashasset_cache.clear();
			impl.terrain_cache.clear();
			impl.inflight_mesh_loads.clear();
			impl.inflight_model_loads.clear();
			impl.inflight_material_loads.clear();
			impl.inflight_ashasset_loads.clear();
			impl.inflight_terrain_loads.clear();
			impl.terrain_load_serial_by_id.clear();
			clear_vegetation_shared_state_locked(impl);
		}

		static auto bump_vegetation_epoch_locked(AssetDatabase::Impl& impl) -> void
		{
			++impl.vegetation_catalog_epoch;
			if (impl.vegetation_catalog_epoch == 0)
			{
				++impl.vegetation_catalog_epoch;
			}
		}

		static auto next_vegetation_token_locked(AssetDatabase::Impl& impl) -> uint64_t
		{
			++impl.next_vegetation_request_token;
			if (impl.next_vegetation_request_token == 0)
			{
				++impl.next_vegetation_request_token;
			}
			return impl.next_vegetation_request_token;
		}

		static auto normalize_resolver_path(
			const std::filesystem::path& root_path,
			const std::filesystem::path& path,
			std::string& out_key) -> bool
		{
			std::filesystem::path relative = path.lexically_normal();
			if (path.is_absolute())
			{
				std::error_code error{};
				relative = std::filesystem::relative(path, root_path, error).lexically_normal();
				if (error || relative.empty() || is_path_outside_root(relative))
				{
					return false;
				}
			}
			if (relative.empty() || relative.is_absolute() || is_path_outside_root(relative))
			{
				return false;
			}
			out_key = normalize_asset_key(relative);
			return !out_key.empty();
		}

		static auto read_vegetation_file_snapshot(
			const std::filesystem::path& absolute_path,
			const uint64_t max_file_bytes,
			std::vector<uint8_t>& out_bytes,
			std::string& out_error) -> VegetationSnapshotReadOutcome
		{
			std::ifstream input(absolute_path, std::ios::binary);
			if (!input.is_open())
			{
				std::error_code exists_error{};
				const bool exists = std::filesystem::exists(absolute_path, exists_error);
				out_error = (!exists && !exists_error) ?
					"Vegetation asset file is missing." :
					"Vegetation asset file could not be opened.";
				return (!exists && !exists_error) ?
					VegetationSnapshotReadOutcome::Missing : VegetationSnapshotReadOutcome::Io;
			}

			std::vector<uint8_t> bytes{};
			if (!Detail::read_vegetation_bounded_stream_snapshot(
				input, max_file_bytes, bytes, &out_error))
			{
				return error_indicates_budget_failure(out_error) ?
					VegetationSnapshotReadOutcome::BudgetExceeded : VegetationSnapshotReadOutcome::Io;
			}
			out_bytes = std::move(bytes);
			out_error.clear();
			return VegetationSnapshotReadOutcome::Succeeded;
		}

		static auto make_resolver_snapshot_locked(
			const AssetDatabase::Impl& impl) -> std::shared_ptr<const VegetationAssetResolverSnapshot>
		{
			auto resolver_impl = std::make_shared<VegetationAssetResolverSnapshot::Impl>();
			resolver_impl->root_path = impl.root_path;
			resolver_impl->catalog_by_key.reserve(impl.index_by_key.size());
			for (const auto& indexed : impl.index_by_key)
			{
				resolver_impl->catalog_by_key.emplace(indexed.first, impl.assets[indexed.second]);
			}
			return std::shared_ptr<const VegetationAssetResolverSnapshot>(
				new VegetationAssetResolverSnapshot(std::move(resolver_impl)));
		}

		static auto set_last_error_locked(AssetDatabase::Impl& impl, const std::string& error) -> void;
		static auto set_load_failed_locked(AssetDatabase::Impl& impl, AssetId id, const std::string& error) -> void;
		static auto set_load_loading_locked(AssetDatabase::Impl& impl, AssetId id) -> void;
		static auto set_load_success_locked(AssetDatabase::Impl& impl, AssetId id) -> void;
		static auto try_get_cached_load_failure_locked(AssetDatabase::Impl& impl, AssetId id, std::string& out_error) -> bool;
		static auto validate_terrain_asset_locked(
			AssetDatabase::Impl& impl,
			const ResolvedAssetInfo& resolved) -> bool;
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
			if (is_vegetation_asset_type(resolved.info.type))
			{
				out_error = "Vegetation assets require a typed AssetDatabase loader.";
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

	VegetationAssetResolverSnapshot::VegetationAssetResolverSnapshot(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}

	VegetationAssetLoadResult<VegetationSpecies>
	VegetationAssetResolverSnapshot::load_species_by_path(
		const std::filesystem::path& path,
		const VegetationLoadBudget& budget) const
	{
		if (!m_impl)
		{
			return make_vegetation_failure_result<VegetationSpecies>(
				VegetationAssetLoadFailure::Missing,
				"Vegetation resolver snapshot is unavailable.");
		}

		try
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			std::string key{};
			if (!normalize_resolver_path(m_impl->root_path, path, key))
			{
				return make_vegetation_failure_result<VegetationSpecies>(
					VegetationAssetLoadFailure::Missing,
					"Vegetation Species path is outside the resolver snapshot root.");
			}

			const auto indexed = m_impl->catalog_by_key.find(key);
			if (indexed == m_impl->catalog_by_key.end())
			{
				return make_vegetation_failure_result<VegetationSpecies>(
					VegetationAssetLoadFailure::Missing,
					"Vegetation Species path was not found in the resolver snapshot.");
			}
			if (indexed->second.is_directory || indexed->second.type != AssetType::Species)
			{
				return make_vegetation_failure_result<VegetationSpecies>(
					VegetationAssetLoadFailure::WrongType,
					"Vegetation resolver path is not a Species asset.");
			}

			const auto cached = m_impl->species_cache.find(key);
			if (cached != m_impl->species_cache.end())
			{
				return read_cached_vegetation_result(cached->second, budget);
			}

			std::vector<uint8_t> bytes{};
			std::string error{};
			const VegetationSnapshotReadOutcome read_outcome = read_vegetation_file_snapshot(
				m_impl->root_path / indexed->second.relative_path,
				budget.max_file_bytes,
				bytes,
				error);
			if (read_outcome != VegetationSnapshotReadOutcome::Succeeded)
			{
				const VegetationAssetLoadFailure failure =
					read_outcome == VegetationSnapshotReadOutcome::Missing ? VegetationAssetLoadFailure::Missing :
					read_outcome == VegetationSnapshotReadOutcome::BudgetExceeded ? VegetationAssetLoadFailure::BudgetExceeded :
					VegetationAssetLoadFailure::Io;
				return make_vegetation_failure_result<VegetationSpecies>(failure, std::move(error));
			}

			VegetationSpecies species{};
			VegetationLoadCost cost{};
			if (!decode_vegetation_species(bytes, budget, species, &error, &cost))
			{
				const VegetationAssetLoadFailure failure =
					is_explicit_vegetation_codec_budget_rejection(error) ?
					VegetationAssetLoadFailure::BudgetExceeded : VegetationAssetLoadFailure::InvalidData;
				return make_vegetation_failure_result<VegetationSpecies>(
					failure,
					std::move(error));
			}

			VegetationSuccessCacheEntry<VegetationSpecies> entry{};
			entry.asset = std::make_shared<const VegetationSpecies>(std::move(species));
			entry.cost = cost;
			const auto inserted = m_impl->species_cache.emplace(key, std::move(entry));
			return make_vegetation_loaded_result<VegetationSpecies>(
				inserted.first->second.asset,
				inserted.first->second.cost);
		}
		catch (const std::exception& exception)
		{
			return make_vegetation_failure_result<VegetationSpecies>(
				VegetationAssetLoadFailure::Io,
				std::string("Vegetation Species resolver failed: ") + exception.what());
		}
		catch (...)
		{
			return make_vegetation_failure_result<VegetationSpecies>(
				VegetationAssetLoadFailure::Io,
				"Vegetation Species resolver failed with an unknown exception.");
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
			out_resolved.catalog_generation = impl->catalog_generation;
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
			out_resolved.catalog_generation = impl->catalog_generation;
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

		struct VegetationTypedRequestContext
		{
			uint64_t catalog_epoch = 0;
			ResolvedAssetInfo resolved{};
			std::shared_ptr<const VegetationAssetResolverSnapshot> resolver{};
		};

		static auto map_dependency_failure(
			const VegetationAssetLoadFailure failure) -> VegetationAssetLoadFailure
		{
			switch (failure)
			{
			case VegetationAssetLoadFailure::BudgetExceeded:
				return VegetationAssetLoadFailure::BudgetExceeded;
			case VegetationAssetLoadFailure::Io:
				return VegetationAssetLoadFailure::Io;
			case VegetationAssetLoadFailure::None:
				return VegetationAssetLoadFailure::None;
			case VegetationAssetLoadFailure::WrongType:
			case VegetationAssetLoadFailure::Missing:
			case VegetationAssetLoadFailure::InvalidData:
			default:
				return VegetationAssetLoadFailure::InvalidData;
			}
		}

		static auto resolve_vegetation_species_dependency(
			const std::shared_ptr<const VegetationAssetResolverSnapshot>& resolver,
			const VegetationPaletteEntry& palette,
			const VegetationLoadBudget& budget,
			VegetationResolvedDependency& out_dependency,
			std::string& out_error) -> VegetationAssetLoadFailure
		{
			if (!resolver)
			{
				out_error = "Vegetation Species resolver snapshot is unavailable.";
				return VegetationAssetLoadFailure::Io;
			}

			const VegetationAssetLoadResult<VegetationSpecies> loaded =
				resolver->load_species_by_path(palette.species_asset_path, budget);
			if (!loaded.asset || loaded.failure != VegetationAssetLoadFailure::None)
			{
				out_error = std::string("Vegetation Species dependency '") +
					palette.species_asset_path + "' failed: " + loaded.error;
				return map_dependency_failure(loaded.failure);
			}
			if (loaded.asset->species_id != palette.species_id)
			{
				out_error = std::string("Vegetation Species dependency '") +
					palette.species_asset_path + "' id does not match the palette record.";
				return VegetationAssetLoadFailure::InvalidData;
			}

			std::vector<uint8_t> canonical{};
			std::string encode_error{};
			if (!encode_vegetation_species(*loaded.asset, canonical, &encode_error))
			{
				out_error = std::string("Vegetation Species dependency '") +
					palette.species_asset_path + "' could not be canonicalized: " + encode_error;
				return VegetationAssetLoadFailure::InvalidData;
			}
			const VegetationSha256 digest = vegetation_sha256(canonical.data(), canonical.size());
			if (digest != palette.species_sha256)
			{
				out_error = std::string("Vegetation Species dependency '") +
					palette.species_asset_path + "' digest does not match the palette record.";
				return VegetationAssetLoadFailure::InvalidData;
			}

			out_dependency.path = palette.species_asset_path;
			out_dependency.id = palette.species_id;
			out_dependency.digest = digest;
			out_dependency.asset = loaded.asset;
			out_dependency.cost = loaded.cost;
			out_error.clear();
			return VegetationAssetLoadFailure::None;
		}

		static auto load_vegetation_species_value(
			const VegetationTypedRequestContext& context,
			const VegetationLoadBudget& budget) -> VegetationLoadedValue<VegetationSpecies>
		{
			VegetationLoadedValue<VegetationSpecies> loaded{};
			loaded.result = context.resolver->load_species_by_path(
				context.resolved.info.relative_path, budget);
			return loaded;
		}

		static auto load_vegetation_layer_value(
			const VegetationTypedRequestContext& context,
			const VegetationLoadBudget& budget) -> VegetationLoadedValue<VegetationLayerSnapshot>
		{
			VegetationLoadedValue<VegetationLayerSnapshot> loaded{};
			std::vector<uint8_t> bytes{};
			std::string error{};
			const VegetationSnapshotReadOutcome read_outcome = read_vegetation_file_snapshot(
				context.resolved.absolute_path, budget.max_file_bytes, bytes, error);
			if (read_outcome != VegetationSnapshotReadOutcome::Succeeded)
			{
				const VegetationAssetLoadFailure failure =
					read_outcome == VegetationSnapshotReadOutcome::Missing ? VegetationAssetLoadFailure::Missing :
					read_outcome == VegetationSnapshotReadOutcome::BudgetExceeded ? VegetationAssetLoadFailure::BudgetExceeded :
					VegetationAssetLoadFailure::Io;
				loaded.result = make_vegetation_failure_result<VegetationLayerSnapshot>(failure, std::move(error));
				return loaded;
			}

			VegetationLayerSnapshot layer{};
			VegetationLoadCost cost{};
			if (!decode_vegetation_layer(bytes, budget, layer, &error, &cost))
			{
				const VegetationAssetLoadFailure failure =
					is_explicit_vegetation_codec_budget_rejection(error) ?
					VegetationAssetLoadFailure::BudgetExceeded : VegetationAssetLoadFailure::InvalidData;
				loaded.result = make_vegetation_failure_result<VegetationLayerSnapshot>(failure, std::move(error));
				return loaded;
			}

			loaded.dependencies.reserve(layer.palette.size());
			for (const VegetationPaletteEntry& palette : layer.palette)
			{
				VegetationResolvedDependency dependency{};
				const VegetationAssetLoadFailure failure = resolve_vegetation_species_dependency(
					context.resolver, palette, budget, dependency, error);
				if (failure != VegetationAssetLoadFailure::None)
				{
					loaded.dependencies.clear();
					loaded.result = make_vegetation_failure_result<VegetationLayerSnapshot>(
						failure, std::move(error));
					return loaded;
				}
				loaded.dependencies.push_back(std::move(dependency));
			}

			loaded.result = make_vegetation_loaded_result<VegetationLayerSnapshot>(
				std::make_shared<const VegetationLayerSnapshot>(std::move(layer)), cost);
			return loaded;
		}

		static auto load_vegetation_chunk_value(
			const VegetationTypedRequestContext& context,
			const VegetationLoadBudget& budget) -> VegetationLoadedValue<VegetationChunk>
		{
			VegetationLoadedValue<VegetationChunk> loaded{};
			std::vector<uint8_t> bytes{};
			std::string error{};
			const VegetationSnapshotReadOutcome read_outcome = read_vegetation_file_snapshot(
				context.resolved.absolute_path, budget.max_file_bytes, bytes, error);
			if (read_outcome != VegetationSnapshotReadOutcome::Succeeded)
			{
				const VegetationAssetLoadFailure failure =
					read_outcome == VegetationSnapshotReadOutcome::Missing ? VegetationAssetLoadFailure::Missing :
					read_outcome == VegetationSnapshotReadOutcome::BudgetExceeded ? VegetationAssetLoadFailure::BudgetExceeded :
					VegetationAssetLoadFailure::Io;
				loaded.result = make_vegetation_failure_result<VegetationChunk>(failure, std::move(error));
				return loaded;
			}

			VegetationChunk chunk{};
			VegetationLoadCost cost{};
			if (!decode_vegetation_chunk(bytes, budget, chunk, &error, &cost))
			{
				const VegetationAssetLoadFailure failure =
					is_explicit_vegetation_codec_budget_rejection(error) ?
					VegetationAssetLoadFailure::BudgetExceeded : VegetationAssetLoadFailure::InvalidData;
				loaded.result = make_vegetation_failure_result<VegetationChunk>(failure, std::move(error));
				return loaded;
			}

			loaded.dependencies.reserve(chunk.species.size());
			for (const VegetationPaletteEntry& palette : chunk.species)
			{
				VegetationResolvedDependency dependency{};
				const VegetationAssetLoadFailure failure = resolve_vegetation_species_dependency(
					context.resolver, palette, budget, dependency, error);
				if (failure != VegetationAssetLoadFailure::None)
				{
					loaded.dependencies.clear();
					loaded.result = make_vegetation_failure_result<VegetationChunk>(
						failure, std::move(error));
					return loaded;
				}
				loaded.dependencies.push_back(std::move(dependency));
			}

			for (const VegetationChunkInstance& instance : chunk.instances)
			{
				if (instance.species_index >= loaded.dependencies.size() ||
					instance.candidate_ordinal >=
						loaded.dependencies[instance.species_index].asset->placement.candidates_per_cell)
				{
					loaded.dependencies.clear();
					loaded.result = make_vegetation_failure_result<VegetationChunk>(
						VegetationAssetLoadFailure::InvalidData,
						"Vegetation Chunk candidate ordinal exceeds the resolved Species contract.");
					return loaded;
				}
			}

			loaded.result = make_vegetation_loaded_result<VegetationChunk>(
				std::make_shared<const VegetationChunk>(std::move(chunk)), cost);
			return loaded;
		}

		template<typename T, typename TCacheMap>
		static auto publish_vegetation_completion_locked(
			AssetDatabase::Impl& impl,
			const AssetId asset_id,
			const uint64_t captured_epoch,
			const uint64_t captured_request_token,
			const uint64_t current_in_flight_token,
			const VegetationLoadedValue<T>& loaded,
			TCacheMap& cache) -> VegetationAssetCompletionPublicationDecision
		{
			const auto load_info = impl.load_info_by_id.find(asset_id);
			const AssetLoadState current_state = load_info == impl.load_info_by_id.end() ?
				AssetLoadState::Unloaded : load_info->second.state;
			const std::string current_error = load_info == impl.load_info_by_id.end() ?
				std::string{} : load_info->second.error;
			const auto current_failure = impl.vegetation_global_failure_by_id.find(asset_id);
			const VegetationAssetLoadFailure current_global_failure =
				current_failure == impl.vegetation_global_failure_by_id.end() ?
				VegetationAssetLoadFailure::None : current_failure->second;

			const VegetationAssetCompletionPublicationDecision decision =
				decide_vegetation_asset_completion_publication({
					captured_epoch,
					impl.vegetation_catalog_epoch,
					captured_request_token,
					current_in_flight_token,
					current_state,
					current_global_failure,
					current_error,
					loaded.result.failure,
					loaded.result.error,
					loaded.result.asset != nullptr });
			if (decision.publish_cache)
			{
				VegetationSuccessCacheEntry<T> entry{};
				entry.asset = loaded.result.asset;
				entry.cost = loaded.result.cost;
				entry.dependencies = loaded.dependencies;
				cache[asset_id] = std::move(entry);
			}
			if (decision.publish_global_state)
			{
				impl.load_info_by_id[asset_id] = { decision.global_state, decision.global_error };
				if (decision.global_failure == VegetationAssetLoadFailure::None)
				{
					impl.vegetation_global_failure_by_id.erase(asset_id);
					impl.last_error.clear();
				}
				else
				{
					impl.vegetation_global_failure_by_id[asset_id] = decision.global_failure;
					impl.last_error = decision.global_error;
				}
			}
			return decision;
		}

		template<typename T, typename TCacheMap, typename ResolveFn, typename LoadFn>
		static auto load_vegetation_sync_common(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const AssetType expected_type,
			const VegetationLoadBudget& budget,
			TCacheMap AssetDatabase::Impl::* cache_member,
			ResolveFn&& resolve,
			LoadFn&& load) -> VegetationAssetLoadResult<T>
		{
			if (!impl)
			{
				return make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::Missing, "AssetDatabase is unavailable.");
			}

			VegetationTypedRequestContext context{};
			bool admitted = false;
			try
			{
				{
					std::scoped_lock<std::mutex> lock(impl->mutex);
					if (!resolve(context.resolved))
					{
						return make_vegetation_failure_result<T>(
							VegetationAssetLoadFailure::Missing,
							"Vegetation asset was not found in the catalog snapshot.");
					}
					if (context.resolved.info.is_directory || context.resolved.info.type != expected_type)
					{
						return make_vegetation_failure_result<T>(
							VegetationAssetLoadFailure::WrongType,
							"Vegetation typed loader received an asset of the wrong type.");
					}
					auto& cache = impl.get()->*cache_member;
					const auto cached = cache.find(context.resolved.info.id);
					if (cached != cache.end())
					{
						return read_cached_vegetation_result(cached->second, budget);
					}
					context.catalog_epoch = impl->vegetation_catalog_epoch;
					admitted = true;
					context.resolver = make_resolver_snapshot_locked(*impl);
				}

				VegetationLoadedValue<T> loaded = load(context, budget);
				{
					std::scoped_lock<std::mutex> lock(impl->mutex);
					auto& cache = impl.get()->*cache_member;
					publish_vegetation_completion_locked(
						*impl,
						context.resolved.info.id,
						context.catalog_epoch,
						0,
						0,
						loaded,
						cache);
				}
				return loaded.result;
			}
			catch (const std::exception& exception)
			{
				VegetationLoadedValue<T> failed{};
				failed.result = make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::Io,
					std::string("Vegetation typed load failed: ") + exception.what());
				if (admitted)
				{
					try
					{
						std::scoped_lock<std::mutex> lock(impl->mutex);
						auto& cache = impl.get()->*cache_member;
						publish_vegetation_completion_locked(
							*impl, context.resolved.info.id, context.catalog_epoch, 0, 0, failed, cache);
					}
					catch (...)
					{
					}
				}
				return failed.result;
			}
			catch (...)
			{
				VegetationLoadedValue<T> failed{};
				failed.result = make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::Io,
					"Vegetation typed load failed with an unknown exception.");
				if (admitted)
				{
					try
					{
						std::scoped_lock<std::mutex> lock(impl->mutex);
						auto& cache = impl.get()->*cache_member;
						publish_vegetation_completion_locked(
							*impl, context.resolved.info.id, context.catalog_epoch, 0, 0, failed, cache);
					}
					catch (...)
					{
					}
				}
				return failed.result;
			}
		}

		template<typename T, typename TCacheMap, typename TInFlightMap, typename ResolveFn, typename LoadFn>
		static auto load_vegetation_async_common(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const AssetType expected_type,
			const VegetationLoadBudget budget,
			TCacheMap AssetDatabase::Impl::* cache_member,
			TInFlightMap AssetDatabase::Impl::* in_flight_member,
			ResolveFn&& resolve,
			LoadFn load,
			const char* debug_name) -> std::shared_future<VegetationAssetLoadResult<T>>
		{
			using Result = VegetationAssetLoadResult<T>;
			struct AsyncRequestState
			{
				VegetationTypedRequestContext context{};
				VegetationInFlightKey key{};
				uint64_t request_token = 0;
				std::shared_ptr<std::promise<Result>> promise{};
				std::shared_future<Result> future{};
				std::atomic_bool completed{ false };
			};
			if (!impl)
			{
				return make_ready_future(make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::Missing, "AssetDatabase is unavailable."));
			}

			try
			{
				auto state = std::make_shared<AsyncRequestState>();
				{
					std::scoped_lock<std::mutex> lock(impl->mutex);
					if (!resolve(state->context.resolved))
					{
						return make_ready_future(make_vegetation_failure_result<T>(
							VegetationAssetLoadFailure::Missing,
							"Vegetation asset was not found in the catalog snapshot."));
					}
					if (state->context.resolved.info.is_directory || state->context.resolved.info.type != expected_type)
					{
						return make_ready_future(make_vegetation_failure_result<T>(
							VegetationAssetLoadFailure::WrongType,
							"Vegetation typed loader received an asset of the wrong type."));
					}

					auto& cache = impl.get()->*cache_member;
					const auto cached = cache.find(state->context.resolved.info.id);
					if (cached != cache.end())
					{
						return make_ready_future(read_cached_vegetation_result(cached->second, budget));
					}

					state->context.catalog_epoch = impl->vegetation_catalog_epoch;
					state->key = {
						expected_type,
						state->context.resolved.info.id,
						state->context.catalog_epoch,
						budget };
					auto& in_flight = impl.get()->*in_flight_member;
					const auto existing = in_flight.find(state->key);
					VegetationAssetInFlightAdmissionInput admission{};
					admission.has_existing = existing != in_flight.end();
					admission.requested_type = state->key.type;
					admission.requested_id = state->key.id;
					admission.requested_epoch = state->key.epoch;
					admission.requested_budget = state->key.budget;
					if (admission.has_existing)
					{
						admission.existing_type = existing->first.type;
						admission.existing_id = existing->first.id;
						admission.existing_epoch = existing->first.epoch;
						admission.existing_budget = existing->first.budget;
					}
					if (decide_vegetation_asset_in_flight_admission(admission) ==
						VegetationAssetInFlightAdmissionDecision::JoinExisting)
					{
						return existing->second.future;
					}

					state->context.resolver = make_resolver_snapshot_locked(*impl);
					state->promise = std::make_shared<std::promise<Result>>();
					state->future = state->promise->get_future().share();
					state->request_token = next_vegetation_token_locked(*impl);
					in_flight.emplace(
						state->key,
						VegetationInFlightEntry<T>{ state->request_token, state->future });
				}

				auto complete = [impl,
					state,
					cache_member,
					in_flight_member](VegetationLoadedValue<T> loaded) noexcept -> void
				{
					bool expected = false;
					if (!state->completed.compare_exchange_strong(
						expected, true, std::memory_order_acq_rel))
					{
						return;
					}

					bool publication_succeeded = false;
					try
					{
						std::scoped_lock<std::mutex> lock(impl->mutex);
						auto& in_flight = impl.get()->*in_flight_member;
						const auto current = in_flight.find(state->key);
						const uint64_t current_token = current == in_flight.end() ? 0 : current->second.request_token;
						auto& cache = impl.get()->*cache_member;
						const VegetationAssetCompletionPublicationDecision decision =
							publish_vegetation_completion_locked(
								*impl,
								state->context.resolved.info.id,
								state->context.catalog_epoch,
								state->request_token,
								current_token,
								loaded,
								cache);
						if (decision.erase_matching_in_flight && current != in_flight.end())
						{
							in_flight.erase(current);
						}
						publication_succeeded = true;
					}
					catch (...)
					{
						try
						{
							std::scoped_lock<std::mutex> lock(impl->mutex);
							auto& in_flight = impl.get()->*in_flight_member;
							const auto current = in_flight.find(state->key);
							if (current != in_flight.end() &&
								current->second.request_token == state->request_token)
							{
								in_flight.erase(current);
							}
						}
						catch (...)
						{
						}
					}

					if (!publication_succeeded)
					{
						set_vegetation_io_failure_noexcept(loaded);
					}
					try
					{
						state->promise->set_value(std::move(loaded.result));
					}
					catch (...)
					{
					}
				};

				ThreadCommandFuture worker{};
				try
				{
					worker = Detail::enqueue_worker_command(
						debug_name,
						[state, budget, load, complete]() mutable
						{
							VegetationLoadedValue<T> loaded{};
							try
							{
								loaded = load(state->context, budget);
							}
							catch (const std::exception& exception)
							{
								set_vegetation_io_failure_noexcept(loaded, exception.what());
							}
							catch (...)
							{
								set_vegetation_io_failure_noexcept(loaded);
							}
							complete(std::move(loaded));
						});
				}
				catch (const std::exception& exception)
				{
					VegetationLoadedValue<T> rejected{};
					set_vegetation_io_failure_noexcept(rejected, exception.what());
					complete(std::move(rejected));
					return state->future;
				}
				catch (...)
				{
					VegetationLoadedValue<T> rejected{};
					set_vegetation_io_failure_noexcept(rejected);
					complete(std::move(rejected));
					return state->future;
				}

				if (worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
				{
					try
					{
						worker.get();
					}
					catch (const std::exception& exception)
					{
						VegetationLoadedValue<T> rejected{};
						set_vegetation_io_failure_noexcept(rejected, exception.what());
						complete(std::move(rejected));
					}
					catch (...)
					{
						VegetationLoadedValue<T> rejected{};
						set_vegetation_io_failure_noexcept(rejected);
						complete(std::move(rejected));
					}
				}
				return state->future;
			}
			catch (const std::exception& exception)
			{
				return make_ready_future(make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::Io,
					std::string("Vegetation async request failed: ") + exception.what()));
			}
			catch (...)
			{
				return make_ready_future(make_vegetation_failure_result<T>(
					VegetationAssetLoadFailure::Io,
					"Vegetation async request failed with an unknown exception."));
			}
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

		struct TerrainLoadRequest
		{
			std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> future{};
			std::shared_ptr<std::promise<std::shared_ptr<const TerrainAssetSnapshot>>> promise{};
			uint64_t captured_load_serial = 0;
		};

		static auto next_terrain_load_serial_locked(AssetDatabase::Impl& impl) -> uint64_t
		{
			++impl.next_terrain_load_serial;
			if (impl.next_terrain_load_serial == 0)
			{
				++impl.next_terrain_load_serial;
			}
			return impl.next_terrain_load_serial;
		}

		static auto advance_catalog_generation_locked(AssetDatabase::Impl& impl) -> void
		{
			++impl.catalog_generation;
			if (impl.catalog_generation == 0)
			{
				++impl.catalog_generation;
			}
		}

		static auto prepare_terrain_load(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const ResolvedAssetInfo& resolved) -> TerrainLoadRequest
		{
			TerrainLoadRequest request{};
			std::scoped_lock<std::mutex> lock(impl->mutex);
			const auto indexed = impl->index_by_id.find(resolved.info.id);
			const bool current_catalog =
				resolved.catalog_generation == impl->catalog_generation;
			if (!current_catalog || indexed == impl->index_by_id.end() ||
				impl->assets[indexed->second].relative_path != resolved.info.relative_path)
			{
				set_last_error_locked(*impl, "Asset catalog changed before Terrain load started.");
				request.future = make_ready_future(
					std::shared_ptr<const TerrainAssetSnapshot>{});
				return request;
			}
			if (!validate_terrain_asset_locked(*impl, resolved))
			{
				request.future = make_ready_future(
					std::shared_ptr<const TerrainAssetSnapshot>{});
				return request;
			}
			const auto cached = impl->terrain_cache.find(resolved.info.id);
			if (cached != impl->terrain_cache.end())
			{
				set_load_success_locked(*impl, resolved.info.id);
				request.future = make_ready_future(cached->second);
				return request;
			}

			std::string cached_error{};
			if (try_get_cached_load_failure_locked(*impl, resolved.info.id, cached_error))
			{
				request.future = make_ready_future(
					std::shared_ptr<const TerrainAssetSnapshot>{});
				return request;
			}

			const auto inflight = impl->inflight_terrain_loads.find(resolved.info.id);
			if (inflight != impl->inflight_terrain_loads.end())
			{
				request.future = inflight->second.future;
				return request;
			}

			request.promise = std::make_shared<
				std::promise<std::shared_ptr<const TerrainAssetSnapshot>>>();
			request.future = request.promise->get_future().share();
			const uint64_t captured_load_serial = next_terrain_load_serial_locked(*impl);
			request.captured_load_serial = captured_load_serial;
			impl->terrain_load_serial_by_id[resolved.info.id] =
				captured_load_serial;
			impl->inflight_terrain_loads[resolved.info.id] = {
				captured_load_serial,
				request.future
			};
			set_load_loading_locked(*impl, resolved.info.id);
			return request;
		}

		static auto load_terrain_snapshot_from_file(
			const ResolvedAssetInfo& resolved,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
			std::string& out_error) -> TerrainContainerResult
		{
			// editor begin 修改原因：把容器恢复元数据原子附着到不可变 snapshot，供 Terrain 恢复 UI 使用。
			out_snapshot.reset();
			std::shared_ptr<const TerrainAssetSnapshot> loaded{};
			TerrainContainerLoadReport report{};
			const TerrainContainerResult result = load_terrain_container(
				resolved.absolute_path,
				loaded,
				&report,
				&out_error);
			if ((result != TerrainContainerResult::Success &&
				result != TerrainContainerResult::RecoveredPreviousGeneration) ||
				!loaded)
			{
				loaded.reset();
				out_snapshot.reset();
				if (out_error.empty())
				{
					out_error = "Failed to load Terrain asset container.";
				}
				return result;
			}

			auto prepared = std::make_shared<TerrainAssetSnapshot>(*loaded);
			prepared->asset_id = resolved.info.id;
			prepared->source_path = resolved.info.relative_path;
			prepared->recovered_previous_generation =
				report.recovered_previous_generation;
			prepared->rejected_content_generation = report.rejected_generation;
			prepared->recovery_detail = std::move(report.recovery_detail);
			prepared->source_revision = report.source_revision;
			out_snapshot = std::move(prepared);
			out_error.clear();
			// editor end
			return result;
		}

		static auto complete_terrain_load(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			TerrainAssetId asset_id,
			uint64_t captured_load_serial,
			const std::shared_ptr<
				std::promise<std::shared_ptr<const TerrainAssetSnapshot>>>& promise,
			std::shared_ptr<const TerrainAssetSnapshot> loaded_snapshot,
			const std::string& load_error,
			bool persistent_failure) -> void
		{
			std::shared_ptr<const TerrainAssetSnapshot> completion_snapshot{};
			{
				std::scoped_lock<std::mutex> lock(impl->mutex);
				const auto serial = impl->terrain_load_serial_by_id.find(asset_id);
				const uint64_t current_load_serial =
					serial != impl->terrain_load_serial_by_id.end() ? serial->second : 0;
				const auto inflight = impl->inflight_terrain_loads.find(asset_id);
				const bool owns_inflight =
					current_load_serial == captured_load_serial &&
					inflight != impl->inflight_terrain_loads.end() &&
					inflight->second.serial == captured_load_serial;
				if (owns_inflight)
				{
					if (loaded_snapshot)
					{
						impl->terrain_cache[asset_id] = loaded_snapshot;
						completion_snapshot = std::move(loaded_snapshot);
						set_load_success_locked(*impl, asset_id);
					}
					else
					{
						const std::string error = load_error.empty() ?
							"Failed to load Terrain asset." : load_error;
						if (persistent_failure)
						{
							set_load_failed_locked(*impl, asset_id, error);
						}
						else
						{
							set_last_error_locked(*impl, error);
							set_load_info_locked(
								*impl, asset_id, AssetLoadState::Unloaded, error);
						}
					}
					impl->inflight_terrain_loads.erase(inflight);
				}
				else
				{
					const auto current = impl->terrain_cache.find(asset_id);
					if (current != impl->terrain_cache.end())
					{
						completion_snapshot = current->second;
					}
				}
			}
			promise->set_value(std::move(completion_snapshot));
		}

		static auto resolve_terrain_dispatch_failure(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			TerrainAssetId asset_id,
			uint64_t captured_load_serial,
			const std::shared_ptr<
				std::promise<std::shared_ptr<const TerrainAssetSnapshot>>>& promise,
			const char* error) noexcept -> void
		{
			try
			{
				complete_terrain_load(
					impl,
					asset_id,
					captured_load_serial,
					promise,
					{},
					error ? error : "Terrain worker command failed.",
					false);
			}
			catch (...)
			{
				try
				{
					promise->set_value({});
				}
				catch (...)
				{
				}
			}
		}

		struct TerrainDispatchState
		{
			std::shared_ptr<AssetDatabase::Impl> impl{};
			TerrainAssetId asset_id = 0;
			uint64_t captured_load_serial = 0;
			std::shared_ptr<
				std::promise<std::shared_ptr<const TerrainAssetSnapshot>>> promise{};
			std::thread::id caller_thread{};
			std::atomic<bool> enqueue_in_progress{ true };
			std::atomic<bool> started{ false };

			TerrainDispatchState(
				std::shared_ptr<AssetDatabase::Impl> in_impl,
				TerrainAssetId in_asset_id,
				uint64_t in_captured_load_serial,
				std::shared_ptr<std::promise<
					std::shared_ptr<const TerrainAssetSnapshot>>> in_promise)
				: impl(std::move(in_impl)),
				  asset_id(in_asset_id),
				  captured_load_serial(in_captured_load_serial),
				  promise(std::move(in_promise)),
				  caller_thread(std::this_thread::get_id())
			{
			}

			~TerrainDispatchState()
			{
				if (!started.exchange(true, std::memory_order_acq_rel))
				{
					resolve_failure("Terrain worker command was rejected before execution.");
				}
			}

			void resolve_failure(const char* error) noexcept
			{
				resolve_terrain_dispatch_failure(
					impl,
					asset_id,
					captured_load_serial,
					promise,
					error);
			}
		};

		static auto perform_terrain_load(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			ResolvedAssetInfo resolved,
			uint64_t captured_load_serial,
			std::shared_ptr<
				std::promise<std::shared_ptr<const TerrainAssetSnapshot>>> promise) -> void
		{
			std::shared_ptr<const TerrainAssetSnapshot> loaded{};
			std::string error{};
			TerrainContainerResult load_result = TerrainContainerResult::InvalidData;
			try
			{
				// editor begin 修改原因：Terrain 恢复加载失败时强制保持空结果，禁止 Editor 缓存半成品。
				load_result = load_terrain_snapshot_from_file(resolved, loaded, error);
				if (load_result != TerrainContainerResult::Success &&
					load_result != TerrainContainerResult::RecoveredPreviousGeneration)
				{
					loaded.reset();
				}
				// editor end
			}
			catch (const std::exception& exception)
			{
				loaded.reset();
				error = exception.what();
			}
			catch (...)
			{
				loaded.reset();
				error = "Terrain asset load raised an unknown exception.";
			}

			complete_terrain_load(
				impl,
				resolved.info.id,
				captured_load_serial,
				promise,
				std::move(loaded),
				error,
				load_result != TerrainContainerResult::Busy &&
					load_result != TerrainContainerResult::SourceChanged);
		}

		static auto validate_terrain_asset_locked(
			AssetDatabase::Impl& impl,
			const ResolvedAssetInfo& resolved) -> bool
		{
			if (!resolved.info.is_directory &&
				resolved.info.type == AssetType::Terrain)
			{
				return true;
			}

			set_load_failed_locked(
				impl,
				resolved.info.id,
				resolved.info.is_directory ?
					"Cannot load a directory as Terrain." :
					"Asset is not a Terrain container.");
			return false;
		}

		static auto load_terrain_resolved(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const ResolvedAssetInfo& resolved,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot) -> bool
		{
			TerrainLoadRequest request = prepare_terrain_load(impl, resolved);
			if (request.promise)
			{
				perform_terrain_load(
					impl,
					resolved,
					request.captured_load_serial,
					request.promise);
			}

			try
			{
				out_snapshot = request.future.get();
			}
			catch (...)
			{
				out_snapshot.reset();
			}
			return out_snapshot != nullptr;
		}

		static auto load_terrain_resolved_async(
			const std::shared_ptr<AssetDatabase::Impl>& impl,
			const ResolvedAssetInfo& resolved)
			-> std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>>
		{
			TerrainLoadRequest request = prepare_terrain_load(impl, resolved);
			if (request.promise)
			{
				std::shared_ptr<TerrainDispatchState> dispatch_state{};
				try
				{
					dispatch_state = std::make_shared<TerrainDispatchState>(
						impl,
						resolved.info.id,
						request.captured_load_serial,
						request.promise);
					dispatch_background_task(
						"AssetDatabase::load_terrain_by_path_async",
						[dispatch_state, resolved]() mutable -> void
						{
							const bool inline_fallback =
								std::this_thread::get_id() == dispatch_state->caller_thread &&
								dispatch_state->enqueue_in_progress.load(
									std::memory_order_acquire);
							if (dispatch_state->started.exchange(
									true, std::memory_order_acq_rel))
							{
								return;
							}
							if (inline_fallback)
							{
								dispatch_state->resolve_failure(
									"Terrain async load requires an available worker thread.");
								return;
							}
							perform_terrain_load(
								dispatch_state->impl,
								std::move(resolved),
								dispatch_state->captured_load_serial,
								dispatch_state->promise);
						});
				}
				catch (...)
				{
					if (dispatch_state)
					{
						dispatch_state->enqueue_in_progress.store(
							false, std::memory_order_release);
						dispatch_state.reset();
					}
					else
					{
						resolve_terrain_dispatch_failure(
							impl,
							resolved.info.id,
							request.captured_load_serial,
							request.promise,
							"Terrain worker command allocation failed.");
					}
				}
				if (dispatch_state)
				{
					dispatch_state->enqueue_in_progress.store(
						false, std::memory_order_release);
				}
			}
			return request.future;
		}

		// editor begin 修改原因：为 Terrain 冲突/恢复提供不写共享 cache 的隔离候选加载。
		static auto make_failed_terrain_candidate(
			const TerrainAssetId asset_id,
			std::string error,
			const bool retryable = false) noexcept
			-> std::shared_ptr<const TerrainAssetSnapshot>
		{
			try
			{
				auto failed = std::make_shared<TerrainAssetSnapshot>();
				failed->asset_id = asset_id;
				failed->failed = true;
				failed->retryable_failure = retryable;
				failed->failure_detail = error.empty()
					? "Terrain candidate load failed." : std::move(error);
				return failed;
			}
			catch (...)
			{
				return {};
			}
		}

		struct TerrainCandidateDispatchState
		{
			TerrainAssetId asset_id = 0u;
			std::shared_ptr<std::promise<
				std::shared_ptr<const TerrainAssetSnapshot>>> promise{};
			std::thread::id caller_thread{};
			std::atomic<bool> enqueue_in_progress{ true };
			std::atomic<bool> started{ false };

			TerrainCandidateDispatchState(
				const TerrainAssetId in_asset_id,
				std::shared_ptr<std::promise<
					std::shared_ptr<const TerrainAssetSnapshot>>> in_promise)
				: asset_id(in_asset_id)
				, promise(std::move(in_promise))
				, caller_thread(std::this_thread::get_id())
			{
			}

			~TerrainCandidateDispatchState()
			{
				if (!started.exchange(true, std::memory_order_acq_rel))
				{
					Resolve({}, "Terrain candidate worker command was rejected before execution.");
				}
			}

			void Resolve(
				std::shared_ptr<const TerrainAssetSnapshot> snapshot,
				std::string error,
				const bool retryable = false) noexcept
			{
				if (!snapshot)
				{
					snapshot = make_failed_terrain_candidate(
						asset_id, std::move(error), retryable);
				}
				try
				{
					promise->set_value(std::move(snapshot));
				}
				catch (...)
				{
				}
			}
		};

		static auto load_terrain_candidate_resolved_async(
			const ResolvedAssetInfo& resolved)
			-> std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>>
		{
			auto promise = std::make_shared<
				std::promise<std::shared_ptr<const TerrainAssetSnapshot>>>();
			auto future = promise->get_future().share();
			std::shared_ptr<TerrainCandidateDispatchState> dispatch_state{};
			try
			{
				dispatch_state = std::make_shared<TerrainCandidateDispatchState>(
					resolved.info.id, std::move(promise));
				dispatch_background_task(
					"AssetDatabase::load_terrain_candidate_by_id_async",
					[dispatch_state, resolved]() mutable
					{
						const bool inline_fallback =
							std::this_thread::get_id() == dispatch_state->caller_thread &&
							dispatch_state->enqueue_in_progress.load(std::memory_order_acquire);
						if (dispatch_state->started.exchange(true, std::memory_order_acq_rel))
						{
							return;
						}
						if (inline_fallback)
						{
							dispatch_state->Resolve(
								{}, "Terrain candidate async load requires an available worker thread.");
							return;
						}

						std::shared_ptr<const TerrainAssetSnapshot> loaded{};
						std::string error{};
						TerrainContainerResult loadResult = TerrainContainerResult::InvalidData;
						try
						{
							loadResult = load_terrain_snapshot_from_file(resolved, loaded, error);
							if (loadResult != TerrainContainerResult::Success &&
								loadResult != TerrainContainerResult::RecoveredPreviousGeneration)
							{
								loaded.reset();
							}
						}
						catch (const std::exception& exception)
						{
							loaded.reset();
							error = exception.what();
						}
						catch (...)
						{
							loaded.reset();
							error = "Terrain candidate load raised an unknown exception.";
						}
						dispatch_state->Resolve(
							std::move(loaded),
							std::move(error),
							loadResult == TerrainContainerResult::Busy ||
								loadResult == TerrainContainerResult::SourceChanged);
					});
			dispatch_state->enqueue_in_progress.store(false, std::memory_order_release);
			}
			catch (...)
			{
				if (dispatch_state)
				{
					dispatch_state->enqueue_in_progress.store(false, std::memory_order_release);
					dispatch_state.reset();
				}
				else
				{
					try
					{
						promise->set_value(make_failed_terrain_candidate(
							resolved.info.id,
							"Terrain candidate worker allocation failed."));
					}
					catch (...)
					{
					}
				}
			}
			return future;
		}
		// editor end
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
		const std::filesystem::path normalized_root =
			(absolute_error ? root_path : absolute_root).lexically_normal();
		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		if (m_impl->root_path == normalized_root)
		{
			return true;
		}

		m_impl->root_path = normalized_root;
		bump_vegetation_epoch_locked(*m_impl);
		advance_catalog_generation_locked(*m_impl);
		m_impl->assets.clear();
		m_impl->index_by_id.clear();
		m_impl->index_by_key.clear();
		clear_all_asset_shared_state_locked(*m_impl);
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

		std::filesystem::path root_path{};
		uint64_t captured_epoch = 0;
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			root_path = m_impl->root_path;
			captured_epoch = m_impl->vegetation_catalog_epoch;
		}

		std::vector<AssetInfo> assets{};
		std::unordered_map<AssetId, size_t> index_by_id{};
		std::unordered_map<std::string, size_t> index_by_key{};
		std::unordered_map<AssetId, AssetLoadInfo> load_info_by_id{};
		VegetationCatalogScanOutcome scan_outcome = VegetationCatalogScanOutcome::Succeeded;
		std::string scan_error{};

		if (root_path.empty())
		{
			scan_outcome = VegetationCatalogScanOutcome::InvalidRoot;
			scan_error = "Asset root path does not exist or is not a directory.";
		}
		else
		{
			std::error_code exists_error{};
			const bool root_exists = std::filesystem::exists(root_path, exists_error);
			if (exists_error)
			{
				scan_outcome = VegetationCatalogScanOutcome::Failed;
				scan_error = exists_error.message();
			}
			else if (!root_exists)
			{
				scan_outcome = VegetationCatalogScanOutcome::InvalidRoot;
				scan_error = "Asset root path does not exist or is not a directory.";
			}
			else
			{
				std::error_code directory_error{};
				const bool root_is_directory = std::filesystem::is_directory(
					root_path, directory_error);
				if (directory_error)
				{
					scan_outcome = VegetationCatalogScanOutcome::Failed;
					scan_error = directory_error.message();
				}
				else if (!root_is_directory)
				{
					scan_outcome = VegetationCatalogScanOutcome::InvalidRoot;
					scan_error = "Asset root path does not exist or is not a directory.";
				}
			}
		}

		std::error_code iterate_error{};
		std::filesystem::recursive_directory_iterator iterator{};
		if (scan_outcome == VegetationCatalogScanOutcome::Succeeded)
		{
			iterator = std::filesystem::recursive_directory_iterator(root_path, iterate_error);
			if (iterate_error)
			{
				scan_outcome = VegetationCatalogScanOutcome::Failed;
				scan_error = iterate_error.message();
			}
		}

		for (std::filesystem::recursive_directory_iterator end;
			scan_outcome == VegetationCatalogScanOutcome::Succeeded && iterator != end;
			iterator.increment(iterate_error))
		{
			if (iterate_error)
			{
				scan_outcome = VegetationCatalogScanOutcome::Failed;
				scan_error = iterate_error.message();
				break;
			}

			const std::filesystem::directory_entry& entry = *iterator;
			const std::filesystem::path relative_path =
				std::filesystem::relative(entry.path(), root_path, iterate_error).lexically_normal();
			if (iterate_error)
			{
				scan_outcome = VegetationCatalogScanOutcome::Failed;
				scan_error = iterate_error.message();
				break;
			}

			const std::string key = normalize_asset_key(relative_path);
			AssetInfo info{};
			info.id = hash_asset_key(key);
			info.name = entry.path().filename().string();
			info.relative_path = relative_path;
			info.parent_path = relative_path.parent_path();
			std::error_code metadata_error{};
			info.is_directory = entry.is_directory(metadata_error);
			if (metadata_error)
			{
				scan_outcome = VegetationCatalogScanOutcome::Failed;
				scan_error = metadata_error.message();
				break;
			}
			info.type = detect_asset_type(relative_path, info.is_directory);

			info.last_write_time_ticks = static_cast<uint64_t>(entry.last_write_time(metadata_error).time_since_epoch().count());
			if (metadata_error)
			{
				scan_outcome = VegetationCatalogScanOutcome::Failed;
				scan_error = metadata_error.message();
				break;
			}
			if (!info.is_directory)
			{
				info.file_size = entry.file_size(metadata_error);
				if (metadata_error)
				{
					scan_outcome = VegetationCatalogScanOutcome::Failed;
					scan_error = metadata_error.message();
					break;
				}
			}

			assets.push_back(std::move(info));
		}
		if (scan_outcome == VegetationCatalogScanOutcome::Succeeded && iterate_error)
		{
			scan_outcome = VegetationCatalogScanOutcome::Failed;
			scan_error = iterate_error.message();
		}

		if (scan_outcome == VegetationCatalogScanOutcome::Succeeded)
		{
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
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const VegetationCatalogPublicationDecision decision = decide_vegetation_catalog_publication({
			captured_epoch,
			m_impl->vegetation_catalog_epoch,
			root_path,
			m_impl->root_path,
			scan_outcome });
		switch (decision)
		{
		case VegetationCatalogPublicationDecision::PublishReplacement:
			bump_vegetation_epoch_locked(*m_impl);
			advance_catalog_generation_locked(*m_impl);
			clear_all_asset_shared_state_locked(*m_impl);
			m_impl->assets = std::move(assets);
			m_impl->index_by_id = std::move(index_by_id);
			m_impl->index_by_key = std::move(index_by_key);
			m_impl->load_info_by_id = std::move(load_info_by_id);
			m_impl->last_error.clear();
			return true;
		case VegetationCatalogPublicationDecision::ResetInvalidRoot:
			bump_vegetation_epoch_locked(*m_impl);
			advance_catalog_generation_locked(*m_impl);
			m_impl->assets.clear();
			m_impl->index_by_id.clear();
			m_impl->index_by_key.clear();
			clear_all_asset_shared_state_locked(*m_impl);
			m_impl->last_error = normalize_vegetation_diagnostic(std::move(scan_error));
			return false;
		case VegetationCatalogPublicationDecision::KeepLastKnownGood:
			m_impl->last_error = normalize_vegetation_diagnostic(std::move(scan_error));
			return false;
		case VegetationCatalogPublicationDecision::DiscardStale:
		default:
			return false;
		}
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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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

	bool AssetDatabase::load_text_by_id_bounded(
		const AssetId id,
		const uint64_t max_file_bytes,
		std::string& out_text)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			if (!resolve_asset_by_id_locked(m_impl, id, resolved) ||
				resolved.info.is_directory || is_opaque_vegetation_asset_type(resolved.info.type))
			{
				return false;
			}
		}

		std::vector<uint8_t> bytes{};
		std::string error{};
		if (read_vegetation_file_snapshot(
			resolved.absolute_path, max_file_bytes, bytes, error) !=
			VegetationSnapshotReadOutcome::Succeeded)
		{
			return false;
		}
		out_text.assign(bytes.begin(), bytes.end());
		return true;
	}

	bool AssetDatabase::load_text_by_path_bounded(
		const std::filesystem::path& path,
		const uint64_t max_file_bytes,
		std::string& out_text)
	{
		if (!m_impl)
		{
			return false;
		}

		ResolvedAssetInfo resolved{};
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			if (!resolve_asset_by_path_locked(m_impl, path, resolved) ||
				resolved.info.is_directory || is_opaque_vegetation_asset_type(resolved.info.type))
			{
				return false;
			}
		}

		std::vector<uint8_t> bytes{};
		std::string error{};
		if (read_vegetation_file_snapshot(
			resolved.absolute_path, max_file_bytes, bytes, error) !=
			VegetationSnapshotReadOutcome::Succeeded)
		{
			return false;
		}
		out_text.assign(bytes.begin(), bytes.end());
		return true;
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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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
		if (is_vegetation_asset_type(resolved.info.type))
		{
			error = "Vegetation assets require a typed AssetDatabase loader.";
			ASH_PROCESS_ERROR(false);
		}

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

	bool AssetDatabase::load_terrain_by_id(
		TerrainAssetId id,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot)
	{
		out_snapshot.reset();
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
		return load_terrain_resolved(m_impl, resolved, out_snapshot);
	}

	bool AssetDatabase::load_terrain_by_path(
		const std::filesystem::path& path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot)
	{
		out_snapshot.reset();
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
		return load_terrain_resolved(m_impl, resolved, out_snapshot);
	}

	std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>>
		AssetDatabase::load_terrain_by_id_async(TerrainAssetId id)
	{
		if (!m_impl)
		{
			return make_ready_future(
				std::shared_ptr<const TerrainAssetSnapshot>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_id(m_impl, id, resolved, error))
		{
			return make_ready_future(
				std::shared_ptr<const TerrainAssetSnapshot>{});
		}
		return load_terrain_resolved_async(m_impl, resolved);
	}

	std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>>
		AssetDatabase::load_terrain_by_path_async(const std::filesystem::path& path)
	{
		if (!m_impl)
		{
			return make_ready_future(
				std::shared_ptr<const TerrainAssetSnapshot>{});
		}

		ResolvedAssetInfo resolved{};
		std::string error{};
		if (!resolve_asset_by_path(m_impl, path, resolved, error))
		{
			return make_ready_future(
				std::shared_ptr<const TerrainAssetSnapshot>{});
		}
		return load_terrain_resolved_async(m_impl, resolved);
	}

	// editor begin 修改原因：只有用户接受 Terrain 磁盘候选后才切换共享发布版本。
	std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>>
		AssetDatabase::load_terrain_candidate_by_id_async(const TerrainAssetId id)
	{
		if (!m_impl)
		{
			return make_ready_future(
				std::shared_ptr<const TerrainAssetSnapshot>{});
		}

		ResolvedAssetInfo resolved{};
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			if (!resolve_asset_by_id_locked(m_impl, id, resolved))
			{
				return make_ready_future(make_failed_terrain_candidate(
					id, "Asset id was not found."));
			}
			if (resolved.info.is_directory || resolved.info.type != AssetType::Terrain)
			{
				return make_ready_future(make_failed_terrain_candidate(
					id,
					resolved.info.is_directory
						? "Cannot load a directory as Terrain."
						: "Asset is not a Terrain container."));
			}
		}
		return load_terrain_candidate_resolved_async(resolved);
	}

	TerrainSnapshotPublicationToken AssetDatabase::capture_terrain_snapshot_publication(
		const TerrainAssetId id) const
	{
		TerrainSnapshotPublicationToken token{};
		if (!m_impl)
		{
			return token;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		token.asset_id = id;
		token.catalog_generation = m_impl->catalog_generation;
		const auto serial = m_impl->terrain_load_serial_by_id.find(id);
		if (serial != m_impl->terrain_load_serial_by_id.end())
		{
			token.load_serial = serial->second;
		}
		const auto cached = m_impl->terrain_cache.find(id);
		if (cached != m_impl->terrain_cache.end())
		{
			token.snapshot = cached->second;
		}
		return token;
	}
	// editor end

	bool AssetDatabase::publish_terrain_snapshot(
		TerrainAssetId id,
		std::shared_ptr<const TerrainAssetSnapshot> snapshot)
	{
		if (!m_impl)
		{
			return false;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const auto indexed = m_impl->index_by_id.find(id);
		if (indexed == m_impl->index_by_id.end() ||
			m_impl->assets[indexed->second].is_directory ||
			m_impl->assets[indexed->second].type != AssetType::Terrain)
		{
			set_last_error_locked(*m_impl, "Terrain asset id is not indexed.");
			return false;
		}
		if (!snapshot || snapshot->failed || snapshot->asset_id != id)
		{
			set_last_error_locked(*m_impl, "Terrain snapshot is invalid or belongs to another asset.");
			return false;
		}

		const auto cached = m_impl->terrain_cache.find(id);
		if (cached != m_impl->terrain_cache.end())
		{
			const TerrainAssetSnapshot& current = *cached->second;
			const bool newer =
				snapshot->content_generation > current.content_generation ||
				(snapshot->content_generation == current.content_generation &&
					snapshot->residency_revision > current.residency_revision);
			if (!newer)
			{
				set_last_error_locked(*m_impl, "Terrain snapshot publication is stale.");
				return false;
			}
		}

		m_impl->terrain_load_serial_by_id[id] =
			next_terrain_load_serial_locked(*m_impl);
		m_impl->terrain_cache[id] = std::move(snapshot);
		m_impl->inflight_terrain_loads.erase(id);
		set_load_success_locked(*m_impl, id);
		return true;
	}

	// editor begin 修改原因：用户接受 Terrain 磁盘候选时只可切换未被并发修改的共享发布版本。
	bool AssetDatabase::compare_exchange_terrain_snapshot(
		const TerrainAssetId id,
		const TerrainSnapshotPublicationToken& expected,
		std::shared_ptr<const TerrainAssetSnapshot> snapshot,
		TerrainSnapshotPublicationToken* const p_result)
	{
		if (!m_impl)
		{
			return false;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const auto indexed = m_impl->index_by_id.find(id);
		if (indexed == m_impl->index_by_id.end() ||
			m_impl->assets[indexed->second].is_directory ||
			m_impl->assets[indexed->second].type != AssetType::Terrain)
		{
			set_last_error_locked(*m_impl, "Terrain asset id is not indexed.");
			return false;
		}
		if (snapshot && (snapshot->failed || snapshot->asset_id != id))
		{
			set_last_error_locked(*m_impl, "Terrain replacement snapshot is invalid or belongs to another asset.");
			return false;
		}
		const auto serial = m_impl->terrain_load_serial_by_id.find(id);
		const uint64_t current_serial = serial != m_impl->terrain_load_serial_by_id.end()
			? serial->second : 0u;
		const auto cached = m_impl->terrain_cache.find(id);
		const std::shared_ptr<const TerrainAssetSnapshot> current =
			cached != m_impl->terrain_cache.end() ? cached->second : nullptr;
		if (expected.asset_id != id ||
			expected.catalog_generation != m_impl->catalog_generation ||
			expected.load_serial != current_serial || expected.snapshot != current)
		{
			set_last_error_locked(
				*m_impl, "Terrain snapshot publication changed before replacement.");
			return false;
		}

		auto mutable_serial = m_impl->terrain_load_serial_by_id.find(id);
		auto mutable_cache = m_impl->terrain_cache.find(id);
		bool inserted_serial = false;
		bool inserted_cache = false;
		try
		{
			if (mutable_serial == m_impl->terrain_load_serial_by_id.end())
			{
				auto inserted = m_impl->terrain_load_serial_by_id.try_emplace(id, current_serial);
				mutable_serial = inserted.first;
				inserted_serial = inserted.second;
			}
			if (snapshot && mutable_cache == m_impl->terrain_cache.end())
			{
				auto inserted = m_impl->terrain_cache.try_emplace(id, current);
				mutable_cache = inserted.first;
				inserted_cache = inserted.second;
			}
		}
		catch (const std::bad_alloc&)
		{
			if (inserted_cache)
			{
				m_impl->terrain_cache.erase(id);
			}
			if (inserted_serial)
			{
				m_impl->terrain_load_serial_by_id.erase(id);
			}
			try
			{
				set_last_error_locked(*m_impl, "Terrain snapshot publication allocation failed.");
			}
			catch (...)
			{
			}
			return false;
		}

		const uint64_t replacement_serial = next_terrain_load_serial_locked(*m_impl);
		mutable_serial->second = replacement_serial;
		if (snapshot)
		{
			mutable_cache->second = snapshot;
		}
		else
		{
			m_impl->terrain_cache.erase(id);
		}
		m_impl->inflight_terrain_loads.erase(id);
		if (snapshot)
		{
			set_load_success_locked(*m_impl, id);
		}
		else
		{
			set_last_error_locked(*m_impl, {});
			set_load_info_locked(*m_impl, id, AssetLoadState::Unloaded, {});
		}
		if (p_result)
		{
			p_result->snapshot = std::move(snapshot);
			p_result->asset_id = id;
			p_result->catalog_generation = m_impl->catalog_generation;
			p_result->load_serial = replacement_serial;
		}
		return true;
	}
	// editor end

	bool AssetDatabase::invalidate_terrain_snapshot(TerrainAssetId id)
	{
		if (!m_impl)
		{
			return false;
		}

		std::scoped_lock<std::mutex> lock(m_impl->mutex);
		const auto indexed = m_impl->index_by_id.find(id);
		if (indexed == m_impl->index_by_id.end() ||
			m_impl->assets[indexed->second].is_directory ||
			m_impl->assets[indexed->second].type != AssetType::Terrain)
		{
			set_last_error_locked(*m_impl, "Terrain asset id is not indexed.");
			return false;
		}

		m_impl->terrain_load_serial_by_id[id] =
			next_terrain_load_serial_locked(*m_impl);
		m_impl->terrain_cache.erase(id);
		m_impl->inflight_terrain_loads.erase(id);
		set_last_error_locked(*m_impl, {});
		set_load_info_locked(*m_impl, id, AssetLoadState::Unloaded, {});
		return true;
	}

	VegetationAssetLoadResult<VegetationSpecies> AssetDatabase::load_vegetation_species_by_id(
		const AssetId id,
		const VegetationLoadBudget& budget)
	{
		const auto impl = m_impl;
		return load_vegetation_sync_common<VegetationSpecies>(
			impl,
			AssetType::Species,
			budget,
			&Impl::vegetation_species_cache,
			[impl, id](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_id_locked(impl, id, resolved);
			},
			load_vegetation_species_value);
	}

	VegetationAssetLoadResult<VegetationSpecies> AssetDatabase::load_vegetation_species_by_path(
		const std::filesystem::path& path,
		const VegetationLoadBudget& budget)
	{
		const auto impl = m_impl;
		const std::filesystem::path requested_path = path;
		return load_vegetation_sync_common<VegetationSpecies>(
			impl,
			AssetType::Species,
			budget,
			&Impl::vegetation_species_cache,
			[impl, requested_path](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_path_locked(impl, requested_path, resolved);
			},
			load_vegetation_species_value);
	}

	std::shared_future<VegetationAssetLoadResult<VegetationSpecies>>
	AssetDatabase::load_vegetation_species_by_id_async(
		const AssetId id,
		const VegetationLoadBudget budget)
	{
		const auto impl = m_impl;
		return load_vegetation_async_common<VegetationSpecies>(
			impl,
			AssetType::Species,
			budget,
			&Impl::vegetation_species_cache,
			&Impl::inflight_vegetation_species_loads,
			[impl, id](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_id_locked(impl, id, resolved);
			},
			load_vegetation_species_value,
			"AssetDatabase::load_vegetation_species_by_id_async");
	}

	std::shared_future<VegetationAssetLoadResult<VegetationSpecies>>
	AssetDatabase::load_vegetation_species_by_path_async(
		const std::filesystem::path& path,
		const VegetationLoadBudget budget)
	{
		const auto impl = m_impl;
		const std::filesystem::path requested_path = path;
		return load_vegetation_async_common<VegetationSpecies>(
			impl,
			AssetType::Species,
			budget,
			&Impl::vegetation_species_cache,
			&Impl::inflight_vegetation_species_loads,
			[impl, requested_path](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_path_locked(impl, requested_path, resolved);
			},
			load_vegetation_species_value,
			"AssetDatabase::load_vegetation_species_by_path_async");
	}

	VegetationAssetLoadResult<VegetationLayerSnapshot> AssetDatabase::load_vegetation_layer_by_id(
		const AssetId id,
		const VegetationLoadBudget& budget)
	{
		const auto impl = m_impl;
		return load_vegetation_sync_common<VegetationLayerSnapshot>(
			impl,
			AssetType::Layer,
			budget,
			&Impl::vegetation_layer_cache,
			[impl, id](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_id_locked(impl, id, resolved);
			},
			load_vegetation_layer_value);
	}

	VegetationAssetLoadResult<VegetationLayerSnapshot> AssetDatabase::load_vegetation_layer_by_path(
		const std::filesystem::path& path,
		const VegetationLoadBudget& budget)
	{
		const auto impl = m_impl;
		const std::filesystem::path requested_path = path;
		return load_vegetation_sync_common<VegetationLayerSnapshot>(
			impl,
			AssetType::Layer,
			budget,
			&Impl::vegetation_layer_cache,
			[impl, requested_path](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_path_locked(impl, requested_path, resolved);
			},
			load_vegetation_layer_value);
	}

	std::shared_future<VegetationAssetLoadResult<VegetationLayerSnapshot>>
	AssetDatabase::load_vegetation_layer_by_id_async(
		const AssetId id,
		const VegetationLoadBudget budget)
	{
		const auto impl = m_impl;
		return load_vegetation_async_common<VegetationLayerSnapshot>(
			impl,
			AssetType::Layer,
			budget,
			&Impl::vegetation_layer_cache,
			&Impl::inflight_vegetation_layer_loads,
			[impl, id](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_id_locked(impl, id, resolved);
			},
			load_vegetation_layer_value,
			"AssetDatabase::load_vegetation_layer_by_id_async");
	}

	std::shared_future<VegetationAssetLoadResult<VegetationLayerSnapshot>>
	AssetDatabase::load_vegetation_layer_by_path_async(
		const std::filesystem::path& path,
		const VegetationLoadBudget budget)
	{
		const auto impl = m_impl;
		const std::filesystem::path requested_path = path;
		return load_vegetation_async_common<VegetationLayerSnapshot>(
			impl,
			AssetType::Layer,
			budget,
			&Impl::vegetation_layer_cache,
			&Impl::inflight_vegetation_layer_loads,
			[impl, requested_path](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_path_locked(impl, requested_path, resolved);
			},
			load_vegetation_layer_value,
			"AssetDatabase::load_vegetation_layer_by_path_async");
	}

	VegetationAssetLoadResult<VegetationChunk> AssetDatabase::load_vegetation_chunk_by_id(
		const AssetId id,
		const VegetationLoadBudget& budget)
	{
		const auto impl = m_impl;
		return load_vegetation_sync_common<VegetationChunk>(
			impl,
			AssetType::Chunk,
			budget,
			&Impl::vegetation_chunk_cache,
			[impl, id](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_id_locked(impl, id, resolved);
			},
			load_vegetation_chunk_value);
	}

	VegetationAssetLoadResult<VegetationChunk> AssetDatabase::load_vegetation_chunk_by_path(
		const std::filesystem::path& path,
		const VegetationLoadBudget& budget)
	{
		const auto impl = m_impl;
		const std::filesystem::path requested_path = path;
		return load_vegetation_sync_common<VegetationChunk>(
			impl,
			AssetType::Chunk,
			budget,
			&Impl::vegetation_chunk_cache,
			[impl, requested_path](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_path_locked(impl, requested_path, resolved);
			},
			load_vegetation_chunk_value);
	}

	std::shared_future<VegetationAssetLoadResult<VegetationChunk>>
	AssetDatabase::load_vegetation_chunk_by_id_async(
		const AssetId id,
		const VegetationLoadBudget budget)
	{
		const auto impl = m_impl;
		return load_vegetation_async_common<VegetationChunk>(
			impl,
			AssetType::Chunk,
			budget,
			&Impl::vegetation_chunk_cache,
			&Impl::inflight_vegetation_chunk_loads,
			[impl, id](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_id_locked(impl, id, resolved);
			},
			load_vegetation_chunk_value,
			"AssetDatabase::load_vegetation_chunk_by_id_async");
	}

	std::shared_future<VegetationAssetLoadResult<VegetationChunk>>
	AssetDatabase::load_vegetation_chunk_by_path_async(
		const std::filesystem::path& path,
		const VegetationLoadBudget budget)
	{
		const auto impl = m_impl;
		const std::filesystem::path requested_path = path;
		return load_vegetation_async_common<VegetationChunk>(
			impl,
			AssetType::Chunk,
			budget,
			&Impl::vegetation_chunk_cache,
			&Impl::inflight_vegetation_chunk_loads,
			[impl, requested_path](ResolvedAssetInfo& resolved)
			{
				return resolve_asset_by_path_locked(impl, requested_path, resolved);
			},
			load_vegetation_chunk_value,
			"AssetDatabase::load_vegetation_chunk_by_path_async");
	}

	std::shared_ptr<const VegetationAssetResolverSnapshot>
	AssetDatabase::capture_vegetation_resolver_snapshot() const
	{
		if (!m_impl)
		{
			return {};
		}
		try
		{
			std::scoped_lock<std::mutex> lock(m_impl->mutex);
			return make_resolver_snapshot_locked(*m_impl);
		}
		catch (...)
		{
			return {};
		}
	}
}
