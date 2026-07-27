#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/TerrainRenderAsset.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/TerrainRenderPass.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
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

	auto MakeSourceRevision(uint64_t generation) ->
		AshEngine::TerrainContainerRevision
	{
		AshEngine::TerrainContainerRevision revision{};
		revision.file_size = 4096u + generation;
		revision.descriptors[0].generation = generation;
		revision.descriptors[0].index_offset = 96u;
		revision.descriptors[0].index_size = 512u;
		revision.descriptors[0].index_crc32 =
			static_cast<uint32_t>(0x12340000u + generation);
		return revision;
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

	auto ReadSource(const char* path) -> std::string
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE_MESSAGE(input.is_open(), "failed to open source contract: ", path);
		std::ostringstream stream{};
		stream << input.rdbuf();
		return stream.str();
	}
}

TEST_CASE("Terrain published runtime owns post-swap LRU and same-layout queues")
{
	const std::string header = ReadSource(
		"project/src/engine/Function/Render/TerrainRenderAsset.h");
	for (const char* required_type : {
		"TerrainRenderResourceSet",
		"TerrainRenderRuntimeState",
		"TerrainRenderCandidateState",
		"TerrainPublishedRenderView" })
	{
		CAPTURE(required_type);
		CHECK(header.find(required_type) != std::string::npos);
	}

	const size_t asset_begin = header.find("class ASH_API TerrainRenderAsset");
	REQUIRE(asset_begin != std::string::npos);
	const std::string asset = header.substr(asset_begin);
	for (const char* forbidden_parallel_owner : {
		"m_accepted_snapshot",
		"m_accepted_render_layout",
		"m_pending_component_uploads",
		"m_pending_weight_updates",
		"m_pending_implicit_weight_resets",
		"m_pending_component_removals",
		"m_frame_boundary_atlas_slots",
		"m_packed_height_buffer",
		"m_dirty_weight_staging_buffer",
		"m_weight_atlases",
		"m_coarse_weight_target" })
	{
		CAPTURE(forbidden_parallel_owner);
		CHECK(asset.find(forbidden_parallel_owner) == std::string::npos);
	}
}

namespace AshEngine
{
	struct TerrainRenderAssetCpuTestSeam
	{
		struct PendingUpload
		{
			uint64_t asset_id = 0u;
			std::shared_ptr<const TerrainAssetSnapshot> accepted_snapshot{};
			std::shared_ptr<const TerrainComponentSnapshot> component{};
			TerrainComponentCoord coord{};
			uint64_t content_generation = 0u;
			uint64_t residency_revision = 0u;
		};

		struct CandidateToken
		{
			std::shared_ptr<TerrainRenderRuntimeState> runtime{};
			uint64_t epoch = 0u;
			TerrainGpuComponentUpload coarse_work{};
		};

		static CandidateToken candidate_token(const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			if (!asset.m_candidate_state)
			{
				return {};
			}
			CandidateToken token{
				asset.m_candidate_state->runtime,
				asset.m_candidate_state->candidate_epoch };
			if (!asset.m_candidate_state->coarse_work.empty())
			{
				token.coarse_work = asset.m_candidate_state->coarse_work.front();
			}
			return token;
		}

		static bool accept_snapshot_with_peak_budget(
			TerrainRenderAsset& asset,
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
			uint64_t peak_budget,
			std::string* error)
		{
			return asset.accept_snapshot_with_peak_budget(
				snapshot, peak_budget, error);
		}

		static bool complete_candidate_coarse_callback(
			TerrainRenderAsset& asset,
			const CandidateToken& token,
			bool succeeded,
			uint64_t render_frame_index)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.complete_candidate_coarse_locked(
				token.runtime, token.epoch, token.coarse_work,
				succeeded, render_frame_index);
		}

		static std::shared_ptr<TerrainRenderAsset> acquire_graph_work_asset(
			RenderAssetManager& manager)
		{
			return manager.acquire_next_terrain_graph_asset();
		}

		static std::shared_ptr<TerrainRenderRuntimeState> latest_runtime(
			TerrainRenderAsset& asset)
		{
			return asset.m_candidate_state ? asset.m_candidate_state->runtime :
				(asset.m_published_view ? asset.m_published_view->runtime : nullptr);
		}

		static std::shared_ptr<const TerrainRenderRuntimeState> latest_runtime(
			const TerrainRenderAsset& asset)
		{
			return asset.m_candidate_state ? asset.m_candidate_state->runtime :
				(asset.m_published_view ? asset.m_published_view->runtime : nullptr);
		}

		static bool complete_front_height_upload(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			if (!runtime || runtime->height_queue.empty())
			{
				return false;
			}

			const TerrainGpuComponentUpload upload = runtime->height_queue.front();
			if (!runtime->state.mark_component_uploaded_for_snapshot(
					upload.content_generation,
					upload.residency_revision,
					upload.coord))
			{
				return false;
			}
			runtime->height_queue.erase(runtime->height_queue.begin());
			return true;
		}

		static bool complete_front_weight_update(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			if (!runtime || runtime->weight_queue.empty())
			{
				return false;
			}

			const TerrainGpuComponentUpload upload = runtime->weight_queue.front();
			TerrainAtlasSlotMetadata& slot = runtime->slots.front();
			slot.asset_id = upload.asset_id;
			slot.coord = upload.coord;
			slot.content_generation = upload.content_generation;
			slot.residency_revision = upload.residency_revision;
			slot.occupied = true;
			runtime->weight_queue.erase(runtime->weight_queue.begin());
			return true;
		}

