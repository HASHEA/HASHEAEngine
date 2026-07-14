#include "Function/Render/RenderDevice.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("Texture2DArray upload descriptor validates every layer and mip")
{
	std::array<std::array<uint8_t, 64>, 8> pixels{};
	std::array<AshEngine::TextureSubresourceUploadDesc, 8> subresources{};
	for (uint32_t layer = 0u; layer < subresources.size(); ++layer)
	{
		subresources[layer] = {
			0u,
			layer,
			pixels[layer].data(),
			16u,
			64u
		};
	}

	AshEngine::Texture2DArrayUploadDesc desc{};
	desc.width = 4u;
	desc.height = 4u;
	desc.array_layer_count = 8u;
	desc.mip_level_count = 1u;
	desc.format = AshEngine::RenderTextureFormat::RGBA8_UNORM;
	desc.subresources = subresources.data();
	desc.subresource_count = static_cast<uint32_t>(subresources.size());

	std::string error{};
	CHECK(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
	CHECK(error.empty());

	SUBCASE("count must cover every layer and mip")
	{
		desc.subresource_count = 1u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error ==
			"subresource_count must equal mip_level_count * array_layer_count.");
	}

	SUBCASE("array layer count must be nonzero")
	{
		desc.array_layer_count = 0u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "array_layer_count must be greater than zero.");
	}

	SUBCASE("mip count must fit the texture dimensions")
	{
		desc.mip_level_count = 4u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "mip_level_count exceeds the maximum for the texture dimensions.");
	}

	SUBCASE("layer and mip coordinates must be in range")
	{
		subresources[7].array_layer = 8u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "array_layer is out of range.");

		subresources[7].array_layer = 7u;
		subresources[7].mip_level = 1u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "mip_level is out of range.");
	}

	SUBCASE("each layer and mip pair must be unique")
	{
		subresources[7].array_layer = 6u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "duplicate array_layer and mip_level subresource.");
	}

	SUBCASE("data and pitches must cover one complete subresource")
	{
		subresources[0].data = nullptr;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "subresource data must not be null.");

		subresources[0].data = pixels[0].data();
		subresources[0].row_pitch = 15u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "row_pitch is smaller than the tight row pitch.");

		subresources[0].row_pitch = 16u;
		subresources[0].slice_pitch = 63u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "slice_pitch is smaller than row_pitch * row_count.");
	}

	SUBCASE("storage without initial data is valid")
	{
		desc.subresources = nullptr;
		desc.subresource_count = 0u;
		CHECK(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error.empty());
	}

	SUBCASE("block-compressed rows use block height")
	{
		std::array<std::array<uint8_t, 8>, 8> blocks{};
		for (uint32_t layer = 0u; layer < subresources.size(); ++layer)
		{
			subresources[layer].data = blocks[layer].data();
			subresources[layer].row_pitch = 8u;
			subresources[layer].slice_pitch = 8u;
		}
		desc.format = AshEngine::RenderTextureFormat::BC1_RGBA_UNORM;
		CHECK(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error.empty());
	}

	SUBCASE("aggregate tightly packed upload must fit the RHI upload size")
	{
		uint8_t placeholder = 0u;
		AshEngine::TextureSubresourceUploadDesc huge_subresource{
			0u,
			0u,
			&placeholder,
			0u,
			0u
		};
		desc.width = UINT16_MAX;
		desc.height = UINT16_MAX;
		desc.array_layer_count = 1u;
		desc.format = AshEngine::RenderTextureFormat::RGBA32_SFLOAT;
		desc.subresources = &huge_subresource;
		desc.subresource_count = 1u;
		CHECK_FALSE(AshEngine::validate_texture_2d_array_upload_desc(desc, &error));
		CHECK(error == "tightly packed upload size exceeds the RHI limit.");
	}
}

TEST_CASE("Texture2DArray creation requests a native array view")
{
	std::ifstream input("project/src/engine/Function/Render/RenderDevice.cpp");
	REQUIRE(input.is_open());
	const std::string source{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>() };
	const size_t helper_begin = source.find("create_texture_2d_array_impl");
	const size_t helper_end = source.find("find_cube_subresource", helper_begin);
	REQUIRE(helper_begin != std::string::npos);
	REQUIRE(helper_end != std::string::npos);
	const std::string helper = source.substr(helper_begin, helper_end - helper_begin);
	CHECK(helper.find("texture_creation.type = RHI::Ash_Texture_2D_Array;") !=
		std::string::npos);
	CHECK(helper.find("texture_creation.type = RHI::Ash_Texture2D;") ==
		std::string::npos);
}
