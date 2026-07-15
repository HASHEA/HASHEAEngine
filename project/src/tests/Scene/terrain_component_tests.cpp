#include "Function/Scene/Scene.h"
#include "Function/Scene/SceneQuery.h"

#include "Base/hthreading.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/TerrainSpatialData.h"
#include "Terrain/TerrainTestUtils.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <chrono>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <system_error>

#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	auto TestRoot(const char* name) -> fs::path
	{
		const fs::path root =
			fs::path("Intermediate/test-temp/tests/terrain-scene") / name;
		std::error_code error_code{};
		fs::remove_all(root, error_code);
		fs::create_directories(root / "terrain", error_code);
		return root;
	}

	void WriteJson(const fs::path& path, const json& value)
	{
		std::ofstream output(path);
		REQUIRE(output.is_open());
		output << value.dump(2);
		REQUIRE(output.good());
	}

	void WriteObj(const fs::path& path)
	{
		std::ofstream output(path);
		REQUIRE(output.is_open());
		output <<
			"v -3 -4 -5\n"
			"v -1 -4 -5\n"
			"v -3 -2 -1\n"
			"f 1 2 3\n";
		REQUIRE(output.good());
	}

	auto ReadJson(const fs::path& path) -> json
	{
		std::ifstream input(path);
		REQUIRE(input.is_open());
		json value{};
		input >> value;
		return value;
	}

	auto MakeTerrain(std::string path = "terrain/TerrainGate.AshTerrain")
		-> AshEngine::TerrainComponent
	{
		AshEngine::TerrainComponent terrain{};
		terrain.asset_path = std::move(path);
		terrain.material_layer_overrides[0] = "materials/Grass.AshMat";
		terrain.material_layer_overrides[7] = "materials/Rock.AshMat";
		return terrain;
	}

	auto MakeSlopedSnapshot(AshEngine::TerrainAssetId asset_id)
		-> std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			asset_id,
			TerrainTests::MakeSmallLayout(),
			{ 0.0f, 100.0f },
			2.0f,
			flat,
			&error));
		REQUIRE_MESSAGE(error.empty(), error);

		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
		auto base_heights = std::make_shared<std::vector<uint16_t>>(
			*flat->base_heights);
		for (uint32_t z = 0u; z < snapshot->layout.sample_count_z; ++z)
		{
			for (uint32_t x = 0u; x < snapshot->layout.sample_count_x; ++x)
			{
				(*base_heights)[static_cast<size_t>(z) * snapshot->layout.sample_count_x + x] =
					AshEngine::encode_terrain_height_r16(
						2.0f + static_cast<float>(x), snapshot->height_mapping);
			}
		}
		snapshot->base_heights = std::move(base_heights);

		for (uint32_t component_z = 0u;
			component_z < snapshot->layout.component_count_z;
			++component_z)
		{
			for (uint32_t component_x = 0u;
				component_x < snapshot->layout.component_count_x;
				++component_x)
			{
				const AshEngine::TerrainComponentCoord coord{
					static_cast<uint16_t>(component_x),
					static_cast<uint16_t>(component_z)
				};
				const size_t index =
					static_cast<size_t>(component_z) * snapshot->layout.component_count_x +
					component_x;
				auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
					*snapshot->components[index]);
				const AshEngine::TerrainSampleRect rect =
					AshEngine::get_terrain_component_snapshot_rect(snapshot->layout, coord);
				component->heights.clear();
				for (uint32_t sample_z = rect.min_z;
					sample_z < rect.max_z_exclusive;
					++sample_z)
				{
					for (uint32_t sample_x = rect.min_x;
						sample_x < rect.max_x_exclusive;
						++sample_x)
					{
						component->heights.push_back(
							2.0f + static_cast<float>(sample_x));
					}
				}
				REQUIRE(AshEngine::build_terrain_component_spatial_data(
					*component, rect.width(), rect.height()));
				snapshot->components[index] = std::move(component);
			}
		}
		return snapshot;
	}

	void SaveSnapshot(
		const fs::path& path,
		const AshEngine::TerrainAssetSnapshot& snapshot)
	{
		std::string error{};
		REQUIRE(AshEngine::save_terrain_container_incremental(
			path, snapshot, {}, nullptr, &error) ==
			AshEngine::TerrainContainerResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
	}

	auto MakeBounds(
		const glm::vec3& minimum,
		const glm::vec3& maximum) -> AshEngine::SceneWorldBounds
	{
		AshEngine::SceneWorldBounds bounds{};
		bounds.is_valid = true;
		bounds.min = minimum;
		bounds.max = maximum;
		bounds.center = (minimum + maximum) * 0.5f;
		bounds.extents = (maximum - minimum) * 0.5f;
		return bounds;
	}

	auto MaximumViewDepth(
		const AshEngine::SceneWorldBounds& bounds,
		const glm::mat4& view) -> float
	{
		const std::array<glm::vec3, 8> corners = {
			glm::vec3(bounds.min.x, bounds.min.y, bounds.min.z),
			glm::vec3(bounds.max.x, bounds.min.y, bounds.min.z),
			glm::vec3(bounds.min.x, bounds.max.y, bounds.min.z),
			glm::vec3(bounds.max.x, bounds.max.y, bounds.min.z),
			glm::vec3(bounds.min.x, bounds.min.y, bounds.max.z),
			glm::vec3(bounds.max.x, bounds.min.y, bounds.max.z),
			glm::vec3(bounds.min.x, bounds.max.y, bounds.max.z),
			glm::vec3(bounds.max.x, bounds.max.y, bounds.max.z),
		};

		float maximum = -std::numeric_limits<float>::infinity();
		for (const glm::vec3& corner : corners)
		{
			maximum = std::max(maximum, (view * glm::vec4(corner, 1.0f)).z);
		}
		return maximum;
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
		std::shared_future<void> release_future = release_promise.get_future().share();
		AshEngine::ThreadCommandFuture blocker_future{};
		bool released = false;

		WorkerBlocker()
		{
			std::promise<void> started_promise{};
			auto started_future = started_promise.get_future();
			blocker_future = AshEngine::dispatch_background_task(
				"TerrainSceneTests::WorkerBlocker",
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

TEST_CASE("TerrainComponent descriptor and facade expose the v6 contract")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Terrain Component Test");
	AshEngine::Entity entity = scene.create_entity("Terrain");
	REQUIRE(entity.is_valid());

	// The generic facade has no asset-path parameter, so it must not create the
	// invalid empty default TerrainComponent. Creation uses the typed facade.
	CHECK_FALSE(AshEngine::can_add_scene_component(
		entity, AshEngine::SceneComponentType::Terrain));
	CHECK_FALSE(AshEngine::add_scene_component(
		entity, AshEngine::SceneComponentType::Terrain));
	CHECK(entity.add_terrain_component(MakeTerrain()));
	CHECK(entity.has_terrain_component());
	CHECK(entity.get_terrain_component().asset_path ==
		"terrain/TerrainGate.AshTerrain");
	CHECK(scene.get_render_terrain_version() > 0u);

	const AshEngine::SceneComponentDesc* descriptor =
		AshEngine::get_scene_component_descriptor(
			AshEngine::SceneComponentType::Terrain);
	REQUIRE(descriptor != nullptr);
	CHECK(std::string(descriptor->name) == "TerrainComponent");
	CHECK(descriptor->byte_size == sizeof(AshEngine::TerrainComponent));

	AshEngine::TerrainComponent generic_read{};
	REQUIRE(entity.read_component(
		AshEngine::SceneComponentType::Terrain,
		&generic_read,
		sizeof(generic_read)));
	CHECK(generic_read.asset_path == "terrain/TerrainGate.AshTerrain");

	AshEngine::TerrainComponent generic_write = generic_read;
	generic_write.visible = false;
	REQUIRE(entity.write_component(
		AshEngine::SceneComponentType::Terrain,
		&generic_write,
		sizeof(generic_write)));
	CHECK_FALSE(entity.get_terrain_component().visible);
	CHECK(AshEngine::can_remove_scene_component(
		entity, AshEngine::SceneComponentType::Terrain));
	const uint64_t version_before_remove = scene.get_render_terrain_version();
	CHECK(AshEngine::remove_scene_component(
		entity, AshEngine::SceneComponentType::Terrain));
	CHECK_FALSE(entity.has_terrain_component());
	CHECK(scene.get_render_terrain_version() > version_before_remove);
}

TEST_CASE("TerrainComponent rejects invalid state atomically and keeps revisions independent")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Terrain Validation");
	AshEngine::Entity entity = scene.create_entity("Terrain");
	REQUIRE(entity.is_valid());
	CHECK_FALSE(entity.add_terrain_component({}));
	CHECK_FALSE(entity.has_terrain_component());

	REQUIRE(entity.add_terrain_component(MakeTerrain()));
	const uint64_t primitive_version = scene.get_render_primitive_version();
	const uint64_t transform_version = scene.get_render_transform_version();
	const uint64_t terrain_version = scene.get_render_terrain_version();
	const AshEngine::TerrainComponent before = entity.get_terrain_component();

	AshEngine::TerrainComponent invalid = before;
	invalid.asset_path.clear();
	CHECK_FALSE(entity.set_terrain_component(invalid));
	CHECK(entity.get_terrain_component().asset_path == before.asset_path);
	CHECK(scene.get_render_terrain_version() == terrain_version);

	AshEngine::TransformComponent valid_transform = entity.get_transform_component();
	valid_transform.position = { 10.0f, 20.0f, 30.0f };
	valid_transform.scale = { 2.0f, 3.0f, 2.0f };
	REQUIRE(entity.set_transform_component(valid_transform));
	CHECK(scene.get_render_transform_version() > transform_version);
	CHECK(scene.get_render_terrain_version() == terrain_version);
	CHECK(scene.get_render_primitive_version() == primitive_version);

	const uint64_t valid_transform_version = scene.get_render_transform_version();
	for (const AshEngine::TransformComponent invalid_transform : {
		AshEngine::TransformComponent{
			valid_transform.position, { 0.0f, 1.0f, 0.0f }, valid_transform.scale },
		AshEngine::TransformComponent{
			valid_transform.position, {}, { 0.0f, 3.0f, 2.0f } },
		AshEngine::TransformComponent{
			valid_transform.position, {}, { -2.0f, 3.0f, 2.0f } },
		AshEngine::TransformComponent{
			valid_transform.position,
			{},
			{ std::numeric_limits<float>::infinity(), 3.0f, 2.0f } },
	})
	{
		CHECK_FALSE(entity.set_transform_component(invalid_transform));
		CHECK(entity.get_transform_component().position == valid_transform.position);
		CHECK(entity.get_transform_component().scale == valid_transform.scale);
		CHECK(scene.get_render_transform_version() == valid_transform_version);
		CHECK(scene.get_render_terrain_version() == terrain_version);
	}

	AshEngine::TerrainComponent changed = before;
	changed.receives_shadow = false;
	REQUIRE(entity.set_terrain_component(changed));
	CHECK(scene.get_render_terrain_version() > terrain_version);
	CHECK(scene.get_render_primitive_version() == primitive_version);
	CHECK(scene.get_render_transform_version() == valid_transform_version);

	AshEngine::Entity parent = scene.create_entity("Terrain Parent");
	REQUIRE(parent.is_valid());
	REQUIRE(entity.set_parent(parent));
	AshEngine::TransformComponent invalid_parent = parent.get_transform_component();
	invalid_parent.rotation_euler_degrees.y = 15.0f;
	CHECK_FALSE(parent.set_transform_component(invalid_parent));
	CHECK(parent.get_transform_component().rotation_euler_degrees == glm::vec3(0.0f));

	AshEngine::Entity rotated_parent = scene.create_entity("Rotated Parent");
	REQUIRE(rotated_parent.set_transform_component(invalid_parent));
	CHECK_FALSE(entity.set_parent(rotated_parent));
	CHECK(entity.get_parent().get_id() == parent.get_id());
	AshEngine::Entity child = rotated_parent.create_child("Invalid Terrain Child");
	CHECK_FALSE(child.add_terrain_component(MakeTerrain("terrain/Other.AshTerrain")));

	const std::vector<AshEngine::SceneTerrainExtractionDesc> extracted =
		scene.extract_terrain_entities();
	REQUIRE(extracted.size() == 1u);
	CHECK(extracted[0].entity_id == entity.get_id());
	CHECK(extracted[0].terrain.asset_path == changed.asset_path);
	CHECK(extracted[0].world_transform[3].x == doctest::Approx(10.0f));
	const uint64_t version_before_destroy = scene.get_render_terrain_version();
	REQUIRE(parent.destroy());
	CHECK(scene.get_render_terrain_version() > version_before_destroy);
	CHECK(scene.extract_terrain_entities().empty());
}

TEST_CASE("Scene v6 preserves Terrain and v5 scenes migrate without Terrain")
{
	const fs::path root = TestRoot("schema-v6");
	const fs::path path = root / "terrain_component_roundtrip.scene.json";
	AshEngine::Scene source = AshEngine::Scene::create("Terrain Save");
	AshEngine::Entity entity = source.create_entity("Terrain");
	AshEngine::TransformComponent transform{};
	transform.position = { 10.0f, 20.0f, 30.0f };
	transform.scale = { 2.0f, 3.0f, 2.0f };
	REQUIRE(entity.set_transform_component(transform));
	REQUIRE(entity.add_terrain_component(MakeTerrain()));

	std::string error{};
	REQUIRE(source.save_to_file(path, &error));
	REQUIRE_MESSAGE(error.empty(), error);
	json saved = ReadJson(path);
	CHECK(saved.at("version") == 6u);
	REQUIRE(saved.at("entities").size() == 1u);
	const json& terrain_json = saved.at("entities")[0].at("terrain");
	CHECK(terrain_json.at("asset_path") == "terrain/TerrainGate.AshTerrain");
	CHECK(terrain_json.at("material_layer_overrides").size() == 8u);
	CHECK(terrain_json.at("material_layer_overrides")[7] == "materials/Rock.AshMat");

	AshEngine::Scene loaded = AshEngine::Scene::load_from_file(path, &error);
	REQUIRE_MESSAGE(loaded.is_valid(), error);
	const auto loaded_terrain = loaded.get_entities_with_component(
		AshEngine::SceneComponentType::Terrain);
	REQUIRE(loaded_terrain.size() == 1u);
	CHECK(loaded_terrain[0].get_terrain_component().material_layer_overrides[0] ==
		"materials/Grass.AshMat");

	json legacy = saved;
	legacy["version"] = 5u;
	legacy["entities"][0].erase("terrain");
	WriteJson(path, legacy);
	AshEngine::Scene legacy_loaded = AshEngine::Scene::load_from_file(path, &error);
	REQUIRE_MESSAGE(legacy_loaded.is_valid(), error);
	CHECK(legacy_loaded.get_entities_with_component(
		AshEngine::SceneComponentType::Terrain).empty());

	json invalid = saved;
	invalid["entities"][0]["terrain"]["asset_path"] = "";
	WriteJson(path, invalid);
	CHECK_FALSE(AshEngine::Scene::load_from_file(path, &error).is_valid());
	CHECK_FALSE(error.empty());

	invalid = saved;
	invalid["entities"][0]["transform"]["rotation_euler_degrees"] =
		json::array({ 0.0f, 10.0f, 0.0f });
	WriteJson(path, invalid);
	CHECK_FALSE(AshEngine::Scene::load_from_file(path, &error).is_valid());
	CHECK_FALSE(error.empty());

	std::error_code filesystem_error{};
	fs::remove_all(root, filesystem_error);
}

TEST_CASE("Terrain world query applies entity translation and positive scale")
{
	const fs::path root = TestRoot("world-query-ready");
	const fs::path relative_path = "terrain/sloped.AshTerrain";
	SaveSnapshot(root / relative_path, *MakeSlopedSnapshot(71u));
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded_snapshot{};
	REQUIRE(database.load_terrain_by_path(relative_path, loaded_snapshot));

	AshEngine::Scene scene = AshEngine::Scene::create("Terrain World Query");
	AshEngine::Entity entity = scene.create_entity("Terrain");
	AshEngine::TransformComponent transform{};
	transform.position = { 10.0f, 20.0f, 30.0f };
	transform.scale = { 2.0f, 3.0f, 2.0f };
	REQUIRE(entity.set_transform_component(transform));
	REQUIRE(entity.add_terrain_component(MakeTerrain(relative_path.generic_string())));

	const glm::vec3 query_position{ 14.0f, -100.0f, 34.0f };
	float world_height = -999.0f;
	CHECK(AshEngine::query_height(
		scene, database, entity.get_id(), query_position, world_height) ==
		AshEngine::TerrainQueryStatus::Ready);
	CHECK(world_height == doctest::Approx(32.0f));

	glm::vec3 world_normal{ 99.0f };
	CHECK(AshEngine::query_normal(
		scene, database, entity.get_id(), query_position, world_normal) ==
		AshEngine::TerrainQueryStatus::Ready);
	const glm::vec3 expected_normal =
		glm::normalize(glm::vec3{ -0.5f, 1.0f / 3.0f, 0.0f });
	CHECK(world_normal.x == doctest::Approx(expected_normal.x));
	CHECK(world_normal.y == doctest::Approx(expected_normal.y));
	CHECK(world_normal.z == doctest::Approx(expected_normal.z));

	AshEngine::EntityId hit_entity = 0u;
	AshEngine::TerrainRayHit hit{};
	hit.distance = -1.0f;
	CHECK(AshEngine::ray_cast_terrain(
		scene,
		database,
		{ { 14.0f, 100.0f, 34.0f }, { 0.0f, -1.0f, 0.0f } },
		100.0f,
		hit_entity,
		hit) == AshEngine::TerrainQueryStatus::Ready);
	CHECK(hit_entity == entity.get_id());
	CHECK(hit.distance == doctest::Approx(68.0f));
	CHECK(hit.position.x == doctest::Approx(14.0f));
	CHECK(hit.position.y == doctest::Approx(32.0f));
	CHECK(hit.position.z == doctest::Approx(34.0f));

	world_height = 77.0f;
	CHECK(AshEngine::query_height(
		scene,
		database,
		entity.get_id(),
		{ 9.0f, 0.0f, 34.0f },
		world_height) == AshEngine::TerrainQueryStatus::Outside);
	CHECK(world_height == 77.0f);

	transform.scale.y = std::numeric_limits<float>::max();
	REQUIRE(entity.set_transform_component(transform));
	world_height = 91.0f;
	CHECK(AshEngine::query_height(
		scene,
		database,
		entity.get_id(),
		query_position,
		world_height) == AshEngine::TerrainQueryStatus::Failed);
	CHECK(world_height == 91.0f);

	std::error_code filesystem_error{};
	fs::remove_all(root, filesystem_error);
}

TEST_CASE("Terrain world query preserves outputs while asset loading is Pending")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig config{};
	config.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(config));
	ThreadingScope threading_scope{};
	WorkerBlocker blocker{};

	const fs::path root = TestRoot("world-query-pending");
	const fs::path relative_path = "terrain/pending.AshTerrain";
	SaveSnapshot(root / relative_path, *MakeSlopedSnapshot(72u));
	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	AshEngine::Scene scene = AshEngine::Scene::create("Terrain Pending Query");
	AshEngine::Entity entity = scene.create_entity("Terrain");
	REQUIRE(entity.add_terrain_component(MakeTerrain(relative_path.generic_string())));

	float height = 77.0f;
	CHECK(AshEngine::query_height(
		scene, database, entity.get_id(), { 2.0f, 0.0f, 2.0f }, height) ==
		AshEngine::TerrainQueryStatus::Pending);
	CHECK(height == 77.0f);

	AshEngine::EntityId hit_entity = 91u;
	AshEngine::TerrainRayHit hit{};
	hit.distance = 123.0f;
	CHECK(AshEngine::ray_cast_terrain(
		scene,
		database,
		{ { 2.0f, 50.0f, 2.0f }, { 0.0f, -1.0f, 0.0f } },
		100.0f,
		hit_entity,
		hit) == AshEngine::TerrainQueryStatus::Pending);
	CHECK(hit_entity == 91u);
	CHECK(hit.distance == 123.0f);

	blocker.release();
	REQUIRE(database.load_terrain_by_path_async(relative_path).get() != nullptr);
	std::error_code filesystem_error{};
	fs::remove_all(root, filesystem_error);
}

