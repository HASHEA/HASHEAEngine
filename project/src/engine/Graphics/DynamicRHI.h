#pragma once
#include "RHIBackend.h"
#include "GraphicsContext.h"

namespace RHI
{
	struct RuntimeRHIConfig
	{
		Backend backend = Backend::Default;
		VulkanValidationConfig vulkanValidation{};
		DX12ValidationConfig dx12Validation{};
	};

	bool is_backend_compiled(Backend backend);
	Backend get_default_backend();
	Backend resolve_runtime_backend(Backend requestedBackend);
	RuntimeRHIConfig load_runtime_rhi_config(const char* configPath);
	Backend load_backend_from_config(const char* configPath);
}
