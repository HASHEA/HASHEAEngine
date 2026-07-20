#include "Function/Asset/VegetationBrush.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr int64_t k_millimeters_per_chunk = 256000;

	std::shared_ptr<const AshEngine::VegetationLayerSnapshot> SharedSnapshot(
		AshEngine::VegetationLayerSnapshot snapshot)
	{
		return std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(snapshot));
	}

	AshEngine::VegetationPaletteEntry PaletteEntry(
		const uint8_t first_id_byte,
		const uint8_t digest_byte,
		std::string path)
	{
		AshEngine::VegetationPaletteEntry entry{};
		entry.species_id = VegetationTest::SequentialId(first_id_byte);
		entry.species_sha256.fill(digest_byte);
		entry.species_asset_path = std::move(path);
		return entry;
	}

	AshEngine::VegetationLayerSnapshot EmptyLayer()
	{
		AshEngine::VegetationLayerSnapshot layer = VegetationTest::MinimalLayerSnapshot();
		layer.tiles.clear();
		return layer;
	}

	AshEngine::VegetationLayerSnapshot TwoSpeciesLayer(const bool include_second_tile = false)
	{
		AshEngine::VegetationLayerSnapshot layer = VegetationTest::MinimalLayerSnapshot();
		const AshEngine::VegetationPaletteEntry second = PaletteEntry(
			33, 0x6b, "vegetation/Phase2SecondSpecies.AshVegetation");
		layer.palette.push_back(second);

		AshEngine::VegetationLayerTile& tile = layer.tiles[0];
		tile.tile_x = 0;
		tile.tile_z = 0;
		tile.planes[0].values.fill(0);
		tile.planes[0].values[0] = 200;
		tile.planes[1].values.fill(0);
		tile.planes[1].values[0] = 150;
		AshEngine::VegetationLayerPlane second_weight = tile.planes[1];
		second_weight.species_id = second.species_id;
		second_weight.values.fill(0);
		second_weight.values[0] = 75;
		tile.planes.push_back(second_weight);

		if (include_second_tile)
		{
			AshEngine::VegetationLayerTile other = tile;
			other.tile_x = 8;
			other.tile_z = 0;
			other.planes[0].values[0] = 180;
			other.planes[1].values[0] = 90;
			other.planes[2].values[0] = 60;
			layer.tiles.push_back(std::move(other));
		}
		return layer;
	}

	AshEngine::VegetationBrushStroke SinglePointStroke(
		const AshEngine::VegetationBrushMode mode,
		const AshEngine::VegetationId& selected_species,
		const AshEngine::VegetationSurfaceSampleRequest& request,
		const uint32_t radius_mm = 250,
		const uint8_t strength = 1,
		const uint8_t falloff = 0,
		const uint32_t spacing_mm = 1)
	{
		AshEngine::VegetationBrushStroke stroke{};
		stroke.mode = mode;
		stroke.selected_species = selected_species;
		stroke.radius_mm = radius_mm;
		stroke.strength = strength;
		stroke.falloff = falloff;
		stroke.spacing_mm = spacing_mm;
		stroke.stroke_seed = 0x0123456789abcdefull;
		stroke.path.push_back(request);
		return stroke;
	}

	const AshEngine::VegetationLayerTile* FindTile(
		const AshEngine::VegetationLayerSnapshot& snapshot,
		const int64_t tile_x,
		const int64_t tile_z)
	{
		const auto iterator = std::find_if(snapshot.tiles.begin(), snapshot.tiles.end(),
			[tile_x, tile_z](const AshEngine::VegetationLayerTile& tile)
			{
				return tile.tile_x == tile_x && tile.tile_z == tile_z;
			});
		return iterator == snapshot.tiles.end() ? nullptr : &*iterator;
	}

	const AshEngine::VegetationLayerPlane* FindPlane(
		const AshEngine::VegetationLayerSnapshot& snapshot,
		const int64_t tile_x,
		const int64_t tile_z,
		const AshEngine::VegetationLayerPlaneKind kind,
		const AshEngine::VegetationId& species_id = {})
	{
		const AshEngine::VegetationLayerTile* tile = FindTile(snapshot, tile_x, tile_z);
		if (tile == nullptr)
		{
			return nullptr;
		}
		const auto iterator = std::find_if(tile->planes.begin(), tile->planes.end(),
			[kind, &species_id](const AshEngine::VegetationLayerPlane& plane)
			{
				return plane.kind == kind && plane.species_id == species_id;
			});
		return iterator == tile->planes.end() ? nullptr : &*iterator;
	}

	bool ContainsChunk(
		const std::vector<AshEngine::VegetationChunkCoord>& coords,
		const int64_t x,
		const int64_t z)
	{
		return std::any_of(coords.begin(), coords.end(), [x, z](const auto& coord)
		{
			return coord.x == x && coord.z == z;
		});
	}

	void CheckDirtyEvidenceEqual(
		const AshEngine::VegetationAuthoringDirtyEvidence& lhs,
		const AshEngine::VegetationAuthoringDirtyEvidence& rhs)
	{
		CHECK(lhs.generation == rhs.generation);
		REQUIRE(lhs.density_coords.size() == rhs.density_coords.size());
		for (size_t index = 0; index < lhs.density_coords.size(); ++index)
		{
			CHECK(lhs.density_coords[index].x == rhs.density_coords[index].x);
			CHECK(lhs.density_coords[index].z == rhs.density_coords[index].z);
		}
		REQUIRE(lhs.species_coords.size() == rhs.species_coords.size());
		for (size_t species_index = 0; species_index < lhs.species_coords.size(); ++species_index)
		{
			const auto& left = lhs.species_coords[species_index];
			const auto& right = rhs.species_coords[species_index];
			CHECK(left.species_id == right.species_id);
			REQUIRE(left.before_coords.size() == right.before_coords.size());
			REQUIRE(left.after_coords.size() == right.after_coords.size());
			for (size_t index = 0; index < left.before_coords.size(); ++index)
			{
				CHECK(left.before_coords[index].x == right.before_coords[index].x);
				CHECK(left.before_coords[index].z == right.before_coords[index].z);
			}
			for (size_t index = 0; index < left.after_coords.size(); ++index)
			{
				CHECK(left.after_coords[index].x == right.after_coords[index].x);
				CHECK(left.after_coords[index].z == right.after_coords[index].z);
			}
		}
	}

	void CheckWorkingSetUnchanged(
		AshEngine::VegetationLayerWorkingSet& working,
		const std::shared_ptr<const AshEngine::VegetationLayerSnapshot>& before)
	{
		const auto after = working.publish_snapshot();
		REQUIRE(after != nullptr);
		CHECK(after.get() == before.get());
		CHECK(working.content_generation() == before->content_generation);
		CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*after) ==
			VegetationTest::CanonicalAuthoringPayloadBytes(*before));
	}

	uint64_t SafeSegmentSquareSum(const AshEngine::VegetationSafeStrokeSegment& segment)
	{
		const int64_t dx = segment.end.x - segment.begin.x;
		const int64_t dz = segment.end.z - segment.begin.z;
		const uint64_t absolute_x = static_cast<uint64_t>(dx < 0 ? -dx : dx);
		const uint64_t absolute_z = static_cast<uint64_t>(dz < 0 ? -dz : dz);
		return absolute_x * absolute_x + absolute_z * absolute_z;
	}
}

