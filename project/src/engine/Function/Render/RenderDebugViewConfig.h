#pragma once

#include "Base/hcore.h"
#include <string>

namespace AshEngine
{
	struct ASH_API RenderDebugViewConfig
	{
		bool enabled = false;
		std::string selected = "Off";
	};

	ASH_API RenderDebugViewConfig make_default_render_debug_view_config();
	ASH_API RenderDebugViewConfig load_runtime_render_debug_view_config(const char* config_path);
	ASH_API void set_runtime_render_debug_view_config(const RenderDebugViewConfig& config);
	ASH_API RenderDebugViewConfig get_runtime_render_debug_view_config();
}
