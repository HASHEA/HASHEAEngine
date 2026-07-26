#include "Function/Render/TerrainRenderAsset.h"

#include "Function/Render/Renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static constexpr uint64_t k_height_upload_byte_budget = 4ull * 1024ull * 1024ull;
		static constexpr auto k_height_upload_wall_clock_budget =
			std::chrono::milliseconds(2);
		static constexpr uint32_t k_terrain_coarse_cells_per_component = 32u;

		auto fail_with_error(std::string* out_error, const char* message) -> bool
		{
			if (out_error)
			{
				*out_error = message;
			}
			return false;
		}

		auto format_spacing(float spacing) -> std::string
		{
			if (std::isnan(spacing))
			{
				return "nan";
			}
			if (std::isinf(spacing))
			{
				return std::signbit(spacing) ? "-inf" : "inf";
			}
			std::ostringstream stream{};
			stream << std::setprecision(std::numeric_limits<float>::max_digits10) <<
				spacing;
			return stream.str();
		}

		auto describe_layout_error(
			const TerrainGridLayout& layout,
			const std::string& reason) -> std::string
		{
			return "terrain render layout rejected: samples=" +
				std::to_string(layout.sample_count_x) + "x" +
				std::to_string(layout.sample_count_z) + ", components=" +
				std::to_string(layout.component_count_x) + "x" +
				std::to_string(layout.component_count_z) + ", quads=" +
				std::to_string(layout.component_quad_count) + ", spacing=" +
				format_spacing(layout.sample_spacing_meters) + "; reason=" + reason;
		}

		auto terrain_candidate_stage_name(
			TerrainRenderCandidateStage stage) -> const char*
		{
			switch (stage)
			{
			case TerrainRenderCandidateStage::CreateResources:
				return "CreateResources";
			case TerrainRenderCandidateStage::UploadHeights:
				return "UploadHeights";
			case TerrainRenderCandidateStage::AwaitGraphWork:
				return "AwaitGraphWork";
			case TerrainRenderCandidateStage::ReadyToPublish:
				return "ReadyToPublish";
			case TerrainRenderCandidateStage::Failed:
			default:
				return "Failed";
			}
		}

		auto describe_terrain_failure(
			const std::string& asset_path,
			bool published_view_retained,
			const char* failure_scope,
			const char* stage,
			const TerrainGridLayout& layout,
			const std::string& reason) -> std::string
		{
			if (asset_path.empty())
			{
				return reason;
			}
			return std::string(failure_scope) + "; " +
				(published_view_retained ? "published view retained" :
					"no published view available") +
				"; asset_path=" + asset_path +
				"; stage=" + stage +
				"; samples=" + std::to_string(layout.sample_count_x) + "x" +
					std::to_string(layout.sample_count_z) +
				"; components=" + std::to_string(layout.component_count_x) + "x" +
					std::to_string(layout.component_count_z) +
				"; quads=" + std::to_string(layout.component_quad_count) +
				"; spacing=" + format_spacing(layout.sample_spacing_meters) +
				"; reason=" + reason;
		}

		auto checked_multiply_u64(
			uint64_t lhs,
			uint64_t rhs,
			uint64_t& out_product) -> bool
		{
			if (lhs != 0u && rhs > std::numeric_limits<uint64_t>::max() / lhs)
			{
				return false;
			}
			out_product = lhs * rhs;
			return true;
		}

		auto checked_add_u64(
			uint64_t lhs,
			uint64_t rhs,
			uint64_t& out_sum) -> bool
		{
			if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
			{
				return false;
			}
			out_sum = lhs + rhs;
			return true;
		}

		auto render_layouts_equal(
			const TerrainGridLayout& lhs,
			const TerrainGridLayout& rhs) -> bool
		{
			return lhs.sample_count_x == rhs.sample_count_x &&
				lhs.sample_count_z == rhs.sample_count_z &&
				lhs.component_count_x == rhs.component_count_x &&
				lhs.component_count_z == rhs.component_count_z &&
				lhs.component_quad_count == rhs.component_quad_count &&
				lhs.sample_spacing_meters == rhs.sample_spacing_meters;
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

	auto TerrainRenderLayoutInfo::component_linear_index(
		TerrainComponentCoord coord) const -> size_t
	{
		return static_cast<size_t>(coord.z) * component_row_stride + coord.x;
	}

	auto TerrainRenderLayoutInfo::contains(TerrainComponentCoord coord) const -> bool
	{
		return coord.x < layout.component_count_x &&
			coord.z < layout.component_count_z;
	}

	auto derive_terrain_render_layout(
		const TerrainGridLayout& layout,
		TerrainRenderLayoutInfo& out_info,
		std::string* out_error) -> bool
	{
		if (out_error)
		{
			out_error->clear();
		}
		const auto reject =
			[&](const std::string& reason)
			{
				const std::string error = describe_layout_error(layout, reason);
				return fail_with_error(out_error, error.c_str());
			};

		if (layout.component_count_x < 1u ||
			layout.component_count_x > k_terrain_component_count)
		{
			return reject("component_count_x must be in [1, 32].");
		}
		if (layout.component_count_z < 1u ||
			layout.component_count_z > k_terrain_component_count)
		{
			return reject("component_count_z must be in [1, 32].");
		}
		if (layout.component_quad_count != k_terrain_component_quad_count)
		{
			return reject("component_quad_count must equal 256.");
		}
		if (!std::isfinite(layout.sample_spacing_meters))
		{
			return reject("sample_spacing_meters must be finite.");
		}
		if (layout.sample_spacing_meters <= 0.0f)
		{
			return reject("sample_spacing_meters must be greater than zero.");
		}
		if (layout.sample_spacing_meters != 1.0f)
		{
			return reject("sample_spacing_meters must equal 1.");
		}

		uint64_t expected_samples_x = 0u;
		uint64_t expected_samples_z = 0u;
		if (!checked_multiply_u64(
				layout.component_count_x,
				layout.component_quad_count,
				expected_samples_x) ||
			!checked_add_u64(expected_samples_x, 1u, expected_samples_x) ||
			!checked_multiply_u64(
				layout.component_count_z,
				layout.component_quad_count,
				expected_samples_z) ||
			!checked_add_u64(expected_samples_z, 1u, expected_samples_z) ||
			expected_samples_x > std::numeric_limits<uint32_t>::max() ||
			expected_samples_z > std::numeric_limits<uint32_t>::max())
		{
			return reject("sample count derivation overflowed uint32_t.");
		}
		if (layout.sample_count_x != expected_samples_x ||
			layout.sample_count_z != expected_samples_z)
		{
			return reject(
				"sample counts must equal component counts * 256 + 1 "
				"(expected samples=" + std::to_string(expected_samples_x) + "x" +
				std::to_string(expected_samples_z) + ").");
		}

		uint64_t component_count = 0u;
		uint64_t height_word_count = 0u;
		uint64_t height_buffer_bytes = 0u;
		if (!checked_multiply_u64(
				layout.component_count_x,
				layout.component_count_z,
				component_count) ||
			component_count > std::numeric_limits<uint32_t>::max())
		{
			return reject("component count overflowed uint32_t.");
		}
		if (!checked_multiply_u64(
				component_count,
				k_terrain_render_height_words_per_component,
				height_word_count) ||
			!checked_multiply_u64(
				height_word_count,
				sizeof(uint32_t),
				height_buffer_bytes) ||
			height_buffer_bytes > std::numeric_limits<uint32_t>::max())
		{
			return reject("height buffer byte count exceeds uint32_t resource size.");
		}

		uint64_t coarse_width = 0u;
		uint64_t coarse_height = 0u;
		if (!checked_multiply_u64(
				layout.component_count_x,
				k_terrain_coarse_cells_per_component,
				coarse_width) ||
			!checked_add_u64(coarse_width, 1u, coarse_width) ||
			!checked_multiply_u64(
				layout.component_count_z,
				k_terrain_coarse_cells_per_component,
				coarse_height) ||
			!checked_add_u64(coarse_height, 1u, coarse_height) ||
			coarse_width > std::numeric_limits<uint16_t>::max() ||
			coarse_height > std::numeric_limits<uint16_t>::max())
		{
			return reject("coarse target dimensions exceed uint16_t resource extent.");
		}

		TerrainRenderLayoutInfo candidate{};
		candidate.layout = layout;
		candidate.component_count = static_cast<uint32_t>(component_count);
		candidate.component_row_stride = layout.component_count_x;
		candidate.height_buffer_bytes = height_buffer_bytes;
		candidate.coarse_width = static_cast<uint32_t>(coarse_width);
		candidate.coarse_height = static_cast<uint32_t>(coarse_height);
		out_info = candidate;
		return true;
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
		TerrainRenderLayoutInfo layout_info{};
		layout_info.layout.component_count_x = k_terrain_component_count;
		layout_info.layout.component_count_z = k_terrain_component_count;
		layout_info.component_count = k_terrain_render_component_capacity;
		layout_info.component_row_stride = k_terrain_component_count;
		begin_content_generation_for_layout(
			content_generation, required_uploads, layout_info, false);
	}

	void TerrainRenderAssetState::begin_content_generation_for_layout(
		uint64_t content_generation,
		uint32_t required_uploads,
		const TerrainRenderLayoutInfo& layout_info,
		bool reset_generation_identity)
	{
		if (!reset_generation_identity && m_has_active_content_generation &&
			content_generation <= m_active_content_generation)
		{
			return;
		}
		begin_snapshot_for_layout(
			content_generation, 0u, required_uploads, layout_info);
	}

	void TerrainRenderAssetState::begin_snapshot_for_layout(
		uint64_t content_generation,
		uint64_t residency_revision,
		uint32_t required_uploads,
		const TerrainRenderLayoutInfo& layout_info)
	{
		m_has_active_content_generation = true;
		m_active_content_generation = content_generation;
		m_active_residency_revision = residency_revision;
		m_required_upload_count = required_uploads;
		m_completed_upload_count = 0u;
		m_completed_component_mask.fill(0u);
		m_component_count = layout_info.component_count;
		m_component_row_stride = layout_info.component_row_stride;
		m_component_count_z = layout_info.layout.component_count_z;
		m_readiness = required_uploads <= m_component_count ?
			TerrainRenderReadiness::Pending : TerrainRenderReadiness::Failed;
	}

	bool TerrainRenderAssetState::mark_component_uploaded(
		uint64_t content_generation,
		TerrainComponentCoord coord)
	{
		return mark_component_uploaded_for_snapshot(
			content_generation, 0u, coord);
	}

	bool TerrainRenderAssetState::mark_component_uploaded_for_snapshot(
		uint64_t content_generation,
		uint64_t residency_revision,
		TerrainComponentCoord coord)
	{
		if (!m_has_active_content_generation ||
			content_generation != m_active_content_generation ||
			residency_revision != m_active_residency_revision ||
			m_readiness != TerrainRenderReadiness::Pending ||
			coord.x >= m_component_row_stride ||
			coord.z >= m_component_count_z ||
			m_completed_upload_count >= m_required_upload_count)
		{
			return false;
		}

		const uint32_t bit_index =
			static_cast<uint32_t>(coord.z) * m_component_row_stride + coord.x;
		if (bit_index >= m_component_count)
		{
			return false;
		}
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
		return publish_snapshot(content_generation, 0u);
	}

	bool TerrainRenderAssetState::publish_snapshot(
		uint64_t content_generation,
		uint64_t residency_revision)
	{
		if (!m_has_active_content_generation ||
			content_generation != m_active_content_generation ||
			residency_revision != m_active_residency_revision ||
			m_readiness != TerrainRenderReadiness::Pending ||
			m_completed_upload_count != m_required_upload_count)
		{
			return false;
		}

		m_published_content_generation = content_generation;
		m_published_residency_revision = residency_revision;
		m_readiness = TerrainRenderReadiness::Ready;
		return true;
	}

	void TerrainRenderAssetState::mark_failed(uint64_t content_generation)
	{
		mark_snapshot_failed(content_generation, 0u);
	}

	void TerrainRenderAssetState::mark_snapshot_failed(
		uint64_t content_generation,
		uint64_t residency_revision)
	{
		if (m_has_active_content_generation &&
			content_generation == m_active_content_generation &&
			residency_revision == m_active_residency_revision)
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

	bool TerrainRenderResourceSet::is_complete() const
	{
		return height && staging && atlas[0] && atlas[1] && coarse;
	}

	namespace
	{
		static constexpr uint64_t k_max_atomic_terrain_resource_bytes =
			1024ull * 1024ull * 1024ull;

		auto terrain_resource_bytes(
			const TerrainRenderLayoutInfo& layout,
			uint64_t& out_bytes) -> bool
		{
			uint64_t atlas_pixels = 0u;
			uint64_t atlas_bytes = 0u;
			uint64_t coarse_pixels = 0u;
			uint64_t coarse_bytes = 0u;
			uint64_t total = 0u;
			return checked_multiply_u64(
				k_terrain_weight_atlas_extent,
				k_terrain_weight_atlas_extent,
				atlas_pixels) &&
				checked_multiply_u64(atlas_pixels, 8u, atlas_bytes) &&
				checked_multiply_u64(
					layout.coarse_width, layout.coarse_height, coarse_pixels) &&
				checked_multiply_u64(coarse_pixels, 4u, coarse_bytes) &&
				checked_add_u64(layout.height_buffer_bytes,
					k_terrain_weight_upload_bytes, total) &&
				checked_add_u64(total, atlas_bytes, total) &&
				checked_add_u64(total, coarse_bytes, out_bytes);
		}

		class RendererTerrainGpuOps final : public TerrainRenderGpuOps
		{
		public:
			explicit RendererTerrainGpuOps(Renderer& renderer) : m_renderer(renderer) {}

			std::shared_ptr<StorageBuffer> create_storage_buffer(
				const StorageBufferDesc& desc) override
			{
				return m_renderer.create_storage_buffer(desc);
			}

			std::shared_ptr<RenderTarget> create_render_target(
				const RenderTargetDesc& desc) override
			{
				return m_renderer.create_render_target(desc);
			}

			bool update_storage_buffer(
				const std::shared_ptr<StorageBuffer>& buffer,
				uint32_t offset,
				uint32_t size,
				const void* data) override
			{
				return buffer && buffer->update(offset, size, data);
			}

		private:
			Renderer& m_renderer;
		};
	}

	TerrainRenderAsset::TerrainRenderAsset() = default;
	TerrainRenderAsset::~TerrainRenderAsset() = default;

	uint64_t TerrainRenderAsset::allocate_candidate_epoch_locked()
	{
		const uint64_t epoch = m_next_candidate_epoch++;
		if (m_next_candidate_epoch == 0u)
		{
			m_next_candidate_epoch = 1u;
		}
		return epoch;
	}

	std::shared_ptr<const TerrainAssetSnapshot>
		TerrainRenderAsset::latest_admitted_snapshot_locked() const
	{
		if (m_candidate_state && m_candidate_state->snapshot)
		{
			return m_candidate_state->snapshot;
		}
		if (m_published_view && m_published_view->runtime &&
			m_published_view->runtime->target_snapshot)
		{
			return m_published_view->runtime->target_snapshot;
		}
		return m_published_view ? m_published_view->snapshot : nullptr;
	}

	std::shared_ptr<TerrainRenderRuntimeState>
		TerrainRenderAsset::current_incremental_runtime_locked() const
	{
		return m_published_view ? m_published_view->runtime : nullptr;
	}

	bool TerrainRenderAsset::matches_pending_weight_update_locked(
		const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
		const TerrainGpuComponentUpload& expected) const
	{
		if (!runtime || !runtime->target_snapshot || runtime->weight_queue.empty())
		{
			return false;
		}
		const std::shared_ptr<const TerrainAssetSnapshot> expected_snapshot =
			expected.accepted_snapshot.lock();
		const TerrainGpuComponentUpload& pending = runtime->weight_queue.front();
		return expected_snapshot && runtime->target_snapshot == expected_snapshot &&
			pending.accepted_snapshot.lock() == expected_snapshot &&
			pending.asset_id == expected.asset_id &&
			pending.coord == expected.coord &&
			pending.content_generation == expected.content_generation &&
			pending.residency_revision == expected.residency_revision &&
			pending.component == expected.component;
	}

	void TerrainRenderAsset::fail_candidate_locked(
		const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
		uint64_t candidate_epoch,
		const std::string& error)
	{
		if (!m_candidate_state || m_candidate_state->candidate_epoch != candidate_epoch ||
			m_candidate_state->runtime != runtime)
		{
			return;
		}
		const TerrainRenderCandidateStage failed_stage =
			m_candidate_state->stage;
		const TerrainGridLayout layout = m_candidate_state->snapshot ?
			m_candidate_state->snapshot->layout : TerrainGridLayout{};
		const std::string detail = describe_terrain_failure(
			m_asset_path,
			m_published_view != nullptr,
			"Terrain candidate failed",
			terrain_candidate_stage_name(failed_stage),
			layout,
			error);
		m_candidate_state->stage = TerrainRenderCandidateStage::Failed;
		m_candidate_state->work_status = TerrainRenderWorkStatus::Failed;
		m_candidate_state->error = detail;
		m_candidate_state->prepared_view.reset();
		m_candidate_state->runtime.reset();
		m_last_error = detail;
	}

	void TerrainRenderAsset::fail_latest_work_locked(const std::string& error)
	{
		if (m_candidate_state)
		{
			fail_candidate_locked(
				m_candidate_state->runtime,
				m_candidate_state->candidate_epoch,
				error);
			return;
		}
		const auto runtime = current_incremental_runtime_locked();
		if (runtime && runtime->target_snapshot)
		{
			runtime->state.mark_snapshot_failed(
				runtime->target_snapshot->content_generation,
				runtime->target_snapshot->residency_revision);
			runtime->work_status = TerrainRenderWorkStatus::Failed;
		}
		const TerrainGridLayout layout =
			runtime && runtime->target_snapshot ?
				runtime->target_snapshot->layout :
				(m_published_view && m_published_view->snapshot ?
					m_published_view->snapshot->layout : TerrainGridLayout{});
		m_last_error = describe_terrain_failure(
			m_asset_path,
			m_published_view != nullptr,
			"Terrain incremental update failed",
			"IncrementalUpdate",
			layout,
			error);
	}

	bool TerrainRenderAsset::accept_snapshot(
		const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
		std::string* out_error)
	{
		return accept_snapshot_with_peak_budget(
			snapshot, k_max_atomic_terrain_resource_bytes, out_error);
	}

	bool TerrainRenderAsset::accept_snapshot_with_peak_budget(
		const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
		uint64_t peak_budget,
		std::string* out_error)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (out_error)
		{
			out_error->clear();
		}
		if (!snapshot)
		{
			const std::string detail = describe_terrain_failure(
				m_asset_path,
				m_published_view != nullptr,
				"Terrain snapshot rejected",
				"ValidateSnapshot",
				TerrainGridLayout{},
				"terrain snapshot must not be null.");
			return fail_with_error(out_error, detail.c_str());
		}

		const auto latest = latest_admitted_snapshot_locked();
		if (latest == snapshot)
		{
			const TerrainRenderWorkStatus status = m_candidate_state ?
				m_candidate_state->work_status :
				(m_published_view && m_published_view->runtime ?
					m_published_view->runtime->work_status :
					TerrainRenderWorkStatus::Pending);
			if (status == TerrainRenderWorkStatus::Failed)
			{
				return fail_with_error(out_error,
					m_last_error.empty() ? "terrain snapshot is failed." :
					m_last_error.c_str());
			}
			return true;
		}
		const bool same_latest_asset = latest && latest->asset_id == snapshot->asset_id;
		const bool newer_same_asset = same_latest_asset &&
			(snapshot->content_generation > latest->content_generation ||
				(snapshot->content_generation == latest->content_generation &&
					snapshot->residency_revision > latest->residency_revision));
		if (same_latest_asset && !newer_same_asset)
		{
			const std::string detail = describe_terrain_failure(
				m_asset_path,
				m_published_view != nullptr,
				"Terrain snapshot rejected",
				"ValidateSnapshot",
				snapshot->layout,
				"terrain snapshot content generation is stale.");
			return fail_with_error(out_error, detail.c_str());
		}

		TerrainRenderLayoutInfo render_layout{};
		std::string error{};
		const bool layout_valid = derive_terrain_render_layout(
			snapshot->layout, render_layout, &error);
		const bool same_published_asset = m_published_view &&
			m_published_view->asset_id == snapshot->asset_id;
		const bool same_published_layout = layout_valid && m_published_view &&
			render_layouts_equal(
				render_layout.layout, m_published_view->layout.layout);
		const bool incremental = same_published_asset && same_published_layout;
		const bool same_latest_layout = latest && layout_valid &&
			render_layouts_equal(render_layout.layout, latest->layout);
		const bool has_viable_first_load_candidate = !m_published_view &&
			m_candidate_state && m_candidate_state->work_status !=
				TerrainRenderWorkStatus::Failed;
		const bool requires_dense_replacement = !incremental &&
			(m_published_view || (has_viable_first_load_candidate &&
				!(same_latest_asset && same_latest_layout)));

		const auto reject = [&](std::string diagnostic) -> bool
		{
			m_last_error = describe_terrain_failure(
				m_asset_path,
				m_published_view != nullptr,
				"Terrain snapshot rejected",
				"ValidateSnapshot",
				snapshot->layout,
				diagnostic);
			if (incremental)
			{
				const auto runtime = current_incremental_runtime_locked();
				if (runtime)
				{
					runtime->target_snapshot = snapshot;
					runtime->state.begin_snapshot_for_layout(
						snapshot->content_generation,
						snapshot->residency_revision,
						0u,
						render_layout);
					runtime->state.mark_snapshot_failed(
						snapshot->content_generation,
						snapshot->residency_revision);
					runtime->work_status = TerrainRenderWorkStatus::Failed;
				}
				m_candidate_state.reset();
			}
			else
			{
				auto failed = std::make_unique<TerrainRenderCandidateState>();
				failed->snapshot = snapshot;
				failed->layout = render_layout;
				failed->candidate_epoch = allocate_candidate_epoch_locked();
				failed->stage = TerrainRenderCandidateStage::Failed;
				failed->work_status = TerrainRenderWorkStatus::Failed;
				failed->error = m_last_error;
				m_candidate_state = std::move(failed);
			}
			return fail_with_error(out_error, m_last_error.c_str());
		};

		if (snapshot->failed)
		{
			return reject(snapshot->failure_detail.empty() ?
				"terrain snapshot is failed." : snapshot->failure_detail);
		}
		if (!layout_valid)
		{
			return reject(error);
		}
		if (!std::isfinite(snapshot->height_mapping.height_offset) ||
			!std::isfinite(snapshot->height_mapping.height_range) ||
			snapshot->height_mapping.height_range <= 0.0f)
		{
			return reject(describe_layout_error(
				snapshot->layout, "terrain height mapping is invalid."));
		}
		if (snapshot->components.size() != render_layout.component_count)
		{
			return reject(describe_layout_error(
				snapshot->layout,
				"component table contains " +
					std::to_string(snapshot->components.size()) +
					" entries; expected " +
					std::to_string(render_layout.component_count) + "."));
		}
		for (size_t index = 0u; index < snapshot->components.size(); ++index)
		{
			const auto& component = snapshot->components[index];
			const TerrainComponentCoord expected_coord{
				static_cast<uint16_t>(index % render_layout.component_row_stride),
				static_cast<uint16_t>(index / render_layout.component_row_stride) };
			if (!component)
			{
				if (requires_dense_replacement)
				{
					return reject(describe_layout_error(
						snapshot->layout,
						"replacement snapshot has a null component at row-major slot " +
						std::to_string(index) + "."));
				}
				continue;
			}
			if (!(component->coord == expected_coord))
			{
				return reject(describe_layout_error(
					snapshot->layout,
					"component at row-major slot " + std::to_string(index) +
						" has coord=(" + std::to_string(component->coord.x) + "," +
						std::to_string(component->coord.z) + "); expected=(" +
						std::to_string(expected_coord.x) + "," +
						std::to_string(expected_coord.z) + ")."));
			}
			std::string shape_error{};
			if (!validate_component_shape(*component, &shape_error))
			{
				return reject(describe_layout_error(snapshot->layout, shape_error));
			}
		}

		const auto make_upload = [&](
			const std::shared_ptr<const TerrainComponentSnapshot>& component)
		{
			TerrainGpuComponentUpload upload{};
			upload.asset_id = snapshot->asset_id;
			upload.accepted_snapshot = snapshot;
			upload.coord = component->coord;
			upload.content_generation = snapshot->content_generation;
			upload.residency_revision = snapshot->residency_revision;
			upload.component = component;
			return upload;
		};

		if (!incremental)
		{
			uint64_t candidate_bytes = 0u;
			uint64_t peak_bytes = 0u;
			if (!terrain_resource_bytes(render_layout, candidate_bytes))
			{
				return reject(describe_layout_error(
					snapshot->layout,
					"candidate Terrain resource size arithmetic overflowed."));
			}
			peak_bytes = candidate_bytes;
			if ((m_published_view &&
				(!terrain_resource_bytes(m_published_view->layout, peak_bytes) ||
					!checked_add_u64(peak_bytes, candidate_bytes, peak_bytes))) ||
				peak_bytes > peak_budget)
			{
				return reject(describe_layout_error(
					snapshot->layout,
					"active plus candidate Terrain resource peak exceeds the approved budget."));
			}

			auto candidate = std::make_unique<TerrainRenderCandidateState>();
			candidate->snapshot = snapshot;
			candidate->layout = render_layout;
			candidate->runtime = std::make_shared<TerrainRenderRuntimeState>();
			candidate->runtime->target_snapshot = snapshot;
			if (m_published_view && m_published_view->runtime)
			{
				candidate->runtime->latest_required_residency =
					m_published_view->runtime->latest_required_residency;
			}
			candidate->candidate_epoch = allocate_candidate_epoch_locked();
			candidate->peak_resource_bytes = peak_bytes;
			for (const auto& component : snapshot->components)
			{
				if (!component)
				{
					continue;
				}
				const TerrainGpuComponentUpload upload = make_upload(component);
				candidate->runtime->height_queue.push_back(upload);
				candidate->coarse_work.push_back(upload);
			}
			candidate->runtime->state.begin_snapshot_for_layout(
				snapshot->content_generation,
				snapshot->residency_revision,
				static_cast<uint32_t>(candidate->runtime->height_queue.size()),
				render_layout);
			try
			{
				candidate->prepared_view =
					std::make_shared<TerrainPublishedRenderView>();
			}
			catch (const std::bad_alloc&)
			{
				const auto runtime = candidate->runtime;
				const uint64_t candidate_epoch = candidate->candidate_epoch;
				m_candidate_state = std::move(candidate);
				fail_candidate_locked(
					runtime,
					candidate_epoch,
					"failed to allocate Terrain candidate publication view.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
			candidate->prepared_view->snapshot = snapshot;
			candidate->prepared_view->layout = render_layout;
			candidate->prepared_view->runtime = candidate->runtime;
			candidate->prepared_view->asset_id = snapshot->asset_id;
			candidate->prepared_view->content_generation =
				snapshot->content_generation;
			candidate->prepared_view->residency_revision =
				snapshot->residency_revision;
			m_candidate_state = std::move(candidate);
			m_last_error.clear();
			return true;
		}

		m_candidate_state.reset();
		const auto runtime = current_incremental_runtime_locked();
		if (!runtime)
		{
			return reject("published Terrain runtime is missing.");
		}
		const auto previous = runtime->target_snapshot ?
			runtime->target_snapshot : m_published_view->snapshot;
		const bool rebuild = runtime->work_status == TerrainRenderWorkStatus::Failed;
		std::vector<TerrainGpuComponentUpload> heights{};
		std::vector<TerrainGpuComponentUpload> weights{};
		std::vector<TerrainComponentCoord> resets{};
		std::vector<TerrainComponentCoord> removals{};
		std::array<bool, k_terrain_render_component_capacity> height_seen{};
		std::array<bool, k_terrain_render_component_capacity> weight_seen{};
		std::array<bool, k_terrain_render_component_capacity> reset_seen{};
		std::array<bool, k_terrain_render_component_capacity> removal_seen{};
		const auto append_coord = [&](std::vector<TerrainComponentCoord>& queue,
			std::array<bool, k_terrain_render_component_capacity>& seen,
			TerrainComponentCoord coord)
		{
			const size_t index = render_layout.component_linear_index(coord);
			if (!seen[index])
			{
				queue.push_back(coord);
				seen[index] = true;
			}
		};
		const auto append_upload = [&](std::vector<TerrainGpuComponentUpload>& queue,
			std::array<bool, k_terrain_render_component_capacity>& seen,
			const std::shared_ptr<const TerrainComponentSnapshot>& component)
		{
			const size_t index = render_layout.component_linear_index(component->coord);
			if (!seen[index])
			{
				queue.push_back(make_upload(component));
				seen[index] = true;
			}
		};

		if (!rebuild)
		{
			for (const auto& pending : runtime->height_queue)
			{
				const size_t index = render_layout.component_linear_index(pending.coord);
				if (render_layout.contains(pending.coord) && pending.component &&
					index < snapshot->components.size() &&
					snapshot->components[index] == pending.component)
				{
					auto carried = pending;
					carried.accepted_snapshot = snapshot;
					carried.content_generation = snapshot->content_generation;
					carried.residency_revision = snapshot->residency_revision;
					heights.push_back(std::move(carried));
					height_seen[index] = true;
				}
			}
			for (const auto& pending : runtime->weight_queue)
			{
				const size_t index = render_layout.component_linear_index(pending.coord);
				if (render_layout.contains(pending.coord) && pending.component &&
					!pending.component->weights.empty() &&
					index < snapshot->components.size() &&
					snapshot->components[index] == pending.component)
				{
					auto carried = pending;
					carried.accepted_snapshot = snapshot;
					carried.content_generation = snapshot->content_generation;
					carried.residency_revision = snapshot->residency_revision;
					weights.push_back(std::move(carried));
					weight_seen[index] = true;
				}
			}
			for (const auto coord : runtime->reset_queue)
			{
				const size_t index = render_layout.component_linear_index(coord);
				if (render_layout.contains(coord) && index < snapshot->components.size() &&
					snapshot->components[index] &&
					snapshot->components[index]->weights.empty())
				{
					append_coord(resets, reset_seen, coord);
				}
			}
			for (const auto coord : runtime->removal_queue)
			{
				const size_t index = render_layout.component_linear_index(coord);
				if (render_layout.contains(coord) && index < snapshot->components.size() &&
					!snapshot->components[index])
				{
					append_coord(removals, removal_seen, coord);
				}
			}
		}

		for (size_t index = 0u; index < snapshot->components.size(); ++index)
		{
			const auto& component = snapshot->components[index];
			const auto previous_component = previous && index < previous->components.size() ?
				previous->components[index] : nullptr;
			const TerrainComponentCoord coord{
				static_cast<uint16_t>(index % render_layout.component_row_stride),
				static_cast<uint16_t>(index / render_layout.component_row_stride) };
			if (!component)
			{
				if (rebuild || previous_component)
				{
					append_coord(removals, removal_seen, coord);
				}
				continue;
			}
			if (!rebuild && previous_component == component)
			{
				if (!component->weights.empty())
				{
					for (auto& slot : runtime->slots)
					{
						if (slot.occupied && slot.coord == coord)
						{
							slot.asset_id = snapshot->asset_id;
							slot.content_generation = snapshot->content_generation;
							slot.residency_revision = snapshot->residency_revision;
						}
					}
				}
				continue;
			}
			append_upload(heights, height_seen, component);
			if (component->weights.empty())
			{
				const bool had_explicit = previous_component &&
					!previous_component->weights.empty();
				const bool resident = std::any_of(runtime->slots.begin(), runtime->slots.end(),
					[coord](const TerrainAtlasSlotMetadata& slot)
					{
						return slot.occupied && slot.coord == coord;
					});
				if (rebuild || had_explicit || resident)
				{
					append_coord(resets, reset_seen, coord);
				}
			}
			else
			{
				append_upload(weights, weight_seen, component);
			}
		}

		runtime->target_snapshot = snapshot;
		runtime->height_queue = std::move(heights);
		runtime->weight_queue = std::move(weights);
		runtime->reset_queue = std::move(resets);
		runtime->removal_queue = std::move(removals);
		runtime->state.begin_snapshot_for_layout(
			snapshot->content_generation,
			snapshot->residency_revision,
			static_cast<uint32_t>(
				runtime->height_queue.size() + runtime->removal_queue.size()),
			render_layout);
		runtime->work_status = TerrainRenderWorkStatus::Pending;
		m_last_error.clear();
		return true;
	}

	TerrainRenderWorkStatus TerrainRenderAsset::latest_work_status() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (m_candidate_state)
		{
			return m_candidate_state->work_status;
		}
		return m_published_view && m_published_view->runtime ?
			m_published_view->runtime->work_status : TerrainRenderWorkStatus::Pending;
	}

	TerrainRenderReadiness TerrainRenderAsset::readiness() const
	{
		const TerrainRenderWorkStatus status = latest_work_status();
		return status == TerrainRenderWorkStatus::Ready ?
			TerrainRenderReadiness::Ready :
			status == TerrainRenderWorkStatus::Failed ?
				TerrainRenderReadiness::Failed : TerrainRenderReadiness::Pending;
	}

	uint64_t TerrainRenderAsset::accepted_content_generation() const
	{
		const auto snapshot = accepted_snapshot();
		return snapshot ? snapshot->content_generation : 0u;
	}

	uint64_t TerrainRenderAsset::accepted_residency_revision() const
	{
		const auto snapshot = accepted_snapshot();
		return snapshot ? snapshot->residency_revision : 0u;
	}

	uint64_t TerrainRenderAsset::published_content_generation() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_published_view ? m_published_view->content_generation : 0u;
	}

	uint64_t TerrainRenderAsset::published_residency_revision() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_published_view ? m_published_view->residency_revision : 0u;
	}

	uint32_t TerrainRenderAsset::pending_component_upload_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		const auto runtime = m_candidate_state ? m_candidate_state->runtime :
			current_incremental_runtime_locked();
		return runtime ? static_cast<uint32_t>(runtime->height_queue.size()) : 0u;
	}

	uint64_t TerrainRenderAsset::pending_cpu_payload_bytes() const { return 0u; }
	uint64_t TerrainRenderAsset::pending_weight_payload_bytes() const { return 0u; }

	uint32_t TerrainRenderAsset::pending_weight_update_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		const auto runtime = m_candidate_state ? m_candidate_state->runtime :
			current_incremental_runtime_locked();
		return runtime ? static_cast<uint32_t>(runtime->weight_queue.size()) : 0u;
	}

	bool TerrainRenderAsset::has_pending_component_upload(
		TerrainComponentCoord coord) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		const auto runtime = m_candidate_state ? m_candidate_state->runtime :
			current_incremental_runtime_locked();
		return runtime && std::any_of(runtime->height_queue.begin(),
			runtime->height_queue.end(), [coord](const TerrainGpuComponentUpload& upload)
			{
				return upload.coord == coord;
			});
	}

	uint32_t TerrainRenderAsset::pending_component_removal_count() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		const auto runtime = m_candidate_state ? m_candidate_state->runtime :
			current_incremental_runtime_locked();
		return runtime ? static_cast<uint32_t>(runtime->removal_queue.size()) : 0u;
	}

	bool TerrainRenderAsset::has_pending_component_removal(
		TerrainComponentCoord coord) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		const auto runtime = m_candidate_state ? m_candidate_state->runtime :
			current_incremental_runtime_locked();
		return runtime && std::find(runtime->removal_queue.begin(),
			runtime->removal_queue.end(), coord) != runtime->removal_queue.end();
	}

	std::shared_ptr<const TerrainAssetSnapshot> TerrainRenderAsset::accepted_snapshot() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return latest_admitted_snapshot_locked();
	}

	std::shared_ptr<const TerrainPublishedRenderView>
		TerrainRenderAsset::published_view() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_published_view;
	}

	TerrainShadowCasterIdentity TerrainRenderAsset::snapshot_shadow_caster_identity() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		TerrainShadowCasterIdentity identity{};
		const auto accepted = latest_admitted_snapshot_locked();
		const auto runtime = m_candidate_state ? m_candidate_state->runtime :
			current_incremental_runtime_locked();
		identity.accepted_snapshot_identity = static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(accepted.get()));
		identity.has_accepted_snapshot = accepted != nullptr;
		if (accepted)
		{
			identity.accepted_asset_id = accepted->asset_id;
			identity.accepted_content_generation = accepted->content_generation;
			identity.accepted_residency_revision = accepted->residency_revision;
		}
		if (runtime)
		{
			identity.active_content_generation = runtime->state.active_content_generation();
			identity.required_upload_count = runtime->state.required_upload_count();
			identity.completed_upload_count = runtime->state.completed_upload_count();
			identity.pending_component_upload_count = static_cast<uint32_t>(
				runtime->height_queue.size());
			identity.pending_component_removal_count = static_cast<uint32_t>(
				runtime->removal_queue.size());
		}
		identity.published_content_generation = m_published_view ?
			m_published_view->content_generation : 0u;
		const auto status = m_candidate_state ? m_candidate_state->work_status :
			(runtime ? runtime->work_status : TerrainRenderWorkStatus::Pending);
		identity.readiness = status == TerrainRenderWorkStatus::Ready ?
			TerrainRenderReadiness::Ready : status == TerrainRenderWorkStatus::Failed ?
				TerrainRenderReadiness::Failed : TerrainRenderReadiness::Pending;
		return identity;
	}

	std::string TerrainRenderAsset::get_last_error() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_last_error;
	}

	std::shared_ptr<StorageBuffer> TerrainRenderAsset::packed_height_buffer() const
	{
		const auto view = published_view();
		return view && view->runtime ? view->runtime->resources.height : nullptr;
	}

	std::shared_ptr<StorageBuffer>
		TerrainRenderAsset::dirty_weight_staging_buffer() const
	{
		const auto view = published_view();
		return view && view->runtime ? view->runtime->resources.staging : nullptr;
	}

	std::shared_ptr<RenderTarget> TerrainRenderAsset::weight_atlas(uint32_t index) const
	{
		const auto view = published_view();
		return view && view->runtime && index < view->runtime->resources.atlas.size() ?
			view->runtime->resources.atlas[index] : nullptr;
	}

	std::shared_ptr<RenderTarget> TerrainRenderAsset::coarse_weight_target() const
	{
		const auto view = published_view();
		return view && view->runtime ? view->runtime->resources.coarse : nullptr;
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

	bool TerrainRenderAsset::finalize_runtime_gpu_resources_locked(
		TerrainRenderRuntimeState& runtime,
		const TerrainRenderLayoutInfo& layout,
		TerrainRenderGpuOps& gpu_ops,
		bool candidate,
		std::string* out_error)
	{
		const auto fail = [&](const std::string& error)
		{
			if (candidate && m_candidate_state)
			{
				fail_candidate_locked(
					m_candidate_state->runtime,
					m_candidate_state->candidate_epoch,
					error);
			}
			else
			{
				fail_latest_work_locked(error);
			}
			return fail_with_error(out_error, m_last_error.c_str());
		};

		if (!runtime.resources.height)
		{
			runtime.resources.height = gpu_ops.create_storage_buffer({
				static_cast<uint32_t>(layout.height_buffer_bytes), sizeof(uint32_t),
				false, false, nullptr, "TerrainHeightWords" });
			if (!runtime.resources.height)
			{
				return fail("failed to create Terrain height resource.");
			}
		}
		if (!runtime.resources.staging)
		{
			runtime.resources.staging = gpu_ops.create_storage_buffer({
				k_terrain_weight_upload_bytes, k_terrain_weight_upload_stride,
				false, false, nullptr, "TerrainWeightUpload" });
			if (!runtime.resources.staging)
			{
				return fail("failed to create Terrain staging resource.");
			}
		}
		for (uint32_t atlas = 0u; atlas < runtime.resources.atlas.size(); ++atlas)
		{
			if (runtime.resources.atlas[atlas])
			{
				continue;
			}
			runtime.resources.atlas[atlas] = gpu_ops.create_render_target({
				static_cast<uint16_t>(k_terrain_weight_atlas_extent),
				static_cast<uint16_t>(k_terrain_weight_atlas_extent),
				RenderTextureFormat::RGBA8_UNORM, true, true,
				atlas == 0u ? "TerrainWeights0" : "TerrainWeights1" });
			if (!runtime.resources.atlas[atlas])
			{
				return fail(atlas == 0u ?
					"failed to create Terrain atlas 0 resource." :
					"failed to create Terrain atlas 1 resource.");
			}
		}
		if (!runtime.resources.coarse)
		{
			runtime.resources.coarse = gpu_ops.create_render_target({
				static_cast<uint16_t>(layout.coarse_width),
				static_cast<uint16_t>(layout.coarse_height),
				RenderTextureFormat::RGBA8_UNORM, true, true,
				"TerrainCoarseWeights" });
			if (!runtime.resources.coarse)
			{
				return fail("failed to create Terrain coarse resource.");
			}
		}

		const auto target = runtime.target_snapshot;
		if (!target)
		{
			return fail("Terrain runtime target snapshot is missing.");
		}
		const uint64_t content_generation = target->content_generation;
		const uint64_t residency_revision = target->residency_revision;
		for (const TerrainComponentCoord coord : runtime.removal_queue)
		{
			for (auto& slot : runtime.slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					slot = {};
				}
			}
			if (!runtime.state.mark_component_uploaded_for_snapshot(
					content_generation, residency_revision, coord))
			{
				return fail("failed to retire a Terrain component residency slot.");
			}
		}
		runtime.removal_queue.clear();

		constexpr uint32_t component_height_bytes =
			k_terrain_render_height_words_per_component * sizeof(uint32_t);
		const auto upload_start = std::chrono::steady_clock::now();
		uint64_t completed_bytes = 0u;
		size_t completed_uploads = 0u;
		std::vector<uint32_t> packed_height_words{};
		while (completed_uploads < runtime.height_queue.size() &&
			terrain_upload_budget_allows_next(
				completed_bytes,
				component_height_bytes,
				k_height_upload_byte_budget,
				std::chrono::steady_clock::now() - upload_start,
				k_height_upload_wall_clock_budget))
		{
			const auto& upload = runtime.height_queue[completed_uploads];
			if (upload.accepted_snapshot.lock() != target || !upload.component ||
				upload.content_generation != content_generation ||
				upload.residency_revision != residency_revision ||
				!layout.contains(upload.coord))
			{
				return fail("Terrain height upload identity is stale.");
			}
			std::string pack_error{};
			packed_height_words.clear();
			if (!build_terrain_component_height_words(
					*upload.component, target->height_mapping,
					packed_height_words, &pack_error) ||
				packed_height_words.size() !=
					k_terrain_render_height_words_per_component)
			{
				return fail(pack_error.empty() ?
					"failed to pack Terrain component height data." : pack_error);
			}
			const uint64_t offset_64 = layout.component_linear_index(upload.coord) *
				static_cast<uint64_t>(component_height_bytes);
			if (offset_64 > std::numeric_limits<uint32_t>::max() ||
				offset_64 + component_height_bytes > layout.height_buffer_bytes)
			{
				return fail("Terrain height upload exceeds its candidate resource.");
			}
			if (!gpu_ops.update_storage_buffer(
					runtime.resources.height,
					static_cast<uint32_t>(offset_64),
					component_height_bytes,
					packed_height_words.data()) ||
				!runtime.state.mark_component_uploaded_for_snapshot(
					content_generation, residency_revision, upload.coord))
			{
				return fail("failed to upload Terrain component height data.");
			}
			completed_bytes += component_height_bytes;
			++completed_uploads;
		}
		if (completed_uploads != 0u)
		{
			runtime.height_queue.erase(runtime.height_queue.begin(),
				runtime.height_queue.begin() + completed_uploads);
		}
		if (!runtime.height_queue.empty())
		{
			return false;
		}

		for (const TerrainComponentCoord coord : runtime.reset_queue)
		{
			for (auto& slot : runtime.slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					slot = {};
				}
			}
		}
		runtime.reset_queue.clear();
		if (runtime.state.readiness() == TerrainRenderReadiness::Pending &&
			!runtime.state.publish_snapshot(content_generation, residency_revision))
		{
			return fail("failed to complete Terrain height publication state.");
		}
		if (!candidate)
		{
			if (m_published_view && m_published_view->runtime.get() == &runtime &&
				m_published_view->snapshot != target)
			{
				auto next_view = std::make_shared<TerrainPublishedRenderView>();
				*next_view = *m_published_view;
				next_view->snapshot = target;
				next_view->asset_id = target->asset_id;
				next_view->content_generation = target->content_generation;
				next_view->residency_revision = target->residency_revision;
				++next_view->publication_epoch;
				m_published_view = std::move(next_view);
			}
			runtime.work_status = runtime.weight_queue.empty() ?
				TerrainRenderWorkStatus::Ready : TerrainRenderWorkStatus::Pending;
		}
		return !candidate && runtime.work_status == TerrainRenderWorkStatus::Ready;
	}

	bool TerrainRenderAsset::finalize_gpu_resources(
		TerrainRenderGpuOps& gpu_ops,
		std::string* out_error)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (out_error)
		{
			out_error->clear();
		}
		if (m_candidate_state)
		{
			if (m_candidate_state->work_status == TerrainRenderWorkStatus::Failed)
			{
				return fail_with_error(out_error, m_candidate_state->error.c_str());
			}
			if (m_candidate_state->work_status ==
				TerrainRenderWorkStatus::ReadyToPublish)
			{
				return false;
			}
			const auto runtime = m_candidate_state->runtime;
			if (!runtime)
			{
				const uint64_t candidate_epoch =
					m_candidate_state->candidate_epoch;
				fail_candidate_locked(
					runtime,
					candidate_epoch,
					"Terrain candidate runtime is missing.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
			m_candidate_state->stage = TerrainRenderCandidateStage::UploadHeights;
			const bool complete = finalize_runtime_gpu_resources_locked(
				*runtime, m_candidate_state->layout, gpu_ops, true, out_error);
			if (!m_candidate_state ||
				m_candidate_state->work_status == TerrainRenderWorkStatus::Failed)
			{
				return false;
			}
			if (runtime->height_queue.empty())
			{
				m_candidate_state->stage = TerrainRenderCandidateStage::AwaitGraphWork;
				update_candidate_ready_locked();
			}
			(void)complete;
			return false;
		}

		const auto runtime = current_incremental_runtime_locked();
		if (!runtime || runtime->work_status == TerrainRenderWorkStatus::Failed)
		{
			return false;
		}
		if (runtime->work_status == TerrainRenderWorkStatus::Ready)
		{
			return true;
		}
		return finalize_runtime_gpu_resources_locked(
			*runtime, m_published_view->layout, gpu_ops, false, out_error);
	}

	bool TerrainRenderAsset::finalize_gpu_resources(
		Renderer& renderer,
		std::string* out_error)
	{
		RendererTerrainGpuOps gpu_ops(renderer);
		return finalize_gpu_resources(gpu_ops, out_error);
	}

	std::vector<TerrainComponentCoord>
		TerrainRenderAsset::select_initial_resident_set(
			const TerrainAssetSnapshot& snapshot,
			const std::array<TerrainAtlasSlotMetadata,
				k_terrain_weight_atlas_slot_count>& old_slots,
			const std::vector<TerrainComponentCoord>& required)
	{
		TerrainRenderLayoutInfo layout{};
		if (!derive_terrain_render_layout(snapshot.layout, layout) ||
			snapshot.components.size() != layout.component_count)
		{
			return {};
		}
		struct Entry
		{
			TerrainComponentCoord coord{};
			uint64_t last_used_frame = 0u;
			bool required = false;
		};
		std::vector<Entry> entries{};
		std::array<bool, k_terrain_render_component_capacity> seen{};
		const auto valid = [&](TerrainComponentCoord coord)
		{
			if (!layout.contains(coord))
			{
				return false;
			}
			const auto& component =
				snapshot.components[layout.component_linear_index(coord)];
			return component && !component->weights.empty();
		};
		for (const auto coord : required)
		{
			if (!valid(coord))
			{
				continue;
			}
			const size_t index = layout.component_linear_index(coord);
			if (!seen[index])
			{
				entries.push_back({ coord, 0u, true });
				seen[index] = true;
			}
		}
		for (const auto& slot : old_slots)
		{
			if (!slot.occupied || !valid(slot.coord))
			{
				continue;
			}
			const size_t index = layout.component_linear_index(slot.coord);
			const auto found = std::find_if(entries.begin(), entries.end(),
				[&](const Entry& entry) { return entry.coord == slot.coord; });
			if (found != entries.end())
			{
				found->last_used_frame = std::max(
					found->last_used_frame, slot.last_used_frame);
			}
			else if (!seen[index])
			{
				entries.push_back({ slot.coord, slot.last_used_frame, false });
				seen[index] = true;
			}
		}
		std::sort(entries.begin(), entries.end(),
			[](const Entry& lhs, const Entry& rhs)
			{
				if (lhs.required != rhs.required)
				{
					return lhs.required > rhs.required;
				}
				if (lhs.last_used_frame != rhs.last_used_frame)
				{
					return lhs.last_used_frame > rhs.last_used_frame;
				}
				return lhs.coord.z != rhs.coord.z ?
					lhs.coord.z < rhs.coord.z : lhs.coord.x < rhs.coord.x;
			});
		std::vector<TerrainComponentCoord> selected{};
		selected.reserve(std::min(entries.size(),
			static_cast<size_t>(k_terrain_weight_atlas_slot_count)));
		for (size_t index = 0u;
			index < entries.size() && index < k_terrain_weight_atlas_slot_count;
			++index)
		{
			selected.push_back(entries[index].coord);
		}
		return selected;
	}

	void TerrainRenderAsset::freeze_candidate_initial_residency_locked(
		uint64_t render_frame_index)
	{
		if (!m_candidate_state || !m_candidate_state->runtime ||
			m_candidate_state->initial_set_frozen)
		{
			return;
		}
		const std::array<TerrainAtlasSlotMetadata,
			k_terrain_weight_atlas_slot_count> empty_slots{};
		const auto& old_slots = m_published_view && m_published_view->runtime ?
			m_published_view->runtime->slots : empty_slots;
		m_candidate_state->initial_resident_set = select_initial_resident_set(
			*m_candidate_state->snapshot,
			old_slots,
			m_candidate_state->runtime->latest_required_residency);
		m_candidate_state->runtime->weight_queue.clear();
		for (const auto coord : m_candidate_state->initial_resident_set)
		{
			const size_t index = m_candidate_state->layout.component_linear_index(coord);
			const auto& component = m_candidate_state->snapshot->components[index];
			if (!component)
			{
				continue;
			}
			TerrainGpuComponentUpload upload{};
			upload.asset_id = m_candidate_state->snapshot->asset_id;
			upload.accepted_snapshot = m_candidate_state->snapshot;
			upload.coord = coord;
			upload.content_generation =
				m_candidate_state->snapshot->content_generation;
			upload.residency_revision =
				m_candidate_state->snapshot->residency_revision;
			upload.component = component;
			m_candidate_state->runtime->weight_queue.push_back(std::move(upload));
		}
		m_candidate_state->initial_set_frozen = true;
		m_candidate_state->last_progress_frame_index = render_frame_index;
		update_candidate_ready_locked();
	}

	void TerrainRenderAsset::update_candidate_ready_locked()
	{
		if (!m_candidate_state || !m_candidate_state->runtime ||
			m_candidate_state->work_status != TerrainRenderWorkStatus::Pending ||
			!m_candidate_state->initial_set_frozen ||
			!m_candidate_state->runtime->height_queue.empty() ||
			!m_candidate_state->coarse_work.empty() ||
			!m_candidate_state->runtime->weight_queue.empty())
		{
			return;
		}
		m_candidate_state->stage = TerrainRenderCandidateStage::ReadyToPublish;
		m_candidate_state->work_status = TerrainRenderWorkStatus::ReadyToPublish;
	}

	bool TerrainRenderAsset::complete_candidate_coarse_locked(
		const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
		uint64_t candidate_epoch,
		const TerrainGpuComponentUpload& expected,
		bool succeeded,
		uint64_t render_frame_index)
	{
		if (!m_candidate_state || !m_candidate_state->runtime ||
			m_candidate_state->runtime != runtime ||
			m_candidate_state->candidate_epoch != candidate_epoch ||
			m_candidate_state->coarse_work.empty())
		{
			return false;
		}
		const TerrainGpuComponentUpload& pending =
			m_candidate_state->coarse_work.front();
		const auto expected_snapshot = expected.accepted_snapshot.lock();
		if (!expected_snapshot || pending.accepted_snapshot.lock() != expected_snapshot ||
			pending.asset_id != expected.asset_id || !(pending.coord == expected.coord) ||
			pending.content_generation != expected.content_generation ||
			pending.residency_revision != expected.residency_revision ||
			pending.component != expected.component)
		{
			return false;
		}
		if (!succeeded)
		{
			fail_candidate_locked(m_candidate_state->runtime,
				m_candidate_state->candidate_epoch,
				"failed to dispatch Terrain candidate coarse work.");
			return false;
		}
		m_candidate_state->coarse_work.erase(
			m_candidate_state->coarse_work.begin());
		m_candidate_state->last_progress_frame_index = render_frame_index;
		update_candidate_ready_locked();
		return true;
	}

	bool TerrainRenderAsset::complete_candidate_initial_locked(
		const std::shared_ptr<TerrainRenderRuntimeState>& runtime,
		uint64_t candidate_epoch,
		const TerrainGpuComponentUpload& expected,
		bool succeeded,
		uint64_t render_frame_index)
	{
		if (!m_candidate_state || !m_candidate_state->runtime ||
			m_candidate_state->runtime != runtime ||
			m_candidate_state->candidate_epoch != candidate_epoch ||
			m_candidate_state->runtime->weight_queue.empty())
		{
			return false;
		}
		if (!matches_pending_weight_update_locked(runtime, expected))
		{
			return false;
		}
		if (!succeeded)
		{
			fail_candidate_locked(m_candidate_state->runtime,
				m_candidate_state->candidate_epoch,
				"failed to dispatch Terrain candidate initial resident work.");
			return false;
		}
		const auto upload = m_candidate_state->runtime->weight_queue.front();
		const size_t completed = m_candidate_state->initial_resident_set.size() -
			m_candidate_state->runtime->weight_queue.size();
		if (completed >= m_candidate_state->runtime->slots.size())
		{
			fail_candidate_locked(m_candidate_state->runtime,
				m_candidate_state->candidate_epoch,
				"Terrain candidate initial resident slot overflowed.");
			return false;
		}
		auto& slot = m_candidate_state->runtime->slots[completed];
		slot.asset_id = upload.asset_id;
		slot.coord = upload.coord;
		slot.content_generation = upload.content_generation;
		slot.residency_revision = upload.residency_revision;
		slot.last_used_frame = render_frame_index;
		slot.occupied = true;
		m_candidate_state->runtime->weight_queue.erase(
			m_candidate_state->runtime->weight_queue.begin());
		m_candidate_state->last_progress_frame_index = render_frame_index;
		update_candidate_ready_locked();
		return true;
	}

	bool TerrainRenderAsset::publish_ready_candidate_locked()
	{
		if (!m_candidate_state || !m_candidate_state->runtime ||
			m_candidate_state->work_status != TerrainRenderWorkStatus::ReadyToPublish)
		{
			return false;
		}
		auto view = std::move(m_candidate_state->prepared_view);
		if (!view || view->snapshot != m_candidate_state->snapshot ||
			view->runtime != m_candidate_state->runtime)
		{
			const auto runtime = m_candidate_state->runtime;
			const uint64_t candidate_epoch = m_candidate_state->candidate_epoch;
			fail_candidate_locked(
				runtime,
				candidate_epoch,
				"Terrain candidate publication view is unavailable.");
			return false;
		}
		view->publication_epoch = m_published_view ?
			m_published_view->publication_epoch + 1u : 1u;
		m_candidate_state->runtime->work_status = TerrainRenderWorkStatus::Ready;
		m_published_view = std::move(view);
		m_candidate_state.reset();
		m_last_error.clear();
		return true;
	}

	void TerrainRenderAsset::record_required_residency(
		const std::shared_ptr<const TerrainPublishedRenderView>& view,
		const std::vector<TerrainComponentCoord>& required,
		uint64_t render_frame_index)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (view && view == m_published_view && view->runtime)
		{
			view->runtime->latest_required_residency = required;
			for (auto& slot : view->runtime->slots)
			{
				if (slot.occupied && std::find(required.begin(), required.end(),
					slot.coord) != required.end())
				{
					slot.last_used_frame = render_frame_index;
				}
			}
		}
		if (m_candidate_state && m_candidate_state->runtime &&
			(!view || view == m_published_view))
		{
			m_candidate_state->runtime->latest_required_residency = required;
		}
	}
}
