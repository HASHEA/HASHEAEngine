#include "Function/Scene/Scene.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <json.hpp>
#include <string>

namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	const AshEngine::ScenePropertyDesc* FindProperty(
		const AshEngine::SceneComponentDesc& descriptor,
		const std::string& name)
	{
		for (uint32_t property_index = 0; property_index < descriptor.property_count; ++property_index)
		{
			const AshEngine::ScenePropertyDesc& property = descriptor.properties[property_index];
			if (property.name && name == property.name)
			{
				return &property;
			}
		}
		return nullptr;
	}

	void CheckPropertyRange(
		const AshEngine::SceneComponentDesc& descriptor,
		const std::string& name,
		float minimum,
		float maximum)
	{
		const AshEngine::ScenePropertyDesc* property = FindProperty(descriptor, name);
		REQUIRE(property != nullptr);
		CHECK(property->use_range);
		CHECK(property->range_min == doctest::Approx(minimum));
		CHECK(property->range_max == doctest::Approx(maximum));
	}

	void CheckPropertyText(
		const AshEngine::SceneComponentDesc& descriptor,
		const std::string& name,
		const std::string& display_name,
		const std::string& tooltip)
	{
		const AshEngine::ScenePropertyDesc* property = FindProperty(descriptor, name);
		REQUIRE(property != nullptr);
		REQUIRE(property->display_name != nullptr);
		REQUIRE(property->tooltip != nullptr);
		CHECK(std::string(property->display_name) == display_name);
		CHECK(std::string(property->tooltip) == tooltip);
	}

	void WriteJson(const fs::path& path, const json& value)
	{
		std::ofstream output(path);
		REQUIRE(output.is_open());
		output << value.dump(2);
		REQUIRE(output.good());
	}

	json ReadJson(const fs::path& path)
	{
		std::ifstream input(path);
		REQUIRE(input.is_open());
		json value{};
		input >> value;
		return value;
	}

	void CheckParticleComponentMatches(
		const AshEngine::ParticleComponent& actual,
		const AshEngine::ParticleComponent& expected)
	{
		CHECK(actual.max_particles == expected.max_particles);
		CHECK(actual.spawn_rate == doctest::Approx(expected.spawn_rate));
		CHECK(actual.lifetime == doctest::Approx(expected.lifetime));
		CHECK(actual.lifetime_variance == doctest::Approx(expected.lifetime_variance));
		CHECK(actual.initial_speed == doctest::Approx(expected.initial_speed));
		CHECK(actual.spread_angle_degrees == doctest::Approx(expected.spread_angle_degrees));
		CHECK(actual.constant_acceleration.x == doctest::Approx(expected.constant_acceleration.x));
		CHECK(actual.constant_acceleration.y == doctest::Approx(expected.constant_acceleration.y));
		CHECK(actual.constant_acceleration.z == doctest::Approx(expected.constant_acceleration.z));
		CHECK(actual.start_size == doctest::Approx(expected.start_size));
		CHECK(actual.end_size == doctest::Approx(expected.end_size));
		CHECK(actual.start_color.x == doctest::Approx(expected.start_color.x));
		CHECK(actual.start_color.y == doctest::Approx(expected.start_color.y));
		CHECK(actual.start_color.z == doctest::Approx(expected.start_color.z));
		CHECK(actual.start_color.w == doctest::Approx(expected.start_color.w));
		CHECK(actual.end_color.x == doctest::Approx(expected.end_color.x));
		CHECK(actual.end_color.y == doctest::Approx(expected.end_color.y));
		CHECK(actual.end_color.z == doctest::Approx(expected.end_color.z));
		CHECK(actual.end_color.w == doctest::Approx(expected.end_color.w));
		CHECK(actual.sprite_texture_path == expected.sprite_texture_path);
		CHECK(actual.radial_falloff == doctest::Approx(expected.radial_falloff));
		CHECK(actual.radial_sharpness == doctest::Approx(expected.radial_sharpness));
		CHECK(actual.soft_particles == expected.soft_particles);
		CHECK(actual.soft_fade_distance == doctest::Approx(expected.soft_fade_distance));
		CHECK(actual.blend_mode == expected.blend_mode);
		CHECK(actual.random_seed == expected.random_seed);
		CHECK(actual.emitting == expected.emitting);
	}
}

