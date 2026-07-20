#include "Base/hthreading.h"
#include "Function/Asset/AssetDatabase.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static_assert(static_cast<uint8_t>(AshEngine::AssetType::Unknown) == 0);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Directory) == 1);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Scene) == 2);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Shader) == 3);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Texture) == 4);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Mesh) == 5);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Model) == 6);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Prefab) == 7);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Material) == 8);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Text) == 9);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Binary) == 10);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Species) == 11);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Layer) == 12);
static_assert(static_cast<uint8_t>(AshEngine::AssetType::Chunk) == 13);

namespace
{
	class ScopedSingleWorkerThreading
	{
	public:
		ScopedSingleWorkerThreading()
			: m_original_role(AshEngine::get_current_thread_role())
		{
			AshEngine::shutdown_threading();
			AshEngine::EngineThreadingConfig config{};
			config.worker_thread_count = 1;
			if (!AshEngine::initialize_threading(config))
			{
				throw std::runtime_error("Failed to initialize one worker thread");
			}
		}

		~ScopedSingleWorkerThreading()
		{
			AshEngine::shutdown_threading();
			AshEngine::register_current_thread_role(m_original_role);
		}

		ScopedSingleWorkerThreading(const ScopedSingleWorkerThreading&) = delete;
		ScopedSingleWorkerThreading& operator=(const ScopedSingleWorkerThreading&) = delete;

	private:
		AshEngine::EngineThreadRole m_original_role = AshEngine::EngineThreadRole::Unknown;
	};

	class ShutdownJoinGuard
	{
	public:
		ShutdownJoinGuard(std::promise<void>& release, std::thread thread)
			: m_release(release), m_thread(std::move(thread))
		{
		}

		~ShutdownJoinGuard()
		{
			Finish();
		}

		void Finish()
		{
			if (!m_released)
			{
				m_release.set_value();
				m_released = true;
			}
			if (m_thread.joinable())
			{
				m_thread.join();
			}
		}

	private:
		std::promise<void>& m_release;
		std::thread m_thread{};
		bool m_released = false;
	};

	struct BlockingWorkerState
	{
		std::promise<void> started_promise{};
		std::shared_future<void> started = started_promise.get_future().share();
		std::promise<void> release_promise{};
		std::shared_future<void> release = release_promise.get_future().share();
		std::atomic<bool> released{ false };

		void ReleaseNoexcept() noexcept
		{
			bool expected = false;
			if (!released.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			{
				return;
			}
			try
			{
				release_promise.set_value();
			}
			catch (...)
			{
			}
		}
	};

	class BlockingWorkerReleaseGuard
	{
	public:
		explicit BlockingWorkerReleaseGuard(std::shared_ptr<BlockingWorkerState> state)
			: m_state(std::move(state))
		{
		}

		~BlockingWorkerReleaseGuard()
		{
			ReleaseNoexcept();
		}

		void ReleaseNoexcept() noexcept
		{
			if (m_state)
			{
				m_state->ReleaseNoexcept();
			}
		}

	private:
		std::shared_ptr<BlockingWorkerState> m_state{};
	};

	class PartialThenMoreStreamBuf final : public std::streambuf
	{
	public:
		PartialThenMoreStreamBuf(std::string bytes, const size_t first_read_bytes)
			: m_bytes(std::move(bytes)), m_first_read_bytes(first_read_bytes)
		{
		}

	protected:
		std::streamsize xsgetn(char* destination, const std::streamsize count) override
		{
			if (count <= 0 || m_offset >= m_bytes.size())
			{
				return 0;
			}

			size_t copied = std::min<size_t>(
				static_cast<size_t>(count), m_bytes.size() - m_offset);
			if (m_first_read)
			{
				copied = std::min(copied, m_first_read_bytes);
				m_first_read = false;
			}
			for (size_t index = 0; index < copied; ++index)
			{
				destination[index] = m_bytes[m_offset + index];
			}
			m_offset += copied;
			return static_cast<std::streamsize>(copied);
		}

		int_type underflow() override
		{
			return m_offset < m_bytes.size() ?
				traits_type::to_int_type(m_bytes[m_offset]) : traits_type::eof();
		}

		int_type uflow() override
		{
			const int_type value = underflow();
			if (!traits_type::eq_int_type(value, traits_type::eof()))
			{
				++m_offset;
			}
			return value;
		}

	private:
		std::string m_bytes{};
		size_t m_first_read_bytes = 0;
		size_t m_offset = 0;
		bool m_first_read = true;
	};

	class GrowingAfterLimitStreamBuf final : public std::streambuf
	{
	public:
		explicit GrowingAfterLimitStreamBuf(std::string initial_bytes)
			: m_bytes(std::move(initial_bytes))
		{
		}

	protected:
		std::streamsize xsgetn(char* destination, const std::streamsize count) override
		{
			if (count <= 0 || m_offset >= m_bytes.size())
			{
				return 0;
			}
			const size_t copied = std::min<size_t>(
				static_cast<size_t>(count), m_bytes.size() - m_offset);
			for (size_t index = 0; index < copied; ++index)
			{
				destination[index] = m_bytes[m_offset + index];
			}
			m_offset += copied;
			return static_cast<std::streamsize>(copied);
		}

		int_type underflow() override
		{
			RevealGrowth();
			return m_offset < m_bytes.size() ?
				traits_type::to_int_type(m_bytes[m_offset]) : traits_type::eof();
		}

		int_type uflow() override
		{
			const int_type value = underflow();
			if (!traits_type::eq_int_type(value, traits_type::eof()))
			{
				++m_offset;
			}
			return value;
		}

	private:
		void RevealGrowth()
		{
			if (!m_growth_revealed && m_offset == m_bytes.size())
			{
				m_bytes.push_back('!');
				m_growth_revealed = true;
			}
		}

		std::string m_bytes{};
		size_t m_offset = 0;
		bool m_growth_revealed = false;
	};

	AshEngine::VegetationLoadBudget TinyFileBudget()
	{
		AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
		budget.max_file_bytes = 1;
		return budget;
	}

	void CheckVegetationCostEqual(
		const AshEngine::VegetationLoadCost& actual,
		const AshEngine::VegetationLoadCost& expected)
	{
		CHECK(actual.file_bytes == expected.file_bytes);
		CHECK(actual.payload_bytes == expected.payload_bytes);
		CHECK(actual.decoded_bytes == expected.decoded_bytes);
		CHECK(actual.palette_records == expected.palette_records);
		CHECK(actual.tile_records == expected.tile_records);
		CHECK(actual.instance_records == expected.instance_records);
	}

	AshEngine::VegetationLoadBudget ExactBudgetFor(
		const std::initializer_list<AshEngine::VegetationLoadCost> costs)
	{
		AshEngine::VegetationLoadBudget budget{};
		for (const AshEngine::VegetationLoadCost& cost : costs)
		{
			budget.max_file_bytes = std::max(budget.max_file_bytes, cost.file_bytes);
			budget.max_payload_bytes = std::max(budget.max_payload_bytes, cost.payload_bytes);
			budget.max_decoded_bytes = std::max(budget.max_decoded_bytes, cost.decoded_bytes);
			budget.max_palette_records = std::max(
				budget.max_palette_records, cost.palette_records);
			budget.max_tile_records = std::max(
				budget.max_tile_records, cost.tile_records);
			budget.max_instance_records = std::max(
				budget.max_instance_records, cost.instance_records);
		}
		return budget;
	}

	void ReplaceFixtureFile(
		const std::filesystem::path& path,
		const std::vector<uint8_t>& bytes)
	{
		std::error_code error{};
		std::filesystem::remove_all(path, error);
		if (error)
		{
			throw std::runtime_error("Failed to remove vegetation I/O fixture");
		}
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output.is_open())
		{
			throw std::runtime_error("Failed to restore vegetation I/O fixture");
		}
		if (!bytes.empty())
		{
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}
		if (!output)
		{
			throw std::runtime_error("Failed to write restored vegetation I/O fixture");
		}
	}

	std::vector<uint8_t> EncodeLayer(const AshEngine::VegetationLayerSnapshot& layer)
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(layer, bytes, &error))
		{
			throw std::runtime_error("Failed to encode Task3 Layer fixture: " + error);
		}
		return bytes;
	}

	std::vector<uint8_t> EncodeChunk(const AshEngine::VegetationChunk& chunk)
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_chunk(chunk, bytes, &error))
		{
			throw std::runtime_error("Failed to encode Task3 Chunk fixture: " + error);
		}
		return bytes;
	}

	template<typename T>
	void CheckVegetationFailureShape(
		const AshEngine::VegetationAssetLoadResult<T>& result,
		const AshEngine::VegetationAssetLoadFailure failure)
	{
		CHECK(result.state == (failure == AshEngine::VegetationAssetLoadFailure::Missing ?
			AshEngine::AssetLoadState::Missing : AshEngine::AssetLoadState::Failed));
		CHECK(result.failure == failure);
		CHECK(result.asset == nullptr);
		CHECK(result.cost.file_bytes == 0);
		CHECK(result.cost.payload_bytes == 0);
		CHECK(result.cost.decoded_bytes == 0);
		CHECK(result.cost.palette_records == 0);
		CHECK(result.cost.tile_records == 0);
		CHECK(result.cost.instance_records == 0);
		CHECK_FALSE(result.error.empty());
	}
}

