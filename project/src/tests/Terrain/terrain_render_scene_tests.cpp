#include "Function/Asset/AssetDatabase.h"
#include "Base/hthreading.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/SunLightShadowPass.h"
#include "Function/Render/TerrainRenderPass.h"
#include "Function/Render/TerrainRenderProxy.h"
#include "Function/Scene/Scene.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
	auto MakeTestRoot(const char* name) -> std::filesystem::path
	{
		const std::filesystem::path root =
			std::filesystem::path(
				"Intermediate/test-temp/tests/terrain-render-scene") / name;
		std::error_code error{};
		std::filesystem::remove_all(root, error);
		std::filesystem::create_directories(root / "terrain", error);
		return root;
	}

	auto PublishFixedSnapshot(
		AshEngine::AssetDatabase& database,
		const std::filesystem::path& relative_path,
		uint64_t generation) ->
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		const AshEngine::AssetInfo* info =
			database.find_asset_by_path(relative_path);
		REQUIRE(info != nullptr);

		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
		snapshot->asset_id = info->id;
		snapshot->source_path = relative_path;
		snapshot->layout = AshEngine::make_default_terrain_grid_layout();
		snapshot->height_mapping = { -100.0f, 1000.0f };
		snapshot->content_generation = generation;
		snapshot->components.resize(
			AshEngine::k_terrain_render_component_capacity);
		REQUIRE(database.publish_terrain_snapshot(info->id, snapshot));
		return snapshot;
	}

	auto MakeInclusiveView() -> AshEngine::SceneView
	{
		AshEngine::SceneView view{};
		view.is_valid = true;
		for (AshEngine::SceneFrustumPlane& plane : view.frustum_planes)
		{
			plane.normal = { 0.0f, 0.0f, 1.0f };
			plane.distance = 100000.0f;
		}
		return view;
	}

	auto ReadSource(const char* path) -> std::string
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
				"TerrainRenderSceneTests::WorkerBlocker",
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

	auto MakeSceneLayout(uint32_t component_count_x, uint32_t component_count_z) ->
		AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x =
			component_count_x * AshEngine::k_terrain_component_quad_count + 1u;
		layout.sample_count_z =
			component_count_z * AshEngine::k_terrain_component_quad_count + 1u;
		layout.component_count_x = component_count_x;
		layout.component_count_z = component_count_z;
		layout.component_quad_count = AshEngine::k_terrain_component_quad_count;
		layout.sample_spacing_meters = 1.0f;
		return layout;
	}

	auto MakeSceneComponent(
		AshEngine::TerrainComponentCoord coord,
		uint64_t generation) ->
		std::shared_ptr<const AshEngine::TerrainComponentSnapshot>
	{
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>();
		component->coord = coord;
		component->content_generation = generation;
		component->sample_width = AshEngine::k_terrain_component_sample_count;
		component->sample_height = AshEngine::k_terrain_component_sample_count;
		component->heights.assign(
			static_cast<size_t>(AshEngine::k_terrain_component_sample_count) *
				AshEngine::k_terrain_component_sample_count,
			0.0f);
		return component;
	}

	auto MakeSceneSnapshot(
		AshEngine::TerrainAssetId asset_id,
		const std::filesystem::path& source_path,
		uint64_t generation,
		const AshEngine::TerrainGridLayout& layout,
		bool dense) -> std::shared_ptr<AshEngine::TerrainAssetSnapshot>
	{
		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
		snapshot->asset_id = asset_id;
		snapshot->source_path = source_path;
		snapshot->layout = layout;
		snapshot->height_mapping = { -100.0f, 1000.0f };
		snapshot->content_generation = generation;
		snapshot->components.resize(
			static_cast<size_t>(layout.component_count_x) *
				layout.component_count_z);
		if (dense)
		{
			for (uint32_t z = 0u; z < layout.component_count_z; ++z)
			{
				for (uint32_t x = 0u; x < layout.component_count_x; ++x)
				{
					const size_t index =
						static_cast<size_t>(z) * layout.component_count_x + x;
					snapshot->components[index] = MakeSceneComponent(
						{ static_cast<uint16_t>(x), static_cast<uint16_t>(z) },
						generation);
				}
			}
		}
		return snapshot;
	}
}

