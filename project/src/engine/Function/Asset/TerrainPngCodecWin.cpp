#include "Function/Asset/TerrainImport.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace AshEngine::TerrainImportDetail
{
	using PngRowProvider =
		std::function<bool(uint32_t, std::vector<float>&, std::string*)>;

	namespace
	{
		using Microsoft::WRL::ComPtr;

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

		class ComMtaScope final
		{
		public:
			ComMtaScope()
				: m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
				m_initialized(SUCCEEDED(m_result))
			{
			}

			~ComMtaScope()
			{
				if (m_initialized)
				{
					CoUninitialize();
				}
			}

			ComMtaScope(const ComMtaScope&) = delete;
			auto operator=(const ComMtaScope&) -> ComMtaScope& = delete;

			auto result() const -> HRESULT
			{
				return m_result;
			}

		private:
			HRESULT m_result = E_FAIL;
			bool m_initialized = false;
		};

		auto create_factory(ComPtr<IWICImagingFactory>& out_factory) -> HRESULT
		{
			return CoCreateInstance(
				CLSID_WICImagingFactory,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(out_factory.GetAddressOf()));
		}

		auto flush_path(const std::filesystem::path& path) -> bool
		{
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
		}

		auto replace_file_atomically(
			const std::filesystem::path& temporary,
			const std::filesystem::path& destination) -> bool
		{
			return (std::filesystem::exists(destination)
				? ReplaceFileW(
					destination.c_str(), temporary.c_str(), nullptr,
					REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
				: MoveFileExW(
					temporary.c_str(), destination.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) != FALSE;
		}
	}

	auto decode_png_height_file(
		const TerrainHeightImportDesc& desc,
		std::vector<float>& out_heights,
		uint32_t& out_bits_per_sample,
		std::string* out_error) -> TerrainImportResult
	{
		try
		{
			if (desc.format != TerrainHeightFileFormat::Png)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The requested height format is not PNG.");
			}
			ComMtaScope apartment{};
			if (apartment.result() == RPC_E_CHANGED_MODE)
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"PNG height decoding requires a COM MTA worker.");
			}
			if (FAILED(apartment.result()))
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Failed to initialize COM for PNG height decoding.");
			}

			ComPtr<IWICImagingFactory> factory{};
			if (FAILED(create_factory(factory)))
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Failed to create the WIC imaging factory.");
			}
			ComPtr<IWICBitmapDecoder> decoder{};
			if (FAILED(factory->CreateDecoderFromFilename(
					desc.source_path.c_str(), nullptr, GENERIC_READ,
					WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())))
			{
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to open the PNG height source.");
			}
			GUID container_format{};
			if (FAILED(decoder->GetContainerFormat(&container_format)))
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Failed to inspect the PNG container format.");
			}
			if (!IsEqualGUID(container_format, GUID_ContainerFormatPng))
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The height source container is not PNG.");
			}
			ComPtr<IWICBitmapFrameDecode> frame{};
			if (FAILED(decoder->GetFrame(0u, frame.GetAddressOf())))
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Failed to decode the PNG height frame.");
			}

			UINT width = 0u;
			UINT height = 0u;
			WICPixelFormatGUID pixel_format{};
			if (FAILED(frame->GetSize(&width, &height)) ||
				FAILED(frame->GetPixelFormat(&pixel_format)))
			{
				return set_error(TerrainImportResult::DecodeFailure, out_error,
					"Failed to inspect the PNG height frame.");
			}
			if (width != desc.source_width || height != desc.source_height)
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"PNG height dimensions do not match the declared source dimensions.");
			}
			const bool gray8 = IsEqualGUID(pixel_format, GUID_WICPixelFormat8bppGray);
			const bool gray16 = IsEqualGUID(pixel_format, GUID_WICPixelFormat16bppGray);
			if (!gray8 && !gray16)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"PNG height sources must use exact 8-bit or 16-bit grayscale pixels.");
			}

			const uint32_t bytes_per_sample = gray8 ? 1u : 2u;
			const uint64_t sample_count = static_cast<uint64_t>(width) * height;
			const uint64_t row_bytes = static_cast<uint64_t>(width) * bytes_per_sample;
			if (width > static_cast<UINT>(std::numeric_limits<INT>::max()) ||
				height > static_cast<UINT>(std::numeric_limits<INT>::max()) ||
				sample_count > std::vector<float>{}.max_size() ||
				row_bytes > std::vector<uint8_t>{}.max_size() ||
				row_bytes > std::numeric_limits<UINT>::max())
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"PNG height dimensions exceed supported limits.");
			}
			std::vector<float> heights(static_cast<size_t>(sample_count));
			std::vector<uint8_t> row(static_cast<size_t>(row_bytes));
			for (uint32_t output_z = 0u; output_z < height; ++output_z)
			{
				if (desc.cancellation.is_cancelled())
				{
					return set_error(TerrainImportResult::Cancelled, out_error,
						"PNG height import was cancelled.");
				}
				const uint32_t source_z = desc.flip_z ? height - 1u - output_z : output_z;
				const WICRect rect = {
					0, static_cast<INT>(source_z), static_cast<INT>(width), 1
				};
				if (FAILED(frame->CopyPixels(
						&rect, static_cast<UINT>(row_bytes),
						static_cast<UINT>(row_bytes), row.data())))
				{
					return set_error(TerrainImportResult::DecodeFailure, out_error,
						"Failed to read a complete PNG height row.");
				}
				for (uint32_t output_x = 0u; output_x < width; ++output_x)
				{
					const uint32_t source_x = desc.flip_x ? width - 1u - output_x : output_x;
					float normalized = 0.0f;
					if (gray8)
					{
						normalized = static_cast<float>(row[source_x]) / 255.0f;
					}
					else
					{
						const size_t offset = static_cast<size_t>(source_x) * 2u;
						const uint16_t encoded = static_cast<uint16_t>(row[offset]) |
							(static_cast<uint16_t>(row[offset + 1u]) << 8u);
						normalized = static_cast<float>(encoded) / 65535.0f;
					}
					heights[static_cast<size_t>(output_z) * width + output_x] =
						desc.height_mapping.height_offset +
						desc.height_mapping.height_range * normalized;
				}
			}
			out_bits_per_sample = gray8 ? 8u : 16u;
			out_heights.swap(heights);
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"PNG height decode allocation failed.");
		}
	}

	auto write_png_height_file(
		const TerrainHeightExportDesc& desc,
		uint32_t width,
		uint32_t height,
		const TerrainHeightMapping& mapping,
		const PngRowProvider& row_provider,
		std::string* out_error) -> TerrainImportResult
	{
		std::filesystem::path temporary = desc.destination_path;
		temporary += ".tmp";
		std::error_code error_code{};
		std::filesystem::remove(temporary, error_code);
		try
		{
			if (desc.format != TerrainHeightFileFormat::Png)
			{
				return set_error(TerrainImportResult::UnsupportedFormat, out_error,
					"The requested export format is not PNG.");
			}
			if (width == 0u || height == 0u ||
				width > std::numeric_limits<UINT>::max() / 2u)
			{
				return set_error(TerrainImportResult::InvalidDimensions, out_error,
					"PNG export dimensions exceed supported limits.");
			}
			const std::filesystem::path parent = desc.destination_path.parent_path();
			if (!parent.empty())
			{
				std::filesystem::create_directories(parent, error_code);
				if (error_code)
				{
					return set_error(TerrainImportResult::IoFailure, out_error,
						"Failed to create the PNG export directory.");
				}
			}

			ComMtaScope apartment{};
			if (apartment.result() == RPC_E_CHANGED_MODE)
			{
				return set_error(TerrainImportResult::EncodeFailure, out_error,
					"PNG height encoding requires a COM MTA worker.");
			}
			if (FAILED(apartment.result()))
			{
				return set_error(TerrainImportResult::EncodeFailure, out_error,
					"Failed to initialize COM for PNG height encoding.");
			}

			ComPtr<IWICImagingFactory> factory{};
			ComPtr<IWICStream> stream{};
			ComPtr<IWICBitmapEncoder> encoder{};
			ComPtr<IWICBitmapFrameEncode> frame{};
			ComPtr<IPropertyBag2> properties{};
			const auto release_wic = [&]() noexcept
				{
					properties.Reset();
					frame.Reset();
					encoder.Reset();
					stream.Reset();
					factory.Reset();
				};
			const auto discard_temporary = [&]() noexcept
				{
					release_wic();
					std::filesystem::remove(temporary, error_code);
				};
			if (FAILED(create_factory(factory)) ||
				FAILED(factory->CreateStream(stream.GetAddressOf())) ||
				FAILED(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE)) ||
				FAILED(factory->CreateEncoder(
					GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
				FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
				FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf())) ||
				FAILED(frame->Initialize(properties.Get())) ||
				FAILED(frame->SetSize(width, height)))
			{
				discard_temporary();
				return set_error(TerrainImportResult::EncodeFailure, out_error,
					"Failed to initialize the WIC PNG height encoder.");
			}
			WICPixelFormatGUID pixel_format = GUID_WICPixelFormat16bppGray;
			if (FAILED(frame->SetPixelFormat(&pixel_format)) ||
				!IsEqualGUID(pixel_format, GUID_WICPixelFormat16bppGray))
			{
				discard_temporary();
				return set_error(TerrainImportResult::EncodeFailure, out_error,
					"WIC refused exact 16-bit grayscale PNG output.");
			}

			std::vector<float> values{};
			std::vector<uint16_t> row(width);
			const UINT stride = width * 2u;
			for (uint32_t z = 0u; z < height; ++z)
			{
				if (desc.cancellation.is_cancelled())
				{
					discard_temporary();
					return set_error(TerrainImportResult::Cancelled, out_error,
						"PNG height export was cancelled.");
				}
				values.clear();
				if (!row_provider(z, values, out_error) || values.size() != width)
				{
					discard_temporary();
					return set_error(TerrainImportResult::EncodeFailure, out_error,
						"Failed to produce a PNG height export row.");
				}
				for (uint32_t x = 0u; x < width; ++x)
				{
					if (!std::isfinite(values[x]))
					{
						discard_temporary();
						return set_error(TerrainImportResult::EncodeFailure, out_error,
							"PNG export source contains a non-finite value.");
					}
					row[x] = encode_terrain_height_r16(values[x], mapping);
				}
				if (FAILED(frame->WritePixels(
						1u, stride, stride, reinterpret_cast<BYTE*>(row.data()))))
				{
					discard_temporary();
					return set_error(TerrainImportResult::EncodeFailure, out_error,
						"Failed to write a PNG height row.");
				}
			}
			if (desc.cancellation.is_cancelled())
			{
				discard_temporary();
				return set_error(TerrainImportResult::Cancelled, out_error,
					"PNG height export was cancelled.");
			}
			if (FAILED(frame->Commit()) || FAILED(encoder->Commit()) ||
				FAILED(stream->Commit(STGC_DEFAULT)))
			{
				discard_temporary();
				return set_error(TerrainImportResult::EncodeFailure, out_error,
					"Failed to commit the PNG height stream.");
			}
			release_wic();
			if (!flush_path(temporary))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to durably flush the PNG height export.");
			}
			if (!replace_file_atomically(temporary, desc.destination_path))
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainImportResult::IoFailure, out_error,
					"Failed to atomically publish the PNG height export.");
			}
			return TerrainImportResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::MemoryLimitExceeded, out_error,
				"PNG height export allocation failed.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainImportResult::IoFailure, out_error,
				"PNG height export filesystem operation failed.");
		}
	}
}
