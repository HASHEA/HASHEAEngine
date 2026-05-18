#include "DynamicRHI.h"
#include "Base/IniConfig.h"
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

#include <string>

namespace
{
	static auto parse_backend_name(const std::string& value) -> RHI::Backend
	{
		const std::string normalized = AshEngine::to_lower_ascii(AshEngine::trim_ini_string(value));
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

		AshEngine::IniConfig iniConfig{};
		if (!iniConfig.load(configPath) || iniConfig.empty())
		{
			HLogInfo("RHI config file '{}' was not found. Falling back to compiled default backend.",
				AshEngine::resolve_runtime_config_path(configPath).string());
			return config;
		}

		if (iniConfig.has_value("RHI", "Backend"))
		{
			const std::string backendName = iniConfig.get_string("RHI", "Backend");
			config.backend = parse_backend_name(backendName);
			if (config.backend == Backend::Default && !AshEngine::trim_ini_string(backendName).empty())
			{
				HLogWarning("RHI config '{}' contains an invalid backend entry '{}'. Falling back to compiled default backend.",
					iniConfig.resolved_path().string(),
					backendName);
			}
		}

		config.vulkanValidation.enableValidation =
			iniConfig.get_bool("VulkanValidation", "Enabled", config.vulkanValidation.enableValidation);
		config.vulkanValidation.enableGpuAssisted =
			iniConfig.get_bool("VulkanValidation", "GpuAssisted", config.vulkanValidation.enableGpuAssisted);
		config.vulkanValidation.enableSynchronizationValidation =
			iniConfig.get_bool("VulkanValidation", "SynchronizationValidation", config.vulkanValidation.enableSynchronizationValidation);
		config.vulkanValidation.breakOnValidationError =
			iniConfig.get_bool("VulkanValidation", "BreakOnValidationError", config.vulkanValidation.breakOnValidationError);

		config.dx12Validation.enableDebugLayer =
			iniConfig.get_bool("DX12Validation", "Enabled", config.dx12Validation.enableDebugLayer);
		config.dx12Validation.enableGpuValidation =
			iniConfig.get_bool("DX12Validation", "GpuValidation", config.dx12Validation.enableGpuValidation);

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
