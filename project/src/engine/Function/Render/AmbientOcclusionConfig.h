#pragma once

#include "Base/hcore.h"

namespace AshEngine
{
	enum class AmbientOcclusionMode : uint8_t
	{
		Off = 0,
		SSAO,
		HBAO,
		GTAO
	};

	enum class AmbientOcclusionQuality : uint8_t
	{
		Low = 0,
		Medium,
		High
	};

	struct ASH_API AmbientOcclusionConfig
	{
		AmbientOcclusionMode mode = AmbientOcclusionMode::Off;
		AmbientOcclusionQuality quality = AmbientOcclusionQuality::Medium;
		float radius = 1.5f;
		float intensity = 1.0f;
		float power = 1.0f;
		bool half_resolution = false;
		bool blur = true;
	};

	ASH_API const char* ambient_occlusion_mode_name(AmbientOcclusionMode mode);
	ASH_API const char* ambient_occlusion_quality_name(AmbientOcclusionQuality quality);
	ASH_API AmbientOcclusionConfig make_default_ambient_occlusion_config();
	ASH_API AmbientOcclusionConfig load_runtime_ambient_occlusion_config(const char* config_path);
	ASH_API void set_runtime_ambient_occlusion_config(const AmbientOcclusionConfig& config);
	ASH_API AmbientOcclusionConfig get_runtime_ambient_occlusion_config();
}