TEST_CASE("Vegetation brush converts chunk local coordinates to signed world millimeters and rejects overflow")
{
	AshEngine::VegetationWorldMillimeterPoint point{ 17, 29 };
	CHECK(AshEngine::vegetation_surface_request_to_world_millimeter(
		VegetationTest::SurfaceRequest({ -1, 2 }, { 255.5, 0.25 }), point));
	CHECK(point == AshEngine::VegetationWorldMillimeterPoint{ -500, 512250 });

	CHECK(AshEngine::vegetation_surface_request_to_world_millimeter(
		VegetationTest::SurfaceRequest({ 0, 0 }, { 0.0005, 0.0015 }), point));
	CHECK(point == AshEngine::VegetationWorldMillimeterPoint{ 0, 2 });

	const int64_t maximum_safe_chunk =
		std::numeric_limits<int64_t>::max() / k_millimeters_per_chunk;
	CHECK(AshEngine::vegetation_surface_request_to_world_millimeter(
		VegetationTest::SurfaceRequest({ maximum_safe_chunk, 0 }, { 0.0, 0.0 }), point));
	CHECK(point.x == maximum_safe_chunk * k_millimeters_per_chunk);

	CHECK_FALSE(AshEngine::vegetation_surface_request_to_world_millimeter(
		VegetationTest::SurfaceRequest(
			{ std::numeric_limits<int64_t>::max(), 0 }, { 0.0, 0.0 }), point));
	CHECK_FALSE(AshEngine::vegetation_surface_request_to_world_millimeter(
		VegetationTest::SurfaceRequest(
			{ std::numeric_limits<int64_t>::min(), 0 }, { 0.0, 0.0 }), point));
}

TEST_CASE("Vegetation brush validates v1 numeric parameter boundaries without partial output")
{
	CHECK(AshEngine::vegetation_brush_amount(0, 250, 1, 0) == 1);
	CHECK(AshEngine::vegetation_brush_amount(0, 1024000, 255, 255) == 255);
	CHECK(AshEngine::vegetation_brush_amount(0, 249, 1, 0) == 0);
	CHECK(AshEngine::vegetation_brush_amount(0, 1024001, 1, 0) == 0);
	CHECK(AshEngine::vegetation_brush_amount(0, 250, 0, 0) == 0);

	const std::vector<AshEngine::VegetationWorldMillimeterPoint> minimum_spacing_path{
		{ 0, 0 }, { 2, 0 }
	};
	const std::vector<AshEngine::VegetationWorldMillimeterPoint> maximum_spacing_path{
		{ 0, 0 }, { 2048000, 0 }
	};
	const auto minimum_spacing = AshEngine::resample_vegetation_stroke(
		minimum_spacing_path, 1);
	const auto maximum_spacing = AshEngine::resample_vegetation_stroke(
		maximum_spacing_path, 2048000);
	REQUIRE(minimum_spacing.succeeded);
	REQUIRE(maximum_spacing.succeeded);
	CHECK(minimum_spacing.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
		{ 0, 0 }, { 1, 0 }, { 2, 0 }
	});
	CHECK(maximum_spacing.dabs == maximum_spacing_path);

	const auto zero_spacing = AshEngine::resample_vegetation_stroke(minimum_spacing_path, 0);
	const auto excessive_spacing = AshEngine::resample_vegetation_stroke(
		minimum_spacing_path, 2048001);
	CHECK_FALSE(zero_spacing.succeeded);
	CHECK(zero_spacing.dabs.empty());
	CHECK_FALSE(excessive_spacing.succeeded);
	CHECK(excessive_spacing.dabs.empty());

	const auto exact_delta = AshEngine::canonicalize_vegetation_stroke({
		{ 0, 0 }, { 1000000000LL, -1000000000LL }
	});
	REQUIRE(exact_delta.succeeded);
	CHECK(exact_delta.safe_segments.size() == 1);
	CHECK(SafeSegmentSquareSum(exact_delta.safe_segments.front()) == 2000000000000000000ull);

	const auto excessive_delta = AshEngine::canonicalize_vegetation_stroke({
		{ 0, 0 }, { 1000000001LL, 0 }
	});
	CHECK_FALSE(excessive_delta.succeeded);
	CHECK(excessive_delta.safe_segments.empty());
}

TEST_CASE("Vegetation brush canonical stroke ignores collinear event density across ten billion millimeters")
{
	std::vector<AshEngine::VegetationWorldMillimeterPoint> one_billion_steps{};
	std::vector<AshEngine::VegetationWorldMillimeterPoint> half_billion_steps{};
	for (int64_t index = 0; index <= 10; ++index)
	{
		one_billion_steps.push_back({ index * 1000000000LL, 0 });
	}
	for (int64_t index = 0; index <= 20; ++index)
	{
		half_billion_steps.push_back({ index * 500000000LL, 0 });
	}

	const auto first = AshEngine::canonicalize_vegetation_stroke(one_billion_steps);
	const auto second = AshEngine::canonicalize_vegetation_stroke(half_billion_steps);
	REQUIRE(first.succeeded);
	REQUIRE(second.succeeded);
	CHECK(first.safe_segments == second.safe_segments);
	REQUIRE(first.safe_segments.size() == 10);
	for (const auto& segment : first.safe_segments)
	{
		CHECK(segment.end.x - segment.begin.x == 1000000000LL);
		CHECK(segment.end.z == segment.begin.z);
		CHECK(SafeSegmentSquareSum(segment) <= 2000000000000000000ull);
	}

	const auto first_dabs = AshEngine::resample_vegetation_stroke(one_billion_steps, 2048000);
	const auto second_dabs = AshEngine::resample_vegetation_stroke(half_billion_steps, 2048000);
	REQUIRE(first_dabs.succeeded);
	REQUIRE(second_dabs.succeeded);
	CHECK(first_dabs.dabs == second_dabs.dabs);
}

