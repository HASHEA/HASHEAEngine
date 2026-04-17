#pragma once

#include <cstdint>

namespace AshSandbox
{
	enum class SandboxDefaultRuntimeMode : uint8_t
	{
		StandardScene = 0
	};

	auto get_default_sandbox_runtime_mode() -> SandboxDefaultRuntimeMode;
	auto get_default_sandbox_runtime_mode_name(SandboxDefaultRuntimeMode mode) -> const char*;
}
