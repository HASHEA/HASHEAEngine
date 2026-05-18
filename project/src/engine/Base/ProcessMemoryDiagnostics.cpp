#include "ProcessMemoryDiagnostics.h"

#if defined(ASH_WINDOWS)
#include <Windows.h>
#include <Psapi.h>
#endif

namespace AshEngine
{
	auto get_current_process_memory_snapshot() -> ProcessMemorySnapshot
	{
		ProcessMemorySnapshot snapshot{};
#if defined(ASH_WINDOWS)
		PROCESS_MEMORY_COUNTERS_EX counters{};
		counters.cb = sizeof(counters);
		if (GetProcessMemoryInfo(
			GetCurrentProcess(),
			reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
			sizeof(counters)))
		{
			snapshot.supported = true;
			snapshot.working_set_bytes = static_cast<uint64_t>(counters.WorkingSetSize);
			snapshot.private_bytes = static_cast<uint64_t>(counters.PrivateUsage);
			snapshot.pagefile_bytes = static_cast<uint64_t>(counters.PagefileUsage);
		}
#endif
		return snapshot;
	}
}