TEST_CASE("Vegetation brush canonical stroke uses GCD directions without cross product or step overflow")
{
	std::vector<AshEngine::VegetationWorldMillimeterPoint> sparse{};
	std::vector<AshEngine::VegetationWorldMillimeterPoint> dense{};
	constexpr int64_t primitive_x = 999999936LL;
	constexpr int64_t primitive_z = 999999928LL;
	for (int64_t index = 0; index <= 10; ++index)
	{
		sparse.push_back({ index * primitive_x, index * primitive_z });
		dense.push_back({ index * primitive_x, index * primitive_z });
		if (index != 10)
		{
			dense.push_back({
				index * primitive_x + primitive_x / 2,
				index * primitive_z + primitive_z / 2
			});
		}
	}

	const auto first = AshEngine::canonicalize_vegetation_stroke(sparse);
	const auto second = AshEngine::canonicalize_vegetation_stroke(dense);
	REQUIRE(first.succeeded);
	REQUIRE(second.succeeded);
	CHECK(first.safe_segments == second.safe_segments);
	REQUIRE(first.safe_segments.size() == 10);
	for (const auto& segment : first.safe_segments)
	{
		CHECK(SafeSegmentSquareSum(segment) <= 2000000000000000000ull);
	}

	const std::vector<AshEngine::VegetationWorldMillimeterPoint> with_duplicates{
		{ -3000, -4000 }, { -3000, -4000 }, { 0, 0 }, { 0, 0 }, { 3000, 4000 }
	};
	const std::vector<AshEngine::VegetationWorldMillimeterPoint> without_duplicates{
		{ -3000, -4000 }, { 3000, 4000 }
	};
	const auto duplicates = AshEngine::canonicalize_vegetation_stroke(with_duplicates);
	const auto unique = AshEngine::canonicalize_vegetation_stroke(without_duplicates);
	REQUIRE(duplicates.succeeded);
	REQUIRE(unique.succeeded);
	CHECK(duplicates.safe_segments == unique.safe_segments);

	const auto overflow = AshEngine::canonicalize_vegetation_stroke({
		{ std::numeric_limits<int64_t>::min(), 0 },
		{ std::numeric_limits<int64_t>::max(), 0 }
	});
	CHECK_FALSE(overflow.succeeded);
	CHECK(overflow.safe_segments.empty());
}

TEST_CASE("Vegetation brush resampling preserves remainder endpoints and rational ties to even")
{
	const std::vector<AshEngine::VegetationWorldMillimeterPoint> golden_path{
		{ 0, 0 }, { 2000, 0 }
	};
	const auto golden = AshEngine::resample_vegetation_stroke(golden_path, 500);
	REQUIRE(golden.succeeded);
	CHECK(golden.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
		{ 0, 0 }, { 500, 0 }, { 1000, 0 }, { 1500, 0 }, { 2000, 0 }
	});

	const auto inserted = AshEngine::resample_vegetation_stroke(
		{ { 0, 0 }, { 1000, 0 }, { 2000, 0 } }, 500);
	REQUIRE(inserted.succeeded);
	CHECK(inserted.dabs == golden.dabs);

	const auto endpoint = AshEngine::resample_vegetation_stroke(
		{ { 0, 0 }, { 1250, 0 } }, 500);
	REQUIRE(endpoint.succeeded);
	CHECK(endpoint.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
		{ 0, 0 }, { 500, 0 }, { 1000, 0 }, { 1250, 0 }
	});

	const auto carried = AshEngine::resample_vegetation_stroke(
		{ { 0, 0 }, { 750, 0 }, { 750, 1000 } }, 500);
	REQUIRE(carried.succeeded);
	CHECK(carried.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
		{ 0, 0 }, { 500, 0 }, { 750, 250 }, { 750, 750 }, { 750, 1000 }
	});

	const auto half_to_even_zero = AshEngine::resample_vegetation_stroke(
		{ { 0, 0 }, { 1, 2 } }, 1);
	REQUIRE(half_to_even_zero.succeeded);
	CHECK(half_to_even_zero.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
		{ 0, 0 }, { 0, 1 }, { 1, 2 }
	});

	const auto half_to_even_two = AshEngine::resample_vegetation_stroke(
		{ { 0, 0 }, { 3, 3 } }, 2);
	REQUIRE(half_to_even_two.succeeded);
	CHECK(half_to_even_two.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
		{ 0, 0 }, { 2, 2 }, { 3, 3 }
	});
}

TEST_CASE("Vegetation brush integer falloff matches v1 golden amounts")
{
	CHECK(AshEngine::vegetation_brush_amount(0, 1000, 128, 255) == 128);
	CHECK(AshEngine::vegetation_brush_amount(500, 1000, 128, 255) == 64);
	CHECK(AshEngine::vegetation_brush_amount(1000, 1000, 128, 255) == 0);
	CHECK(AshEngine::vegetation_brush_amount(999, 1000, 255, 0) == 255);
	CHECK(AshEngine::vegetation_brush_amount(1000, 1000, 255, 0) == 0);
}

TEST_CASE("Vegetation brush Paint saturates density and selected weight while Erase subtracts every plane")
{
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(TwoSpeciesLayer()));
	const AshEngine::VegetationId first_species = working.publish_snapshot()->palette[0].species_id;
	const AshEngine::VegetationId second_species = working.publish_snapshot()->palette[1].species_id;
	const auto center = VegetationTest::SurfaceRequest(0.5, 0.5);

	const uint64_t paint_generation = working.content_generation();
	const auto paint = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			first_species, center, 250, 105, 255));
	REQUIRE(paint.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(paint.new_generation == paint_generation + 1);
	CHECK(working.content_generation() == paint_generation + 1);
	CHECK_FALSE(paint.patch.entries.empty());

	const auto painted = working.publish_snapshot();
	REQUIRE(painted != nullptr);
	REQUIRE(FindPlane(*painted, 0, 0, AshEngine::VegetationLayerPlaneKind::Density) != nullptr);
	REQUIRE(FindPlane(*painted, 0, 0,
		AshEngine::VegetationLayerPlaneKind::SpeciesWeight, first_species) != nullptr);
	REQUIRE(FindPlane(*painted, 0, 0,
		AshEngine::VegetationLayerPlaneKind::SpeciesWeight, second_species) != nullptr);
	CHECK(FindPlane(*painted, 0, 0,
		AshEngine::VegetationLayerPlaneKind::Density)->values[0] == 255);
	CHECK(FindPlane(*painted, 0, 0,
		AshEngine::VegetationLayerPlaneKind::SpeciesWeight, first_species)->values[0] == 255);
	CHECK(FindPlane(*painted, 0, 0,
		AshEngine::VegetationLayerPlaneKind::SpeciesWeight, second_species)->values[0] == 75);

	const uint64_t erase_generation = working.content_generation();
	const auto erase = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Erase,
			VegetationTest::SequentialId(97), center, 250, 100, 255));
	REQUIRE(erase.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(erase.new_generation == erase_generation + 1);
	const auto erased = working.publish_snapshot();
	REQUIRE(FindPlane(*erased, 0, 0, AshEngine::VegetationLayerPlaneKind::Density) != nullptr);
	CHECK(FindPlane(*erased, 0, 0,
		AshEngine::VegetationLayerPlaneKind::Density)->values[0] == 155);
	CHECK(FindPlane(*erased, 0, 0,
		AshEngine::VegetationLayerPlaneKind::SpeciesWeight, first_species)->values[0] == 155);
	CHECK(FindPlane(*erased, 0, 0,
		AshEngine::VegetationLayerPlaneKind::SpeciesWeight, second_species) == nullptr);

	const auto erase_to_zero = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Erase,
			{}, center, 250, 255, 255));
	REQUIRE(erase_to_zero.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(FindTile(*working.publish_snapshot(), 0, 0) == nullptr);
}

