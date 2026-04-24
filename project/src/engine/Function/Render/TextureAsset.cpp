#include "Function/Render/TextureAsset.h"

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
				lowered == ".hdr";
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
		if (extension == ".dds")
		{
			return make_error(out_error, "DDS is not supported by the V1 material texture path.");
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

		clear_error(out_error);
		return true;
	}
}