TEST_CASE("Terrain world bounds use resident roots and merge with Mesh subtrees")
{
	const fs::path root = TestRoot("world-bounds-resident");
	const fs::path terrain_path = "terrain/sloped-bounds.AshTerrain";
	const fs::path mesh_path = "bounds.obj";
	SaveSnapshot(root / terrain_path, *MakeSlopedSnapshot(81u));
	WriteObj(root / mesh_path);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded_snapshot{};
	REQUIRE(database.load_terrain_by_path(terrain_path, loaded_snapshot));

	AshEngine::Scene scene = AshEngine::Scene::create("Terrain Bounds");
	AshEngine::Entity parent = scene.create_entity("Scaled Parent");
	AshEngine::TransformComponent parent_transform{};
	parent_transform.position = { 10.0f, 20.0f, 30.0f };
	parent_transform.scale = { 2.0f, 3.0f, 4.0f };
	REQUIRE(parent.set_transform_component(parent_transform));

	AshEngine::Entity terrain = parent.create_child("Terrain");
	AshEngine::TransformComponent terrain_transform{};
	terrain_transform.position = { 1.0f, 2.0f, 3.0f };
	terrain_transform.scale = { 0.5f, 2.0f, 0.25f };
	REQUIRE(terrain.set_transform_component(terrain_transform));
	REQUIRE(terrain.add_terrain_component(MakeTerrain(terrain_path.generic_string())));

	AshEngine::Entity mesh = parent.create_child("Mesh");
	AshEngine::MeshComponent mesh_component{};
	mesh_component.asset_path = mesh_path.generic_string();
	REQUIRE(mesh.add_mesh_component(mesh_component));

	AshEngine::SceneWorldBounds terrain_bounds{};
	REQUIRE(AshEngine::get_entity_world_bounds(
		scene, database, terrain.get_id(), terrain_bounds));
	CHECK(terrain_bounds.min.x == doctest::Approx(12.0f));
	CHECK(terrain_bounds.min.y == doctest::Approx(38.0f));
	CHECK(terrain_bounds.min.z == doctest::Approx(42.0f));
	CHECK(terrain_bounds.max.x == doctest::Approx(20.0f));
	CHECK(terrain_bounds.max.y == doctest::Approx(86.0f));
	CHECK(terrain_bounds.max.z == doctest::Approx(50.0f));

	AshEngine::SceneWorldBounds subtree_bounds{};
	REQUIRE(AshEngine::get_entity_subtree_world_bounds(
		scene, database, parent.get_id(), subtree_bounds));
	CHECK(subtree_bounds.min.x == doctest::Approx(4.0f));
	CHECK(subtree_bounds.min.y == doctest::Approx(8.0f));
	CHECK(subtree_bounds.min.z == doctest::Approx(10.0f));
	CHECK(subtree_bounds.max.x == doctest::Approx(20.0f));
	CHECK(subtree_bounds.max.y == doctest::Approx(86.0f));
	CHECK(subtree_bounds.max.z == doctest::Approx(50.0f));

	std::error_code filesystem_error{};
	fs::remove_all(root, filesystem_error);
}