namespace AshEngine
{
	struct TerrainRenderSceneTestSeam
	{
		static auto InstallPublishedView(
			TerrainRenderAsset& asset,
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
			uint64_t publication_epoch) ->
			std::shared_ptr<const TerrainPublishedRenderView>
		{
			TerrainRenderLayoutInfo layout{};
			REQUIRE(derive_terrain_render_layout(snapshot->layout, layout));
			auto runtime = std::make_shared<TerrainRenderRuntimeState>();
			runtime->target_snapshot = snapshot;
			runtime->work_status = TerrainRenderWorkStatus::Ready;
			runtime->resources.height = std::shared_ptr<StorageBuffer>(
				reinterpret_cast<StorageBuffer*>(uintptr_t{ 1u }),
				[](StorageBuffer*) {});
			runtime->resources.staging = std::shared_ptr<StorageBuffer>(
				reinterpret_cast<StorageBuffer*>(uintptr_t{ 2u }),
				[](StorageBuffer*) {});
			for (uint32_t index = 0u; index < runtime->resources.atlas.size(); ++index)
			{
				runtime->resources.atlas[index] = std::shared_ptr<RenderTarget>(
					reinterpret_cast<RenderTarget*>(
						uintptr_t{ static_cast<uintptr_t>(3u + index) }),
					[](RenderTarget*) {});
			}
			runtime->resources.coarse = std::shared_ptr<RenderTarget>(
				reinterpret_cast<RenderTarget*>(uintptr_t{ 5u }),
				[](RenderTarget*) {});

			auto view = std::make_shared<TerrainPublishedRenderView>();
			view->snapshot = snapshot;
			view->layout = layout;
			view->runtime = runtime;
			view->asset_id = snapshot->asset_id;
			view->content_generation = snapshot->content_generation;
			view->residency_revision = snapshot->residency_revision;
			view->publication_epoch = publication_epoch;
			{
				std::scoped_lock<std::mutex> lock(asset.m_mutex);
				asset.m_candidate_state.reset();
				asset.m_published_view = view;
				asset.m_last_error.clear();
			}
			return view;
		}

		static void FailCandidate(
			TerrainRenderAsset& asset,
			TerrainRenderCandidateStage stage,
			const std::string& reason)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			REQUIRE(asset.m_candidate_state != nullptr);
			asset.m_candidate_state->stage = stage;
			asset.fail_candidate_locked(
				asset.m_candidate_state->runtime,
				asset.m_candidate_state->candidate_epoch,
				reason);
		}
	};
}

TEST_CASE("Terrain RenderScene async resolve keeps non Terrain content valid while pending")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threading_config{};
	threading_config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threading_config));
	ThreadingScope threading_scope{};
	WorkerBlocker blocker{};

	const std::filesystem::path root = MakeTestRoot("async-pending");
	const std::filesystem::path relative_path = "terrain/Pending.AshTerrain";
	{
		std::ofstream placeholder(root / relative_path, std::ios::binary);
		REQUIRE(placeholder.is_open());
		placeholder.put('\0');
	}
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	AshEngine::RenderAssetManager render_asset_manager{};
	render_asset_manager.initialize(&database, nullptr);

	AshEngine::Scene scene = AshEngine::Scene::create("Pending Terrain Scene");
	AshEngine::Entity terrain_entity = scene.create_entity("Terrain");
	AshEngine::TerrainComponent terrain{};
	terrain.asset_path = relative_path.generic_string();
	REQUIRE(terrain_entity.add_terrain_component(terrain));

	AshEngine::RenderScene render_scene{};
	CHECK(render_scene.rebuild_from_scene(scene, render_asset_manager));
	const AshEngine::TerrainSceneResolveResult result =
		render_scene.last_terrain_resolve_result();
	CHECK(result.status == AshEngine::TerrainSceneResolveStatus::Pending);
	CHECK(result.asset_path == relative_path.generic_string());
	CHECK(result.diagnostic.empty());
	AshEngine::VisibleRenderFrame visible_frame{};
	CHECK(render_scene.build_visible_render_frame(
		1u, MakeInclusiveView(), visible_frame));
	CHECK(visible_frame.terrains.empty());
	CHECK(visible_frame.terrain_resolve_status ==
		AshEngine::TerrainSceneResolveStatus::Pending);

	blocker.release();
	AshEngine::TerrainSceneResolveResult completed = result;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(2);
	while (completed.status == AshEngine::TerrainSceneResolveStatus::Pending &&
		std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		completed = render_scene.rebuild_terrains_from_scene(
			scene, render_asset_manager);
	}
	CHECK(completed.status == AshEngine::TerrainSceneResolveStatus::Failed);
	render_asset_manager.shutdown();
	std::error_code error{};
	std::filesystem::remove_all(root, error);
}

TEST_CASE("Terrain RenderScene async resolve reports stable failure diagnostics")
{
	const std::filesystem::path root = MakeTestRoot("async-failed");
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	AshEngine::RenderAssetManager render_asset_manager{};
	render_asset_manager.initialize(&database, nullptr);

	AshEngine::Scene scene = AshEngine::Scene::create("Failed Terrain Scene");
	AshEngine::Entity terrain_entity = scene.create_entity("Terrain");
	AshEngine::TerrainComponent terrain{};
	terrain.asset_path = "terrain/Missing.AshTerrain";
	REQUIRE(terrain_entity.add_terrain_component(terrain));

	AshEngine::RenderScene render_scene{};
	const AshEngine::TerrainSceneResolveResult result =
		render_scene.rebuild_terrains_from_scene(scene, render_asset_manager);
	CHECK(result.status == AshEngine::TerrainSceneResolveStatus::Failed);
	CHECK(result.asset_path == terrain.asset_path);
	CHECK_FALSE(result.diagnostic.empty());
	CHECK(result.content_generation == 0u);

	AshEngine::TerrainSceneResolveResult same = result;
	CHECK_FALSE(AshEngine::terrain_scene_resolve_transition_changed(result, same));
	same.diagnostic += " changed";
	CHECK(AshEngine::terrain_scene_resolve_transition_changed(result, same));
	same = result;
	same.status = AshEngine::TerrainSceneResolveStatus::Ready;
	CHECK(AshEngine::terrain_scene_resolve_transition_changed(result, same));

	render_asset_manager.shutdown();
	std::error_code error{};
	std::filesystem::remove_all(root, error);
}