		static std::vector<PendingUpload> pending_height_uploads(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			std::vector<PendingUpload> result{};
			const auto runtime = latest_runtime(asset);
			if (!runtime)
			{
				return result;
			}
			result.reserve(runtime->height_queue.size());
			for (const TerrainGpuComponentUpload& upload : runtime->height_queue)
			{
				result.push_back({
					upload.asset_id,
					upload.accepted_snapshot.lock(),
					upload.component,
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
			const auto runtime = latest_runtime(asset);
			if (!runtime)
			{
				return result;
			}
			result.reserve(runtime->weight_queue.size());
			for (const TerrainGpuComponentUpload& upload : runtime->weight_queue)
			{
				result.push_back({
					upload.asset_id,
					upload.accepted_snapshot.lock(),
					upload.component,
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
			const auto runtime = latest_runtime(asset);
			return runtime ? runtime->reset_queue :
				std::vector<TerrainComponentCoord>{};
		}

		static std::vector<TerrainComponentCoord> pending_component_removals(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			return runtime ? runtime->removal_queue :
				std::vector<TerrainComponentCoord>{};
		}

		static uint64_t resident_weight_generation(
			const TerrainRenderAsset& asset,
			TerrainComponentCoord coord)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			if (!runtime)
			{
				return 0u;
			}
			for (const TerrainAtlasSlotMetadata& slot : runtime->slots)
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
			const auto runtime = latest_runtime(asset);
			if (!runtime)
			{
				return 0u;
			}
			for (const TerrainAtlasSlotMetadata& slot : runtime->slots)
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
			const auto runtime = latest_runtime(asset);
			if (!runtime)
			{
				return false;
			}
			const auto pending = std::find_if(
				runtime->height_queue.begin(), runtime->height_queue.end(),
				[&](const TerrainGpuComponentUpload& upload)
				{
					return upload.coord == completion.coord &&
						upload.content_generation == completion.content_generation &&
						upload.residency_revision == completion.residency_revision;
				});
			if (pending == runtime->height_queue.end() ||
				!runtime->state.mark_component_uploaded_for_snapshot(
					completion.content_generation,
					completion.residency_revision,
					completion.coord))
			{
				return false;
			}
			runtime->height_queue.erase(pending);
			return true;
		}

		static bool complete_weight_update(
			TerrainRenderAsset& asset,
			const PendingUpload& completion)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			if (!runtime)
			{
				return false;
			}
			TerrainGpuComponentUpload expected{};
			expected.asset_id = completion.asset_id;
			expected.accepted_snapshot = completion.accepted_snapshot;
			expected.component = completion.component;
			expected.coord = completion.coord;
			expected.content_generation = completion.content_generation;
			expected.residency_revision = completion.residency_revision;
			if (!asset.matches_pending_weight_update_locked(runtime, expected))
			{
				return false;
			}
			const TerrainGpuComponentUpload& upload = runtime->weight_queue.front();
			TerrainAtlasSlotMetadata& slot = runtime->slots.front();
			slot.asset_id = upload.asset_id;
			slot.coord = upload.coord;
			slot.content_generation = upload.content_generation;
			slot.residency_revision = upload.residency_revision;
			slot.occupied = true;
			runtime->weight_queue.erase(runtime->weight_queue.begin());
			return true;
		}

		static bool complete_component_removal(
			TerrainRenderAsset& asset,
			TerrainComponentCoord coord,
			uint64_t content_generation,
			uint64_t residency_revision)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			if (!runtime || !runtime->target_snapshot ||
				runtime->target_snapshot->content_generation != content_generation ||
				runtime->target_snapshot->residency_revision != residency_revision)
			{
				return false;
			}
			const auto pending = std::find(
				runtime->removal_queue.begin(), runtime->removal_queue.end(),
				coord);
			if (pending == runtime->removal_queue.end() ||
				!runtime->state.mark_component_uploaded_for_snapshot(
					content_generation, residency_revision, coord))
			{
				return false;
			}
			runtime->removal_queue.erase(pending);
			return true;
		}

		static void install_layout_dependent_resource_sentinels(
			TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			REQUIRE(runtime != nullptr);
			runtime->resources.height = std::shared_ptr<StorageBuffer>(
				reinterpret_cast<StorageBuffer*>(uintptr_t{ 1u }),
				[](StorageBuffer*) {});
			runtime->resources.coarse = std::shared_ptr<RenderTarget>(
				reinterpret_cast<RenderTarget*>(uintptr_t{ 1u }),
				[](RenderTarget*) {});
		}

		static bool publish_active_snapshot(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			const auto runtime = latest_runtime(asset);
			if (!runtime || !runtime->target_snapshot ||
				!runtime->state.publish_snapshot(
					runtime->state.active_content_generation(),
					runtime->state.m_active_residency_revision))
			{
				return false;
			}
			if (asset.m_candidate_state)
			{
				asset.m_candidate_state->coarse_work.clear();
				asset.m_candidate_state->initial_set_frozen = true;
				asset.m_candidate_state->runtime->weight_queue.clear();
				asset.update_candidate_ready_locked();
				return asset.publish_ready_candidate_locked();
			}
			if (asset.m_published_view &&
				asset.m_published_view->snapshot != runtime->target_snapshot)
			{
				auto next = std::make_shared<TerrainPublishedRenderView>(
					*asset.m_published_view);
				next->snapshot = runtime->target_snapshot;
				next->asset_id = runtime->target_snapshot->asset_id;
				next->content_generation =
					runtime->target_snapshot->content_generation;
				next->residency_revision =
					runtime->target_snapshot->residency_revision;
				++next->publication_epoch;
				asset.m_published_view = std::move(next);
			}
			runtime->work_status = runtime->weight_queue.empty() ?
				TerrainRenderWorkStatus::Ready : TerrainRenderWorkStatus::Pending;
			return runtime->work_status == TerrainRenderWorkStatus::Ready;
		}

		struct FakeGpuOps final : TerrainRenderGpuOps
		{
			uint32_t fail_create_call = 0u;
			uint32_t fail_height_upload_call = 0u;
			uint32_t create_call = 0u;
			uint32_t height_upload_call = 0u;
			uint64_t uploaded_bytes = 0u;
			std::vector<uint32_t> storage_sizes{};
			std::vector<std::pair<uint16_t, uint16_t>> target_extents{};

			std::shared_ptr<StorageBuffer> create_storage_buffer(
				const StorageBufferDesc& desc) override
			{
				++create_call;
				storage_sizes.push_back(desc.size);
				if (create_call == fail_create_call)
				{
					return nullptr;
				}
				auto owner = std::make_shared<uint32_t>(create_call);
				return std::shared_ptr<StorageBuffer>(
					owner, reinterpret_cast<StorageBuffer*>(owner.get()));
			}

			std::shared_ptr<RenderTarget> create_render_target(
				const RenderTargetDesc& desc) override
			{
				++create_call;
				target_extents.emplace_back(desc.width, desc.height);
				if (create_call == fail_create_call)
				{
					return nullptr;
				}
				auto owner = std::make_shared<uint32_t>(create_call);
				return std::shared_ptr<RenderTarget>(
					owner, reinterpret_cast<RenderTarget*>(owner.get()));
			}

			bool update_storage_buffer(
				const std::shared_ptr<StorageBuffer>&,
				uint32_t,
				uint32_t size,
				const void*) override
			{
				++height_upload_call;
				uploaded_bytes += size;
				return height_upload_call != fail_height_upload_call;
			}
		};

		struct ResourceAdvance
		{
			bool ready = false;
			uint32_t create_calls = 0u;
			uint32_t update_calls = 0u;
			uint64_t uploaded_bytes = 0u;
		};

		static void install_published_bundle(
			TerrainRenderAsset& asset,
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
			uint64_t publication_epoch = 1u)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			TerrainRenderLayoutInfo layout{};
			REQUIRE(derive_terrain_render_layout(snapshot->layout, layout));
			auto runtime = std::make_shared<TerrainRenderRuntimeState>();
			runtime->target_snapshot = snapshot;
			auto MakeBuffer = [](uint32_t value)
			{
				auto owner = std::make_shared<uint32_t>(value);
				return std::shared_ptr<StorageBuffer>(
					owner, reinterpret_cast<StorageBuffer*>(owner.get()));
			};
			auto MakeTarget = [](uint32_t value)
			{
				auto owner = std::make_shared<uint32_t>(value);
				return std::shared_ptr<RenderTarget>(
					owner, reinterpret_cast<RenderTarget*>(owner.get()));
			};
			runtime->resources.height = MakeBuffer(11u);
			runtime->resources.staging = MakeBuffer(12u);
			runtime->resources.atlas[0] = MakeTarget(13u);
			runtime->resources.atlas[1] = MakeTarget(14u);
			runtime->resources.coarse = MakeTarget(15u);
			runtime->state.begin_snapshot_for_layout(
				snapshot->content_generation,
				snapshot->residency_revision,
				0u,
				layout);
			REQUIRE(runtime->state.publish_snapshot(
				snapshot->content_generation,
				snapshot->residency_revision));
			runtime->work_status = TerrainRenderWorkStatus::Ready;
			auto view = std::make_shared<TerrainPublishedRenderView>();
			view->snapshot = snapshot;
			view->layout = layout;
			view->runtime = runtime;
			view->asset_id = snapshot->asset_id;
			view->content_generation = snapshot->content_generation;
			view->residency_revision = snapshot->residency_revision;
			view->publication_epoch = publication_epoch;
			asset.m_published_view = std::move(view);
			asset.m_candidate_state.reset();
		}

		static ResourceAdvance advance_candidate_resource_frame(
			TerrainRenderAsset& asset,
			FakeGpuOps& ops)
		{
			const uint32_t create_calls_before = ops.create_call;
			const uint32_t update_calls_before = ops.height_upload_call;
			const uint64_t uploaded_bytes_before = ops.uploaded_bytes;
			std::string error{};
			const bool result = asset.finalize_gpu_resources(ops, &error);
			return {
				result,
				ops.create_call - create_calls_before,
				ops.height_upload_call - update_calls_before,
				ops.uploaded_bytes - uploaded_bytes_before };
		}

		static ResourceAdvance advance_candidate_resource_frame(
			TerrainRenderAsset& asset,
			uint32_t fail_create_call = 0u,
			uint32_t fail_height_upload_call = 0u)
		{
			FakeGpuOps ops{};
			ops.fail_create_call = fail_create_call;
			ops.fail_height_upload_call = fail_height_upload_call;
			return advance_candidate_resource_frame(asset, ops);
		}

		static void record_candidate_required(
			TerrainRenderAsset& asset,
			const std::vector<TerrainComponentCoord>& required)
		{
			asset.record_required_residency(
				asset.published_view(), required, 100u);
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			REQUIRE(asset.m_candidate_state != nullptr);
			asset.freeze_candidate_initial_residency_locked(100u);
		}

		static void record_required(
			TerrainRenderAsset& asset,
			const std::shared_ptr<const TerrainPublishedRenderView>& view,
			const std::vector<TerrainComponentCoord>& required)
		{
			asset.record_required_residency(view, required, 100u);
		}

		static std::vector<TerrainComponentCoord> candidate_required(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_candidate_state && asset.m_candidate_state->runtime ?
				asset.m_candidate_state->runtime->latest_required_residency :
				std::vector<TerrainComponentCoord>{};
		}

		static bool complete_candidate_coarse(
			TerrainRenderAsset& asset,
			bool succeed)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			REQUIRE(asset.m_candidate_state != nullptr);
			REQUIRE_FALSE(asset.m_candidate_state->coarse_work.empty());
			return asset.complete_candidate_coarse_locked(
				asset.m_candidate_state->runtime,
				asset.m_candidate_state->candidate_epoch,
				asset.m_candidate_state->coarse_work.front(),
				succeed, 100u);
		}

		static bool complete_candidate_initial(
			TerrainRenderAsset& asset,
			bool succeed)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			REQUIRE(asset.m_candidate_state != nullptr);
			REQUIRE(asset.m_candidate_state->runtime != nullptr);
			REQUIRE_FALSE(asset.m_candidate_state->runtime->weight_queue.empty());
			return asset.complete_candidate_initial_locked(
				asset.m_candidate_state->runtime,
				asset.m_candidate_state->candidate_epoch,
				asset.m_candidate_state->runtime->weight_queue.front(),
				succeed, 100u);
		}

		static uint32_t pending_candidate_coarse_count(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_candidate_state ? static_cast<uint32_t>(
				asset.m_candidate_state->coarse_work.size()) : 0u;
		}

		static uint32_t pending_candidate_height_count(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_candidate_state && asset.m_candidate_state->runtime ?
				static_cast<uint32_t>(
					asset.m_candidate_state->runtime->height_queue.size()) : 0u;
		}

		static uint32_t pending_candidate_initial_count(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_candidate_state && asset.m_candidate_state->runtime ?
				static_cast<uint32_t>(
					asset.m_candidate_state->runtime->weight_queue.size()) : 0u;
		}

		static std::vector<TerrainComponentCoord> candidate_initial_set(
			const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_candidate_state ?
				asset.m_candidate_state->initial_resident_set :
				std::vector<TerrainComponentCoord>{};
		}

		static bool publish_ready_candidate(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.publish_ready_candidate_locked();
		}

		static void drop_candidate_prepared_view(TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			REQUIRE(asset.m_candidate_state != nullptr);
			REQUIRE(asset.m_candidate_state->work_status ==
				TerrainRenderWorkStatus::ReadyToPublish);
			asset.m_candidate_state->prepared_view.reset();
		}

