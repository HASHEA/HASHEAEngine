#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class TextureColorSpace : uint8_t
	{
		Linear = 0,
		SRGB
	};

	enum class TextureAssetState : uint8_t
	{
		Loading = 0,
		Ready,
		Failed
	};

	struct ASH_API TextureSourceData
	{
		uint32_t width = 0;
		uint32_t height = 0;
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		TextureColorSpace color_space = TextureColorSpace::Linear;
		uint32_t row_pitch = 0;
		uint8_t mip_level_count = 1;
		bool is_hdr = false;
		std::vector<uint8_t> pixel_data{};
	};

	class ASH_API TextureAsset
	{
	public:
		std::string asset_path{};
		uint32_t width = 0;
		uint32_t height = 0;
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		TextureColorSpace color_space = TextureColorSpace::Linear;
		TextureAssetState state = TextureAssetState::Ready;
		std::string last_error{};
		std::shared_ptr<RenderTarget> resource = nullptr;
		uint64_t change_version = 1;

	public:
		bool is_valid() const;
	};

	ASH_API bool decode_texture_source_from_file(
		const std::filesystem::path& path,
		TextureColorSpace color_space,
		TextureSourceData& out_source,
		std::string* out_error = nullptr);
}
