#pragma once

namespace RHI
{
	enum class Backend
	{
		Default,
		Vulkan,
		DirectX12,
	};

	inline const char* backend_to_string(Backend backend)
	{
		switch (backend)
		{
		case Backend::Vulkan:
			return "Vulkan";
		case Backend::DirectX12:
			return "DirectX 12";
		case Backend::Default:
		default:
			return "Default";
		}
	}
}
