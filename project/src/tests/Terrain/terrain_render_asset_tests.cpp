#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/TerrainRenderAsset.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
	constexpr size_t k_component_sample_total =
		static_cast<size_t>(AshEngine::k_terrain_component_sample_count) *
		AshEngine::k_terrain_component_sample_count;

	auto MakeComponent(
		AshEngine::TerrainComponentCoord coord,
		uint64_t content_generation) ->
		std::shared_ptr<const AshEngine::TerrainComponentSnapshot>
	{
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>();
		component->coord = coord;
		component->content_generation = content_generation;
		component->sample_width = AshEngine::k_terrain_component_sample_count;
		component->sample_height = AshEngine::k_terrain_component_sample_count;
		component->heights.assign(k_component_sample_total, 0.0f);
		return component;
	}

	auto MakePaintedComponent(
		AshEngine::TerrainComponentCoord coord,
		uint64_t content_generation) ->
		std::shared_ptr<const AshEngine::TerrainComponentSnapshot>
	{
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
			*MakeComponent(coord, content_generation));
		component->weights.assign(
			k_component_sample_total,
			std::array<uint8_t, AshEngine::k_terrain_material_layer_count>{
				255u, 0u, 0u, 0u, 0u, 0u, 0u, 0u });
		return component;
	}

	auto MakeRenderLayout(uint32_t component_count_x, uint32_t component_count_z) ->
		AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x = component_count_x *
			AshEngine::k_terrain_component_quad_count + 1u;
		layout.sample_count_z = component_count_z *
			AshEngine::k_terrain_component_quad_count + 1u;
		layout.component_count_x = component_count_x;
		layout.component_count_z = component_count_z;
		layout.component_quad_count = AshEngine::k_terrain_component_quad_count;
		layout.sample_spacing_meters = 1.0f;
		return layout;
	}

	auto MakeSnapshot(
		uint64_t content_generation,
		const AshEngine::TerrainGridLayout& layout) ->
		std::shared_ptr<AshEngine::TerrainAssetSnapshot>
	{
		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
		snapshot->asset_id = 77u;
		snapshot->layout = layout;
		snapshot->height_mapping = { 0.0f, 100.0f };
		snapshot->content_generation = content_generation;
		snapshot->components.resize(
			static_cast<size_t>(snapshot->layout.component_count_x) *
			snapshot->layout.component_count_z);
		return snapshot;
	}

	auto MakeSnapshot(uint64_t content_generation) ->
		std::shared_ptr<AshEngine::TerrainAssetSnapshot>
	{
		return MakeSnapshot(
			content_generation,
			AshEngine::make_default_terrain_grid_layout());
	}

	void FillCompleteSnapshot(
		const std::shared_ptr<AshEngine::TerrainAssetSnapshot>& snapshot)
	{
		for (uint32_t z = 0u; z < snapshot->layout.component_count_z; ++z)
		{
			for (uint32_t x = 0u; x < snapshot->layout.component_count_x; ++x)
			{
				const size_t index = static_cast<size_t>(z) *
					snapshot->layout.component_count_x + x;
				snapshot->components[index] = MakeComponent(
					{ static_cast<uint16_t>(x), static_cast<uint16_t>(z) },
					snapshot->content_generation);
			}
		}
	}
}

namespace AshEngine
{
	struct TerrainRenderAssetCpuTestSeam
	{
		struct PendingUpload
		{
			TerrainComponentCoord coord{};
			uint64_t content_generation = 0u;
			uint64_t residency_revision = 0u;
		};

		static bool complete_front_height_upload(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			if (asset.m_pending_component_uploads.empty())
			{
				return false;
			}

			const TerrainRenderAsset::TerrainGpuComponentUpload upload =
				asset.m_pending_component_uploads.front();
			if (!asset.m_state.mark_component_uploaded_for_snapshot(
					upload.content_generation,
					upload.residency_revision,
					upload.coord))
			{
				return false;
			}
			asset.m_pending_component_uploads.erase(
				asset.m_pending_component_uploads.begin());
			return true;
		}

		static bool complete_front_weight_update(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			if (asset.m_pending_weight_updates.empty())
			{
				return false;
			}

			const TerrainRenderAsset::TerrainGpuComponentUpload upload =
				asset.m_pending_weight_updates.front();
			TerrainRenderAsset::TerrainAtlasSlotMetadata& slot =
				asset.m_frame_boundary_atlas_slots.front();
			slot.coord = upload.coord;
			slot.content_generation = upload.content_generation;
			slot.residency_revision = upload.residency_revision;
			slot.occupied = true;
			asset.m_pending_weight_updates.erase(
				asset.m_pending_weight_updates.begin());
			return true;
		}

		static std::vector<PendingUpload> pending_height_uploads(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			std::vector<PendingUpload> result{};
			result.reserve(asset.m_pending_component_uploads.size());
			for (const TerrainRenderAsset::TerrainGpuComponentUpload& upload :
				asset.m_pending_component_uploads)
			{
				result.push_back({
					upload.coord,
					upload.content_generation,
					upload.residency_revision });
			}
			return result;
		}

		static std::vector<PendingUpload> pending_weight_updates(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			std::vector<PendingUpload> result{};
			result.reserve(asset.m_pending_weight_updates.size());
			for (const TerrainRenderAsset::TerrainGpuComponentUpload& upload :
				asset.m_pending_weight_updates)
			{
				result.push_back({
					upload.coord,
					upload.content_generation,
					upload.residency_revision });
			}
			return result;
		}

		static std::vector<TerrainComponentCoord> pending_implicit_weight_resets(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_pending_implicit_weight_resets;
		}

		static std::vector<TerrainComponentCoord> pending_component_removals(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_pending_component_removals;
		}