TEST_CASE("Vegetation brush maps one meter texel centers across negative tile boundaries exactly")
{
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(EmptyLayer()));
	const AshEngine::VegetationId species = working.publish_snapshot()->palette[0].species_id;
	const auto result = AshEngine::apply_vegetation_brush_stroke(
		working,
		SinglePointStroke(
			AshEngine::VegetationBrushMode::Paint,
			species,
			VegetationTest::SurfaceRequest(0.5, 0.5),
			1001,
			1,
			0));
	REQUIRE(result.status == AshEngine::VegetationMutationStatus::Applied);
	REQUIRE(result.patch.entries.size() == 6u);

	struct ExpectedTexel
	{
		int64_t tile_x = 0;
		int64_t tile_z = 0;
		size_t index = 0;
	};
	constexpr std::array<ExpectedTexel, 5> expected{ {
		{ -1, 0, 31 },
		{ 0, -1, 992 },
		{ 0, 0, 0 },
		{ 0, 0, 1 },
		{ 0, 0, 32 },
	} };

	const auto snapshot = working.publish_snapshot();
	REQUIRE(snapshot != nullptr);
	REQUIRE(snapshot->tiles.size() == 3u);
	for (const ExpectedTexel& texel : expected)
	{
		const auto* density = FindPlane(
			*snapshot, texel.tile_x, texel.tile_z,
			AshEngine::VegetationLayerPlaneKind::Density);
		const auto* weight = FindPlane(
			*snapshot, texel.tile_x, texel.tile_z,
			AshEngine::VegetationLayerPlaneKind::SpeciesWeight, species);
		REQUIRE(density != nullptr);
		REQUIRE(weight != nullptr);
		CHECK(density->values[texel.index] == 1u);
		CHECK(weight->values[texel.index] == 1u);
	}

	size_t nonzero_values = 0;
	for (const auto& tile : snapshot->tiles)
	{
		for (const auto& plane : tile.planes)
		{
			nonzero_values += static_cast<size_t>(std::count_if(
				plane.values.begin(), plane.values.end(),
				[](const uint8_t value) { return value != 0; }));
		}
	}
	CHECK(nonzero_values == expected.size() * 2u);
}

TEST_CASE("Vegetation brush Erase removes a tile when its density reaches zero before weights")
{
	AshEngine::VegetationLayerSnapshot source = TwoSpeciesLayer();
	source.tiles[0].planes[0].values[0] = 1;
	source.tiles[0].planes[1].values[0] = 2;
	source.tiles[0].planes[2].values[0] = 3;
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));

	const auto result = AshEngine::apply_vegetation_brush_stroke(
		working,
		SinglePointStroke(
			AshEngine::VegetationBrushMode::Erase,
			{},
			VegetationTest::SurfaceRequest(0.5, 0.5),
			250,
			1,
			255));

	REQUIRE(result.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(FindTile(*working.publish_snapshot(), 0, 0) == nullptr);
	REQUIRE(result.patch.entries.size() == 3u);
	for (const auto& entry : result.patch.entries)
	{
		CHECK_FALSE(entry.before_bytes.empty());
		CHECK(entry.after_bytes.empty());
	}
}

TEST_CASE("Vegetation brush invalid empty and no-op strokes preserve publication")
{
	const AshEngine::VegetationLayerSnapshot source = TwoSpeciesLayer();
	const AshEngine::VegetationId selected = source.palette[0].species_id;
	const auto valid = SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
		selected, VegetationTest::SurfaceRequest(0.5, 0.5), 250, 1, 0);

	auto check_rejected = [&source](const AshEngine::VegetationBrushStroke& stroke)
	{
		AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));
		const auto before = working.publish_snapshot();
		const auto result = AshEngine::apply_vegetation_brush_stroke(working, stroke);
		CHECK(result.status == AshEngine::VegetationMutationStatus::Rejected);
		CHECK(result.patch.entries.empty());
		CheckWorkingSetUnchanged(working, before);
	};

	auto no_species = valid;
	no_species.selected_species = {};
	check_rejected(no_species);

	auto unknown_species = valid;
	unknown_species.selected_species = VegetationTest::SequentialId(97);
	check_rejected(unknown_species);

	auto empty = valid;
	empty.path.clear();
	check_rejected(empty);

	auto invalid_local = valid;
	invalid_local.path[0].local_xz.x = std::numeric_limits<double>::quiet_NaN();
	check_rejected(invalid_local);
	invalid_local = valid;
	invalid_local.path[0].local_xz.y = 256.0;
	check_rejected(invalid_local);

	auto overflowing = valid;
	overflowing.path[0] = VegetationTest::SurfaceRequest(
		{ std::numeric_limits<int64_t>::max(), 0 }, { 0.0, 0.0 });
	check_rejected(overflowing);

	auto invalid_radius = valid;
	invalid_radius.radius_mm = 249;
	check_rejected(invalid_radius);
	invalid_radius.radius_mm = 1024001;
	check_rejected(invalid_radius);
	auto invalid_strength = valid;
	invalid_strength.strength = 0;
	check_rejected(invalid_strength);
	auto invalid_spacing = valid;
	invalid_spacing.spacing_mm = 0;
	check_rejected(invalid_spacing);
	invalid_spacing.spacing_mm = 2048001;
	check_rejected(invalid_spacing);

	AshEngine::VegetationLayerSnapshot saturated = source;
	saturated.tiles[0].planes[0].values[0] = 255;
	saturated.tiles[0].planes[1].values[0] = 255;
	AshEngine::VegetationLayerWorkingSet no_op_working(SharedSnapshot(saturated));
	const auto no_op_before = no_op_working.publish_snapshot();
	const auto no_op = AshEngine::apply_vegetation_brush_stroke(no_op_working, valid);
	CHECK(no_op.status == AshEngine::VegetationMutationStatus::NoChange);
	CHECK(no_op.patch.entries.empty());
	CheckWorkingSetUnchanged(no_op_working, no_op_before);
}

TEST_CASE("Vegetation mutation access rejects brush palette patch apply and patch revert")
{
	const AshEngine::VegetationLayerSnapshot source = EmptyLayer();
	const auto stroke = SinglePointStroke(
		AshEngine::VegetationBrushMode::Paint,
		source.palette[0].species_id,
		VegetationTest::SurfaceRequest(0.5, 0.5),
		250,
		17,
		255);

	AshEngine::VegetationLayerWorkingSet read_only(
		SharedSnapshot(source), AshEngine::VegetationLayerMutationAccess::ReadOnly);
	const auto read_only_before = read_only.publish_snapshot();
	const auto brush = AshEngine::apply_vegetation_brush_stroke(read_only, stroke);
	CHECK(brush.status == AshEngine::VegetationMutationStatus::Rejected);
	CHECK(brush.patch.entries.empty());
	CheckWorkingSetUnchanged(read_only, read_only_before);

	AshEngine::VegetationPaletteEdit add{};
	add.mode = AshEngine::VegetationPaletteEditMode::Add;
	add.replacement = PaletteEntry(
		33, 0x6b, "vegetation/Phase2SecondSpecies.AshVegetation");
	const auto palette = AshEngine::apply_vegetation_palette_edit(read_only, add);
	CHECK(palette.status == AshEngine::VegetationMutationStatus::Rejected);
	CHECK(palette.patch.entries.empty());
	CheckWorkingSetUnchanged(read_only, read_only_before);

	AshEngine::VegetationLayerWorkingSet producer(SharedSnapshot(source));
	const auto produced = AshEngine::apply_vegetation_brush_stroke(producer, stroke);
	REQUIRE(produced.status == AshEngine::VegetationMutationStatus::Applied);

	CHECK(AshEngine::apply_vegetation_layer_patch(
		read_only, produced.patch, read_only.content_generation()) ==
		AshEngine::VegetationPatchApplyStatus::ReadOnly);
	CheckWorkingSetUnchanged(read_only, read_only_before);

	AshEngine::VegetationLayerWorkingSet read_only_after(
		producer.publish_snapshot(), AshEngine::VegetationLayerMutationAccess::ReadOnly);
	const auto read_only_after_before = read_only_after.publish_snapshot();
	CHECK(AshEngine::revert_vegetation_layer_patch(
		read_only_after, produced.patch, read_only_after.content_generation()) ==
		AshEngine::VegetationPatchApplyStatus::ReadOnly);
	CheckWorkingSetUnchanged(read_only_after, read_only_after_before);
}

