#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

namespace
{
	std::string ReadSource(const char* path)
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE_MESSAGE(input.is_open(), "failed to open shader source contract");
		return {
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	float ProjectViewDepth(const glm::mat4& projection, float view_depth)
	{
		const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, view_depth, 1.0f);
		return clip.z / clip.w;
	}
}

TEST_CASE("Scene depth clear preserves finite reverse-Z terrain depth below legacy epsilon")
{
	constexpr float near_plane = 0.03f;
	constexpr float far_plane = 8701.6f;
	constexpr float terrain_far_depth = 8292.0f;
	const glm::mat4 reverse_z = glm::perspectiveLH_ZO(
		glm::radians(60.0f), 16.0f / 9.0f, far_plane, near_plane);
	const float device_depth = ProjectViewDepth(reverse_z, terrain_far_depth);
	CHECK(device_depth > 0.0f);
	CHECK(device_depth <= 1.0e-6f);
	CHECK(device_depth != 0.0f);
}

TEST_CASE("Scene depth clear preserves finite normal-Z terrain depth above legacy epsilon")
{
	constexpr float near_plane = 0.03f;
	constexpr float far_plane = 8701.6f;
	constexpr float terrain_far_depth = 8292.0f;
	const glm::mat4 normal_z = glm::perspectiveLH_ZO(
		glm::radians(60.0f), 16.0f / 9.0f, near_plane, far_plane);
	const float device_depth = ProjectViewDepth(normal_z, terrain_far_depth);
	CHECK(device_depth < 1.0f);
	CHECK(device_depth >= 0.999999f);
	CHECK(device_depth != 1.0f);
}

TEST_CASE("Scene depth clear consumers use the shared exact-endpoint contract")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Scene/SceneDepthCommon.hlsli");
	CHECK(common.find("reverse_z ? depth <= 0.0 : depth >= 1.0") != std::string::npos);
	CHECK(common.find("0.000001") == std::string::npos);
	CHECK(common.find("0.999999") == std::string::npos);

	const std::array<const char*, 8> consumers{
		"project/src/engine/Shaders/Deferred/DeferredCommon.hlsli",
		"project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli",
		"project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli",
		"project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli",
		"project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl",
		"project/src/engine/Shaders/Shadow/DirectionalShadowCascadeDebug.hlsl",
		"project/src/engine/Shaders/Debug/RenderDebugView.hlsl",
		"project/src/engine/Shaders/Particles/ParticleSystem.hlsl",
	};
	for (const char* path : consumers)
	{
		CAPTURE(path);
		const std::string source = ReadSource(path);
		CHECK(source.find("SceneDepthCommon.hlsli") != std::string::npos);
		CHECK(source.find("AshSceneDepthIsBackground(") != std::string::npos);
		CHECK(source.find("depth <= 0.000001") == std::string::npos);
		CHECK(source.find("depth >= 0.999999") == std::string::npos);
		CHECK(source.find("scene_device_depth <= 1.0e-6") == std::string::npos);
		CHECK(source.find("scene_device_depth >= 0.999999") == std::string::npos);
	}
}
