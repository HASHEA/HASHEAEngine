#pragma once

#include "Function/Scene/SceneComponents.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>

namespace AshEngine::ParticleSystemInternal
{
	inline glm::vec4 make_depth_reconstruct_coefficients(const glm::mat4& projection)
	{
		return glm::vec4(
			projection[2][2],
			projection[2][3],
			projection[3][2],
			projection[3][3]);
	}

	inline float reconstruct_linear_view_depth(const glm::vec4& coefficients, float device_depth)
	{
		const float denominator = device_depth * coefficients.y - coefficients.x;
		return std::abs(
			(coefficients.z - device_depth * coefficients.w) /
			(std::abs(denominator) > 1.0e-6f ? denominator : 1.0e-6f));
	}

	inline uint32_t float_bits(float value)
	{
		static_assert(sizeof(float) == sizeof(uint32_t));
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	inline void hash_simulation_value(uint64_t& hash_value, uint32_t value)
	{
		hash_value ^= static_cast<uint64_t>(value);
		hash_value *= 1099511628211ull;
	}

	inline uint64_t build_simulation_config_hash(const ParticleComponent& particle)
	{
		uint64_t hash_value = 1469598103934665603ull;
		hash_simulation_value(hash_value, float_bits(particle.spawn_rate));
		hash_simulation_value(hash_value, float_bits(particle.lifetime));
		hash_simulation_value(hash_value, float_bits(particle.lifetime_variance));
		hash_simulation_value(hash_value, float_bits(particle.initial_speed));
		hash_simulation_value(hash_value, float_bits(particle.spread_angle_degrees));
		hash_simulation_value(hash_value, float_bits(particle.constant_acceleration.x));
		hash_simulation_value(hash_value, float_bits(particle.constant_acceleration.y));
		hash_simulation_value(hash_value, float_bits(particle.constant_acceleration.z));
		hash_simulation_value(hash_value, particle.random_seed);
		return hash_value;
	}
}