TEST_CASE("Vegetation mutations reject exhausted generation without wrapping or publishing")
{
	AshEngine::VegetationLayerSnapshot exhausted = EmptyLayer();
	exhausted.content_generation = std::numeric_limits<uint64_t>::max();
	const auto stroke = SinglePointStroke(
		AshEngine::VegetationBrushMode::Paint,
		exhausted.palette[0].species_id,
		VegetationTest::SurfaceRequest(0.5, 0.5),
		250,
		17,
		255);

	AshEngine::VegetationLayerWorkingSet brush_working(SharedSnapshot(exhausted));
	const auto brush_before = brush_working.publish_snapshot();
	const auto brush = AshEngine::apply_vegetation_brush_stroke(brush_working, stroke);
	CHECK(brush.status == AshEngine::VegetationMutationStatus::Rejected);
	CHECK(brush.new_generation == std::numeric_limits<uint64_t>::max());
	CHECK(brush.patch.entries.empty());
	CheckWorkingSetUnchanged(brush_working, brush_before);

	AshEngine::VegetationLayerWorkingSet palette_working(SharedSnapshot(exhausted));
	const auto palette_before = palette_working.publish_snapshot();
	AshEngine::VegetationPaletteEdit add{};
	add.mode = AshEngine::VegetationPaletteEditMode::Add;
	add.replacement = PaletteEntry(
		33, 0x6b, "vegetation/Phase2SecondSpecies.AshVegetation");
	const auto palette = AshEngine::apply_vegetation_palette_edit(palette_working, add);
	CHECK(palette.status == AshEngine::VegetationMutationStatus::Rejected);
	CHECK(palette.new_generation == std::numeric_limits<uint64_t>::max());
	CHECK(palette.patch.entries.empty());
	CheckWorkingSetUnchanged(palette_working, palette_before);

	AshEngine::VegetationLayerSnapshot normal = EmptyLayer();
	AshEngine::VegetationLayerWorkingSet producer(SharedSnapshot(normal));
	const auto produced = AshEngine::apply_vegetation_brush_stroke(producer, stroke);
	REQUIRE(produced.status == AshEngine::VegetationMutationStatus::Applied);

	AshEngine::VegetationLayerSnapshot exhausted_before = normal;
	exhausted_before.content_generation = std::numeric_limits<uint64_t>::max();
	AshEngine::VegetationLayerWorkingSet apply_working(SharedSnapshot(exhausted_before));
	const auto apply_before = apply_working.publish_snapshot();
	CHECK(AshEngine::apply_vegetation_layer_patch(
		apply_working, produced.patch, apply_working.content_generation()) ==
		AshEngine::VegetationPatchApplyStatus::GenerationExhausted);
	CheckWorkingSetUnchanged(apply_working, apply_before);

	AshEngine::VegetationLayerSnapshot exhausted_after = *producer.publish_snapshot();
	exhausted_after.content_generation = std::numeric_limits<uint64_t>::max();
	AshEngine::VegetationLayerWorkingSet revert_working(SharedSnapshot(exhausted_after));
	const auto revert_before = revert_working.publish_snapshot();
	CHECK(AshEngine::revert_vegetation_layer_patch(
		revert_working, produced.patch, revert_working.content_generation()) ==
		AshEngine::VegetationPatchApplyStatus::GenerationExhausted);
	CheckWorkingSetUnchanged(revert_working, revert_before);
}

TEST_CASE("Vegetation brush pure Asset mutation succeeds without a surface provider dependency")
{
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(EmptyLayer()));
	const auto before_generation = working.content_generation();
	const auto result = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			working.publish_snapshot()->palette[0].species_id,
			VegetationTest::SurfaceRequest(0.5, 0.5), 250, 17, 255));
	REQUIRE(result.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(result.new_generation == before_generation + 1);
	CHECK(FindTile(*working.publish_snapshot(), 0, 0) != nullptr);
}

TEST_CASE("Vegetation patch is canonically ordered and stores compressed before and after bytes")
{
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(EmptyLayer()));
	const auto result = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			working.publish_snapshot()->palette[0].species_id,
			VegetationTest::SurfaceRequest(32.0, 32.0), 1000, 255, 255));
	REQUIRE(result.status == AshEngine::VegetationMutationStatus::Applied);
	REQUIRE(result.patch.entries.size() == 8);

	const auto less = [](const auto& lhs, const auto& rhs)
	{
		if (lhs.tile_z != rhs.tile_z) return lhs.tile_z < rhs.tile_z;
		if (lhs.tile_x != rhs.tile_x) return lhs.tile_x < rhs.tile_x;
		if (lhs.plane_kind != rhs.plane_kind)
		{
			return static_cast<uint8_t>(lhs.plane_kind) <
				static_cast<uint8_t>(rhs.plane_kind);
		}
		return lhs.species_id < rhs.species_id;
	};
	CHECK(std::is_sorted(result.patch.entries.begin(), result.patch.entries.end(), less));
	for (const auto& entry : result.patch.entries)
	{
		CHECK(entry.before_bytes.empty());
		CHECK_FALSE(entry.after_bytes.empty());
		CHECK(entry.after_bytes.size() < 1024);
	}
}