		static uint64_t resident_weight_generation(
			const TerrainRenderAsset& asset,
			TerrainComponentCoord coord)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			for (const TerrainRenderAsset::TerrainAtlasSlotMetadata& slot :
				asset.m_frame_boundary_atlas_slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					return slot.content_generation;
				}
			}
			return 0u;
		}

		static uint64_t resident_weight_revision(
			const TerrainRenderAsset& asset,
			TerrainComponentCoord coord)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			for (const TerrainRenderAsset::TerrainAtlasSlotMetadata& slot :
				asset.m_frame_boundary_atlas_slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					return slot.residency_revision;
				}
			}
			return 0u;
		}

		static bool complete_height_upload(
			TerrainRenderAsset& asset,
			const PendingUpload& completion)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto pending = std::find_if(
				asset.m_pending_component_uploads.begin(),
				asset.m_pending_component_uploads.end(),
				[&](const TerrainRenderAsset::TerrainGpuComponentUpload& upload)
				{
					return upload.coord == completion.coord &&
						upload.content_generation == completion.content_generation &&
						upload.residency_revision == completion.residency_revision;
				});
			if (pending == asset.m_pending_component_uploads.end() ||
				!asset.m_state.mark_component_uploaded_for_snapshot(
					completion.content_generation,
					completion.residency_revision,
					completion.coord))
			{
				return false;
			}
			asset.m_pending_component_uploads.erase(pending);
			return true;
		}

		static bool complete_weight_update(
			TerrainRenderAsset& asset,
			const PendingUpload& completion)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			if (asset.m_pending_weight_updates.empty())
			{
				return false;
			}
			const TerrainRenderAsset::TerrainGpuComponentUpload& upload =
				asset.m_pending_weight_updates.front();
			if (!(upload.coord == completion.coord) ||
				upload.content_generation != completion.content_generation ||
				upload.residency_revision != completion.residency_revision)
			{
				return false;
			}
			TerrainRenderAsset::TerrainAtlasSlotMetadata& slot =
				asset.m_frame_boundary_atlas_slots.front();
			slot.coord = upload.coord;
			slot.content_generation = upload.content_generation;
			slot.residency_revision = upload.residency_revision;
			slot.occupied = true;
			asset.m_pending_weight_updates.erase(
				asset.m_pending_weight_updates.begin());
			return true;
		}

		static bool complete_component_removal(
			TerrainRenderAsset& asset,
			TerrainComponentCoord coord,
			uint64_t content_generation,
			uint64_t residency_revision)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			if (!asset.m_accepted_snapshot ||
				asset.m_accepted_snapshot->content_generation != content_generation ||
				asset.m_accepted_snapshot->residency_revision != residency_revision)
			{
				return false;
			}
			const auto pending = std::find(
				asset.m_pending_component_removals.begin(),
				asset.m_pending_component_removals.end(),
				coord);
			if (pending == asset.m_pending_component_removals.end() ||
				!asset.m_state.mark_component_uploaded_for_snapshot(
					content_generation, residency_revision, coord))
			{
				return false;
			}
			asset.m_pending_component_removals.erase(pending);
			return true;
		}

		static void install_layout_dependent_resource_sentinels(
			TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			asset.m_packed_height_buffer = std::shared_ptr<StorageBuffer>(
				reinterpret_cast<StorageBuffer*>(uintptr_t{ 1u }),
				[](StorageBuffer*) {});
			asset.m_coarse_weight_target = std::shared_ptr<RenderTarget>(
				reinterpret_cast<RenderTarget*>(uintptr_t{ 1u }),
				[](RenderTarget*) {});
		}

		static bool publish_active_snapshot(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_state.publish_snapshot(
				asset.m_state.active_content_generation(),
				asset.m_state.m_active_residency_revision);
		}
	};
}

TEST_CASE("Terrain render asset publishes only the newest completed content generation")
{
	AshEngine::TerrainRenderAssetState state{};
	state.begin_content_generation(7u, 2u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Pending);
	CHECK(state.mark_component_uploaded(7u, { 0u, 0u }));
	CHECK_FALSE(state.publish_content_generation(7u));

	state.begin_content_generation(8u, 1u);
	CHECK_FALSE(state.mark_component_uploaded(7u, { 1u, 0u }));
	CHECK(state.mark_component_uploaded(8u, { 0u, 0u }));
	CHECK_FALSE(state.mark_component_uploaded(8u, { 0u, 0u }));
	CHECK_FALSE(state.mark_component_uploaded(8u, { 32u, 0u }));
	CHECK(state.publish_content_generation(8u));
	CHECK(state.published_content_generation() == 8u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Ready);
}

TEST_CASE("Terrain render asset failure persists until a newer generation succeeds")
{
	AshEngine::TerrainRenderAssetState state{};
	state.begin_content_generation(8u, 1u);
	state.mark_failed(8u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Failed);
	CHECK_FALSE(state.publish_content_generation(8u));

	state.begin_content_generation(8u, 1u);
	state.begin_content_generation(7u, 1u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Failed);

	state.begin_content_generation(9u, 1u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Pending);
	CHECK(state.mark_component_uploaded(9u, { 31u, 31u }));
	CHECK(state.publish_content_generation(9u));
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Ready);
	CHECK(state.published_content_generation() == 9u);
}

TEST_CASE("Terrain render layout derives rectangular resource sizes")
{
	struct ExpectedLayout
	{
		uint32_t component_count_x = 0u;
		uint32_t component_count_z = 0u;
		uint32_t component_count = 0u;
		uint64_t height_buffer_bytes = 0u;
		uint32_t coarse_width = 0u;
		uint32_t coarse_height = 0u;
		AshEngine::TerrainComponentCoord last_coord{};
		size_t last_index = 0u;
	};
	const std::array<ExpectedLayout, 5> expected_layouts =
	{
		ExpectedLayout{ 1u, 1u, 1u, 132100ull, 33u, 33u, { 0u, 0u }, 0u },
		ExpectedLayout{ 1u, 32u, 32u, 4227200ull, 33u, 1025u, { 0u, 31u }, 31u },
		ExpectedLayout{ 8u, 8u, 64u, 8454400ull, 257u, 257u, { 7u, 7u }, 63u },
		ExpectedLayout{ 8u, 16u, 128u, 16908800ull, 257u, 513u, { 7u, 15u }, 127u },
		ExpectedLayout{ 32u, 32u, 1024u, 135270400ull, 1025u, 1025u, { 31u, 31u }, 1023u }
	};

	for (const ExpectedLayout& expected : expected_layouts)
	{
		CAPTURE(expected.component_count_x);
		CAPTURE(expected.component_count_z);
		const AshEngine::TerrainGridLayout layout = MakeRenderLayout(
			expected.component_count_x, expected.component_count_z);
		AshEngine::TerrainRenderLayoutInfo info{};
		std::string error = "stale";
		REQUIRE(AshEngine::derive_terrain_render_layout(layout, info, &error));
		CHECK(error.empty());
		CHECK(info.layout.sample_count_x == layout.sample_count_x);
		CHECK(info.layout.sample_count_z == layout.sample_count_z);
		CHECK(info.layout.component_count_x == expected.component_count_x);
		CHECK(info.layout.component_count_z == expected.component_count_z);
		CHECK(info.component_count == expected.component_count);
		CHECK(info.component_row_stride == expected.component_count_x);
		CHECK(info.height_buffer_bytes == expected.height_buffer_bytes);
		CHECK(info.coarse_width == expected.coarse_width);
		CHECK(info.coarse_height == expected.coarse_height);
		CHECK(info.contains(expected.last_coord));
		CHECK(info.component_linear_index(expected.last_coord) ==
			expected.last_index);
		CHECK_FALSE(info.contains({
			static_cast<uint16_t>(expected.component_count_x), 0u }));
		CHECK_FALSE(info.contains({
			0u, static_cast<uint16_t>(expected.component_count_z) }));
	}
}