TEST_CASE("Terrain world bounds conservatively include edits when residency is incomplete")
{
	const fs::path root = TestRoot("world-bounds-incomplete");
	const fs::path terrain_path = "terrain/incomplete-bounds.AshTerrain";
	AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();
	layout.sample_spacing_meters = 2.5f;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		82u, layout, { 0.0f, 100.0f }, 50.0f, flat, &error));
	REQUIRE_MESSAGE(error.empty(), error);
	SaveSnapshot(root / terrain_path, *flat);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded_snapshot{};
	REQUIRE(database.load_terrain_by_path(terrain_path, loaded_snapshot));
	REQUIRE(loaded_snapshot != nullptr);

	auto incomplete = std::make_shared<AshEngine::TerrainAssetSnapshot>(*loaded_snapshot);
	REQUIRE_FALSE(incomplete->components.empty());
	incomplete->components.front().reset();
	incomplete->residency_revision = loaded_snapshot->residency_revision + 1u;

	AshEngine::TerrainEditLayer additive{};
	additive.id.bytes[0] = 1u;
	additive.name = "Additive high range";
	additive.height_blocks.push_back({ { 0u, 0u }, { 0u, 0u, 1u, 1u }, { 1.0f }, { 250.0f } });
	AshEngine::TerrainEditLayer alpha{};
	alpha.id.bytes[0] = 2u;
	alpha.name = "Alpha low range";
	alpha.height_blocks.push_back({ { 0u, 0u }, { 0u, 0u, 1u, 1u }, { 0.0f }, { -80.0f } });
	incomplete->edit_layers =
		std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ additive, alpha });
	REQUIRE(database.publish_terrain_snapshot(incomplete->asset_id, incomplete));

	AshEngine::Scene scene = AshEngine::Scene::create("Incomplete Terrain Bounds");
	AshEngine::Entity terrain = scene.create_entity("Terrain");
	AshEngine::TransformComponent transform{};
	transform.position.y = 5.0f;
	transform.scale = { 1.0f, 2.0f, 1.0f };
	REQUIRE(terrain.set_transform_component(transform));
	REQUIRE(terrain.add_terrain_component(MakeTerrain(terrain_path.generic_string())));

	AshEngine::SceneWorldBounds bounds{};
	REQUIRE(AshEngine::get_entity_world_bounds(
		scene, database, terrain.get_id(), bounds));
	CHECK(bounds.min.x == doctest::Approx(0.0f));
	CHECK(bounds.max.x == doctest::Approx(20.0f));
	CHECK(bounds.min.z == doctest::Approx(0.0f));
	CHECK(bounds.max.z == doctest::Approx(20.0f));
	CHECK(bounds.min.y == doctest::Approx(-155.0f));
	CHECK(bounds.max.y == doctest::Approx(705.0f));

	std::error_code filesystem_error{};
	fs::remove_all(root, filesystem_error);
}