		static void set_published_resident(
			TerrainRenderAsset& asset,
			uint32_t slot_index,
			TerrainComponentCoord coord,
			uint64_t last_used_frame)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			REQUIRE(asset.m_published_view != nullptr);
			auto& slot = asset.m_published_view->runtime->slots[slot_index];
			slot.asset_id = asset.m_published_view->asset_id;
			slot.coord = coord;
			slot.content_generation = asset.m_published_view->content_generation;
			slot.residency_revision = asset.m_published_view->residency_revision;
			slot.last_used_frame = last_used_frame;
			slot.occupied = true;
		}

		static std::vector<TerrainComponentCoord> select_initial_set(
			const TerrainAssetSnapshot& snapshot,
			const std::vector<std::pair<TerrainComponentCoord, uint64_t>>& residents,
			const std::vector<TerrainComponentCoord>& required)
		{
			std::array<TerrainAtlasSlotMetadata,
				k_terrain_weight_atlas_slot_count> slots{};
			for (size_t index = 0u;
				index < residents.size() && index < slots.size(); ++index)
			{
				slots[index].coord = residents[index].first;
				slots[index].last_used_frame = residents[index].second;
				slots[index].occupied = true;
			}
			return TerrainRenderAsset::select_initial_resident_set(
				snapshot, slots, required);
		}
	};
}

namespace
{
	void CompleteCandidateGraphWork(AshEngine::TerrainRenderAsset& asset)
	{
		while (AshEngine::TerrainRenderAssetCpuTestSeam::
			pending_candidate_coarse_count(asset) != 0u)
		{
			REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
				complete_candidate_coarse(asset, true));
		}
		while (AshEngine::TerrainRenderAssetCpuTestSeam::
			pending_candidate_initial_count(asset) != 0u)
		{
			REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
				complete_candidate_initial(asset, true));
		}
	}

	void DriveCandidateReadyToPublish(
		AshEngine::TerrainRenderAsset& asset,
		const std::vector<AshEngine::TerrainComponentCoord>& required = {})
	{
		uint32_t resource_frames = 0u;
		bool all_frames_pending = true;
		bool all_frames_within_upload_budget = true;
		do
		{
			const auto advance = AshEngine::TerrainRenderAssetCpuTestSeam::
				advance_candidate_resource_frame(asset);
			all_frames_pending = all_frames_pending && !advance.ready;
			all_frames_within_upload_budget =
				all_frames_within_upload_budget &&
				advance.uploaded_bytes <= 4u * 1024u * 1024u;
			++resource_frames;
		} while (AshEngine::TerrainRenderAssetCpuTestSeam::
			pending_candidate_height_count(asset) != 0u);
		CHECK(all_frames_pending);
		CHECK(all_frames_within_upload_budget);
		CHECK(resource_frames != 0u);
		AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
			asset, required);
		CompleteCandidateGraphWork(asset);
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::ReadyToPublish);
	}
}

TEST_CASE("Terrain candidate resource creation failure preserves the old published view")
{
	const auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);
	for (uint32_t failed_create = 1u; failed_create <= 5u; ++failed_create)
	{
		CAPTURE(failed_create);
		AshEngine::TerrainRenderAsset asset{};
		AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
			asset, old_snapshot);
		const auto old_view = asset.published_view();
		REQUIRE(old_view != nullptr);

		auto replacement = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
		replacement->asset_id = 808u + failed_create;
		FillCompleteSnapshot(replacement);
		REQUIRE(asset.accept_snapshot(replacement));
		const auto advance = AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(asset, failed_create);
		CHECK_FALSE(advance.ready);
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Failed);
		const auto retained = asset.published_view();
		REQUIRE(retained == old_view);
		CHECK(retained->snapshot == old_snapshot);
		CHECK(retained->runtime->resources.height ==
			old_view->runtime->resources.height);
		CHECK(retained->runtime->resources.staging ==
			old_view->runtime->resources.staging);
		CHECK(retained->runtime->resources.atlas ==
			old_view->runtime->resources.atlas);
		CHECK(retained->runtime->resources.coarse ==
			old_view->runtime->resources.coarse);
		CHECK(retained->runtime->state.readiness() ==
			AshEngine::TerrainRenderReadiness::Ready);
	}
}

TEST_CASE("Terrain candidate peak budget rejects before the first resource creation")
{
	auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);
	AshEngine::TerrainRenderAsset asset{};
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, old_snapshot);
	const auto old_view = asset.published_view();

	auto replacement = MakeSnapshot(10u, MakeRenderLayout(1u, 2u));
	FillCompleteSnapshot(replacement);
	std::string error{};
	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		accept_snapshot_with_peak_budget(asset, replacement, 1u, &error));
	CHECK(error.find("peak") != std::string::npos);
	CHECK(asset.published_view() == old_view);
	AshEngine::TerrainRenderAssetCpuTestSeam::FakeGpuOps ops{};
	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		advance_candidate_resource_frame(asset, ops).ready);
	CHECK(ops.create_call == 0u);
}

TEST_CASE("Terrain candidate height preparation remains budgeted across frames")
{
	auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);
	AshEngine::TerrainRenderAsset asset{};
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, old_snapshot);

	auto replacement = MakeSnapshot(10u, MakeRenderLayout(8u, 8u));
	FillCompleteSnapshot(replacement);
	REQUIRE(asset.accept_snapshot(replacement));

	uint32_t frame_count = 0u;
	uint32_t total_uploads = 0u;
	bool all_frames_pending = true;
	bool all_frames_within_upload_budget = true;
	bool all_frames_within_upload_count = true;
	AshEngine::TerrainRenderAssetCpuTestSeam::FakeGpuOps ops{};
	do
	{
		const auto advance = AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(asset, ops);
		all_frames_pending = all_frames_pending && !advance.ready;
		all_frames_within_upload_budget =
			all_frames_within_upload_budget &&
			advance.uploaded_bytes <= 4u * 1024u * 1024u;
		all_frames_within_upload_count =
			all_frames_within_upload_count && advance.update_calls <= 31u;
		total_uploads += advance.update_calls;
		++frame_count;
	} while (AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_height_count(asset) != 0u);

	CHECK(all_frames_pending);
	CHECK(all_frames_within_upload_budget);
	CHECK(all_frames_within_upload_count);
	CHECK(frame_count >= 3u);
	CHECK(total_uploads == 64u);
	CHECK(ops.storage_sizes.size() == 2u);
	CHECK(std::count(ops.storage_sizes.begin(), ops.storage_sizes.end(),
		AshEngine::k_terrain_weight_upload_bytes) == 1);
	CHECK(ops.target_extents.size() == 3u);
}

TEST_CASE("Terrain candidate epochs remain monotonic and stale callbacks cannot mutate superseding work")
{
	auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);
	AshEngine::TerrainRenderAsset asset{};
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, old_snapshot);

	auto candidate_a = MakeSnapshot(1u, MakeRenderLayout(1u, 2u));
	candidate_a->asset_id = 88u;
	FillCompleteSnapshot(candidate_a);
	REQUIRE(asset.accept_snapshot(candidate_a));
	const auto token_a =
		AshEngine::TerrainRenderAssetCpuTestSeam::candidate_token(asset);
	REQUIRE(token_a.runtime != nullptr);

	auto incremental_old_asset = MakeSnapshot(10u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(incremental_old_asset);
	REQUIRE(asset.accept_snapshot(incremental_old_asset));
	auto candidate_b = MakeSnapshot(2u, MakeRenderLayout(1u, 2u));
	candidate_b->asset_id = 88u;
	FillCompleteSnapshot(candidate_b);
	REQUIRE(asset.accept_snapshot(candidate_b));
	const auto token_b =
		AshEngine::TerrainRenderAssetCpuTestSeam::candidate_token(asset);
	REQUIRE(token_b.runtime != nullptr);
	CHECK(token_b.runtime != token_a.runtime);
	CHECK(token_b.epoch > token_a.epoch);

	const uint32_t pending_before =
		AshEngine::TerrainRenderAssetCpuTestSeam::
			pending_candidate_coarse_count(asset);
	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_candidate_coarse_callback(asset, token_a, true, 77u));
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_coarse_count(asset) == pending_before);
	CHECK(asset.accepted_snapshot() == candidate_b);
}

TEST_CASE("Terrain candidate revision matching candidate layout remains dense relative to published layout")
{
	auto published = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(published);
	AshEngine::TerrainRenderAsset asset{};
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, published);

	auto candidate = MakeSnapshot(10u, MakeRenderLayout(1u, 2u));
	FillCompleteSnapshot(candidate);
	REQUIRE(asset.accept_snapshot(candidate));
	auto sparse_revision = MakeSnapshot(10u, MakeRenderLayout(1u, 2u));
	sparse_revision->residency_revision = 1u;
	sparse_revision->components[0] = MakeComponent({ 0u, 0u }, 10u);
	std::string error{};
	CHECK_FALSE(asset.accept_snapshot(sparse_revision, &error));
	CHECK(error.find("replacement snapshot has a null component") !=
		std::string::npos);
	CHECK(asset.published_view()->snapshot == published);
	CHECK(asset.latest_work_status() ==
		AshEngine::TerrainRenderWorkStatus::Failed);
}

TEST_CASE("Terrain graph scheduler retains pending assets independently of visibility")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(snapshot);
	const auto asset = manager.request_terrain_asset(
		"terrain/OffscreenCandidate.AshTerrain", snapshot);
	REQUIRE(asset != nullptr);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		acquire_graph_work_asset(manager) == nullptr);
	while (AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_height_count(*asset) != 0u)
	{
		AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(*asset);
	}
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		acquire_graph_work_asset(manager) == asset);
	manager.shutdown();
}