TEST_CASE("Terrain render layout fails closed with precise diagnostics")
{
	const auto CheckRejected = [](
		const AshEngine::TerrainGridLayout& layout,
		const std::string& reason,
		const std::string& spacing)
	{
		AshEngine::TerrainRenderLayoutInfo info{};
		info.component_count = 99u;
		info.component_row_stride = 77u;
		info.height_buffer_bytes = 55u;
		std::string error{};
		CHECK_FALSE(AshEngine::derive_terrain_render_layout(layout, info, &error));
		CHECK(error.find(
			"samples=" + std::to_string(layout.sample_count_x) + "x" +
			std::to_string(layout.sample_count_z)) != std::string::npos);
		CHECK(error.find(
			"components=" + std::to_string(layout.component_count_x) + "x" +
			std::to_string(layout.component_count_z)) != std::string::npos);
		CHECK(error.find(
			"quads=" + std::to_string(layout.component_quad_count)) !=
			std::string::npos);
		CHECK(error.find("spacing=" + spacing) != std::string::npos);
		CHECK(error.find("reason=" + reason) != std::string::npos);
		CHECK(info.component_count == 99u);
		CHECK(info.component_row_stride == 77u);
		CHECK(info.height_buffer_bytes == 55u);
	};

	AshEngine::TerrainGridLayout zero_x = MakeRenderLayout(0u, 1u);
	CheckRejected(
		zero_x,
		"component_count_x must be in [1, 32].",
		"1");

	AshEngine::TerrainGridLayout too_many_z = MakeRenderLayout(1u, 33u);
	CheckRejected(
		too_many_z,
		"component_count_z must be in [1, 32].",
		"1");

	AshEngine::TerrainGridLayout wrong_quads = MakeRenderLayout(1u, 1u);
	wrong_quads.component_quad_count = 255u;
	CheckRejected(
		wrong_quads,
		"component_quad_count must equal 256.",
		"1");

	AshEngine::TerrainGridLayout non_finite_spacing = MakeRenderLayout(1u, 1u);
	non_finite_spacing.sample_spacing_meters =
		std::numeric_limits<float>::infinity();
	CheckRejected(
		non_finite_spacing,
		"sample_spacing_meters must be finite.",
		"inf");

	AshEngine::TerrainGridLayout zero_spacing = MakeRenderLayout(1u, 1u);
	zero_spacing.sample_spacing_meters = 0.0f;
	CheckRejected(
		zero_spacing,
		"sample_spacing_meters must be greater than zero.",
		"0");

	AshEngine::TerrainGridLayout wrong_spacing = MakeRenderLayout(1u, 1u);
	wrong_spacing.sample_spacing_meters = 0.5f;
	CheckRejected(
		wrong_spacing,
		"sample_spacing_meters must equal 1.",
		"0.5");

	AshEngine::TerrainGridLayout wrong_samples = MakeRenderLayout(8u, 16u);
	--wrong_samples.sample_count_x;
	CheckRejected(
		wrong_samples,
		"sample counts must equal component counts * 256 + 1 "
		"(expected samples=2049x4097).",
		"1");

	AshEngine::TerrainGridLayout huge_components{};
	huge_components.sample_count_x = std::numeric_limits<uint32_t>::max();
	huge_components.sample_count_z = std::numeric_limits<uint32_t>::max();
	huge_components.component_count_x = std::numeric_limits<uint32_t>::max();
	huge_components.component_count_z = std::numeric_limits<uint32_t>::max();
	CheckRejected(
		huge_components,
		"component_count_x must be in [1, 32].",
		"1");

	AshEngine::TerrainRenderLayoutInfo maximum{};
	REQUIRE(AshEngine::derive_terrain_render_layout(
		MakeRenderLayout(32u, 32u), maximum));
	CHECK(maximum.height_buffer_bytes <=
		std::numeric_limits<uint32_t>::max());
	CHECK(maximum.coarse_width <= std::numeric_limits<uint16_t>::max());
	CHECK(maximum.coarse_height <= std::numeric_limits<uint16_t>::max());
}

TEST_CASE("Terrain render layout validates rectangular snapshot shape")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(8u, 16u);

	SUBCASE("component table size is exact")
	{
		auto snapshot = MakeSnapshot(1u, layout);
		snapshot->components.pop_back();
		AshEngine::TerrainRenderAsset asset{};
		std::string error{};
		CHECK_FALSE(asset.accept_snapshot(snapshot, &error));
		CHECK(error.find("component table contains 127 entries; expected 128.") !=
			std::string::npos);
		CHECK(error.find("components=8x16") != std::string::npos);
	}

	SUBCASE("non-null components match their dense row-major slots")
	{
		auto snapshot = MakeSnapshot(1u, layout);
		snapshot->components[127] = MakeComponent({ 6u, 15u }, 1u);
		AshEngine::TerrainRenderAsset asset{};
		std::string error{};
		CHECK_FALSE(asset.accept_snapshot(snapshot, &error));
		CHECK(error.find(
			"component at row-major slot 127 has coord=(6,15); expected=(7,15).") !=
			std::string::npos);
	}

	SUBCASE("first load may be sparse")
	{
		auto snapshot = MakeSnapshot(1u, layout);
		snapshot->components[127] = MakeComponent({ 7u, 15u }, 1u);
		AshEngine::TerrainRenderAsset asset{};
		std::string error{};
		REQUIRE(asset.accept_snapshot(snapshot, &error));
		CHECK(error.empty());
		CHECK(asset.pending_component_upload_count() == 1u);
		CHECK(asset.has_pending_component_upload({ 7u, 15u }));
		CHECK_FALSE(asset.has_pending_component_upload({ 8u, 15u }));
	}
}

