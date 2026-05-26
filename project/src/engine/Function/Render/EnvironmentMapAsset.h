#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include <cstdint>
#include <filesystem>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace AshEngine
{
	static constexpr char k_ashibl_magic[8] = { 'A', 'S', 'H', 'I', 'B', 'L', '\0', '\0' };
	static constexpr uint32_t k_ashibl_version = 1;
	static constexpr uint32_t k_ashibl_face_count = 6;

	enum class AshIBLPayloadKind : uint32_t
	{
		RadianceCubemap = 1,
		IrradianceCubemap = 2,
		PrefilteredSpecularCubemap = 3,
		BRDFLut2D = 4,
		PreviewThumbnail2D = 5
	};

	enum class AshIBLCompression : uint32_t
	{
		None = 0
	};

	enum class EnvironmentMapAssetState : uint8_t
	{
		Loading = 0,
		Ready,
		Failed
	};

	struct ASH_API TextureSubresourcePayload
	{
		uint32_t mip_level = 0;
		uint32_t array_layer = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t row_pitch = 0;
		std::vector<uint8_t> pixel_data{};
	};

	struct ASH_API TextureCubePayload
	{
		RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mip_count = 1;
		std::vector<TextureSubresourcePayload> subresources{};
	};

	struct ASH_API Texture2DPayload
	{
		RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t row_pitch = 0;
		std::vector<uint8_t> pixel_data{};
	};

	struct ASH_API EnvironmentMapBuildDesc
	{
		std::string source_texture_path{};
		uint32_t radiance_size = 1024;
		uint32_t irradiance_size = 64;
		uint32_t prefilter_size = 256;
		uint32_t prefilter_mip_count = 8;
		uint32_t brdf_lut_size = 256;
		RenderTextureFormat hdr_format = RenderTextureFormat::RGBA16_SFLOAT;
		uint32_t sample_count = 1024;
	};

	struct ASH_API EnvironmentDominantLightMetadata
	{
		bool valid = false;
		glm::vec3 direction{ 0.0f, 1.0f, 0.0f };
		float azimuth_degrees = 0.0f;
		float elevation_degrees = 90.0f;
		float luminance = 0.0f;
		std::string source{};
	};

	struct ASH_API EnvironmentMapCookedData
	{
		EnvironmentMapBuildDesc build_desc{};
		uint64_t source_content_hash = 0;
		EnvironmentDominantLightMetadata dominant_light{};
		TextureCubePayload radiance{};
		TextureCubePayload irradiance{};
		TextureCubePayload prefiltered_specular{};
		Texture2DPayload brdf_lut{};
	};

	struct ASH_API EnvironmentMapMetadata
	{
		EnvironmentMapBuildDesc build_desc{};
		uint64_t source_content_hash = 0;
		EnvironmentDominantLightMetadata dominant_light{};
	};

	struct ASH_API EnvironmentMapRuntimeResource
	{
		std::shared_ptr<RenderTarget> radiance_cubemap = nullptr;
		std::shared_ptr<RenderTarget> irradiance_cubemap = nullptr;
		std::shared_ptr<RenderTarget> prefiltered_specular_cubemap = nullptr;
		std::shared_ptr<RenderTarget> brdf_lut = nullptr;
		EnvironmentMapAssetState state = EnvironmentMapAssetState::Ready;
		std::string last_error{};
		uint64_t change_version = 1;
	};

	struct ASH_API EnvironmentLightingConfig
	{
		bool runtime_bake_cache = true;
		uint32_t default_radiance_size = 1024;
		uint32_t default_irradiance_size = 64;
		uint32_t default_prefilter_size = 256;
		uint32_t default_prefilter_mip_count = 8;
		uint32_t default_brdf_lut_size = 256;
		uint32_t default_sample_count = 256;
	};

	ASH_API EnvironmentLightingConfig make_default_environment_lighting_config();
	ASH_API EnvironmentLightingConfig load_runtime_environment_lighting_config(const char* config_path);
	ASH_API void set_runtime_environment_lighting_config(const EnvironmentLightingConfig& config);
	ASH_API EnvironmentLightingConfig get_runtime_environment_lighting_config();
	ASH_API EnvironmentMapBuildDesc make_environment_map_build_desc_from_runtime_config(const std::string& source_texture_path);
	ASH_API uint64_t hash_environment_source_file(const std::filesystem::path& path);
	ASH_API std::filesystem::path make_environment_map_source_cache_path(uint64_t source_content_hash);

	ASH_API bool validate_environment_map_cooked_data(const EnvironmentMapCookedData& data, std::string* out_error = nullptr);
	ASH_API bool write_ashibl_file(const std::filesystem::path& path, const EnvironmentMapCookedData& data, std::string* out_error = nullptr);
	ASH_API bool read_ashibl_file(const std::filesystem::path& path, EnvironmentMapCookedData& out_data, std::string* out_error = nullptr);
	ASH_API bool read_ashibl_metadata_file(const std::filesystem::path& path, EnvironmentMapMetadata& out_metadata, std::string* out_error = nullptr);
	ASH_API void fill_environment_map_test_pattern(EnvironmentMapCookedData& data);
	ASH_API std::string make_environment_map_asset_key(const std::string& ibl_asset_path, const std::string& source_texture_path);
}
