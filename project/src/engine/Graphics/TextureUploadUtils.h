#pragma once

#include "Graphics/Texture.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace RHI
{
	struct TextureUploadFormatInfo
	{
		uint32_t bytesPerBlock = 0;
		uint32_t widthPerBlock = 0;
		uint32_t heightPerBlock = 0;
	};

	struct TextureUploadSubresource
	{
		uint32_t subresourceIndex = 0;
		uint32_t mipLevel = 0;
		uint32_t arrayLayer = 0;
		uint32_t width = 1;
		uint32_t height = 1;
		uint32_t depth = 1;
		uint32_t rowCount = 1;
		uint32_t rowBytes = 0;
		uint32_t sliceBytes = 0;
		uint64_t sourceOffset = 0;
		uint64_t sourceSize = 0;
	};

	inline auto build_tightly_packed_texture_upload_layout(
		const TextureCreation& creation,
		const TextureUploadFormatInfo& formatInfo,
		std::vector<TextureUploadSubresource>& outSubresources,
		uint64_t& outTotalBytes) -> bool
	{
		if (formatInfo.bytesPerBlock == 0 || formatInfo.widthPerBlock == 0 || formatInfo.heightPerBlock == 0)
		{
			return false;
		}
		if (creation.mip_level_count == 0 || creation.width == 0 || creation.height == 0 || creation.depth == 0)
		{
			return false;
		}

		const uint32_t arrayLayerCount = creation.type == Ash_Texture3D ?
			1u :
			std::max<uint32_t>(1u, static_cast<uint32_t>(creation.array_layer_count));

		outSubresources.clear();
		outSubresources.reserve(static_cast<size_t>(arrayLayerCount) * static_cast<size_t>(creation.mip_level_count));
		outTotalBytes = 0;

		for (uint32_t arrayLayer = 0; arrayLayer < arrayLayerCount; ++arrayLayer)
		{
			for (uint32_t mipLevel = 0; mipLevel < creation.mip_level_count; ++mipLevel)
			{
				const uint32_t width = std::max<uint32_t>(1u, static_cast<uint32_t>(creation.width) >> mipLevel);
				const uint32_t height = (creation.type == Ash_Texture1D || creation.type == Ash_Texture_1D_Array) ?
					1u :
					std::max<uint32_t>(1u, static_cast<uint32_t>(creation.height) >> mipLevel);
				const uint32_t depth = creation.type == Ash_Texture3D ?
					std::max<uint32_t>(1u, static_cast<uint32_t>(creation.depth) >> mipLevel) :
					1u;

				const uint32_t blockCountX = std::max<uint32_t>(1u, (width + formatInfo.widthPerBlock - 1u) / formatInfo.widthPerBlock);
				const uint32_t blockCountY = std::max<uint32_t>(1u, (height + formatInfo.heightPerBlock - 1u) / formatInfo.heightPerBlock);
				const uint32_t rowBytes = blockCountX * formatInfo.bytesPerBlock;
				const uint32_t sliceBytes = rowBytes * blockCountY;
				const uint64_t sourceSize = static_cast<uint64_t>(sliceBytes) * static_cast<uint64_t>(depth);

				TextureUploadSubresource subresource{};
				subresource.subresourceIndex = mipLevel + arrayLayer * creation.mip_level_count;
				subresource.mipLevel = mipLevel;
				subresource.arrayLayer = arrayLayer;
				subresource.width = width;
				subresource.height = height;
				subresource.depth = depth;
				subresource.rowCount = blockCountY;
				subresource.rowBytes = rowBytes;
				subresource.sliceBytes = sliceBytes;
				subresource.sourceOffset = outTotalBytes;
				subresource.sourceSize = sourceSize;

				outSubresources.push_back(subresource);
				outTotalBytes += sourceSize;
			}
		}

		return true;
	}
}