TEST_CASE("ParticleComponent descriptor exposes stable enum and editor ranges")
{
	const AshEngine::SceneComponentDesc* descriptor =
		AshEngine::get_scene_component_descriptor(AshEngine::SceneComponentType::Particle);
	REQUIRE(descriptor != nullptr);
	CHECK(std::string(descriptor->name) == "ParticleComponent");
	CHECK(descriptor->byte_size == sizeof(AshEngine::ParticleComponent));
	CHECK(descriptor->property_count == 19u);

	const AshEngine::ScenePropertyDesc* capacity = FindProperty(*descriptor, "max_particles");
	REQUIRE(capacity != nullptr);
	CHECK(capacity->type == AshEngine::ScenePropertyType::UInt32);
	CheckPropertyRange(*descriptor, "max_particles", 1.0f, 65536.0f);
	CheckPropertyRange(*descriptor, "spawn_rate", 0.0f, 20000.0f);
	CheckPropertyRange(*descriptor, "lifetime", 0.01f, 60.0f);
	CheckPropertyRange(*descriptor, "lifetime_variance", 0.0f, 30.0f);
	CheckPropertyRange(*descriptor, "initial_speed", 0.0f, 100.0f);
	CheckPropertyRange(*descriptor, "spread_angle_degrees", 0.0f, 90.0f);
	CheckPropertyRange(*descriptor, "start_size", 0.0f, 10.0f);
	CheckPropertyRange(*descriptor, "end_size", 0.0f, 10.0f);
	CheckPropertyRange(*descriptor, "radial_falloff", 0.0f, 1.0f);
	CheckPropertyRange(*descriptor, "radial_sharpness", 0.25f, 8.0f);
	CheckPropertyRange(*descriptor, "soft_fade_distance", 0.001f, 10.0f);

	const AshEngine::ScenePropertyDesc* sprite_texture = FindProperty(*descriptor, "sprite_texture_path");
	REQUIRE(sprite_texture != nullptr);
	CHECK(sprite_texture->type == AshEngine::ScenePropertyType::String);
	CHECK(sprite_texture->editor_hint == AshEngine::ScenePropertyEditorHint::AssetPath);
	CHECK(sprite_texture->asset_ref_kind == AshEngine::ScenePropertyAssetRefKind::Texture);

	const AshEngine::ScenePropertyDesc* soft_particles = FindProperty(*descriptor, "soft_particles");
	REQUIRE(soft_particles != nullptr);
	CHECK(soft_particles->type == AshEngine::ScenePropertyType::Bool);

	CheckPropertyText(
		*descriptor,
		"sprite_texture_path",
		"Sprite Texture",
		"RGBA sprite texture; empty uses the default white sprite.");
	CheckPropertyText(
		*descriptor,
		"radial_falloff",
		"Radial Falloff",
		"Blend between sprite-only and the analytic radial mask.");
	CheckPropertyText(
		*descriptor,
		"radial_sharpness",
		"Radial Sharpness",
		"Power exponent for the analytic radial mask.");
	CheckPropertyText(
		*descriptor,
		"soft_particles",
		"Soft Particles",
		"Fade near opaque scene depth intersections.");
	CheckPropertyText(
		*descriptor,
		"soft_fade_distance",
		"Soft Fade Distance",
		"World-space depth interval used by soft particles.");

	const AshEngine::ScenePropertyDesc* blend_mode = FindProperty(*descriptor, "blend_mode");
	REQUIRE(blend_mode != nullptr);
	CHECK(blend_mode->type == AshEngine::ScenePropertyType::Enum);
	CHECK(std::string(blend_mode->enum_name) == "ParticleBlendMode");

	const AshEngine::SceneEnumDesc* enum_descriptor =
		AshEngine::get_scene_enum_descriptor("ParticleBlendMode");
	REQUIRE(enum_descriptor != nullptr);
	REQUIRE(enum_descriptor->value_count == 2u);
	CHECK(enum_descriptor->values[0].value == static_cast<int32_t>(AshEngine::ParticleBlendMode::Additive));
	CHECK(std::string(enum_descriptor->values[0].name) == "Additive");
	CHECK(enum_descriptor->values[1].value == static_cast<int32_t>(AshEngine::ParticleBlendMode::AlphaBlend));
	CHECK(std::string(enum_descriptor->values[1].name) == "AlphaBlend");
}

