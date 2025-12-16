#include "KGFX_HashFunDX12.h"
#include <Windows.h>

namespace gfx
{

    uint64_t Fnv1a64(const void* data, size_t len, uint64_t h)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        while (len--)
        {
            h ^= *p++;
            h *= 1099511628211ull;
        }
        return h;
    }

    void HashCombine(uint64_t& h, uint64_t v)
    {
        // 64bit boost-like combine
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    }

    uint64_t PtrValueHash(const void* p)
    {
        uintptr_t v = reinterpret_cast<uintptr_t>(p);
        return Fnv1a64(&v, sizeof(v));
    }

    std::wstring ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring ws(len - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
        return ws;
    }

    void Fnv1a64Append(uint64_t& h, uint64_t v)
    {
        constexpr uint64_t PRIME = 1099511628211ull;
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h *= PRIME;
    }
}
