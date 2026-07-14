#include "Function/Asset/TerrainImport.h"

#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace AshEngine::TerrainImportDetail
{
	using RawRowProvider =
		std::function<bool(uint32_t, std::vector<float>&, std::string*)>;

	auto decode_raw_height_file(
		const TerrainHeightImportDesc& desc,
		std::vector<float>& out_heights,
		std::string* out_error) -> TerrainImportResult;

	auto write_raw_height_file(
		const TerrainHeightExportDesc& desc,
		uint32_t width,
		uint32_t height,
		const TerrainHeightMapping& mapping,
		const RawRowProvider& row_provider,
		std::string* out_error) -> TerrainImportResult;
}

namespace AshEngine
{
	namespace
	{
		auto set_error(
			TerrainImportResult result,
			std::string* out_error,
			const char* detail) noexcept -> TerrainImportResult
		{
			if (out_error != nullptr)
			{
				try
				{
					*out_error = detail;
				}
				catch (...)
				{
					out_error->clear();
				}
			}
			return result;
		}

		auto checked_multiply(uint64_t lhs, uint64_t rhs, uint64_t& out_result) -> bool
		{
			if (lhs != 0u && rhs > std::numeric_limits<uint64_t>::max() / lhs)
			{
				return false;
			}
			out_result = lhs * rhs;
			return true;
		}

		auto checked_add(uint64_t lhs, uint64_t rhs, uint64_t& out_result) -> bool
		{
			if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
			{
				return false;
			}
			out_result = lhs + rhs;
			return true;
		}

		auto valid_height_mapping(const TerrainHeightMapping& mapping) -> bool
		{
			return std::isfinite(mapping.height_offset) &&
				std::isfinite(mapping.height_range) && mapping.height_range > 0.0f &&
				std::isfinite(mapping.height_offset + mapping.height_range);
		}

		auto valid_byte_order(TerrainByteOrder order) -> bool
		{
			return order == TerrainByteOrder::LittleEndian ||
				order == TerrainByteOrder::BigEndian;
		}

		auto valid_resize_policy(TerrainResizePolicy policy) -> bool
		{
			return policy == TerrainResizePolicy::Reject ||
				policy == TerrainResizePolicy::Crop ||
				policy == TerrainResizePolicy::CatmullRom;
		}

		auto estimate_import_peak_bytes(
			const TerrainHeightImportDesc& desc,
			uint64_t& out_estimate) -> bool
		{
			uint64_t source_samples = 0u;
			uint64_t target_samples = 0u;
			uint64_t component_count = 0u;
			const uint64_t component_side =
				static_cast<uint64_t>(desc.target_layout.component_quad_count) + 1u;
			uint64_t component_samples = 0u;
			uint64_t source_bytes = 0u;
			uint64_t row_bytes = 0u;
			uint64_t base_bytes = 0u;
			uint64_t precision_bytes = 0u;
			uint64_t component_bytes = 0u;
			if (!checked_multiply(desc.source_width, desc.source_height, source_samples) ||
				!checked_multiply(
					desc.target_layout.sample_count_x,
					desc.target_layout.sample_count_z,
					target_samples) ||
				!checked_multiply(
					desc.target_layout.component_count_x,
					desc.target_layout.component_count_z,
					component_count) ||
				!checked_multiply(component_side, component_side, component_samples) ||
				!checked_multiply(component_samples, component_count, component_samples) ||
				!checked_multiply(source_samples, sizeof(float), source_bytes) ||
				!checked_multiply(desc.source_width, sizeof(float), row_bytes) ||
				!checked_multiply(target_samples, sizeof(uint16_t), base_bytes) ||
				// Four height bytes plus a conservative byte per sample for the
				// resident min/max hierarchy built during composition.
				!checked_multiply(component_samples, 5u, component_bytes))
			{
				return false;
			}
			if (desc.format == TerrainHeightFileFormat::RawR32F &&
				!checked_multiply(target_samples, 2u * sizeof(float), precision_bytes))
			{
				return false;
			}

			uint64_t decode_peak = 0u;
			uint64_t conversion_peak = 0u;
			uint64_t composition_peak = 0u;
			// These phases are mutually exclusive: the decoder row dies before
			// conversion, and the source vector is released before composition.
			if (!checked_add(source_bytes, row_bytes, decode_peak) ||
				!checked_add(source_bytes, base_bytes, conversion_peak) ||
				!checked_add(conversion_peak, precision_bytes, conversion_peak) ||
				!checked_add(base_bytes, precision_bytes, composition_peak) ||
				!checked_add(composition_peak, component_bytes, composition_peak))
			{
				return false;
			}
			constexpr uint64_t allocation_headroom = 32ull * 1024ull * 1024ull;
			return checked_add(
				std::max({ decode_peak, conversion_peak, composition_peak }),
				allocation_headroom,
				out_estimate);
		}

