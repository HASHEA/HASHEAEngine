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

		m_has_active_content_generation = true;
		m_active_content_generation = content_generation;
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
		if (!m_has_active_content_generation ||
			content_generation != m_active_content_generation ||
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
		const bool same_accepted_asset = m_accepted_snapshot &&
			snapshot->asset_id == m_accepted_snapshot->asset_id;
		if (same_accepted_asset &&
			snapshot->content_generation <= m_accepted_snapshot->content_generation)
		{
			return fail_with_error(
				out_error, "terrain snapshot content generation is stale.");
		}
		const auto reject_snapshot =
			[&](
				std::string error,
				const TerrainRenderLayoutInfo* rejected_layout,
				bool preserve_accepted)
			{
				if (preserve_accepted)
				{
					m_last_error = std::move(error);
					return fail_with_error(out_error, m_last_error.c_str());
				}
				if (rejected_layout)
				{
					m_state.begin_content_generation_for_layout(
						snapshot->content_generation,
						0u,
						*rejected_layout,
						m_accepted_snapshot && !same_accepted_asset);
				}
				else
				{
					TerrainRenderLayoutInfo fallback_layout{};
					fallback_layout.layout.component_count_x =
						k_terrain_component_count;
					fallback_layout.layout.component_count_z =
						k_terrain_component_count;
					fallback_layout.component_count =
						k_terrain_render_component_capacity;
					fallback_layout.component_row_stride =
						k_terrain_component_count;
					m_state.begin_content_generation_for_layout(
						snapshot->content_generation,
						0u,
						fallback_layout,
						m_accepted_snapshot && !same_accepted_asset);
				}
				m_state.mark_failed(snapshot->content_generation);
				m_accepted_snapshot = snapshot;
				m_accepted_render_layout = {};
				m_has_accepted_render_layout = false;
				m_pending_component_uploads.clear();
				m_pending_weight_updates.clear();
				m_pending_implicit_weight_resets.clear();
				m_pending_component_removals.clear();
				m_last_error = std::move(error);
				return fail_with_error(out_error, m_last_error.c_str());
			};

		if (snapshot->failed)
		{
			const bool is_replacement = m_has_accepted_render_layout &&
				(!same_accepted_asset || !render_layouts_equal(
					snapshot->layout, m_accepted_render_layout.layout));
			return reject_snapshot(snapshot->failure_detail.empty() ?
				"terrain snapshot is failed." : snapshot->failure_detail,
				nullptr,
				is_replacement);
		}

		TerrainRenderLayoutInfo render_layout{};
		std::string render_layout_error{};
		if (!derive_terrain_render_layout(
				snapshot->layout, render_layout, &render_layout_error))
		{
			const bool is_replacement = m_has_accepted_render_layout &&
				(!same_accepted_asset || !render_layouts_equal(
					snapshot->layout, m_accepted_render_layout.layout));
			return reject_snapshot(
				std::move(render_layout_error), nullptr, is_replacement);
		}
		const bool is_replacement = m_has_accepted_render_layout &&
			(!same_accepted_asset || !render_layouts_equal(
				render_layout.layout, m_accepted_render_layout.layout));

		if (!std::isfinite(snapshot->height_mapping.height_offset) ||
			!std::isfinite(snapshot->height_mapping.height_range) ||
			snapshot->height_mapping.height_range <= 0.0f)
		{
			return reject_snapshot(
				describe_layout_error(
					snapshot->layout, "terrain height mapping is invalid."),
				&render_layout,
				is_replacement);
		}

		if (snapshot->components.size() != render_layout.component_count)
		{
			return reject_snapshot(
				describe_layout_error(
					snapshot->layout,
					"component table contains " +
						std::to_string(snapshot->components.size()) +
						" entries; expected " +
						std::to_string(render_layout.component_count) + "."),
				&render_layout,
				is_replacement);
		}

		for (size_t index = 0u; index < snapshot->components.size(); ++index)
		{
			const std::shared_ptr<const TerrainComponentSnapshot>& component =
				snapshot->components[index];
			const TerrainComponentCoord expected_coord{
				static_cast<uint16_t>(index % render_layout.component_row_stride),
				static_cast<uint16_t>(index / render_layout.component_row_stride)
			};
			if (!component)
			{
				if (is_replacement)
				{
					return reject_snapshot(
						describe_layout_error(
							snapshot->layout,
							"replacement snapshot has a null component at "
							"row-major slot " + std::to_string(index) + "."),
						&render_layout,
						true);
				}
				continue;
			}
			if (!(component->coord == expected_coord))
			{
				return reject_snapshot(
					describe_layout_error(
						snapshot->layout,
						"component at row-major slot " + std::to_string(index) +
							" has coord=(" + std::to_string(component->coord.x) +
							"," + std::to_string(component->coord.z) +
							"); expected=(" + std::to_string(expected_coord.x) +
							"," + std::to_string(expected_coord.z) + ")."),
					&render_layout,
					is_replacement);
			}
			std::string shape_error{};
			if (!validate_component_shape(*component, &shape_error))
			{
				return reject_snapshot(
					describe_layout_error(snapshot->layout, shape_error),
					&render_layout,
					is_replacement);
			}
		}

		const bool rebuild_after_failure =
			m_state.readiness() == TerrainRenderReadiness::Failed || is_replacement;
		const std::shared_ptr<const TerrainAssetSnapshot> previous_snapshot =
			is_replacement ? nullptr : m_accepted_snapshot;
		std::vector<TerrainGpuComponentUpload> uploads{};
		std::vector<TerrainGpuComponentUpload> weight_updates{};
		std::vector<TerrainComponentCoord> implicit_weight_resets{};
		std::vector<TerrainComponentCoord> removals{};
		std::vector<TerrainComponentCoord> resident_weight_rebinds{};
		std::array<bool, k_terrain_render_component_capacity> upload_scheduled{};
		std::array<bool, k_terrain_render_component_capacity> weight_scheduled{};
		std::array<bool, k_terrain_render_component_capacity> reset_scheduled{};
		std::array<bool, k_terrain_render_component_capacity> removal_scheduled{};

		const auto try_component_index =
			[&render_layout](TerrainComponentCoord coord, size_t& out_index)
			{
				if (!render_layout.contains(coord))
				{
					return false;
				}
				out_index = render_layout.component_linear_index(coord);
				return true;
			};
		const auto append_upload =
			[&](
				std::vector<TerrainGpuComponentUpload>& destination,
				std::array<bool, k_terrain_render_component_capacity>& scheduled,
				const std::shared_ptr<const TerrainComponentSnapshot>& component)
			{
				const size_t index =
					render_layout.component_linear_index(component->coord);
				if (scheduled[index])
				{
					return;
				}
				TerrainGpuComponentUpload upload{};
				upload.coord = component->coord;
				upload.content_generation = snapshot->content_generation;
				upload.component = component;
				destination.push_back(std::move(upload));
				scheduled[index] = true;
			};
		const auto append_coord =
			[&](
				std::vector<TerrainComponentCoord>& destination,
				std::array<bool, k_terrain_render_component_capacity>& scheduled,
				TerrainComponentCoord coord)
			{
				const size_t index = render_layout.component_linear_index(coord);
				if (!scheduled[index])
				{
					destination.push_back(coord);
					scheduled[index] = true;
				}
			};

		if (!rebuild_after_failure)
		{
			for (const TerrainGpuComponentUpload& pending :
				m_pending_component_uploads)
			{
				size_t index = 0u;
				if (!try_component_index(pending.coord, index) ||
					index >= snapshot->components.size() ||
					!pending.component ||
					snapshot->components[index] != pending.component ||
					upload_scheduled[index])
				{
					continue;
				}
				TerrainGpuComponentUpload carried = pending;
				carried.content_generation = snapshot->content_generation;
				uploads.push_back(std::move(carried));
				upload_scheduled[index] = true;
			}
			for (const TerrainGpuComponentUpload& pending : m_pending_weight_updates)
			{
				size_t index = 0u;
				if (!try_component_index(pending.coord, index) ||
					index >= snapshot->components.size() ||
					!pending.component ||
					pending.component->weights.empty() ||
					snapshot->components[index] != pending.component ||
					weight_scheduled[index])
				{
					continue;
				}
				TerrainGpuComponentUpload carried = pending;
				carried.content_generation = snapshot->content_generation;
				weight_updates.push_back(std::move(carried));
				weight_scheduled[index] = true;
			}
			for (TerrainComponentCoord coord : m_pending_implicit_weight_resets)
			{
				size_t index = 0u;
				if (!try_component_index(coord, index) ||
					index >= snapshot->components.size())
				{
					continue;
				}
				const std::shared_ptr<const TerrainComponentSnapshot>& component =
					snapshot->components[index];
				if (component && component->weights.empty())
				{
					append_coord(
						implicit_weight_resets, reset_scheduled, coord);
				}
			}
			for (TerrainComponentCoord coord : m_pending_component_removals)
			{
				size_t index = 0u;
				if (try_component_index(coord, index) &&
					index < snapshot->components.size() &&
					!snapshot->components[index])
				{
					append_coord(removals, removal_scheduled, coord);
				}
			}
		}

		for (size_t index = 0u; index < snapshot->components.size(); ++index)
		{
			const std::shared_ptr<const TerrainComponentSnapshot>& component =
				snapshot->components[index];
			const TerrainComponentCoord expected_coord{
				static_cast<uint16_t>(index % render_layout.component_row_stride),
				static_cast<uint16_t>(index / render_layout.component_row_stride)
			};
			const std::shared_ptr<const TerrainComponentSnapshot> previous_component =
				previous_snapshot && index < previous_snapshot->components.size() ?
					previous_snapshot->components[index] : nullptr;
			if (!component)
			{
				if (rebuild_after_failure || previous_component)
				{
					append_coord(removals, removal_scheduled, expected_coord);
				}
				continue;
			}

			if (!rebuild_after_failure && previous_component == component)
			{
				if (!component->weights.empty() && !weight_scheduled[index])
				{
					resident_weight_rebinds.push_back(expected_coord);
				}
				continue;
			}

			append_upload(uploads, upload_scheduled, component);
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
					append_coord(
						implicit_weight_resets, reset_scheduled, expected_coord);
				}
			}
			else
			{
				append_upload(weight_updates, weight_scheduled, component);
			}
		}

		m_state.begin_content_generation_for_layout(
			snapshot->content_generation,
			static_cast<uint32_t>(uploads.size() + removals.size()),
			render_layout,
			m_accepted_snapshot && !same_accepted_asset);
		for (TerrainComponentCoord coord : resident_weight_rebinds)
		{
			for (TerrainAtlasSlotMetadata& slot : m_frame_boundary_atlas_slots)
			{
				if (slot.occupied && slot.coord == coord)
				{
					slot.content_generation = snapshot->content_generation;
				}
			}
		}
		m_accepted_snapshot = snapshot;
		m_accepted_render_layout = render_layout;
		m_has_accepted_render_layout = true;
		m_pending_component_uploads = std::move(uploads);
		m_pending_weight_updates = std::move(weight_updates);
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

		TerrainRenderLayoutInfo render_layout{};
		std::string render_layout_error{};
		if (!derive_terrain_render_layout(
				m_accepted_snapshot->layout,
				render_layout,
				&render_layout_error))
		{
			fail_active_generation(render_layout_error);
			return fail_with_error(out_error, m_last_error.c_str());
		}

		if (!m_packed_height_buffer)
		{
			m_packed_height_buffer = renderer.create_storage_buffer({
				static_cast<uint32_t>(render_layout.height_buffer_bytes),
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
				static_cast<uint16_t>(render_layout.coarse_width),
				static_cast<uint16_t>(render_layout.coarse_height),
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

			if (!render_layout.contains(upload.coord))
			{
				fail_active_generation(
					"Terrain height upload coordinate is outside the accepted layout.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
			const uint64_t offset_64 =
				render_layout.component_linear_index(upload.coord) *
				static_cast<uint64_t>(component_height_bytes);
			if (offset_64 > std::numeric_limits<uint32_t>::max() ||
				offset_64 + component_height_bytes >
					render_layout.height_buffer_bytes)
			{
				fail_active_generation(
					"Terrain height upload range exceeds the accepted layout buffer.");
				return fail_with_error(out_error, m_last_error.c_str());
			}
			const uint32_t offset = static_cast<uint32_t>(offset_64);
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