TEST_CASE("Vegetation palette Add rejects duplicate path and ID and returns a reusable patch")
{
	const AshEngine::VegetationLayerSnapshot source = EmptyLayer();
	const AshEngine::VegetationPaletteEntry existing = source.palette[0];
	const AshEngine::VegetationPaletteEntry second = PaletteEntry(
		33, 0x6b, "vegetation/Phase2SecondSpecies.AshVegetation");

	auto check_rejected = [&source](const AshEngine::VegetationPaletteEntry& replacement)
	{
		AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));
		const auto before = working.publish_snapshot();
		AshEngine::VegetationPaletteEdit edit{};
		edit.mode = AshEngine::VegetationPaletteEditMode::Add;
		edit.replacement = replacement;
		const auto result = AshEngine::apply_vegetation_palette_edit(working, edit);
		CHECK(result.status == AshEngine::VegetationMutationStatus::Rejected);
		CHECK(result.patch.entries.empty());
		CheckWorkingSetUnchanged(working, before);
	};

	AshEngine::VegetationPaletteEntry duplicate_path = second;
	duplicate_path.species_asset_path = existing.species_asset_path;
	check_rejected(duplicate_path);
	AshEngine::VegetationPaletteEntry duplicate_id = second;
	duplicate_id.species_id = existing.species_id;
	check_rejected(duplicate_id);

	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));
	const auto before = working.publish_snapshot();
	AshEngine::VegetationPaletteEdit add{};
	add.mode = AshEngine::VegetationPaletteEditMode::Add;
	add.replacement = second;
	const auto added = AshEngine::apply_vegetation_palette_edit(working, add);
	REQUIRE(added.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(added.new_generation == before->content_generation + 1);
	REQUIRE(working.publish_snapshot()->palette.size() == 2);
	CHECK(working.publish_snapshot()->palette[0].species_id <
		working.publish_snapshot()->palette[1].species_id);
	const auto add_dirty = working.snapshot_bake_dirty_evidence();
	const auto added_species = std::find_if(
		add_dirty.species_coords.begin(), add_dirty.species_coords.end(),
		[&second](const auto& entry)
		{
			return entry.species_id == second.species_id;
		});
	REQUIRE(added_species != add_dirty.species_coords.end());
	CHECK(added_species->before_coords.empty());
	CHECK(added_species->after_coords.empty());

	const auto after_payload = VegetationTest::CanonicalAuthoringPayloadBytes(
		*working.publish_snapshot());
	const uint64_t revert_generation = working.content_generation();
	AshEngine::revert_vegetation_layer_patch(
		working, added.patch, revert_generation);
	CHECK(working.content_generation() == revert_generation + 1);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working.publish_snapshot()) ==
		VegetationTest::CanonicalAuthoringPayloadBytes(*before));
	const uint64_t apply_generation = working.content_generation();
	AshEngine::apply_vegetation_layer_patch(working, added.patch, apply_generation);
	CHECK(working.content_generation() == apply_generation + 1);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working.publish_snapshot()) ==
		after_payload);
}

TEST_CASE("Vegetation palette Replace requires the same embedded species ID and new path digest")
{
	const AshEngine::VegetationLayerSnapshot source = TwoSpeciesLayer(true);
	const AshEngine::VegetationPaletteEntry existing = source.palette[0];
	AshEngine::VegetationPaletteEntry replacement = existing;
	replacement.species_sha256.fill(0xa7);
	replacement.species_asset_path = "vegetation/Phase2ReplacementSpecies.AshVegetation";

	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));
	AshEngine::VegetationPaletteEdit replace{};
	replace.mode = AshEngine::VegetationPaletteEditMode::Replace;
	replace.target_species_id = existing.species_id;
	replace.replacement = replacement;
	const uint64_t generation = working.content_generation();
	const auto result = AshEngine::apply_vegetation_palette_edit(working, replace);
	REQUIRE(result.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(result.new_generation == generation + 1);
	CHECK(working.publish_snapshot()->palette[0].species_id == existing.species_id);
	CHECK(working.publish_snapshot()->palette[0].species_sha256 == replacement.species_sha256);
	CHECK(working.publish_snapshot()->palette[0].species_asset_path ==
		replacement.species_asset_path);
	const auto dirty = working.snapshot_bake_dirty_evidence();
	const auto dirty_species = std::find_if(
		dirty.species_coords.begin(), dirty.species_coords.end(),
		[&existing](const auto& entry)
		{
			return entry.species_id == existing.species_id;
		});
	REQUIRE(dirty_species != dirty.species_coords.end());
	CHECK(ContainsChunk(dirty_species->before_coords, 0, 0));
	CHECK(ContainsChunk(dirty_species->before_coords, 1, 0));
	CHECK(ContainsChunk(dirty_species->after_coords, 0, 0));
	CHECK(ContainsChunk(dirty_species->after_coords, 1, 0));

	replacement.species_id = VegetationTest::SequentialId(65);
	AshEngine::VegetationLayerWorkingSet rejected_working(SharedSnapshot(source));
	const auto rejected_before = rejected_working.publish_snapshot();
	replace.replacement = replacement;
	const auto rejected = AshEngine::apply_vegetation_palette_edit(rejected_working, replace);
	CHECK(rejected.status == AshEngine::VegetationMutationStatus::Rejected);
	CheckWorkingSetUnchanged(rejected_working, rejected_before);

	replacement = existing;
	replacement.species_sha256.fill(0xa8);
	replacement.species_asset_path = source.palette[1].species_asset_path;
	AshEngine::VegetationLayerWorkingSet duplicate_path_working(SharedSnapshot(source));
	const auto duplicate_path_before = duplicate_path_working.publish_snapshot();
	replace.replacement = replacement;
	const auto duplicate_path = AshEngine::apply_vegetation_palette_edit(
		duplicate_path_working, replace);
	CHECK(duplicate_path.status == AshEngine::VegetationMutationStatus::Rejected);
	CheckWorkingSetUnchanged(duplicate_path_working, duplicate_path_before);
}