		auto estimate_container_pipeline_peak_bytes(
			const TerrainHeightImportDesc& desc,
			uint64_t& out_estimate) -> bool
		{
			uint64_t import_peak = 0u;
			uint64_t target_samples = 0u;
			uint64_t component_count = 0u;
			const uint64_t component_side =
				static_cast<uint64_t>(desc.target_layout.component_quad_count) + 1u;
			uint64_t component_samples = 0u;
			uint64_t resident = 0u;
			uint64_t value = 0u;
			if (!estimate_import_peak_bytes(desc, import_peak) ||
				!checked_multiply(
					desc.target_layout.sample_count_x,
					desc.target_layout.sample_count_z,
					target_samples) ||
				!checked_multiply(
					desc.target_layout.component_count_x,
					desc.target_layout.component_count_z,
					component_count) ||
				!checked_multiply(component_side, component_side, component_samples) ||
				!checked_multiply(component_samples, component_count, component_samples) ||
				!checked_multiply(target_samples, sizeof(uint16_t), value) ||
				!checked_add(resident, value, resident) ||
				!checked_multiply(component_samples, sizeof(float), value) ||
				!checked_add(resident, value, resident))
			{
				return false;
			}
			if (desc.format == TerrainHeightFileFormat::RawR32F &&
				(!checked_multiply(target_samples, 8u, value) ||
					!checked_add(resident, value, resident)))
			{
				return false;
			}
			constexpr uint64_t pipeline_headroom = 64ull * 1024ull * 1024ull;
			uint64_t pipeline_peak = 0u;
			// Full saves stream Base and one component/edit block at a time. The
			// imported snapshot is released before the validated reload begins.
			if (!checked_add(resident, pipeline_headroom, pipeline_peak))
			{
				return false;
			}
			out_estimate = std::max(import_peak, pipeline_peak);
			return true;
		}