TEST_CASE("Terrain render layout rejects incomplete replacements without index pollution")
{
	const AshEngine::TerrainGridLayout initial_layout = MakeRenderLayout(1u, 1u);
	auto initial = MakeSnapshot(7u, initial_layout);
	initial->components[0] = MakeComponent({ 0u, 0u }, 7u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(initial, &error));
	REQUIRE(asset.accepted_snapshot() == initial);
	REQUIRE(asset.pending_component_upload_count() == 1u);

	SUBCASE("asset replacement cannot reset generation with a null table entry")
	{
		auto replacement = MakeSnapshot(1u, initial_layout);
		replacement->asset_id = 78u;
		CHECK_FALSE(asset.accept_snapshot(replacement, &error));
		CHECK(error.find(
			"replacement snapshot has a null component at row-major slot 0.") !=
			std::string::npos);
		CHECK(asset.accepted_snapshot() == initial);
		CHECK(asset.pending_component_upload_count() == 1u);
		CHECK(asset.has_pending_component_upload({ 0u, 0u }));
	}

	SUBCASE("complete asset replacement resets CPU generation identity")
	{
		auto replacement = MakeSnapshot(1u, initial_layout);
		replacement->asset_id = 78u;
		replacement->components[0] = MakeComponent({ 0u, 0u }, 1u);
		REQUIRE(asset.accept_snapshot(replacement, &error));
		CHECK(asset.accepted_snapshot() == replacement);
		CHECK(asset.pending_component_upload_count() == 1u);
		CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
			complete_front_height_upload(asset));
	}

	SUBCASE("layout replacement validates row-major order before indexing")
	{
		const AshEngine::TerrainGridLayout replacement_layout =
			MakeRenderLayout(8u, 16u);
		auto replacement = MakeSnapshot(8u, replacement_layout);
		const auto repeated_component = MakeComponent({ 0u, 0u }, 8u);
		std::fill(
			replacement->components.begin(),
			replacement->components.end(),
			repeated_component);
		CHECK_FALSE(asset.accept_snapshot(replacement, &error));
		CHECK(error.find(
			"component at row-major slot 1 has coord=(0,0); expected=(1,0).") !=
			std::string::npos);
		CHECK(asset.accepted_snapshot() == initial);
		CHECK(asset.pending_component_upload_count() == 1u);
		CHECK(asset.has_pending_component_upload({ 0u, 0u }));
		CHECK_FALSE(asset.has_pending_component_upload({ 7u, 15u }));
	}
}

TEST_CASE("Terrain render asset keeps active resources when a different layout arrives")
{
	const AshEngine::TerrainGridLayout initial_layout = MakeRenderLayout(1u, 1u);
	auto initial = MakeSnapshot(1u, initial_layout);
	initial->components[0] = MakeComponent({ 0u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(initial, &error));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		publish_active_snapshot(asset));
	AshEngine::TerrainRenderAssetCpuTestSeam::
		install_layout_dependent_resource_sentinels(asset);
	const auto old_height = asset.packed_height_buffer();
	const auto old_coarse = asset.coarse_weight_target();
	const AshEngine::TerrainShadowCasterIdentity old_state =
		asset.snapshot_shadow_caster_identity();

	auto malformed = MakeSnapshot(2u, MakeRenderLayout(8u, 16u));
	FillCompleteSnapshot(malformed);
	malformed->components[1] = malformed->components[0];
	CHECK_FALSE(asset.accept_snapshot(malformed, &error));
	CHECK(error.find(
		"component at row-major slot 1 has coord=(0,0); expected=(1,0).") !=
		std::string::npos);
	CHECK(asset.accepted_snapshot() == initial);

	auto replacement = MakeSnapshot(2u, MakeRenderLayout(8u, 16u));
	FillCompleteSnapshot(replacement);
	CHECK_FALSE(asset.accept_snapshot(replacement, &error));
	CHECK(error.find("layout-dependent GPU resources") != std::string::npos);
	CHECK(asset.accepted_snapshot() == initial);
	CHECK(asset.packed_height_buffer() == old_height);
	CHECK(asset.coarse_weight_target() == old_coarse);
	CHECK(asset.snapshot_shadow_caster_identity().accepted_snapshot_identity ==
		old_state.accepted_snapshot_identity);
	CHECK(asset.snapshot_shadow_caster_identity().readiness ==
		AshEngine::TerrainRenderReadiness::Ready);
	CHECK(asset.pending_component_upload_count() == 0u);
	CHECK(asset.pending_component_removal_count() == 0u);
}

TEST_CASE("Terrain render asset retains its resource layout guard after a same-layout failure")
{
	const AshEngine::TerrainGridLayout initial_layout = MakeRenderLayout(1u, 1u);
	auto initial = MakeSnapshot(1u, initial_layout);
	initial->components[0] = MakeComponent({ 0u, 0u }, 1u);
	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(initial, &error));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::publish_active_snapshot(asset));
	AshEngine::TerrainRenderAssetCpuTestSeam::
		install_layout_dependent_resource_sentinels(asset);
	const auto old_height = asset.packed_height_buffer();
	const auto old_coarse = asset.coarse_weight_target();

	auto failed = MakeSnapshot(2u, initial_layout);
	failed->failed = true;
	failed->failure_detail = "same-layout decode failed";
	CHECK_FALSE(asset.accept_snapshot(failed, &error));
	REQUIRE(asset.accepted_snapshot() == failed);

	auto replacement = MakeSnapshot(3u, MakeRenderLayout(8u, 16u));
	FillCompleteSnapshot(replacement);
	CHECK_FALSE(asset.accept_snapshot(replacement, &error));
	CHECK(error.find("layout-dependent GPU resources") != std::string::npos);
	CHECK(asset.accepted_snapshot() == failed);
	CHECK(asset.packed_height_buffer() == old_height);
	CHECK(asset.coarse_weight_target() == old_coarse);
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Failed);
}

TEST_CASE("Terrain render asset orders same-asset snapshots by generation and residency revision")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto initial = MakeSnapshot(5u, layout);
	initial->residency_revision = 3u;
	initial->components[0] = MakeComponent({ 0u, 0u }, 5u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(initial, &error));

	auto newer_residency = MakeSnapshot(5u, layout);
	newer_residency->residency_revision = 4u;
	newer_residency->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(asset.accept_snapshot(newer_residency, &error));
	CHECK(asset.accepted_snapshot() == newer_residency);
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Pending);
	CHECK_FALSE(asset.accept_snapshot(newer_residency, &error));

	const auto accepted = asset.accepted_snapshot();
	const AshEngine::TerrainShadowCasterIdentity accepted_state =
		asset.snapshot_shadow_caster_identity();
	for (uint64_t generation : { 5u, 4u })
	{
		auto stale = MakeSnapshot(generation, layout);
		stale->residency_revision = generation == 5u ? 4u : 99u;
		stale->components[0] = MakeComponent({ 0u, 0u }, generation);
		CHECK_FALSE(asset.accept_snapshot(stale, &error));
		CHECK(asset.accepted_snapshot() == accepted);
		CHECK(asset.snapshot_shadow_caster_identity().accepted_snapshot_identity ==
			accepted_state.accepted_snapshot_identity);
		CHECK(asset.pending_component_upload_count() == 1u);
	}
}

