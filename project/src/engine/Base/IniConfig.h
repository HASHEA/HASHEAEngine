#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace AshEngine
{
	class IniConfig
	{
	public:
		bool load(const char* config_path);
		bool load(const std::filesystem::path& config_path);
		bool empty() const;
		const std::filesystem::path& resolved_path() const;
		bool has_value(const char* section, const char* key) const;
		std::string get_string(const char* section, const char* key, const char* default_value = "") const;
		bool get_bool(const char* section, const char* key, bool default_value) const;
		bool try_get_bool(const char* section, const char* key, bool& out_value) const;

	private:
		std::filesystem::path m_resolved_path{};
		std::unordered_map<std::string, std::string> m_values{};
	};

	std::filesystem::path resolve_runtime_config_path(const char* config_path);
	std::string trim_ini_string(const std::string& value);
	std::string to_lower_ascii(std::string value);
}