TEST_CASE("Vegetation AssetDatabase detects case-insensitive types and exposes immutable typed loads")
{
	VegetationTest::ScopedAssetRoot root("typed-load-red");
	root.Write("vegetation/Phase2ManualSpecies.ASHVEGETATION",
		VegetationTest::CanonicalGrassSpeciesJson());
	root.Write("flora/meadow.AshVegetationLayer", VegetationTest::ResolvedMinimalLayerBytes());
	root.Write("flora/0_0.ashvegetationchunk", VegetationTest::ResolvedMinimalChunkBytes());

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	REQUIRE(database.is_valid());
	REQUIRE(database.refresh());
	const AshEngine::AssetInfo* species_info =
		database.find_asset_by_path("vegetation/Phase2ManualSpecies.ASHVEGETATION");
	const AshEngine::AssetInfo* layer_info =
		database.find_asset_by_path("flora/meadow.AshVegetationLayer");
	const AshEngine::AssetInfo* chunk_info =
		database.find_asset_by_path("flora/0_0.ashvegetationchunk");
	REQUIRE(species_info != nullptr);
	REQUIRE(layer_info != nullptr);
	REQUIRE(chunk_info != nullptr);
	CHECK(species_info->type == AshEngine::AssetType::Species);
	CHECK(layer_info->type == AshEngine::AssetType::Layer);
	CHECK(chunk_info->type == AshEngine::AssetType::Chunk);

	const auto first = database.load_vegetation_species_by_path_async(
		"vegetation/Phase2ManualSpecies.ASHVEGETATION", VegetationTest::GenerousLoadBudget());
	const auto second = database.load_vegetation_species_by_id_async(
		species_info->id, VegetationTest::GenerousLoadBudget());
	const auto first_result = first.get();
	const auto second_result = second.get();
	REQUIRE(first_result.asset != nullptr);
	REQUIRE(second_result.asset != nullptr);
	CHECK(first_result.state == AshEngine::AssetLoadState::Loaded);
	CHECK(first_result.failure == AshEngine::VegetationAssetLoadFailure::None);
	CHECK(first_result.asset == second_result.asset);
	const auto species_by_path = database.load_vegetation_species_by_path(
		"vegetation/Phase2ManualSpecies.ASHVEGETATION", VegetationTest::GenerousLoadBudget());
	const auto species_by_id = database.load_vegetation_species_by_id(
		species_info->id, VegetationTest::GenerousLoadBudget());
	REQUIRE(species_by_path.asset != nullptr);
	REQUIRE(species_by_id.asset != nullptr);
	CHECK(species_by_path.asset == first_result.asset);
	CHECK(species_by_id.asset == first_result.asset);

	const auto layer_sync = database.load_vegetation_layer_by_path(
		"flora/meadow.AshVegetationLayer", VegetationTest::GenerousLoadBudget());
	const auto layer_async = database.load_vegetation_layer_by_id_async(
		layer_info->id, VegetationTest::GenerousLoadBudget()).get();
	const auto layer_by_id = database.load_vegetation_layer_by_id(
		layer_info->id, VegetationTest::GenerousLoadBudget());
	const auto layer_by_path_async = database.load_vegetation_layer_by_path_async(
		"flora/meadow.AshVegetationLayer", VegetationTest::GenerousLoadBudget()).get();
	REQUIRE(layer_sync.asset != nullptr);
	REQUIRE(layer_async.asset != nullptr);
	REQUIRE(layer_by_id.asset != nullptr);
	REQUIRE(layer_by_path_async.asset != nullptr);
	CHECK(layer_sync.asset == layer_async.asset);
	CHECK(layer_sync.asset == layer_by_id.asset);
	CHECK(layer_sync.asset == layer_by_path_async.asset);
	CHECK(layer_sync.asset->palette[0].species_id == first_result.asset->species_id);

	const auto chunk_sync = database.load_vegetation_chunk_by_id(
		chunk_info->id, VegetationTest::GenerousLoadBudget());
	const auto chunk_async = database.load_vegetation_chunk_by_path_async(
		"flora/0_0.ashvegetationchunk", VegetationTest::GenerousLoadBudget()).get();
	const auto chunk_by_path = database.load_vegetation_chunk_by_path(
		"flora/0_0.ashvegetationchunk", VegetationTest::GenerousLoadBudget());
	const auto chunk_by_id_async = database.load_vegetation_chunk_by_id_async(
		chunk_info->id, VegetationTest::GenerousLoadBudget()).get();
	REQUIRE(chunk_sync.asset != nullptr);
	REQUIRE(chunk_async.asset != nullptr);
	REQUIRE(chunk_by_path.asset != nullptr);
	REQUIRE(chunk_by_id_async.asset != nullptr);
	CHECK(chunk_sync.asset == chunk_async.asset);
	CHECK(chunk_sync.asset == chunk_by_path.asset);
	CHECK(chunk_sync.asset == chunk_by_id_async.asset);
	CHECK(chunk_sync.asset->species[0].species_id == first_result.asset->species_id);
}

TEST_CASE("Vegetation AssetDatabase reports codec costs and admits exact independent budgets")
{
	std::string error{};
	AshEngine::VegetationSpecies first_species{};
	REQUIRE(AshEngine::decode_vegetation_species(
		VegetationTest::CanonicalGrassSpeciesJson(),
		VegetationTest::GenerousLoadBudget(),
		first_species,
		&error));
	first_species.name.assign(256, 'a');
	auto long_asset_path = [](const std::string& leaf)
	{
		std::string path = "vegetation/";
		while (path.size() < 1024)
		{
			path += "segment/";
		}
		path += leaf;
		return path;
	};
	for (size_t index = 0; index < first_species.mesh_lods.size(); ++index)
	{
		first_species.mesh_lods[index].mesh_asset_path =
			long_asset_path("first-lod" + std::to_string(index) + ".ashmesh");
	}
	first_species.render.impostor_asset_path = long_asset_path("first-impostor.dds");
	first_species.render.chunk_hlod_asset_path = long_asset_path("first-hlod.ashmesh");

	std::vector<uint8_t> first_species_bytes{};
	REQUIRE(AshEngine::encode_vegetation_species(
		first_species, first_species_bytes, &error));
	AshEngine::VegetationSpecies decoded_first_species{};
	AshEngine::VegetationLoadCost first_species_codec_cost{};
	REQUIRE(AshEngine::decode_vegetation_species(
		first_species_bytes,
		VegetationTest::GenerousLoadBudget(),
		decoded_first_species,
		&error,
		&first_species_codec_cost));

	AshEngine::VegetationSpecies second_species = first_species;
	second_species.species_id.back() ^= 0x40;
	second_species.name.assign(256, 'b');
	std::vector<uint8_t> second_species_bytes{};
	REQUIRE(AshEngine::encode_vegetation_species(
		second_species, second_species_bytes, &error));
	AshEngine::VegetationSpecies decoded_second_species{};
	AshEngine::VegetationLoadCost second_species_codec_cost{};
	REQUIRE(AshEngine::decode_vegetation_species(
		second_species_bytes,
		VegetationTest::GenerousLoadBudget(),
		decoded_second_species,
		&error,
		&second_species_codec_cost));

	auto make_palette = [](const AshEngine::VegetationSpecies& species,
		const std::vector<uint8_t>& canonical,
		const std::string& path)
	{
		AshEngine::VegetationPaletteEntry entry{};
		entry.species_id = species.species_id;
		entry.species_sha256 = AshEngine::vegetation_sha256(
			canonical.data(), canonical.size());
		entry.species_asset_path = path;
		return entry;
	};
	const AshEngine::VegetationPaletteEntry first_entry = make_palette(
		decoded_first_species, first_species_bytes, "vegetation/first.AshVegetation");
	const AshEngine::VegetationPaletteEntry second_entry = make_palette(
		decoded_second_species, second_species_bytes, "vegetation/second.AshVegetation");

	AshEngine::VegetationLayerSnapshot layer = VegetationTest::ResolvedMinimalLayerSnapshot();
	layer.palette = { first_entry, second_entry };
	std::sort(layer.palette.begin(), layer.palette.end(),
		[](const AshEngine::VegetationPaletteEntry& lhs,
			const AshEngine::VegetationPaletteEntry& rhs)
		{
			return lhs.species_id < rhs.species_id;
		});
	layer.tiles[0].planes[1].species_id = first_entry.species_id;
	const std::vector<uint8_t> layer_bytes = EncodeLayer(layer);
	AshEngine::VegetationLayerSnapshot decoded_layer{};
	AshEngine::VegetationLoadCost layer_codec_cost{};
	REQUIRE(AshEngine::decode_vegetation_layer(
		layer_bytes,
		VegetationTest::GenerousLoadBudget(),
		decoded_layer,
		&error,
		&layer_codec_cost));

	AshEngine::VegetationChunk chunk = VegetationTest::ResolvedMinimalChunk();
	chunk.species = { first_entry };
	const std::vector<uint8_t> chunk_bytes = EncodeChunk(chunk);
	AshEngine::VegetationChunk decoded_chunk{};
	AshEngine::VegetationLoadCost chunk_codec_cost{};
	REQUIRE(AshEngine::decode_vegetation_chunk(
		chunk_bytes,
		VegetationTest::GenerousLoadBudget(),
		decoded_chunk,
		&error,
		&chunk_codec_cost));

	VegetationTest::ScopedAssetRoot root("codec-cost-boundaries");
	root.Write("vegetation/first.AshVegetation", first_species_bytes);
	root.Write("vegetation/second.AshVegetation", second_species_bytes);
	root.Write("flora/multi.AshVegetationLayer", layer_bytes);
	root.Write("flora/single.AshVegetationChunk", chunk_bytes);

	AshEngine::AssetDatabase measured_database = AshEngine::AssetDatabase::create(root.Path());
	const auto measured_species = measured_database.load_vegetation_species_by_path(
		"vegetation/first.AshVegetation", VegetationTest::GenerousLoadBudget());
	const auto measured_layer = measured_database.load_vegetation_layer_by_path(
		"flora/multi.AshVegetationLayer", VegetationTest::GenerousLoadBudget());
	const auto measured_chunk = measured_database.load_vegetation_chunk_by_path(
		"flora/single.AshVegetationChunk", VegetationTest::GenerousLoadBudget());
	REQUIRE(measured_species.asset != nullptr);
	REQUIRE(measured_layer.asset != nullptr);
	REQUIRE(measured_chunk.asset != nullptr);
	CheckVegetationCostEqual(measured_species.cost, first_species_codec_cost);
	CheckVegetationCostEqual(measured_layer.cost, layer_codec_cost);
	CheckVegetationCostEqual(measured_chunk.cost, chunk_codec_cost);

	const AshEngine::VegetationLoadBudget exact_species = ExactBudgetFor({
		first_species_codec_cost });
	const AshEngine::VegetationLoadBudget exact_layer = ExactBudgetFor({
		layer_codec_cost, first_species_codec_cost, second_species_codec_cost });
	const AshEngine::VegetationLoadBudget exact_chunk = ExactBudgetFor({
		chunk_codec_cost, first_species_codec_cost });
	CHECK(first_species_codec_cost.file_bytes + second_species_codec_cost.file_bytes >
		exact_layer.max_file_bytes);
	CHECK(first_species_codec_cost.payload_bytes + second_species_codec_cost.payload_bytes >
		exact_layer.max_payload_bytes);
	CHECK(first_species_codec_cost.decoded_bytes + second_species_codec_cost.decoded_bytes >
		exact_layer.max_decoded_bytes);

	AshEngine::AssetDatabase exact_database = AshEngine::AssetDatabase::create(root.Path());
	for (size_t attempt = 0; attempt < 2; ++attempt)
	{
		const auto exact_species_result = exact_database.load_vegetation_species_by_path(
			"vegetation/first.AshVegetation", exact_species);
		const auto exact_layer_result = exact_database.load_vegetation_layer_by_path(
			"flora/multi.AshVegetationLayer", exact_layer);
		const auto exact_chunk_result = exact_database.load_vegetation_chunk_by_path(
			"flora/single.AshVegetationChunk", exact_chunk);
		REQUIRE(exact_species_result.asset != nullptr);
		REQUIRE(exact_layer_result.asset != nullptr);
		REQUIRE(exact_chunk_result.asset != nullptr);
		CHECK(exact_species_result.failure == AshEngine::VegetationAssetLoadFailure::None);
		CHECK(exact_layer_result.failure == AshEngine::VegetationAssetLoadFailure::None);
		CHECK(exact_chunk_result.failure == AshEngine::VegetationAssetLoadFailure::None);
		CheckVegetationCostEqual(exact_species_result.cost, first_species_codec_cost);
		CheckVegetationCostEqual(exact_layer_result.cost, layer_codec_cost);
		CheckVegetationCostEqual(exact_chunk_result.cost, chunk_codec_cost);
	}
}