TEST_CASE("Visible terrain frame keeps an immutable content-generation snapshot")
{
	auto mutable5 = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	mutable5->asset_id = 42u;
	mutable5->content_generation = 5u;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot5 = mutable5;

	AshEngine::RenderTerrainProxy proxy{};
	REQUIRE(proxy.initialize(
		9u,
		snapshot5,
		glm::mat4(1.0f),
		true,
		true,
		true));
	AshEngine::VisibleTerrainFrame visible = proxy.make_visible_frame();

	auto mutable6 =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot5);
	mutable6->content_generation = 6u;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot6 = mutable6;
	REQUIRE(proxy.replace_snapshot(snapshot6));

	REQUIRE(visible.asset_snapshot != nullptr);
	CHECK(visible.asset_snapshot->content_generation == 5u);
	REQUIRE(proxy.make_visible_frame().asset_snapshot != nullptr);
	CHECK(proxy.make_visible_frame().asset_snapshot->content_generation == 6u);
	CHECK(visible.entity_id == 9u);
}

TEST_CASE("Terrain proxy rejects overflowing bounds without changing published state")
{
	auto initial = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	initial->asset_id = 42u;
	initial->content_generation = 1u;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> initial_snapshot = initial;

	AshEngine::RenderTerrainProxy proxy{};
	REQUIRE(proxy.initialize(
		9u,
		initial_snapshot,
		glm::mat4(1.0f),
		true,
		true,
		true));
	const AshEngine::VisibleTerrainFrame initial_frame =
		proxy.make_visible_frame();

	glm::mat4 overflowing_transform{ 1.0f };
	overflowing_transform[0][0] = std::numeric_limits<float>::max();
	CHECK_FALSE(proxy.update_world_transform(overflowing_transform));
	const AshEngine::VisibleTerrainFrame after_transform =
		proxy.make_visible_frame();
	CHECK(after_transform.world_transform == initial_frame.world_transform);
	CHECK(after_transform.world_bounds.world_max ==
		initial_frame.world_bounds.world_max);

	auto overflowing =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*initial_snapshot);
	overflowing->content_generation = 2u;
	overflowing->layout.sample_count_x =
		std::numeric_limits<uint32_t>::max();
	overflowing->layout.sample_spacing_meters =
		std::numeric_limits<float>::max();
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> overflowing_snapshot =
		overflowing;
	CHECK_FALSE(proxy.replace_snapshot(overflowing_snapshot));
	CHECK(proxy.get_snapshot() == initial_snapshot);
	CHECK(proxy.get_bounds().world_max == initial_frame.world_bounds.world_max);
}

TEST_CASE("Terrain published view switches bounds temporal and shadow identity only on next visible frame")
{
	const auto old_snapshot = MakeSceneSnapshot(
		42u, "terrain/Coherent.AshTerrain", 5u, MakeSceneLayout(1u, 1u), true);
	const auto new_snapshot = MakeSceneSnapshot(
		42u, "terrain/Coherent.AshTerrain", 6u, MakeSceneLayout(2u, 1u), true);
	auto render_asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	const auto old_view = AshEngine::TerrainRenderSceneTestSeam::
		InstallPublishedView(*render_asset, old_snapshot, 7u);

	AshEngine::RenderTerrainProxy proxy{};
	REQUIRE(proxy.initialize(
		9u,
		old_snapshot,
		glm::mat4(1.0f),
		true,
		true,
		true,
		render_asset,
		"terrain/Coherent.AshTerrain"));
	const AshEngine::VisibleTerrainFrame frame_n = proxy.make_visible_frame();
	REQUIRE(frame_n.published_view == old_view);
	CHECK(frame_n.asset_snapshot == old_snapshot);
	CHECK(frame_n.world_bounds.local_max.x == doctest::Approx(256.0f));

	AshEngine::VisibleRenderFrame visible_n{};
	visible_n.scene_runtime_id = 11u;
	visible_n.scene_content_epoch = 13u;
	visible_n.terrains.push_back(frame_n);
	const uint64_t temporal_n =
		AshEngine::compute_terrain_temporal_signature(visible_n);
	const uint64_t shadow_n =
		AshEngine::compute_static_shadow_caster_revision(visible_n);

	const auto new_view = AshEngine::TerrainRenderSceneTestSeam::
		InstallPublishedView(*render_asset, new_snapshot, 8u);
	CHECK(frame_n.published_view == old_view);
	CHECK(frame_n.asset_snapshot == old_snapshot);
	CHECK(frame_n.world_bounds.local_max.x == doctest::Approx(256.0f));
	CHECK(AshEngine::compute_terrain_temporal_signature(visible_n) == temporal_n);
	CHECK(AshEngine::compute_static_shadow_caster_revision(visible_n) == shadow_n);

	const AshEngine::VisibleTerrainFrame frame_n_plus_1 =
		proxy.make_visible_frame();
	REQUIRE(frame_n_plus_1.published_view == new_view);
	CHECK(frame_n_plus_1.asset_snapshot == new_snapshot);
	CHECK(frame_n_plus_1.world_bounds.local_max.x == doctest::Approx(512.0f));
	CHECK(frame_n_plus_1.published_view->runtime !=
		frame_n.published_view->runtime);

	AshEngine::VisibleRenderFrame visible_n_plus_1 = visible_n;
	visible_n_plus_1.terrains = { frame_n_plus_1 };
	CHECK(AshEngine::compute_terrain_temporal_signature(visible_n_plus_1) !=
		temporal_n);
	CHECK(AshEngine::compute_static_shadow_caster_revision(visible_n_plus_1) !=
		shadow_n);
}