		auto catmull_rom(
			double p0,
			double p1,
			double p2,
			double p3,
			double t) -> double
		{
			const double t2 = t * t;
			const double t3 = t2 * t;
			return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
				(2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
				(-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
		}

		auto sample_catmull_rom(
			const std::vector<float>& source,
			uint32_t source_width,
			uint32_t source_height,
			uint32_t target_x,
			uint32_t target_z,
			uint32_t target_width,
			uint32_t target_height) -> float
		{
			const double source_x = target_width == 1u ? 0.0 :
				static_cast<double>(target_x) * (source_width - 1u) / (target_width - 1u);
			const double source_z = target_height == 1u ? 0.0 :
				static_cast<double>(target_z) * (source_height - 1u) / (target_height - 1u);
			const int64_t base_x = static_cast<int64_t>(std::floor(source_x));
			const int64_t base_z = static_cast<int64_t>(std::floor(source_z));
			const double tx = source_x - static_cast<double>(base_x);
			const double tz = source_z - static_cast<double>(base_z);
			double rows[4]{};
			for (int64_t row = -1; row <= 2; ++row)
			{
				double samples[4]{};
				const uint32_t z = static_cast<uint32_t>(std::clamp<int64_t>(
					base_z + row, 0, static_cast<int64_t>(source_height) - 1));
				for (int64_t column = -1; column <= 2; ++column)
				{
					const uint32_t x = static_cast<uint32_t>(std::clamp<int64_t>(
						base_x + column, 0, static_cast<int64_t>(source_width) - 1));
					samples[column + 1] = source[static_cast<size_t>(z) * source_width + x];
				}
				rows[row + 1] = catmull_rom(
					samples[0], samples[1], samples[2], samples[3], tx);
			}
			return static_cast<float>(catmull_rom(rows[0], rows[1], rows[2], rows[3], tz));
		}

		auto replace_file_atomically(
			const std::filesystem::path& temporary,
			const std::filesystem::path& destination) -> bool
		{
#if defined(_WIN32)
			return (std::filesystem::exists(destination)
				? ReplaceFileW(
					destination.c_str(), temporary.c_str(), nullptr,
					REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
				: MoveFileExW(
					temporary.c_str(), destination.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) != FALSE;
#else
			std::error_code error_code{};
			std::filesystem::rename(temporary, destination, error_code);
			return !error_code;
#endif
		}

		auto map_container_result(TerrainContainerResult result) -> TerrainImportResult
		{
			return result == TerrainContainerResult::IoFailure ||
				result == TerrainContainerResult::NotFound
				? TerrainImportResult::IoFailure
				: TerrainImportResult::DecodeFailure;
		}
	}

	TerrainCancellationToken::TerrainCancellationToken()
		: m_cancelled(std::make_shared<std::atomic<bool>>(false))
	{
	}

	TerrainCancellationToken::TerrainCancellationToken(
		const TerrainCancellationToken& other) = default;

	TerrainCancellationToken::TerrainCancellationToken(
		TerrainCancellationToken&& other) noexcept = default;

	auto TerrainCancellationToken::operator=(
		const TerrainCancellationToken& other) -> TerrainCancellationToken& = default;

	auto TerrainCancellationToken::operator=(
		TerrainCancellationToken&& other) noexcept -> TerrainCancellationToken& = default;

	TerrainCancellationToken::~TerrainCancellationToken() = default;

	void TerrainCancellationToken::cancel()
	{
		if (!m_cancelled)
		{
			m_cancelled = std::make_shared<std::atomic<bool>>(false);
		}
		m_cancelled->store(true, std::memory_order_release);
	}

	bool TerrainCancellationToken::is_cancelled() const
	{
		return m_cancelled && m_cancelled->load(std::memory_order_acquire);
	}

	TerrainImportResult import_terrain_height(
		TerrainAssetId asset_id,
		const TerrainHeightImportDesc& desc,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainImportReport* out_report,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		try
		{
			if (desc.source_path.empty() || desc.source_width == 0u ||
				desc.source_height == 0u ||
				!is_valid_terrain_grid_layout(desc.target_layout) ||
				!valid_height_mapping(desc.height_mapping) ||
				!valid_byte_order(desc.byte_order) ||
				!valid_resize_policy(desc.resize_policy))
			{
				return set_error(TerrainImportResult::InvalidArguments, out_error,
					"Terrain height import arguments are invalid.");
			}
			if (desc.format != TerrainHeightFileFormat::RawR16 &&
				desc.format != TerrainHeightFileFormat::RawR32F)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"This Terrain height format is not implemented by the RAW importer.");
			}
			const bool dimensions_match =
				desc.source_width == desc.target_layout.sample_count_x &&
				desc.source_height == desc.target_layout.sample_count_z;
			if (desc.resize_policy == TerrainResizePolicy::Reject && !dimensions_match)
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"Terrain source dimensions do not match the target layout.");
			}
			if (desc.resize_policy == TerrainResizePolicy::Crop &&
				(desc.source_width < desc.target_layout.sample_count_x ||
					desc.source_height < desc.target_layout.sample_count_z))
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"Terrain center crop cannot enlarge the source.");
			}
			if (desc.cancellation.is_cancelled())
			{
				return set_error(TerrainImportResult::Cancelled, out_error,
					"Terrain height import was cancelled.");
			}
			uint64_t peak_estimate = 0u;
			if (!estimate_import_peak_bytes(desc, peak_estimate) ||
				peak_estimate > desc.peak_memory_limit_bytes)
			{
				return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
					"Terrain height import exceeds its peak memory limit.");
			}

			std::vector<float> source{};
			const TerrainImportResult decode_result =
				TerrainImportDetail::decode_raw_height_file(desc, source, out_error);
			if (decode_result != TerrainImportResult::Success)
			{
				return decode_result;
			}
			const uint32_t target_width = desc.target_layout.sample_count_x;
			const uint32_t target_height = desc.target_layout.sample_count_z;
			const size_t target_count =
				static_cast<size_t>(target_width) * target_height;
			const size_t component_count =
				static_cast<size_t>(desc.target_layout.component_count_x) *
				desc.target_layout.component_count_z;
			std::vector<uint16_t> encoded(target_count);
			TerrainEditLayer precision_layer{};
			const bool preserve_r32f = desc.format == TerrainHeightFileFormat::RawR32F;
			if (preserve_r32f)
			{
				if (component_count > std::numeric_limits<uint16_t>::max())
				{
					return set_error(TerrainImportResult::InvalidDimensions, out_error,
						"Terrain R32F precision layer exceeds the container block limit.");
				}
				precision_layer.id.bytes = {
					0x41u, 0x73u, 0x68u, 0x52u, 0x33u, 0x32u, 0x46u, 0x50u,
					0x72u, 0x65u, 0x63u, 0x69u, 0x73u, 0x69u, 0x6fu, 0x6eu
				};
				precision_layer.name = "Imported R32F Precision";
				precision_layer.height_blend_mode = TerrainHeightBlendMode::Alpha;
				precision_layer.height_blocks.reserve(component_count);
				for (uint32_t component_z = 0u;
					component_z < desc.target_layout.component_count_z; ++component_z)
				{
					for (uint32_t component_x = 0u;
						component_x < desc.target_layout.component_count_x; ++component_x)
					{
						TerrainSparseHeightBlock block{};
						block.owner = {
							static_cast<uint16_t>(component_x),
							static_cast<uint16_t>(component_z)
						};
						block.changed_rect = get_terrain_component_owned_rect(
							desc.target_layout, block.owner);
						const size_t area = static_cast<size_t>(block.changed_rect.width()) *
							block.changed_rect.height();
						block.values.resize(area);
						block.coverage.assign(area, 1.0f);
						precision_layer.height_blocks.push_back(std::move(block));
					}
				}
			}
			const uint32_t crop_x = desc.source_width >= target_width
				? (desc.source_width - target_width) / 2u : 0u;
			const uint32_t crop_z = desc.source_height >= target_height
				? (desc.source_height - target_height) / 2u : 0u;
			for (uint32_t z = 0u; z < target_height; ++z)
			{
				if (desc.cancellation.is_cancelled())
				{
					return set_error(TerrainImportResult::Cancelled, out_error,
						"Terrain height resize was cancelled.");
				}
				for (uint32_t x = 0u; x < target_width; ++x)
				{
					float world_height = 0.0f;
					if (desc.resize_policy == TerrainResizePolicy::CatmullRom &&
						!dimensions_match)
					{
						world_height = sample_catmull_rom(
							source, desc.source_width, desc.source_height,
							x, z, target_width, target_height);
					}
					else
					{
						const uint32_t source_x = desc.resize_policy == TerrainResizePolicy::Crop
							? x + crop_x : x;
						const uint32_t source_z = desc.resize_policy == TerrainResizePolicy::Crop
							? z + crop_z : z;
						world_height = source[static_cast<size_t>(source_z) *
							desc.source_width + source_x];
					}
					if (!std::isfinite(world_height))
					{
						return set_error(TerrainImportResult::DecodeFailure, out_error,
							"Terrain height resize produced a non-finite value.");
					}
					encoded[static_cast<size_t>(z) * target_width + x] =
						encode_terrain_height_r16(world_height, desc.height_mapping);
					if (preserve_r32f)
					{
						const TerrainComponentCoord owner = get_terrain_sample_owner(
							desc.target_layout, x, z);
						TerrainSparseHeightBlock& block = precision_layer.height_blocks[
							static_cast<size_t>(owner.z) *
								desc.target_layout.component_count_x + owner.x];
						const size_t local = static_cast<size_t>(z - block.changed_rect.min_z) *
							block.changed_rect.width() + (x - block.changed_rect.min_x);
						block.values[local] = world_height;
					}
				}
			}
			std::vector<float>{}.swap(source);

			TerrainWorkingSet working_set{};
			working_set.asset_id = asset_id;
			working_set.source_path = desc.source_path;
			working_set.layout = desc.target_layout;
			working_set.height_mapping = desc.height_mapping;
			working_set.content_generation = 1u;
			working_set.base_heights = std::move(encoded);
			if (preserve_r32f)
			{
				working_set.edit_layers.push_back(std::move(precision_layer));
			}
			working_set.components.resize(component_count);
			working_set.dirty_components.reserve(component_count);
			for (uint32_t z = 0u; z < desc.target_layout.component_count_z; ++z)
			{
				for (uint32_t x = 0u; x < desc.target_layout.component_count_x; ++x)
				{
					working_set.dirty_components.push_back({
						static_cast<uint16_t>(x), static_cast<uint16_t>(z) });
				}
			}
			std::vector<TerrainDirtyComponentPayload> payloads{};
			if (!compose_terrain_components(
					working_set, working_set.dirty_components, payloads, out_error))
			{
				return TerrainImportResult::DecodeFailure;
			}
			if (payloads.size() != component_count)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Terrain import composition did not return every component.");
			}
			if (desc.cancellation.is_cancelled())
			{
				return set_error(TerrainImportResult::Cancelled, out_error,
					"Terrain height import was cancelled before publication.");
			}
			auto mutable_snapshot = std::make_shared<TerrainAssetSnapshot>();
			auto immutable_base = std::make_shared<const std::vector<uint16_t>>(
				std::move(working_set.base_heights));
			auto immutable_layers = std::make_shared<const std::vector<TerrainEditLayer>>(
				std::move(working_set.edit_layers));
			mutable_snapshot->asset_id = working_set.asset_id;
			mutable_snapshot->source_path = working_set.source_path;
			mutable_snapshot->layout = working_set.layout;
			mutable_snapshot->height_mapping = working_set.height_mapping;
			mutable_snapshot->material_layers = working_set.material_layers;
			mutable_snapshot->content_generation = working_set.content_generation;
			mutable_snapshot->residency_revision = working_set.residency_revision;
			mutable_snapshot->base_heights = std::move(immutable_base);
			mutable_snapshot->edit_layers = std::move(immutable_layers);
			mutable_snapshot->components = std::move(working_set.components);
			for (const TerrainDirtyComponentPayload& payload : payloads)
			{
				const size_t index = static_cast<size_t>(payload.coord.z) *
					desc.target_layout.component_count_x + payload.coord.x;
				if (!payload.component || index >= mutable_snapshot->components.size())
				{
					return set_error(TerrainImportResult::DecodeFailure, out_error,
						"Terrain import composition returned an invalid component.");
				}
				mutable_snapshot->components[index] = payload.component;
			}
			std::shared_ptr<const TerrainAssetSnapshot> candidate =
				std::move(mutable_snapshot);
			TerrainImportReport report{};
			report.source_width = desc.source_width;
			report.source_height = desc.source_height;
			report.source_bits_per_sample =
				desc.format == TerrainHeightFileFormat::RawR16 ? 16u : 32u;
			if (out_report != nullptr)
			{
				out_report->source_width = report.source_width;
				out_report->source_height = report.source_height;
				out_report->source_bits_per_sample = report.source_bits_per_sample;
				out_report->warnings.swap(report.warnings);
			}
			out_snapshot = std::move(candidate);
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"Terrain height import allocation failed.");
		}
		catch (const std::length_error&)
		{
			return set_error(TerrainImportResult::InvalidDimensions, out_error,
				"Terrain height import dimensions exceed container limits.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			return set_error(TerrainImportResult::IoFailure, out_error,
				"Terrain height import filesystem operation failed.");
		}
	}

	TerrainImportResult export_terrain_height(
		const TerrainAssetSnapshot& snapshot,
		const TerrainHeightExportDesc& desc,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		if (desc.destination_path.empty() || !is_valid_terrain_grid_layout(snapshot.layout) ||
			!valid_height_mapping(snapshot.height_mapping) || !valid_byte_order(desc.byte_order))
		{
			return set_error(TerrainImportResult::InvalidArguments, out_error,
				"Terrain height export arguments are invalid.");
		}
		if (desc.format != TerrainHeightFileFormat::RawR16 &&
			desc.format != TerrainHeightFileFormat::RawR32F)
		{
			return set_error(TerrainImportResult::UnsupportedFormat, out_error,
				"This Terrain height format is not implemented by the RAW exporter.");
		}

		const TerrainEditLayer* selected_layer = nullptr;
		if (desc.source == TerrainExportSource::HeightEditLayer ||
			desc.source == TerrainExportSource::MaterialWeightLayer)
		{
			if (!snapshot.edit_layers || !desc.source_layer_id.is_valid())
			{
				return set_error(TerrainImportResult::InvalidArguments, out_error,
					"Terrain layer export requires a valid source layer.");
			}
			const auto found = std::find_if(
				snapshot.edit_layers->begin(), snapshot.edit_layers->end(),
				[&](const TerrainEditLayer& layer)
				{
					return layer.id == desc.source_layer_id;
				});
			if (found == snapshot.edit_layers->end())
			{
				return set_error(TerrainImportResult::InvalidArguments, out_error,
					"Terrain export source layer was not found.");
			}
			selected_layer = &*found;
		}
		if (desc.source == TerrainExportSource::MaterialWeightLayer &&
			desc.material_layer_index >= k_terrain_material_layer_count)
		{
			return set_error(TerrainImportResult::InvalidArguments, out_error,
				"Terrain material weight export index is invalid.");
		}
		if (desc.source != TerrainExportSource::FinalComposedHeight &&
			desc.source != TerrainExportSource::BaseHeight &&
			desc.source != TerrainExportSource::HeightEditLayer &&
			desc.source != TerrainExportSource::MaterialWeightLayer)
		{
			return set_error(TerrainImportResult::InvalidArguments, out_error,
				"Terrain export source is invalid.");
		}
		const size_t expected_global_samples =
			static_cast<size_t>(snapshot.layout.sample_count_x) * snapshot.layout.sample_count_z;
		if ((desc.source == TerrainExportSource::BaseHeight &&
				(!snapshot.base_heights ||
					snapshot.base_heights->size() != expected_global_samples)) ||
			(desc.source == TerrainExportSource::FinalComposedHeight &&
				snapshot.components.size() != static_cast<size_t>(
					snapshot.layout.component_count_x) * snapshot.layout.component_count_z))
		{
			return set_error(TerrainImportResult::InvalidArguments, out_error,
				"Terrain export source data is unavailable.");
		}

		TerrainImportDetail::RawRowProvider provider =
			[&](uint32_t z, std::vector<float>& out_values, std::string* error) -> bool
			{
				const uint32_t width = snapshot.layout.sample_count_x;
				out_values.assign(width, 0.0f);
				if (desc.source == TerrainExportSource::BaseHeight)
				{
					for (uint32_t x = 0u; x < width; ++x)
					{
						out_values[x] = decode_terrain_height_r16(
							(*snapshot.base_heights)[static_cast<size_t>(z) * width + x],
							snapshot.height_mapping);
					}
					return true;
				}
				if (desc.source == TerrainExportSource::FinalComposedHeight)
				{
					for (uint32_t x = 0u; x < width; ++x)
					{
						const TerrainComponentCoord owner =
							get_terrain_sample_owner(snapshot.layout, x, z);
						const size_t component_index = static_cast<size_t>(owner.z) *
							snapshot.layout.component_count_x + owner.x;
						if (component_index >= snapshot.components.size() ||
							!snapshot.components[component_index])
						{
							set_error(TerrainImportResult::EncodeFailure, error,
								"Terrain composed export component is missing.");
							return false;
						}
						const TerrainComponentSnapshot& component =
							*snapshot.components[component_index];
						const TerrainSampleRect rect =
							get_terrain_component_snapshot_rect(snapshot.layout, owner);
						const size_t local = static_cast<size_t>(z - rect.min_z) *
							component.sample_width + (x - rect.min_x);
						if (local >= component.heights.size())
						{
							set_error(TerrainImportResult::EncodeFailure, error,
								"Terrain composed export component shape is invalid.");
							return false;
						}
						out_values[x] = component.heights[local];
					}
					return true;
				}
				if (desc.source == TerrainExportSource::HeightEditLayer)
				{
					for (const TerrainSparseHeightBlock& block : selected_layer->height_blocks)
					{
						if (z < block.changed_rect.min_z || z >= block.changed_rect.max_z_exclusive)
						{
							continue;
						}
						for (uint32_t x = block.changed_rect.min_x;
							x < block.changed_rect.max_x_exclusive; ++x)
						{
							const size_t local = static_cast<size_t>(z - block.changed_rect.min_z) *
								block.changed_rect.width() + (x - block.changed_rect.min_x);
							if (x >= width || local >= block.values.size() ||
								local >= block.coverage.size())
							{
								return false;
							}
							out_values[x] = block.values[local] * block.coverage[local];
						}
					}
					return true;
				}
				for (const TerrainSparseWeightBlock& block : selected_layer->weight_blocks)
				{
					if (z < block.changed_rect.min_z || z >= block.changed_rect.max_z_exclusive)
					{
						continue;
					}
					for (uint32_t x = block.changed_rect.min_x;
						x < block.changed_rect.max_x_exclusive; ++x)
					{
						const size_t local = static_cast<size_t>(z - block.changed_rect.min_z) *
							block.changed_rect.width() + (x - block.changed_rect.min_x);
						if (x >= width || local >= block.values.size() ||
							local >= block.coverage.size())
						{
							return false;
						}
						double sum = 0.0;
						for (float value : block.values[local])
						{
							if (!std::isfinite(value) || value < 0.0f)
							{
								return false;
							}
							sum += value;
						}
						const double normalized = sum > 0.0
							? block.values[local][desc.material_layer_index] / sum : 0.0;
						out_values[x] = static_cast<float>(std::clamp(normalized, 0.0, 1.0) *
							std::clamp(static_cast<double>(block.coverage[local]), 0.0, 1.0));
					}
				}
				return true;
			};

		return TerrainImportDetail::write_raw_height_file(
			desc,
			snapshot.layout.sample_count_x,
			snapshot.layout.sample_count_z,
			snapshot.height_mapping,
			provider,
			out_error);
	}

	TerrainImportResult import_terrain_height_to_container(
		TerrainAssetId asset_id,
		const TerrainHeightImportDesc& desc,
		const std::filesystem::path& destination_path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainImportReport* out_report,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		if (destination_path.empty())
		{
			return set_error(TerrainImportResult::InvalidArguments, out_error,
				"Terrain container import destination is empty.");
		}
		std::filesystem::path temporary = destination_path;
		temporary += ".import.tmp";
		std::error_code error_code{};
		std::filesystem::remove(temporary, error_code);
		try
		{
			if (!desc.source_path.empty() && desc.source_width != 0u &&
				desc.source_height != 0u && is_valid_terrain_grid_layout(desc.target_layout) &&
				valid_height_mapping(desc.height_mapping) && valid_byte_order(desc.byte_order) &&
				valid_resize_policy(desc.resize_policy) &&
				(desc.format == TerrainHeightFileFormat::RawR16 ||
					desc.format == TerrainHeightFileFormat::RawR32F))
			{
				uint64_t pipeline_peak = 0u;
				if (!estimate_container_pipeline_peak_bytes(desc, pipeline_peak) ||
					pipeline_peak > desc.peak_memory_limit_bytes)
				{
					return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
						"Terrain container import exceeds its peak memory limit.");
				}
			}
			std::shared_ptr<const TerrainAssetSnapshot> imported{};
			TerrainImportReport report{};
			const TerrainImportResult import_result = import_terrain_height(
				asset_id, desc, imported, &report, out_error);
			if (import_result != TerrainImportResult::Success)
			{
				std::filesystem::remove(temporary, error_code);
				return import_result;
			}
			if (desc.cancellation.is_cancelled())
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::Cancelled, out_error,
					"Terrain container import was cancelled.");
			}
			const TerrainContainerResult save_result =
				save_terrain_container_incremental(temporary, *imported, {}, nullptr, out_error);
			if (save_result != TerrainContainerResult::Success)
			{
				std::filesystem::remove(temporary, error_code);
				return map_container_result(save_result);
			}
			imported.reset();
			std::shared_ptr<const TerrainAssetSnapshot> validated{};
			const TerrainContainerResult load_result =
				load_terrain_container(temporary, validated, nullptr, out_error);
			if (load_result != TerrainContainerResult::Success || !validated)
			{
				std::filesystem::remove(temporary, error_code);
				return map_container_result(load_result);
			}
			if (desc.cancellation.is_cancelled())
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::Cancelled, out_error,
					"Terrain container import was cancelled before publication.");
			}
			auto prepared = std::make_shared<TerrainAssetSnapshot>(*validated);
			prepared->asset_id = asset_id;
			prepared->source_path = destination_path;
			std::shared_ptr<const TerrainAssetSnapshot> prepared_publication =
				std::move(prepared);
			if (!replace_file_atomically(temporary, destination_path))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to atomically publish the imported Terrain container.");
			}
			out_snapshot = std::move(prepared_publication);
			if (out_report != nullptr)
			{
				out_report->source_width = report.source_width;
				out_report->source_height = report.source_height;
				out_report->source_bits_per_sample = report.source_bits_per_sample;
				out_report->warnings.swap(report.warnings);
			}
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"Terrain container import allocation failed.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::IoFailure, out_error,
				"Terrain container import filesystem operation failed.");
		}
	}
}