TEST_CASE("Vegetation palette Remove requires clear weights and atomically deletes every species plane")
{
	const AshEngine::VegetationLayerSnapshot source = TwoSpeciesLayer(true);
	const AshEngine::VegetationId removed_species = source.palette[0].species_id;
	const AshEngine::VegetationId retained_species = source.palette[1].species_id;
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));

	AshEngine::VegetationPaletteEdit remove{};
	remove.mode = AshEngine::VegetationPaletteEditMode::Remove;
	remove.target_species_id = removed_species;
	remove.clear_weights = false;
	const auto before = working.publish_snapshot();
	const auto rejected = AshEngine::apply_vegetation_palette_edit(working, remove);
	CHECK(rejected.status == AshEngine::VegetationMutationStatus::Rejected);
	CheckWorkingSetUnchanged(working, before);

	remove.clear_weights = true;
	const uint64_t generation = working.content_generation();
	const auto confirmed = AshEngine::apply_vegetation_palette_edit(working, remove);
	REQUIRE(confirmed.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK(confirmed.new_generation == generation + 1);
	const auto removed = working.publish_snapshot();
	REQUIRE(removed->palette.size() == 1);
	CHECK(removed->palette[0].species_id == retained_species);
	for (const auto& tile : removed->tiles)
	{
		CHECK(FindPlane(*removed, tile.tile_x, tile.tile_z,
			AshEngine::VegetationLayerPlaneKind::SpeciesWeight, removed_species) == nullptr);
		CHECK(FindPlane(*removed, tile.tile_x, tile.tile_z,
			AshEngine::VegetationLayerPlaneKind::SpeciesWeight, retained_species) != nullptr);
		CHECK(FindPlane(*removed, tile.tile_x, tile.tile_z,
			AshEngine::VegetationLayerPlaneKind::Density) != nullptr);
	}

	const uint64_t undo_generation = working.content_generation();
	AshEngine::revert_vegetation_layer_patch(working, confirmed.patch, undo_generation);
	CHECK(working.content_generation() == undo_generation + 1);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working.publish_snapshot()) ==
		VegetationTest::CanonicalAuthoringPayloadBytes(source));

	AshEngine::VegetationLayerSnapshot no_weights = TwoSpeciesLayer();
	for (auto& tile : no_weights.tiles)
	{
		tile.planes.erase(
			std::remove_if(
				tile.planes.begin(), tile.planes.end(),
				[&removed_species](const AshEngine::VegetationLayerPlane& plane)
				{
					return plane.kind == AshEngine::VegetationLayerPlaneKind::SpeciesWeight &&
						plane.species_id == removed_species;
				}),
			tile.planes.end());
	}
	AshEngine::VegetationLayerWorkingSet no_weight_working(SharedSnapshot(no_weights));
	remove.clear_weights = false;
	const auto no_weight_remove = AshEngine::apply_vegetation_palette_edit(
		no_weight_working, remove);
	REQUIRE(no_weight_remove.status == AshEngine::VegetationMutationStatus::Applied);
	REQUIRE(no_weight_working.publish_snapshot()->palette.size() == 1u);
	CHECK(no_weight_working.publish_snapshot()->palette[0].species_id == retained_species);
	const auto no_weight_dirty = no_weight_working.snapshot_bake_dirty_evidence();
	const auto no_weight_species = std::find_if(
		no_weight_dirty.species_coords.begin(), no_weight_dirty.species_coords.end(),
		[&removed_species](const auto& entry)
		{
			return entry.species_id == removed_species;
		});
	REQUIRE(no_weight_species != no_weight_dirty.species_coords.end());
	CHECK(no_weight_species->before_coords.empty());
	CHECK(no_weight_species->after_coords.empty());
}

TEST_CASE("Vegetation patch preflights generation shape species and all direction source bytes atomically")
{
	AshEngine::VegetationLayerWorkingSet producer(SharedSnapshot(TwoSpeciesLayer()));
	const auto before = producer.publish_snapshot();
	const auto paint = AshEngine::apply_vegetation_brush_stroke(producer,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			before->palette[0].species_id,
			VegetationTest::SurfaceRequest(0.5, 0.5), 250, 10, 255));
	REQUIRE(paint.status == AshEngine::VegetationMutationStatus::Applied);
	REQUIRE(paint.patch.entries.size() >= 2);
	for (const auto& entry : paint.patch.entries)
	{
		CHECK_FALSE(entry.before_bytes.empty());
		CHECK_FALSE(entry.after_bytes.empty());
		CHECK(entry.before_bytes.size() < 1024);
		CHECK(entry.after_bytes.size() < 1024);
	}
	const auto after = producer.publish_snapshot();

	SUBCASE("expected current generation")
	{
		AshEngine::VegetationLayerWorkingSet working(before);
		const auto unchanged = working.publish_snapshot();
		AshEngine::apply_vegetation_layer_patch(
			working, paint.patch, working.content_generation() + 1);
		CheckWorkingSetUnchanged(working, unchanged);
	}

	SUBCASE("noncanonical working source")
	{
		AshEngine::VegetationLayerSnapshot noncanonical = *before;
		AshEngine::VegetationLayerTile invalid_tile = noncanonical.tiles.front();
		invalid_tile.tile_x = 8;
		for (auto& plane : invalid_tile.planes)
		{
			plane.values.fill(0);
		}
		noncanonical.tiles.push_back(std::move(invalid_tile));
		AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(noncanonical));
		const auto unchanged = working.publish_snapshot();
		CHECK(AshEngine::apply_vegetation_layer_patch(
			working, paint.patch, working.content_generation()) ==
			AshEngine::VegetationPatchApplyStatus::InvalidPatch);
		CHECK(working.publish_snapshot().get() == unchanged.get());
		CHECK(working.content_generation() == unchanged->content_generation);
		const auto dirty = working.snapshot_bake_dirty_evidence();
		CHECK(dirty.density_coords.empty());
		CHECK(dirty.species_coords.empty());
	}

	SUBCASE("direction source shape")
	{
		AshEngine::VegetationLayerPatch malformed = paint.patch;
		REQUIRE_FALSE(malformed.entries.back().before_bytes.empty());
		malformed.entries.back().before_bytes.pop_back();
		AshEngine::VegetationLayerWorkingSet working(before);
		const auto unchanged = working.publish_snapshot();
		AshEngine::apply_vegetation_layer_patch(
			working, malformed, working.content_generation());
		CheckWorkingSetUnchanged(working, unchanged);
	}

	SUBCASE("species membership")
	{
		AshEngine::VegetationLayerPatch malformed = paint.patch;
		malformed.entries.back().species_id = VegetationTest::SequentialId(97);
		AshEngine::VegetationLayerWorkingSet working(before);
		const auto unchanged = working.publish_snapshot();
		AshEngine::apply_vegetation_layer_patch(
			working, malformed, working.content_generation());
		CheckWorkingSetUnchanged(working, unchanged);
	}

	SUBCASE("last apply source byte")
	{
		AshEngine::VegetationLayerPatch tampered = paint.patch;
		REQUIRE_FALSE(tampered.entries.back().before_bytes.empty());
		tampered.entries.back().before_bytes.back() ^= 0xffu;
		AshEngine::VegetationLayerWorkingSet working(before);
		const auto unchanged = working.publish_snapshot();
		CHECK(AshEngine::apply_vegetation_layer_patch(
			working, tampered, working.content_generation()) ==
			AshEngine::VegetationPatchApplyStatus::SourceMismatch);
		CheckWorkingSetUnchanged(working, unchanged);
	}

	SUBCASE("last revert source byte")
	{
		AshEngine::VegetationLayerPatch tampered = paint.patch;
		REQUIRE_FALSE(tampered.entries.back().after_bytes.empty());
		tampered.entries.back().after_bytes.back() ^= 0xffu;
		AshEngine::VegetationLayerWorkingSet working(after);
		const auto unchanged = working.publish_snapshot();
		CHECK(AshEngine::revert_vegetation_layer_patch(
			working, tampered, working.content_generation()) ==
			AshEngine::VegetationPatchApplyStatus::SourceMismatch);
		CheckWorkingSetUnchanged(working, unchanged);
	}

	SUBCASE("late revert failure preserves accumulated dirty evidence")
	{
		AshEngine::VegetationLayerWorkingSet working(before);
		REQUIRE(AshEngine::apply_vegetation_layer_patch(
			working, paint.patch, working.content_generation()) ==
			AshEngine::VegetationPatchApplyStatus::Applied);
		const auto unchanged = working.publish_snapshot();
		const auto dirty_before = working.snapshot_bake_dirty_evidence();

		AshEngine::VegetationLayerPatch tampered = paint.patch;
		REQUIRE_FALSE(tampered.entries.back().after_bytes.empty());
		tampered.entries.back().after_bytes.back() ^= 0xffu;
		CHECK(AshEngine::revert_vegetation_layer_patch(
			working, tampered, working.content_generation()) ==
			AshEngine::VegetationPatchApplyStatus::SourceMismatch);
		CHECK(working.publish_snapshot().get() == unchanged.get());
		CheckDirtyEvidenceEqual(
			working.snapshot_bake_dirty_evidence(), dirty_before);

		AshEngine::VegetationLayerPatch malformed = paint.patch;
		malformed.entries.back().after_bytes.pop_back();
		CHECK(AshEngine::revert_vegetation_layer_patch(
			working, malformed, working.content_generation()) ==
			AshEngine::VegetationPatchApplyStatus::InvalidPatch);
		CHECK(working.publish_snapshot().get() == unchanged.get());
		CheckDirtyEvidenceEqual(
			working.snapshot_bake_dirty_evidence(), dirty_before);
	}
}