TEST_CASE("ray_cast_scene keeps mixed Mesh Terrain entities mesh-only")
{
	const fs::path root = TestRoot("mixed-entity-mesh-ray");
	const fs::path terrain_path = "terrain/mixed-ray.AshTerrain";
	const fs::path mesh_path = "mixed-ray.obj";
	SaveSnapshot(root / terrain_path, *MakeSlopedSnapshot(83u));
	WriteObj(root / mesh_path);

	AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root);
	REQUIRE(database.is_valid());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded_snapshot{};
	REQUIRE(database.load_terrain_by_path(terrain_path, loaded_snapshot));

	AshEngine::Scene scene = AshEngine::Scene::create("Mixed Entity Ray");
	AshEngine::Entity entity = scene.create_entity("Mesh and Terrain");
	AshEngine::MeshComponent mesh_component{};
	mesh_component.asset_path = mesh_path.generic_string();
	REQUIRE(entity.add_mesh_component(mesh_component));
	REQUIRE(entity.add_terrain_component(MakeTerrain(terrain_path.generic_string())));

	AshEngine::SceneWorldBounds union_bounds{};
	REQUIRE(AshEngine::get_entity_world_bounds(
		scene, database, entity.get_id(), union_bounds));
	CHECK(union_bounds.max.x == doctest::Approx(8.0f));
	CHECK(union_bounds.max.z == doctest::Approx(8.0f));

	const std::vector<AshEngine::SceneRayHit> hits = AshEngine::ray_cast_scene(
		scene,
		database,
		{ { 4.0f, 100.0f, 4.0f }, { 0.0f, -1.0f, 0.0f } },
		200.0f);
	CHECK(hits.empty());

	std::error_code filesystem_error{};
	fs::remove_all(root, filesystem_error);
}