TEST_CASE("Terrain published view pins pending state and capture readiness against live getters")
{
	const auto old_snapshot = MakeSceneSnapshot(
		42u, "terrain/Pending.AshTerrain", 5u, MakeSceneLayout(1u, 1u), true);
	const auto candidate_snapshot = MakeSceneSnapshot(
		42u, "terrain/Pending.AshTerrain", 6u, MakeSceneLayout(1u, 2u), true);
	auto render_asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	const auto old_view = AshEngine::TerrainRenderSceneTestSeam::
		InstallPublishedView(*render_asset, old_snapshot, 3u);

	AshEngine::RenderTerrainProxy proxy{};
	REQUIRE(proxy.initialize(
		9u,
		old_snapshot,
		glm::mat4(1.0f),
		true,
		true,
		true,
		render_asset,
		"terrain/Pending.AshTerrain"));
	AshEngine::VisibleRenderFrame frame{};
	frame.scene_runtime_id = 1u;
	frame.scene_content_epoch = 1u;
	frame.render_asset_epoch = 20u;
	frame.terrains.push_back(proxy.make_visible_frame());
	REQUIRE(frame.terrains.front().published_view == old_view);
	const uint64_t temporal =
		AshEngine::compute_terrain_temporal_signature(frame);
	const uint64_t shadow =
		AshEngine::compute_static_shadow_caster_revision(frame);

	REQUIRE(render_asset->accept_snapshot(candidate_snapshot));
	CHECK(proxy.make_visible_frame().published_view == old_view);
	CHECK(AshEngine::compute_terrain_temporal_signature(frame) == temporal);
	CHECK(AshEngine::compute_static_shadow_caster_revision(frame) == shadow);
	AshEngine::TerrainRenderPass pass{};
	CHECK(pass.is_capture_ready(frame));
	CHECK(AshEngine::terrain_frame_asset_epoch_is_current(frame, 20u));
	CHECK_FALSE(AshEngine::terrain_frame_asset_epoch_is_current(frame, 21u));
}

TEST_CASE("Terrain published view stale empty frame cannot report capture ready after asset epoch advances")
{
	AshEngine::VisibleRenderFrame frame{};
	frame.render_asset_epoch = 40u;
	CHECK(frame.terrains.empty());
	CHECK(AshEngine::terrain_frame_asset_epoch_is_current(frame, 40u));
	CHECK_FALSE(AshEngine::terrain_frame_asset_epoch_is_current(frame, 41u));
}

TEST_CASE("Terrain candidate error retains same path published view with exact stage and layout")
{
	const std::filesystem::path root = MakeTestRoot("candidate-error-retains");
	const std::filesystem::path relative_path = "terrain/Retained.AshTerrain";
	{
		std::ofstream placeholder(root / relative_path, std::ios::binary);
		REQUIRE(placeholder.is_open());
		placeholder.put('\0');
	}
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* info = database.find_asset_by_path(relative_path);
	REQUIRE(info != nullptr);
	const auto old_snapshot = MakeSceneSnapshot(
		info->id, relative_path, 1u, MakeSceneLayout(1u, 1u), true);
	REQUIRE(database.publish_terrain_snapshot(info->id, old_snapshot));

	AshEngine::RenderAssetManager manager{};
	manager.initialize(&database, nullptr);
	const auto asset = manager.request_terrain_asset(
		relative_path.generic_string(), old_snapshot);
	REQUIRE(asset != nullptr);
	const auto old_view = AshEngine::TerrainRenderSceneTestSeam::
		InstallPublishedView(*asset, old_snapshot, 1u);

	AshEngine::Scene scene = AshEngine::Scene::create("Retained Terrain Scene");
	AshEngine::Entity terrain_entity = scene.create_entity("Terrain");
	AshEngine::TerrainComponent terrain{};
	terrain.asset_path = relative_path.generic_string();
	REQUIRE(terrain_entity.add_terrain_component(terrain));
	AshEngine::RenderScene render_scene{};
	REQUIRE(render_scene.rebuild_terrains_from_scene(scene, manager).status ==
		AshEngine::TerrainSceneResolveStatus::Ready);

	const auto candidate_snapshot = MakeSceneSnapshot(
		info->id, relative_path, 2u, MakeSceneLayout(1u, 2u), true);
	REQUIRE(database.publish_terrain_snapshot(info->id, candidate_snapshot));
	const AshEngine::TerrainSceneResolveResult pending =
		render_scene.rebuild_terrains_from_scene(scene, manager);
	CHECK(pending.status == AshEngine::TerrainSceneResolveStatus::Pending);
	CHECK(pending.drawable_published_view);
	AshEngine::VisibleRenderFrame pending_frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		2u, MakeInclusiveView(), pending_frame));
	REQUIRE(pending_frame.terrains.size() == 1u);
	CHECK(pending_frame.terrains.front().published_view == old_view);

	AshEngine::TerrainRenderSceneTestSeam::FailCandidate(
		*asset,
		AshEngine::TerrainRenderCandidateStage::UploadHeights,
		"injected height upload failure");
	const AshEngine::TerrainSceneResolveResult failed =
		render_scene.rebuild_terrains_from_scene(scene, manager);
	CHECK(failed.status == AshEngine::TerrainSceneResolveStatus::Failed);
	CHECK(failed.drawable_published_view);
	CHECK(failed.asset_path == relative_path.generic_string());
	CHECK(failed.diagnostic.find("stage=UploadHeights") != std::string::npos);
	CHECK(failed.diagnostic.find("samples=257x513") != std::string::npos);
	CHECK(failed.diagnostic.find("components=1x2") != std::string::npos);
	CHECK(failed.diagnostic.find("quads=256") != std::string::npos);
	CHECK(failed.diagnostic.find("spacing=1") != std::string::npos);
	CHECK(failed.diagnostic.find("injected height upload failure") !=
		std::string::npos);
	CHECK(failed.diagnostic.find(relative_path.generic_string()) !=
		std::string::npos);
	CHECK(failed.diagnostic.find("published view retained") !=
		std::string::npos);
	AshEngine::VisibleRenderFrame failed_frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		3u, MakeInclusiveView(), failed_frame));
	REQUIRE(failed_frame.terrains.size() == 1u);
	CHECK(failed_frame.terrains.front().published_view == old_view);

	manager.shutdown();
	std::error_code error{};
	std::filesystem::remove_all(root, error);
}

