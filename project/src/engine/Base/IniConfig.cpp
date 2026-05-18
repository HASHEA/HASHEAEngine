#include "IniConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#if defined(ASH_WINDOWS)
#include <windows.h>
#endif

namespace AshEngine
{
	namespace
	{
		static auto get_executable_directory() -> std::filesystem::path
		{
#if defined(ASH_WINDOWS)
			wchar_t module_path[MAX_PATH]{};
			const DWORD length = GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
			if (length > 0 && length < std::size(module_path))
			{
				return std::filesystem::path(module_path).parent_path();
			}
#endif
			return std::filesystem::current_path();
		}

		static auto make_ini_key(const char* section, const char* key) -> std::string
		{
			std::string section_name = section ? section : "";
			std::string key_name = key ? key : "";
			section_name = to_lower_ascii(trim_ini_string(section_name));
			key_name = to_lower_ascii(trim_ini_string(key_name));
			return section_name + "." + key_name;
		}

		static auto try_parse_bool_value(const std::string& value, bool& out_value) -> bool
		{
			const std::string normalized = to_lower_ascii(trim_ini_string(value));
			if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
			{
				out_value = true;
				return true;
			}
			if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
			{
				out_value = false;
				return true;
			}
			return false;
		}
	}

	std::filesystem::path resolve_runtime_config_path(const char* config_path)
	{
		if (config_path == nullptr || *config_path == '\0')
		{
			return {};
		}

		std::filesystem::path requested_path(config_path);
		if (requested_path.is_absolute() && std::filesystem::exists(requested_path))
		{
			return requested_path;
		}

		if (std::filesystem::exists(requested_path))
		{
			return std::filesystem::absolute(requested_path);
		}

		std::filesystem::path probe_base = get_executable_directory();
		while (!probe_base.empty())
		{
			const std::filesystem::path candidate = probe_base / requested_path;
			if (std::filesystem::exists(candidate))
			{
				return candidate;
			}

			const std::filesystem::path parent = probe_base.parent_path();
			if (parent == probe_base)
			{
				break;
			}
			probe_base = parent;
		}

		return requested_path;
	}

	std::string trim_ini_string(const std::string& value)
	{
		size_t begin = 0;
		size_t end = value.size();
		if (value.size() >= 3 &&
			static_cast<unsigned char>(value[0]) == 0xEF &&
			static_cast<unsigned char>(value[1]) == 0xBB &&
			static_cast<unsigned char>(value[2]) == 0xBF)
		{
			begin = 3;
		}
		while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
		{
			++begin;
		}
		while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		{
			--end;
		}
		return value.substr(begin, end - begin);
	}

	std::string to_lower_ascii(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	bool IniConfig::load(const char* config_path)
	{
		return load(resolve_runtime_config_path(config_path));
	}

	bool IniConfig::load(const std::filesystem::path& config_path)
	{
		m_values.clear();
		m_resolved_path = config_path;
		if (m_resolved_path.empty())
		{
			return false;
		}

		std::ifstream file(m_resolved_path);
		if (!file.is_open())
		{
			return false;
		}

		std::string current_section{};
		std::string line{};
		while (std::getline(file, line))
		{
			const size_t comment_pos = line.find_first_of("#;");
			if (comment_pos != std::string::npos)
			{
				line.erase(comment_pos);
			}

			const std::string trimmed_line = trim_ini_string(line);
			if (trimmed_line.empty())
			{
				continue;
			}

			if (trimmed_line.front() == '[' && trimmed_line.back() == ']')
			{
				current_section = to_lower_ascii(trim_ini_string(trimmed_line.substr(1, trimmed_line.size() - 2)));
				continue;
			}

			const size_t separator_pos = trimmed_line.find('=');
			if (separator_pos == std::string::npos)
			{
				continue;
			}

			const std::string key = to_lower_ascii(trim_ini_string(trimmed_line.substr(0, separator_pos)));
			const std::string value = trim_ini_string(trimmed_line.substr(separator_pos + 1));
			m_values[current_section + "." + key] = value;
		}

		return true;
	}

	bool IniConfig::empty() const
	{
		return m_values.empty();
	}

	const std::filesystem::path& IniConfig::resolved_path() const
	{
		return m_resolved_path;
	}

	bool IniConfig::has_value(const char* section, const char* key) const
	{
		return m_values.find(make_ini_key(section, key)) != m_values.end();
	}

	std::string IniConfig::get_string(const char* section, const char* key, const char* default_value) const
	{
		const auto it = m_values.find(make_ini_key(section, key));
		if (it == m_values.end())
		{
			return default_value ? default_value : "";
		}
		return it->second;
	}

	bool IniConfig::get_bool(const char* section, const char* key, bool default_value) const
	{
		bool parsed_value = default_value;
		return try_get_bool(section, key, parsed_value) ? parsed_value : default_value;
	}

	bool IniConfig::try_get_bool(const char* section, const char* key, bool& out_value) const
	{
		const auto it = m_values.find(make_ini_key(section, key));
		if (it == m_values.end())
		{
			return false;
		}
		return try_parse_bool_value(it->second, out_value);
	}
}
