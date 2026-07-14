#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"
#include "Function/Render/RenderDevice.h"

#include <array>
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

	enum class TerrainRenderReadiness : uint8_t
	{
		Pending = 0,
		Ready,
		Failed
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
		std::array<uint64_t, 16> m_completed_component_mask{};
		uint64_t m_active_content_generation = 0u;
		uint64_t m_published_content_generation = 0u;
		uint32_t m_required_upload_count = 0u;
		uint32_t m_completed_upload_count = 0u;
		TerrainRenderReadiness m_readiness = TerrainRenderReadiness::Pending;
		bool m_has_active_content_generation = false;
	};

	ASH_API bool build_terrain_component_gpu_data(
		const TerrainComponentSnapshot& component,
		const TerrainHeightMapping& height_mapping,
		std::vector<uint32_t>& out_packed_height_words,
		std::array<std::vector<uint8_t>, 2>& out_weight_rgba8,
		std::string* out_error = nullptr);

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
		uint64_t published_content_generation() const;
		uint32_t pending_component_upload_count() const;
		bool has_pending_component_upload(TerrainComponentCoord coord) const;
		uint32_t pending_component_removal_count() const;
		bool has_pending_component_removal(TerrainComponentCoord coord) const;
		std::shared_ptr<const TerrainAssetSnapshot> accepted_snapshot() const;
		std::string get_last_error() const;

		std::shared_ptr<StorageBuffer> packed_height_buffer() const;
		std::shared_ptr<StorageBuffer> dirty_weight_staging_buffer() const;
		std::shared_ptr<RenderTarget> weight_atlas(uint32_t index) const;
		std::shared_ptr<RenderTarget> coarse_weight_target() const;
		std::shared_ptr<RenderTarget> material_texture_array(uint32_t index) const;

	private:
		struct TerrainGpuComponentUpload
		{
			TerrainComponentCoord coord{};
			uint64_t content_generation = 0u;
			std::vector<uint32_t> packed_height_words{};
			std::array<std::vector<uint8_t>, 2> weight_rgba8{};
		};

		struct TerrainAtlasSlotMetadata
		{
			TerrainComponentCoord coord{};
			uint64_t content_generation = 0u;
			uint64_t last_used_frame = 0u;
			bool occupied = false;
			bool pinned = false;
		};

		bool finalize_gpu_resources(Renderer& renderer, std::string* out_error);
		void fail_active_generation(const std::string& error);

	private:
		mutable std::mutex m_mutex{};
		std::string m_asset_path{};
		std::shared_ptr<const TerrainAssetSnapshot> m_accepted_snapshot{};
		std::vector<TerrainGpuComponentUpload> m_pending_component_uploads{};
		std::vector<TerrainComponentCoord> m_pending_component_removals{};
		TerrainRenderAssetState m_state{};
		std::string m_last_error{};

		std::shared_ptr<StorageBuffer> m_packed_height_buffer{};
		std::shared_ptr<StorageBuffer> m_dirty_weight_staging_buffer{};
		std::array<std::shared_ptr<RenderTarget>, 2> m_weight_atlases{};
		std::shared_ptr<RenderTarget> m_coarse_weight_target{};
		std::array<std::shared_ptr<RenderTarget>, 3> m_material_texture_arrays{};
		std::array<TerrainAtlasSlotMetadata, k_terrain_weight_atlas_slot_count>
			m_frame_boundary_atlas_slots{};
		friend class RenderAssetManager;
		friend class TerrainRenderPass;
	};
}
