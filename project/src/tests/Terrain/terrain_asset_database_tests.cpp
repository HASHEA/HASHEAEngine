#include "doctest.h"

#include "Base/hthreading.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Scene/TerrainQuery.h"
#include "Terrain/TerrainTestUtils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
	auto TestRoot(const char* name) -> std::filesystem::path
	{
		const std::filesystem::path root =
			std::filesystem::path("Intermediate/test-temp/tests/terrain-asset-database") /
			name;
		std::error_code error_code{};
		std::filesystem::remove_all(root, error_code);
		std::filesystem::create_directories(root / "terrain");
		return root;
	}

	auto MakeSnapshot(
		AshEngine::TerrainAssetId asset_id = 71u,
		float height = 12.0f) -> std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			asset_id,
			TerrainTests::MakeSmallLayout(),
			{ -100.0f, 1000.0f },
			height,
			snapshot,
			&error));
		REQUIRE_MESSAGE(error.empty(), error);
		return snapshot;
	}

	auto SaveSnapshot(
		const std::filesystem::path& path,
		const AshEngine::TerrainAssetSnapshot& snapshot) -> void
	{
		std::filesystem::create_directories(path.parent_path());
		std::filesystem::remove(path);
		std::string error{};
		REQUIRE(AshEngine::save_terrain_container_incremental(
			path, snapshot, {}, nullptr, &error) ==
			AshEngine::TerrainContainerResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
	}

	auto ReadSource(const std::filesystem::path& path) -> std::string
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE(input.is_open());
		return std::string(
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	struct ThreadingScope
	{
		~ThreadingScope()
		{
			AshEngine::shutdown_threading();
		}
	};

	struct WorkerBlocker
	{
		std::promise<void> release_promise{};
		std::shared_future<void> release_future{};
		AshEngine::ThreadCommandFuture blocker_future{};
		bool released = false;

		WorkerBlocker()
		{
			release_future = release_promise.get_future().share();
			std::promise<void> started_promise{};
			auto started_future = started_promise.get_future();
			blocker_future = AshEngine::dispatch_background_task(
				"TerrainAssetDatabaseTests::WorkerBlocker",
				[started = std::move(started_promise),
					release = release_future]() mutable
				{
					started.set_value();
					release.wait();
				});
			started_future.wait();
		}

		~WorkerBlocker()
		{
			release();
			if (blocker_future.valid())
			{
				blocker_future.wait();
			}
		}

		void release()
		{
			if (!released)
			{
				released = true;
				release_promise.set_value();
			}
		}
	};
}

TEST_CASE("Terrain AssetDatabase detects loads and shares immutable cache entries")
{
	const std::filesystem::path root = TestRoot("load-cache");
	const std::filesystem::path relative_path = "terrain/test.AshTerrain";
	SaveSnapshot(root / relative_path, *MakeSnapshot());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	CHECK(info->type == AshEngine::AssetType::Terrain);
	const AshEngine::AssetId asset_id = info->id;

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> by_id{};
	REQUIRE(database.load_terrain_by_id(asset_id, by_id));
	REQUIRE(by_id != nullptr);
	CHECK(by_id->asset_id == asset_id);
	CHECK(database.get_asset_load_state(asset_id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_asset_last_error(asset_id).empty());

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> by_path{};
	REQUIRE(database.load_terrain_by_path(relative_path, by_path));
	CHECK(by_path == by_id);
	const auto first_async = database.load_terrain_by_id_async(asset_id);
	const auto second_async = database.load_terrain_by_path_async(relative_path);
	REQUIRE(first_async.valid());
	REQUIRE(second_async.valid());
	CHECK(first_async.get() == by_id);
	CHECK(second_async.get() == by_id);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase publishes newer tuples and invalidates only one terrain")
{
	const std::filesystem::path root = TestRoot("publish-invalidate");
	const std::filesystem::path first_path = "terrain/first.AshTerrain";
	const std::filesystem::path second_path = "terrain/second.AshTerrain";
	SaveSnapshot(root / first_path, *MakeSnapshot(71u, 5.0f));
	SaveSnapshot(root / second_path, *MakeSnapshot(72u, 15.0f));
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* first_info = database.find_asset_by_path(first_path);
	const AshEngine::AssetInfo* second_info = database.find_asset_by_path(second_path);
	REQUIRE(first_info != nullptr);
	REQUIRE(second_info != nullptr);
	const AshEngine::AssetId first_id = first_info->id;
	const AshEngine::AssetId second_id = second_info->id;

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> disk_first{};
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> disk_second{};
	REQUIRE(database.load_terrain_by_id(first_id, disk_first));
	REQUIRE(database.load_terrain_by_id(second_id, disk_second));
	REQUIRE(disk_first);
	REQUIRE(disk_second);
	CHECK(disk_first->content_generation == 1u);
	CHECK(disk_first->residency_revision == 0u);

	auto generation_one_revision_one =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*disk_first);
	generation_one_revision_one->residency_revision = 1u;
	REQUIRE(database.publish_terrain_snapshot(first_id, generation_one_revision_one));
	CHECK_FALSE(database.publish_terrain_snapshot(first_id, generation_one_revision_one));
	auto generation_one_revision_zero =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*disk_first);
	CHECK_FALSE(database.publish_terrain_snapshot(first_id, generation_one_revision_zero));

	auto generation_two_revision_zero =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*disk_first);
	generation_two_revision_zero->content_generation = 2u;
	generation_two_revision_zero->residency_revision = 0u;
	REQUIRE(database.publish_terrain_snapshot(first_id, generation_two_revision_zero));
	CHECK_FALSE(database.publish_terrain_snapshot(first_id, generation_one_revision_one));

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> newest{};
	REQUIRE(database.load_terrain_by_path(first_path, newest));
	CHECK(newest == generation_two_revision_zero);
	CHECK(database.load_terrain_by_id_async(first_id).get() == newest);

	REQUIRE(database.invalidate_terrain_snapshot(first_id));
	CHECK(database.get_asset_load_state(first_id) == AshEngine::AssetLoadState::Unloaded);
	CHECK(database.get_asset_last_error(first_id).empty());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> second_after_invalidation{};
	REQUIRE(database.load_terrain_by_id(second_id, second_after_invalidation));
	CHECK(second_after_invalidation == disk_second);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> reloaded_first{};
	REQUIRE(database.load_terrain_by_id(first_id, reloaded_first));
	REQUIRE(reloaded_first);
	CHECK(reloaded_first != newest);
	CHECK(reloaded_first->content_generation == 1u);
	CHECK(reloaded_first->residency_revision == 0u);
	CHECK_FALSE(database.invalidate_terrain_snapshot(0xfeedu));
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase recovers a failed asset only after exact invalidation")
{
	const std::filesystem::path root = TestRoot("failed-recovery");
	const std::filesystem::path relative_path = "terrain/corrupt.AshTerrain";
	{
		std::ofstream output(root / relative_path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		output << "not a terrain container";
	}
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	CHECK(info->type == AshEngine::AssetType::Terrain);
	const AshEngine::AssetId asset_id = info->id;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	CHECK_FALSE(database.load_terrain_by_id(asset_id, snapshot));
	CHECK_FALSE(snapshot);
	CHECK(database.get_asset_load_state(asset_id) == AshEngine::AssetLoadState::Failed);
	CHECK_FALSE(database.get_asset_last_error(asset_id).empty());
	CHECK_FALSE(database.load_terrain_by_id_async(asset_id).get());

	SaveSnapshot(root / relative_path, *MakeSnapshot());
	CHECK_FALSE(database.load_terrain_by_id(asset_id, snapshot));
	REQUIRE(database.invalidate_terrain_snapshot(asset_id));
	REQUIRE(database.load_terrain_by_id(asset_id, snapshot));
	REQUIRE(snapshot);
	CHECK(snapshot->asset_id == asset_id);
	CHECK(database.get_asset_load_state(asset_id) == AshEngine::AssetLoadState::Loaded);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase worker completion has an exact stale serial guard")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Asset/AssetDatabase.cpp");
	const size_t serial_map = source.find("terrain_load_serial_by_id");
	const size_t captured = source.find("captured_load_serial", serial_map);
	const size_t current = source.find("current_load_serial", captured);
	const size_t guard = source.find(
		"current_load_serial == captured_load_serial", current);
	const size_t cache_write = source.find("terrain_cache[asset_id]", guard);
	REQUIRE(serial_map != std::string::npos);
	REQUIRE(captured != std::string::npos);
	REQUIRE(current != std::string::npos);
	REQUIRE(guard != std::string::npos);
	REQUIRE(cache_write != std::string::npos);
	CHECK(guard < cache_write);
}

TEST_CASE("Terrain AssetDatabase prepare rejects a resolved entry from an older catalog")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Asset/AssetDatabase.cpp");
	const size_t prepare = source.find("prepare_terrain_load");
	const size_t catalog_guard = source.find(
		"resolved.catalog_generation == impl->catalog_generation", prepare);
	const size_t captured = source.find("captured_load_serial", catalog_guard);
	REQUIRE(prepare != std::string::npos);
	REQUIRE(catalog_guard != std::string::npos);
	REQUIRE(captured != std::string::npos);
	CHECK(catalog_guard < captured);
}