TEST_CASE("Terrain graph scheduler round robins pending candidates and exposes one owner per frame")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot_a = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
	auto snapshot_b = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
	snapshot_b->asset_id = 78u;
	FillCompleteSnapshot(snapshot_a);
	FillCompleteSnapshot(snapshot_b);
	const auto asset_a = manager.request_terrain_asset(
		"terrain/ScheduleA.AshTerrain", snapshot_a);
	const auto asset_b = manager.request_terrain_asset(
		"terrain/ScheduleB.AshTerrain", snapshot_b);
	REQUIRE(asset_a != nullptr);
	REQUIRE(asset_b != nullptr);
	while (AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_height_count(*asset_a) != 0u)
	{
		AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(*asset_a);
	}
	while (AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_height_count(*asset_b) != 0u)
	{
		AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(*asset_b);
	}

	const auto first = AshEngine::TerrainRenderAssetCpuTestSeam::
		acquire_graph_work_asset(manager);
	const auto second = AshEngine::TerrainRenderAssetCpuTestSeam::
		acquire_graph_work_asset(manager);
	const auto third = AshEngine::TerrainRenderAssetCpuTestSeam::
		acquire_graph_work_asset(manager);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	CHECK(first != second);
	CHECK(third == first);
	CHECK((first == asset_a || first == asset_b));
	CHECK((second == asset_a || second == asset_b));
	manager.shutdown();
}

TEST_CASE("Terrain graph scheduler bounds fairness across candidate and incremental work")
{
	AshEngine::RenderAssetManager manager{};
	const auto layout = MakeRenderLayout(1u, 3u);
	auto candidate_snapshot = MakeSnapshot(1u, layout);
	FillCompleteSnapshot(candidate_snapshot);
	const auto candidate_asset = manager.request_terrain_asset(
		"terrain/FairCandidate.AshTerrain", candidate_snapshot);
	REQUIRE(candidate_asset != nullptr);
	while (AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_height_count(*candidate_asset) != 0u)
	{
		AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(*candidate_asset);
	}
	AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
		*candidate_asset, {});
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_coarse_count(*candidate_asset) == 3u);

	auto published_snapshot = MakeSnapshot(1u, layout);
	published_snapshot->asset_id = 78u;
	FillCompleteSnapshot(published_snapshot);
	const auto incremental_asset = manager.request_terrain_asset(
		"terrain/FairIncremental.AshTerrain", published_snapshot);
	REQUIRE(incremental_asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*incremental_asset, published_snapshot);
	REQUIRE(manager.finalize_pending_terrain_asset(incremental_asset));

	auto incremental_snapshot = MakeSnapshot(2u, layout);
	incremental_snapshot->asset_id = 78u;
	for (uint32_t z = 0u; z < 3u; ++z)
	{
		incremental_snapshot->components[z] = MakePaintedComponent(
			{ 0u, static_cast<uint16_t>(z) }, 2u);
	}
	REQUIRE(manager.request_terrain_asset(
		"terrain/fairincremental.ashterrain", incremental_snapshot) ==
		incremental_asset);
	while (incremental_asset->pending_component_upload_count() != 0u)
	{
		REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
			complete_front_height_upload(*incremental_asset));
	}
	REQUIRE(incremental_asset->pending_weight_update_count() == 3u);

	std::vector<std::shared_ptr<AshEngine::TerrainRenderAsset>> scheduled{};
	for (uint32_t step = 0u; step < 6u; ++step)
	{
		const auto selected = AshEngine::TerrainRenderAssetCpuTestSeam::
			acquire_graph_work_asset(manager);
		REQUIRE(selected != nullptr);
		scheduled.push_back(selected);
		if (selected == candidate_asset)
		{
			REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
				complete_candidate_coarse(*candidate_asset, true));
		}
		else
		{
			REQUIRE(selected == incremental_asset);
			REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
				complete_front_weight_update(*incremental_asset));
		}
	}
	for (size_t index = 1u; index < scheduled.size(); ++index)
	{
		CHECK(scheduled[index] != scheduled[index - 1u]);
	}
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_candidate_coarse_count(*candidate_asset) == 0u);
	CHECK(incremental_asset->pending_weight_update_count() == 0u);
	manager.shutdown();
}

TEST_CASE("Terrain manager publishes ready candidate on the next submit boundary exactly once")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(snapshot);
	const auto asset = manager.request_terrain_asset(
		"terrain/Boundary.AshTerrain", snapshot);
	REQUIRE(asset != nullptr);
	DriveCandidateReadyToPublish(*asset);
	CHECK(asset->published_view() == nullptr);
	CHECK(manager.query_readiness().pending);

	REQUIRE(manager.finalize_pending_terrain_asset(asset));
	const auto published = asset->published_view();
	REQUIRE(published != nullptr);
	CHECK(published->snapshot == snapshot);
	CHECK_FALSE(manager.query_readiness().pending);
	CHECK(manager.finalize_pending_terrain_asset(asset));
	CHECK(asset->published_view() == published);
	CHECK_FALSE(manager.query_readiness().pending);
	manager.shutdown();
}

TEST_CASE("Terrain publication preparation failure preserves the published bundle idempotently")
{
	AshEngine::RenderAssetManager manager{};
	auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);
	const auto asset = manager.request_terrain_asset(
		"terrain/PublicationFailure.AshTerrain", old_snapshot);
	REQUIRE(asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*asset, old_snapshot);
	REQUIRE(manager.finalize_pending_terrain_asset(asset));
	const auto old_view = asset->published_view();
	REQUIRE(old_view != nullptr);
	const auto old_runtime = old_view->runtime;
	REQUIRE(old_runtime != nullptr);
	const auto old_height = old_runtime->resources.height;
	const auto old_staging = old_runtime->resources.staging;
	const auto old_atlas_0 = old_runtime->resources.atlas[0];
	const auto old_atlas_1 = old_runtime->resources.atlas[1];
	const auto old_coarse = old_runtime->resources.coarse;

	auto replacement = MakeSnapshot(10u, MakeRenderLayout(1u, 2u));
	FillCompleteSnapshot(replacement);
	REQUIRE(manager.request_terrain_asset(
		"terrain/publicationfailure.ashterrain", replacement) == asset);
	DriveCandidateReadyToPublish(*asset);
	AshEngine::TerrainRenderAssetCpuTestSeam::drop_candidate_prepared_view(*asset);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));
	CHECK(asset->latest_work_status() == AshEngine::TerrainRenderWorkStatus::Failed);
	CHECK(asset->get_last_error().find("published view retained") !=
		std::string::npos);
	CHECK(asset->get_last_error().find("stage=ReadyToPublish") !=
		std::string::npos);
	CHECK(asset->get_last_error().find(
		"reason=Terrain candidate publication view is unavailable.") !=
		std::string::npos);
	CHECK(asset->published_view() == old_view);
	CHECK(old_view->snapshot == old_snapshot);
	CHECK(old_view->runtime == old_runtime);
	CHECK(old_runtime->work_status == AshEngine::TerrainRenderWorkStatus::Ready);
	CHECK(old_runtime->resources.height == old_height);
	CHECK(old_runtime->resources.staging == old_staging);
	CHECK(old_runtime->resources.atlas[0] == old_atlas_0);
	CHECK(old_runtime->resources.atlas[1] == old_atlas_1);
	CHECK(old_runtime->resources.coarse == old_coarse);

	const auto failed_readiness = manager.query_readiness();
	CHECK_FALSE(failed_readiness.pending);
	CHECK(failed_readiness.failed);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));
	CHECK(manager.query_readiness().activity_epoch ==
		failed_readiness.activity_epoch);
	CHECK(asset->published_view() == old_view);
	manager.shutdown();
}

TEST_CASE("Terrain candidate height coarse and initial failures preserve the old published view")
{
	const auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);

	SUBCASE("Nth height upload failure")
	{
		AshEngine::TerrainRenderAsset asset{};
		AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
			asset, old_snapshot);
		const auto old_view = asset.published_view();
		auto replacement = MakeSnapshot(10u, MakeRenderLayout(1u, 2u));
		FillCompleteSnapshot(replacement);
		REQUIRE(asset.accept_snapshot(replacement));
		AshEngine::TerrainRenderAssetCpuTestSeam::FakeGpuOps ops{};
		ops.fail_height_upload_call = 2u;
		bool all_frames_pending = true;
		do
		{
			const auto advance = AshEngine::TerrainRenderAssetCpuTestSeam::
				advance_candidate_resource_frame(asset, ops);
			all_frames_pending = all_frames_pending && !advance.ready;
		} while (asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Pending &&
			AshEngine::TerrainRenderAssetCpuTestSeam::
				pending_candidate_height_count(asset) != 0u);
		CHECK(all_frames_pending);
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Failed);
		CHECK(asset.published_view() == old_view);
	}

	SUBCASE("coarse work failure")
	{
		AshEngine::TerrainRenderAsset asset{};
		AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
			asset, old_snapshot);
		const auto old_view = asset.published_view();
		auto replacement = MakeSnapshot(10u, MakeRenderLayout(1u, 1u));
		replacement->asset_id = 900u;
		FillCompleteSnapshot(replacement);
		REQUIRE(asset.accept_snapshot(replacement));
		CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(asset).ready);
		AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
			asset, {});
		CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
			complete_candidate_coarse(asset, false));
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Failed);
		CHECK(asset.published_view() == old_view);
	}

	SUBCASE("initial resident work failure")
	{
		AshEngine::TerrainRenderAsset asset{};
		AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
			asset, old_snapshot);
		const auto old_view = asset.published_view();
		auto replacement = MakeSnapshot(10u, MakeRenderLayout(1u, 1u));
		replacement->asset_id = 901u;
		replacement->components[0] = MakePaintedComponent({ 0u, 0u }, 10u);
		REQUIRE(asset.accept_snapshot(replacement));
		CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
			advance_candidate_resource_frame(asset).ready);
		AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
			asset, { { 0u, 0u } });
		while (AshEngine::TerrainRenderAssetCpuTestSeam::
			pending_candidate_coarse_count(asset) != 0u)
		{
			REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
				complete_candidate_coarse(asset, true));
		}
		CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
			complete_candidate_initial(asset, false));
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Failed);
		CHECK(asset.published_view() == old_view);
	}
}