TEST_CASE("Vegetation AssetDatabase validates dependency digests from canonical Species re-encoding")
{
	const std::vector<uint8_t> canonical_source = VegetationTest::CanonicalGrassSpeciesJson();
	std::vector<uint8_t> noncanonical_source{ ' ', '\t', '\r', '\n' };
	noncanonical_source.insert(
		noncanonical_source.end(), canonical_source.begin(), canonical_source.end());
	CHECK(AshEngine::vegetation_sha256(
		noncanonical_source.data(), noncanonical_source.size()) !=
		AshEngine::vegetation_sha256(canonical_source.data(), canonical_source.size()));

	AshEngine::VegetationSpecies decoded{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		noncanonical_source,
		VegetationTest::GenerousLoadBudget(),
		decoded,
		&error));
	std::vector<uint8_t> canonical_reencoded{};
	REQUIRE(AshEngine::encode_vegetation_species(
		decoded, canonical_reencoded, &error));
	CHECK(canonical_reencoded == canonical_source);

	AshEngine::VegetationLayerSnapshot layer = VegetationTest::ResolvedMinimalLayerSnapshot();
	layer.palette[0].species_id = decoded.species_id;
	layer.palette[0].species_sha256 = AshEngine::vegetation_sha256(
		canonical_reencoded.data(), canonical_reencoded.size());
	layer.palette[0].species_asset_path = "vegetation/noncanonical.AshVegetation";
	layer.tiles[0].planes[1].species_id = decoded.species_id;

	VegetationTest::ScopedAssetRoot root("noncanonical-species-digest");
	root.Write("vegetation/noncanonical.AshVegetation", noncanonical_source);
	root.Write("flora/noncanonical.AshVegetationLayer", EncodeLayer(layer));
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const auto loaded = database.load_vegetation_layer_by_path(
		"flora/noncanonical.AshVegetationLayer",
		VegetationTest::GenerousLoadBudget());
	REQUIRE(loaded.asset != nullptr);
	CHECK(loaded.failure == AshEngine::VegetationAssetLoadFailure::None);
	CHECK(loaded.asset->palette[0].species_sha256 == layer.palette[0].species_sha256);
}

TEST_CASE("Vegetation AssetDatabase pure reducers isolate budgets epochs and catalog outcomes")
{
	AshEngine::VegetationAssetInFlightAdmissionInput admission{};
	admission.has_existing = true;
	admission.requested_type = AshEngine::AssetType::Species;
	admission.existing_type = AshEngine::AssetType::Species;
	admission.requested_id = 7;
	admission.existing_id = 7;
	admission.requested_epoch = 11;
	admission.existing_epoch = 11;
	admission.requested_budget = VegetationTest::GenerousLoadBudget();
	admission.existing_budget = admission.requested_budget;
	CHECK(AshEngine::decide_vegetation_asset_in_flight_admission(admission) ==
		AshEngine::VegetationAssetInFlightAdmissionDecision::JoinExisting);
	auto requires_new_request = [&](const AshEngine::VegetationAssetInFlightAdmissionInput& input)
	{
		CHECK(AshEngine::decide_vegetation_asset_in_flight_admission(input) ==
			AshEngine::VegetationAssetInFlightAdmissionDecision::LaunchNew);
	};
	AshEngine::VegetationAssetInFlightAdmissionInput different = admission;
	different.has_existing = false;
	requires_new_request(different);
	different = admission;
	different.existing_type = AshEngine::AssetType::Layer;
	requires_new_request(different);
	different = admission;
	++different.existing_id;
	requires_new_request(different);
	different = admission;
	++different.existing_epoch;
	requires_new_request(different);
	different = admission;
	--different.existing_budget.max_file_bytes;
	requires_new_request(different);
	different = admission;
	--different.existing_budget.max_payload_bytes;
	requires_new_request(different);
	different = admission;
	--different.existing_budget.max_decoded_bytes;
	requires_new_request(different);
	different = admission;
	--different.existing_budget.max_palette_records;
	requires_new_request(different);
	different = admission;
	--different.existing_budget.max_tile_records;
	requires_new_request(different);
	different = admission;
	--different.existing_budget.max_instance_records;
	requires_new_request(different);

	AshEngine::VegetationAssetCompletionPublicationInput completion{};
	completion.captured_epoch = 1;
	completion.current_epoch = 2;
	completion.captured_request_token = 3;
	completion.current_in_flight_token = 3;
	completion.completion_has_asset = true;
	const auto stale = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(stale.erase_matching_in_flight);
	CHECK_FALSE(stale.publish_cache);
	CHECK_FALSE(stale.publish_global_state);
	completion.current_epoch = 1;
	completion.current_in_flight_token = 4;
	const auto token_mismatch =
		AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(token_mismatch.erase_matching_in_flight);
	CHECK(token_mismatch.publish_cache);
	completion.current_in_flight_token = completion.captured_request_token;
	completion.completion_has_asset = false;
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::Missing;
	completion.completion_error = " missing ";
	const auto missing = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK(missing.erase_matching_in_flight);
	CHECK(missing.publish_global_state);
	CHECK(missing.global_state == AshEngine::AssetLoadState::Missing);
	completion.current_global_state = missing.global_state;
	completion.current_global_failure = missing.global_failure;
	completion.current_global_error = missing.global_error;
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::Io;
	completion.completion_error = "io";
	const auto io = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK(io.publish_global_state);
	CHECK(io.global_failure == AshEngine::VegetationAssetLoadFailure::Io);
	completion.current_global_state = io.global_state;
	completion.current_global_failure = io.global_failure;
	completion.current_global_error = io.global_error;
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::InvalidData;
	completion.completion_error = " z-invalid ";
	const auto invalid = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK(invalid.publish_global_state);
	CHECK(invalid.global_failure == AshEngine::VegetationAssetLoadFailure::InvalidData);
	completion.current_global_state = invalid.global_state;
	completion.current_global_failure = invalid.global_failure;
	completion.current_global_error = invalid.global_error;
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::Missing;
	completion.completion_error = "missing-later";
	const auto lower_rank = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(lower_rank.publish_global_state);
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::InvalidData;
	completion.completion_error = " a-invalid ";
	const auto lexical_winner =
		AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK(lexical_winner.publish_global_state);
	CHECK(lexical_winner.global_error == "a-invalid");
	completion.current_global_state = lexical_winner.global_state;
	completion.current_global_failure = lexical_winner.global_failure;
	completion.current_global_error = lexical_winner.global_error;
	completion.completion_error = "b-invalid";
	const auto lexical_loser =
		AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(lexical_loser.publish_global_state);
	completion.current_global_failure = AshEngine::VegetationAssetLoadFailure::Io;
	completion.current_global_error = "z-io";
	completion.completion_error = "  C:\\vegetation\\broken.AshVegetation  ";
	const auto normalized_invalid =
		AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK(normalized_invalid.publish_global_state);
	CHECK(normalized_invalid.global_failure == AshEngine::VegetationAssetLoadFailure::InvalidData);
	CHECK(normalized_invalid.global_error == "C:/vegetation/broken.AshVegetation");
	completion.current_global_state = normalized_invalid.global_state;
	completion.current_global_failure = normalized_invalid.global_failure;
	completion.current_global_error = normalized_invalid.global_error;
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::None;
	completion.completion_error.clear();
	completion.completion_has_asset = true;
	const auto loaded = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK(loaded.publish_cache);
	CHECK(loaded.publish_global_state);
	CHECK(loaded.global_state == AshEngine::AssetLoadState::Loaded);
	completion.current_global_state = loaded.global_state;
	completion.current_global_failure = loaded.global_failure;
	completion.current_global_error = loaded.global_error;
	completion.completion_has_asset = false;
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::InvalidData;
	completion.completion_error = "cannot-downgrade-loaded";
	const auto no_downgrade =
		AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(no_downgrade.publish_global_state);
	completion.current_global_state = AshEngine::AssetLoadState::Unloaded;
	completion.current_global_failure = AshEngine::VegetationAssetLoadFailure::None;
	completion.current_global_error.clear();
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::BudgetExceeded;
	const auto budget_local = AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(budget_local.publish_global_state);
	completion.completion_failure = AshEngine::VegetationAssetLoadFailure::WrongType;
	const auto wrong_type_local =
		AshEngine::decide_vegetation_asset_completion_publication(completion);
	CHECK_FALSE(wrong_type_local.publish_global_state);

	AshEngine::VegetationCatalogPublicationInput catalog{};
	catalog.captured_epoch = 4;
	catalog.current_epoch = 4;
	catalog.captured_root = "old-root";
	catalog.current_root = "old-root";
	catalog.scan_outcome = AshEngine::VegetationCatalogScanOutcome::InvalidRoot;
	CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
		AshEngine::VegetationCatalogPublicationDecision::ResetInvalidRoot);
	catalog.current_root = "new-root";
	CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
		AshEngine::VegetationCatalogPublicationDecision::DiscardStale);
	catalog.current_root = catalog.captured_root;
	++catalog.current_epoch;
	CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
		AshEngine::VegetationCatalogPublicationDecision::DiscardStale);
	catalog.current_epoch = catalog.captured_epoch;
	for (const AshEngine::VegetationCatalogScanOutcome outcome : {
		AshEngine::VegetationCatalogScanOutcome::Succeeded,
		AshEngine::VegetationCatalogScanOutcome::InvalidRoot,
		AshEngine::VegetationCatalogScanOutcome::Failed })
	{
		catalog.scan_outcome = outcome;
		catalog.current_root = "new-root";
		CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
			AshEngine::VegetationCatalogPublicationDecision::DiscardStale);
		catalog.current_root = catalog.captured_root;
		++catalog.current_epoch;
		CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
			AshEngine::VegetationCatalogPublicationDecision::DiscardStale);
		catalog.current_epoch = catalog.captured_epoch;
	}
	catalog.scan_outcome = AshEngine::VegetationCatalogScanOutcome::Succeeded;
	CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
		AshEngine::VegetationCatalogPublicationDecision::PublishReplacement);
	catalog.scan_outcome = AshEngine::VegetationCatalogScanOutcome::Failed;
	CHECK(AshEngine::decide_vegetation_catalog_publication(catalog) ==
		AshEngine::VegetationCatalogPublicationDecision::KeepLastKnownGood);
}

