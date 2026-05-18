#pragma once

#include "hcore.h"

#include <cstdint>

namespace AshEngine
{
	struct ProcessMemorySnapshot
	{
		bool supported = false;
		uint64_t working_set_bytes = 0;
		uint64_t private_bytes = 0;
		uint64_t pagefile_bytes = 0;
	};

	auto ASH_API get_current_process_memory_snapshot() -> ProcessMemorySnapshot;
}
