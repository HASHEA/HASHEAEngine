#include "doctest.h"

#include "Function/Asset/TerrainBrush.h"

#include <cmath>
#include <limits>
#include <vector>

namespace
{
	auto CheckSample(
		const AshEngine::TerrainStrokeSample& sample,
		float expected_x,
		float expected_z,
		float expected_pressure) -> void
	{
		CHECK(sample.terrain_local_xz.x == doctest::Approx(expected_x));
		CHECK(sample.terrain_local_xz.y == doctest::Approx(expected_z));
		CHECK(sample.pressure == doctest::Approx(expected_pressure));
	}
}

TEST_CASE("Terrain stroke sampling is invariant to source point density in world metric")
{
	const AshEngine::TerrainBrushMetric metric{ { 2.0f, 0.5f } };
	const std::vector<AshEngine::TerrainStrokeSample> endpoint_only{
		{ { 0.0f, 0.0f }, 0.0f },
		{ { 2.0f, 0.0f }, 0.5f },
		{ { 2.0f, 4.0f }, 1.0f }
	};
	const std::vector<AshEngine::TerrainStrokeSample> subdivided{
		{ { 0.0f, 0.0f }, 0.0f },
		{ { 0.50f, 0.0f }, 0.125f },
		{ { 1.00f, 0.0f }, 0.250f },
		{ { 1.50f, 0.0f }, 0.375f },
		{ { 2.00f, 0.0f }, 0.500f },
		{ { 2.00f, 2.0f }, 0.750f },
		{ { 2.00f, 4.0f }, 1.000f }
	};

	std::vector<AshEngine::TerrainStrokeSample> sparse_output{};
	std::vector<AshEngine::TerrainStrokeSample> dense_output{};
	std::string error{ "stale" };
	REQUIRE(AshEngine::resample_terrain_stroke(
		endpoint_only,
		metric,
		1.0f,
		sparse_output,
		&error));
	CHECK(error.empty());
	REQUIRE(AshEngine::resample_terrain_stroke(
		subdivided,
		metric,
		1.0f,
		dense_output,
		&error));
	CHECK(error.empty());
	REQUIRE(sparse_output.size() == 7u);
	REQUIRE(dense_output.size() == sparse_output.size());
	for (size_t index = 0u; index < sparse_output.size(); ++index)
	{
		CHECK(dense_output[index].terrain_local_xz == sparse_output[index].terrain_local_xz);
		CHECK(dense_output[index].pressure == sparse_output[index].pressure);
	}
	CheckSample(sparse_output[0], 0.0f, 0.0f, 0.000f);
	CheckSample(sparse_output[1], 0.5f, 0.0f, 0.125f);
	CheckSample(sparse_output[2], 1.0f, 0.0f, 0.250f);
	CheckSample(sparse_output[3], 1.5f, 0.0f, 0.375f);
	CheckSample(sparse_output[4], 2.0f, 0.0f, 0.500f);
	CheckSample(sparse_output[5], 2.0f, 2.0f, 0.750f);
	CheckSample(sparse_output[6], 2.0f, 4.0f, 1.000f);
	CHECK(sparse_output.front().terrain_local_xz == endpoint_only.front().terrain_local_xz);
	CHECK(sparse_output.back().terrain_local_xz == endpoint_only.back().terrain_local_xz);
}

TEST_CASE("Terrain stroke sampling replaces adjacent metric duplicates with the later sample")
{
	const AshEngine::TerrainBrushMetric metric{ { 2.0f, 0.5f } };
	const std::vector<AshEngine::TerrainStrokeSample> input{
		{ { 0.0f, 0.0f }, 0.1f },
		{ { 0.0000004f, 0.0f }, 0.4f },
		{ { 1.0f, 0.0f }, 0.9f }
	};
	std::vector<AshEngine::TerrainStrokeSample> output{};
	REQUIRE(AshEngine::resample_terrain_stroke(input, metric, 4.0f, output));
	REQUIRE(output.size() == 2u);
	CHECK(output.front().terrain_local_xz == input[1].terrain_local_xz);
	CHECK(output.front().pressure == input[1].pressure);
	CHECK(output.back().terrain_local_xz == input.back().terrain_local_xz);
	CHECK(output.back().pressure == input.back().pressure);
}

TEST_CASE("Terrain stroke sampling defines empty and single-point strokes")
{
	const AshEngine::TerrainBrushMetric metric{};
	std::vector<AshEngine::TerrainStrokeSample> output{ { { 9.0f, 9.0f }, 0.0f } };
	std::string error{ "stale" };
	REQUIRE(AshEngine::resample_terrain_stroke({}, metric, 1.0f, output, &error));
	CHECK(output.empty());
	CHECK(error.empty());

	const std::vector<AshEngine::TerrainStrokeSample> single{
		{ { 3.0f, 5.0f }, 0.25f }
	};
	REQUIRE(AshEngine::resample_terrain_stroke(single, metric, 1.0f, output, &error));
	REQUIRE(output.size() == 1u);
	CheckSample(output[0], 3.0f, 5.0f, 0.25f);
	CHECK(error.empty());
}

TEST_CASE("Terrain stroke sampling rejects invalid metric inputs without changing output")
{
	const std::vector<AshEngine::TerrainStrokeSample> valid{
		{ { 0.0f, 0.0f }, 0.25f },
		{ { 1.0f, 1.0f }, 0.75f }
	};
	const std::vector<AshEngine::TerrainStrokeSample> sentinel{
		{ { 9.0f, 8.0f }, 0.5f }
	};

	auto CheckRejected = [&](const std::vector<AshEngine::TerrainStrokeSample>& input,
		const AshEngine::TerrainBrushMetric& metric, float spacing)
	{
		std::vector<AshEngine::TerrainStrokeSample> output = sentinel;
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::resample_terrain_stroke(input, metric, spacing, output, &error));
		REQUIRE(output.size() == 1u);
		CheckSample(output[0], 9.0f, 8.0f, 0.5f);
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	CheckRejected(valid, {}, 0.0f);
	CheckRejected(valid, {}, -1.0f);
	CheckRejected(valid, {}, std::numeric_limits<float>::infinity());
	CheckRejected(valid, { { 0.0f, 1.0f } }, 1.0f);
	CheckRejected(valid, { { -1.0f, 1.0f } }, 1.0f);
	CheckRejected(valid, { { std::numeric_limits<float>::infinity(), 1.0f } }, 1.0f);

	auto invalid = valid;
	invalid[0].terrain_local_xz.x = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(invalid, {}, 1.0f);
	invalid = valid;
	invalid[0].pressure = -0.01f;
	CheckRejected(invalid, {}, 1.0f);
	invalid[0].pressure = 1.01f;
	CheckRejected(invalid, {}, 1.0f);
	invalid[0].pressure = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(invalid, {}, 1.0f);
}
