#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/SunLightShadowPass.h"
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
	CHECK(AshEngine::compute_static_shadow_caster_revision(changed) != enabled);
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