TEST_CASE("Terrain published view path change never retains the prior proxy")
{
	const std::filesystem::path root = MakeTestRoot("path-change");
	const std::filesystem::path old_path = "terrain/Old.AshTerrain";
	const std::filesystem::path new_path = "terrain/New.AshTerrain";
	for (const auto& path : { old_path, new_path })
	{
		std::ofstream placeholder(root / path, std::ios::binary);
		REQUIRE(placeholder.is_open());
		placeholder.put('\0');
	}
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const AshEngine::AssetInfo* old_info = database.find_asset_by_path(old_path);
	const AshEngine::AssetInfo* new_info = database.find_asset_by_path(new_path);
	REQUIRE(old_info != nullptr);
	REQUIRE(new_info != nullptr);
	const auto old_snapshot = MakeSceneSnapshot(
		old_info->id, old_path, 1u, MakeSceneLayout(1u, 1u), true);
	const auto new_snapshot = MakeSceneSnapshot(
		new_info->id, new_path, 1u, MakeSceneLayout(1u, 2u), true);
	REQUIRE(database.publish_terrain_snapshot(old_info->id, old_snapshot));
	REQUIRE(database.publish_terrain_snapshot(new_info->id, new_snapshot));

	AshEngine::RenderAssetManager manager{};
	manager.initialize(&database, nullptr);
	const auto old_asset = manager.request_terrain_asset(
		old_path.generic_string(), old_snapshot);
	REQUIRE(old_asset != nullptr);
	AshEngine::TerrainRenderSceneTestSeam::InstallPublishedView(
		*old_asset, old_snapshot, 1u);
	AshEngine::Scene scene = AshEngine::Scene::create("Terrain Path Change");
	AshEngine::Entity entity = scene.create_entity("Terrain");
	AshEngine::TerrainComponent terrain{};
	terrain.asset_path = old_path.generic_string();
	REQUIRE(entity.add_terrain_component(terrain));
	AshEngine::RenderScene render_scene{};
	REQUIRE(render_scene.rebuild_terrains_from_scene(scene, manager).status ==
		AshEngine::TerrainSceneResolveStatus::Ready);

	terrain.asset_path = new_path.generic_string();
	REQUIRE(entity.set_terrain_component(terrain));
	const AshEngine::TerrainSceneResolveResult changed =
		render_scene.rebuild_terrains_from_scene(scene, manager);
	CHECK(changed.status == AshEngine::TerrainSceneResolveStatus::Pending);
	CHECK_FALSE(changed.drawable_published_view);
	AshEngine::VisibleRenderFrame frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		2u, MakeInclusiveView(), frame));
	CHECK(frame.terrains.empty());

	manager.shutdown();
	std::error_code error{};
	std::filesystem::remove_all(root, error);
}

