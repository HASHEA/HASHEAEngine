#include "Tests/SandboxTestRegistry.h"

namespace AshSandbox
{
	auto get_default_sandbox_runtime_mode() -> SandboxDefaultRuntimeMode
	{
		return SandboxDefaultRuntimeMode::StandardScene;
	}

	auto get_default_sandbox_runtime_mode_name(SandboxDefaultRuntimeMode mode) -> const char*
	{
		switch (mode)
		{
		case SandboxDefaultRuntimeMode::StandardScene:
			return "StandardScene";
		default:
			return "Unknown";
		}
	}
}
