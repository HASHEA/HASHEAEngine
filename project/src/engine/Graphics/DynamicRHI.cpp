#include "DynamicRHI.h"
#include "GraphicsContext.h"
#include "SwapChain.h"
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"

#if defined(ASH_HAS_DX12)
#include "DirectX12/DX12Context.h"
#include "DirectX12/DX12Swapchain.h"
#endif

#if defined(ASH_HAS_VULKAN)
#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanSwapchain.h"
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#if defined(ASH_WINDOWS)
#include <windows.h>
#endif

namespace
{
	static auto get_executable_directory() -> std::filesystem::path
	{
#if defined(ASH_WINDOWS)
		wchar_t modulePath[MAX_PATH]{};
		const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
		if (length > 0 && length < std::size(modulePath))
		{
			return std::filesystem::path(modulePath).parent_path();
		}
#endif
		return std::filesystem::current_path();
	}

	static auto resolve_config_path(const char* configPath) -> std::filesystem::path
	{
		if (configPath == nullptr || *configPath == '\0')
		{
			return {};
		}

		std::filesystem::path requestedPath(configPath);
		if (requestedPath.is_absolute() && std::filesystem::exists(requestedPath))
		{
			return requestedPath;
		}

		if (std::filesystem::exists(requestedPath))
		{
			return std::filesystem::absolute(requestedPath);
		}

		std::filesystem::path probeBase = get_executable_directory();
		while (!probeBase.empty())
		{
			const std::filesystem::path candidate = probeBase / requestedPath;
			if (std::filesystem::exists(candidate))
			{
				return candidate;
			}

			const std::filesystem::path parent = probeBase.parent_path();
			if (parent == probeBase)
			{
				break;
			}
			probeBase = parent;
		}

		return requestedPath;
	}

	static auto trim_string(const std::string& value) -> std::string
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

	static auto to_lower_ascii(std::string value) -> std::string
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return value;
	}

	static auto parse_backend_name(const std::string& value) -> RHI::Backend
	{
		const std::string normalized = to_lower_ascii(trim_string(value));
		if (normalized == "vulkan" || normalized == "vk")
		{
			return RHI::Backend::Vulkan;
		}
		if (normalized == "directx12" || normalized == "dx12" || normalized == "d3d12")
		{
			return RHI::Backend::DirectX12;
		}
		if (normalized == "default" || normalized.empty())
		{
			return RHI::Backend::Default;
		}
		return RHI::Backend::Default;
	}

	static auto parse_bool_value(const std::string& value, bool defaultValue) -> bool
	{
		const std::string normalized = to_lower_ascii(trim_string(value));
		if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
		{
			return true;
		}
		if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
		{
			return false;
		}
		return defaultValue;
	}

	static auto parse_ini_file(const std::filesystem::path& configPath) -> std::unordered_map<std::string, std::string>
	{
		std::unordered_map<std::string, std::string> values{};
		if (configPath.empty())
		{
			return values;
		}

		std::ifstream file(configPath);
		if (!file.is_open())
		{
			return values;
		}

		std::string currentSection{};
		std::string line{};
		while (std::getline(file, line))
		{
			const size_t commentPos = line.find_first_of("#;");
			if (commentPos != std::string::npos)
			{
				line.erase(commentPos);
			}

			const std::string trimmedLine = trim_string(line);
			if (trimmedLine.empty())
			{
				continue;
			}

			if (trimmedLine.front() == '[' && trimmedLine.back() == ']')
			{
				currentSection = to_lower_ascii(trim_string(trimmedLine.substr(1, trimmedLine.size() - 2)));
				continue;
			}

			const size_t separatorPos = trimmedLine.find('=');
			if (separatorPos == std::string::npos)
			{
				continue;
			}

			const std::string key = to_lower_ascii(trim_string(trimmedLine.substr(0, separatorPos)));
			const std::string value = trim_string(trimmedLine.substr(separatorPos + 1));
			values[currentSection + "." + key] = value;
		}

		return values;
	}
}

namespace RHI
{
	bool is_backend_compiled(Backend backend)
	{
		switch (backend)
		{
		case Backend::Vulkan:
#if defined(ASH_HAS_VULKAN)
			return true;
#else
			return false;
#endif
		case Backend::DirectX12:
#if defined(ASH_HAS_DX12)
			return true;
#else
			return false;
#endif
		case Backend::Default:
		default:
			return false;
		}
	}

	Backend get_default_backend()
	{
#if defined(ASH_WINDOWS)
#if defined(ASH_HAS_DX12)
		return Backend::DirectX12;
#elif defined(ASH_HAS_VULKAN)
		return Backend::Vulkan;
#else
		return Backend::Default;
#endif
#else
#if defined(ASH_HAS_VULKAN)
		return Backend::Vulkan;
#elif defined(ASH_HAS_DX12)
		return Backend::DirectX12;
#else
		return Backend::Default;
#endif
#endif
	}

