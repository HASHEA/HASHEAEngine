#include "Function/Render/TextureAsset.h"

#include "Function/Render/TextureCookedDecoder.h"
#include <stb_image.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

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

		static auto is_supported_texture_extension(std::string_view extension) -> bool
		{
			const std::string lowered = to_lower_copy(std::string(extension));
			return lowered == ".png" ||
				lowered == ".jpg" ||
				lowered == ".jpeg" ||
				lowered == ".tga" ||
				lowered == ".bmp" ||
				lowered == ".hdr" ||
				is_cooked_texture_extension(lowered);
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

		static auto append_next_rgba8_mip(
			std::vector<uint8_t>& mip_chain,
			size_t previous_offset,
			uint32_t previous_width,
			uint32_t previous_height,
			uint32_t next_width,
			uint32_t next_height) -> void
		{
			const uint8_t* previous_pixels = mip_chain.data() + previous_offset;
			std::vector<uint8_t> next_pixels(static_cast<size_t>(next_width) * static_cast<size_t>(next_height) * 4u, 0u);

			for (uint32_t y = 0; y < next_height; ++y)
			{
				for (uint32_t x = 0; x < next_width; ++x)
				{
					const uint32_t src_x0 = std::min(previous_width - 1u, x * 2u);
					const uint32_t src_x1 = std::min(previous_width - 1u, src_x0 + 1u);
					const uint32_t src_y0 = std::min(previous_height - 1u, y * 2u);
					const uint32_t src_y1 = std::min(previous_height - 1u, src_y0 + 1u);
					uint32_t sum[4] = {};
					uint32_t sample_count = 0;

					for (uint32_t src_y = src_y0; src_y <= src_y1; ++src_y)
					{
						for (uint32_t src_x = src_x0; src_x <= src_x1; ++src_x)
						{
							const uint8_t* sample = previous_pixels +
								(static_cast<size_t>(src_y) * previous_width + src_x) * 4u;
							for (uint32_t channel = 0; channel < 4u; ++channel)
							{
								sum[channel] += sample[channel];
							}
							++sample_count;
						}
					}

					uint8_t* output = next_pixels.data() + (static_cast<size_t>(y) * next_width + x) * 4u;
					for (uint32_t channel = 0; channel < 4u; ++channel)
					{
						output[channel] = static_cast<uint8_t>((sum[channel] + sample_count / 2u) / sample_count);
					}
				}
			}

			mip_chain.insert(mip_chain.end(), next_pixels.begin(), next_pixels.end());
		}

		static auto build_rgba8_mip_chain(uint32_t width, uint32_t height, std::vector<uint8_t>& pixels) -> uint8_t
		{
			const uint8_t target_mips = calculate_full_mip_count(width, height);
			size_t previous_offset = 0;
			uint32_t previous_width = width;
			uint32_t previous_height = height;
			uint8_t generated_mips = 1;

			while (generated_mips < target_mips)
			{
				const size_t previous_size =
					static_cast<size_t>(previous_width) * static_cast<size_t>(previous_height) * 4u;
				const uint32_t next_width = std::max<uint32_t>(1u, previous_width >> 1u);
				const uint32_t next_height = std::max<uint32_t>(1u, previous_height >> 1u);
				append_next_rgba8_mip(pixels, previous_offset, previous_width, previous_height, next_width, next_height);
				previous_offset += previous_size;
				previous_width = next_width;
				previous_height = next_height;
				++generated_mips;
			}

			return generated_mips;
		}
	}

	bool TextureAsset::is_valid() const
	{
		return width > 0 && height > 0 && format != RenderTextureFormat::Unknown && resource != nullptr;
	}

	bool decode_texture_source_from_file(
		const std::filesystem::path& path,
		TextureColorSpace color_space,
		TextureSourceData& out_source,
		std::string* out_error)
	{
		const std::string extension = to_lower_copy(path.extension().string());
		if (path.empty())
		{
			return make_error(out_error, "Texture path is empty.");
		}
		if (is_cooked_texture_extension(extension))
		{
			return decode_cooked_texture_source_from_file(path, color_space, out_source, out_error);
		}
		if (!is_supported_texture_extension(extension))
		{
			return make_error(out_error, "Texture file extension is not supported.");
		}

		if (extension == ".hdr")
		{
			int width = 0;
			int height = 0;
			int channels = 0;
			float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
			if (!pixels)
			{
				return make_error(out_error, stbi_failure_reason() ? stbi_failure_reason() : "Failed to decode HDR texture.");
			}
			if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
			{
				stbi_image_free(pixels);
				return make_error(out_error, "Texture dimensions are invalid or exceed the 2D upload limit.");
			}

			const size_t byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u * sizeof(float);
			out_source = TextureSourceData{};
			out_source.width = static_cast<uint32_t>(width);
			out_source.height = static_cast<uint32_t>(height);
			out_source.format = RenderTextureFormat::RGBA32_SFLOAT;
			out_source.color_space = TextureColorSpace::Linear;
			out_source.row_pitch = static_cast<uint32_t>(width) * 4u * static_cast<uint32_t>(sizeof(float));
			out_source.mip_level_count = 1;
			out_source.is_hdr = true;
			out_source.pixel_data.resize(byte_count);
			std::memcpy(out_source.pixel_data.data(), pixels, byte_count);
			stbi_image_free(pixels);
			clear_error(out_error);
			return true;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
		if (!pixels)
		{
			return make_error(out_error, stbi_failure_reason() ? stbi_failure_reason() : "Failed to decode texture.");
		}
		if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
		{
			stbi_image_free(pixels);
			return make_error(out_error, "Texture dimensions are invalid or exceed the 2D upload limit.");
		}

		const size_t byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
		out_source = TextureSourceData{};
		out_source.width = static_cast<uint32_t>(width);
		out_source.height = static_cast<uint32_t>(height);
		out_source.format = color_space == TextureColorSpace::SRGB ? RenderTextureFormat::RGBA8_SRGB : RenderTextureFormat::RGBA8_UNORM;
		out_source.color_space = color_space;
		out_source.row_pitch = static_cast<uint32_t>(width) * 4u;
		out_source.is_hdr = false;
		out_source.pixel_data.resize(byte_count);
		std::memcpy(out_source.pixel_data.data(), pixels, byte_count);
		stbi_image_free(pixels);
		out_source.mip_level_count = build_rgba8_mip_chain(out_source.width, out_source.height, out_source.pixel_data);

		clear_error(out_error);
		return true;
	}
}
