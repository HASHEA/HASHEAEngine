#include "Function/Render/TerrainRenderAsset.h"

#include "Function/Render/Renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
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
		static constexpr uint64_t k_height_upload_byte_budget = 4ull * 1024ull * 1024ull;
		static constexpr auto k_height_upload_wall_clock_budget =
			std::chrono::milliseconds(2);

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

		auto validate_component_shape(
			const TerrainComponentSnapshot& component,
			std::string* out_error) -> bool
		{
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
					out_error,
					"terrain component height count must match the sample count.");
			}
			if (!component.weights.empty() && component.weights.size() != sample_count)
			{
				return fail_with_error(
					out_error,
					"terrain component weight count must be zero or match the sample count.");
			}
			return true;
		}

	}

	bool TerrainFallbackMaterialArrays::is_valid() const
	{
		return std::all_of(
			arrays.begin(),
			arrays.end(),
			[](const std::shared_ptr<RenderTarget>& array)
			{
				return array != nullptr;
			});
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

	bool build_terrain_component_height_words(
		const TerrainComponentSnapshot& component,
		const TerrainHeightMapping& height_mapping,
		std::vector<uint32_t>& out_packed_height_words,
		std::string* out_error)
	{
		if (out_error)
		{
			out_error->clear();
		}
		if (!validate_component_shape(component, out_error))
		{
			return false;
		}
		if (!std::isfinite(height_mapping.height_offset) ||
			!std::isfinite(height_mapping.height_range) ||
			height_mapping.height_range <= 0.0f)
		{
			return fail_with_error(out_error, "terrain height mapping is invalid.");
		}
		constexpr size_t sample_count =
			static_cast<size_t>(k_terrain_component_sample_count) *
			k_terrain_component_sample_count;

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

		out_packed_height_words = std::move(packed_height_words);
		return true;
	}

	bool build_terrain_component_weight_rgba8(
		const TerrainComponentSnapshot& component,
		std::array<std::vector<uint8_t>, 2>& out_weight_rgba8,
		std::string* out_error)
	{
		if (out_error)
		{
			out_error->clear();
		}
		out_weight_rgba8 = {};
		if (!validate_component_shape(component, out_error))
		{
			return false;
		}
		if (component.weights.empty())
		{
			return true;
		}

		constexpr size_t sample_count =
			static_cast<size_t>(k_terrain_component_sample_count) *
			k_terrain_component_sample_count;
		for (const auto& weights : component.weights)
		{
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

		std::array<std::vector<uint8_t>, 2> weight_rgba8{};
		weight_rgba8[0].resize(sample_count * 4u, 0u);
		weight_rgba8[1].resize(sample_count * 4u, 0u);
		for (size_t sample = 0u; sample < sample_count; ++sample)
		{
			const auto& weights = component.weights[sample];
			const size_t output_offset = sample * 4u;
			for (size_t channel = 0u; channel < 4u; ++channel)
			{
				weight_rgba8[0][output_offset + channel] = weights[channel];
				weight_rgba8[1][output_offset + channel] = weights[channel + 4u];
			}
		}

		out_weight_rgba8 = std::move(weight_rgba8);
		return true;
	}

	bool build_terrain_component_gpu_data(
		const TerrainComponentSnapshot& component,
		const TerrainHeightMapping& height_mapping,
		std::vector<uint32_t>& out_packed_height_words,
		std::array<std::vector<uint8_t>, 2>& out_weight_rgba8,
		std::string* out_error)
	{
		if (!build_terrain_component_height_words(
			component, height_mapping, out_packed_height_words, out_error) ||
			!build_terrain_component_weight_rgba8(
				component, out_weight_rgba8, out_error))
		{
			return false;
		}
		if (out_weight_rgba8[0].empty())
		{
			constexpr size_t sample_count =
				static_cast<size_t>(k_terrain_component_sample_count) *
				k_terrain_component_sample_count;
			out_weight_rgba8[0].resize(sample_count * 4u, 0u);
			out_weight_rgba8[1].resize(sample_count * 4u, 0u);
			for (size_t sample = 0u; sample < sample_count; ++sample)
			{
				out_weight_rgba8[0][sample * 4u] = 255u;
			}
		}
		return true;
	}

	bool terrain_upload_budget_allows_next(
		uint64_t completed_bytes,
		uint64_t next_upload_bytes,
		uint64_t byte_budget,
		std::chrono::steady_clock::duration elapsed,
		std::chrono::steady_clock::duration wall_clock_budget)
	{
		return next_upload_bytes != 0u &&
			completed_bytes <= byte_budget &&
			next_upload_bytes <= byte_budget - completed_bytes &&
			wall_clock_budget > std::chrono::steady_clock::duration::zero() &&
			elapsed >= std::chrono::steady_clock::duration::zero() &&
			elapsed < wall_clock_budget;
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
				m_pending_weight_updates.clear();
				m_pending_implicit_weight_resets.clear();
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
		if (!std::isfinite(snapshot->height_mapping.height_offset) ||
			!std::isfinite(snapshot->height_mapping.height_range) ||
			snapshot->height_mapping.height_range <= 0.0f)
		{
			return reject_snapshot("terrain height mapping is invalid.");
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
		const bool rebuild_unpublished_generation =
			m_state.readiness() != TerrainRenderReadiness::Ready;
		std::vector<TerrainGpuComponentUpload> uploads{};
		std::vector<TerrainComponentCoord> implicit_weight_resets{};
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
			if (!rebuild_unpublished_generation && previous_component == component &&
				(m_accepted_snapshot || component))
			{
				continue;
			}
			if (!component)
			{
				const bool removal_was_pending = std::find(
					m_pending_component_removals.begin(),
					m_pending_component_removals.end(),
					expected_coord) != m_pending_component_removals.end();
				if (rebuild_after_failure || previous_component || removal_was_pending)
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
			std::string shape_error{};
			if (!validate_component_shape(*component, &shape_error))
			{
				return reject_snapshot(std::move(shape_error));
			}
			TerrainGpuComponentUpload upload{};
			upload.coord = component->coord;
			upload.content_generation = snapshot->content_generation;
			upload.component = component;
			if (component->weights.empty())
			{
				const bool had_explicit_weights =
					previous_component && !previous_component->weights.empty();
				const bool has_resident_slot = std::any_of(
					m_frame_boundary_atlas_slots.begin(),
					m_frame_boundary_atlas_slots.end(),
					[expected_coord](const TerrainAtlasSlotMetadata& slot)
					{
						return slot.occupied && slot.coord == expected_coord;
					});
				if (rebuild_after_failure || had_explicit_weights || has_resident_slot)
				{
					implicit_weight_resets.push_back(expected_coord);
				}
			}
			uploads.push_back(std::move(upload));
		}

		m_state.begin_content_generation(
			snapshot->content_generation,
			static_cast<uint32_t>(uploads.size() + removals.size()));
		m_accepted_snapshot = snapshot;
		m_pending_component_uploads = std::move(uploads);
		m_pending_weight_updates.clear();
		m_pending_weight_updates.reserve(m_pending_component_uploads.size());
		for (const TerrainGpuComponentUpload& upload : m_pending_component_uploads)
		{
			if (upload.component && !upload.component->weights.empty())
			{
				m_pending_weight_updates.push_back(upload);
			}
		}
		m_pending_implicit_weight_resets = std::move(implicit_weight_resets);
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

	uint64_t TerrainRenderAsset::pending_cpu_payload_bytes() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return 0u;
	}

	uint64_t TerrainRenderAsset::pending_weight_payload_bytes() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return 0u;
	}

	uint32_t TerrainRenderAsset::pending_weight_update_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_pending_weight_updates.size());
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

	TerrainShadowCasterIdentity
		TerrainRenderAsset::snapshot_shadow_caster_identity() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		TerrainShadowCasterIdentity identity{};
		identity.accepted_snapshot_identity = static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(m_accepted_snapshot.get()));
		identity.has_accepted_snapshot = m_accepted_snapshot != nullptr;
		if (m_accepted_snapshot)
		{
			identity.accepted_asset_id = m_accepted_snapshot->asset_id;
			identity.accepted_content_generation =
				m_accepted_snapshot->content_generation;
			identity.accepted_residency_revision =
				m_accepted_snapshot->residency_revision;
		}
		identity.active_content_generation =
			m_state.active_content_generation();
		identity.published_content_generation =
			m_state.published_content_generation();
		identity.required_upload_count = m_state.required_upload_count();
		identity.completed_upload_count = m_state.completed_upload_count();
		identity.pending_component_upload_count = static_cast<uint32_t>(
			m_pending_component_uploads.size());
		identity.pending_component_removal_count = static_cast<uint32_t>(
			m_pending_component_removals.size());
		identity.readiness = m_state.readiness();
		return identity;
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
		return m_fallback_material_arrays &&
			index < m_fallback_material_arrays->arrays.size() ?
			m_fallback_material_arrays->arrays[index] : nullptr;
	}

	bool TerrainRenderAsset::set_fallback_material_arrays(
		const std::shared_ptr<const TerrainFallbackMaterialArrays>& arrays)
	{
		if (!arrays || !arrays->is_valid())
		{
			return false;
		}

		std::scoped_lock<std::mutex> lock(m_mutex);
		m_fallback_material_arrays = arrays;
		return true;
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
			m_dirty_weight_staging_buffer = renderer.create_storage_buffer({
				k_terrain_weight_upload_bytes,
				k_terrain_weight_upload_stride,
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
		if (!m_packed_height_buffer ||
			!m_dirty_weight_staging_buffer ||
			!m_weight_atlases[0] ||
			!m_weight_atlases[1] ||
			!m_coarse_weight_target ||
			!m_fallback_material_arrays ||
			!m_fallback_material_arrays->is_valid())
		{
			fail_active_generation("failed to create Terrain GPU resources.");
			return fail_with_error(out_error, m_last_error.c_str());
		}

		constexpr uint32_t component_height_bytes =
			k_terrain_render_height_words_per_component * sizeof(uint32_t);
		const uint64_t content_generation = m_state.active_content_generation();
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
		m_pending_component_removals.clear();

		const auto upload_start = std::chrono::steady_clock::now();
		uint64_t completed_bytes = 0u;
		size_t completed_uploads = 0u;
		std::vector<uint32_t> packed_height_words{};
		while (completed_uploads < m_pending_component_uploads.size() &&
			terrain_upload_budget_allows_next(
				completed_bytes,
				component_height_bytes,
				k_height_upload_byte_budget,
				std::chrono::steady_clock::now() - upload_start,
				k_height_upload_wall_clock_budget))
		{
			const TerrainGpuComponentUpload& upload =
				m_pending_component_uploads[completed_uploads];
			if (upload.content_generation != content_generation || !upload.component)
			{
				fail_active_generation("Terrain height upload generation is stale.");
				return fail_with_error(out_error, m_last_error.c_str());
			}

			std::string pack_error{};
			packed_height_words.clear();
			if (!build_terrain_component_height_words(
					*upload.component,
					m_accepted_snapshot->height_mapping,
					packed_height_words,
					&pack_error) ||
				packed_height_words.size() !=
					k_terrain_render_height_words_per_component)
			{
				fail_active_generation(pack_error.empty() ?
					"failed to pack Terrain component height data." : pack_error);
				return fail_with_error(out_error, m_last_error.c_str());
			}

			const uint32_t offset =
				component_linear_index(upload.coord) * component_height_bytes;
			if (!m_packed_height_buffer->update(
					offset,
					component_height_bytes,
					packed_height_words.data()) ||
				!m_state.mark_component_uploaded(content_generation, upload.coord))
			{
				fail_active_generation("failed to upload Terrain component height data.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
			completed_bytes += component_height_bytes;
			++completed_uploads;
		}
		if (completed_uploads != 0u)
		{
			m_pending_component_uploads.erase(
				m_pending_component_uploads.begin(),
				m_pending_component_uploads.begin() + completed_uploads);
		}
		if (!m_pending_component_uploads.empty())
		{
			return false;
		}
		for (TerrainComponentCoord coord : m_pending_implicit_weight_resets)
		{
			for (TerrainAtlasSlotMetadata& slot : m_frame_boundary_atlas_slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					slot = {};
				}
			}
		}
		m_pending_implicit_weight_resets.clear();

		if (!m_state.publish_content_generation(content_generation))
		{
			fail_active_generation("failed to publish Terrain content generation.");
			return fail_with_error(out_error, m_last_error.c_str());
		}
		m_last_error.clear();
		return true;
	}
}
