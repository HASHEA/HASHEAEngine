#pragma once

#include "Base/hcore.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace AshEngine
{
	enum class RenderSwitch : uint8_t
	{
		Count = 0
	};

	struct RenderSwitchDescriptor
	{
		RenderSwitch id = RenderSwitch::Count;
		const char* section = nullptr;
		const char* key = nullptr;
		bool default_enabled = false;
		const char* display_name = nullptr;
	};

	struct ASH_API RenderFeatureConfig
	{
		RenderFeatureConfig();

		bool is_enabled(RenderSwitch render_switch) const;
		void set_enabled(RenderSwitch render_switch, bool enabled);

		std::array<bool, static_cast<size_t>(RenderSwitch::Count)> switches{};
	};

	ASH_API const RenderSwitchDescriptor* get_render_switch_descriptors(uint32_t& out_count);
	ASH_API RenderFeatureConfig make_default_render_feature_config();
	ASH_API RenderFeatureConfig load_runtime_render_feature_config(const char* config_path);
	ASH_API void set_runtime_render_feature_config(const RenderFeatureConfig& config);
	ASH_API RenderFeatureConfig get_runtime_render_feature_config();
}