	Backend resolve_runtime_backend(Backend requestedBackend)
	{
		Backend resolvedBackend = requestedBackend;
		if (resolvedBackend == Backend::Default)
		{
			resolvedBackend = get_default_backend();
		}

		if (is_backend_compiled(resolvedBackend))
		{
			return resolvedBackend;
		}

		const Backend fallbackBackend = get_default_backend();
		if (fallbackBackend != Backend::Default && is_backend_compiled(fallbackBackend))
		{
			HLogWarning("Requested backend '{}' is not compiled in this build. Falling back to '{}'.",
				backend_to_string(requestedBackend),
				backend_to_string(fallbackBackend));
			return fallbackBackend;
		}

		HLogError("No compiled RHI backend is available. Requested backend was '{}'.", backend_to_string(requestedBackend));
		return Backend::Default;
	}

	RuntimeRHIConfig load_runtime_rhi_config(const char* configPath)
	{
		RuntimeRHIConfig config{};
		if (configPath == nullptr || *configPath == '\0')
		{
			return config;
		}

		const std::filesystem::path resolvedConfigPath = resolve_config_path(configPath);
		const auto values = parse_ini_file(resolvedConfigPath);
		if (values.empty())
		{
			HLogInfo("RHI config file '{}' was not found. Falling back to compiled default backend.", resolvedConfigPath.string());
			return config;
		}

		if (const auto it = values.find("rhi.backend"); it != values.end())
		{
			config.backend = parse_backend_name(it->second);
			if (config.backend == Backend::Default && !trim_string(it->second).empty())
			{
				HLogWarning("RHI config '{}' contains an invalid backend entry '{}'. Falling back to compiled default backend.",
					resolvedConfigPath.string(),
					it->second);
			}
		}

		if (const auto it = values.find("vulkanvalidation.enabled"); it != values.end())
		{
			config.vulkanValidation.enableValidation = parse_bool_value(it->second, config.vulkanValidation.enableValidation);
		}
		if (const auto it = values.find("vulkanvalidation.gpuassisted"); it != values.end())
		{
			config.vulkanValidation.enableGpuAssisted = parse_bool_value(it->second, config.vulkanValidation.enableGpuAssisted);
		}
		if (const auto it = values.find("vulkanvalidation.synchronizationvalidation"); it != values.end())
		{
			config.vulkanValidation.enableSynchronizationValidation = parse_bool_value(it->second, config.vulkanValidation.enableSynchronizationValidation);
		}
		if (const auto it = values.find("vulkanvalidation.breakonvalidationerror"); it != values.end())
		{
			config.vulkanValidation.breakOnValidationError = parse_bool_value(it->second, config.vulkanValidation.breakOnValidationError);
		}

		if (const auto it = values.find("dx12validation.enabled"); it != values.end())
		{
			config.dx12Validation.enableDebugLayer = parse_bool_value(it->second, config.dx12Validation.enableDebugLayer);
		}
		if (const auto it = values.find("dx12validation.gpuvalidation"); it != values.end())
		{
			config.dx12Validation.enableGpuValidation = parse_bool_value(it->second, config.dx12Validation.enableGpuValidation);
		}

#if !defined(ASH_DEBUG)
		config.dx12Validation.enableDebugLayer = false;
		config.dx12Validation.enableGpuValidation = false;
#endif

		return config;
	}

	Backend load_backend_from_config(const char* configPath)
	{
		return load_runtime_rhi_config(configPath).backend;
	}

	GraphicsContext* GraphicsContext::create(Backend backend)
	{
		switch (resolve_runtime_backend(backend))
		{
		case Backend::Vulkan:
#if defined(ASH_HAS_VULKAN)
			return Ash_New<VulkanContext>();
#else
			break;
#endif
		case Backend::DirectX12:
#if defined(ASH_HAS_DX12)
			return Ash_New<DX12Context>();
#else
			break;
#endif
		case Backend::Default:
		default:
			break;
		}

		H_ASSERTLOG(false, "Failed to create GraphicsContext for backend '{}'.", backend_to_string(backend));
		return nullptr;
	}

	Swapchain* Swapchain::create(Backend backend)
	{
		switch (resolve_runtime_backend(backend))
		{
		case Backend::Vulkan:
#if defined(ASH_HAS_VULKAN)
			return Ash_New<VulkanSwapchain>();
#else
			break;
#endif
		case Backend::DirectX12:
#if defined(ASH_HAS_DX12)
			return Ash_New<DX12Swapchain>();
#else
			break;
#endif
		case Backend::Default:
		default:
			break;
		}

		H_ASSERTLOG(false, "Failed to create Swapchain for backend '{}'.", backend_to_string(backend));
		return nullptr;
	}
}