TEST_CASE("Terrain candidate intermediate and last graph failures preserve the published bundle")
{
	for (const bool fail_initial : { false, true })
	{
		for (const uint32_t failed_call : { 2u, 3u })
		{
			CAPTURE(fail_initial);
			CAPTURE(failed_call);
			auto old_snapshot = MakeSnapshot(9u, MakeRenderLayout(1u, 1u));
			FillCompleteSnapshot(old_snapshot);
			AshEngine::TerrainRenderAsset asset{};
			AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
				asset, old_snapshot);
			const auto old_view = asset.published_view();

			auto replacement = MakeSnapshot(10u, MakeRenderLayout(1u, 3u));
			replacement->asset_id = 900u + failed_call + (fail_initial ? 10u : 0u);
			for (uint16_t z = 0u; z < 3u; ++z)
			{
				replacement->components[z] = MakePaintedComponent(
					{ 0u, z }, replacement->content_generation);
			}
			REQUIRE(asset.accept_snapshot(replacement));
			while (AshEngine::TerrainRenderAssetCpuTestSeam::
				pending_candidate_height_count(asset) != 0u)
			{
				AshEngine::TerrainRenderAssetCpuTestSeam::
					advance_candidate_resource_frame(asset);
			}
			AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
				asset, { { 0u, 0u }, { 0u, 1u }, { 0u, 2u } });

			for (uint32_t call = 1u; call <= 3u; ++call)
			{
				const bool should_fail = !fail_initial && call == failed_call;
				const bool completed = AshEngine::TerrainRenderAssetCpuTestSeam::
					complete_candidate_coarse(asset, !should_fail);
				CHECK(completed == !should_fail);
				if (should_fail)
				{
					break;
				}
			}
			if (fail_initial)
			{
				for (uint32_t call = 1u; call <= 3u; ++call)
				{
					const bool should_fail = call == failed_call;
					const bool completed = AshEngine::TerrainRenderAssetCpuTestSeam::
						complete_candidate_initial(asset, !should_fail);
					CHECK(completed == !should_fail);
					if (should_fail)
					{
						break;
					}
				}
			}
			CHECK(asset.latest_work_status() ==
				AshEngine::TerrainRenderWorkStatus::Failed);
			CHECK(asset.published_view() == old_view);
		}
	}
}

TEST_CASE("Terrain layout replacement publishes only at the frame boundary")
{
	struct ReplacementCase
	{
		uint32_t old_x;
		uint32_t old_z;
		uint32_t new_x;
		uint32_t new_z;
		uint64_t old_asset;
		uint64_t new_asset;
		uint64_t old_generation;
		uint64_t new_generation;
	};
	const std::array<ReplacementCase, 3> cases = {
		ReplacementCase{ 8u, 8u, 8u, 16u, 77u, 77u, 9u, 10u },
		ReplacementCase{ 32u, 32u, 8u, 8u, 77u, 77u, 9u, 10u },
		ReplacementCase{ 1u, 1u, 1u, 1u, 77u, 88u, 900u, 1u }
	};
	for (const ReplacementCase& replacement_case : cases)
	{
		CAPTURE(replacement_case.old_x);
		CAPTURE(replacement_case.old_z);
		CAPTURE(replacement_case.new_x);
		CAPTURE(replacement_case.new_z);
		auto old_snapshot = MakeSnapshot(
			replacement_case.old_generation,
			MakeRenderLayout(replacement_case.old_x, replacement_case.old_z));
		old_snapshot->asset_id = replacement_case.old_asset;
		FillCompleteSnapshot(old_snapshot);
		AshEngine::TerrainRenderAsset asset{};
		AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
			asset, old_snapshot, 41u);
		const auto old_view = asset.published_view();

		auto replacement = MakeSnapshot(
			replacement_case.new_generation,
			MakeRenderLayout(replacement_case.new_x, replacement_case.new_z));
		replacement->asset_id = replacement_case.new_asset;
		FillCompleteSnapshot(replacement);
		REQUIRE(asset.accept_snapshot(replacement));
		DriveCandidateReadyToPublish(asset, { { 0u, 0u } });
		CHECK(asset.published_view() == old_view);
		CHECK(asset.accepted_snapshot() == replacement);
		REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
			publish_ready_candidate(asset));
		const auto published = asset.published_view();
		REQUIRE(published != nullptr);
		CHECK(published != old_view);
		CHECK(published->snapshot == replacement);
		CHECK(published->layout.layout.component_count_x ==
			replacement_case.new_x);
		CHECK(published->layout.layout.component_count_z ==
			replacement_case.new_z);
		CHECK(published->publication_epoch == 42u);
		CHECK(old_view->snapshot == old_snapshot);
		CHECK(old_view->publication_epoch == 41u);
		CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
			publish_ready_candidate(asset));
	}
}

TEST_CASE("Terrain sparse first load remains legal without a published view")
{
	auto sparse = MakeSnapshot(1u, MakeRenderLayout(1u, 2u));
	sparse->components[1] = MakePaintedComponent({ 0u, 1u }, 1u);
	AshEngine::TerrainRenderAsset asset{};
	REQUIRE(asset.accept_snapshot(sparse));
	CHECK(asset.published_view() == nullptr);
	DriveCandidateReadyToPublish(asset, { { 0u, 1u }, { 0u, 0u } });
	CHECK(asset.published_view() == nullptr);
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		publish_ready_candidate(asset));
	REQUIRE(asset.published_view() != nullptr);
	CHECK(asset.published_view()->snapshot == sparse);
}

TEST_CASE("Terrain required residency seeds first load and ignores stale published views")
{
	SUBCASE("first load records visible residency without a published view")
	{
		auto snapshot = MakeSnapshot(1u, MakeRenderLayout(1u, 2u));
		snapshot->components[0] = MakePaintedComponent({ 0u, 0u }, 1u);
		snapshot->components[1] = MakePaintedComponent({ 0u, 1u }, 1u);
		AshEngine::TerrainRenderAsset asset{};
		REQUIRE(asset.accept_snapshot(snapshot));
		while (AshEngine::TerrainRenderAssetCpuTestSeam::
			pending_candidate_height_count(asset) != 0u)
		{
			AshEngine::TerrainRenderAssetCpuTestSeam::
				advance_candidate_resource_frame(asset);
		}
		const std::vector<AshEngine::TerrainComponentCoord> required = {
			{ 0u, 1u }
		};
		AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
			asset, required);
		CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
			candidate_required(asset) == required);
		CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
			candidate_initial_set(asset) == required);
	}

	SUBCASE("retired view cannot update the active publication or candidate")
	{
		auto old_snapshot = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
		FillCompleteSnapshot(old_snapshot);
		AshEngine::TerrainRenderAsset asset{};
		AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
			asset, old_snapshot);
		const auto retired_view = asset.published_view();
		REQUIRE(retired_view != nullptr);

		auto replacement = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
		replacement->asset_id = 2u;
		FillCompleteSnapshot(replacement);
		REQUIRE(asset.accept_snapshot(replacement));
		DriveCandidateReadyToPublish(asset);
		REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
			publish_ready_candidate(asset));
		const auto current_view = asset.published_view();
		REQUIRE(current_view != nullptr);
		REQUIRE(current_view != retired_view);

		auto next = MakeSnapshot(1u, MakeRenderLayout(1u, 2u));
		next->asset_id = 3u;
		FillCompleteSnapshot(next);
		REQUIRE(asset.accept_snapshot(next));
		const std::vector<AshEngine::TerrainComponentCoord> stale_required = {
			{ 0u, 1u }
		};
		AshEngine::TerrainRenderAssetCpuTestSeam::record_required(
			asset, retired_view, stale_required);
		CHECK(retired_view->runtime->latest_required_residency.empty());
		CHECK(current_view->runtime->latest_required_residency.empty());
		CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
			candidate_required(asset).empty());
	}
}

TEST_CASE("Terrain sparse first load failure leaves no publication")
{
	auto sparse = MakeSnapshot(1u, MakeRenderLayout(1u, 2u));
	sparse->components[1] = MakeComponent({ 0u, 1u }, 1u);
	AshEngine::TerrainRenderAsset asset{};
	REQUIRE(asset.accept_snapshot(sparse));
	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		advance_candidate_resource_frame(asset, 3u).ready);
	CHECK(asset.latest_work_status() ==
		AshEngine::TerrainRenderWorkStatus::Failed);
	CHECK(asset.published_view() == nullptr);
}

