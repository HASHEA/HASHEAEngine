#include "doctest.h"

#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainSpatialData.h"
#include "Function/Scene/TerrainQuery.h"
#include "Terrain/TerrainTestUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace
{
	auto MakeQuerySnapshot() -> AshEngine::TerrainAssetSnapshot
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
		if (!AshEngine::create_flat_terrain_snapshot(
			23u,
			TerrainTests::MakeSmallLayout(),
			{ 0.0f, 65535.0f },
			0.0f,
			flat))
		{
			throw std::runtime_error("could not create Terrain query fixture");
		}
		AshEngine::TerrainAssetSnapshot snapshot = *flat;
		for (uint32_t z = 0u; z < snapshot.layout.component_count_z; ++z)
		{
			for (uint32_t x = 0u; x < snapshot.layout.component_count_x; ++x)
			{
				const AshEngine::TerrainComponentCoord coord{
					static_cast<uint16_t>(x), static_cast<uint16_t>(z)
				};
				const AshEngine::TerrainSampleRect rect =
					AshEngine::get_terrain_component_snapshot_rect(snapshot.layout, coord);
				auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
					*snapshot.components[static_cast<size_t>(z) * snapshot.layout.component_count_x + x]);
				component->heights.clear();
				for (uint32_t sample_z = rect.min_z; sample_z < rect.max_z_exclusive; ++sample_z)
				{
					for (uint32_t sample_x = rect.min_x; sample_x < rect.max_x_exclusive; ++sample_x)
					{
						component->heights.push_back(
							2.0f * static_cast<float>(sample_x) +
							3.0f * static_cast<float>(sample_z));
					}
				}
				if (!AshEngine::build_terrain_component_spatial_data(
						*component, rect.width(), rect.height()))
				{
					throw std::runtime_error("could not build Terrain query spatial data");
				}
				snapshot.components[static_cast<size_t>(z) * snapshot.layout.component_count_x + x] =
					std::move(component);
			}
		}
		return snapshot;
	}

	auto MakeRaySnapshot() -> AshEngine::TerrainAssetSnapshot
	{
		auto snapshot = MakeQuerySnapshot();
		for (auto& component_ptr : snapshot.components)
		{
			auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(*component_ptr);
			std::fill(component->heights.begin(), component->heights.end(), 0.0f);
			if (component->coord == AshEngine::TerrainComponentCoord{ 0u, 0u })
			{
				component->heights[1u * component->sample_width + 1u] = 2.0f;
			}
			if (!AshEngine::build_terrain_component_spatial_data(
					*component, component->sample_width, component->sample_height))
			{
				throw std::runtime_error("could not build Terrain ray spatial data");
			}
			component_ptr = std::move(component);
		}
		return snapshot;
	}
}

TEST_CASE("Terrain query returns bilinear height and centered plane normal")
{
	const auto snapshot = MakeQuerySnapshot();
	float height = -99.0f;
	CHECK(AshEngine::query_height(snapshot, { 1.25f, 2.5f }, height) ==
		AshEngine::TerrainQueryStatus::Ready);
	CHECK(height == doctest::Approx(10.0f));

	glm::vec3 normal{ 9.0f };
	CHECK(AshEngine::query_normal(snapshot, { 2.0f, 2.0f }, normal) ==
		AshEngine::TerrainQueryStatus::Ready);
	const glm::vec3 expected = glm::normalize(glm::vec3{ -2.0f, 1.0f, -3.0f });
	CHECK(normal.x == doctest::Approx(expected.x));
	CHECK(normal.y == doctest::Approx(expected.y));
	CHECK(normal.z == doctest::Approx(expected.z));
}

TEST_CASE("Terrain query status precedence preserves outputs until Ready")
{
	const auto snapshot = MakeQuerySnapshot();
	float height = 77.0f;
	CHECK(AshEngine::query_height(snapshot, { -0.01f, 2.0f }, height) ==
		AshEngine::TerrainQueryStatus::Outside);
	CHECK(height == 77.0f);
	CHECK(AshEngine::query_height(snapshot, { 8.0f, 2.0f }, height) ==
		AshEngine::TerrainQueryStatus::Outside);
	CHECK(height == 77.0f);

	auto pending = snapshot;
	pending.components[1u].reset();
	CHECK(AshEngine::query_height(pending, { 6.0f, 2.0f }, height) ==
		AshEngine::TerrainQueryStatus::Pending);
	CHECK(height == 77.0f);

	auto failed = pending;
	failed.failed = true;
	CHECK(AshEngine::query_height(failed, { -1.0f, -1.0f }, height) ==
		AshEngine::TerrainQueryStatus::Failed);
	CHECK(height == 77.0f);

	auto malformed = snapshot;
	malformed.components.pop_back();
	CHECK(AshEngine::query_height(malformed, { 2.0f, 2.0f }, height) ==
		AshEngine::TerrainQueryStatus::Failed);
	CHECK(height == 77.0f);
}

TEST_CASE("Terrain query ray cast hits both heightfield cell triangles exactly")
{
	const auto snapshot = MakeRaySnapshot();
	for (const glm::vec2 xz : { glm::vec2{ 0.75f, 0.25f }, glm::vec2{ 0.25f, 0.75f } })
	{
		CAPTURE(xz.x);
		CAPTURE(xz.y);
		AshEngine::TerrainRayHit hit{};
		hit.distance = -1.0f;
		const AshEngine::TerrainRay ray{
			{ xz.x, 10.0f, xz.y },
			{ 0.0f, -1.0f, 0.0f }
		};
		CHECK(AshEngine::ray_cast_terrain(snapshot, ray, 20.0f, hit) ==
			AshEngine::TerrainQueryStatus::Ready);
		CHECK(hit.distance == doctest::Approx(9.5f));
		CHECK(hit.position.x == doctest::Approx(xz.x));
		CHECK(hit.position.y == doctest::Approx(0.5f));
		CHECK(hit.position.z == doctest::Approx(xz.y));
		CHECK(hit.component == AshEngine::TerrainComponentCoord{ 0u, 0u });
	}
}

TEST_CASE("Terrain query ray cast rejects invalid directions without changing output")
{
	const auto snapshot = MakeRaySnapshot();
	AshEngine::TerrainRayHit hit{};
	hit.distance = 123.0f;
	const AshEngine::TerrainRayHit before = hit;
	CHECK(AshEngine::ray_cast_terrain(
		snapshot,
		{ { 1.0f, 10.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
		20.0f,
		hit) == AshEngine::TerrainQueryStatus::Failed);
	CHECK(hit.distance == before.distance);
}
