#include "App/SandboxStandardScene.h"
#include "Function/Application.h"
#include "Function/Scene/Scene.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <GLFW/glfw3.h>
#include <json.hpp>
#include <string>

namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	constexpr const char* k_scene_path = "product/assets/scenes/VegetationBaseline.scene.json";

	auto read_json(const fs::path& path) -> json
	{
		std::ifstream input(path);
		REQUIRE_MESSAGE(input.is_open(), "Failed to open benchmark scene: ", path.generic_string());
		json value{};
		input >> value;
		return value;
	}

	auto resolve_asset_reference(const std::string& reference) -> fs::path
	{
		const fs::path path = fs::path(reference).lexically_normal();
		if (path.empty() || path.is_absolute())
		{
			return path;
		}
		const auto first_component = path.begin();
		if (first_component != path.end() && first_component->generic_string() == "product")
		{
			return path;
		}
		return (fs::path("product/assets") / path).lexically_normal();
	}

	auto contains_vegetation_data(const json& value) -> bool
	{
		if (value.is_object())
		{
			for (auto it = value.begin(); it != value.end(); ++it)
			{
				std::string key = it.key();
				std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
				if (key.find("vegetation") != std::string::npos || contains_vegetation_data(it.value()))
				{
					return true;
				}
			}
		}
		else if (value.is_array())
		{
			for (const json& element : value)
			{
				if (contains_vegetation_data(element))
				{
					return true;
				}
			}
		}
		else if (value.is_string())
		{
			std::string text = value.get<std::string>();
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return text.find("vegetation") != std::string::npos;
		}
		return false;
	}

	auto primary_camera_transform(const AshSandbox::SandboxStandardSceneSnapshot& snapshot)
		-> AshEngine::TransformComponent
	{
		const AshEngine::Entity camera = snapshot.scene.find_entity(snapshot.primary_camera_entity_id);
		REQUIRE(camera.is_valid());
		REQUIRE(camera.has_camera_component());
		return camera.get_transform_component();
	}
}

TEST_CASE("Vegetation baseline scene is an independent full-pipeline no-vegetation contract")
{
	const fs::path scene_path = k_scene_path;
	REQUIRE_MESSAGE(fs::exists(scene_path), "Missing fixed benchmark scene: ", scene_path.generic_string());

	const json source = read_json(scene_path);
	REQUIRE(source.is_object());
	CHECK(source.at("version") == 6u);
	CHECK_FALSE(contains_vegetation_data(source));
	REQUIRE(source.contains("scene_config"));
	const json& source_config = source.at("scene_config");
	CHECK(source_config.contains("ambient_occlusion"));
	CHECK(source_config.contains("directional_shadows"));
	CHECK(source_config.contains("bloom"));
	CHECK(source_config.contains("volumetric_lighting"));
	CHECK(source_config.contains("temporal_aa"));
	CHECK(source_config.contains("tonemap"));
	const std::string ao_mode = source_config.at("ambient_occlusion").at("mode").get<std::string>();
	const bool directional_shadows_enabled = source_config.at("directional_shadows").at("enabled").get<bool>();
	const bool bloom_enabled = source_config.at("bloom").at("enabled").get<bool>();
	const bool volumetric_lighting_enabled = source_config.at("volumetric_lighting").at("enabled").get<bool>();
	const bool temporal_aa_enabled = source_config.at("temporal_aa").at("enabled").get<bool>();
	const double tonemap_exposure = source_config.at("tonemap").at("exposure").get<double>();
	CHECK(ao_mode != "Off");
	CHECK(directional_shadows_enabled);
	CHECK(bloom_enabled);
	CHECK(volumetric_lighting_enabled);
	CHECK(temporal_aa_enabled);
	CHECK(tonemap_exposure > 0.0);

	std::string load_error{};
	const AshEngine::Scene scene = AshEngine::Scene::load_from_file(scene_path, &load_error);
	REQUIRE_MESSAGE(scene.is_valid(), load_error);
	CHECK(load_error.empty());

	size_t primary_camera_count = 0;
	for (const AshEngine::Entity& camera_entity :
		scene.get_entities_with_component(AshEngine::SceneComponentType::Camera))
	{
		if (!camera_entity.get_camera_component().primary)
		{
			continue;
		}
		++primary_camera_count;
		const AshEngine::TransformComponent transform = camera_entity.get_transform_component();
		CHECK(transform.position.x == doctest::Approx(0.0f));
		CHECK(transform.position.y == doctest::Approx(10.0f));
		CHECK(transform.position.z == doctest::Approx(-8.0f));
		CHECK(transform.rotation_euler_degrees.x == doctest::Approx(0.0f));
		CHECK(transform.rotation_euler_degrees.y == doctest::Approx(0.0f));
		CHECK(transform.rotation_euler_degrees.z == doctest::Approx(0.0f));
	}
	CHECK(primary_camera_count == 1u);

	const std::vector<AshEngine::SceneMeshExtractionDesc> meshes = scene.extract_mesh_entities();
	REQUIRE_FALSE(meshes.empty());
	CHECK(std::any_of(meshes.begin(), meshes.end(), [](const AshEngine::SceneMeshExtractionDesc& mesh)
	{
		return mesh.visible;
	}));
	for (const AshEngine::SceneMeshExtractionDesc& mesh : meshes)
	{
		const fs::path asset_path = resolve_asset_reference(mesh.asset_path);
		const bool mesh_asset_exists = !asset_path.empty() && fs::exists(asset_path);
		CHECK_MESSAGE(mesh_asset_exists, "Missing mesh asset: ", asset_path.generic_string());
		for (const AshEngine::MeshMaterialOverride& material : mesh.material_overrides)
		{
			const fs::path material_path = resolve_asset_reference(material.material_path);
			const bool material_asset_exists = !material_path.empty() && fs::exists(material_path);
			CHECK_MESSAGE(
				material_asset_exists,
				"Missing material asset: ",
				material_path.generic_string());
		}
	}

	AshEngine::SceneEnvironmentExtractionDesc environment{};
	REQUIRE(scene.extract_active_environment(environment));
	CHECK(environment.visible_background);
	CHECK(environment.affect_lighting);
	const fs::path ibl_path = resolve_asset_reference(environment.ibl_asset_path);
	const fs::path source_texture_path = resolve_asset_reference(environment.source_texture_path);
	const bool ibl_asset_exists = !ibl_path.empty() && fs::exists(ibl_path);
	const bool source_texture_exists = !source_texture_path.empty() && fs::exists(source_texture_path);
	CHECK_MESSAGE(ibl_asset_exists, "Missing IBL asset: ", ibl_path.generic_string());
	CHECK_MESSAGE(
		source_texture_exists,
		"Missing sky source asset: ",
		source_texture_path.generic_string());

	const std::vector<AshEngine::SceneLightExtractionDesc> lights = scene.extract_light_entities();
	CHECK(std::any_of(lights.begin(), lights.end(), [](const AshEngine::SceneLightExtractionDesc& light)
	{
		return light.light.type == AshEngine::LightType::Directional &&
			light.light.sunlight &&
			light.light.casts_shadow;
	}));

	const std::vector<AshEngine::SceneParticleExtractionDesc> particles = scene.extract_particle_entities();
	REQUIRE_FALSE(particles.empty());
	CHECK(std::any_of(particles.begin(), particles.end(), [](const AshEngine::SceneParticleExtractionDesc& particle)
	{
		return particle.particle.emitting &&
			particle.particle.max_particles > 0u &&
			particle.particle.spawn_rate > 0.0f;
	}));
	for (const AshEngine::SceneParticleExtractionDesc& particle : particles)
	{
		if (particle.particle.sprite_texture_path.empty())
		{
			continue;
		}
		const fs::path sprite_path = resolve_asset_reference(particle.particle.sprite_texture_path);
		CHECK_MESSAGE(fs::exists(sprite_path), "Missing particle sprite asset: ", sprite_path.generic_string());
	}

	const AshEngine::SceneRenderConfig& config = scene.get_render_config();
	CHECK(config.ambient_occlusion.mode != AshEngine::AmbientOcclusionMode::Off);
	CHECK(config.directional_shadows.enabled);
	CHECK(config.bloom.enabled);
	CHECK(config.volumetric_lighting.enabled);
	CHECK(config.temporal_aa.enabled);
	CHECK(config.tonemap.exposure > 0.0f);
}

