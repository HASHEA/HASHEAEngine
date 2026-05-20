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

	enum class AmbientOcclusionDebugView : uint8_t
	{
		Off = 0,
		RawAO,
		FinalAO,
		Depth,
		Normal,
		MotionVector,
		TemporalAO,
		HistoryWeight
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
		bool temporal = false;
		float temporal_blend = 0.85f;
		float temporal_depth_threshold = 0.01f;
		float temporal_normal_threshold = 0.75f;
		AmbientOcclusionDebugView debug_view = AmbientOcclusionDebugView::Off;
	};

	ASH_API const char* ambient_occlusion_mode_name(AmbientOcclusionMode mode);
	ASH_API const char* ambient_occlusion_quality_name(AmbientOcclusionQuality quality);
	ASH_API const char* ambient_occlusion_debug_view_name(AmbientOcclusionDebugView view);
	ASH_API AmbientOcclusionConfig make_default_ambient_occlusion_config();
	ASH_API AmbientOcclusionConfig load_runtime_ambient_occlusion_config(const char* config_path);
	ASH_API void set_runtime_ambient_occlusion_config(const AmbientOcclusionConfig& config);
	ASH_API AmbientOcclusionConfig get_runtime_ambient_occlusion_config();
}