TEST_CASE("Visible terrain frame culls bounds and keeps transform updates immutable")
{
	const std::filesystem::path root = MakeTestRoot("visibility-transform");
	const std::filesystem::path relative_path = "terrain/Test.AshTerrain";
	{
		std::ofstream placeholder(root / relative_path, std::ios::binary);
		REQUIRE(placeholder.is_open());
		placeholder.put('\0');
	}

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	const auto first_snapshot =
		PublishFixedSnapshot(database, relative_path, 1u);
	AshEngine::RenderAssetManager render_asset_manager{};
	render_asset_manager.initialize(&database, nullptr);

	AshEngine::Scene scene = AshEngine::Scene::create("Terrain Render Scene");
	AshEngine::Entity terrain_entity = scene.create_entity("Terrain");
	AshEngine::TerrainComponent terrain{};
	terrain.asset_path = relative_path.generic_string();
	REQUIRE(terrain_entity.add_terrain_component(terrain));

	AshEngine::RenderScene render_scene{};
	REQUIRE(render_scene.rebuild_terrains_from_scene(
		scene, render_asset_manager).status ==
		AshEngine::TerrainSceneResolveStatus::Ready);
	AshEngine::SceneView inclusive_view = MakeInclusiveView();
	AshEngine::VisibleRenderFrame first_frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		1u, inclusive_view, first_frame));
	REQUIRE(first_frame.terrains.size() == 1u);
	CHECK(first_frame.terrains[0].entity_id == terrain_entity.get_id());
	CHECK(first_frame.terrains[0].asset_snapshot == first_snapshot);
	REQUIRE(first_frame.terrains[0].render_asset != nullptr);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> first_render_asset =
		first_frame.terrains[0].render_asset;

	AshEngine::SceneView excluded_view = inclusive_view;
	excluded_view.frustum_planes[0].normal = { 1.0f, 0.0f, 0.0f };
	excluded_view.frustum_planes[0].distance = -9000.0f;
	AshEngine::VisibleRenderFrame excluded_frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		2u, excluded_view, excluded_frame));
	CHECK(excluded_frame.terrains.empty());

	AshEngine::TransformComponent moved{};
	moved.position = { 1000.0f, 20.0f, 30.0f };
	moved.scale = { 2.0f, 3.0f, 2.0f };
	REQUIRE(terrain_entity.set_transform_component(moved));
	REQUIRE(render_scene.update_terrain_transforms_from_scene(scene));
	AshEngine::VisibleRenderFrame moved_frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		3u, inclusive_view, moved_frame));
	REQUIRE(moved_frame.terrains.size() == 1u);
	CHECK(moved_frame.terrains[0].asset_snapshot == first_snapshot);
	CHECK(moved_frame.terrains[0].render_asset == first_render_asset);
	CHECK(moved_frame.terrains[0].world_bounds.world_min.x ==
		doctest::Approx(1000.0f));
	CHECK(moved_frame.terrains[0].world_bounds.world_min.y ==
		doctest::Approx(-280.0f));

	const auto second_snapshot =
		PublishFixedSnapshot(database, relative_path, 2u);
	REQUIRE(render_scene.rebuild_terrains_from_scene(
		scene, render_asset_manager).status ==
		AshEngine::TerrainSceneResolveStatus::Ready);
	AshEngine::VisibleRenderFrame rebuilt_frame{};
	REQUIRE(render_scene.build_visible_render_frame(
		4u, inclusive_view, rebuilt_frame));
	REQUIRE(rebuilt_frame.terrains.size() == 1u);
	CHECK(rebuilt_frame.terrains[0].asset_snapshot == second_snapshot);
	CHECK(rebuilt_frame.terrains[0].render_asset == first_render_asset);

	render_asset_manager.shutdown();
	std::error_code error{};
	std::filesystem::remove_all(root, error);
}

TEST_CASE("Terrain presentation tracks an independent terrain revision")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Render/ScenePresentationSubsystem.cpp");
	CHECK(source.find("uint64_t last_terrain_version = 0") !=
		std::string::npos);
	CHECK(source.find("get_render_terrain_version()") !=
		std::string::npos);
	CHECK(source.find("rebuild_terrains_from_scene") !=
		std::string::npos);
	CHECK(source.find("update_terrain_transforms_from_scene") !=
		std::string::npos);

	const size_t terrain_rebuild_branch = source.find(
		"if (scene_state->last_terrain_version != scene_terrain_version ||");
	const size_t transform_update_branch = source.find(
		"if (scene_state->last_transform_version != scene_transform_version)");
	REQUIRE(terrain_rebuild_branch != std::string::npos);
	REQUIRE(transform_update_branch != std::string::npos);
	CHECK(terrain_rebuild_branch < transform_update_branch);
}

TEST_CASE("Terrain presentation logs typed resolve state transitions once")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Render/ScenePresentationSubsystem.cpp");
	CHECK(source.find("record_terrain_resolve_transition") !=
		std::string::npos);
	CHECK(source.find("terrain_scene_resolve_transition_changed") !=
		std::string::npos);
	CHECK(source.find("Terrain resolve pending") != std::string::npos);
	CHECK(source.find("Terrain resolve failed") != std::string::npos);
	CHECK(source.find("Terrain resolve recovered") != std::string::npos);
	CHECK(source.find(
		"failed to rebuild RenderScene terrains for binding") ==
		std::string::npos);
	CHECK(source.find(
		"terrain_resolve_result.status ==\n\t\t\t\t\t\tTerrainSceneResolveStatus::Pending") !=
		std::string::npos);
}

