#include "Function/Render/TextureCookedDecoder.h"

#include "Function/Render/RenderFormatUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <utility>

namespace AshEngine
{
	namespace
	{
		static auto make_error(std::string* out_error, std::string_view message) -> bool
		{
			if (out_error)
			{
				*out_error = std::string(message);
			}
			return false;
		}

		static auto clear_error(std::string* out_error) -> void
		{
			if (out_error)
			{
				out_error->clear();
			}
		}

		static auto to_lower_copy(std::string value) -> std::string
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		static auto calculate_full_mip_count(uint32_t width, uint32_t height) -> uint8_t
		{
			uint8_t mip_count = 1;
			while ((width > 1 || height > 1) && mip_count < 16)
			{
				width = std::max<uint32_t>(1u, width >> 1u);
				height = std::max<uint32_t>(1u, height >> 1u);
				++mip_count;
			}
			return mip_count;
		}

		static auto read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes, std::string* out_error) -> bool
		{
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input)
			{
				return make_error(out_error, "Failed to open texture file.");
			}

			const std::streamoff file_size = input.tellg();
			if (file_size <= 0)
			{
				return make_error(out_error, "Texture file is empty.");
			}

			input.seekg(0, std::ios::beg);
			out_bytes.resize(static_cast<size_t>(file_size));
			input.read(reinterpret_cast<char*>(out_bytes.data()), file_size);
			if (!input)
			{
				return make_error(out_error, "Failed to read texture file.");
			}
			return true;
		}

		static auto read_u32_le(const std::vector<uint8_t>& bytes, size_t offset) -> uint32_t
		{
			return static_cast<uint32_t>(bytes[offset]) |
				(static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
				(static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
				(static_cast<uint32_t>(bytes[offset + 3]) << 24u);
		}

		static auto read_u64_le(const std::vector<uint8_t>& bytes, size_t offset) -> uint64_t
		{
			uint64_t value = 0;
			for (uint32_t index = 0; index < 8u; ++index)
			{
				value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8u);
			}
			return value;
		}

		static constexpr auto make_fourcc(char a, char b, char c, char d) -> uint32_t
		{
			return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
				(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8u) |
				(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16u) |
				(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24u);
		}

		static auto dxgi_format_to_render_format(uint32_t dxgi_format, RenderTextureFormat& out_format, TextureColorSpace& out_color_space) -> bool
		{
			switch (dxgi_format)
			{
			case 71u: // DXGI_FORMAT_BC1_UNORM
				out_format = RenderTextureFormat::BC1_RGBA_UNORM;
				return true;
			case 72u: // DXGI_FORMAT_BC1_UNORM_SRGB
				out_format = RenderTextureFormat::BC1_RGBA_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 74u: // DXGI_FORMAT_BC2_UNORM
				out_format = RenderTextureFormat::BC2_UNORM;
				return true;
			case 75u: // DXGI_FORMAT_BC2_UNORM_SRGB
				out_format = RenderTextureFormat::BC2_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 77u: // DXGI_FORMAT_BC3_UNORM
				out_format = RenderTextureFormat::BC3_UNORM;
				return true;
			case 78u: // DXGI_FORMAT_BC3_UNORM_SRGB
				out_format = RenderTextureFormat::BC3_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 80u: // DXGI_FORMAT_BC4_UNORM
				out_format = RenderTextureFormat::BC4_UNORM;
				return true;
			case 81u: // DXGI_FORMAT_BC4_SNORM
				out_format = RenderTextureFormat::BC4_SNORM;
				return true;
			case 83u: // DXGI_FORMAT_BC5_UNORM
				out_format = RenderTextureFormat::BC5_UNORM;
				return true;
			case 84u: // DXGI_FORMAT_BC5_SNORM
				out_format = RenderTextureFormat::BC5_SNORM;
				return true;
			case 95u: // DXGI_FORMAT_BC6H_UF16
				out_format = RenderTextureFormat::BC6H_UFLOAT;
				return true;
			case 96u: // DXGI_FORMAT_BC6H_SF16
				out_format = RenderTextureFormat::BC6H_SFLOAT;
				return true;
			case 98u: // DXGI_FORMAT_BC7_UNORM
				out_format = RenderTextureFormat::BC7_UNORM;
				return true;
			case 99u: // DXGI_FORMAT_BC7_UNORM_SRGB
				out_format = RenderTextureFormat::BC7_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			default:
				return false;
			}
		}

		static auto vk_format_to_render_format(uint32_t vk_format, RenderTextureFormat& out_format, TextureColorSpace& out_color_space) -> bool
		{
			switch (vk_format)
			{
			case 131u: // VK_FORMAT_BC1_RGB_UNORM_BLOCK
				out_format = RenderTextureFormat::BC1_RGB_UNORM;
				return true;
			case 132u: // VK_FORMAT_BC1_RGB_SRGB_BLOCK
				out_format = RenderTextureFormat::BC1_RGB_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 133u: // VK_FORMAT_BC1_RGBA_UNORM_BLOCK
				out_format = RenderTextureFormat::BC1_RGBA_UNORM;
				return true;
			case 134u: // VK_FORMAT_BC1_RGBA_SRGB_BLOCK
				out_format = RenderTextureFormat::BC1_RGBA_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 135u: // VK_FORMAT_BC2_UNORM_BLOCK
				out_format = RenderTextureFormat::BC2_UNORM;
				return true;
			case 136u: // VK_FORMAT_BC2_SRGB_BLOCK
				out_format = RenderTextureFormat::BC2_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 137u: // VK_FORMAT_BC3_UNORM_BLOCK
				out_format = RenderTextureFormat::BC3_UNORM;
				return true;
			case 138u: // VK_FORMAT_BC3_SRGB_BLOCK
				out_format = RenderTextureFormat::BC3_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			case 139u: // VK_FORMAT_BC4_UNORM_BLOCK
				out_format = RenderTextureFormat::BC4_UNORM;
				return true;
			case 140u: // VK_FORMAT_BC4_SNORM_BLOCK
				out_format = RenderTextureFormat::BC4_SNORM;
				return true;
			case 141u: // VK_FORMAT_BC5_UNORM_BLOCK
				out_format = RenderTextureFormat::BC5_UNORM;
				return true;
			case 142u: // VK_FORMAT_BC5_SNORM_BLOCK
				out_format = RenderTextureFormat::BC5_SNORM;
				return true;
			case 143u: // VK_FORMAT_BC6H_UFLOAT_BLOCK
				out_format = RenderTextureFormat::BC6H_UFLOAT;
				return true;
			case 144u: // VK_FORMAT_BC6H_SFLOAT_BLOCK
				out_format = RenderTextureFormat::BC6H_SFLOAT;
				return true;
			case 145u: // VK_FORMAT_BC7_UNORM_BLOCK
				out_format = RenderTextureFormat::BC7_UNORM;
				return true;
			case 146u: // VK_FORMAT_BC7_SRGB_BLOCK
				out_format = RenderTextureFormat::BC7_SRGB_UNORM;
				out_color_space = TextureColorSpace::SRGB;
				return true;
			default:
				return false;
			}
		}

		static auto legacy_dds_fourcc_to_render_format(
			uint32_t fourcc,
			TextureColorSpace requested_color_space,
			RenderTextureFormat& out_format,
			TextureColorSpace& out_color_space) -> bool
		{
			out_color_space = requested_color_space;
			switch (fourcc)
			{
			case make_fourcc('D', 'X', 'T', '1'):
				out_format = requested_color_space == TextureColorSpace::SRGB ?
					RenderTextureFormat::BC1_RGBA_SRGB_UNORM :
					RenderTextureFormat::BC1_RGBA_UNORM;
				return true;
			case make_fourcc('D', 'X', 'T', '3'):
				out_format = requested_color_space == TextureColorSpace::SRGB ?
					RenderTextureFormat::BC2_SRGB_UNORM :
					RenderTextureFormat::BC2_UNORM;
				return true;
			case make_fourcc('D', 'X', 'T', '5'):
				out_format = requested_color_space == TextureColorSpace::SRGB ?
					RenderTextureFormat::BC3_SRGB_UNORM :
					RenderTextureFormat::BC3_UNORM;
				return true;
			case make_fourcc('A', 'T', 'I', '1'):
			case make_fourcc('B', 'C', '4', 'U'):
				out_format = RenderTextureFormat::BC4_UNORM;
				out_color_space = TextureColorSpace::Linear;
				return true;
			case make_fourcc('B', 'C', '4', 'S'):
				out_format = RenderTextureFormat::BC4_SNORM;
				out_color_space = TextureColorSpace::Linear;
				return true;
			case make_fourcc('A', 'T', 'I', '2'):
			case make_fourcc('B', 'C', '5', 'U'):
				out_format = RenderTextureFormat::BC5_UNORM;
				out_color_space = TextureColorSpace::Linear;
				return true;
			case make_fourcc('B', 'C', '5', 'S'):
				out_format = RenderTextureFormat::BC5_SNORM;
				out_color_space = TextureColorSpace::Linear;
				return true;
			default:
				return false;
			}
		}

		static auto append_tight_mip_payload(
			const std::vector<uint8_t>& file_bytes,
			uint64_t source_offset,
			uint64_t source_size,
			std::vector<uint8_t>& out_pixels,
			std::string* out_error) -> bool
		{
			if (source_offset > file_bytes.size() || source_size > file_bytes.size() - source_offset)
			{
				return make_error(out_error, "Cooked texture mip payload is out of file bounds.");
			}

			const size_t offset = static_cast<size_t>(source_offset);
			const size_t size = static_cast<size_t>(source_size);
			out_pixels.insert(out_pixels.end(), file_bytes.begin() + offset, file_bytes.begin() + offset + size);
			return true;
		}

		static auto decode_dds_texture_source_from_file(
			const std::filesystem::path& path,
			TextureColorSpace color_space,
			TextureSourceData& out_source,
			std::string* out_error) -> bool
		{
			std::vector<uint8_t> bytes{};
			if (!read_binary_file(path, bytes, out_error))
			{
				return false;
			}
			if (bytes.size() < 128u || read_u32_le(bytes, 0) != make_fourcc('D', 'D', 'S', ' '))
			{
				return make_error(out_error, "DDS header is invalid.");
			}
			if (read_u32_le(bytes, 4) != 124u || read_u32_le(bytes, 76) != 32u)
			{
				return make_error(out_error, "DDS header size is invalid.");
			}

			const uint32_t height = read_u32_le(bytes, 12);
			const uint32_t width = read_u32_le(bytes, 16);
			const uint32_t depth = read_u32_le(bytes, 24);
			const uint32_t requested_mips = read_u32_le(bytes, 28);
			const uint32_t pixel_format_flags = read_u32_le(bytes, 80);
			const uint32_t fourcc = read_u32_le(bytes, 84);
			const uint32_t caps2 = read_u32_le(bytes, 112);
			if (width == 0 || height == 0 || width > 65535 || height > 65535 || depth > 1)
			{
				return make_error(out_error, "DDS dimensions are invalid or exceed the 2D upload limit.");
			}
			if ((caps2 & 0x0000FE00u) != 0u)
			{
				return make_error(out_error, "DDS cube maps are not supported by the runtime texture path yet.");
			}
			if ((pixel_format_flags & 0x00000004u) == 0u)
			{
				return make_error(out_error, "Only FourCC/DX10 DDS cooked textures are supported.");
			}

			RenderTextureFormat format = RenderTextureFormat::Unknown;
			TextureColorSpace resolved_color_space = color_space;
			uint64_t payload_offset = 128u;
			if (fourcc == make_fourcc('D', 'X', '1', '0'))
			{
				if (bytes.size() < 148u)
				{
					return make_error(out_error, "DDS DX10 header is truncated.");
				}
				const uint32_t dxgi_format = read_u32_le(bytes, 128);
				const uint32_t resource_dimension = read_u32_le(bytes, 132);
				const uint32_t array_size = read_u32_le(bytes, 140);
				if (resource_dimension != 3u || array_size > 1u)
				{
					return make_error(out_error, "Only 2D non-array DDS textures are supported.");
				}
				if (!dxgi_format_to_render_format(dxgi_format, format, resolved_color_space))
				{
					return make_error(out_error, "DDS DX10 format is not supported.");
				}
				payload_offset += 20u;
			}
			else if (!legacy_dds_fourcc_to_render_format(fourcc, color_space, format, resolved_color_space))
			{
				return make_error(out_error, "DDS FourCC format is not supported.");
			}

			const uint32_t full_mip_count = calculate_full_mip_count(width, height);
			const uint8_t mip_count = static_cast<uint8_t>(std::max<uint32_t>(1u, std::min<uint32_t>(requested_mips == 0 ? 1u : requested_mips, full_mip_count)));
			std::vector<uint8_t> pixel_data{};
			uint64_t current_offset = payload_offset;
			for (uint8_t mip = 0; mip < mip_count; ++mip)
			{
				const uint32_t mip_width = std::max<uint32_t>(1u, width >> mip);
				const uint32_t mip_height = std::max<uint32_t>(1u, height >> mip);
				const uint64_t mip_size = calculate_render_texture_tight_mip_size(format, mip_width, mip_height);
				if (mip_size == 0)
				{
					return make_error(out_error, "DDS format block size is invalid.");
				}
				if (!append_tight_mip_payload(bytes, current_offset, mip_size, pixel_data, out_error))
				{
					return false;
				}
				current_offset += mip_size;
			}

			out_source = TextureSourceData{};
			out_source.width = width;
			out_source.height = height;
			out_source.format = format;
			out_source.color_space = resolved_color_space;
			out_source.row_pitch = calculate_render_texture_tight_row_pitch(format, width);
			out_source.mip_level_count = mip_count;
			out_source.is_hdr = false;
			out_source.pixel_data = std::move(pixel_data);
			clear_error(out_error);
			return true;
		}

		static auto decode_ktx2_texture_source_from_file(
			const std::filesystem::path& path,
			TextureColorSpace color_space,
			TextureSourceData& out_source,
			std::string* out_error) -> bool
		{
			std::vector<uint8_t> bytes{};
			if (!read_binary_file(path, bytes, out_error))
			{
				return false;
			}
			static constexpr std::array<uint8_t, 12> k_ktx2_identifier = {
				0xABu, 'K', 'T', 'X', ' ', '2', '0', 0xBBu, '\r', '\n', 0x1Au, '\n'
			};
			if (bytes.size() < 104u || !std::equal(k_ktx2_identifier.begin(), k_ktx2_identifier.end(), bytes.begin()))
			{
				return make_error(out_error, "KTX2 header is invalid.");
			}

			const uint32_t vk_format = read_u32_le(bytes, 12);
			const uint32_t pixel_width = read_u32_le(bytes, 20);
			const uint32_t pixel_height = read_u32_le(bytes, 24);
			const uint32_t pixel_depth = read_u32_le(bytes, 28);
			const uint32_t layer_count = read_u32_le(bytes, 32);
			const uint32_t face_count = read_u32_le(bytes, 36);
			uint32_t level_count = read_u32_le(bytes, 40);
			const uint32_t supercompression_scheme = read_u32_le(bytes, 44);
			if (pixel_width == 0 || pixel_height == 0 || pixel_width > 65535 || pixel_height > 65535 || pixel_depth > 0)
			{
				return make_error(out_error, "KTX2 dimensions are invalid or exceed the 2D upload limit.");
			}
			if (layer_count > 1u || face_count != 1u)
			{
				return make_error(out_error, "Only 2D non-array KTX2 textures are supported.");
			}
			if (supercompression_scheme != 0u)
			{
				return make_error(out_error, "Supercompressed KTX2 payloads require offline transcoding before runtime load.");
			}

			RenderTextureFormat format = RenderTextureFormat::Unknown;
			TextureColorSpace resolved_color_space = color_space;
			if (!vk_format_to_render_format(vk_format, format, resolved_color_space))
			{
				return make_error(out_error, "KTX2 VkFormat is not supported.");
			}

			const uint32_t full_mip_count = calculate_full_mip_count(pixel_width, pixel_height);
			level_count = level_count == 0u ? full_mip_count : std::min<uint32_t>(level_count, full_mip_count);
			if (level_count == 0u || level_count > 16u)
			{
				return make_error(out_error, "KTX2 mip count is invalid.");
			}
			const size_t level_index_offset = 80u;
			if (bytes.size() < level_index_offset + static_cast<size_t>(level_count) * 24u)
			{
				return make_error(out_error, "KTX2 level index is truncated.");
			}

			std::vector<uint8_t> pixel_data{};
			for (uint32_t mip = 0; mip < level_count; ++mip)
			{
				const size_t entry_offset = level_index_offset + static_cast<size_t>(mip) * 24u;
				const uint64_t byte_offset = read_u64_le(bytes, entry_offset);
				const uint64_t byte_length = read_u64_le(bytes, entry_offset + 8u);
				const uint32_t mip_width = std::max<uint32_t>(1u, pixel_width >> mip);
				const uint32_t mip_height = std::max<uint32_t>(1u, pixel_height >> mip);
				const uint64_t expected_size = calculate_render_texture_tight_mip_size(format, mip_width, mip_height);
				if (byte_length != expected_size || expected_size == 0)
				{
					return make_error(out_error, "KTX2 level payload size does not match the declared format.");
				}
				if (!append_tight_mip_payload(bytes, byte_offset, byte_length, pixel_data, out_error))
				{
					return false;
				}
			}

			out_source = TextureSourceData{};
			out_source.width = pixel_width;
			out_source.height = pixel_height;
			out_source.format = format;
			out_source.color_space = resolved_color_space;
			out_source.row_pitch = calculate_render_texture_tight_row_pitch(format, pixel_width);
			out_source.mip_level_count = static_cast<uint8_t>(level_count);
			out_source.is_hdr = false;
			out_source.pixel_data = std::move(pixel_data);
			clear_error(out_error);
			return true;
		}
	}

	bool is_cooked_texture_extension(std::string_view extension)
	{
		const std::string lowered = to_lower_copy(std::string(extension));
		return lowered == ".dds" || lowered == ".ktx2";
	}

	bool decode_cooked_texture_source_from_file(
		const std::filesystem::path& path,
		TextureColorSpace color_space,
		TextureSourceData& out_source,
		std::string* out_error)
	{
		const std::string extension = to_lower_copy(path.extension().string());
		if (extension == ".dds")
		{
			return decode_dds_texture_source_from_file(path, color_space, out_source, out_error);
		}
		if (extension == ".ktx2")
		{
			return decode_ktx2_texture_source_from_file(path, color_space, out_source, out_error);
		}
		return make_error(out_error, "Texture file extension is not a supported cooked texture format.");
	}
}
