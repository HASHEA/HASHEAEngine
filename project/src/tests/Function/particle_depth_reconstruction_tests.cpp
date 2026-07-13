#include "Function/Render/ParticleSystemMath.h"
#include "Function/Render/RenderScene.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

namespace
{
	float ProjectViewDepth(const glm::mat4& projection, float view_depth)
	{
		const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, view_depth, 1.0f);
		return clip.z / clip.w;
	}

	void CheckDepthReconstruction(const glm::mat4& projection, bool reverse_z)
	{
		const glm::vec4 coefficients =
			AshEngine::ParticleSystemInternal::make_depth_reconstruct_coefficients(projection);
		for (const float view_depth : std::array<float, 4>{ 0.1f, 1.0f, 25.0f, 99.0f })
		{
			const float device_depth = ProjectViewDepth(projection, view_depth);
			const float reconstructed =
				AshEngine::ParticleSystemInternal::reconstruct_linear_view_depth(coefficients, device_depth);
			CHECK(std::abs(reconstructed - view_depth) < 0.01f);
		}

		const float near_depth = ProjectViewDepth(projection, 0.1f);
		const float far_depth = ProjectViewDepth(projection, 100.0f);
		CHECK(std::abs(near_depth - (reverse_z ? 1.0f : 0.0f)) < 0.0001f);
		CHECK(std::abs(far_depth - (reverse_z ? 0.0f : 1.0f)) < 0.0001f);
	}
}

TEST_CASE("Particle system visible emitter defaults to no prepared sprite")
{
	const AshEngine::VisibleParticleEmitter emitter{};
	CHECK(emitter.sprite_texture == nullptr);
}

TEST_CASE("Particle system retires previous soft depth binding before adding passes")
{
	std::ifstream source_file("project/src/engine/Function/Render/ParticleSystemPass.cpp");
	REQUIRE_MESSAGE(source_file.is_open(), "failed to open ParticleSystemPass.cpp source contract");
	const std::string source{
		std::istreambuf_iterator<char>(source_file),
		std::istreambuf_iterator<char>() };

	const size_t add_passes_start = source.find("bool ParticleSystemPass::add_passes(");
	REQUIRE(add_passes_start != std::string::npos);
	const size_t add_passes_body_start = source.find('{', add_passes_start);
	REQUIRE(add_passes_body_start != std::string::npos);
	const size_t add_passes_body_end = source.find("\n\t}\n}", add_passes_body_start);
	REQUIRE(add_passes_body_end != std::string::npos);
	const std::string add_passes_body = source.substr(
		add_passes_body_start,
		add_passes_body_end - add_passes_body_start);

	const size_t prune = add_passes_body.find("prune_stale_states(frame);");
	const size_t clear = add_passes_body.find("clear_soft_depth_bindings();");
	const size_t empty_early_return =
		add_passes_body.find("if (frame.particle_emitters.empty())");
	REQUIRE(prune != std::string::npos);
	REQUIRE(clear != std::string::npos);
	REQUIRE(empty_early_return != std::string::npos);
	CHECK(prune < clear);
	CHECK(clear < empty_early_return);
}

TEST_CASE("Particle system reconstructs linear depth for supported projections")
{
	SUBCASE("perspective normal Z")
	{
		CheckDepthReconstruction(
			glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f),
			false);
	}
	SUBCASE("perspective reverse Z")
	{
		CheckDepthReconstruction(
			glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 100.0f, 0.1f),
			true);
	}
	SUBCASE("orthographic normal Z")
	{
		CheckDepthReconstruction(glm::orthoLH_ZO(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f), false);
	}
	SUBCASE("orthographic reverse Z")
	{
		CheckDepthReconstruction(glm::orthoLH_ZO(-4.0f, 4.0f, -3.0f, 3.0f, 100.0f, 0.1f), true);
	}
}

TEST_CASE("Particle system simulation hash excludes appearance and runtime toggles")
{
	AshEngine::ParticleComponent baseline{};
	const uint64_t baseline_hash =
		AshEngine::ParticleSystemInternal::build_simulation_config_hash(baseline);

	AshEngine::ParticleComponent appearance = baseline;
	appearance.sprite_texture_path = "textures/particle.png";
	appearance.radial_falloff = 0.25f;
	appearance.radial_sharpness = 6.0f;
	appearance.soft_particles = !baseline.soft_particles;
	appearance.soft_fade_distance = 3.0f;
	appearance.start_size = 0.75f;
	appearance.end_size = 1.5f;
	appearance.start_color = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
	appearance.end_color = glm::vec4(0.9f, 0.8f, 0.7f, 0.6f);
	appearance.blend_mode = AshEngine::ParticleBlendMode::AlphaBlend;
	appearance.emitting = !baseline.emitting;

	CHECK(AshEngine::ParticleSystemInternal::build_simulation_config_hash(appearance) == baseline_hash);
}

TEST_CASE("Particle system simulation hash includes every simulation field")
{
	const AshEngine::ParticleComponent baseline{};
	const uint64_t baseline_hash =
		AshEngine::ParticleSystemInternal::build_simulation_config_hash(baseline);

	const auto check_changed = [&](const AshEngine::ParticleComponent& changed)
	{
		CHECK(AshEngine::ParticleSystemInternal::build_simulation_config_hash(changed) != baseline_hash);
	};

	SUBCASE("spawn rate")
	{
		auto changed = baseline;
		changed.spawn_rate += 1.0f;
		check_changed(changed);
	}
	SUBCASE("lifetime")
	{
		auto changed = baseline;
		changed.lifetime += 1.0f;
		check_changed(changed);
	}
	SUBCASE("lifetime variance")
	{
		auto changed = baseline;
		changed.lifetime_variance += 1.0f;
		check_changed(changed);
	}
	SUBCASE("initial speed")
	{
		auto changed = baseline;
		changed.initial_speed += 1.0f;
		check_changed(changed);
	}
	SUBCASE("spread")
	{
		auto changed = baseline;
		changed.spread_angle_degrees += 1.0f;
		check_changed(changed);
	}
	SUBCASE("acceleration x")
	{
		auto changed = baseline;
		changed.constant_acceleration.x += 1.0f;
		check_changed(changed);
	}
	SUBCASE("acceleration y")
	{
		auto changed = baseline;
		changed.constant_acceleration.y += 1.0f;
		check_changed(changed);
	}
	SUBCASE("acceleration z")
	{
		auto changed = baseline;
		changed.constant_acceleration.z += 1.0f;
		check_changed(changed);
	}
	SUBCASE("random seed")
	{
		auto changed = baseline;
		++changed.random_seed;
		check_changed(changed);
	}
}
