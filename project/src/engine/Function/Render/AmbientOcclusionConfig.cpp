#include "Function/Render/AmbientOcclusionConfig.h"

#include "Base/IniConfig.h"
#include "Base/hlog.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_token(std::string value) -> std::string
		{
			value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			}), value.end());
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		auto parse_ao_mode(const std::string& value, AmbientOcclusionMode& out_mode) -> bool
		{
			const std::string token = normalize_token(value);
			if (token == "off")
			{
				out_mode = AmbientOcclusionMode::Off;
				return true;
			}
			if (token == "ssao")
			{
				out_mode = AmbientOcclusionMode::SSAO;
				return true;
			}
			if (token == "hbao")
			{
				out_mode = AmbientOcclusionMode::HBAO;
				return true;
			}
			if (token == "gtao")
			{
				out_mode = AmbientOcclusionMode::GTAO;
				return true;
			}
			return false;
		}

		auto parse_ao_quality(const std::string& value, AmbientOcclusionQuality& out_quality) -> bool
		{
			const std::string token = normalize_token(value);
			if (token == "low")
			{
				out_quality = AmbientOcclusionQuality::Low;
				return true;
			}
			if (token == "medium")
			{
				out_quality = AmbientOcclusionQuality::Medium;
				return true;
			}
			if (token == "high")
			{
				out_quality = AmbientOcclusionQuality::High;
				return true;
			}
			return false;
		}

		auto try_get_ini_string(const IniConfig& ini_config, const char* section, const char* key, std::string& out_value) -> bool
		{
			if (!ini_config.has_value(section, key))
			{
				return false;
			}
			out_value = ini_config.get_string(section, key, "");
			return true;
		}

		auto try_parse_float_value(const std::string& text, float& out_value) -> bool
		{
			const std::string trimmed = trim_ini_string(text);
			if (trimmed.empty())
			{
				return false;
			}

			char* parse_end = nullptr;
			errno = 0;
			const float parsed = std::strtof(trimmed.c_str(), &parse_end);
			if (parse_end == trimmed.c_str() || *parse_end != '\0' || errno == ERANGE || !std::isfinite(parsed))
			{
				return false;
			}

			out_value = parsed;
			return true;
		}

		auto try_get_ini_float(const IniConfig& ini_config, const char* section, const char* key, float& out_value) -> bool
		{
			std::string text{};
			return try_get_ini_string(ini_config, section, key, text) && try_parse_float_value(text, out_value);
		}

		auto clamp_range(float value, float fallback, float minimum, float maximum) -> float
		{
			if (value < minimum)
			{
				return fallback;
			}
			return std::clamp(value, minimum, maximum);
		}

		auto runtime_config_storage() -> AmbientOcclusionConfig&
		{
			static AmbientOcclusionConfig config = make_default_ambient_occlusion_config();
			return config;
		}

		auto runtime_config_mutex() -> std::mutex&
		{
			static std::mutex mutex;
			return mutex;
		}
	}

	const char* ambient_occlusion_mode_name(AmbientOcclusionMode mode)
	{
		switch (mode)
		{
		case AmbientOcclusionMode::SSAO:
			return "SSAO";
		case AmbientOcclusionMode::HBAO:
			return "HBAO";
		case AmbientOcclusionMode::GTAO:
			return "GTAO";
		case AmbientOcclusionMode::Off:
		default:
			return "Off";
		}
	}

	const char* ambient_occlusion_quality_name(AmbientOcclusionQuality quality)
	{
		switch (quality)
		{
		case AmbientOcclusionQuality::Low:
			return "Low";
		case AmbientOcclusionQuality::High:
			return "High";
		case AmbientOcclusionQuality::Medium:
		default:
			return "Medium";
		}
	}

	AmbientOcclusionConfig make_default_ambient_occlusion_config()
	{
		return AmbientOcclusionConfig{};
	}

	AmbientOcclusionConfig load_runtime_ambient_occlusion_config(const char* config_path)
	{
		AmbientOcclusionConfig config = make_default_ambient_occlusion_config();
		IniConfig ini_config{};
		if (!ini_config.load(config_path))
		{
			HLogInfo("Ambient occlusion config file '{}' was not found. Using default AO config.", resolve_runtime_config_path(config_path).string());
			return config;
		}

		std::string mode_text{};
		if (try_get_ini_string(ini_config, "AmbientOcclusion", "Mode", mode_text))
		{
			AmbientOcclusionMode parsed_mode = config.mode;
			if (parse_ao_mode(mode_text, parsed_mode))
			{
				config.mode = parsed_mode;
			}
			else
			{
				HLogWarning("AmbientOcclusion.Mode '{}' is invalid. Keeping default '{}'.", mode_text, ambient_occlusion_mode_name(config.mode));
			}
		}

		std::string quality_text{};
		if (try_get_ini_string(ini_config, "AmbientOcclusion", "Quality", quality_text))
		{
			AmbientOcclusionQuality parsed_quality = config.quality;
			if (parse_ao_quality(quality_text, parsed_quality))
			{
				config.quality = parsed_quality;
			}
			else
			{
				HLogWarning("AmbientOcclusion.Quality '{}' is invalid. Keeping default '{}'.", quality_text, ambient_occlusion_quality_name(config.quality));
			}
		}

		float value = 0.0f;
		if (try_get_ini_float(ini_config, "AmbientOcclusion", "Radius", value))
		{
			config.radius = clamp_range(value, config.radius, 0.05f, 20.0f);
		}
		if (try_get_ini_float(ini_config, "AmbientOcclusion", "Intensity", value))
		{
			config.intensity = clamp_range(value, config.intensity, 0.0f, 8.0f);
		}
		if (try_get_ini_float(ini_config, "AmbientOcclusion", "Power", value))
		{
			config.power = clamp_range(value, config.power, 0.05f, 8.0f);
		}

		bool bool_value = false;
		if (ini_config.try_get_bool("AmbientOcclusion", "HalfResolution", bool_value))
		{
			config.half_resolution = bool_value;
		}
		if (ini_config.try_get_bool("AmbientOcclusion", "Blur", bool_value))
		{
			config.blur = bool_value;
		}

		HLogInfo(
			"Runtime ambient occlusion config loaded. mode={} quality={} radius={} intensity={} power={} half_resolution={} blur={}.",
			ambient_occlusion_mode_name(config.mode),
			ambient_occlusion_quality_name(config.quality),
			config.radius,
			config.intensity,
			config.power,
			config.half_resolution,
			config.blur);
		return config;
	}

	void set_runtime_ambient_occlusion_config(const AmbientOcclusionConfig& config)
	{
		std::lock_guard<std::mutex> lock(runtime_config_mutex());
		runtime_config_storage() = config;
	}

	AmbientOcclusionConfig get_runtime_ambient_occlusion_config()
	{
		std::lock_guard<std::mutex> lock(runtime_config_mutex());
		return runtime_config_storage();
	}
}