TEST_CASE("Terrain candidate initial resident set is deterministic filtered unique and capped")
{
	auto snapshot = MakeSnapshot(2u, MakeRenderLayout(32u, 32u));
	for (uint32_t z = 0u; z < 32u; ++z)
	{
		for (uint32_t x = 0u; x < 32u; ++x)
		{
			const size_t index = static_cast<size_t>(z) * 32u + x;
			auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>();
			component->coord = {
				static_cast<uint16_t>(x), static_cast<uint16_t>(z) };
			component->weights.resize(1u);
			snapshot->components[index] = std::move(component);
		}
	}
	snapshot->components[31u * 32u + 31u].reset();
	std::vector<std::pair<AshEngine::TerrainComponentCoord, uint64_t>> residents{};
	for (uint32_t index = 0u; index < 256u; ++index)
	{
		residents.push_back({ {
			static_cast<uint16_t>(index % 32u),
			static_cast<uint16_t>(index / 32u) },
			1000u - index });
	}
	const std::vector<AshEngine::TerrainComponentCoord> required = {
		{ 20u, 20u }, { 20u, 20u }, { 31u, 31u }, { 30u, 30u }, { 3u, 3u }
	};
	const auto selected = AshEngine::TerrainRenderAssetCpuTestSeam::
		select_initial_set(*snapshot, residents, required);
	REQUIRE(selected.size() == AshEngine::k_terrain_weight_atlas_slot_count);
	CHECK(selected[0] == AshEngine::TerrainComponentCoord{ 3u, 3u });
	CHECK(selected[1] == AshEngine::TerrainComponentCoord{ 20u, 20u });
	CHECK(selected[2] == AshEngine::TerrainComponentCoord{ 30u, 30u });
	CHECK(std::count(selected.begin(), selected.end(),
		AshEngine::TerrainComponentCoord{ 20u, 20u }) == 1);
	CHECK(std::find(selected.begin(), selected.end(),
		AshEngine::TerrainComponentCoord{ 31u, 31u }) == selected.end());
	CHECK(std::find(selected.begin(), selected.end(),
		AshEngine::TerrainComponentCoord{ 0u, 0u }) != selected.end());
}

TEST_CASE("Terrain candidate initial selection filters shrink and implicit entries with deterministic ties")
{
	auto snapshot = MakeSnapshot(2u, MakeRenderLayout(3u, 2u));
	for (uint16_t z = 0u; z < 2u; ++z)
	{
		for (uint16_t x = 0u; x < 3u; ++x)
		{
			const size_t index = static_cast<size_t>(z) * 3u + x;
			snapshot->components[index] = MakePaintedComponent({ x, z }, 2u);
		}
	}
	snapshot->components[2] = MakeComponent({ 2u, 0u }, 2u);
	const auto selected = AshEngine::TerrainRenderAssetCpuTestSeam::
		select_initial_set(
			*snapshot,
			{
				{ { 1u, 1u }, 7u },
				{ { 1u, 0u }, 7u },
				{ { 2u, 0u }, 99u },
				{ { 3u, 0u }, 100u },
				{ { 0u, 0u }, 5u }
			},
			{ { 0u, 0u }, { 0u, 1u }, { 0u, 0u }, { 2u, 0u } });
	REQUIRE(selected.size() == 4u);
	CHECK(selected[0] == AshEngine::TerrainComponentCoord{ 0u, 0u });
	CHECK(selected[1] == AshEngine::TerrainComponentCoord{ 0u, 1u });
	CHECK(selected[2] == AshEngine::TerrainComponentCoord{ 1u, 0u });
	CHECK(selected[3] == AshEngine::TerrainComponentCoord{ 1u, 1u });
	CHECK(std::count(selected.begin(), selected.end(),
		AshEngine::TerrainComponentCoord{ 0u, 0u }) == 1);
	CHECK(std::find(selected.begin(), selected.end(),
		AshEngine::TerrainComponentCoord{ 2u, 0u }) == selected.end());
}

TEST_CASE("Terrain published runtime owns post-swap state while retained old views stay unchanged")
{
	auto old_snapshot = MakeSnapshot(7u, MakeRenderLayout(1u, 1u));
	old_snapshot->components[0] = MakePaintedComponent({ 0u, 0u }, 7u);
	AshEngine::TerrainRenderAsset asset{};
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, old_snapshot);
	AshEngine::TerrainRenderAssetCpuTestSeam::set_published_resident(
		asset, 0u, { 0u, 0u }, 99u);
	const auto old_view = asset.published_view();

	auto replacement = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
	replacement->asset_id = 99u;
	replacement->components[0] = MakePaintedComponent({ 0u, 0u }, 1u);
	REQUIRE(asset.accept_snapshot(replacement));
	DriveCandidateReadyToPublish(asset, { { 0u, 0u } });
	const auto old_slots = old_view->runtime->slots;
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		publish_ready_candidate(asset));
	const auto new_view = asset.published_view();
	REQUIRE(new_view != nullptr);
	CHECK(new_view->runtime != old_view->runtime);

	auto incremental = MakeSnapshot(2u, MakeRenderLayout(1u, 1u));
	incremental->asset_id = 99u;
	incremental->components[0] = MakePaintedComponent({ 0u, 0u }, 2u);
	REQUIRE(asset.accept_snapshot(incremental));
	CHECK(asset.published_view()->runtime == new_view->runtime);
	CHECK(asset.published_view()->runtime->target_snapshot == incremental);
	CHECK_FALSE(asset.published_view()->runtime->height_queue.empty());
	CHECK(old_view->runtime->target_snapshot == old_snapshot);
	CHECK(old_view->runtime->height_queue.empty());
	for (size_t index = 0u; index < old_slots.size(); ++index)
	{
		const auto& retained = old_view->runtime->slots[index];
		const auto& expected = old_slots[index];
		CAPTURE(index);
		CHECK(retained.asset_id == expected.asset_id);
		CHECK(retained.coord == expected.coord);
		CHECK(retained.content_generation == expected.content_generation);
		CHECK(retained.residency_revision == expected.residency_revision);
		CHECK(retained.last_used_frame == expected.last_used_frame);
		CHECK(retained.occupied == expected.occupied);
	}
}

TEST_CASE("Terrain manager tracks latest work independently from published drawable state")
{
	AshEngine::RenderAssetManager manager{};
	auto old_snapshot = MakeSnapshot(7u, MakeRenderLayout(1u, 1u));
	FillCompleteSnapshot(old_snapshot);
	const auto asset = manager.request_terrain_asset(
		"terrain/Atomic.AshTerrain", old_snapshot);
	REQUIRE(asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*asset, old_snapshot);
	REQUIRE(manager.finalize_pending_terrain_asset(asset));
	REQUIRE_FALSE(manager.query_readiness().pending);

	auto replacement = MakeSnapshot(1u, MakeRenderLayout(1u, 1u));
	replacement->asset_id = 88u;
	FillCompleteSnapshot(replacement);
	REQUIRE(manager.request_terrain_asset(
		"terrain/atomic.ashterrain", replacement) == asset);
	CHECK(manager.query_readiness().pending);
	CHECK_FALSE(manager.query_readiness().failed);
	CHECK(asset->published_view()->snapshot == old_snapshot);
	CHECK(asset->published_view()->runtime->state.readiness() ==
		AshEngine::TerrainRenderReadiness::Ready);

	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		advance_candidate_resource_frame(*asset, 5u).ready);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));
	CHECK_FALSE(manager.query_readiness().pending);
	CHECK(manager.query_readiness().failed);
	CHECK(asset->published_view()->snapshot == old_snapshot);
	CHECK(asset->published_view()->runtime->state.readiness() ==
		AshEngine::TerrainRenderReadiness::Ready);
	manager.shutdown();
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
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, initial);
	REQUIRE(asset.published_view()->snapshot == initial);

	SUBCASE("asset replacement cannot reset generation with a null table entry")
	{
		auto replacement = MakeSnapshot(1u, initial_layout);
		replacement->asset_id = 78u;
		CHECK_FALSE(asset.accept_snapshot(replacement, &error));
		CHECK(error.find(
			"replacement snapshot has a null component at row-major slot 0.") !=
			std::string::npos);
		CHECK(asset.accepted_snapshot() == replacement);
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Failed);
		CHECK(asset.published_view()->snapshot == initial);
		CHECK(asset.pending_component_upload_count() == 0u);
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
		CHECK(asset.accepted_snapshot() == replacement);
		CHECK(asset.latest_work_status() ==
			AshEngine::TerrainRenderWorkStatus::Failed);
		CHECK(asset.published_view()->snapshot == initial);
		CHECK(asset.pending_component_upload_count() == 0u);
		CHECK_FALSE(asset.has_pending_component_upload({ 7u, 15u }));
	}
}

TEST_CASE("Terrain render asset keeps published resources while a different-layout candidate is pending")
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
	CHECK(asset.published_view()->snapshot == initial);

	auto replacement = MakeSnapshot(3u, MakeRenderLayout(8u, 16u));
	FillCompleteSnapshot(replacement);
	REQUIRE(asset.accept_snapshot(replacement, &error));
	CHECK(error.empty());
	CHECK(asset.accepted_snapshot() == replacement);
	CHECK(asset.published_view()->snapshot == initial);
	CHECK(asset.packed_height_buffer() == old_height);
	CHECK(asset.coarse_weight_target() == old_coarse);
	CHECK(asset.published_content_generation() ==
		old_state.published_content_generation);
	CHECK(asset.latest_work_status() == AshEngine::TerrainRenderWorkStatus::Pending);
	CHECK(asset.pending_component_upload_count() == 128u);
	CHECK(asset.pending_component_removal_count() == 0u);
}

TEST_CASE("Terrain render asset keeps its published bundle while a different-asset candidate is pending")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto accepted = MakeSnapshot(5u, layout);
	accepted->asset_id = 101u;
	accepted->residency_revision = 4u;
	accepted->components[0] = MakeComponent({ 0u, 0u }, 5u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(accepted, &error));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::publish_active_snapshot(asset));
	AshEngine::TerrainRenderAssetCpuTestSeam::
		install_layout_dependent_resource_sentinels(asset);
	const auto old_height = asset.packed_height_buffer();
	const auto old_coarse = asset.coarse_weight_target();
	const AshEngine::TerrainShadowCasterIdentity old_state =
		asset.snapshot_shadow_caster_identity();

	auto replacement = MakeSnapshot(5u, layout);
	replacement->asset_id = 202u;
	replacement->residency_revision = 4u;
	FillCompleteSnapshot(replacement);
	REQUIRE(asset.accept_snapshot(replacement, &error));
	CHECK(error.empty());
	CHECK(asset.accepted_snapshot() == replacement);
	CHECK(asset.published_view()->snapshot == accepted);
	CHECK(asset.packed_height_buffer() == old_height);
	CHECK(asset.coarse_weight_target() == old_coarse);
	const AshEngine::TerrainShadowCasterIdentity retained_state =
		asset.snapshot_shadow_caster_identity();
	CHECK(retained_state.accepted_asset_id == replacement->asset_id);
	CHECK(retained_state.active_content_generation ==
		replacement->content_generation);
	CHECK(retained_state.published_content_generation ==
		old_state.published_content_generation);
	CHECK(retained_state.readiness == AshEngine::TerrainRenderReadiness::Pending);
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK(asset.pending_weight_update_count() == 0u);
	CHECK(asset.pending_component_removal_count() == 0u);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		pending_implicit_weight_resets(asset).empty());
}

