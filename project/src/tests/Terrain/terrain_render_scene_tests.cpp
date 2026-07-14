#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/TerrainRenderProxy.h"
#include "Function/Scene/Scene.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>

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
		scene, render_asset_manager));
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
		scene, render_asset_manager));
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
		"if (scene_state->last_terrain_version != scene_terrain_version)");
	const size_t transform_update_branch = source.find(
		"if (scene_state->last_transform_version != scene_transform_version)");
	REQUIRE(terrain_rebuild_branch != std::string::npos);
	REQUIRE(transform_update_branch != std::string::npos);
	CHECK(terrain_rebuild_branch < transform_update_branch);
}