TEST_CASE("Terrain render asset isolates completions across residency revisions")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 3u);
	auto revision_3 = MakeSnapshot(5u, layout);
	revision_3->residency_revision = 3u;
	revision_3->components[0] = MakePaintedComponent({ 0u, 0u }, 5u);
	revision_3->components[1] = MakeComponent({ 0u, 1u }, 5u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(revision_3, &error));
	const auto old_heights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	const auto old_weights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset);
	REQUIRE(old_heights.size() == 2u);
	REQUIRE(old_weights.size() == 1u);
	CHECK(old_heights.front().residency_revision == 3u);
	CHECK(old_weights.front().residency_revision == 3u);

	auto revision_4 = MakeSnapshot(5u, layout);
	revision_4->residency_revision = 4u;
	revision_4->components[0] = MakePaintedComponent({ 0u, 0u }, 5u);
	revision_4->components[2] = MakeComponent({ 0u, 2u }, 5u);
	REQUIRE(asset.accept_snapshot(revision_4, &error));
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Pending);
	CHECK(asset.pending_component_upload_count() == 2u);
	CHECK(asset.pending_component_removal_count() == 1u);
	CHECK(asset.pending_weight_update_count() == 1u);
	CHECK(asset.snapshot_shadow_caster_identity().required_upload_count == 3u);
	CHECK(asset.snapshot_shadow_caster_identity().completed_upload_count == 0u);

	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_height_upload(asset, old_heights.front()));
	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_weight_update(asset, old_weights.front()));
	CHECK(asset.snapshot_shadow_caster_identity().completed_upload_count == 0u);
	CHECK(asset.pending_component_upload_count() == 2u);
	CHECK(asset.pending_weight_update_count() == 1u);

	const auto current_heights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	const auto current_weights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset);
	REQUIRE(current_heights.size() == 2u);
	REQUIRE(current_weights.size() == 1u);
	CHECK(current_heights.front().residency_revision == 4u);
	CHECK(current_weights.front().residency_revision == 4u);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_height_upload(asset, current_heights[0]));
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_height_upload(asset, current_heights[1]));
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::complete_component_removal(
		asset, { 0u, 1u }, 5u, 4u));
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_weight_update(asset, current_weights.front()));
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::resident_weight_revision(
		asset, { 0u, 0u }) == 4u);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::publish_active_snapshot(asset));
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Ready);
	CHECK(asset.accepted_snapshot() == revision_4);
}

TEST_CASE("Terrain render layout resets failed asset identity before recovery")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	AshEngine::TerrainRenderAsset asset{};
	std::string error{};

	auto first_failure = MakeSnapshot(10u, layout);
	--first_failure->layout.sample_count_x;
	CHECK_FALSE(asset.accept_snapshot(first_failure, &error));
	CHECK(asset.accepted_content_generation() == 10u);

	auto replacement_failure = MakeSnapshot(1u, layout);
	replacement_failure->asset_id = 78u;
	replacement_failure->failed = true;
	replacement_failure->failure_detail = "replacement decode failed";
	CHECK_FALSE(asset.accept_snapshot(replacement_failure, &error));
	CHECK(error == "replacement decode failed");
	CHECK(asset.accepted_content_generation() == 1u);

	auto recovered = MakeSnapshot(2u, layout);
	recovered->asset_id = 78u;
	recovered->components[0] = MakeComponent({ 0u, 0u }, 2u);
	REQUIRE(asset.accept_snapshot(recovered, &error));
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(asset));
}

TEST_CASE("Terrain render asset packs R16 heights and exact eight-lane weights")
{
	AshEngine::TerrainComponentSnapshot component{};
	component.coord = { 0u, 0u };
	component.content_generation = 1u;
	component.sample_width = AshEngine::k_terrain_component_sample_count;
	component.sample_height = AshEngine::k_terrain_component_sample_count;
	component.heights.assign(k_component_sample_total, 0.0f);
	component.heights[0] = 0.0f;
	component.heights[1] = 50.0f;
	component.heights[2] = 100.0f;
	component.weights.assign(
		k_component_sample_total,
		std::array<uint8_t, AshEngine::k_terrain_material_layer_count>{
			1u, 2u, 3u, 4u, 5u, 6u, 7u, 227u });

	std::vector<uint32_t> height_words{};
	std::array<std::vector<uint8_t>, 2> weight_rgba8{};
	std::string error{};
	REQUIRE(AshEngine::build_terrain_component_gpu_data(
		component,
		{ 0.0f, 100.0f },
		height_words,
		weight_rgba8,
		&error));
	CHECK(error.empty());
	REQUIRE(height_words.size() == (k_component_sample_total + 1u) / 2u);
	CHECK(height_words[0] == 0x80000000u);
	CHECK(height_words[1] == 0x0000FFFFu);
	CHECK((height_words.back() & 0xFFFF0000u) == 0u);
	REQUIRE(weight_rgba8[0].size() == k_component_sample_total * 4u);
	REQUIRE(weight_rgba8[1].size() == k_component_sample_total * 4u);
	CHECK(weight_rgba8[0][0] == 1u);
	CHECK(weight_rgba8[0][1] == 2u);
	CHECK(weight_rgba8[0][2] == 3u);
	CHECK(weight_rgba8[0][3] == 4u);
	CHECK(weight_rgba8[1][0] == 5u);
	CHECK(weight_rgba8[1][1] == 6u);
	CHECK(weight_rgba8[1][2] == 7u);
	CHECK(weight_rgba8[1][3] == 227u);

	component.weights.clear();
	REQUIRE(AshEngine::build_terrain_component_gpu_data(
		component,
		{ 0.0f, 100.0f },
		height_words,
		weight_rgba8,
		&error));
	bool implicit_weights_are_layer_zero = true;
	for (size_t sample = 0u; sample < k_component_sample_total; ++sample)
	{
		const size_t offset = sample * 4u;
		implicit_weights_are_layer_zero = implicit_weights_are_layer_zero &&
			weight_rgba8[0][offset] == 255u &&
			weight_rgba8[0][offset + 1u] == 0u &&
			weight_rgba8[0][offset + 2u] == 0u &&
			weight_rgba8[0][offset + 3u] == 0u &&
			weight_rgba8[1][offset] == 0u &&
			weight_rgba8[1][offset + 1u] == 0u &&
			weight_rgba8[1][offset + 2u] == 0u &&
			weight_rgba8[1][offset + 3u] == 0u;
	}
	CHECK(implicit_weights_are_layer_zero);
}