TEST_CASE("Vegetation patch apply and revert advance once remove zeros and alternate canonical bytes")
{
	AshEngine::VegetationLayerSnapshot source = TwoSpeciesLayer();
	AshEngine::VegetationLayerWorkingSet producer(SharedSnapshot(source));
	const auto before = producer.publish_snapshot();
	const auto erase = AshEngine::apply_vegetation_brush_stroke(producer,
		SinglePointStroke(AshEngine::VegetationBrushMode::Erase, {},
			VegetationTest::SurfaceRequest(0.5, 0.5), 250, 255, 255));
	REQUIRE(erase.status == AshEngine::VegetationMutationStatus::Applied);
	const auto after = producer.publish_snapshot();
	CHECK(FindTile(*after, 0, 0) == nullptr);

	const auto before_bytes = VegetationTest::CanonicalAuthoringPayloadBytes(*before);
	const auto after_bytes = VegetationTest::CanonicalAuthoringPayloadBytes(*after);
	CHECK(before_bytes != after_bytes);

	AshEngine::VegetationLayerWorkingSet working(before);
	for (size_t cycle = 0; cycle < 3; ++cycle)
	{
		const uint64_t apply_generation = working.content_generation();
		AshEngine::apply_vegetation_layer_patch(
			working, erase.patch, apply_generation);
		CHECK(working.content_generation() == apply_generation + 1);
		CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working.publish_snapshot()) ==
			after_bytes);
		CHECK(FindTile(*working.publish_snapshot(), 0, 0) == nullptr);

		const uint64_t revert_generation = working.content_generation();
		AshEngine::revert_vegetation_layer_patch(
			working, erase.patch, revert_generation);
		CHECK(working.content_generation() == revert_generation + 1);
		CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working.publish_snapshot()) ==
			before_bytes);
	}
}

TEST_CASE("Vegetation patch dirty evidence snapshot is non-destructive across Undo and Redo")
{
	const AshEngine::VegetationLayerSnapshot source = TwoSpeciesLayer(true);
	const AshEngine::VegetationId removed_species = source.palette[0].species_id;
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(source));

	const auto paint = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			removed_species, VegetationTest::SurfaceRequest(0.5, 0.5), 250, 1, 255));
	REQUIRE(paint.status == AshEngine::VegetationMutationStatus::Applied);

	AshEngine::VegetationPaletteEdit remove{};
	remove.mode = AshEngine::VegetationPaletteEditMode::Remove;
	remove.target_species_id = removed_species;
	remove.clear_weights = true;
	const auto removed = AshEngine::apply_vegetation_palette_edit(working, remove);
	REQUIRE(removed.status == AshEngine::VegetationMutationStatus::Applied);

	const uint64_t undo_generation = working.content_generation();
	AshEngine::revert_vegetation_layer_patch(working, removed.patch, undo_generation);
	CHECK(working.content_generation() == undo_generation + 1);
	const uint64_t redo_generation = working.content_generation();
	AshEngine::apply_vegetation_layer_patch(working, removed.patch, redo_generation);
	CHECK(working.content_generation() == redo_generation + 1);

	const auto evidence = working.snapshot_bake_dirty_evidence();
	CHECK(evidence.generation == working.content_generation());
	CHECK(ContainsChunk(evidence.density_coords, 0, 0));
	const auto species = std::find_if(
		evidence.species_coords.begin(), evidence.species_coords.end(),
		[&removed_species](const auto& entry)
		{
			return entry.species_id == removed_species;
		});
	REQUIRE(species != evidence.species_coords.end());
	CHECK(ContainsChunk(species->before_coords, 0, 0));
	CHECK(ContainsChunk(species->before_coords, 1, 0));
	CHECK(ContainsChunk(species->after_coords, 0, 0));
	CHECK(ContainsChunk(species->after_coords, 1, 0));

	const auto observed_payload = VegetationTest::CanonicalAuthoringPayloadBytes(
		*working.publish_snapshot());
	CHECK_FALSE(observed_payload.empty());
	const auto after_snapshot = working.snapshot_bake_dirty_evidence();
	CHECK(after_snapshot.generation == evidence.generation);
	CHECK(after_snapshot.density_coords.size() == evidence.density_coords.size());
	CHECK(after_snapshot.species_coords.size() == evidence.species_coords.size());
}

TEST_CASE("Vegetation patch dirty evidence clears only for the exact captured generation acknowledgement")
{
	AshEngine::VegetationLayerWorkingSet working(SharedSnapshot(EmptyLayer()));
	const AshEngine::VegetationId species = working.publish_snapshot()->palette[0].species_id;
	const auto first = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			species, VegetationTest::SurfaceRequest(0.5, 0.5), 250, 17, 255));
	REQUIRE(first.status == AshEngine::VegetationMutationStatus::Applied);
	const auto captured = working.snapshot_bake_dirty_evidence();
	REQUIRE_FALSE(captured.density_coords.empty());

	CHECK_FALSE(working.acknowledge_bake_dirty_evidence(captured.generation - 1));
	CHECK_FALSE(working.acknowledge_bake_dirty_evidence(
		std::numeric_limits<uint64_t>::max()));
	CHECK(working.snapshot_bake_dirty_evidence().density_coords.size() ==
		captured.density_coords.size());

	const auto later = AshEngine::apply_vegetation_brush_stroke(working,
		SinglePointStroke(AshEngine::VegetationBrushMode::Paint,
			species, VegetationTest::SurfaceRequest(256.5, 0.5), 250, 19, 255));
	REQUIRE(later.status == AshEngine::VegetationMutationStatus::Applied);
	CHECK_FALSE(working.acknowledge_bake_dirty_evidence(captured.generation));
	const auto retained = working.snapshot_bake_dirty_evidence();
	CHECK(ContainsChunk(retained.density_coords, 0, 0));
	CHECK(ContainsChunk(retained.density_coords, 1, 0));

	CHECK(working.acknowledge_bake_dirty_evidence(retained.generation));
	const auto cleared = working.snapshot_bake_dirty_evidence();
	CHECK(cleared.generation == working.content_generation());
	CHECK(cleared.density_coords.empty());
	CHECK(cleared.species_coords.empty());
}
