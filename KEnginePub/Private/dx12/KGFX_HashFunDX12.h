#pragma once
#include <cstdint>
#include <string>

namespace gfx
{
    uint64_t Fnv1a64(const void* data, size_t len, uint64_t h = 1469598103934665603ull);

    void Fnv1a64Append(uint64_t& h, uint64_t v);

    void HashCombine(uint64_t& h, uint64_t v);

    // 将指针“数值”安全转换为 64 位
    uint64_t PtrValueHash(const void* p);

    std::wstring ToWide(const std::string& s);
}