TEST_CASE("Static shadow caster revision binds scene identity and exact static mesh draws")
{
	AshEngine::VisibleRenderFrame frame{};
	frame.scene_runtime_id = 101u;
	frame.scene_content_epoch = 7u;
	frame.static_scene_revision = 11u;
	frame.transform_scene_revision = 17u;

	auto static_asset = std::make_shared<AshEngine::StaticMeshRenderAsset>();
	static_asset->asset_path = "models/static.glb";
	static_asset->mesh_index = 2u;
	static_asset->state = AshEngine::StaticMeshRenderAssetState::GpuReady;
	static_asset->resource =
		std::make_shared<AshEngine::StaticMeshRenderResource>();

	AshEngine::VisibleStaticMeshDraw static_draw{};
	static_draw.primitive_id = 4u;
	static_draw.entity_id = 8u;
	static_draw.mobility = AshEngine::SceneMobility::Static;
	static_draw.render_asset = static_asset;
	static_draw.world_transform = glm::mat4(1.0f);
	AshEngine::ResolvedStaticMeshSection section{};
	section.first_index = 3u;
	section.index_count = 12u;
	section.depth_only_publication_identity = 23u;
	static_draw.sections.push_back(section);
	frame.shadow_caster_static_mesh_draws.push_back(static_draw);

	AshEngine::VisibleStaticMeshDraw movable_draw = static_draw;
	movable_draw.primitive_id = 5u;
	movable_draw.entity_id = 9u;
	movable_draw.mobility = AshEngine::SceneMobility::Movable;
	frame.shadow_caster_static_mesh_draws.push_back(movable_draw);

	const uint64_t original =
		AshEngine::compute_static_shadow_caster_revision(frame);
	CHECK(original != 0u);
	CHECK(AshEngine::compute_static_shadow_caster_revision(frame) == original);

	AshEngine::VisibleRenderFrame changed = frame;
	changed.scene_runtime_id += 1u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);

	changed = frame;
	changed.scene_content_epoch += 1u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);

	changed = frame;
	changed.static_scene_revision += 1u;
	changed.shadow_caster_static_mesh_draws[1].sections[0].index_count += 3u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) == original);

	changed = frame;
	changed.transform_scene_revision += 1u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) == original);

	changed = frame;
	changed.shadow_caster_static_mesh_draws[1].world_transform[3][0] = 50.0f;
	changed.shadow_caster_static_mesh_draws[1].sections[0].index_count += 3u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) == original);

	changed = frame;
	changed.shadow_caster_static_mesh_draws[0].world_transform[3][0] = 8.0f;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);

	changed = frame;
	changed.shadow_caster_static_mesh_draws[0].sections[0].index_count += 3u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);

	changed = frame;
	changed.shadow_caster_static_mesh_draws[0]
		.sections[0].depth_only_publication_identity += 1u;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);

	changed = frame;
	changed.shadow_caster_static_mesh_draws[0].render_asset =
		std::make_shared<AshEngine::StaticMeshRenderAsset>();
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);

	static_asset->resource =
		std::make_shared<AshEngine::StaticMeshRenderResource>();
	CHECK(AshEngine::compute_static_shadow_caster_revision(frame) != original);
}

TEST_CASE("Static shadow caster revision includes only enabled terrain casters")
{
	AshEngine::VisibleRenderFrame frame{};
	frame.scene_runtime_id = 101u;
	frame.scene_content_epoch = 7u;
	frame.static_scene_revision = 11u;

	auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	snapshot->asset_id = 42u;
	snapshot->layout = AshEngine::make_default_terrain_grid_layout();
	snapshot->content_generation = 5u;
	snapshot->residency_revision = 3u;
	snapshot->components.resize(AshEngine::k_terrain_render_component_capacity);

	auto render_asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	REQUIRE(render_asset->accept_snapshot(snapshot));

	AshEngine::VisibleTerrainFrame terrain{};
	terrain.entity_id = 9u;
	terrain.asset_snapshot = snapshot;
	terrain.render_asset = render_asset;
	terrain.world_transform = glm::mat4(1.0f);
	terrain.casts_shadow = false;
	frame.terrains.push_back(terrain);

	const uint64_t original =
		AshEngine::compute_static_shadow_caster_revision(frame);
	AshEngine::VisibleRenderFrame changed = frame;
	auto changed_snapshot =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot);
	changed_snapshot->content_generation += 1u;
	changed.terrains[0].asset_snapshot = changed_snapshot;
	changed.terrains[0].world_transform[3][0] = 8.0f;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) == original);

	changed = frame;
	changed.terrains[0].casts_shadow = true;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != original);
	const uint64_t enabled =
		AshEngine::compute_static_shadow_caster_revision(changed);

	AshEngine::VisibleRenderFrame changed_enabled = changed;
	changed_snapshot =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot);
	changed_snapshot->residency_revision += 1u;
	changed_enabled.terrains[0].asset_snapshot = changed_snapshot;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed_enabled) != enabled);

	changed_enabled = changed;
	changed_enabled.terrains[0].world_transform[3][0] = 8.0f;
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed_enabled) != enabled);

	auto next_snapshot =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot);
	next_snapshot->content_generation += 1u;
	REQUIRE(render_asset->accept_snapshot(next_snapshot));
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) == enabled);
}

