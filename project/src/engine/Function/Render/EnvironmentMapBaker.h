#pragma once

#include "Function/Render/EnvironmentMapAsset.h"

namespace AshEngine
{
	struct ASH_API EnvironmentBakeReport
	{
		bool succeeded = false;
		std::string message{};
		uint32_t generated_radiance_faces = 0;
		uint32_t generated_irradiance_faces = 0;
		uint32_t generated_prefilter_mips = 0;
	};

	struct ASH_API EnvironmentBakeOverrides
	{
		uint32_t radiance_size = 0;
		uint32_t irradiance_size = 0;
		uint32_t prefilter_size = 0;
		uint32_t prefilter_mip_count = 0;
		uint32_t brdf_lut_size = 0;
		uint32_t sample_count = 0;
	};

	class ASH_API EnvironmentMapBaker
	{
	public:
		static bool bake_to_cooked_data(
			const EnvironmentMapBuildDesc& desc,
			EnvironmentMapCookedData& out_data,
			EnvironmentBakeReport* out_report = nullptr);

		static bool write_ashibl(
			const EnvironmentMapCookedData& data,
			const std::filesystem::path& output_path,
			EnvironmentBakeReport* out_report = nullptr);

		static bool read_ashibl(
			const std::filesystem::path& input_path,
			EnvironmentMapCookedData& out_data,
			EnvironmentBakeReport* out_report = nullptr);
	};

	ASH_API int32_t bake_ashibl_file_from_runtime_config(
		const char* source_texture_path,
		const char* output_path,
		const char* config_path,
		EnvironmentBakeReport* out_report = nullptr);

	ASH_API int32_t bake_ashibl_file_from_runtime_config(
		const char* source_texture_path,
		const char* output_path,
		const char* config_path,
		const EnvironmentBakeOverrides* overrides,
		EnvironmentBakeReport* out_report = nullptr);
}