TEST_CASE("Terrain render asset rejects malformed component upload data")
{
	AshEngine::TerrainComponentSnapshot component = *MakeComponent({ 0u, 0u }, 1u);
	std::vector<uint32_t> height_words{};
	std::array<std::vector<uint8_t>, 2> weight_rgba8{};
	std::string error{};

	component.sample_width = 256u;
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component dimensions must be 257 x 257.");

	component.sample_width = AshEngine::k_terrain_component_sample_count;
	component.weights.resize(1u);
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component weight count must be zero or match the sample count.");

	component.weights.assign(k_component_sample_total, {});
	component.weights[0][0] = 254u;
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component weights must sum to 255 for every sample.");

	component.weights.clear();
	component.heights[0] = std::numeric_limits<float>::infinity();
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component heights must be finite.");
}

TEST_CASE("Terrain render asset bounds pending CPU payload")
{
	auto snapshot = MakeSnapshot(1u);
	snapshot->components[0] = MakeComponent({ 0u, 0u }, 1u);
	snapshot->components[1] = MakeComponent({ 1u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(snapshot, &error));
	CHECK(asset.pending_component_upload_count() == 2u);
	CHECK(asset.pending_weight_update_count() == 0u);
	CHECK(asset.pending_cpu_payload_bytes() <=
		static_cast<uint64_t>(
			AshEngine::k_terrain_render_height_words_per_component) *
			sizeof(uint32_t));
	CHECK(asset.pending_weight_payload_bytes() == 0u);
}

TEST_CASE("Terrain render asset leaves empty weights implicit")
{
	const std::shared_ptr<const AshEngine::TerrainComponentSnapshot> component =
		MakeComponent({ 0u, 0u }, 1u);
	std::array<std::vector<uint8_t>, 2> weights{};
	std::string error{};
	REQUIRE(AshEngine::build_terrain_component_weight_rgba8(
		*component, weights, &error));
	CHECK(error.empty());
	CHECK(weights[0].empty());
	CHECK(weights[1].empty());

	auto painted_snapshot = MakeSnapshot(2u);
	auto painted_component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*MakeComponent({ 0u, 0u }, 2u));
	painted_component->weights.assign(
		k_component_sample_total,
		std::array<uint8_t, AshEngine::k_terrain_material_layer_count>{
			255u, 0u, 0u, 0u, 0u, 0u, 0u, 0u });
	painted_snapshot->components[0] = painted_component;
	AshEngine::TerrainRenderAsset painted_asset{};
	REQUIRE(painted_asset.accept_snapshot(painted_snapshot, &error));
	CHECK(painted_asset.pending_weight_update_count() == 1u);
	CHECK(painted_asset.pending_weight_payload_bytes() == 0u);
}

TEST_CASE("Terrain upload work budget is byte and wall clock bounded")
{
	using namespace std::chrono_literals;
	constexpr uint64_t component_bytes =
		static_cast<uint64_t>(
			AshEngine::k_terrain_render_height_words_per_component) *
			sizeof(uint32_t);
	constexpr uint64_t byte_budget = component_bytes * 4u;
	constexpr auto wall_clock_budget = 2ms;

	CHECK(AshEngine::terrain_upload_budget_allows_next(
		0u, component_bytes, byte_budget, 0ns, wall_clock_budget));
	CHECK_FALSE(AshEngine::terrain_upload_budget_allows_next(
		byte_budget, component_bytes, byte_budget, 1ms, wall_clock_budget));
	CHECK_FALSE(AshEngine::terrain_upload_budget_allows_next(
		component_bytes, component_bytes, byte_budget,
		wall_clock_budget, wall_clock_budget));
}

TEST_CASE("Terrain render asset keeps unfinished pointer-equal uploads across generations")
{
	auto first = MakeSnapshot(1u);
	first->components[0] = MakeComponent({ 0u, 0u }, 1u);
	first->components[1] = MakeComponent({ 1u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(first, &error));
	CHECK(asset.pending_component_upload_count() == 2u);
	CHECK(asset.has_pending_component_upload({ 0u, 0u }));
	CHECK(asset.has_pending_component_upload({ 1u, 0u }));

	auto second = MakeSnapshot(2u);
	second->components[0] = first->components[0];
	second->components[1] = MakeComponent({ 1u, 0u }, 2u);
	REQUIRE(asset.accept_snapshot(second, &error));
	CHECK(asset.pending_component_upload_count() == 2u);
	CHECK(asset.has_pending_component_upload({ 0u, 0u }));
	CHECK(asset.has_pending_component_upload({ 1u, 0u }));

	auto third = MakeSnapshot(3u);
	third->components[1] = second->components[1];
	REQUIRE(asset.accept_snapshot(third, &error));
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK(asset.has_pending_component_upload({ 1u, 0u }));
	CHECK(asset.pending_component_removal_count() == 1u);
	CHECK(asset.has_pending_component_removal({ 0u, 0u }));

	CHECK_FALSE(asset.accept_snapshot(first, &error));
	CHECK(error == "terrain snapshot content generation is stale.");
	CHECK(asset.pending_component_removal_count() == 1u);

	auto failed = MakeSnapshot(4u);
	failed->failed = true;
	failed->failure_detail = "terrain decode failed";
	failed->components.clear();
	CHECK_FALSE(asset.accept_snapshot(failed, &error));
	CHECK(error == "terrain decode failed");
	CHECK_FALSE(asset.accept_snapshot(failed, &error));
	CHECK(error == "terrain decode failed");
	auto recovered = MakeSnapshot(5u);
	recovered->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(asset.accept_snapshot(recovered, &error));
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK(asset.has_pending_component_upload({ 0u, 0u }));
}

TEST_CASE("Terrain render asset coalesces partially completed uploads into a newer generation")
{
	auto first = MakeSnapshot(1u);
	first->components[0] = MakeComponent({ 0u, 0u }, 1u);
	first->components[1] = MakeComponent({ 1u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(first, &error));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::complete_front_height_upload(asset));

	auto second = MakeSnapshot(2u);
	second->components[0] = first->components[0];
	second->components[1] = first->components[1];
	second->components[2] = MakeComponent({ 2u, 0u }, 2u);
	REQUIRE(asset.accept_snapshot(second, &error));

	const auto pending =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	REQUIRE(pending.size() == 2u);
	CHECK(pending[0].coord == AshEngine::TerrainComponentCoord{ 1u, 0u });
	CHECK(pending[0].content_generation == 2u);
	CHECK(pending[1].coord == AshEngine::TerrainComponentCoord{ 2u, 0u });
	CHECK(pending[1].content_generation == 2u);
}

TEST_CASE("Terrain render asset coalesces completed and pending weight work")
{
	auto first = MakeSnapshot(1u);
	first->components[0] = MakePaintedComponent({ 0u, 0u }, 1u);
	first->components[1] = MakePaintedComponent({ 1u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(first, &error));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::complete_front_height_upload(asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::complete_front_weight_update(asset));

	auto second = MakeSnapshot(2u);
	second->components[0] = first->components[0];
	second->components[1] = first->components[1];
	second->components[2] = MakePaintedComponent({ 2u, 0u }, 2u);
	REQUIRE(asset.accept_snapshot(second, &error));

	const auto heights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	REQUIRE(heights.size() == 2u);
	CHECK(heights[0].coord == AshEngine::TerrainComponentCoord{ 1u, 0u });
	CHECK(heights[1].coord == AshEngine::TerrainComponentCoord{ 2u, 0u });

	const auto weights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset);
	REQUIRE(weights.size() == 2u);
	CHECK(weights[0].coord == AshEngine::TerrainComponentCoord{ 1u, 0u });
	CHECK(weights[0].content_generation == 2u);
	CHECK(weights[1].coord == AshEngine::TerrainComponentCoord{ 2u, 0u });
	CHECK(weights[1].content_generation == 2u);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::resident_weight_generation(
		asset, { 0u, 0u }) == 2u);
}