TEST_CASE("Terrain AssetDatabase shares a queued in-flight load and prefetch stays pending")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};
	WorkerBlocker blocker{};

	const std::filesystem::path root = TestRoot("shared-inflight");
	const std::filesystem::path relative_path = "terrain/shared.AshTerrain";
	SaveSnapshot(root / relative_path, *MakeSnapshot());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	const AshEngine::AssetId asset_id = info->id;

	const auto first = database.load_terrain_by_id_async(asset_id);
	const auto second = database.load_terrain_by_path_async(relative_path);
	CHECK(first.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
	CHECK(second.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
	CHECK(AshEngine::prefetch_query_region(
		database, asset_id, { 0u, 0u, 2u, 2u }) ==
		AshEngine::TerrainQueryStatus::Pending);

	blocker.release();
	const auto first_snapshot = first.get();
	const auto second_snapshot = second.get();
	REQUIRE(first_snapshot != nullptr);
	CHECK(second_snapshot == first_snapshot);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase rejected worker dispatch resolves null and clears Loading")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};
	WorkerBlocker blocker{};

	const std::filesystem::path root = TestRoot("dispatch-rejected");
	const std::filesystem::path relative_path = "terrain/rejected.AshTerrain";
	SaveSnapshot(root / relative_path, *MakeSnapshot());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	const AshEngine::AssetId asset_id = info->id;

	auto shutdown_future = std::async(std::launch::async, []
	{
		AshEngine::shutdown_threading();
	});
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(2);
	while (!AshEngine::is_threading_shutting_down() &&
		std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::yield();
	}
	const bool shutting_down = AshEngine::is_threading_shutting_down();
	CHECK(shutting_down);
	if (!shutting_down)
	{
		blocker.release();
		shutdown_future.get();
		std::filesystem::remove_all(root);
		return;
	}

	const auto rejected = database.load_terrain_by_id_async(asset_id);
	const bool rejected_ready =
		rejected.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	CHECK(rejected_ready);
	if (!rejected_ready)
	{
		blocker.release();
		shutdown_future.get();
		std::filesystem::remove_all(root);
		return;
	}
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> rejected_snapshot{};
	bool threw = false;
	try
	{
		rejected_snapshot = rejected.get();
	}
	catch (...)
	{
		threw = true;
	}
	CHECK_FALSE(threw);
	CHECK_FALSE(rejected_snapshot);
	CHECK(database.get_asset_load_state(asset_id) == AshEngine::AssetLoadState::Unloaded);

	blocker.release();
	shutdown_future.get();
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase async dispatch uses the public worker interface")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Asset/AssetDatabase.cpp");
	const size_t async_load = source.find("load_terrain_resolved_async");
	REQUIRE(async_load != std::string::npos);
	const size_t function_scope_end = source.find(
		"AssetDatabase::AssetDatabase(", async_load + 1u);
	REQUIRE(function_scope_end != std::string::npos);
	const std::string async_load_body = source.substr(
		async_load, function_scope_end - async_load);
	const size_t public_dispatch = async_load_body.find(
		"dispatch_background_task(");
	const size_t detail_dispatch = async_load_body.find(
		"Detail::enqueue_worker_command(");
	REQUIRE(public_dispatch != std::string::npos);
	CHECK(detail_dispatch == std::string::npos);
}

