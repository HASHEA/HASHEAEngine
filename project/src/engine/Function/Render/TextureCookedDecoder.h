#pragma once

#include "Function/Render/TextureAsset.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace AshEngine
{
	bool is_cooked_texture_extension(std::string_view extension);
	bool decode_cooked_texture_source_from_file(
		const std::filesystem::path& path,
		TextureColorSpace color_space,
		TextureSourceData& out_source,
		std::string* out_error = nullptr);
}
