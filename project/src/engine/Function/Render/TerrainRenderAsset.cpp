#include "Function/Render/TerrainRenderAsset.h"

#include "Function/Render/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static constexpr uint32_t k_material_array_extent = 1024u;
		static constexpr uint8_t k_material_array_mip_count = 11u;

		auto fail_with_error(std::string* out_error, const char* message) -> bool
		{
			if (out_error)
			{
				*out_error = message;
			}
			return false;
		}

		auto is_supported_render_layout(const TerrainGridLayout& layout) -> bool
		{
			return layout.sample_count_x == k_terrain_sample_count &&
				layout.sample_count_z == k_terrain_sample_count &&
				layout.component_count_x == k_terrain_component_count &&
				layout.component_count_z == k_terrain_component_count &&
				layout.component_quad_count == k_terrain_component_quad_count &&
				std::isfinite(layout.sample_spacing_meters) &&
				layout.sample_spacing_meters > 0.0f;
		}

		auto component_linear_index(TerrainComponentCoord coord) -> uint32_t
		{
			return static_cast<uint32_t>(coord.z) * k_terrain_component_count + coord.x;
		}

		auto create_fallback_material_array(
			Renderer& renderer,
			RenderTextureFormat format,
			const std::array<uint8_t, 4>& value,
			const char* name) -> std::shared_ptr<RenderTarget>
		{
			std::vector<uint8_t> pixels(
				static_cast<size_t>(k_material_array_extent) *
				k_material_array_extent * 4u);
			for (size_t offset = 0u; offset < pixels.size(); offset += 4u)
			{
				pixels[offset] = value[0];
				pixels[offset + 1u] = value[1];
				pixels[offset + 2u] = value[2];
				pixels[offset + 3u] = value[3];
			}

			std::array<TextureSubresourceUploadDesc,
				k_terrain_material_layer_count * k_material_array_mip_count> subresources{};
			for (uint32_t layer = 0u; layer < k_terrain_material_layer_count; ++layer)
			{
				for (uint32_t mip = 0u; mip < k_material_array_mip_count; ++mip)
				{
					const uint32_t mip_extent = std::max<uint32_t>(
						1u, k_material_array_extent >> mip);
					TextureSubresourceUploadDesc& subresource =
						subresources[static_cast<size_t>(layer) *
							k_material_array_mip_count + mip];
					subresource.mip_level = mip;
					subresource.array_layer = layer;
					subresource.data = pixels.data();
					subresource.row_pitch = mip_extent * 4u;
					subresource.slice_pitch = mip_extent * mip_extent * 4u;
				}
			}

			Texture2DArrayUploadDesc desc{};
			desc.width = static_cast<uint16_t>(k_material_array_extent);
			desc.height = static_cast<uint16_t>(k_material_array_extent);
			desc.format = format;
			desc.array_layer_count = k_terrain_material_layer_count;
			desc.mip_level_count = k_material_array_mip_count;
			desc.subresources = subresources.data();
			desc.subresource_count = static_cast<uint32_t>(subresources.size());
			desc.name = name;
			return renderer.create_texture_2d_array(desc);
		}
	}

	void TerrainRenderAssetState::begin_content_generation(
		uint64_t content_generation,
		uint32_t required_uploads)
	{
		if (m_has_active_content_generation &&
			content_generation <= m_active_content_generation)
		{
			return;
		}

		m_has_active_content_generation = true;
		m_active_content_generation = content_generation;
		m_required_upload_count = required_uploads;
		m_completed_upload_count = 0u;
		m_completed_component_mask.fill(0u);
		m_readiness = required_uploads <= k_terrain_render_component_capacity ?
			TerrainRenderReadiness::Pending : TerrainRenderReadiness::Failed;
	}

	bool TerrainRenderAssetState::mark_component_uploaded(
		uint64_t content_generation,
		TerrainComponentCoord coord)
	{
		if (!m_has_active_content_generation ||
			content_generation != m_active_content_generation ||
			m_readiness != TerrainRenderReadiness::Pending ||
			coord.x >= k_terrain_component_count ||
			coord.z >= k_terrain_component_count ||
			m_completed_upload_count >= m_required_upload_count)
		{
			return false;
		}

		const uint32_t bit_index = component_linear_index(coord);
		const uint32_t word_index = bit_index / 64u;
		const uint64_t bit = uint64_t{ 1u } << (bit_index % 64u);
		if ((m_completed_component_mask[word_index] & bit) != 0u)
		{
			return false;
		}

		m_completed_component_mask[word_index] |= bit;
		++m_completed_upload_count;
		return true;
	}

	bool TerrainRenderAssetState::publish_content_generation(
		uint64_t content_generation)
	{
		if (!m_has_active_content_generation ||
			content_generation != m_active_content_generation ||
			m_readiness != TerrainRenderReadiness::Pending ||
			m_completed_upload_count != m_required_upload_count)
		{
			return false;
		}

		m_published_content_generation = content_generation;
		m_readiness = TerrainRenderReadiness::Ready;
		return true;
	}

	void TerrainRenderAssetState::mark_failed(uint64_t content_generation)
	{
		if (m_has_active_content_generation &&
			content_generation == m_active_content_generation)
		{
			m_readiness = TerrainRenderReadiness::Failed;
		}
	}

	TerrainRenderReadiness TerrainRenderAssetState::readiness() const
	{
		return m_readiness;
	}

	uint64_t TerrainRenderAssetState::active_content_generation() const
	{
		return m_active_content_generation;
	}

	uint64_t TerrainRenderAssetState::published_content_generation() const
	{
		return m_published_content_generation;
	}

	uint32_t TerrainRenderAssetState::required_upload_count() const
	{
		return m_required_upload_count;
	}

	uint32_t TerrainRenderAssetState::completed_upload_count() const
	{
		return m_completed_upload_count;
	}

	bool build_terrain_component_gpu_data(
		const TerrainComponentSnapshot& component,
		const TerrainHeightMapping& height_mapping,
		std::vector<uint32_t>& out_packed_height_words,
		std::array<std::vector<uint8_t>, 2>& out_weight_rgba8,
		std::string* out_error)
	{
		if (out_error)
		{
			out_error->clear();
		}
		if (component.sample_width != k_terrain_component_sample_count ||
			component.sample_height != k_terrain_component_sample_count)
		{
			return fail_with_error(
				out_error, "terrain component dimensions must be 257 x 257.");
		}

		constexpr size_t sample_count =
			static_cast<size_t>(k_terrain_component_sample_count) *
			k_terrain_component_sample_count;
		if (component.heights.size() != sample_count)
		{
			return fail_with_error(
				out_error, "terrain component height count must match the sample count.");
		}
		if (!component.weights.empty() && component.weights.size() != sample_count)
		{
			return fail_with_error(
				out_error,
				"terrain component weight count must be zero or match the sample count.");
		}
		if (!std::isfinite(height_mapping.height_offset) ||
			!std::isfinite(height_mapping.height_range) ||
			height_mapping.height_range <= 0.0f)
		{
			return fail_with_error(out_error, "terrain height mapping is invalid.");
		}

		std::vector<uint32_t> packed_height_words(
			(sample_count + 1u) / 2u, 0u);
		for (size_t sample = 0u; sample < sample_count; ++sample)
		{
			const float height = component.heights[sample];
			if (!std::isfinite(height))
			{
				return fail_with_error(
					out_error, "terrain component heights must be finite.");
			}

			const uint32_t encoded = encode_terrain_height_r16(height, height_mapping);
			const uint32_t shift = (sample & 1u) == 0u ? 0u : 16u;
			packed_height_words[sample / 2u] |= encoded << shift;
		}

		std::array<std::vector<uint8_t>, 2> weight_rgba8{};
		weight_rgba8[0].resize(sample_count * 4u, 0u);
		weight_rgba8[1].resize(sample_count * 4u, 0u);
		for (size_t sample = 0u; sample < sample_count; ++sample)
		{
			std::array<uint8_t, k_terrain_material_layer_count> weights{};
			if (component.weights.empty())
			{
				weights[0] = 255u;
			}
			else
			{
				weights = component.weights[sample];
				uint32_t sum = 0u;
				for (uint8_t weight : weights)
				{
					sum += weight;
				}
				if (sum != 255u)
				{
					return fail_with_error(
						out_error,
						"terrain component weights must sum to 255 for every sample.");
				}
			}

			const size_t output_offset = sample * 4u;
			for (size_t channel = 0u; channel < 4u; ++channel)
			{
				weight_rgba8[0][output_offset + channel] = weights[channel];
				weight_rgba8[1][output_offset + channel] = weights[channel + 4u];
			}
		}

		out_packed_height_words = std::move(packed_height_words);
		out_weight_rgba8 = std::move(weight_rgba8);
		return true;
	}

	TerrainRenderAsset::TerrainRenderAsset() = default;
	TerrainRenderAsset::~TerrainRenderAsset() = default;

	bool TerrainRenderAsset::accept_snapshot(
		const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
		std::string* out_error)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (out_error)
		{
			out_error->clear();
		}
		if (!snapshot)
		{
			return fail_with_error(out_error, "terrain snapshot must not be null.");
		}
		if (m_accepted_snapshot == snapshot)
		{
			if (m_state.readiness() == TerrainRenderReadiness::Failed)
			{
				return fail_with_error(
					out_error,
					m_last_error.empty() ? "terrain snapshot is failed." :
						m_last_error.c_str());
			}
			return true;
		}
		if (m_accepted_snapshot &&
			snapshot->content_generation <= m_accepted_snapshot->content_generation)
		{
			return fail_with_error(
				out_error, "terrain snapshot content generation is stale.");
		}
		const auto reject_snapshot =
			[&](std::string error)
			{
				m_state.begin_content_generation(snapshot->content_generation, 0u);
				m_state.mark_failed(snapshot->content_generation);
				m_accepted_snapshot = snapshot;
				m_pending_component_uploads.clear();
				m_pending_component_removals.clear();
				m_last_error = std::move(error);
				return fail_with_error(out_error, m_last_error.c_str());
			};

		if (snapshot->failed)
		{
			return reject_snapshot(snapshot->failure_detail.empty() ?
				"terrain snapshot is failed." : snapshot->failure_detail);
		}
		if (!is_supported_render_layout(snapshot->layout))
		{
			return reject_snapshot(
				"terrain snapshot layout is not the fixed Phase 2 render layout.");
		}

		const size_t expected_component_count =
			static_cast<size_t>(snapshot->layout.component_count_x) *
			snapshot->layout.component_count_z;
		if (snapshot->components.size() != expected_component_count)
		{
			return reject_snapshot(
				"terrain snapshot component table must contain 1024 entries.");
		}

		const bool rebuild_after_failure =
			m_state.readiness() == TerrainRenderReadiness::Failed;
		std::vector<TerrainGpuComponentUpload> uploads{};
		std::vector<TerrainComponentCoord> removals{};
		for (size_t index = 0u; index < snapshot->components.size(); ++index)
		{
			const std::shared_ptr<const TerrainComponentSnapshot>& component =
				snapshot->components[index];
			const TerrainComponentCoord expected_coord{
				static_cast<uint16_t>(index % k_terrain_component_count),
				static_cast<uint16_t>(index / k_terrain_component_count)
			};
			const std::shared_ptr<const TerrainComponentSnapshot> previous_component =
				m_accepted_snapshot && index < m_accepted_snapshot->components.size() ?
					m_accepted_snapshot->components[index] : nullptr;
			if (!rebuild_after_failure && previous_component == component &&
				(m_accepted_snapshot || component))
			{
				continue;
			}
			if (!component)
			{
				if (rebuild_after_failure || previous_component)
				{
					removals.push_back(expected_coord);
				}
				continue;
			}

			if (!(component->coord == expected_coord))
			{
				return reject_snapshot(
					"terrain component coordinate does not match its row-major slot.");
			}
			TerrainGpuComponentUpload upload{};
			upload.coord = component->coord;
			upload.content_generation = snapshot->content_generation;
			std::string pack_error{};
			if (!build_terrain_component_gpu_data(
				*component,
				snapshot->height_mapping,
				upload.packed_height_words,
				upload.weight_rgba8,
				&pack_error))
			{
				return reject_snapshot(std::move(pack_error));
			}
			uploads.push_back(std::move(upload));
		}

		m_state.begin_content_generation(
			snapshot->content_generation,
			static_cast<uint32_t>(uploads.size() + removals.size()));
		m_accepted_snapshot = snapshot;
		m_pending_component_uploads = std::move(uploads);
		m_pending_component_removals = std::move(removals);
		m_last_error.clear();
		return true;
	}

	TerrainRenderReadiness TerrainRenderAsset::readiness() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_state.readiness();
	}

	uint64_t TerrainRenderAsset::accepted_content_generation() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_accepted_snapshot ? m_accepted_snapshot->content_generation : 0u;
	}

	uint64_t TerrainRenderAsset::published_content_generation() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_state.published_content_generation();
	}

	uint32_t TerrainRenderAsset::pending_component_upload_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_pending_component_uploads.size());
	}

	bool TerrainRenderAsset::has_pending_component_upload(TerrainComponentCoord coord) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return std::any_of(
			m_pending_component_uploads.begin(),
			m_pending_component_uploads.end(),
			[coord](const TerrainGpuComponentUpload& upload)
			{
				return upload.coord == coord;
			});
	}

	uint32_t TerrainRenderAsset::pending_component_removal_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_pending_component_removals.size());
	}

	bool TerrainRenderAsset::has_pending_component_removal(TerrainComponentCoord coord) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return std::find(
			m_pending_component_removals.begin(),
			m_pending_component_removals.end(),
			coord) != m_pending_component_removals.end();
	}

	std::shared_ptr<const TerrainAssetSnapshot> TerrainRenderAsset::accepted_snapshot() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_accepted_snapshot;
	}

	std::string TerrainRenderAsset::get_last_error() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_last_error;
	}

	std::shared_ptr<StorageBuffer> TerrainRenderAsset::packed_height_buffer() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_packed_height_buffer;
	}

	std::shared_ptr<StorageBuffer> TerrainRenderAsset::dirty_weight_staging_buffer() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_dirty_weight_staging_buffer;
	}

	std::shared_ptr<RenderTarget> TerrainRenderAsset::weight_atlas(uint32_t index) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return index < m_weight_atlases.size() ? m_weight_atlases[index] : nullptr;
	}

	std::shared_ptr<RenderTarget> TerrainRenderAsset::coarse_weight_target() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_coarse_weight_target;
	}

	std::shared_ptr<RenderTarget> TerrainRenderAsset::material_texture_array(uint32_t index) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return index < m_material_texture_arrays.size() ?
			m_material_texture_arrays[index] : nullptr;
	}

	void TerrainRenderAsset::fail_active_generation(const std::string& error)
	{
		m_state.mark_failed(m_state.active_content_generation());
		m_last_error = error;
	}

	bool TerrainRenderAsset::finalize_gpu_resources(
		Renderer& renderer,
		std::string* out_error)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (out_error)
		{
			out_error->clear();
		}
		if (m_state.readiness() == TerrainRenderReadiness::Ready)
		{
			return true;
		}
		if (m_state.readiness() == TerrainRenderReadiness::Failed ||
			!m_accepted_snapshot)
		{
			return fail_with_error(
				out_error,
				m_last_error.empty() ? "terrain render asset is not pending." :
					m_last_error.c_str());
		}

		if (!m_packed_height_buffer)
		{
			constexpr uint32_t height_buffer_size =
				k_terrain_render_height_words_per_component *
				k_terrain_render_component_capacity * sizeof(uint32_t);
			m_packed_height_buffer = renderer.create_storage_buffer({
				height_buffer_size,
				sizeof(uint32_t),
				false,
				false,
				nullptr,
				"TerrainHeightWords"
			});
		}
		if (!m_dirty_weight_staging_buffer)
		{
			constexpr uint32_t weight_staging_size =
				k_terrain_component_sample_count *
				k_terrain_component_sample_count * 8u;
			m_dirty_weight_staging_buffer = renderer.create_storage_buffer({
				weight_staging_size,
				sizeof(uint32_t),
				false,
				false,
				nullptr,
				"TerrainWeightUpload"
			});
		}
		if (!m_weight_atlases[0])
		{
			m_weight_atlases[0] = renderer.create_render_target({
				static_cast<uint16_t>(k_terrain_weight_atlas_extent),
				static_cast<uint16_t>(k_terrain_weight_atlas_extent),
				RenderTextureFormat::RGBA8_UNORM,
				true,
				true,
				"TerrainWeights0"
			});
		}
		if (!m_weight_atlases[1])
		{
			m_weight_atlases[1] = renderer.create_render_target({
				static_cast<uint16_t>(k_terrain_weight_atlas_extent),
				static_cast<uint16_t>(k_terrain_weight_atlas_extent),
				RenderTextureFormat::RGBA8_UNORM,
				true,
				true,
				"TerrainWeights1"
			});
		}
		if (!m_coarse_weight_target)
		{
			m_coarse_weight_target = renderer.create_render_target({
				static_cast<uint16_t>(k_terrain_coarse_weight_extent),
				static_cast<uint16_t>(k_terrain_coarse_weight_extent),
				RenderTextureFormat::RGBA8_UNORM,
				true,
				true,
				"TerrainCoarseWeights"
			});
		}
		if (!m_material_texture_arrays[0])
		{
			m_material_texture_arrays[0] = create_fallback_material_array(
				renderer,
				RenderTextureFormat::RGBA8_SRGB,
				{ 255u, 255u, 255u, 255u },
				"TerrainBaseColorLayers");
		}
		if (!m_material_texture_arrays[1])
		{
			m_material_texture_arrays[1] = create_fallback_material_array(
				renderer,
				RenderTextureFormat::RGBA8_UNORM,
				{ 128u, 128u, 255u, 255u },
				"TerrainNormalLayers");
		}
		if (!m_material_texture_arrays[2])
		{
			m_material_texture_arrays[2] = create_fallback_material_array(
				renderer,
				RenderTextureFormat::RGBA8_UNORM,
				{ 255u, 255u, 0u, 255u },
				"TerrainOrmLayers");
		}

		if (!m_packed_height_buffer ||
			!m_dirty_weight_staging_buffer ||
			!m_weight_atlases[0] ||
			!m_weight_atlases[1] ||
			!m_coarse_weight_target ||
			!m_material_texture_arrays[0] ||
			!m_material_texture_arrays[1] ||
			!m_material_texture_arrays[2])
		{
			fail_active_generation("failed to create Terrain GPU resources.");
			return fail_with_error(out_error, m_last_error.c_str());
		}

		constexpr uint32_t component_height_bytes =
			k_terrain_render_height_words_per_component * sizeof(uint32_t);
		const uint64_t content_generation = m_state.active_content_generation();
		for (const TerrainGpuComponentUpload& upload : m_pending_component_uploads)
		{
			const uint32_t offset =
				component_linear_index(upload.coord) * component_height_bytes;
			if (upload.packed_height_words.size() !=
				k_terrain_render_height_words_per_component ||
				!m_packed_height_buffer->update(
					offset,
					component_height_bytes,
					upload.packed_height_words.data()) ||
				!m_state.mark_component_uploaded(content_generation, upload.coord))
			{
				fail_active_generation("failed to upload Terrain component height data.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
		}
		for (TerrainComponentCoord coord : m_pending_component_removals)
		{
			for (TerrainAtlasSlotMetadata& slot : m_frame_boundary_atlas_slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					slot = {};
				}
			}
			if (!m_state.mark_component_uploaded(content_generation, coord))
			{
				fail_active_generation("failed to retire a Terrain component residency slot.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
		}

		if (!m_state.publish_content_generation(content_generation))
		{
			fail_active_generation("failed to publish Terrain content generation.");
			return fail_with_error(out_error, m_last_error.c_str());
		}
		m_last_error.clear();
		return true;
	}
}
