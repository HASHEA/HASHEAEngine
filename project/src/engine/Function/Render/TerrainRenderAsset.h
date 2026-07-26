#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"
#include "Function/Render/RenderDevice.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AshEngine
{
	class Renderer;
	class RenderAssetManager;
	class TerrainRenderPass;
	struct TerrainRenderAssetCpuTestSeam;

	static constexpr uint32_t k_terrain_render_component_capacity =
		k_terrain_component_count * k_terrain_component_count;
	static constexpr uint32_t k_terrain_render_height_words_per_component =
		(k_terrain_component_sample_count * k_terrain_component_sample_count + 1u) / 2u;
	static constexpr uint32_t k_terrain_weight_atlas_slot_count = 256u;
	static constexpr uint32_t k_terrain_weight_atlas_slot_extent =
		k_terrain_component_sample_count + 2u;
	static constexpr uint32_t k_terrain_weight_atlas_extent =
		16u * k_terrain_weight_atlas_slot_extent;
	static constexpr uint32_t k_terrain_coarse_weight_extent = 1025u;
	static constexpr uint32_t k_terrain_weight_upload_bytes =
		k_terrain_component_sample_count * k_terrain_component_sample_count * 8u;
	// ByteAddressBuffer requires a raw SRV rather than a structured stride.
	static constexpr uint32_t k_terrain_weight_upload_stride = 0u;

	struct ASH_API TerrainRenderLayoutInfo
	{
		TerrainGridLayout layout{};
		uint32_t component_count = 0u;
		uint32_t component_row_stride = 0u;
		uint64_t height_buffer_bytes = 0u;
		uint32_t coarse_width = 0u;
		uint32_t coarse_height = 0u;

		auto component_linear_index(TerrainComponentCoord coord) const -> size_t;
		auto contains(TerrainComponentCoord coord) const -> bool;
	};

	ASH_API auto derive_terrain_render_layout(
		const TerrainGridLayout& layout,
		TerrainRenderLayoutInfo& out_info,
		std::string* out_error = nullptr) -> bool;

	enum class TerrainRenderReadiness : uint8_t
	{
		Pending = 0,
		Ready,
		Failed
	};

	struct ASH_API TerrainFallbackMaterialArrays
	{
		std::array<std::shared_ptr<RenderTarget>, 3> arrays{};

		bool is_valid() const;
	};

	// Immutable, one-lock view of every TerrainRenderAsset field that can change
	// the depth recorded for a static shadow cache tile.
	struct ASH_API TerrainShadowCasterIdentity
	{
		uint64_t accepted_snapshot_identity = 0u;
		uint64_t accepted_asset_id = 0u;
		uint64_t accepted_content_generation = 0u;
		uint64_t accepted_residency_revision = 0u;
		uint64_t active_content_generation = 0u;
		uint64_t published_content_generation = 0u;
		uint32_t required_upload_count = 0u;
		uint32_t completed_upload_count = 0u;
		uint32_t pending_component_upload_count = 0u;
		uint32_t pending_component_removal_count = 0u;
		TerrainRenderReadiness readiness = TerrainRenderReadiness::Pending;
		bool has_accepted_snapshot = false;
	};

	class ASH_API TerrainRenderAssetState
	{
	public:
		void begin_content_generation(uint64_t content_generation, uint32_t required_uploads);
		bool mark_component_uploaded(
			uint64_t content_generation,
			TerrainComponentCoord coord);
		bool publish_content_generation(uint64_t content_generation);
		void mark_failed(uint64_t content_generation);
		TerrainRenderReadiness readiness() const;
		uint64_t active_content_generation() const;
		uint64_t published_content_generation() const;
		uint32_t required_upload_count() const;
		uint32_t completed_upload_count() const;

	private:
		void begin_snapshot_for_layout(
			uint64_t content_generation,
			uint64_t residency_revision,
			uint32_t required_uploads,
			const TerrainRenderLayoutInfo& layout_info);
		void begin_content_generation_for_layout(
			uint64_t content_generation,
			uint32_t required_uploads,
			const TerrainRenderLayoutInfo& layout_info,
			bool reset_generation_identity);
		bool mark_component_uploaded_for_snapshot(
			uint64_t content_generation,
			uint64_t residency_revision,
			TerrainComponentCoord coord);
		bool publish_snapshot(
			uint64_t content_generation,
			uint64_t residency_revision);
		void mark_snapshot_failed(
			uint64_t content_generation,
			uint64_t residency_revision);

		std::array<uint64_t, 16> m_completed_component_mask{};
		uint64_t m_active_content_generation = 0u;
		uint64_t m_active_residency_revision = 0u;
		uint64_t m_published_content_generation = 0u;
		uint64_t m_published_residency_revision = 0u;
		uint32_t m_required_upload_count = 0u;
		uint32_t m_completed_upload_count = 0u;
		TerrainRenderReadiness m_readiness = TerrainRenderReadiness::Pending;
		bool m_has_active_content_generation = false;
		uint32_t m_component_count = k_terrain_render_component_capacity;
		uint32_t m_component_row_stride = k_terrain_component_count;
		uint32_t m_component_count_z = k_terrain_component_count;
		friend class TerrainRenderAsset;
		friend struct TerrainRenderAssetCpuTestSeam;
	};

	struct TerrainGpuComponentUpload
	{
		uint64_t asset_id = 0u;
		std::weak_ptr<const TerrainAssetSnapshot> accepted_snapshot{};
		TerrainComponentCoord coord{};
		uint64_t content_generation = 0u;
		uint64_t residency_revision = 0u;
		std::shared_ptr<const TerrainComponentSnapshot> component{};
	};

	struct TerrainAtlasSlotMetadata
	{
		uint64_t asset_id = 0u;
		TerrainComponentCoord coord{};
		uint64_t content_generation = 0u;
		uint64_t residency_revision = 0u;
		uint64_t last_used_frame = 0u;
		bool occupied = false;
	};

	struct ASH_API TerrainRenderResourceSet
	{
		std::shared_ptr<StorageBuffer> height{};
		std::shared_ptr<StorageBuffer> staging{};
		std::array<std::shared_ptr<RenderTarget>, 2> atlas{};
		std::shared_ptr<RenderTarget> coarse{};

		bool is_complete() const;
	};

	enum class TerrainRenderWorkStatus : uint8_t
	{
		Pending = 0,
		ReadyToPublish,
		Ready,
		Failed
	};

	enum class TerrainRenderCandidateStage : uint8_t
	{
		CreateResources = 0,
		UploadHeights,
		AwaitGraphWork,
		ReadyToPublish,
		Failed
	};

	struct ASH_API TerrainRenderRuntimeState
	{
		TerrainRenderResourceSet resources{};
		std::array<TerrainAtlasSlotMetadata, k_terrain_weight_atlas_slot_count>
			slots{};
		std::vector<TerrainGpuComponentUpload> height_queue{};
		std::vector<TerrainGpuComponentUpload> weight_queue{};
		std::vector<TerrainComponentCoord> reset_queue{};
		std::vector<TerrainComponentCoord> removal_queue{};
		TerrainRenderAssetState state{};
		std::shared_ptr<const TerrainAssetSnapshot> target_snapshot{};
		std::vector<TerrainComponentCoord> latest_required_residency{};
		TerrainRenderWorkStatus work_status = TerrainRenderWorkStatus::Pending;
		uint64_t last_atlas_completion_frame = 0u;
		bool has_atlas_completion = false;
	};

	struct TerrainPublishedRenderView;

	struct ASH_API TerrainRenderCandidateState
	{
		std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		TerrainRenderLayoutInfo layout{};
		std::shared_ptr<TerrainRenderRuntimeState> runtime{};
		std::shared_ptr<TerrainPublishedRenderView> prepared_view{};
		std::vector<TerrainComponentCoord> initial_resident_set{};
		std::vector<TerrainGpuComponentUpload> coarse_work{};
		TerrainRenderCandidateStage stage =
			TerrainRenderCandidateStage::CreateResources;
		TerrainRenderWorkStatus work_status = TerrainRenderWorkStatus::Pending;
		uint64_t candidate_epoch = 0u;
		uint64_t peak_resource_bytes = 0u;
		uint64_t last_progress_frame_index = 0u;
		std::string error{};
		bool initial_set_frozen = false;
	};

	struct ASH_API TerrainPublishedRenderView
	{
		std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		TerrainRenderLayoutInfo layout{};
		std::shared_ptr<TerrainRenderRuntimeState> runtime{};
		uint64_t asset_id = 0u;
		uint64_t content_generation = 0u;
		uint64_t residency_revision = 0u;
		uint64_t publication_epoch = 0u;
	};

	// Function-private adapter seam. Production forwards to Renderer/resources;
	// tests inject sentinel resources and deterministic stage failures.
	class TerrainRenderGpuOps
	{
	public:
		virtual ~TerrainRenderGpuOps() = default;
		virtual std::shared_ptr<StorageBuffer> create_storage_buffer(
			const StorageBufferDesc& desc) = 0;
		virtual std::shared_ptr<RenderTarget> create_render_target(
			const RenderTargetDesc& desc) = 0;
		virtual bool update_storage_buffer(
			const std::shared_ptr<StorageBuffer>& buffer,
			uint32_t offset,
			uint32_t size,
			const void* data) = 0;
	};

	ASH_API bool build_terrain_component_gpu_data(
		const TerrainComponentSnapshot& component,
		const TerrainHeightMapping& height_mapping,
		std::vector<uint32_t>& out_packed_height_words,
		std::array<std::vector<uint8_t>, 2>& out_weight_rgba8,
		std::string* out_error = nullptr);
	ASH_API bool build_terrain_component_height_words(
		const TerrainComponentSnapshot& component,
		const TerrainHeightMapping& height_mapping,
		std::vector<uint32_t>& out_packed_height_words,
		std::string* out_error = nullptr);
	ASH_API bool build_terrain_component_weight_rgba8(
		const TerrainComponentSnapshot& component,
		std::array<std::vector<uint8_t>, 2>& out_weight_rgba8,
		std::string* out_error = nullptr);
	ASH_API bool terrain_upload_budget_allows_next(
		uint64_t completed_bytes,
		uint64_t next_upload_bytes,
		uint64_t byte_budget,
		std::chrono::steady_clock::duration elapsed,
		std::chrono::steady_clock::duration wall_clock_budget);

	class ASH_API TerrainRenderAsset
	{
	public:
		TerrainRenderAsset();
		~TerrainRenderAsset();

		TerrainRenderAsset(const TerrainRenderAsset&) = delete;
		TerrainRenderAsset& operator=(const TerrainRenderAsset&) = delete;

	public:
		bool accept_snapshot(
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
			std::string* out_error = nullptr);
		TerrainRenderReadiness readiness() const;
		uint64_t accepted_content_generation() const;
		uint64_t accepted_residency_revision() const;
		uint64_t published_content_generation() const;
		uint64_t published_residency_revision() const;
		uint32_t pending_component_upload_count() const;
		uint64_t pending_cpu_payload_bytes() const;
		uint64_t pending_weight_payload_bytes() const;
		uint32_t pending_weight_update_count() const;
		bool has_pending_component_upload(TerrainComponentCoord coord) const;
		uint32_t pending_component_removal_count() const;
		bool has_pending_component_removal(TerrainComponentCoord coord) const;
		std::shared_ptr<const TerrainAssetSnapshot> accepted_snapshot() const;
		std::shared_ptr<const TerrainPublishedRenderView> published_view() const;
		TerrainRenderWorkStatus latest_work_status() const;
		TerrainShadowCasterIdentity snapshot_shadow_caster_identity() const;
		std::string get_last_error() const;

		std::shared_ptr<StorageBuffer> packed_height_buffer() const;
		std::shared_ptr<StorageBuffer> dirty_weight_staging_buffer() const;
		std::shared_ptr<RenderTarget> weight_atlas(uint32_t index) const;
		std::shared_ptr<RenderTarget> coarse_weight_target() const;
		std::shared_ptr<RenderTarget> material_texture_array(uint32_t index) const;
		bool set_fallback_material_arrays(
			const std::shared_ptr<const TerrainFallbackMaterialArrays>& arrays);

	private:
		bool accept_snapshot_with_peak_budget(
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
			uint64_t peak_budget,
			std::string* out_error);
		bool matches_pending_weight_update_locked(
			const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
			const TerrainGpuComponentUpload& expected) const;
		bool finalize_gpu_resources(Renderer& renderer, std::string* out_error);
		bool finalize_gpu_resources(
			TerrainRenderGpuOps& gpu_ops,
			std::string* out_error);
		bool finalize_runtime_gpu_resources_locked(
			TerrainRenderRuntimeState& runtime,
			const TerrainRenderLayoutInfo& layout,
			TerrainRenderGpuOps& gpu_ops,
			bool candidate,
			std::string* out_error);
		void fail_latest_work_locked(const std::string& error);
		void fail_candidate_locked(
			const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
			uint64_t candidate_epoch,
			const std::string& error);
		void freeze_candidate_initial_residency_locked(uint64_t render_frame_index);
		bool complete_candidate_coarse_locked(
			const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
			uint64_t candidate_epoch,
			const TerrainGpuComponentUpload& expected,
			bool succeeded,
			uint64_t render_frame_index);
		bool complete_candidate_initial_locked(
			const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
			uint64_t candidate_epoch,
			const TerrainGpuComponentUpload& expected,
			bool succeeded,
			uint64_t render_frame_index);
		bool publish_ready_candidate_locked();
		void update_candidate_ready_locked();
		void record_required_residency(
			const std::shared_ptr<const TerrainPublishedRenderView>& view,
			const std::vector<TerrainComponentCoord>& required,
			uint64_t render_frame_index);
		static std::vector<TerrainComponentCoord> select_initial_resident_set(
			const TerrainAssetSnapshot& snapshot,
			const std::array<TerrainAtlasSlotMetadata,
				k_terrain_weight_atlas_slot_count>& old_slots,
			const std::vector<TerrainComponentCoord>& required);
		std::shared_ptr<const TerrainAssetSnapshot>
			latest_admitted_snapshot_locked() const;
		std::shared_ptr<TerrainRenderRuntimeState>
			current_incremental_runtime_locked() const;
		uint64_t allocate_candidate_epoch_locked();

	private:
		mutable std::mutex m_mutex{};
		std::string m_asset_path{};
		std::shared_ptr<const TerrainPublishedRenderView> m_published_view{};
		std::unique_ptr<TerrainRenderCandidateState> m_candidate_state{};
		uint64_t m_next_candidate_epoch = 1u;
		std::string m_last_error{};
		std::shared_ptr<const TerrainFallbackMaterialArrays>
			m_fallback_material_arrays{};
		friend class RenderAssetManager;
		friend class TerrainRenderPass;
		friend struct TerrainRenderAssetCpuTestSeam;
		friend struct TerrainRenderGraphTestSeam;
		friend struct TerrainRenderSceneTestSeam;
	};
}