TEST_CASE("Vegetation AssetDatabase request-local failures do not poison typed global state")
{
	VegetationTest::ScopedAssetRoot root("request-local-state");
	const std::vector<uint8_t> species_bytes = VegetationTest::CanonicalGrassSpeciesJson();
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation", species_bytes);
	std::vector<uint8_t> corrupt_species = species_bytes;
	corrupt_species.push_back('x');
	root.Write("vegetation/CorruptSpecies.AshVegetation", corrupt_species);
	root.Write("notes/readme.txt", std::vector<uint8_t>{ 'o', 'k' });
	std::vector<uint8_t> corrupt_layer = VegetationTest::ResolvedMinimalLayerBytes();
	corrupt_layer.push_back(0x7f);
	root.Write("flora/corrupt.AshVegetationLayer", corrupt_layer);
	std::vector<uint8_t> corrupt_chunk = VegetationTest::ResolvedMinimalChunkBytes();
	corrupt_chunk.push_back(0x7f);
	root.Write("flora/corrupt.AshVegetationChunk", corrupt_chunk);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* species_info =
		database.find_asset_by_path("vegetation/Phase2ManualSpecies.AshVegetation");
	const AshEngine::AssetInfo* text_info = database.find_asset_by_path("notes/readme.txt");
	const AshEngine::AssetInfo* corrupt_species_info =
		database.find_asset_by_path("vegetation/CorruptSpecies.AshVegetation");
	const AshEngine::AssetInfo* corrupt_layer_info =
		database.find_asset_by_path("flora/corrupt.AshVegetationLayer");
	const AshEngine::AssetInfo* corrupt_chunk_info =
		database.find_asset_by_path("flora/corrupt.AshVegetationChunk");
	REQUIRE(species_info != nullptr);
	REQUIRE(text_info != nullptr);
	REQUIRE(corrupt_species_info != nullptr);
	REQUIRE(corrupt_layer_info != nullptr);
	REQUIRE(corrupt_chunk_info != nullptr);

	std::string legacy_text = "sentinel";
	CHECK_FALSE(database.load_text_by_id(species_info->id, legacy_text));
	CHECK(legacy_text == "sentinel");
	CHECK(database.get_asset_load_state(species_info->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(database.get_last_error().empty());
	std::vector<uint8_t> legacy_binary{ 0xaa };
	CHECK_FALSE(database.load_binary_by_path(
		"vegetation/Phase2ManualSpecies.AshVegetation", legacy_binary));
	CHECK(legacy_binary == std::vector<uint8_t>{ 0xaa });
	CHECK(database.get_asset_load_state(species_info->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(database.get_last_error().empty());

	const auto wrong_type = database.load_vegetation_species_by_id(
		text_info->id, VegetationTest::GenerousLoadBudget());
	CHECK(wrong_type.asset == nullptr);
	CHECK(wrong_type.failure == AshEngine::VegetationAssetLoadFailure::WrongType);
	CHECK(database.get_asset_load_state(text_info->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(database.get_last_error().empty());
	const auto missing = database.load_vegetation_species_by_path(
		"vegetation/missing.AshVegetation", VegetationTest::GenerousLoadBudget());
	CHECK(missing.asset == nullptr);
	CHECK(missing.failure == AshEngine::VegetationAssetLoadFailure::Missing);
	CHECK(database.get_last_error().empty());

	const auto tiny_cold = database.load_vegetation_species_by_id(
		species_info->id, TinyFileBudget());
	CHECK(tiny_cold.asset == nullptr);
	CHECK(tiny_cold.failure == AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CHECK(database.get_asset_load_state(species_info->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(database.get_last_error().empty());
	const auto loaded = database.load_vegetation_species_by_id(
		species_info->id, VegetationTest::GenerousLoadBudget());
	REQUIRE(loaded.asset != nullptr);
	CHECK(database.get_asset_load_state(species_info->id) == AshEngine::AssetLoadState::Loaded);
	const auto tiny_warm = database.load_vegetation_species_by_id(
		species_info->id, TinyFileBudget());
	CHECK(tiny_warm.asset == nullptr);
	CHECK(tiny_warm.failure == AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CHECK(database.get_asset_load_state(species_info->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_asset_last_error(species_info->id).empty());
	const auto bad_species = database.load_vegetation_species_by_id(
		corrupt_species_info->id, VegetationTest::GenerousLoadBudget());
	CHECK(bad_species.asset == nullptr);
	CHECK(bad_species.failure == AshEngine::VegetationAssetLoadFailure::InvalidData);
	CHECK(database.get_asset_load_state(corrupt_species_info->id) == AshEngine::AssetLoadState::Failed);

	const auto bad_layer = database.load_vegetation_layer_by_id(
		corrupt_layer_info->id, VegetationTest::GenerousLoadBudget());
	CHECK(bad_layer.asset == nullptr);
	CHECK(bad_layer.failure == AshEngine::VegetationAssetLoadFailure::InvalidData);
	CHECK(database.get_asset_load_state(corrupt_layer_info->id) == AshEngine::AssetLoadState::Failed);
	const auto bad_chunk = database.load_vegetation_chunk_by_id(
		corrupt_chunk_info->id, VegetationTest::GenerousLoadBudget());
	CHECK(bad_chunk.asset == nullptr);
	CHECK(bad_chunk.failure == AshEngine::VegetationAssetLoadFailure::InvalidData);
	CHECK(database.get_asset_load_state(corrupt_chunk_info->id) == AshEngine::AssetLoadState::Failed);
}

TEST_CASE("Vegetation AssetDatabase typed failures cover every type accessor without negative caching")
{
	VegetationTest::ScopedAssetRoot root("typed-failure-matrix");
	root.Write("vegetation/good.AshVegetation", VegetationTest::CanonicalGrassSpeciesJson());
	root.Write("flora/good.AshVegetationLayer", VegetationTest::ResolvedMinimalLayerBytes());
	root.Write("flora/good.AshVegetationChunk", VegetationTest::ResolvedMinimalChunkBytes());
	root.Write("notes/readme.txt", std::vector<uint8_t>{ 'o', 'k' });
	std::vector<uint8_t> bad_species = VegetationTest::CanonicalGrassSpeciesJson();
	bad_species.push_back('x');
	root.Write("vegetation/bad.AshVegetation", bad_species);
	std::vector<uint8_t> bad_layer = VegetationTest::ResolvedMinimalLayerBytes();
	bad_layer.push_back(0x44);
	root.Write("flora/bad.AshVegetationLayer", bad_layer);
	std::vector<uint8_t> bad_chunk = VegetationTest::ResolvedMinimalChunkBytes();
	bad_chunk.push_back(0x44);
	root.Write("flora/bad.AshVegetationChunk", bad_chunk);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* species = database.find_asset_by_path("vegetation/good.AshVegetation");
	const AshEngine::AssetInfo* layer = database.find_asset_by_path("flora/good.AshVegetationLayer");
	const AshEngine::AssetInfo* text = database.find_asset_by_path("notes/readme.txt");
	const AshEngine::AssetInfo* bad_species_info = database.find_asset_by_path("vegetation/bad.AshVegetation");
	const AshEngine::AssetInfo* bad_layer_info = database.find_asset_by_path("flora/bad.AshVegetationLayer");
	const AshEngine::AssetInfo* bad_chunk_info = database.find_asset_by_path("flora/bad.AshVegetationChunk");
	REQUIRE(species != nullptr);
	REQUIRE(layer != nullptr);
	REQUIRE(text != nullptr);
	REQUIRE(bad_species_info != nullptr);
	REQUIRE(bad_layer_info != nullptr);
	REQUIRE(bad_chunk_info != nullptr);
	const AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
	const AshEngine::AssetId missing_id = ~AshEngine::AssetId{ 0 };

	CheckVegetationFailureShape(database.load_vegetation_species_by_id(text->id, budget),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_species_by_path("notes/readme.txt", budget),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_species_by_id_async(text->id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_species_by_path_async("notes/readme.txt", budget).get(),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_id(species->id, budget),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path("vegetation/good.AshVegetation", budget),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_id_async(species->id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path_async("vegetation/good.AshVegetation", budget).get(),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id(layer->id, budget),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_path("flora/good.AshVegetationLayer", budget),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id_async(layer->id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::WrongType);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_path_async("flora/good.AshVegetationLayer", budget).get(),
		AshEngine::VegetationAssetLoadFailure::WrongType);

	CheckVegetationFailureShape(database.load_vegetation_species_by_id(missing_id, budget),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_species_by_path("missing.AshVegetation", budget),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_species_by_id_async(missing_id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_species_by_path_async("missing.AshVegetation", budget).get(),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_id(missing_id, budget),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path("missing.AshVegetationLayer", budget),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_id_async(missing_id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path_async("missing.AshVegetationLayer", budget).get(),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id(missing_id, budget),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_path("missing.AshVegetationChunk", budget),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id_async(missing_id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_path_async("missing.AshVegetationChunk", budget).get(),
		AshEngine::VegetationAssetLoadFailure::Missing);

	CheckVegetationFailureShape(database.load_vegetation_species_by_id(bad_species_info->id, budget),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_species_by_path("vegetation/bad.AshVegetation", budget),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_species_by_id_async(bad_species_info->id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_species_by_path_async("vegetation/bad.AshVegetation", budget).get(),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_id(bad_layer_info->id, budget),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path("flora/bad.AshVegetationLayer", budget),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_id_async(bad_layer_info->id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path_async("flora/bad.AshVegetationLayer", budget).get(),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id(bad_chunk_info->id, budget),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_path("flora/bad.AshVegetationChunk", budget),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id_async(bad_chunk_info->id, budget).get(),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_path_async("flora/bad.AshVegetationChunk", budget).get(),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
}

TEST_CASE("Vegetation AssetDatabase admitted retries preserve failure precedence and later load upgrades")
{
	VegetationTest::ScopedAssetRoot root("admitted-retry-precedence");
	std::vector<uint8_t> corrupt = VegetationTest::CanonicalGrassSpeciesJson();
	corrupt.push_back('x');
	const std::filesystem::path relative_path = "vegetation/retry.AshVegetation";
	root.Write(relative_path, corrupt);
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	const AshEngine::AssetId id = info->id;
	CheckVegetationFailureShape(database.load_vegetation_species_by_id(
		id, VegetationTest::GenerousLoadBudget()),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	CHECK(database.get_asset_load_state(id) == AshEngine::AssetLoadState::Failed);
	const std::string invalid_error = database.get_asset_last_error(id);
	const std::string invalid_database_error = database.get_last_error();
	REQUIRE_FALSE(invalid_error.empty());
	REQUIRE_FALSE(invalid_database_error.empty());

	std::error_code remove_error{};
	std::filesystem::remove(root.Path() / relative_path, remove_error);
	REQUIRE_FALSE(static_cast<bool>(remove_error));
	ScopedSingleWorkerThreading threading{};
	const auto blocker_state = std::make_shared<BlockingWorkerState>();
	BlockingWorkerReleaseGuard release_guard(blocker_state);
	const AshEngine::ThreadCommandFuture blocker = AshEngine::Detail::enqueue_worker_command(
		"vegetation-retry-precedence-blocker", [blocker_state]()
		{
			blocker_state->started_promise.set_value();
			blocker_state->release.wait();
		});
	REQUIRE(blocker_state->started.wait_for(std::chrono::seconds(2)) ==
		std::future_status::ready);
	const auto missing_future = database.load_vegetation_species_by_path_async(
		relative_path, VegetationTest::GenerousLoadBudget());
	CHECK(database.get_asset_load_state(id) == AshEngine::AssetLoadState::Failed);
	CHECK(database.get_asset_last_error(id) == invalid_error);
	CHECK(database.get_last_error() == invalid_database_error);
	release_guard.ReleaseNoexcept();
	REQUIRE(blocker.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	CHECK_NOTHROW(blocker.get());
	REQUIRE(missing_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	CheckVegetationFailureShape(missing_future.get(),
		AshEngine::VegetationAssetLoadFailure::Missing);
	CHECK(database.get_asset_load_state(id) == AshEngine::AssetLoadState::Failed);
	CHECK(database.get_asset_last_error(id) == invalid_error);
	CHECK(database.get_last_error() == invalid_database_error);

	root.Write(relative_path, VegetationTest::CanonicalGrassSpeciesJson());
	const auto loaded_future = database.load_vegetation_species_by_path_async(
		relative_path, VegetationTest::GenerousLoadBudget());
	REQUIRE(loaded_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	const auto loaded = loaded_future.get();
	REQUIRE(loaded.asset != nullptr);
	CHECK(loaded.state == AshEngine::AssetLoadState::Loaded);
	CHECK(loaded.failure == AshEngine::VegetationAssetLoadFailure::None);
	CHECK(loaded.error.empty());
	CHECK(database.get_asset_load_state(id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_asset_last_error(id).empty());
	CHECK(database.get_last_error().empty());
}

TEST_CASE("Vegetation AssetDatabase validates Species dependency identity digest and candidate contract")
{
	VegetationTest::ScopedAssetRoot root("dependency-contract");
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::CanonicalGrassSpeciesJson());

	AshEngine::VegetationLayerSnapshot wrong_id_layer =
		VegetationTest::ResolvedMinimalLayerSnapshot();
	wrong_id_layer.palette[0].species_id[0] ^= 0x40;
	wrong_id_layer.tiles[0].planes[1].species_id = wrong_id_layer.palette[0].species_id;
	root.Write("flora/wrong-id.AshVegetationLayer", EncodeLayer(wrong_id_layer));

	AshEngine::VegetationLayerSnapshot wrong_digest_layer =
		VegetationTest::ResolvedMinimalLayerSnapshot();
	wrong_digest_layer.palette[0].species_sha256[0] ^= 0x40;
	root.Write("flora/wrong-digest.AshVegetationLayer", EncodeLayer(wrong_digest_layer));

	AshEngine::VegetationLayerSnapshot missing_species_layer =
		VegetationTest::ResolvedMinimalLayerSnapshot();
	missing_species_layer.palette[0].species_asset_path =
		"vegetation/MissingSpecies.AshVegetation";
	root.Write("flora/missing-species.AshVegetationLayer", EncodeLayer(missing_species_layer));

	AshEngine::VegetationSpecies species{};
	std::string decode_error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		VegetationTest::CanonicalGrassSpeciesJson(),
		VegetationTest::GenerousLoadBudget(),
		species,
		&decode_error));
	AshEngine::VegetationChunk wrong_candidate_chunk = VegetationTest::ResolvedMinimalChunk();
	wrong_candidate_chunk.instances[0].candidate_ordinal = species.placement.candidates_per_cell;
	root.Write("flora/wrong-candidate.AshVegetationChunk", EncodeChunk(wrong_candidate_chunk));
	AshEngine::VegetationChunk wrong_id_chunk = VegetationTest::ResolvedMinimalChunk();
	wrong_id_chunk.species[0].species_id[0] ^= 0x20;
	root.Write("flora/wrong-id.AshVegetationChunk", EncodeChunk(wrong_id_chunk));
	AshEngine::VegetationChunk wrong_digest_chunk = VegetationTest::ResolvedMinimalChunk();
	wrong_digest_chunk.species[0].species_sha256[0] ^= 0x20;
	root.Write("flora/wrong-digest.AshVegetationChunk", EncodeChunk(wrong_digest_chunk));

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const std::array<std::filesystem::path, 3> invalid_layers{
		"flora/missing-species.AshVegetationLayer",
		"flora/wrong-id.AshVegetationLayer",
		"flora/wrong-digest.AshVegetationLayer"
	};
	const std::array<std::filesystem::path, 3> invalid_chunks{
		"flora/wrong-candidate.AshVegetationChunk",
		"flora/wrong-id.AshVegetationChunk",
		"flora/wrong-digest.AshVegetationChunk"
	};
	std::array<AshEngine::AssetId, 3> invalid_layer_ids{};
	std::array<AshEngine::AssetId, 3> invalid_chunk_ids{};
	for (size_t index = 0; index < invalid_layers.size(); ++index)
	{
		const AshEngine::AssetInfo* info = database.find_asset_by_path(invalid_layers[index]);
		REQUIRE(info != nullptr);
		invalid_layer_ids[index] = info->id;
		CheckVegetationFailureShape(database.load_vegetation_layer_by_path(
			invalid_layers[index], VegetationTest::GenerousLoadBudget()),
			AshEngine::VegetationAssetLoadFailure::InvalidData);
		CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Failed);
		CHECK_FALSE(database.get_asset_last_error(info->id).empty());
	}
	for (size_t index = 0; index < invalid_chunks.size(); ++index)
	{
		const AshEngine::AssetInfo* info = database.find_asset_by_path(invalid_chunks[index]);
		REQUIRE(info != nullptr);
		invalid_chunk_ids[index] = info->id;
		const auto result = index == invalid_chunks.size() - 1 ?
			database.load_vegetation_chunk_by_path_async(
				invalid_chunks[index], VegetationTest::GenerousLoadBudget()).get() :
			database.load_vegetation_chunk_by_path(
				invalid_chunks[index], VegetationTest::GenerousLoadBudget());
		CheckVegetationFailureShape(
			result, AshEngine::VegetationAssetLoadFailure::InvalidData);
		CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Failed);
		CHECK_FALSE(database.get_asset_last_error(info->id).empty());
	}
	CHECK_FALSE(database.get_last_error().empty());

	for (const std::filesystem::path& path : invalid_layers)
	{
		ReplaceFixtureFile(root.Path() / path, VegetationTest::ResolvedMinimalLayerBytes());
	}
	for (const std::filesystem::path& path : invalid_chunks)
	{
		ReplaceFixtureFile(root.Path() / path, VegetationTest::ResolvedMinimalChunkBytes());
	}
	REQUIRE(database.refresh());
	CHECK(database.get_last_error().empty());
	for (size_t index = 0; index < invalid_layers.size(); ++index)
	{
		CHECK(database.get_asset_load_state(invalid_layer_ids[index]) ==
			AshEngine::AssetLoadState::Unloaded);
		const auto repaired = database.load_vegetation_layer_by_id(
			invalid_layer_ids[index], VegetationTest::GenerousLoadBudget());
		REQUIRE(repaired.asset != nullptr);
		CHECK(repaired.failure == AshEngine::VegetationAssetLoadFailure::None);
	}
	for (size_t index = 0; index < invalid_chunks.size(); ++index)
	{
		CHECK(database.get_asset_load_state(invalid_chunk_ids[index]) ==
			AshEngine::AssetLoadState::Unloaded);
		const auto repaired = database.load_vegetation_chunk_by_id_async(
			invalid_chunk_ids[index], VegetationTest::GenerousLoadBudget()).get();
		REQUIRE(repaired.asset != nullptr);
		CHECK(repaired.failure == AshEngine::VegetationAssetLoadFailure::None);
	}
}

TEST_CASE("Vegetation AssetDatabase filesystem I/O failures are shaped and retryable for every typed asset")
{
	VegetationTest::ScopedAssetRoot root("typed-io-retry");
	const std::filesystem::path species_path =
		"vegetation/Phase2ManualSpecies.AshVegetation";
	const std::filesystem::path layer_path = "flora/meadow.AshVegetationLayer";
	const std::filesystem::path chunk_path = "flora/0_0.AshVegetationChunk";
	const std::vector<uint8_t> species_bytes = VegetationTest::CanonicalGrassSpeciesJson();
	const std::vector<uint8_t> layer_bytes = VegetationTest::ResolvedMinimalLayerBytes();
	const std::vector<uint8_t> chunk_bytes = VegetationTest::ResolvedMinimalChunkBytes();
	root.Write(species_path, species_bytes);
	root.Write(layer_path, layer_bytes);
	root.Write(chunk_path, chunk_bytes);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* species_info = database.find_asset_by_path(species_path);
	const AshEngine::AssetInfo* layer_info = database.find_asset_by_path(layer_path);
	const AshEngine::AssetInfo* chunk_info = database.find_asset_by_path(chunk_path);
	REQUIRE(species_info != nullptr);
	REQUIRE(layer_info != nullptr);
	REQUIRE(chunk_info != nullptr);
	const AshEngine::AssetId species_id = species_info->id;
	const AshEngine::AssetId layer_id = layer_info->id;
	const AshEngine::AssetId chunk_id = chunk_info->id;

	auto replace_with_directory = [&](const std::filesystem::path& relative_path)
	{
		std::error_code error{};
		std::filesystem::remove(root.Path() / relative_path, error);
		REQUIRE_FALSE(static_cast<bool>(error));
		REQUIRE(std::filesystem::create_directory(root.Path() / relative_path, error));
		REQUIRE_FALSE(static_cast<bool>(error));
	};

	replace_with_directory(species_path);
	CheckVegetationFailureShape(database.load_vegetation_species_by_id(
		species_id, VegetationTest::GenerousLoadBudget()),
		AshEngine::VegetationAssetLoadFailure::Io);
	CHECK(database.get_asset_load_state(species_id) == AshEngine::AssetLoadState::Failed);
	ReplaceFixtureFile(root.Path() / species_path, species_bytes);
	const auto species_retry = database.load_vegetation_species_by_path_async(
		species_path, VegetationTest::GenerousLoadBudget()).get();
	REQUIRE(species_retry.asset != nullptr);
	CHECK(species_retry.failure == AshEngine::VegetationAssetLoadFailure::None);
	CHECK(database.get_asset_load_state(species_id) == AshEngine::AssetLoadState::Loaded);

	replace_with_directory(layer_path);
	CheckVegetationFailureShape(database.load_vegetation_layer_by_path_async(
		layer_path, VegetationTest::GenerousLoadBudget()).get(),
		AshEngine::VegetationAssetLoadFailure::Io);
	CHECK(database.get_asset_load_state(layer_id) == AshEngine::AssetLoadState::Failed);
	ReplaceFixtureFile(root.Path() / layer_path, layer_bytes);
	const auto layer_retry = database.load_vegetation_layer_by_id(
		layer_id, VegetationTest::GenerousLoadBudget());
	REQUIRE(layer_retry.asset != nullptr);
	CHECK(layer_retry.failure == AshEngine::VegetationAssetLoadFailure::None);
	CHECK(database.get_asset_load_state(layer_id) == AshEngine::AssetLoadState::Loaded);

	replace_with_directory(chunk_path);
	CheckVegetationFailureShape(database.load_vegetation_chunk_by_id(
		chunk_id, VegetationTest::GenerousLoadBudget()),
		AshEngine::VegetationAssetLoadFailure::Io);
	CHECK(database.get_asset_load_state(chunk_id) == AshEngine::AssetLoadState::Failed);
	ReplaceFixtureFile(root.Path() / chunk_path, chunk_bytes);
	const auto chunk_retry = database.load_vegetation_chunk_by_path_async(
		chunk_path, VegetationTest::GenerousLoadBudget()).get();
	REQUIRE(chunk_retry.asset != nullptr);
	CHECK(chunk_retry.failure == AshEngine::VegetationAssetLoadFailure::None);
	CHECK(database.get_asset_load_state(chunk_id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_last_error().empty());
}

TEST_CASE("Vegetation AssetDatabase bounded public preview preserves output and rejects opaque payloads")
{
	VegetationTest::ScopedAssetRoot root("bounded-public");
	const std::vector<uint8_t> species_bytes = VegetationTest::CanonicalGrassSpeciesJson();
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation", species_bytes);
	std::vector<uint8_t> corrupt_species = species_bytes;
	corrupt_species.push_back('x');
	root.Write("vegetation/corrupt.AshVegetation", corrupt_species);
	root.Write("vegetation/empty.AshVegetation", std::vector<uint8_t>{});
	const std::vector<uint8_t> ordinary_text{ 'p', 'r', 'e', 'v', 'i', 'e', 'w' };
	root.Write("notes/readme.txt", ordinary_text);
	root.Write("flora/meadow.AshVegetationLayer", VegetationTest::ResolvedMinimalLayerBytes());
	root.Write("flora/0_0.AshVegetationChunk", VegetationTest::ResolvedMinimalChunkBytes());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* species_info =
		database.find_asset_by_path("vegetation/Phase2ManualSpecies.AshVegetation");
	const AshEngine::AssetInfo* corrupt_info =
		database.find_asset_by_path("vegetation/corrupt.AshVegetation");
	const AshEngine::AssetInfo* empty_info =
		database.find_asset_by_path("vegetation/empty.AshVegetation");
	const AshEngine::AssetInfo* layer_info =
		database.find_asset_by_path("flora/meadow.AshVegetationLayer");
	const AshEngine::AssetInfo* chunk_info =
		database.find_asset_by_path("flora/0_0.AshVegetationChunk");
	const AshEngine::AssetInfo* text_info = database.find_asset_by_path("notes/readme.txt");
	REQUIRE(species_info != nullptr);
	REQUIRE(corrupt_info != nullptr);
	REQUIRE(empty_info != nullptr);
	REQUIRE(layer_info != nullptr);
	REQUIRE(chunk_info != nullptr);
	REQUIRE(text_info != nullptr);
	CheckVegetationFailureShape(database.load_vegetation_species_by_id(
		corrupt_info->id, VegetationTest::GenerousLoadBudget()),
		AshEngine::VegetationAssetLoadFailure::InvalidData);
	std::string missing_text = "missing-sentinel";
	CHECK_FALSE(database.load_text_by_path("notes/missing.txt", missing_text));
	CHECK(missing_text == "missing-sentinel");
	const std::string database_error_before = database.get_last_error();
	REQUIRE_FALSE(database_error_before.empty());
	const AshEngine::AssetLoadState corrupt_state_before =
		database.get_asset_load_state(corrupt_info->id);
	const std::string corrupt_error_before = database.get_asset_last_error(corrupt_info->id);
	const AshEngine::AssetLoadState species_state_before =
		database.get_asset_load_state(species_info->id);
	const std::string species_error_before = database.get_asset_last_error(species_info->id);
	const AshEngine::AssetLoadState layer_state_before = database.get_asset_load_state(layer_info->id);
	const std::string layer_error_before = database.get_asset_last_error(layer_info->id);
	const AshEngine::AssetLoadState chunk_state_before = database.get_asset_load_state(chunk_info->id);
	const std::string chunk_error_before = database.get_asset_last_error(chunk_info->id);
	const AshEngine::AssetLoadState text_state_before = database.get_asset_load_state(text_info->id);
	const std::string text_error_before = database.get_asset_last_error(text_info->id);

	std::string preview = "old";
	REQUIRE(database.load_text_by_path_bounded(
		"vegetation/Phase2ManualSpecies.AshVegetation",
		static_cast<uint64_t>(species_bytes.size() + 17),
		preview));
	CHECK(std::vector<uint8_t>(preview.begin(), preview.end()) == species_bytes);
	REQUIRE(database.load_text_by_id_bounded(
		species_info->id, static_cast<uint64_t>(species_bytes.size()), preview));
	CHECK(std::vector<uint8_t>(preview.begin(), preview.end()) == species_bytes);
	preview = "unchanged";
	CHECK_FALSE(database.load_text_by_path_bounded(
		"vegetation/Phase2ManualSpecies.AshVegetation",
		static_cast<uint64_t>(species_bytes.size() - 1),
		preview));
	CHECK(preview == "unchanged");
	CHECK_FALSE(database.load_text_by_path_bounded(
		"flora/meadow.AshVegetationLayer", 1024ull * 1024ull, preview));
	CHECK(preview == "unchanged");
	CHECK_FALSE(database.load_text_by_id_bounded(
		chunk_info->id, 1024ull * 1024ull, preview));
	CHECK(preview == "unchanged");
	REQUIRE(database.load_text_by_id_bounded(empty_info->id, 0, preview));
	CHECK(preview.empty());
	preview = "zero-nonempty";
	CHECK_FALSE(database.load_text_by_id_bounded(species_info->id, 0, preview));
	CHECK(preview == "zero-nonempty");
	preview = "ordinary-old";
	REQUIRE(database.load_text_by_id_bounded(
		text_info->id, static_cast<uint64_t>(ordinary_text.size()), preview));
	CHECK(std::vector<uint8_t>(preview.begin(), preview.end()) == ordinary_text);
	preview = "ordinary-preserved";
	CHECK_FALSE(database.load_text_by_path_bounded(
		"notes/readme.txt", static_cast<uint64_t>(ordinary_text.size() - 1), preview));
	CHECK(preview == "ordinary-preserved");

	for (const AshEngine::AssetInfo* info : { species_info, layer_info, chunk_info })
	{
		std::string legacy_text = "text-sentinel";
		CHECK_FALSE(database.load_text_by_id(info->id, legacy_text));
		CHECK(legacy_text == "text-sentinel");
		legacy_text = "path-sentinel";
		CHECK_FALSE(database.load_text_by_path(info->relative_path, legacy_text));
		CHECK(legacy_text == "path-sentinel");
		std::vector<uint8_t> legacy_bytes{ 0xa5 };
		CHECK_FALSE(database.load_binary_by_id(info->id, legacy_bytes));
		CHECK(legacy_bytes == std::vector<uint8_t>{ 0xa5 });
		legacy_bytes = { 0x5a };
		CHECK_FALSE(database.load_binary_by_path(info->relative_path, legacy_bytes));
		CHECK(legacy_bytes == std::vector<uint8_t>{ 0x5a });
	}

	CHECK(database.get_asset_load_state(corrupt_info->id) == corrupt_state_before);
	CHECK(database.get_asset_last_error(corrupt_info->id) == corrupt_error_before);
	CHECK(database.get_asset_load_state(species_info->id) == species_state_before);
	CHECK(database.get_asset_last_error(species_info->id) == species_error_before);
	CHECK(database.get_asset_load_state(layer_info->id) == layer_state_before);
	CHECK(database.get_asset_last_error(layer_info->id) == layer_error_before);
	CHECK(database.get_asset_load_state(chunk_info->id) == chunk_state_before);
	CHECK(database.get_asset_last_error(chunk_info->id) == chunk_error_before);
	CHECK(database.get_asset_load_state(text_info->id) == text_state_before);
	CHECK(database.get_asset_last_error(text_info->id) == text_error_before);
	CHECK(database.get_last_error() == database_error_before);
}

TEST_CASE("Vegetation AssetDatabase warm cache re-admits every budget dimension")
{
	VegetationTest::ScopedAssetRoot root("warm-budget");
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::CanonicalGrassSpeciesJson());
	root.Write("flora/meadow.AshVegetationLayer", VegetationTest::ResolvedMinimalLayerBytes());
	root.Write("flora/0_0.AshVegetationChunk", VegetationTest::ResolvedMinimalChunkBytes());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());

	const auto layer = database.load_vegetation_layer_by_path(
		"flora/meadow.AshVegetationLayer", VegetationTest::GenerousLoadBudget());
	const auto chunk = database.load_vegetation_chunk_by_path(
		"flora/0_0.AshVegetationChunk", VegetationTest::GenerousLoadBudget());
	REQUIRE(layer.asset != nullptr);
	REQUIRE(chunk.asset != nullptr);
	REQUIRE(layer.cost.file_bytes > 0);
	REQUIRE(layer.cost.payload_bytes > 0);
	REQUIRE(layer.cost.decoded_bytes > 0);
	REQUIRE(layer.cost.palette_records > 0);
	REQUIRE(layer.cost.tile_records > 0);
	REQUIRE(chunk.cost.instance_records > 0);

	std::vector<AshEngine::VegetationLoadBudget> rejected_budgets{};
	AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
	budget.max_file_bytes = layer.cost.file_bytes - 1;
	rejected_budgets.push_back(budget);
	budget = VegetationTest::GenerousLoadBudget();
	budget.max_payload_bytes = layer.cost.payload_bytes - 1;
	rejected_budgets.push_back(budget);
	budget = VegetationTest::GenerousLoadBudget();
	budget.max_decoded_bytes = layer.cost.decoded_bytes - 1;
	rejected_budgets.push_back(budget);
	budget = VegetationTest::GenerousLoadBudget();
	budget.max_palette_records = layer.cost.palette_records - 1;
	rejected_budgets.push_back(budget);
	budget = VegetationTest::GenerousLoadBudget();
	budget.max_tile_records = layer.cost.tile_records - 1;
	rejected_budgets.push_back(budget);

	for (const AshEngine::VegetationLoadBudget& rejected : rejected_budgets)
	{
		const auto result = database.load_vegetation_layer_by_path(
			"flora/meadow.AshVegetationLayer", rejected);
		CHECK(result.asset == nullptr);
		CHECK(result.failure == AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	}
	budget = VegetationTest::GenerousLoadBudget();
	budget.max_instance_records = chunk.cost.instance_records - 1;
	const auto rejected_chunk = database.load_vegetation_chunk_by_path(
		"flora/0_0.AshVegetationChunk", budget);
	CHECK(rejected_chunk.asset == nullptr);
	CHECK(rejected_chunk.failure == AshEngine::VegetationAssetLoadFailure::BudgetExceeded);

	AshEngine::AssetDatabase cold_database = AshEngine::AssetDatabase::create(root.Path());
	for (const AshEngine::VegetationLoadBudget& rejected : rejected_budgets)
	{
		CheckVegetationFailureShape(cold_database.load_vegetation_layer_by_path(
			"flora/meadow.AshVegetationLayer", rejected),
			AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	}
	CheckVegetationFailureShape(cold_database.load_vegetation_chunk_by_path(
		"flora/0_0.AshVegetationChunk", budget),
		AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	REQUIRE(cold_database.load_vegetation_layer_by_path(
		"flora/meadow.AshVegetationLayer", VegetationTest::GenerousLoadBudget()).asset != nullptr);
	REQUIRE(cold_database.load_vegetation_chunk_by_path(
		"flora/0_0.AshVegetationChunk", VegetationTest::GenerousLoadBudget()).asset != nullptr);
}

TEST_CASE("Vegetation AssetDatabase outer and dependency budgets are identical on cold and warm loads")
{
	VegetationTest::ScopedAssetRoot outer_root("outer-cold-warm-budget");
	outer_root.Write("vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::CanonicalGrassSpeciesJson());
	outer_root.Write("flora/meadow.AshVegetationLayer", VegetationTest::ResolvedMinimalLayerBytes());
	outer_root.Write("flora/0_0.AshVegetationChunk", VegetationTest::ResolvedMinimalChunkBytes());
	AshEngine::AssetDatabase outer_database = AshEngine::AssetDatabase::create(outer_root.Path());
	const AshEngine::AssetInfo* outer_layer =
		outer_database.find_asset_by_path("flora/meadow.AshVegetationLayer");
	const AshEngine::AssetInfo* outer_chunk =
		outer_database.find_asset_by_path("flora/0_0.AshVegetationChunk");
	REQUIRE(outer_layer != nullptr);
	REQUIRE(outer_chunk != nullptr);
	AshEngine::VegetationLoadBudget decoded_tiny = VegetationTest::GenerousLoadBudget();
	decoded_tiny.max_decoded_bytes = 0;
	const std::string outer_database_error_before = outer_database.get_last_error();
	const std::string outer_layer_error_before = outer_database.get_asset_last_error(outer_layer->id);
	const std::string outer_chunk_error_before = outer_database.get_asset_last_error(outer_chunk->id);
	CheckVegetationFailureShape(
		outer_database.load_vegetation_layer_by_id(outer_layer->id, decoded_tiny),
		AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CheckVegetationFailureShape(
		outer_database.load_vegetation_chunk_by_path("flora/0_0.AshVegetationChunk", decoded_tiny),
		AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CHECK(outer_database.get_asset_load_state(outer_layer->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(outer_database.get_asset_load_state(outer_chunk->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(outer_database.get_asset_last_error(outer_layer->id) == outer_layer_error_before);
	CHECK(outer_database.get_asset_last_error(outer_chunk->id) == outer_chunk_error_before);
	CHECK(outer_database.get_last_error() == outer_database_error_before);
	REQUIRE(outer_database.load_vegetation_layer_by_id(
		outer_layer->id, VegetationTest::GenerousLoadBudget()).asset != nullptr);
	REQUIRE(outer_database.load_vegetation_chunk_by_id(
		outer_chunk->id, VegetationTest::GenerousLoadBudget()).asset != nullptr);
	CheckVegetationFailureShape(
		outer_database.load_vegetation_layer_by_path("flora/meadow.AshVegetationLayer", decoded_tiny),
		AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CheckVegetationFailureShape(
		outer_database.load_vegetation_chunk_by_id(outer_chunk->id, decoded_tiny),
		AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CHECK(outer_database.get_asset_load_state(outer_layer->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(outer_database.get_asset_load_state(outer_chunk->id) == AshEngine::AssetLoadState::Loaded);

	VegetationTest::ScopedAssetRoot dependency_root("dependency-cold-warm-budget");
	AshEngine::VegetationSpecies large_species{};
	std::string species_error{};
	REQUIRE(AshEngine::decode_vegetation_species(
		VegetationTest::CanonicalGrassSpeciesJson(),
		VegetationTest::GenerousLoadBudget(),
		large_species,
		&species_error));
	large_species.name.assign(256, 'g');
	auto make_long_asset_path = [](const std::string& leaf)
	{
		std::string path = "vegetation/";
		while (path.size() < 3000)
		{
			path += "segment/";
		}
		path += leaf;
		return path;
	};
	for (size_t index = 0; index < large_species.mesh_lods.size(); ++index)
	{
		large_species.mesh_lods[index].mesh_asset_path =
			make_long_asset_path("lod" + std::to_string(index) + ".ashmesh");
	}
	large_species.render.impostor_asset_path = make_long_asset_path("impostor.dds");
	large_species.render.chunk_hlod_asset_path = make_long_asset_path("hlod.ashmesh");
	std::vector<uint8_t> large_species_canonical{};
	REQUIRE(AshEngine::encode_vegetation_species(
		large_species, large_species_canonical, &species_error));
	const std::vector<uint8_t> large_species_bytes = large_species_canonical;
	AshEngine::VegetationPaletteEntry large_entry = VegetationTest::ResolvedMinimalPaletteEntry();
	large_entry.species_id = large_species.species_id;
	large_entry.species_sha256 = AshEngine::vegetation_sha256(
		large_species_canonical.data(), large_species_canonical.size());
	AshEngine::VegetationLayerSnapshot large_layer = VegetationTest::ResolvedMinimalLayerSnapshot();
	large_layer.palette[0] = large_entry;
	large_layer.tiles[0].planes[1].species_id = large_entry.species_id;
	AshEngine::VegetationChunk large_chunk = VegetationTest::ResolvedMinimalChunk();
	large_chunk.species[0] = large_entry;
	const std::vector<uint8_t> large_layer_bytes = EncodeLayer(large_layer);
	const std::vector<uint8_t> large_chunk_bytes = EncodeChunk(large_chunk);
	dependency_root.Write("vegetation/Phase2ManualSpecies.AshVegetation", large_species_bytes);
	dependency_root.Write("flora/meadow.AshVegetationLayer", large_layer_bytes);
	dependency_root.Write("flora/0_0.AshVegetationChunk", large_chunk_bytes);
	AshEngine::AssetDatabase measurement_database = AshEngine::AssetDatabase::create(dependency_root.Path());
	const auto dependency_species_cost = measurement_database.load_vegetation_species_by_path(
		"vegetation/Phase2ManualSpecies.AshVegetation", VegetationTest::GenerousLoadBudget());
	const auto measurement_layer = measurement_database.load_vegetation_layer_by_path(
		"flora/meadow.AshVegetationLayer", VegetationTest::GenerousLoadBudget());
	const auto measurement_chunk = measurement_database.load_vegetation_chunk_by_path(
		"flora/0_0.AshVegetationChunk", VegetationTest::GenerousLoadBudget());
	REQUIRE(dependency_species_cost.asset != nullptr);
	REQUIRE(measurement_layer.asset != nullptr);
	REQUIRE(measurement_chunk.asset != nullptr);
	REQUIRE(dependency_species_cost.cost.file_bytes > measurement_layer.cost.file_bytes);
	REQUIRE(dependency_species_cost.cost.file_bytes > measurement_chunk.cost.file_bytes);
	REQUIRE(dependency_species_cost.cost.payload_bytes > measurement_layer.cost.payload_bytes);
	REQUIRE(dependency_species_cost.cost.payload_bytes > measurement_chunk.cost.payload_bytes);
	REQUIRE(dependency_species_cost.cost.decoded_bytes > measurement_layer.cost.decoded_bytes);
	REQUIRE(dependency_species_cost.cost.decoded_bytes > measurement_chunk.cost.decoded_bytes);
	AshEngine::AssetDatabase dependency_database = AshEngine::AssetDatabase::create(dependency_root.Path());
	const AshEngine::AssetInfo* dependency_layer =
		dependency_database.find_asset_by_path("flora/meadow.AshVegetationLayer");
	const AshEngine::AssetInfo* dependency_chunk =
		dependency_database.find_asset_by_path("flora/0_0.AshVegetationChunk");
	REQUIRE(dependency_layer != nullptr);
	REQUIRE(dependency_chunk != nullptr);
	std::vector<AshEngine::VegetationLoadBudget> dependency_tiny_budgets{};
	AshEngine::VegetationLoadBudget dependency_tiny = VegetationTest::GenerousLoadBudget();
	dependency_tiny.max_file_bytes = dependency_species_cost.cost.file_bytes - 1;
	dependency_tiny_budgets.push_back(dependency_tiny);
	dependency_tiny = VegetationTest::GenerousLoadBudget();
	dependency_tiny.max_payload_bytes = dependency_species_cost.cost.payload_bytes - 1;
	dependency_tiny_budgets.push_back(dependency_tiny);
	dependency_tiny = VegetationTest::GenerousLoadBudget();
	dependency_tiny.max_decoded_bytes = dependency_species_cost.cost.decoded_bytes - 1;
	dependency_tiny_budgets.push_back(dependency_tiny);
	const std::string dependency_database_error_before = dependency_database.get_last_error();
	const std::string dependency_layer_error_before = dependency_database.get_asset_last_error(dependency_layer->id);
	const std::string dependency_chunk_error_before = dependency_database.get_asset_last_error(dependency_chunk->id);
	for (const AshEngine::VegetationLoadBudget& rejected : dependency_tiny_budgets)
	{
		CheckVegetationFailureShape(
			dependency_database.load_vegetation_layer_by_path("flora/meadow.AshVegetationLayer", rejected),
			AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
		CheckVegetationFailureShape(
			dependency_database.load_vegetation_chunk_by_id(dependency_chunk->id, rejected),
			AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	}
	CHECK(dependency_database.get_asset_load_state(dependency_layer->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(dependency_database.get_asset_load_state(dependency_chunk->id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(dependency_database.get_asset_last_error(dependency_layer->id) == dependency_layer_error_before);
	CHECK(dependency_database.get_asset_last_error(dependency_chunk->id) == dependency_chunk_error_before);
	CHECK(dependency_database.get_last_error() == dependency_database_error_before);
	REQUIRE(dependency_database.load_vegetation_layer_by_id(
		dependency_layer->id, VegetationTest::GenerousLoadBudget()).asset != nullptr);
	REQUIRE(dependency_database.load_vegetation_chunk_by_path(
		"flora/0_0.AshVegetationChunk", VegetationTest::GenerousLoadBudget()).asset != nullptr);
	for (const AshEngine::VegetationLoadBudget& rejected : dependency_tiny_budgets)
	{
		CheckVegetationFailureShape(
			dependency_database.load_vegetation_layer_by_id(dependency_layer->id, rejected),
			AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
		CheckVegetationFailureShape(
			dependency_database.load_vegetation_chunk_by_path("flora/0_0.AshVegetationChunk", rejected),
			AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	}
	CHECK(dependency_database.get_asset_load_state(dependency_layer->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(dependency_database.get_asset_load_state(dependency_chunk->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(dependency_database.get_last_error() == dependency_database_error_before);
}

TEST_CASE("Vegetation AssetDatabase invalid root refresh resets the active typed cache and catalog")
{
	VegetationTest::ScopedAssetRoot root("active-invalid-root-reset");
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::CanonicalGrassSpeciesJson());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(
		"vegetation/Phase2ManualSpecies.AshVegetation");
	REQUIRE(info != nullptr);
	const AshEngine::AssetId id = info->id;
	const auto loaded = database.load_vegetation_species_by_id(
		id, VegetationTest::GenerousLoadBudget());
	REQUIRE(loaded.asset != nullptr);
	CHECK(database.get_asset_load_state(id) == AshEngine::AssetLoadState::Loaded);

	std::error_code remove_error{};
	std::filesystem::remove_all(root.Path(), remove_error);
	REQUIRE_FALSE(static_cast<bool>(remove_error));
	CHECK_FALSE(database.refresh());
	CHECK(database.get_assets().empty());
	CHECK(database.find_asset_by_id(id) == nullptr);
	CHECK(database.get_asset_load_state(id) == AshEngine::AssetLoadState::Unknown);
	CHECK_FALSE(database.get_last_error().empty());
	CheckVegetationFailureShape(database.load_vegetation_species_by_id(
		id, VegetationTest::GenerousLoadBudget()),
		AshEngine::VegetationAssetLoadFailure::Missing);
}

TEST_CASE("Vegetation AssetDatabase root reset invalidates shared state while resolver snapshot stays detached")
{
	VegetationTest::ScopedAssetRoot first_root("catalog-first");
	VegetationTest::ScopedAssetRoot second_root("catalog-second");
	first_root.Write("vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::CanonicalGrassSpeciesJson());
	second_root.Write("notes/other.txt", std::vector<uint8_t>{ 'x' });

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(first_root.Path());
	const AshEngine::AssetInfo* species_info =
		database.find_asset_by_path("vegetation/Phase2ManualSpecies.AshVegetation");
	REQUIRE(species_info != nullptr);
	const AshEngine::AssetId species_id = species_info->id;
	const auto first_loaded = database.load_vegetation_species_by_id(
		species_id, VegetationTest::GenerousLoadBudget());
	REQUIRE(first_loaded.asset != nullptr);
	const auto resolver = database.capture_vegetation_resolver_snapshot();
	REQUIRE(resolver != nullptr);

	REQUIRE(database.set_root_path(first_root.Path()));
	CHECK(database.get_asset_load_state(species_id) == AshEngine::AssetLoadState::Loaded);
	REQUIRE(database.refresh());
	CHECK(database.get_asset_load_state(species_id) == AshEngine::AssetLoadState::Unloaded);
	const auto refreshed_load = database.load_vegetation_species_by_id(
		species_id, VegetationTest::GenerousLoadBudget());
	REQUIRE(refreshed_load.asset != nullptr);
	CHECK(refreshed_load.asset != first_loaded.asset);
	REQUIRE(database.set_root_path(second_root.Path()));
	CHECK(database.get_asset_load_state(species_id) == AshEngine::AssetLoadState::Unknown);
	REQUIRE(database.refresh());
	CHECK(database.find_asset_by_id(species_id) == nullptr);
	const auto detached = resolver->load_species_by_path(
		"vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::GenerousLoadBudget());
	REQUIRE(detached.asset != nullptr);

	std::error_code remove_error{};
	std::filesystem::remove_all(second_root.Path(), remove_error);
	REQUIRE_FALSE(static_cast<bool>(remove_error));
	CHECK_FALSE(database.refresh());
	CHECK(database.get_assets().empty());
	CHECK(database.get_asset_load_state(species_id) == AshEngine::AssetLoadState::Unknown);
}

TEST_CASE("Vegetation AssetDatabase bounded stream snapshot publishes only exact EOF")
{
	std::vector<uint8_t> bytes{ 0xaa };
	std::string error{};
	std::istringstream exact("abc");
	REQUIRE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		exact, 3, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 'a', 'b', 'c' });
	CHECK(error.empty());
	std::istringstream final_partial("partial");
	bytes = { 0xee };
	REQUIRE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		final_partial, 64, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 'p', 'a', 'r', 't', 'i', 'a', 'l' });
	CHECK(error.empty());

	std::istringstream too_large("abcd");
	bytes = { 0xaa };
	CHECK_FALSE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		too_large, 3, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 0xaa });
	CHECK_FALSE(error.empty());
	std::istringstream zero_nonempty("x");
	bytes = { 0xab };
	CHECK_FALSE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		zero_nonempty, 0, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 0xab });
	CHECK_FALSE(error.empty());

	std::istringstream empty("");
	REQUIRE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		empty, 0, bytes, &error));
	CHECK(bytes.empty());
	std::istringstream short_without_eof("abc");
	short_without_eof.setstate(std::ios::failbit);
	bytes = { 0xbc };
	CHECK_FALSE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		short_without_eof, 3, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 0xbc });
	CHECK(error.find("without EOF") != std::string::npos);
	PartialThenMoreStreamBuf partial_then_more_buffer("abcdef", 3);
	std::istream partial_then_more(&partial_then_more_buffer);
	bytes = { 0xbd };
	CHECK_FALSE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		partial_then_more, 64, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 0xbd });
	CHECK(error.find("without EOF") != std::string::npos);
	CHECK(error != "Vegetation bounded stream exceeds maximum byte count.");

	GrowingAfterLimitStreamBuf growing_buffer("abc");
	std::istream growing(&growing_buffer);
	bytes = { 0xbe };
	CHECK_FALSE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		growing, 3, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 0xbe });
	CHECK(error == "Vegetation bounded stream exceeds maximum byte count.");

	std::istringstream bad("abc");
	bad.setstate(std::ios::badbit);
	bytes = { 0xbb };
	CHECK_FALSE(AshEngine::Detail::read_vegetation_bounded_stream_snapshot(
		bad, 3, bytes, &error));
	CHECK(bytes == std::vector<uint8_t>{ 0xbb });
}

TEST_CASE("Vegetation AssetDatabase concurrent budgets isolate requests while preview stays non-writing")
{
	VegetationTest::ScopedAssetRoot root("concurrent-budget-preview");
	const std::vector<uint8_t> species_bytes = VegetationTest::CanonicalGrassSpeciesJson();
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation", species_bytes);
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	const AshEngine::AssetInfo* species =
		database.find_asset_by_path("vegetation/Phase2ManualSpecies.AshVegetation");
	REQUIRE(species != nullptr);
	ScopedSingleWorkerThreading threading{};
	const auto blocker_state = std::make_shared<BlockingWorkerState>();
	BlockingWorkerReleaseGuard release_guard(blocker_state);
	const AshEngine::ThreadCommandFuture blocker = AshEngine::Detail::enqueue_worker_command(
		"vegetation-concurrent-budget-blocker", [blocker_state]()
		{
			blocker_state->started_promise.set_value();
			blocker_state->release.wait();
		});
	const bool blocker_did_start = blocker_state->started.wait_for(std::chrono::seconds(2)) ==
		std::future_status::ready;
	REQUIRE(blocker_did_start);

	std::string missing = "legacy-sentinel";
	CHECK_FALSE(database.load_text_by_path("notes/missing.txt", missing));
	const AshEngine::AssetLoadState state_before = database.get_asset_load_state(species->id);
	const std::string asset_error_before = database.get_asset_last_error(species->id);
	const std::string database_error_before = database.get_last_error();
	const auto generous_first = database.load_vegetation_species_by_path_async(
		"vegetation/Phase2ManualSpecies.AshVegetation", VegetationTest::GenerousLoadBudget());
	const auto generous_join = database.load_vegetation_species_by_id_async(
		species->id, VegetationTest::GenerousLoadBudget());
	const auto tiny_isolated = database.load_vegetation_species_by_path_async(
		"vegetation/Phase2ManualSpecies.AshVegetation", TinyFileBudget());
	std::string preview = "preview-sentinel";
	CHECK(database.load_text_by_id_bounded(
		species->id, static_cast<uint64_t>(species_bytes.size()), preview));
	CHECK(std::vector<uint8_t>(preview.begin(), preview.end()) == species_bytes);
	CHECK(database.get_asset_load_state(species->id) == state_before);
	CHECK(database.get_asset_last_error(species->id) == asset_error_before);
	CHECK(database.get_last_error() == database_error_before);

	release_guard.ReleaseNoexcept();
	REQUIRE(blocker.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	REQUIRE(generous_first.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	REQUIRE(generous_join.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	REQUIRE(tiny_isolated.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	CHECK_NOTHROW(blocker.get());
	const auto first_result = generous_first.get();
	const auto joined_result = generous_join.get();
	const auto tiny_result = tiny_isolated.get();
	REQUIRE(first_result.asset != nullptr);
	REQUIRE(joined_result.asset != nullptr);
	CHECK(first_result.asset == joined_result.asset);
	CheckVegetationFailureShape(tiny_result,
		AshEngine::VegetationAssetLoadFailure::BudgetExceeded);
	CHECK(database.get_asset_load_state(species->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_asset_last_error(species->id).empty());
}

TEST_CASE("Vegetation AssetDatabase worker queue wakes one idle worker and closes shutdown admission")
{
	VegetationTest::ScopedAssetRoot root("shutdown-admission");
	root.Write("vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::CanonicalGrassSpeciesJson());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
	ScopedSingleWorkerThreading threading{};
	std::promise<void> blocker_started{};
	std::promise<void> release_blocker{};
	std::shared_future<void> release = release_blocker.get_future().share();
	const AshEngine::ThreadCommandFuture blocker = AshEngine::Detail::enqueue_worker_command(
		"vegetation-blocker", [&blocker_started, release]() mutable
		{
			blocker_started.set_value();
			release.wait();
		});
	const bool blocker_did_start =
		blocker_started.get_future().wait_for(std::chrono::seconds(2)) ==
		std::future_status::ready;
	CHECK(blocker_did_start);

	std::atomic<int> accepted_count{ 0 };
	const AshEngine::ThreadCommandFuture accepted = AshEngine::Detail::enqueue_worker_command(
		"vegetation-accepted-before-stop", [&accepted_count]()
		{
			accepted_count.fetch_add(1, std::memory_order_relaxed);
		});
	ShutdownJoinGuard shutdown_guard(
		release_blocker, std::thread([]() { AshEngine::shutdown_threading(); }));
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!AshEngine::is_threading_shutting_down() &&
		std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::yield();
	}
	const bool shutdown_was_observed = AshEngine::is_threading_shutting_down();
	CHECK(shutdown_was_observed);
	std::atomic<int> rejected_count{ 0 };
	AshEngine::ThreadCommandFuture rejected{};
	if (shutdown_was_observed)
	{
		rejected = AshEngine::Detail::enqueue_worker_command(
			"vegetation-rejected-after-stop", [&rejected_count]()
			{
				rejected_count.fetch_add(1, std::memory_order_relaxed);
			});
		const bool rejected_is_ready =
			rejected.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		CHECK(rejected_is_ready);
		if (rejected_is_ready)
		{
			CHECK_THROWS(rejected.get());
		}

		const auto typed_rejected = database.load_vegetation_species_by_path_async(
			"vegetation/Phase2ManualSpecies.AshVegetation",
			VegetationTest::GenerousLoadBudget());
		const bool typed_rejected_is_ready =
			typed_rejected.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		CHECK(typed_rejected_is_ready);
		if (typed_rejected_is_ready)
		{
			const auto typed_result = typed_rejected.get();
			CHECK(typed_result.asset == nullptr);
			CHECK(typed_result.failure == AshEngine::VegetationAssetLoadFailure::Io);
			CHECK_FALSE(typed_result.error.empty());
		}
	}
	shutdown_guard.Finish();
	CHECK_NOTHROW(blocker.get());
	CHECK_NOTHROW(accepted.get());
	CHECK(accepted_count.load(std::memory_order_relaxed) == 1);
	CHECK(rejected_count.load(std::memory_order_relaxed) == 0);

	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1;
	REQUIRE(AshEngine::initialize_threading(config));
	std::promise<void> handshake_complete{};
	const AshEngine::ThreadCommandFuture handshake = AshEngine::Detail::enqueue_worker_command(
		"vegetation-worker-idle-handshake", [&handshake_complete]()
		{
			handshake_complete.set_value();
		});
	REQUIRE(handshake_complete.get_future().wait_for(std::chrono::seconds(2)) ==
		std::future_status::ready);
	REQUIRE(handshake.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	CHECK_NOTHROW(handshake.get());
	std::atomic<int> sole_count{ 0 };
	const AshEngine::ThreadCommandFuture sole = AshEngine::Detail::enqueue_worker_command(
		"vegetation-idle-sole-command", [&sole_count]()
		{
			sole_count.fetch_add(1, std::memory_order_relaxed);
		});
	const bool sole_is_ready =
		sole.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
	CHECK(sole_is_ready);
	if (sole_is_ready)
	{
		CHECK_NOTHROW(sole.get());
	}
	CHECK(sole_count.load(std::memory_order_relaxed) == 1);
	const auto typed_retry = database.load_vegetation_species_by_path_async(
		"vegetation/Phase2ManualSpecies.AshVegetation",
		VegetationTest::GenerousLoadBudget());
	REQUIRE(typed_retry.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	const auto typed_retry_result = typed_retry.get();
	REQUIRE(typed_retry_result.asset != nullptr);
	CHECK(typed_retry_result.failure == AshEngine::VegetationAssetLoadFailure::None);
}