TEST_CASE("Vegetation baseline scene freezes the free camera only in PerfGate mode")
{
	AshEngine::EngineInitConfig application_config{};
	AshEngine::Application application(application_config);
	application.set_scene_path_override(k_scene_path);
	application.set_frame_dump_path("Intermediate/test-reports/vegetation-fixed-camera.png");

	AshEngine::AssetDatabase asset_database = AshEngine::AssetDatabase::create("product/assets");
	REQUIRE(asset_database.is_valid());
	REQUIRE(asset_database.refresh());

	AshSandbox::SandboxStandardScene standard_scene{};
	REQUIRE(standard_scene.start(asset_database, true));
	const AshEngine::TransformComponent fixed_before =
		primary_camera_transform(standard_scene.snapshot());
	CHECK(fixed_before.position.x == doctest::Approx(0.0f));
	CHECK(fixed_before.position.y == doctest::Approx(10.0f));
	CHECK(fixed_before.position.z == doctest::Approx(-8.0f));

	AshEngine::InputState forward_input{};
	forward_input.set_key_state(GLFW_KEY_W, true, false);
	REQUIRE(standard_scene.update_logic(forward_input));
	const AshEngine::TransformComponent fixed_after =
		primary_camera_transform(standard_scene.snapshot());
	CHECK(fixed_after.position.x == doctest::Approx(fixed_before.position.x));
	CHECK(fixed_after.position.y == doctest::Approx(fixed_before.position.y));
	CHECK(fixed_after.position.z == doctest::Approx(fixed_before.position.z));
	CHECK(fixed_after.rotation_euler_degrees.x == doctest::Approx(fixed_before.rotation_euler_degrees.x));
	CHECK(fixed_after.rotation_euler_degrees.y == doctest::Approx(fixed_before.rotation_euler_degrees.y));
	CHECK(fixed_after.rotation_euler_degrees.z == doctest::Approx(fixed_before.rotation_euler_degrees.z));

	standard_scene.reset();
	application.set_frame_dump_path({});
	REQUIRE(standard_scene.start(asset_database, false));
	const AshEngine::TransformComponent interactive_before =
		primary_camera_transform(standard_scene.snapshot());
	REQUIRE(standard_scene.update_logic(forward_input));
	const AshEngine::TransformComponent interactive_after =
		primary_camera_transform(standard_scene.snapshot());
	CHECK(interactive_after.position.z > interactive_before.position.z);
}
