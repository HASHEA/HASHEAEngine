#include "Function/Render/RenderFeatureConfig.h"

#include "Base/IniConfig.h"
#include "Base/hlog.h"
#include <atomic>
#include <iterator>

namespace AshEngine
{
	namespace
	{
		static constexpr std::array<RenderSwitchDescriptor, 0> k_render_switch_descriptors{};

		static auto switch_index(RenderSwitch render_switch) -> size_t
		{
			return static_cast<size_t>(render_switch);
		}

		static auto make_switch_mask(const RenderFeatureConfig& config) -> uint32_t
		{
			uint32_t mask = 0;
			for (size_t index = 0; index < config.switches.size(); ++index)
			{
				if (config.switches[index])
				{
					mask |= (1u << index);
				}
			}
			return mask;
		}

		static auto make_config_from_switch_mask(uint32_t mask) -> RenderFeatureConfig
		{
			RenderFeatureConfig config = make_default_render_feature_config();
			for (size_t index = 0; index < config.switches.size(); ++index)
			{
				config.switches[index] = (mask & (1u << index)) != 0;
			}
			return config;
		}

		static auto default_switch_mask() -> uint32_t
		{
			return make_switch_mask(make_default_render_feature_config());
		}

		static auto runtime_switch_mask() -> std::atomic<uint32_t>&
		{
			static std::atomic<uint32_t> mask{ default_switch_mask() };
			return mask;
		}
	}

	RenderFeatureConfig::RenderFeatureConfig()
	{
		switches.fill(false);
		for (const RenderSwitchDescriptor& descriptor : k_render_switch_descriptors)
		{
			set_enabled(descriptor.id, descriptor.default_enabled);
		}
	}

	bool RenderFeatureConfig::is_enabled(RenderSwitch render_switch) const
	{
		const size_t index = switch_index(render_switch);
		return index < switches.size() && switches[index];
	}

	void RenderFeatureConfig::set_enabled(RenderSwitch render_switch, bool enabled)
	{
		const size_t index = switch_index(render_switch);
		if (index < switches.size())
		{
			switches[index] = enabled;
		}
	}

	const RenderSwitchDescriptor* get_render_switch_descriptors(uint32_t& out_count)
	{
		out_count = static_cast<uint32_t>(std::size(k_render_switch_descriptors));
		return k_render_switch_descriptors.empty() ? nullptr : k_render_switch_descriptors.data();
	}

	RenderFeatureConfig make_default_render_feature_config()
	{
		RenderFeatureConfig config{};
		return config;
	}

	RenderFeatureConfig load_runtime_render_feature_config(const char* config_path)
	{
		RenderFeatureConfig config = make_default_render_feature_config();
		IniConfig ini_config{};
		if (!ini_config.load(config_path))
		{
			HLogInfo("Render feature config file '{}' was not found. Using default render switches.", resolve_runtime_config_path(config_path).string());
			return config;
		}

		for (const RenderSwitchDescriptor& descriptor : k_render_switch_descriptors)
		{
			if (!ini_config.has_value(descriptor.section, descriptor.key))
			{
				continue;
			}

			bool enabled = descriptor.default_enabled;
			if (ini_config.try_get_bool(descriptor.section, descriptor.key, enabled))
			{
				config.set_enabled(descriptor.id, enabled);
				continue;
			}

			HLogWarning(
				"Render feature config '{}' contains invalid boolean value for [{}] {}. Keeping default '{}'.",
				ini_config.resolved_path().string(),
				descriptor.section ? descriptor.section : "",
				descriptor.key ? descriptor.key : "",
				descriptor.default_enabled ? "true" : "false");
		}

		HLogInfo("Runtime render switch config loaded. registered_switches={}.", static_cast<uint32_t>(std::size(k_render_switch_descriptors)));
		return config;
	}

	void set_runtime_render_feature_config(const RenderFeatureConfig& config)
	{
		runtime_switch_mask().store(make_switch_mask(config), std::memory_order_release);
	}

	RenderFeatureConfig get_runtime_render_feature_config()
	{
		return make_config_from_switch_mask(runtime_switch_mask().load(std::memory_order_acquire));
	}
}