TEST_CASE("ParticleComponent facade, extraction, and scene serialization preserve emitter state")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Particle Test");
	REQUIRE(scene.is_valid());
	AshEngine::Entity emitter = scene.create_entity("Emitter");
	REQUIRE(emitter.is_valid());

	const uint64_t initial_primitive_version = scene.get_render_primitive_version();
	const uint64_t initial_transform_version = scene.get_render_transform_version();
	const uint64_t initial_light_version = scene.get_render_light_version();
	const uint64_t initial_environment_version = scene.get_render_environment_version();
	const uint64_t initial_particle_version = scene.get_render_particle_version();
	const uint64_t initial_config_version = scene.get_render_config_version();
	CHECK(AshEngine::can_add_scene_component(emitter, AshEngine::SceneComponentType::Particle));
	CHECK(AshEngine::add_scene_component(emitter, AshEngine::SceneComponentType::Particle));
	CHECK(emitter.has_particle_component());
	CHECK(scene.get_render_particle_version() > initial_particle_version);
	CHECK(scene.get_render_primitive_version() == initial_primitive_version);
	CHECK(scene.get_render_transform_version() == initial_transform_version);
	CHECK(scene.get_render_light_version() == initial_light_version);
	CHECK(scene.get_render_environment_version() == initial_environment_version);
	CHECK(scene.get_render_config_version() == initial_config_version);

	AshEngine::ParticleComponent expected{};
	expected.max_particles = 8192u;
	expected.spawn_rate = 321.5f;
	expected.lifetime = 4.25f;
	expected.lifetime_variance = 0.75f;
	expected.initial_speed = 7.5f;
	expected.spread_angle_degrees = 33.0f;
	expected.constant_acceleration = { 1.0f, -4.5f, 2.0f };
	expected.start_size = 0.35f;
	expected.end_size = 0.08f;
	expected.start_color = { 0.9f, 0.6f, 0.2f, 0.8f };
	expected.end_color = { 0.1f, 0.2f, 0.7f, 0.0f };
	expected.sprite_texture_path = "textures/particles/T_ParticleSmoke.png";
	expected.radial_falloff = 0.35f;
	expected.radial_sharpness = 3.5f;
	expected.soft_particles = false;
	expected.soft_fade_distance = 0.75f;
	expected.blend_mode = AshEngine::ParticleBlendMode::AlphaBlend;
	expected.random_seed = 0x1234abcdU;
	expected.emitting = false;
	const uint64_t version_before_set = scene.get_render_particle_version();
	CHECK(emitter.set_particle_component(expected));
	CHECK(scene.get_render_particle_version() > version_before_set);
	CHECK(scene.get_render_primitive_version() == initial_primitive_version);
	CHECK(scene.get_render_transform_version() == initial_transform_version);
	CHECK(scene.get_render_light_version() == initial_light_version);
	CHECK(scene.get_render_environment_version() == initial_environment_version);
	CHECK(scene.get_render_config_version() == initial_config_version);
	CheckParticleComponentMatches(emitter.get_particle_component(), expected);

	const std::vector<AshEngine::SceneParticleExtractionDesc> extracted = scene.extract_particle_entities();
	REQUIRE(extracted.size() == 1u);
	CHECK(extracted[0].entity_id == emitter.get_id());
	CheckParticleComponentMatches(extracted[0].particle, expected);

	const fs::path temp_directory = fs::path("Intermediate") / "test-temp" / "scene";
	const fs::path scene_path = temp_directory / "particle_component_roundtrip.scene.json";
	std::error_code filesystem_error{};
	fs::create_directories(temp_directory, filesystem_error);
	REQUIRE_FALSE(filesystem_error);

	std::string error_message{};
	REQUIRE(scene.save_to_file(scene_path, &error_message));
	CHECK(error_message.empty());
	json saved_json = ReadJson(scene_path);
	CHECK(saved_json.at("version") == 6u);
	REQUIRE(saved_json.at("entities").size() == 1u);
	const json& saved_particle = saved_json.at("entities")[0].at("particle");
	CHECK(saved_particle.at("sprite_texture_path") == expected.sprite_texture_path);
	CHECK(saved_particle.at("radial_falloff") == doctest::Approx(expected.radial_falloff));
	CHECK(saved_particle.at("radial_sharpness") == doctest::Approx(expected.radial_sharpness));
	CHECK(saved_particle.at("soft_particles") == expected.soft_particles);
	CHECK(saved_particle.at("soft_fade_distance") == doctest::Approx(expected.soft_fade_distance));
	CHECK(saved_particle.at("blend_mode") == "AlphaBlend");

	AshEngine::Scene loaded = AshEngine::Scene::load_from_file(scene_path, &error_message);
	REQUIRE_MESSAGE(loaded.is_valid(), error_message);
	AshEngine::Entity loaded_emitter = loaded.find_entity(emitter.get_id());
	REQUIRE(loaded_emitter.is_valid());
	REQUIRE(loaded_emitter.has_particle_component());
	CheckParticleComponentMatches(loaded_emitter.get_particle_component(), expected);

	json legacy_v5_json = saved_json;
	legacy_v5_json["version"] = 5u;
	json& legacy_v5_particle = legacy_v5_json["entities"][0]["particle"];
	legacy_v5_particle.erase("sprite_texture_path");
	legacy_v5_particle.erase("radial_falloff");
	legacy_v5_particle.erase("radial_sharpness");
	legacy_v5_particle.erase("soft_particles");
	legacy_v5_particle.erase("soft_fade_distance");
	WriteJson(scene_path, legacy_v5_json);
	AshEngine::Scene legacy_v5_loaded = AshEngine::Scene::load_from_file(scene_path, &error_message);
	REQUIRE_MESSAGE(legacy_v5_loaded.is_valid(), error_message);
	const AshEngine::Entity legacy_v5_emitter = legacy_v5_loaded.find_entity(emitter.get_id());
	REQUIRE(legacy_v5_emitter.has_particle_component());
	const AshEngine::ParticleComponent defaults{};
	const AshEngine::ParticleComponent legacy_v5_component = legacy_v5_emitter.get_particle_component();
	CHECK(legacy_v5_component.sprite_texture_path == defaults.sprite_texture_path);
	CHECK(legacy_v5_component.radial_falloff == doctest::Approx(defaults.radial_falloff));
	CHECK(legacy_v5_component.radial_sharpness == doctest::Approx(defaults.radial_sharpness));
	CHECK(legacy_v5_component.soft_particles == defaults.soft_particles);
	CHECK(legacy_v5_component.soft_fade_distance == doctest::Approx(defaults.soft_fade_distance));
	CHECK(legacy_v5_component.blend_mode == AshEngine::ParticleBlendMode::AlphaBlend);

	CHECK(AshEngine::can_remove_scene_component(loaded_emitter, AshEngine::SceneComponentType::Particle));
	CHECK(AshEngine::remove_scene_component(loaded_emitter, AshEngine::SceneComponentType::Particle));
	CHECK_FALSE(loaded_emitter.has_particle_component());

	legacy_v5_json["version"] = 4u;
	legacy_v5_json["entities"][0]["particle"]["blend_mode"] =
		static_cast<int32_t>(AshEngine::ParticleBlendMode::AlphaBlend);
	WriteJson(scene_path, legacy_v5_json);
	AshEngine::Scene legacy_loaded = AshEngine::Scene::load_from_file(scene_path, &error_message);
	REQUIRE_MESSAGE(legacy_loaded.is_valid(), error_message);
	const AshEngine::Entity legacy_emitter = legacy_loaded.find_entity(emitter.get_id());
	REQUIRE(legacy_emitter.has_particle_component());
	CHECK(legacy_emitter.get_particle_component().blend_mode == AshEngine::ParticleBlendMode::AlphaBlend);

	fs::remove(scene_path, filesystem_error);
}