TEST_CASE("Camera clip range covers scene bounds through focus navigation and scale")
{
	// A positive 2x scale of an 8192-unit Terrain must not be truncated by the
	// editor's former fixed 20 km orbit-distance ceiling.
	const AshEngine::SceneWorldBounds doubled_terrain_bounds = MakeBounds(
		{ 0.0f, -100.0f, 0.0f },
		{ 16384.0f, 100.0f, 16384.0f });
	float doubled_terrain_framing_distance = -1.0f;
	REQUIRE(AshEngine::compute_camera_bounds_framing_distance(
		doubled_terrain_bounds,
		60.0f,
		1.15f,
		doubled_terrain_framing_distance));
	const double doubled_terrain_radius = std::sqrt(
		8192.0 * 8192.0 + 100.0 * 100.0 + 8192.0 * 8192.0);
	const double minimum_enclosing_distance =
		doubled_terrain_radius / std::sin(std::acos(-1.0) / 6.0) * 1.15;
	CHECK(doubled_terrain_framing_distance == doctest::Approx(
		static_cast<float>(minimum_enclosing_distance)).epsilon(0.00001));

	float doubled_terrain_navigation_limit = -1.0f;
	REQUIRE(AshEngine::compute_camera_bounds_framing_distance(
		doubled_terrain_bounds,
		60.0f,
		1.15f * 2.0f,
		doubled_terrain_navigation_limit));
	CHECK(doubled_terrain_navigation_limit >=
		doubled_terrain_framing_distance * 1.9999f);

	float preserved_framing_distance = 123.0f;
	CHECK_FALSE(AshEngine::compute_camera_bounds_framing_distance(
		{}, 60.0f, 1.15f, preserved_framing_distance));
	CHECK(preserved_framing_distance == 123.0f);
	CHECK_FALSE(AshEngine::compute_camera_bounds_framing_distance(
		doubled_terrain_bounds,
		std::numeric_limits<float>::quiet_NaN(),
		1.15f,
		preserved_framing_distance));
	CHECK(preserved_framing_distance == 123.0f);
	CHECK_FALSE(AshEngine::compute_camera_bounds_framing_distance(
		doubled_terrain_bounds, 180.0f, 1.15f, preserved_framing_distance));
	CHECK(preserved_framing_distance == 123.0f);
	CHECK_FALSE(AshEngine::compute_camera_bounds_framing_distance(
		doubled_terrain_bounds,
		60.0f,
		std::numeric_limits<float>::max(),
		preserved_framing_distance));
	CHECK(preserved_framing_distance == 123.0f);

	const AshEngine::SceneWorldBounds terrain_bounds = MakeBounds(
		{ -32.0f, -10.0f, 100.0f },
		{ 8192.0f, 250.0f, 8292.0f });
	AshEngine::SceneCameraClipRange initial_clip{};
	REQUIRE(AshEngine::compute_camera_clip_range(
		terrain_bounds, glm::mat4(1.0f), 0.03f, initial_clip));
	CHECK(initial_clip.near_plane >= 0.03f);
	CHECK(initial_clip.near_plane < 100.0f);
	CHECK(initial_clip.far_plane > MaximumViewDepth(terrain_bounds, glm::mat4(1.0f)));

	const glm::mat4 dolly_view = glm::translate(
		glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2000.0f));
	AshEngine::SceneCameraClipRange dolly_clip{};
	REQUIRE(AshEngine::compute_camera_clip_range(
		terrain_bounds, dolly_view, 0.03f, dolly_clip));
	CHECK(dolly_clip.near_plane > initial_clip.near_plane);
	CHECK(dolly_clip.far_plane > MaximumViewDepth(terrain_bounds, dolly_view));
	CHECK(dolly_clip.far_plane > initial_clip.far_plane);

	const glm::vec3 center = terrain_bounds.center;
	const glm::mat4 orbit_view = glm::lookAtLH(
		center + glm::vec3(-9000.0f, 5000.0f, -11000.0f),
		center,
		glm::vec3(0.0f, 1.0f, 0.0f));
	AshEngine::SceneCameraClipRange orbit_clip{};
	REQUIRE(AshEngine::compute_camera_clip_range(
		terrain_bounds, orbit_view, 0.03f, orbit_clip));
	CHECK(orbit_clip.far_plane > MaximumViewDepth(terrain_bounds, orbit_view));

	AshEngine::SceneWorldBounds scaled_bounds = terrain_bounds;
	scaled_bounds.max.z = 16484.0f;
	scaled_bounds.center = (scaled_bounds.min + scaled_bounds.max) * 0.5f;
	scaled_bounds.extents = (scaled_bounds.max - scaled_bounds.min) * 0.5f;
	AshEngine::SceneCameraClipRange scaled_clip{};
	REQUIRE(AshEngine::compute_camera_clip_range(
		scaled_bounds, glm::mat4(1.0f), 0.03f, scaled_clip));
	CHECK(scaled_clip.far_plane > MaximumViewDepth(scaled_bounds, glm::mat4(1.0f)));
	CHECK(scaled_clip.far_plane > initial_clip.far_plane);

	const AshEngine::SceneWorldBounds camera_inside_bounds = MakeBounds(
		{ -10.0f, -10.0f, -10.0f },
		{ 10.0f, 10.0f, 10.0f });
	AshEngine::SceneCameraClipRange inside_clip{};
	REQUIRE(AshEngine::compute_camera_clip_range(
		camera_inside_bounds, glm::mat4(1.0f), 0.03f, inside_clip));
	CHECK(inside_clip.near_plane == doctest::Approx(0.03f));
	CHECK(inside_clip.far_plane > 10.0f);

	// Focusing a small foreground Mesh must not replace the full-scene clip target
	// and cut a large Terrain that remains visible behind it.
	const AshEngine::SceneWorldBounds scene_after_small_focus = MakeBounds(
		{ -1.0f, -1.0f, 4.0f },
		{ 8192.0f, 250.0f, 8192.0f });
	const glm::mat4 small_focus_view = glm::translate(
		glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f));
	AshEngine::SceneCameraClipRange small_focus_clip{};
	REQUIRE(AshEngine::compute_camera_clip_range(
		scene_after_small_focus, small_focus_view, 0.03f, small_focus_clip));
	CHECK(small_focus_clip.far_plane >
		MaximumViewDepth(scene_after_small_focus, small_focus_view));

	AshEngine::SceneCameraClipRange preserved{ 7.0f, 11.0f };
	CHECK_FALSE(AshEngine::compute_camera_clip_range(
		{}, glm::mat4(1.0f), 0.03f, preserved));
	CHECK(preserved.near_plane == 7.0f);
	CHECK(preserved.far_plane == 11.0f);
}