TEST_CASE("Terrain shadow caster identity is captured by one immutable snapshot")
{
	auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	snapshot->asset_id = 42u;
	snapshot->layout = AshEngine::make_default_terrain_grid_layout();
	snapshot->content_generation = 5u;
	snapshot->residency_revision = 3u;
	snapshot->components.resize(AshEngine::k_terrain_render_component_capacity);

	AshEngine::TerrainRenderAsset render_asset{};
	REQUIRE(render_asset.accept_snapshot(snapshot));
	const AshEngine::TerrainShadowCasterIdentity identity =
		render_asset.snapshot_shadow_caster_identity();
	CHECK(identity.has_accepted_snapshot);
	CHECK(identity.accepted_asset_id == 42u);
	CHECK(identity.accepted_content_generation == 5u);
	CHECK(identity.accepted_residency_revision == 3u);
	CHECK(identity.active_content_generation == 5u);
	CHECK(identity.pending_component_upload_count == 0u);
	CHECK(identity.pending_component_removal_count == 0u);

	auto next_snapshot =
		std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot);
	next_snapshot->content_generation = 6u;
	next_snapshot->residency_revision = 4u;
	REQUIRE(render_asset.accept_snapshot(next_snapshot));
	const AshEngine::TerrainShadowCasterIdentity changed =
		render_asset.snapshot_shadow_caster_identity();
	CHECK(changed.accepted_snapshot_identity !=
		identity.accepted_snapshot_identity);
	CHECK(changed.accepted_content_generation == 6u);
	CHECK(changed.accepted_residency_revision == 4u);
	CHECK(changed.active_content_generation == 6u);
}

TEST_CASE("Sunlight static cache consumes the complete shadow caster revision")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Render/SunLightShadowPass.cpp");
	const std::string presentation_source = ReadSource(
		"project/src/engine/Function/Render/ScenePresentationSubsystem.cpp");
	CHECK(source.find(
		"compute_static_shadow_caster_revision(frame)") !=
		std::string::npos);
	CHECK(source.find("frame.transform_scene_revision") == std::string::npos);

	const size_t planner_begin = source.find(
		"bool build_sunlight_shadow_frame_plan_internal(");
	const size_t planner_end = source.find(
		"void commit_static_cache_refresh_for_tests(", planner_begin);
	REQUIRE(planner_begin != std::string::npos);
	REQUIRE(planner_end != std::string::npos);
	CHECK(source.substr(planner_begin, planner_end - planner_begin).find(
		"commit_static_cache_refresh(") == std::string::npos);

	const size_t refresh_pass_begin = source.find(
		"SceneDirectionalShadowStaticCacheRefreshPass");
	const size_t dynamic_pass_begin = source.find(
		"SceneDirectionalShadowDynamicCascadePass_", refresh_pass_begin);
	REQUIRE(refresh_pass_begin != std::string::npos);
	REQUIRE(dynamic_pass_begin != std::string::npos);
	const std::string refresh_pass = source.substr(
		refresh_pass_begin, dynamic_pass_begin - refresh_pass_begin);
	const size_t clear_draw = refresh_pass.find("context.draw(");
	const size_t static_draw = refresh_pass.find("draw_callback(");
	const size_t commit = refresh_pass.find("commit_static_cache_refresh(");
	REQUIRE(clear_draw != std::string::npos);
	REQUIRE(static_draw != std::string::npos);
	REQUIRE(commit != std::string::npos);
	CHECK(clear_draw < static_draw);
	CHECK(static_draw < commit);
	const size_t publication_capture = presentation_source.find(
		"section.depth_only_publication_identity =");
	const size_t publication_identity = presentation_source.find(
		"material_proxy->get_surface_staticmesh_depthonly_publication_identity()",
		publication_capture);
	REQUIRE(publication_capture != std::string::npos);
	REQUIRE(publication_identity != std::string::npos);
	CHECK(publication_identity - publication_capture < 160u);
}

TEST_CASE("Static shadow cache refresh commits only after clear and caster recording succeed")
{
	auto record = [](bool clear_succeeds,
		bool caster_draw_succeeds,
		std::vector<std::string>& events,
		bool& committed)
	{
		return AshEngine::SunLightShadowDetail::record_static_cache_refresh_if_complete(
			[&]()
			{
				events.emplace_back("clear");
				return clear_succeeds;
			},
			[&]()
			{
				events.emplace_back("casters");
				return caster_draw_succeeds;
			},
			[&]()
			{
				events.emplace_back("commit");
				committed = true;
			});
	};

	SUBCASE("clear failure stops before caster recording")
	{
		std::vector<std::string> events{};
		bool committed = false;
		CHECK_FALSE(record(false, true, events, committed));
		CHECK((events == std::vector<std::string>{ "clear" }));
		CHECK_FALSE(committed);
	}

	SUBCASE("caster failure leaves the cache uncommitted")
	{
		std::vector<std::string> events{};
		bool committed = false;
		CHECK_FALSE(record(true, false, events, committed));
		CHECK((events == std::vector<std::string>{ "clear", "casters" }));
		CHECK_FALSE(committed);
	}

	SUBCASE("both recordings commit exactly once and in order")
	{
		std::vector<std::string> events{};
		bool committed = false;
		CHECK(record(true, true, events, committed));
		CHECK((events ==
			std::vector<std::string>{ "clear", "casters", "commit" }));
		CHECK(committed);
	}
}