TEST_CASE("Terrain failed same-layout work can be superseded without disturbing the published bundle")
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
	REQUIRE(asset.accept_snapshot(replacement, &error));
	CHECK(error.empty());
	CHECK(asset.accepted_snapshot() == replacement);
	CHECK(asset.published_view()->snapshot == initial);
	CHECK(asset.packed_height_buffer() == old_height);
	CHECK(asset.coarse_weight_target() == old_coarse);
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Pending);
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
	const AshEngine::TerrainShadowCasterIdentity before_repeat =
		asset.snapshot_shadow_caster_identity();
	const auto before_repeat_heights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	const auto before_repeat_weights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset);
	const auto before_repeat_resets =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_implicit_weight_resets(asset);
	const auto before_repeat_removals =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_component_removals(asset);

	CHECK(asset.accept_snapshot(newer_residency, &error));
	CHECK(error.empty());
	CHECK(asset.accepted_snapshot() == newer_residency);
	const AshEngine::TerrainShadowCasterIdentity after_repeat =
		asset.snapshot_shadow_caster_identity();
	CHECK(after_repeat.accepted_snapshot_identity ==
		before_repeat.accepted_snapshot_identity);
	CHECK(after_repeat.active_content_generation ==
		before_repeat.active_content_generation);
	CHECK(after_repeat.published_content_generation ==
		before_repeat.published_content_generation);
	CHECK(after_repeat.required_upload_count == before_repeat.required_upload_count);
	CHECK(after_repeat.completed_upload_count == before_repeat.completed_upload_count);
	CHECK(after_repeat.readiness == before_repeat.readiness);
	const auto after_repeat_heights =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	REQUIRE(after_repeat_heights.size() == before_repeat_heights.size());
	REQUIRE(after_repeat_heights.size() == 1u);
	CHECK(after_repeat_heights[0].coord == before_repeat_heights[0].coord);
	CHECK(after_repeat_heights[0].content_generation ==
		before_repeat_heights[0].content_generation);
	CHECK(after_repeat_heights[0].residency_revision ==
		before_repeat_heights[0].residency_revision);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset).size() ==
		before_repeat_weights.size());
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::pending_implicit_weight_resets(asset) ==
		before_repeat_resets);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::pending_component_removals(asset) ==
		before_repeat_removals);

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

TEST_CASE("Terrain published runtime rejects stale same-layout snapshots without disturbing pending work")
{
	const auto layout = MakeRenderLayout(1u, 1u);
	auto published = MakeSnapshot(5u, layout);
	published->residency_revision = 3u;
	published->components[0] = MakeComponent({ 0u, 0u }, 5u);
	AshEngine::TerrainRenderAsset asset{};
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, published);

	auto pending = MakeSnapshot(5u, layout);
	pending->residency_revision = 4u;
	pending->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(asset.accept_snapshot(pending));
	const auto pending_before =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	REQUIRE(pending_before.size() == 1u);

	auto stale = MakeSnapshot(5u, layout);
	stale->residency_revision = 3u;
	stale->components[0] = MakeComponent({ 0u, 0u }, 5u);
	std::string error{};
	CHECK_FALSE(asset.accept_snapshot(stale, &error));
	CHECK(error == "terrain snapshot content generation is stale.");
	CHECK(asset.accepted_snapshot() == pending);
	CHECK(asset.published_view()->snapshot == published);
	const auto pending_after =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_height_uploads(asset);
	REQUIRE(pending_after.size() == 1u);
	CHECK(pending_after[0].component == pending_before[0].component);
}

TEST_CASE("Terrain manager returns a retained publication for stale requests independent of error output")
{
	AshEngine::RenderAssetManager manager{};
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto published = MakeSnapshot(5u, layout);
	published->residency_revision = 3u;
	published->components[0] = MakeComponent({ 0u, 0u }, 5u);
	const auto asset = manager.request_terrain_asset(
		"terrain/StaleRetained.AshTerrain", published);
	REQUIRE(asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*asset, published);

	auto pending = MakeSnapshot(5u, layout);
	pending->residency_revision = 5u;
	pending->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(manager.request_terrain_asset(
		"terrain/staleretained.ashterrain", pending) == asset);

	auto stale = MakeSnapshot(5u, layout);
	stale->residency_revision = 4u;
	stale->components[0] = MakeComponent({ 0u, 0u }, 5u);
	CHECK(manager.request_terrain_asset(
		"terrain/staleretained.ashterrain", stale) == asset);
	std::string error{};
	CHECK(manager.request_terrain_asset(
		"terrain/staleretained.ashterrain", stale, &error) == asset);
	CHECK(error.find("terrain snapshot content generation is stale.") !=
		std::string::npos);
	CHECK(asset->published_view()->snapshot == published);
	CHECK(asset->accepted_snapshot() == pending);
	manager.shutdown();
}

TEST_CASE("Terrain manager treats a reloaded identical container revision as the same request")
{
	AshEngine::RenderAssetManager manager{};
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto original = MakeSnapshot(5u, layout);
	original->residency_revision = 3u;
	original->source_revision = MakeSourceRevision(5u);
	original->components[0] = MakeComponent({ 0u, 0u }, 5u);
	const auto asset = manager.request_terrain_asset(
		"terrain/IdenticalReimport.AshTerrain", original);
	REQUIRE(asset != nullptr);
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(*asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		publish_active_snapshot(*asset));
	REQUIRE(manager.finalize_pending_terrain_asset(asset));
	const AshEngine::RenderAssetReadinessSnapshot ready =
		manager.query_readiness();
	REQUIRE_FALSE(ready.pending);
	REQUIRE_FALSE(ready.failed);

	auto reloaded = MakeSnapshot(5u, layout);
	reloaded->residency_revision = 3u;
	reloaded->source_revision = original->source_revision;
	reloaded->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(reloaded != original);
	REQUIRE(reloaded->components[0] != original->components[0]);
	std::string error = "sentinel";
	CHECK(manager.request_terrain_asset(
		"terrain/identicalreimport.ashterrain", reloaded, &error) == asset);
	CHECK(error.empty());
	CHECK(asset->accepted_snapshot() == original);
	REQUIRE(asset->published_view() != nullptr);
	CHECK(asset->published_view()->snapshot == original);
	const AshEngine::RenderAssetReadinessSnapshot after =
		manager.query_readiness();
	CHECK_FALSE(after.pending);
	CHECK_FALSE(after.failed);
	CHECK(after.activity_epoch == ready.activity_epoch);
	manager.shutdown();
}

TEST_CASE("Terrain manager keeps equal generations with different container revisions stale")
{
	AshEngine::RenderAssetManager manager{};
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto original = MakeSnapshot(5u, layout);
	original->residency_revision = 3u;
	original->source_revision = MakeSourceRevision(5u);
	original->components[0] = MakeComponent({ 0u, 0u }, 5u);
	const auto asset = manager.request_terrain_asset(
		"terrain/ChangedReimport.AshTerrain", original);
	REQUIRE(asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*asset, original);

	auto changed = MakeSnapshot(5u, layout);
	changed->residency_revision = 3u;
	changed->source_revision = MakeSourceRevision(6u);
	changed->components[0] = MakeComponent({ 0u, 0u }, 5u);
	std::string error{};
	CHECK(manager.request_terrain_asset(
		"terrain/changedreimport.ashterrain", changed, &error) == asset);
	CHECK(error.find("terrain snapshot content generation is stale.") !=
		std::string::npos);
	CHECK(asset->accepted_snapshot() == original);
	REQUIRE(asset->published_view() != nullptr);
	CHECK(asset->published_view()->snapshot == original);
	manager.shutdown();
}

TEST_CASE("Terrain manager validation failure reports retained path stage and layout")
{
	AshEngine::RenderAssetManager manager{};
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto published = MakeSnapshot(1u, layout);
	published->components[0] = MakeComponent({ 0u, 0u }, 1u);
	const auto asset = manager.request_terrain_asset(
		"terrain/ValidationContext.AshTerrain", published);
	REQUIRE(asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*asset, published);

	auto rejected = MakeSnapshot(2u, layout);
	rejected->components[0] = MakeComponent({ 1u, 0u }, 2u);
	std::string error{};
	CHECK(manager.request_terrain_asset(
		"terrain/validationcontext.ashterrain", rejected, &error) == asset);
	CHECK(error.find("published view retained") != std::string::npos);
	CHECK(error.find(
		"asset_path=terrain/ValidationContext.AshTerrain") !=
		std::string::npos);
	CHECK(error.find("stage=ValidateSnapshot") != std::string::npos);
	CHECK(error.find("samples=257x257") != std::string::npos);
	CHECK(error.find("components=1x1") != std::string::npos);
	CHECK(error.find("quads=256") != std::string::npos);
	CHECK(error.find("spacing=1") != std::string::npos);
	CHECK(error.find("has coord=(1,0); expected=(0,0)") !=
		std::string::npos);
	CHECK(asset->published_view()->snapshot == published);
	manager.shutdown();
}