TEST_CASE("Scene content epoch changes on reload but not ordinary particle edits")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Particle Epoch Test");
	AshEngine::Entity emitter = scene.create_entity("Emitter");
	REQUIRE(emitter.add_particle_component({}));
	const uint64_t content_epoch = scene.get_content_epoch();
	REQUIRE(content_epoch != 0);

	AshEngine::ParticleComponent edited = emitter.get_particle_component();
	edited.spawn_rate += 1.0f;
	REQUIRE(emitter.set_particle_component(edited));
	CHECK(scene.get_content_epoch() == content_epoch);

	const fs::path temp_directory = fs::path("Intermediate") / "test-temp" / "scene";
	const fs::path scene_path = temp_directory / "particle_content_epoch.scene.json";
	std::error_code filesystem_error{};
	fs::create_directories(temp_directory, filesystem_error);
	REQUIRE_FALSE(filesystem_error);
	std::string error_message{};
	REQUIRE(scene.save_to_file(scene_path, &error_message));
	REQUIRE(scene.reload_from_file(scene_path, &error_message));
	CHECK(scene.get_content_epoch() > content_epoch);
	fs::remove(scene_path, filesystem_error);
}

TEST_CASE("ParticleComponent add and set sanitize values before storing engine state")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Particle Sanitize Test");
	AshEngine::Entity emitter = scene.create_entity("Emitter");
	REQUIRE(emitter.is_valid());

	AshEngine::ParticleComponent invalid{};
	invalid.max_particles = 0u;
	invalid.spawn_rate = std::numeric_limits<float>::infinity();
	invalid.lifetime = -1.0f;
	invalid.lifetime_variance = 31.0f;
	invalid.initial_speed = -10.0f;
	invalid.spread_angle_degrees = std::numeric_limits<float>::quiet_NaN();
	invalid.constant_acceleration =
		{ std::numeric_limits<float>::quiet_NaN(), 2.0f, std::numeric_limits<float>::infinity() };
	invalid.start_size = 11.0f;
	invalid.end_size = -1.0f;
	invalid.start_color = { -1.0f, 0.25f, 2.0f, std::numeric_limits<float>::quiet_NaN() };
	invalid.end_color = { std::numeric_limits<float>::infinity(), 0.3f, -1.0f, 2.0f };
	invalid.radial_falloff = -1.0f;
	invalid.radial_sharpness = std::numeric_limits<float>::infinity();
	invalid.soft_fade_distance = 11.0f;
	invalid.blend_mode = static_cast<AshEngine::ParticleBlendMode>(255u);
	REQUIRE(emitter.add_particle_component(invalid));

	const AshEngine::ParticleComponent defaults{};
	const AshEngine::ParticleComponent sanitized = emitter.get_particle_component();
	CHECK(sanitized.max_particles == 1u);
	CHECK(sanitized.spawn_rate == doctest::Approx(defaults.spawn_rate));
	CHECK(sanitized.lifetime == doctest::Approx(0.01f));
	CHECK(sanitized.lifetime_variance == doctest::Approx(30.0f));
	CHECK(sanitized.initial_speed == doctest::Approx(0.0f));
	CHECK(sanitized.spread_angle_degrees == doctest::Approx(defaults.spread_angle_degrees));
	CHECK(sanitized.constant_acceleration.x == doctest::Approx(defaults.constant_acceleration.x));
	CHECK(sanitized.constant_acceleration.y == doctest::Approx(2.0f));
	CHECK(sanitized.constant_acceleration.z == doctest::Approx(defaults.constant_acceleration.z));
	CHECK(sanitized.start_size == doctest::Approx(10.0f));
	CHECK(sanitized.end_size == doctest::Approx(0.0f));
	CHECK(sanitized.start_color.x == doctest::Approx(0.0f));
	CHECK(sanitized.start_color.y == doctest::Approx(0.25f));
	CHECK(sanitized.start_color.z == doctest::Approx(1.0f));
	CHECK(sanitized.start_color.w == doctest::Approx(defaults.start_color.w));
	CHECK(sanitized.end_color.x == doctest::Approx(defaults.end_color.x));
	CHECK(sanitized.end_color.y == doctest::Approx(0.3f));
	CHECK(sanitized.end_color.z == doctest::Approx(0.0f));
	CHECK(sanitized.end_color.w == doctest::Approx(1.0f));
	CHECK(sanitized.radial_falloff == doctest::Approx(0.0f));
	CHECK(sanitized.radial_sharpness == doctest::Approx(defaults.radial_sharpness));
	CHECK(sanitized.soft_fade_distance == doctest::Approx(10.0f));
	CHECK(sanitized.blend_mode == AshEngine::ParticleBlendMode::Additive);

	invalid.max_particles = std::numeric_limits<uint32_t>::max();
	invalid.spawn_rate = 30000.0f;
	invalid.lifetime = 100.0f;
	invalid.initial_speed = 101.0f;
	invalid.spread_angle_degrees = 180.0f;
	REQUIRE(emitter.set_particle_component(invalid));
	const AshEngine::ParticleComponent upper_sanitized = emitter.get_particle_component();
	CHECK(upper_sanitized.max_particles == AshEngine::k_max_particles_per_emitter);
	CHECK(upper_sanitized.spawn_rate == doctest::Approx(20000.0f));
	CHECK(upper_sanitized.lifetime == doctest::Approx(60.0f));
	CHECK(upper_sanitized.initial_speed == doctest::Approx(100.0f));
	CHECK(upper_sanitized.spread_angle_degrees == doctest::Approx(90.0f));
}

