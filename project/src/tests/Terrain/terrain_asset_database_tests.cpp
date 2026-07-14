#include "doctest.h"

#include "Base/hthreading.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/TerrainContainerFormat.h"
#include "Function/Scene/TerrainQuery.h"
#include "Terrain/TerrainCommitLeaseTestUtils.h"
#include "Terrain/TerrainTestUtils.h"

#include <chrono>
#include <cstddef>
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

	auto AppendGenerationTwo(
		const std::filesystem::path& path,
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& generation_one) -> void
	{
		auto generation_two =
			std::make_shared<AshEngine::TerrainAssetSnapshot>(*generation_one);
		generation_two->content_generation = 2u;
		auto changed_component =
			std::make_shared<AshEngine::TerrainComponentSnapshot>(
				*generation_two->components.front());
		changed_component->content_generation = 2u;
		generation_two->components.front() = changed_component;
		const AshEngine::TerrainDirtyComponentPayload dirty{
			changed_component->coord,
			changed_component->content_generation,
			changed_component
		};
		std::string error{};
		REQUIRE(AshEngine::save_terrain_container_incremental(
			path, *generation_two, { dirty }, nullptr, &error) ==
			AshEngine::TerrainContainerResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
	}

	auto CorruptNewerIndexCrc(const std::filesystem::path& path) -> void
	{
		using AshEngine::TerrainContainerFormat::FileHeaderDisk;
		using AshEngine::TerrainContainerFormat::IndexDescriptorDisk;
		FileHeaderDisk header{};
		{
			std::ifstream input(path, std::ios::binary);
			REQUIRE(input.read(reinterpret_cast<char*>(&header), sizeof(header)));
		}
		const size_t newer_slot = header.index_descriptors[1].generation_le >
			header.index_descriptors[0].generation_le ? 1u : 0u;
		REQUIRE(header.index_descriptors[newer_slot].generation_le == 2u);
		const uint64_t crc_offset = offsetof(FileHeaderDisk, index_descriptors) +
			newer_slot * sizeof(IndexDescriptorDisk) +
			offsetof(IndexDescriptorDisk, index_crc32_le);
		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(file.is_open());
		file.seekg(static_cast<std::streamoff>(crc_offset));
		char value = 0;
		REQUIRE(file.read(&value, 1));
		value ^= 0x5a;
		file.seekp(static_cast<std::streamoff>(crc_offset));
		REQUIRE(file.write(&value, 1));
	}

	auto CorruptCriticalPayload(const std::filesystem::path& path) -> uint64_t
	{
		using AshEngine::TerrainContainerFormat::BlockKind;
		using AshEngine::TerrainContainerFormat::BlockRecordDisk;
		using AshEngine::TerrainContainerFormat::FileHeaderDisk;
		FileHeaderDisk header{};
		std::ifstream input(path, std::ios::binary);
		REQUIRE(input.read(reinterpret_cast<char*>(&header), sizeof(header)));
		const size_t live_slot = header.index_descriptors[1].generation_le >
			header.index_descriptors[0].generation_le ? 1u : 0u;
		const auto& descriptor = header.index_descriptors[live_slot];
		REQUIRE(descriptor.index_size_le > 0u);
		REQUIRE(descriptor.index_size_le % sizeof(BlockRecordDisk) == 0u);
		std::vector<BlockRecordDisk> records(
			static_cast<size_t>(descriptor.index_size_le / sizeof(BlockRecordDisk)));
		input.seekg(static_cast<std::streamoff>(descriptor.index_offset_le));
		REQUIRE(input.read(
			reinterpret_cast<char*>(records.data()),
			static_cast<std::streamsize>(descriptor.index_size_le)));
		input.close();

		const BlockRecordDisk* metadata = nullptr;
		for (const BlockRecordDisk& record : records)
		{
			if (record.kind == static_cast<uint8_t>(BlockKind::Metadata))
			{
				metadata = &record;
				break;
			}
		}
		REQUIRE(metadata != nullptr);
		REQUIRE(metadata->stored_size_le > 0u);
		const uint64_t byte_offset =
			metadata->offset_le + metadata->stored_size_le / 2u;
		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(file.is_open());
		file.seekg(static_cast<std::streamoff>(byte_offset));
		char value = 0;
		REQUIRE(file.read(&value, 1));
		value = static_cast<char>(static_cast<unsigned char>(value) ^ 0x5au);
		file.seekp(static_cast<std::streamoff>(byte_offset));
		REQUIRE(file.write(&value, 1));
		return metadata->offset_le;
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
	CHECK_FALSE(by_id->recovered_previous_generation);
	CHECK(by_id->rejected_content_generation == 0u);
	CHECK(by_id->recovery_detail.empty());
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

TEST_CASE("Terrain AssetDatabase candidate reload bypasses cache until explicit replacement")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};

	const std::filesystem::path root = TestRoot("candidate-replacement");
	const std::filesystem::path relative_path = "terrain/candidate.AshTerrain";
	const std::filesystem::path absolute_path = root / relative_path;
	SaveSnapshot(absolute_path, *MakeSnapshot());

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	const AshEngine::TerrainAssetId asset_id = info->id;

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> generation_one{};
	REQUIRE(database.load_terrain_by_id(asset_id, generation_one));
	REQUIRE(generation_one);
	CHECK(generation_one->content_generation == 1u);

	auto disk_generation_two =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*generation_one);
	disk_generation_two->content_generation = 2u;
	disk_generation_two->residency_revision = 0u;
	for (size_t component_index = 0u;
		component_index < disk_generation_two->components.size();
		++component_index)
	{
		REQUIRE(disk_generation_two->components[component_index]);
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
			*disk_generation_two->components[component_index]);
		component->content_generation = disk_generation_two->content_generation;
		disk_generation_two->components[component_index] = std::move(component);
	}
	SaveSnapshot(absolute_path, *disk_generation_two);

	const auto candidate_future =
		database.load_terrain_candidate_by_id_async(asset_id);
	REQUIRE(candidate_future.valid());
	const auto candidate = candidate_future.get();
	REQUIRE(candidate);
	CHECK_FALSE(candidate->failed);
	CHECK(candidate != generation_one);
	CHECK(candidate->content_generation == 2u);
	for (const auto& component : candidate->components)
	{
		REQUIRE(component);
		CHECK(component->content_generation == candidate->content_generation);
	}

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> still_published{};
	REQUIRE(database.load_terrain_by_id(asset_id, still_published));
	CHECK(still_published == generation_one);
	CHECK(still_published->content_generation == 1u);

	const AshEngine::TerrainSnapshotPublicationToken generation_one_token =
		database.capture_terrain_snapshot_publication(asset_id);
	REQUIRE(generation_one_token.snapshot == generation_one);
	REQUIRE(database.compare_exchange_terrain_snapshot(
		asset_id, generation_one_token, candidate));
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> replaced{};
	REQUIRE(database.load_terrain_by_id(asset_id, replaced));
	CHECK(replaced == candidate);
	CHECK(replaced->content_generation == 2u);

	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase stale candidate replacement cannot overwrite a later publication")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};

	const std::filesystem::path root = TestRoot("candidate-replacement-race");
	const std::filesystem::path relative_path = "terrain/candidate-race.AshTerrain";
	const std::filesystem::path absolute_path = root / relative_path;
	SaveSnapshot(absolute_path, *MakeSnapshot());

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	const AshEngine::TerrainAssetId asset_id = info->id;

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> generation_one{};
	REQUIRE(database.load_terrain_by_id(asset_id, generation_one));
	REQUIRE(generation_one);
	CHECK(generation_one->content_generation == 1u);
	AppendGenerationTwo(absolute_path, generation_one);

	WorkerBlocker blocker{};
	const AshEngine::TerrainSnapshotPublicationToken generation_one_token =
		database.capture_terrain_snapshot_publication(asset_id);
	REQUIRE(generation_one_token.snapshot == generation_one);
	const auto stale_candidate_future =
		database.load_terrain_candidate_by_id_async(asset_id);
	REQUIRE(stale_candidate_future.valid());
	CHECK(stale_candidate_future.wait_for(std::chrono::seconds(0)) ==
		std::future_status::timeout);

	auto generation_three =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*generation_one);
	generation_three->content_generation = 3u;
	for (size_t component_index = 0u;
		component_index < generation_three->components.size();
		++component_index)
	{
		REQUIRE(generation_three->components[component_index]);
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
			*generation_three->components[component_index]);
		component->content_generation = generation_three->content_generation;
		generation_three->components[component_index] = std::move(component);
	}
	REQUIRE(database.publish_terrain_snapshot(asset_id, generation_three));

	blocker.release();
	const auto stale_candidate = stale_candidate_future.get();
	REQUIRE(stale_candidate);
	CHECK_FALSE(stale_candidate->failed);
	CHECK(stale_candidate->content_generation == 2u);

	CHECK_FALSE(database.compare_exchange_terrain_snapshot(
		asset_id, generation_one_token, stale_candidate));
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> still_published{};
	REQUIRE(database.load_terrain_by_id(asset_id, still_published));
	CHECK(still_published == generation_three);
	CHECK(still_published->content_generation == 3u);

	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase publication tokens are bound to one Terrain asset")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};

	const std::filesystem::path root = TestRoot("candidate-token-asset-identity");
	const std::filesystem::path first_path = "terrain/first.AshTerrain";
	const std::filesystem::path second_path = "terrain/second.AshTerrain";
	SaveSnapshot(root / first_path, *MakeSnapshot());
	SaveSnapshot(root / second_path, *MakeSnapshot());

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* first_info = database.find_asset_by_path(first_path);
	const AshEngine::AssetInfo* second_info = database.find_asset_by_path(second_path);
	REQUIRE(first_info != nullptr);
	REQUIRE(second_info != nullptr);
	REQUIRE(first_info->id != second_info->id);

	const AshEngine::TerrainSnapshotPublicationToken first_token =
		database.capture_terrain_snapshot_publication(first_info->id);
	const auto second_candidate =
		database.load_terrain_candidate_by_id_async(second_info->id).get();
	REQUIRE(second_candidate);
	REQUIRE_FALSE(second_candidate->failed);
	REQUIRE(second_candidate->asset_id == second_info->id);

	CHECK_FALSE(database.compare_exchange_terrain_snapshot(
		second_info->id, first_token, second_candidate));
	const AshEngine::TerrainSnapshotPublicationToken second_token =
		database.capture_terrain_snapshot_publication(second_info->id);
	CHECK(second_token.snapshot == nullptr);
	CHECK(second_token.load_serial == 0u);

	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase candidate probes leave diagnostics and load state unchanged")
{
	const std::filesystem::path root = TestRoot("candidate-probe-state");
	const std::filesystem::path text_path = "terrain/not-terrain.txt";
	{
		std::ofstream output(root / text_path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		output << "not a terrain container";
	}

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* text_info = database.find_asset_by_path(text_path);
	REQUIRE(text_info != nullptr);
	CHECK(text_info->type == AshEngine::AssetType::Text);
	const AshEngine::AssetId text_id = text_info->id;

	const AshEngine::AssetId invalid_id = 0xf00du;
	const AshEngine::AssetLoadState invalid_state_before =
		database.get_asset_load_state(invalid_id);
	const std::string invalid_error_before =
		database.get_asset_last_error(invalid_id);
	const std::string global_error_before_invalid = database.get_last_error();
	const auto invalid_candidate =
		database.load_terrain_candidate_by_id_async(invalid_id).get();
	REQUIRE(invalid_candidate);
	CHECK(invalid_candidate->failed);
	CHECK(database.get_asset_load_state(invalid_id) == invalid_state_before);
	CHECK(database.get_asset_last_error(invalid_id) == invalid_error_before);
	CHECK(database.get_last_error() == global_error_before_invalid);

	REQUIRE(database.refresh());
	const AshEngine::AssetLoadState text_state_before =
		database.get_asset_load_state(text_id);
	const std::string text_error_before =
		database.get_asset_last_error(text_id);
	const std::string global_error_before_text = database.get_last_error();
	const auto text_candidate =
		database.load_terrain_candidate_by_id_async(text_id).get();
	REQUIRE(text_candidate);
	CHECK(text_candidate->failed);
	CHECK(database.get_asset_load_state(text_id) == text_state_before);
	CHECK(database.get_asset_last_error(text_id) == text_error_before);
	CHECK(database.get_last_error() == global_error_before_text);

	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase preserves recoverable container diagnostics")
{
	const std::filesystem::path root = TestRoot("recovery-diagnostics");
	const std::filesystem::path relative_path = "terrain/recovered.AshTerrain";
	const std::filesystem::path absolute_path = root / relative_path;
	const auto generation_one = MakeSnapshot();
	SaveSnapshot(absolute_path, *generation_one);
	AppendGenerationTwo(absolute_path, generation_one);
	CorruptNewerIndexCrc(absolute_path);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(database.load_terrain_by_id(info->id, loaded));
	REQUIRE(loaded);
	CHECK_FALSE(loaded->failed);
	CHECK(loaded->content_generation == 1u);
	CHECK(loaded->recovered_previous_generation);
	CHECK(loaded->rejected_content_generation == 2u);
	CHECK(loaded->recovery_detail ==
		"Terrain generation 2 index CRC is invalid.");
	CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_asset_last_error(info->id).empty());

	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain AssetDatabase preserves critical block failure diagnostics")
{
	const std::filesystem::path root = TestRoot("critical-block-failure");
	const std::filesystem::path relative_path = "terrain/corrupt-block.AshTerrain";
	const std::filesystem::path absolute_path = root / relative_path;
	SaveSnapshot(absolute_path, *MakeSnapshot());
	const uint64_t corrupt_block_offset = CorruptCriticalPayload(absolute_path);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	CHECK_FALSE(database.load_terrain_by_id(info->id, loaded));
	CHECK_FALSE(loaded);
	CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Failed);
	CHECK(database.get_asset_last_error(info->id) ==
		"Terrain block CRC mismatch at offset " +
		std::to_string(corrupt_block_offset) + ".");

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
	CHECK(database.get_asset_last_error(asset_id) ==
		"Terrain container header is truncated.");
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

#if defined(_WIN32)
TEST_CASE("Terrain AssetDatabase treats a commit lease as retryable Unloaded state")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};

	const std::filesystem::path root = TestRoot("commit-lease-retry");
	const std::filesystem::path relative_path = "terrain/retry.AshTerrain";
	const std::filesystem::path absolute_path = root / relative_path;
	SaveSnapshot(absolute_path, *MakeSnapshot());
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);

	{
		TerrainTests::ScopedTerrainCommitLeaseForTest lease(absolute_path);
		REQUIRE(lease.acquired());
		const auto blocked = database.load_terrain_by_id_async(info->id);
		CHECK_FALSE(blocked.get());
		CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Unloaded);
		CHECK(database.get_asset_last_error(info->id).find("lease") != std::string::npos);
	}

	const auto recovered = database.load_terrain_by_id_async(info->id).get();
	REQUIRE(recovered != nullptr);
	CHECK(database.get_asset_load_state(info->id) == AshEngine::AssetLoadState::Loaded);
	CHECK(database.get_asset_last_error(info->id).empty());
	std::filesystem::remove_all(root);
}
#endif

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