TEST_CASE("Terrain render asset carries resets and removals before appending new work")
{
	auto first = MakeSnapshot(1u);
	first->components[0] = MakePaintedComponent({ 0u, 0u }, 1u);
	first->components[1] = MakePaintedComponent({ 1u, 0u }, 1u);
	first->components[3] = MakePaintedComponent({ 3u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(first, &error));

	auto second = MakeSnapshot(2u);
	second->components[0] = MakeComponent({ 0u, 0u }, 2u);
	second->components[3] = first->components[3];
	REQUIRE(asset.accept_snapshot(second, &error));
	CHECK(asset.has_pending_component_removal({ 1u, 0u }));

	auto third = MakeSnapshot(3u);
	third->components[0] = second->components[0];
	third->components[2] = MakeComponent({ 2u, 0u }, 3u);
	REQUIRE(asset.accept_snapshot(third, &error));

	const auto resets =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_implicit_weight_resets(asset);
	REQUIRE(resets.size() == 1u);
	CHECK(resets[0] == AshEngine::TerrainComponentCoord{ 0u, 0u });

	const auto removals =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_component_removals(asset);
	REQUIRE(removals.size() == 2u);
	CHECK(removals[0] == AshEngine::TerrainComponentCoord{ 1u, 0u });
	CHECK(removals[1] == AshEngine::TerrainComponentCoord{ 3u, 0u });
}

TEST_CASE("Terrain render asset rejects older generations after a malformed snapshot")
{
	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	auto malformed = MakeSnapshot(10u);
	--malformed->layout.sample_count_x;
	CHECK_FALSE(asset.accept_snapshot(malformed, &error));
	CHECK(asset.accepted_content_generation() == 10u);
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Failed);

	auto older = MakeSnapshot(9u);
	CHECK_FALSE(asset.accept_snapshot(older, &error));
	CHECK(error == "terrain snapshot content generation is stale.");
	CHECK(asset.accepted_content_generation() == 10u);

	auto recovered = MakeSnapshot(11u);
	recovered->components[0] = MakeComponent({ 0u, 0u }, 11u);
	REQUIRE(asset.accept_snapshot(recovered, &error));
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK(asset.pending_component_removal_count() ==
		AshEngine::k_terrain_render_component_capacity - 1u);
	const auto recovery_resets =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_implicit_weight_resets(asset);
	REQUIRE(recovery_resets.size() == 1u);
	CHECK(recovery_resets[0] == AshEngine::TerrainComponentCoord{ 0u, 0u });
}

TEST_CASE("Terrain render asset manager counts one pending owner per asset generation")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot = MakeSnapshot(1u);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> first =
		manager.request_terrain_asset("terrain/Test.AshTerrain", snapshot);
	REQUIRE(first != nullptr);
	const AshEngine::RenderAssetReadinessSnapshot first_readiness =
		manager.query_readiness();
	CHECK(first_readiness.pending);
	CHECK_FALSE(first_readiness.failed);
	CHECK(first_readiness.activity_epoch == 1u);

	CHECK(manager.request_terrain_asset("terrain/test.ashterrain", snapshot) == first);
	const AshEngine::RenderAssetReadinessSnapshot duplicate_readiness =
		manager.query_readiness();
	CHECK(duplicate_readiness.pending);
	CHECK(duplicate_readiness.activity_epoch == first_readiness.activity_epoch);

	auto failed = MakeSnapshot(1u);
	failed->failed = true;
	failed->failure_detail = "corrupt terrain";
	CHECK(manager.request_terrain_asset("terrain/Failed.AshTerrain", failed) == nullptr);
	const AshEngine::RenderAssetReadinessSnapshot failed_readiness =
		manager.query_readiness();
	CHECK(failed_readiness.pending);
	CHECK(failed_readiness.failed);
	CHECK(failed_readiness.activity_epoch == 3u);

	manager.shutdown();
}

TEST_CASE("Terrain render asset manager retires a failed pending owner only once")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot = MakeSnapshot(1u);
	snapshot->components[0] = MakeComponent({ 0u, 0u }, 1u);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> asset =
		manager.request_terrain_asset("terrain/WrongThread.AshTerrain", snapshot);
	REQUIRE(asset != nullptr);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));

	const AshEngine::RenderAssetReadinessSnapshot failed = manager.query_readiness();
	CHECK_FALSE(failed.pending);
	CHECK(failed.failed);
	CHECK(failed.activity_epoch == 2u);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));
	CHECK(manager.query_readiness().activity_epoch == failed.activity_epoch);

	auto recovery_snapshot = MakeSnapshot(2u);
	recovery_snapshot->components[0] = snapshot->components[0];
	CHECK(manager.request_terrain_asset(
		"terrain/wrongthread.ashterrain", recovery_snapshot) == asset);
	CHECK(asset->pending_component_upload_count() == 1u);
	CHECK(asset->has_pending_component_upload({ 0u, 0u }));
	const AshEngine::RenderAssetReadinessSnapshot recovered = manager.query_readiness();
	CHECK(recovered.pending);
	CHECK_FALSE(recovered.failed);
	CHECK(recovered.activity_epoch == 3u);
	manager.shutdown();
}