TEST_CASE("Terrain same layout upload failure reports retained incremental context")
{
	AshEngine::RenderAssetManager manager{};
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto published = MakeSnapshot(1u, layout);
	published->components[0] = MakeComponent({ 0u, 0u }, 1u);
	const auto asset = manager.request_terrain_asset(
		"terrain/IncrementalContext.AshTerrain", published);
	REQUIRE(asset != nullptr);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		*asset, published);

	auto replacement = MakeSnapshot(2u, layout);
	replacement->components[0] = MakeComponent({ 0u, 0u }, 2u);
	REQUIRE(manager.request_terrain_asset(
		"terrain/incrementalcontext.ashterrain", replacement) == asset);
	AshEngine::TerrainRenderAssetCpuTestSeam::FakeGpuOps gpu_ops{};
	gpu_ops.fail_height_upload_call = 1u;
	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		advance_candidate_resource_frame(*asset, gpu_ops).ready);
	const std::string error = asset->get_last_error();
	CHECK(error.find("published view retained") != std::string::npos);
	CHECK(error.find(
		"asset_path=terrain/IncrementalContext.AshTerrain") !=
		std::string::npos);
	CHECK(error.find("stage=IncrementalUpdate") != std::string::npos);
	CHECK(error.find("samples=257x257") != std::string::npos);
	CHECK(error.find("components=1x1") != std::string::npos);
	CHECK(error.find("reason=failed to upload Terrain component height data.") !=
		std::string::npos);
	CHECK(asset->published_view()->snapshot == published);
	manager.shutdown();
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
	auto revision_2 = MakeSnapshot(5u, layout);
	revision_2->residency_revision = 2u;
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, revision_2);
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

TEST_CASE("Terrain atlas completion isolates different assets with the same tuple")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto asset_a = MakeSnapshot(5u, layout);
	asset_a->asset_id = 101u;
	asset_a->residency_revision = 4u;
	asset_a->components[0] = MakePaintedComponent({ 0u, 0u }, 5u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(asset_a, &error));
	AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
		asset, { { 0u, 0u } });
	const auto stale_a_upload =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset);
	REQUIRE(stale_a_upload.size() == 1u);
	CHECK(stale_a_upload[0].asset_id == 101u);
	CHECK(stale_a_upload[0].accepted_snapshot == asset_a);

	auto asset_b = MakeSnapshot(5u, layout);
	asset_b->asset_id = 202u;
	asset_b->residency_revision = 4u;
	asset_b->components[0] = MakePaintedComponent({ 0u, 0u }, 5u);
	REQUIRE(asset.accept_snapshot(asset_b, &error));
	AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
		asset, { { 0u, 0u } });
	REQUIRE(asset.pending_weight_update_count() == 1u);

	CHECK_FALSE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_weight_update(asset, stale_a_upload.front()));
	CHECK(asset.accepted_snapshot() == asset_b);
	CHECK(asset.pending_weight_update_count() == 1u);
	const auto pending_b =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_weight_updates(asset);
	REQUIRE(pending_b.size() == 1u);
	CHECK(pending_b[0].asset_id == 202u);
	CHECK(pending_b[0].accepted_snapshot == asset_b);
	CHECK(pending_b[0].component == asset_b->components[0]);
}

TEST_CASE("Terrain capture rejects a stale visible snapshot with the same tuple")
{
	const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
	auto snapshot_a = MakeSnapshot(5u, layout);
	snapshot_a->asset_id = 101u;
	snapshot_a->residency_revision = 4u;
	snapshot_a->components[0] = MakeComponent({ 0u, 0u }, 5u);
	auto render_asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	REQUIRE(render_asset->accept_snapshot(snapshot_a));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(*render_asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		publish_active_snapshot(*render_asset));

	auto snapshot_b = MakeSnapshot(5u, layout);
	snapshot_b->asset_id = 202u;
	snapshot_b->residency_revision = 4u;
	snapshot_b->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(render_asset->accept_snapshot(snapshot_b));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		complete_front_height_upload(*render_asset));
	REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
		publish_active_snapshot(*render_asset));

	AshEngine::VisibleRenderFrame frame{};
	AshEngine::VisibleTerrainFrame visible{};
	visible.asset_snapshot = snapshot_a;
	visible.render_asset = render_asset;
	frame.terrains.push_back(visible);
	AshEngine::TerrainRenderPass pass{};
	CHECK_FALSE(pass.is_capture_ready(frame));
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
	AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
		painted_asset, { { 0u, 0u } });
	CHECK(painted_asset.pending_weight_update_count() == 1u);
	CHECK(painted_asset.pending_weight_payload_bytes() == 0u);

	auto implicit_snapshot = MakeSnapshot(2u, MakeRenderLayout(1u, 1u));
	implicit_snapshot->components[0] = component;
	AshEngine::TerrainRenderAsset implicit_asset{};
	REQUIRE(implicit_asset.accept_snapshot(implicit_snapshot, &error));
	AshEngine::TerrainRenderAssetCpuTestSeam::record_candidate_required(
		implicit_asset, { { 0u, 0u } });
	CHECK(implicit_asset.pending_weight_update_count() == 0u);
	CHECK(AshEngine::TerrainRenderAssetCpuTestSeam::
		candidate_initial_set(implicit_asset).empty());
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
	auto baseline = MakeSnapshot(0u);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, baseline);
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
	auto baseline = MakeSnapshot(0u);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, baseline);
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
	auto baseline = MakeSnapshot(0u);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, baseline);
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
	auto baseline = MakeSnapshot(0u);
	AshEngine::TerrainRenderAssetCpuTestSeam::install_published_bundle(
		asset, baseline);
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
	CHECK(asset.pending_component_removal_count() == 0u);
	const auto recovery_resets =
		AshEngine::TerrainRenderAssetCpuTestSeam::pending_implicit_weight_resets(asset);
	CHECK(recovery_resets.empty());
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

TEST_CASE("Terrain render asset manager recovers a rejected latest attempt idempotently")
{
	SUBCASE("published asset accepts a newer recovery without losing its view")
	{
		AshEngine::RenderAssetManager manager{};
		const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
		auto accepted = MakeSnapshot(1u, layout);
		accepted->components[0] = MakeComponent({ 0u, 0u }, 1u);
		const auto asset = manager.request_terrain_asset(
			"terrain/RejectedReady.AshTerrain", accepted);
		REQUIRE(asset != nullptr);
		REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
			complete_front_height_upload(*asset));
		REQUIRE(AshEngine::TerrainRenderAssetCpuTestSeam::
			publish_active_snapshot(*asset));
		REQUIRE(manager.finalize_pending_terrain_asset(asset));
		AshEngine::TerrainRenderAssetCpuTestSeam::
			install_layout_dependent_resource_sentinels(*asset);
		const auto ready = manager.query_readiness();
		REQUIRE_FALSE(ready.pending);
		REQUIRE_FALSE(ready.failed);

		auto rejected = MakeSnapshot(2u, MakeRenderLayout(8u, 16u));
		FillCompleteSnapshot(rejected);
		rejected->components[1] = rejected->components[0];
		CHECK(manager.request_terrain_asset(
			"terrain/rejectedready.ashterrain", rejected) == asset);
		const auto failed = manager.query_readiness();
		CHECK_FALSE(failed.pending);
		CHECK(failed.failed);

		CHECK(manager.request_terrain_asset(
			"terrain/rejectedready.ashterrain", rejected) == asset);
		CHECK(manager.query_readiness().activity_epoch == failed.activity_epoch);

		auto recovered_snapshot = MakeSnapshot(3u, layout);
		recovered_snapshot->components[0] = accepted->components[0];
		CHECK(manager.request_terrain_asset(
			"terrain/rejectedready.ashterrain", recovered_snapshot) == asset);
		const auto recovered = manager.query_readiness();
		CHECK(recovered.pending);
		CHECK_FALSE(recovered.failed);
		CHECK(recovered.activity_epoch == failed.activity_epoch + 1u);
		CHECK(manager.request_terrain_asset(
			"terrain/rejectedready.ashterrain", recovered_snapshot) == asset);
		CHECK(manager.query_readiness().activity_epoch == recovered.activity_epoch);
		manager.shutdown();
	}

	SUBCASE("pending accepted snapshot restores its pending owner")
	{
		AshEngine::RenderAssetManager manager{};
		const AshEngine::TerrainGridLayout layout = MakeRenderLayout(1u, 1u);
		auto accepted = MakeSnapshot(1u, layout);
		accepted->components[0] = MakeComponent({ 0u, 0u }, 1u);
		const auto asset = manager.request_terrain_asset(
			"terrain/RejectedPending.AshTerrain", accepted);
		REQUIRE(asset != nullptr);
		AshEngine::TerrainRenderAssetCpuTestSeam::
			install_layout_dependent_resource_sentinels(*asset);

		auto rejected = MakeSnapshot(2u, MakeRenderLayout(8u, 16u));
		FillCompleteSnapshot(rejected);
		rejected->components[1] = rejected->components[0];
		CHECK(manager.request_terrain_asset(
			"terrain/rejectedpending.ashterrain", rejected) == nullptr);
		const auto failed = manager.query_readiness();
		CHECK_FALSE(failed.pending);
		CHECK(failed.failed);

		auto recovered_snapshot = MakeSnapshot(3u, layout);
		recovered_snapshot->components[0] = accepted->components[0];
		CHECK(manager.request_terrain_asset(
			"terrain/rejectedpending.ashterrain", recovered_snapshot) == asset);
		const auto recovered = manager.query_readiness();
		CHECK(recovered.pending);
		CHECK_FALSE(recovered.failed);
		CHECK(recovered.activity_epoch == failed.activity_epoch + 1u);
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
