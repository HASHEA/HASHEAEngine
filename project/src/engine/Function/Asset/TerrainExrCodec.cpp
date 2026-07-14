#include "Function/Asset/TerrainImport.h"

#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_ZLIB 0
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
	using ExrRowProvider =
		std::function<bool(uint32_t, std::vector<float>&, std::string*)>;

	namespace
	{
		constexpr uint64_t k_exr_export_memory_limit = 1024ull * 1024ull * 1024ull;

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

		auto set_tinyexr_error(
			TerrainImportResult result,
			std::string* out_error,
			const char* detail,
			const char* tinyexr_error) noexcept -> TerrainImportResult
		{
			if (out_error != nullptr)
			{
				try
				{
					*out_error = detail;
					if (tinyexr_error != nullptr && tinyexr_error[0] != '\0')
					{
						out_error->append(" ");
						out_error->append(tinyexr_error);
					}
				}
				catch (...)
				{
					out_error->clear();
				}
			}
			if (tinyexr_error != nullptr)
			{
				FreeEXRErrorMessage(tinyexr_error);
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

		auto miniz_compress_bound(uint64_t source_bytes, uint64_t& out_bound) -> bool
		{
			if (source_bytes > std::numeric_limits<mz_ulong>::max())
			{
				return false;
			}
			out_bound = static_cast<uint64_t>(
				mz_compressBound(static_cast<mz_ulong>(source_bytes)));
			return out_bound >= source_bytes;
		}

		auto estimate_exr_export_peak_bytes(
			uint32_t width,
			uint32_t height,
			TerrainExrPixelType output_pixel_type,
			uint64_t pixel_buffer_bytes,
			uint64_t& out_peak_bytes) -> bool
		{
			constexpr uint64_t scanlines_per_zip_block = 16u;
			constexpr uint64_t block_header_bytes = 8u;
			constexpr uint64_t file_header_bound = 4096u;
			constexpr uint64_t allocation_headroom = 32ull * 1024ull * 1024ull;
			const uint64_t output_sample_bytes =
				output_pixel_type == TerrainExrPixelType::Half ? 2u : 4u;
			const uint64_t full_block_count = height / scanlines_per_zip_block;
			const uint64_t tail_scanlines = height % scanlines_per_zip_block;
			const uint64_t block_count = full_block_count + (tail_scanlines != 0u ? 1u : 0u);

			uint64_t full_block_samples = 0u;
			uint64_t full_block_bytes = 0u;
			uint64_t full_block_bound = 0u;
			uint64_t full_block_record = 0u;
			uint64_t block_records_bound = 0u;
			if (!checked_multiply(width, scanlines_per_zip_block, full_block_samples) ||
				!checked_multiply(full_block_samples, output_sample_bytes, full_block_bytes) ||
				!miniz_compress_bound(full_block_bytes, full_block_bound) ||
				!checked_add(block_header_bytes, full_block_bound, full_block_record) ||
				!checked_multiply(full_block_count, full_block_record, block_records_bound))
			{
				return false;
			}

			uint64_t tail_block_bytes = 0u;
			uint64_t tail_block_bound = 0u;
			if (tail_scanlines != 0u)
			{
				uint64_t tail_block_samples = 0u;
				uint64_t tail_block_record = 0u;
				if (!checked_multiply(width, tail_scanlines, tail_block_samples) ||
					!checked_multiply(
						tail_block_samples, output_sample_bytes, tail_block_bytes) ||
					!miniz_compress_bound(tail_block_bytes, tail_block_bound) ||
					!checked_add(block_header_bytes, tail_block_bound, tail_block_record) ||
					!checked_add(
						block_records_bound, tail_block_record, block_records_bound))
				{
					return false;
				}
			}

			uint64_t offset_table_bytes = 0u;
			uint64_t encoded_bound = 0u;
			if (!checked_multiply(block_count, sizeof(uint64_t), offset_table_bytes) ||
				!checked_add(file_header_bound, offset_table_bytes, encoded_bound) ||
				!checked_add(encoded_bound, block_records_bound, encoded_bound))
			{
				return false;
			}

			const uint64_t max_block_bytes = std::max(full_block_bytes, tail_block_bytes);
			const uint64_t max_block_bound = std::max(full_block_bound, tail_block_bound);
			uint64_t final_assembly_peak = 0u;
			uint64_t compression_peak = 0u;
			uint64_t block_working_bytes = 0u;
			if (!checked_multiply(encoded_bound, 2u, final_assembly_peak) ||
				!checked_multiply(max_block_bytes, 2u, block_working_bytes) ||
				!checked_add(block_working_bytes, max_block_bound, block_working_bytes) ||
				!checked_add(encoded_bound, block_working_bytes, compression_peak))
			{
				return false;
			}

			uint64_t row_buffer_bytes = 0u;
			uint64_t peak_bytes = 0u;
			if (!checked_multiply(width, sizeof(float), row_buffer_bytes) ||
				!checked_add(
					pixel_buffer_bytes,
					std::max(final_assembly_peak, compression_peak),
					peak_bytes) ||
				!checked_add(peak_bytes, row_buffer_bytes, peak_bytes) ||
				!checked_add(peak_bytes, allocation_headroom, out_peak_bytes))
			{
				return false;
			}
			return true;
		}

		class ExrHeaderGuard final
		{
		public:
			ExrHeaderGuard()
			{
				InitEXRHeader(&value);
			}

			~ExrHeaderGuard()
			{
				FreeEXRHeader(&value);
			}

			ExrHeaderGuard(const ExrHeaderGuard&) = delete;
			auto operator=(const ExrHeaderGuard&) -> ExrHeaderGuard& = delete;

			EXRHeader value{};
		};

		class ExrImageGuard final
		{
		public:
			ExrImageGuard()
			{
				InitEXRImage(&value);
			}

			~ExrImageGuard()
			{
				FreeEXRImage(&value);
			}

			ExrImageGuard(const ExrImageGuard&) = delete;
			auto operator=(const ExrImageGuard&) -> ExrImageGuard& = delete;

			EXRImage value{};
		};

		class ExrMemoryGuard final
		{
		public:
			ExrMemoryGuard() = default;

			~ExrMemoryGuard()
			{
				std::free(value);
			}

			ExrMemoryGuard(const ExrMemoryGuard&) = delete;
			auto operator=(const ExrMemoryGuard&) -> ExrMemoryGuard& = delete;

			auto address() noexcept -> unsigned char**
			{
				return &value;
			}

			unsigned char* value = nullptr;
		};

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

	auto decode_exr_height_file(
		const TerrainHeightImportDesc& desc,
		std::vector<float>& out_heights,
		uint32_t& out_bits_per_sample,
		std::string* out_error) -> TerrainImportResult
	{
		try
		{
			if (desc.format != TerrainHeightFileFormat::Exr)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The requested height format is not EXR.");
			}
			if (desc.exr_channel.empty() || desc.exr_channel.size() >= 256u ||
				desc.exr_channel.find('\0') != std::string::npos)
			{
				return set_error(TerrainImportResult::InvalidArguments, out_error,
					"EXR height import requires a valid explicit channel name.");
			}
			const std::string source_path = desc.source_path.u8string();
			EXRVersion version{};
			if (ParseEXRVersionFromFile(&version, source_path.c_str()) != TINYEXR_SUCCESS)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Failed to parse the EXR version header.");
			}
			if (version.multipart || version.non_image || version.tiled)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"Terrain EXR import supports single-part scanline images only.");
			}

			ExrHeaderGuard header{};
			const char* tinyexr_error = nullptr;
			if (ParseEXRHeaderFromFile(
					&header.value, &version, source_path.c_str(), &tinyexr_error) !=
				TINYEXR_SUCCESS)
			{
				return set_tinyexr_error(
					TerrainImportResult::DecodeFailure, out_error,
					"Failed to parse the EXR height header.", tinyexr_error);
			}
			if (tinyexr_error != nullptr)
			{
				FreeEXRErrorMessage(tinyexr_error);
				tinyexr_error = nullptr;
			}
			if (header.value.num_channels <= 0 || header.value.channels == nullptr ||
				header.value.pixel_types == nullptr ||
				header.value.requested_pixel_types == nullptr)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"EXR height source has no decodable channels.");
			}
			int selected_channel = -1;
			for (int index = 0; index < header.value.num_channels; ++index)
			{
				if (desc.exr_channel == header.value.channels[index].name)
				{
					selected_channel = index;
					break;
				}
			}
			if (selected_channel < 0)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"The requested EXR height channel was not found.");
			}
			const int selected_pixel_type = header.value.pixel_types[selected_channel];
			if (selected_pixel_type != TINYEXR_PIXELTYPE_HALF &&
				selected_pixel_type != TINYEXR_PIXELTYPE_FLOAT)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"Terrain EXR height channels must use half or float samples.");
			}
			const int64_t width64 = static_cast<int64_t>(header.value.data_window.max_x) -
				header.value.data_window.min_x + 1;
			const int64_t height64 = static_cast<int64_t>(header.value.data_window.max_y) -
				header.value.data_window.min_y + 1;
			if (width64 <= 0 || height64 <= 0 ||
				width64 > std::numeric_limits<int>::max() ||
				height64 > std::numeric_limits<int>::max())
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"EXR height dimensions exceed supported limits.");
			}
			const uint32_t width = static_cast<uint32_t>(width64);
			const uint32_t height = static_cast<uint32_t>(height64);
			if (width != desc.source_width || height != desc.source_height)
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"EXR height dimensions do not match the declared source dimensions.");
			}
			uint64_t sample_count = 0u;
			uint64_t decoded_bytes = 0u;
			uint64_t output_bytes = 0u;
			uint64_t peak_bytes = 0u;
			if (!checked_multiply(width, height, sample_count) ||
				!checked_multiply(
					sample_count, static_cast<uint64_t>(header.value.num_channels),
					decoded_bytes) ||
				!checked_multiply(decoded_bytes, sizeof(float), decoded_bytes) ||
				!checked_multiply(sample_count, sizeof(float), output_bytes) ||
				!checked_add(decoded_bytes, output_bytes, peak_bytes) ||
				!checked_add(peak_bytes, 32ull * 1024ull * 1024ull, peak_bytes) ||
				peak_bytes > desc.peak_memory_limit_bytes ||
				sample_count > std::vector<float>{}.max_size())
			{
				return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
					"EXR height decode exceeds its peak memory limit.");
			}
			for (int index = 0; index < header.value.num_channels; ++index)
			{
				if (header.value.pixel_types[index] == TINYEXR_PIXELTYPE_HALF)
				{
					header.value.requested_pixel_types[index] = TINYEXR_PIXELTYPE_FLOAT;
				}
			}
			if (desc.cancellation.is_cancelled())
			{
				return set_error(TerrainImportResult::Cancelled, out_error,
					"EXR height import was cancelled.");
			}

			ExrImageGuard image{};
			if (LoadEXRImageFromFile(
					&image.value, &header.value, source_path.c_str(), &tinyexr_error) !=
				TINYEXR_SUCCESS)
			{
				return set_tinyexr_error(
					TerrainImportResult::DecodeFailure, out_error,
					"Failed to decode the EXR height image.", tinyexr_error);
			}
			if (tinyexr_error != nullptr)
			{
				FreeEXRErrorMessage(tinyexr_error);
				tinyexr_error = nullptr;
			}
			if (image.value.width != static_cast<int>(width) ||
				image.value.height != static_cast<int>(height) ||
				image.value.num_channels != header.value.num_channels ||
				image.value.images == nullptr || image.value.images[selected_channel] == nullptr)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Decoded EXR height image shape is invalid.");
			}
			const float* selected = reinterpret_cast<const float*>(
				image.value.images[selected_channel]);
			std::vector<float> heights(static_cast<size_t>(sample_count));
			for (uint32_t output_z = 0u; output_z < height; ++output_z)
			{
				if (desc.cancellation.is_cancelled())
				{
					return set_error(TerrainImportResult::Cancelled, out_error,
						"EXR height import was cancelled.");
				}
				const uint32_t source_z = desc.flip_z ? height - 1u - output_z : output_z;
				for (uint32_t output_x = 0u; output_x < width; ++output_x)
				{
					const uint32_t source_x = desc.flip_x ? width - 1u - output_x : output_x;
					const float value = selected[static_cast<size_t>(source_z) * width + source_x];
					if (!std::isfinite(value))
					{
						return set_error(TerrainImportResult::DecodeFailure, out_error,
							"EXR height channel contains a non-finite value.");
					}
					heights[static_cast<size_t>(output_z) * width + output_x] = value;
				}
			}
			out_bits_per_sample =
				selected_pixel_type == TINYEXR_PIXELTYPE_HALF ? 16u : 32u;
			out_heights.swap(heights);
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"EXR height decode allocation failed.");
		}
	}

	auto write_exr_height_file(
		const TerrainHeightExportDesc& desc,
		uint32_t width,
		uint32_t height,
		const ExrRowProvider& row_provider,
		std::string* out_error) -> TerrainImportResult
	{
		std::filesystem::path temporary = desc.destination_path;
		temporary += ".tmp";
		std::error_code error_code{};
		std::filesystem::remove(temporary, error_code);
		try
		{
			if (desc.format != TerrainHeightFileFormat::Exr)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The requested export format is not EXR.");
			}
			if (desc.exr_channel.empty() || desc.exr_channel.size() >= 256u ||
				desc.exr_channel.find('\0') != std::string::npos)
			{
				return set_error(TerrainImportResult::InvalidArguments, out_error,
					"EXR height export requires a valid explicit channel name.");
			}
			if (desc.exr_pixel_type != TerrainExrPixelType::Half &&
				desc.exr_pixel_type != TerrainExrPixelType::Float)
			{
				return set_error(TerrainImportResult::InvalidArguments, out_error,
					"EXR height export requires a valid half or float pixel type.");
			}
			uint64_t sample_count = 0u;
			uint64_t buffer_bytes = 0u;
			uint64_t peak_bytes = 0u;
			if (width == 0u || height == 0u ||
				!checked_multiply(width, height, sample_count) ||
				!checked_multiply(sample_count, sizeof(float), buffer_bytes) ||
				!estimate_exr_export_peak_bytes(
					width, height, desc.exr_pixel_type, buffer_bytes, peak_bytes) ||
				peak_bytes > k_exr_export_memory_limit ||
				sample_count > std::vector<float>{}.max_size())
			{
				return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
					"EXR height export exceeds its one GiB peak memory limit.");
			}
			if (desc.cancellation.is_cancelled())
			{
				return set_error(TerrainImportResult::Cancelled, out_error,
					"EXR height export was cancelled.");
			}
			const std::filesystem::path parent = desc.destination_path.parent_path();
			if (!parent.empty())
			{
				std::filesystem::create_directories(parent, error_code);
				if (error_code)
				{
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to create the EXR export directory.");
				}
			}

			std::vector<float> pixels(static_cast<size_t>(sample_count));
			std::vector<float> values{};
			for (uint32_t z = 0u; z < height; ++z)
			{
				if (desc.cancellation.is_cancelled())
				{
					return set_error(TerrainImportResult::Cancelled, out_error,
						"EXR height export was cancelled.");
				}
				values.clear();
				if (!row_provider(z, values, out_error) || values.size() != width)
				{
					return set_error(TerrainImportResult::EncodeFailure, out_error,
						"Failed to produce an EXR height export row.");
				}
				for (uint32_t x = 0u; x < width; ++x)
				{
					if (!std::isfinite(values[x]))
					{
						return set_error(TerrainImportResult::EncodeFailure, out_error,
							"EXR export source contains a non-finite value.");
					}
					pixels[static_cast<size_t>(z) * width + x] = values[x];
				}
			}
			if (desc.cancellation.is_cancelled())
			{
				return set_error(TerrainImportResult::Cancelled, out_error,
					"EXR height export was cancelled before encoding.");
			}

			EXRHeader header{};
			InitEXRHeader(&header);
			EXRChannelInfo channel{};
			std::memcpy(
				channel.name, desc.exr_channel.c_str(), desc.exr_channel.size() + 1u);
			const int output_pixel_type =
				desc.exr_pixel_type == TerrainExrPixelType::Half
				? TINYEXR_PIXELTYPE_HALF : TINYEXR_PIXELTYPE_FLOAT;
			channel.pixel_type = output_pixel_type;
			channel.x_sampling = 1;
			channel.y_sampling = 1;
			int pixel_type = TINYEXR_PIXELTYPE_FLOAT;
			int requested_pixel_type = output_pixel_type;
			header.num_channels = 1;
			header.channels = &channel;
			header.pixel_types = &pixel_type;
			header.requested_pixel_types = &requested_pixel_type;
			header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;
			header.long_name = desc.exr_channel.size() > 31u ? 1 : 0;

			EXRImage image{};
			InitEXRImage(&image);
			unsigned char* channel_bytes = reinterpret_cast<unsigned char*>(pixels.data());
			image.num_channels = 1;
			image.images = &channel_bytes;
			image.width = static_cast<int>(width);
			image.height = static_cast<int>(height);
			const char* tinyexr_error = nullptr;
			ExrMemoryGuard encoded{};
			const size_t encoded_size = SaveEXRImageToMemory(
				&image, &header, encoded.address(), &tinyexr_error);
			if (encoded_size == 0u || encoded.value == nullptr)
			{
				return set_tinyexr_error(
					TerrainImportResult::EncodeFailure, out_error,
					"Failed to encode the EXR height image in memory.", tinyexr_error);
			}
			if (tinyexr_error != nullptr)
			{
				FreeEXRErrorMessage(tinyexr_error);
				tinyexr_error = nullptr;
			}
			if (desc.cancellation.is_cancelled())
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::Cancelled, out_error,
					"EXR height export was cancelled before publication.");
			}
			if (encoded_size > static_cast<size_t>(
					std::numeric_limits<std::streamsize>::max()))
			{
				return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
					"Encoded EXR height output exceeds the stream write limit.");
			}
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output.is_open())
				{
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to open the temporary EXR height export.");
				}
				constexpr size_t write_chunk_size = 1024u * 1024u;
				size_t write_offset = 0u;
				bool cancelled_during_write = false;
				while (write_offset < encoded_size)
				{
					if (desc.cancellation.is_cancelled())
					{
						cancelled_during_write = true;
						break;
					}
					const size_t remaining = encoded_size - write_offset;
					const size_t chunk_size = std::min(write_chunk_size, remaining);
					output.write(
						reinterpret_cast<const char*>(encoded.value + write_offset),
						static_cast<std::streamsize>(chunk_size));
					if (!output)
					{
						break;
					}
					write_offset += chunk_size;
				}
				if (!output)
				{
					output.close();
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to write the temporary EXR height export.");
				}
				output.close();
				if (!output)
				{
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to close the temporary EXR height export.");
				}
				if (cancelled_during_write)
				{
					std::filesystem::remove(temporary, error_code);
					return set_error(TerrainImportResult::Cancelled, out_error,
						"EXR height export was cancelled during temporary write.");
				}
			}
			if (desc.cancellation.is_cancelled())
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::Cancelled, out_error,
					"EXR height export was cancelled after encoding.");
			}
			if (!flush_path(temporary))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to durably flush the EXR height export.");
			}
			if (desc.cancellation.is_cancelled())
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::Cancelled, out_error,
					"EXR height export was cancelled before final rename.");
			}
			if (!replace_file_atomically(temporary, desc.destination_path))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to atomically publish the EXR height export.");
			}
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"EXR height export allocation failed.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::IoFailure, out_error,
				"EXR height export filesystem operation failed.");
		}
	}
}