TEST_CASE("Scene world bounds merge keeps incomplete camera coverage conservative")
{
	AshEngine::SceneWorldBounds accumulated = MakeBounds(
		{ -100.0f, -20.0f, -40.0f },
		{ 50.0f, 80.0f, 120.0f });
	const AshEngine::SceneWorldBounds partial = MakeBounds(
		{ -10.0f, -200.0f, 30.0f },
		{ 400.0f, 60.0f, 500.0f });

	REQUIRE(AshEngine::merge_scene_world_bounds(accumulated, partial));
	CHECK(accumulated.min.x == doctest::Approx(-100.0f));
	CHECK(accumulated.min.y == doctest::Approx(-200.0f));
	CHECK(accumulated.min.z == doctest::Approx(-40.0f));
	CHECK(accumulated.max.x == doctest::Approx(400.0f));
	CHECK(accumulated.max.y == doctest::Approx(80.0f));
	CHECK(accumulated.max.z == doctest::Approx(500.0f));
	CHECK(accumulated.center.x == doctest::Approx(150.0f));
	CHECK(accumulated.center.y == doctest::Approx(-60.0f));
	CHECK(accumulated.center.z == doctest::Approx(230.0f));

	const AshEngine::SceneWorldBounds preserved = accumulated;
	CHECK(AshEngine::merge_scene_world_bounds(accumulated, {}));
	CHECK(accumulated.min == preserved.min);
	CHECK(accumulated.max == preserved.max);

	AshEngine::SceneWorldBounds initially_empty{};
	REQUIRE(AshEngine::merge_scene_world_bounds(initially_empty, partial));
	CHECK(initially_empty.min == partial.min);
	CHECK(initially_empty.max == partial.max);

	AshEngine::SceneWorldBounds still_empty{};
	CHECK_FALSE(AshEngine::merge_scene_world_bounds(still_empty, {}));
	CHECK_FALSE(still_empty.is_valid);
}
