#include <chrono>
#include "Timing.h"
#include "KBase/Public/KMemLeak.h"

namespace ETC_PAK
{
    uint64_t GetTime()
    {
        return std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()).time_since_epoch().count();
    }
}; // namespace ETC_PAK