TEST_CASE("ParticleComponent JSON load falls back safely for malformed fields")
{
	const fs::path temp_directory = fs::path("Intermediate") / "test-temp" / "scene";
	const fs::path scene_path = temp_directory / "particle_component_malformed.scene.json";
	std::error_code filesystem_error{};
	fs::create_directories(temp_directory, filesystem_error);
	REQUIRE_FALSE(filesystem_error);

	json malformed =
	{
		{ "version", 6u },
		{ "name", "Malformed Particle" },
		{ "entities", json::array({
			{
				{ "id", 1u },
				{ "name", "Malformed Fields" },
				{ "particle", {
					{ "max_particles", -1 },
					{ "spawn_rate", json::array() },
					{ "lifetime", -100.0f },
					{ "constant_acceleration", json::array({ 1.0f, "bad", 3.0f }) },
					{ "start_color", "red" },
					{ "end_color", json::array({ 2.0f, -1.0f, 0.5f, 3.0f }) },
					{ "sprite_texture_path", json::array() },
					{ "radial_falloff", -5.0f },
					{ "radial_sharpness", "sharp" },
					{ "soft_particles", 42 },
					{ "soft_fade_distance", 100.0f },
					{ "blend_mode", "Unknown" },
					{ "random_seed", 1.5 },
					{ "emitting", 42 },
				} },
			},
			{
				{ "id", 2u },
				{ "name", "Malformed Component" },
				{ "particle", "not-an-object" },
			},
		}) },
	};
	WriteJson(scene_path, malformed);

	std::string error_message{};
	AshEngine::Scene loaded = AshEngine::Scene::load_from_file(scene_path, &error_message);
	REQUIRE_MESSAGE(loaded.is_valid(), error_message);
	CHECK(error_message.empty());

	const AshEngine::ParticleComponent defaults{};
	const AshEngine::Entity malformed_fields = loaded.find_entity(1u);
	REQUIRE(malformed_fields.has_particle_component());
	const AshEngine::ParticleComponent sanitized = malformed_fields.get_particle_component();
	CHECK(sanitized.max_particles == defaults.max_particles);
	CHECK(sanitized.spawn_rate == doctest::Approx(defaults.spawn_rate));
	CHECK(sanitized.lifetime == doctest::Approx(0.01f));
	CHECK(sanitized.constant_acceleration.x == doctest::Approx(defaults.constant_acceleration.x));
	CHECK(sanitized.constant_acceleration.y == doctest::Approx(defaults.constant_acceleration.y));
	CHECK(sanitized.constant_acceleration.z == doctest::Approx(defaults.constant_acceleration.z));
	CHECK(sanitized.start_color.x == doctest::Approx(defaults.start_color.x));
	CHECK(sanitized.start_color.y == doctest::Approx(defaults.start_color.y));
	CHECK(sanitized.start_color.z == doctest::Approx(defaults.start_color.z));
	CHECK(sanitized.start_color.w == doctest::Approx(defaults.start_color.w));
	CHECK(sanitized.end_color.x == doctest::Approx(1.0f));
	CHECK(sanitized.end_color.y == doctest::Approx(0.0f));
	CHECK(sanitized.end_color.z == doctest::Approx(0.5f));
	CHECK(sanitized.end_color.w == doctest::Approx(1.0f));
	CHECK(sanitized.sprite_texture_path.empty());
	CHECK(sanitized.radial_falloff == doctest::Approx(0.0f));
	CHECK(sanitized.radial_sharpness == doctest::Approx(defaults.radial_sharpness));
	CHECK(sanitized.soft_particles == defaults.soft_particles);
	CHECK(sanitized.soft_fade_distance == doctest::Approx(10.0f));
	CHECK(sanitized.blend_mode == AshEngine::ParticleBlendMode::Additive);
	CHECK(sanitized.random_seed == defaults.random_seed);
	CHECK(sanitized.emitting == defaults.emitting);

	const AshEngine::Entity malformed_component = loaded.find_entity(2u);
	REQUIRE(malformed_component.has_particle_component());
	CheckParticleComponentMatches(malformed_component.get_particle_component(), defaults);

	fs::remove(scene_path, filesystem_error);
}