TEST_CASE("Terrain query prefetch never performs a cold disk load inline")
{
	AshEngine::shutdown_threading();
	const std::filesystem::path root = TestRoot("query-no-inline");
	const std::filesystem::path relative_path = "terrain/valid.AshTerrain";
	SaveSnapshot(root / relative_path, *MakeSnapshot());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	CHECK(AshEngine::prefetch_query_region(
		database, info->id, { 0u, 0u, 2u, 2u }) ==
		AshEngine::TerrainQueryStatus::Failed);
	CHECK(database.get_asset_load_state(info->id) ==
		AshEngine::AssetLoadState::Unloaded);
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};
	const auto retry = database.load_terrain_by_id_async(info->id);
	const auto recovered = retry.get();
	REQUIRE(recovered != nullptr);
	CHECK(database.get_asset_load_state(info->id) ==
		AshEngine::AssetLoadState::Loaded);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain query prefetch resolves loaded outside and failed regions without blocking")
{
	const std::filesystem::path root = TestRoot("query-prefetch");
	const std::filesystem::path valid_path = "terrain/valid.AshTerrain";
	const std::filesystem::path corrupt_path = "terrain/corrupt.AshTerrain";
	SaveSnapshot(root / valid_path, *MakeSnapshot());
	{
		std::ofstream output(root / corrupt_path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		output << "broken";
	}
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* valid_info = database.find_asset_by_path(valid_path);
	const AshEngine::AssetInfo* corrupt_info = database.find_asset_by_path(corrupt_path);
	REQUIRE(valid_info != nullptr);
	REQUIRE(corrupt_info != nullptr);
	const AshEngine::AssetId valid_id = valid_info->id;
	const AshEngine::AssetId corrupt_id = corrupt_info->id;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(database.load_terrain_by_id(valid_id, loaded));
	CHECK_FALSE(database.load_terrain_by_id(corrupt_id, loaded));
	CHECK(AshEngine::prefetch_query_region(
		database, valid_id, { 0u, 0u, 2u, 2u }) ==
		AshEngine::TerrainQueryStatus::Ready);
	CHECK(AshEngine::prefetch_query_region(
		database, valid_id, { 20u, 20u, 24u, 24u }) ==
		AshEngine::TerrainQueryStatus::Outside);
	CHECK(AshEngine::prefetch_query_region(
		database, valid_id, { 2u, 2u, 2u, 3u }) ==
		AshEngine::TerrainQueryStatus::Outside);
	CHECK(AshEngine::prefetch_query_region(
		database, corrupt_id, { 0u, 0u, 2u, 2u }) ==
		AshEngine::TerrainQueryStatus::Failed);
	CHECK(AshEngine::prefetch_query_region(
		database, 0xabcdefu, { 0u, 0u, 2u, 2u }) ==
		AshEngine::TerrainQueryStatus::Failed);
	std::filesystem::remove_all(root);
}
