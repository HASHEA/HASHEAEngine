#include "Function/Asset/TerrainImport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <new>
#include <string>
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

		auto flush_path(const std::filesystem::path& path) -> bool
		{
#if defined(_WIN32)
			const HANDLE file = CreateFileW(
				path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			const bool result = FlushFileBuffers(file) != FALSE;
			CloseHandle(file);
			return result;
#else
			(void)path;
			return true;
#endif
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
	}

	auto decode_raw_height_file(
		const TerrainHeightImportDesc& desc,
		std::vector<float>& out_heights,
		std::string* out_error) -> TerrainImportResult
	{
		try
		{
			const uint32_t bytes_per_sample =
				desc.format == TerrainHeightFileFormat::RawR16 ? 2u :
				desc.format == TerrainHeightFileFormat::RawR32F ? 4u : 0u;
			if (bytes_per_sample == 0u)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The requested height format is not a RAW format.");
			}
			uint64_t sample_count = 0u;
			uint64_t expected_size = 0u;
			uint64_t row_size = 0u;
			if (desc.source_width == 0u || desc.source_height == 0u ||
				!checked_multiply(desc.source_width, desc.source_height, sample_count) ||
				!checked_multiply(sample_count, bytes_per_sample, expected_size) ||
				!checked_multiply(desc.source_width, bytes_per_sample, row_size) ||
				sample_count > std::vector<float>{}.max_size() ||
				row_size > std::vector<uint8_t>{}.max_size() ||
				row_size > static_cast<uint64_t>(
					std::numeric_limits<std::streamsize>::max()))
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"RAW source dimensions exceed supported limits.");
			}
			std::error_code error_code{};
			const uint64_t actual_size = std::filesystem::file_size(desc.source_path, error_code);
			if (error_code)
			{
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to inspect the RAW height source.");
			}
			if (actual_size != expected_size)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"RAW height source byte size does not match its dimensions.");
			}
			std::ifstream input(desc.source_path, std::ios::binary);
			if (!input.is_open())
			{
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to open the RAW height source.");
			}
			std::vector<float> heights(static_cast<size_t>(sample_count));
			std::vector<uint8_t> row(static_cast<size_t>(row_size));
			for (uint32_t output_z = 0u; output_z < desc.source_height; ++output_z)
			{
				if (desc.cancellation.is_cancelled())
				{
					return set_error(TerrainImportResult::Cancelled, out_error,
						"RAW height import was cancelled.");
				}
				const uint32_t file_z = desc.flip_z
					? desc.source_height - 1u - output_z : output_z;
				const uint64_t file_offset = static_cast<uint64_t>(file_z) * row_size;
				if (file_offset >
						static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
				{
					return set_error(TerrainImportResult::InvalidDimensions, out_error,
						"RAW height row offset exceeds stream limits.");
				}
				input.seekg(static_cast<std::streamoff>(file_offset));
				if (!input.read(
					reinterpret_cast<char*>(row.data()),
					static_cast<std::streamsize>(row.size())))
				{
					return set_error(TerrainImportResult::DecodeFailure, out_error,
						"Failed to read a complete RAW height row.");
				}
				for (uint32_t output_x = 0u; output_x < desc.source_width; ++output_x)
				{
					const uint32_t file_x = desc.flip_x
						? desc.source_width - 1u - output_x : output_x;
					const size_t byte_offset = static_cast<size_t>(file_x) * bytes_per_sample;
					float world_height = 0.0f;
					if (desc.format == TerrainHeightFileFormat::RawR16)
					{
						const uint16_t encoded = desc.byte_order == TerrainByteOrder::LittleEndian
							? static_cast<uint16_t>(row[byte_offset]) |
								(static_cast<uint16_t>(row[byte_offset + 1u]) << 8u)
							: static_cast<uint16_t>(row[byte_offset + 1u]) |
								(static_cast<uint16_t>(row[byte_offset]) << 8u);
						world_height = decode_terrain_height_r16(encoded, desc.height_mapping);
					}
					else
					{
						std::array<uint8_t, 4> bytes{};
						std::copy_n(row.data() + byte_offset, 4u, bytes.data());
						if (desc.byte_order == TerrainByteOrder::BigEndian)
						{
							std::reverse(bytes.begin(), bytes.end());
						}
						std::memcpy(&world_height, bytes.data(), sizeof(world_height));
						if (!std::isfinite(world_height))
						{
							return set_error(TerrainImportResult::DecodeFailure, out_error,
								"RAW R32F source contains a non-finite height.");
						}
					}
					heights[static_cast<size_t>(output_z) * desc.source_width + output_x] =
						world_height;
				}
			}
			out_heights.swap(heights);
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"RAW height decode allocation failed.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			return set_error(TerrainImportResult::IoFailure, out_error,
				"RAW height decode filesystem operation failed.");
		}
	}

	auto write_raw_height_file(
		const TerrainHeightExportDesc& desc,
		uint32_t width,
		uint32_t height,
		const TerrainHeightMapping& mapping,
		const RawRowProvider& row_provider,
		std::string* out_error) -> TerrainImportResult
	{
		std::filesystem::path temporary = desc.destination_path;
		temporary += ".tmp";
		std::error_code error_code{};
		std::filesystem::remove(temporary, error_code);
		try
		{
			const uint32_t bytes_per_sample =
				desc.format == TerrainHeightFileFormat::RawR16 ? 2u :
				desc.format == TerrainHeightFileFormat::RawR32F ? 4u : 0u;
			if (bytes_per_sample == 0u)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The requested export format is not a RAW format.");
			}
			uint64_t row_size = 0u;
			if (width == 0u || height == 0u ||
				!checked_multiply(width, bytes_per_sample, row_size) ||
				row_size > std::vector<uint8_t>{}.max_size() ||
				row_size > static_cast<uint64_t>(
					std::numeric_limits<std::streamsize>::max()))
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"RAW export dimensions exceed supported limits.");
			}
			const std::filesystem::path parent = desc.destination_path.parent_path();
			if (!parent.empty())
			{
				std::filesystem::create_directories(parent, error_code);
				if (error_code)
				{
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to create the RAW export directory.");
				}
			}
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output.is_open())
			{
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to open the RAW export temporary file.");
			}
			std::vector<float> values{};
			std::vector<uint8_t> row(static_cast<size_t>(row_size));
			for (uint32_t z = 0u; z < height; ++z)
			{
				if (desc.cancellation.is_cancelled())
				{
					output.close();
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::Cancelled, out_error,
						"RAW height export was cancelled.");
				}
				values.clear();
				if (!row_provider(z, values, out_error) || values.size() != width)
				{
					output.close();
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::EncodeFailure, out_error,
						"Failed to produce a RAW height export row.");
				}
				for (uint32_t x = 0u; x < width; ++x)
				{
					if (!std::isfinite(values[x]))
					{
						output.close();
						std::filesystem::remove(temporary, error_code);
						return set_error(TerrainImportResult::EncodeFailure, out_error,
							"RAW export source contains a non-finite value.");
					}
					const size_t byte_offset = static_cast<size_t>(x) * bytes_per_sample;
					if (desc.format == TerrainHeightFileFormat::RawR16)
					{
						const uint16_t encoded = encode_terrain_height_r16(values[x], mapping);
						row[byte_offset] = static_cast<uint8_t>(encoded);
						row[byte_offset + 1u] = static_cast<uint8_t>(encoded >> 8u);
						if (desc.byte_order == TerrainByteOrder::BigEndian)
						{
							std::swap(row[byte_offset], row[byte_offset + 1u]);
						}
					}
					else
					{
						std::array<uint8_t, 4> bytes{};
						std::memcpy(bytes.data(), &values[x], sizeof(float));
						if (desc.byte_order == TerrainByteOrder::BigEndian)
						{
							std::reverse(bytes.begin(), bytes.end());
						}
						std::copy(bytes.begin(), bytes.end(), row.begin() + byte_offset);
					}
				}
				if (!output.write(
					reinterpret_cast<const char*>(row.data()),
					static_cast<std::streamsize>(row.size())))
				{
					output.close();
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to write a RAW height row.");
				}
			}
			if (desc.cancellation.is_cancelled())
			{
				output.close();
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::Cancelled, out_error,
					"RAW height export was cancelled.");
			}
			output.flush();
			const bool wrote = output.good();
			output.close();
			if (!wrote || !flush_path(temporary))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to durably flush the RAW height export.");
			}
			if (!replace_file_atomically(temporary, desc.destination_path))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to atomically publish the RAW height export.");
			}
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"RAW height export allocation failed.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::IoFailure, out_error,
				"RAW height export filesystem operation failed.");
		}
	}
}