TEST_CASE("Terrain render asset manager reopens pending work for a newer residency revision")
{
	AshEngine::RenderAssetManager manager{};
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto revision_3 = MakeSnapshot(5u, layout);
	revision_3->residency_revision = 3u;
	revision_3->components[0] = MakeComponent({ 0u, 0u }, 5u);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> asset =
		manager.request_terrain_asset("terrain/Revision.AshTerrain", revision_3);
	REQUIRE(asset != nullptr);
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(*asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::publish_active_snapshot(*asset));
	REQUIRE(manager.finalize_pending_terrain_asset(asset));
	const AshEngine::RenderAssetReadinessSnapshot ready = manager.query_readiness();
	REQUIRE_FALSE(ready.pending);

	auto revision_4 = MakeSnapshot(5u, layout);
	revision_4->residency_revision = 4u;
	revision_4->components[0] = MakeComponent({ 0u, 0u }, 5u);
	CHECK(manager.request_terrain_asset(
		"terrain/revision.ashterrain", revision_4) == asset);
	CHECK(asset->accepted_snapshot() == revision_4);
	CHECK(asset->readiness() == AshEngine::TerrainRenderReadiness::Pending);
	const AshEngine::RenderAssetReadinessSnapshot pending = manager.query_readiness();
	CHECK(pending.pending);
	CHECK_FALSE(pending.failed);
	CHECK(pending.activity_epoch == ready.activity_epoch + 1u);
	manager.shutdown();
}

TEST_CASE("Terrain render asset manager advances activity for one pending revision owner")
{
	AshEngine::RenderAssetManager manager{};
	auto revision_3 = MakeSnapshot(5u);
	revision_3->residency_revision = 3u;
	revision_3->components[0] = MakeComponent({ 0u, 0u }, 5u);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> asset =
		manager.request_terrain_asset("terrain/PendingRevision.AshTerrain", revision_3);
	REQUIRE(asset != nullptr);
	const AshEngine::RenderAssetReadinessSnapshot initial = manager.query_readiness();
	REQUIRE(initial.pending);
	REQUIRE(initial.activity_epoch == 1u);

	CHECK(manager.request_terrain_asset(
		"terrain/pendingrevision.ashterrain", revision_3) == asset);
	CHECK(manager.query_readiness().activity_epoch == initial.activity_epoch);

	auto revision_4 = MakeSnapshot(5u);
	revision_4->residency_revision = 4u;
	revision_4->components[0] = MakeComponent({ 0u, 0u }, 5u);
	CHECK(manager.request_terrain_asset(
		"terrain/pendingrevision.ashterrain", revision_4) == asset);
	const AshEngine::RenderAssetReadinessSnapshot updated = manager.query_readiness();
	CHECK(updated.pending);
	CHECK_FALSE(updated.failed);
	CHECK(updated.activity_epoch == initial.activity_epoch + 1u);
	manager.shutdown();
}

TEST_CASE("Terrain render asset manager accounts newer-revision failures as failures")
{
	SUBCASE("explicit failure")
	{
		AshEngine::RenderAssetManager manager{};
		auto revision_3 = MakeSnapshot(5u);
		revision_3->residency_revision = 3u;
		REQUIRE(manager.request_terrain_asset(
			"terrain/FailedRevision.AshTerrain", revision_3) != nullptr);

		auto revision_4 = MakeSnapshot(5u);
		revision_4->residency_revision = 4u;
		revision_4->failed = true;
		revision_4->failure_detail = "revision decode failed";
		CHECK(manager.request_terrain_asset(
			"terrain/failedrevision.ashterrain", revision_4) == nullptr);
		const AshEngine::RenderAssetReadinessSnapshot failed =
			manager.query_readiness();
		CHECK_FALSE(failed.pending);
		CHECK(failed.failed);
		CHECK(failed.activity_epoch == 2u);
		manager.shutdown();
	}

	SUBCASE("malformed snapshot")
	{
		AshEngine::RenderAssetManager manager{};
		auto revision_3 = MakeSnapshot(5u);
		revision_3->residency_revision = 3u;
		REQUIRE(manager.request_terrain_asset(
			"terrain/MalformedRevision.AshTerrain", revision_3) != nullptr);

		auto revision_4 = MakeSnapshot(5u);
		revision_4->residency_revision = 4u;
		revision_4->components.pop_back();
		CHECK(manager.request_terrain_asset(
			"terrain/malformedrevision.ashterrain", revision_4) == nullptr);
		const AshEngine::RenderAssetReadinessSnapshot failed =
			manager.query_readiness();
		CHECK_FALSE(failed.pending);
		CHECK(failed.failed);
		CHECK(failed.activity_epoch == 2u);
		manager.shutdown();
	}
}

TEST_CASE("Terrain render asset keeps the approved maximum capacity and atlas residency budget")
{
	CHECK(AshEngine::k_terrain_render_height_words_per_component == 33025u);
	CHECK(AshEngine::k_terrain_render_component_capacity == 1024u);
	CHECK(AshEngine::k_terrain_weight_atlas_slot_extent == 259u);
	CHECK(AshEngine::k_terrain_weight_atlas_extent == 4144u);
	CHECK(AshEngine::k_terrain_weight_atlas_slot_count == 256u);
	CHECK(AshEngine::k_terrain_coarse_weight_extent == 1025u);
}

TEST_CASE("Terrain fallback material arrays are manager owned and shared")
{
	auto MakeOpaqueRenderTarget = [](uint32_t value)
	{
		auto owner = std::make_shared<uint32_t>(value);
		return std::shared_ptr<AshEngine::RenderTarget>(
			owner,
			reinterpret_cast<AshEngine::RenderTarget*>(owner.get()));
	};

	auto shared_arrays =
		std::make_shared<AshEngine::TerrainFallbackMaterialArrays>();
	for (uint32_t index = 0u; index < shared_arrays->arrays.size(); ++index)
	{
		shared_arrays->arrays[index] = MakeOpaqueRenderTarget(index + 1u);
	}
	REQUIRE(shared_arrays->is_valid());
	std::weak_ptr<const AshEngine::TerrainFallbackMaterialArrays> weak_arrays =
		shared_arrays;
	{
		AshEngine::TerrainRenderAsset first{};
		AshEngine::TerrainRenderAsset second{};
		REQUIRE(first.set_fallback_material_arrays(shared_arrays));
		REQUIRE(second.set_fallback_material_arrays(shared_arrays));
		for (uint32_t index = 0u; index < shared_arrays->arrays.size(); ++index)
		{
			CHECK(first.material_texture_array(index) ==
				second.material_texture_array(index));
			CHECK(first.material_texture_array(index) ==
				shared_arrays->arrays[index]);
		}
		shared_arrays.reset();
		CHECK_FALSE(weak_arrays.expired());
	}
	CHECK(weak_arrays.expired());
}
