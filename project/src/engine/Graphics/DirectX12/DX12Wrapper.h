#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <string>

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#define DX12_CHECK_RESULT(hr)                                                                   \
    {                                                                                            \
        HRESULT _hr = (hr);                                                                      \
        if (FAILED(_hr))                                                                         \
        {                                                                                        \
            HLogError("Fatal: DX12 CALL ERROR: HRESULT 0x{:08X} in {} at line {}", (uint32_t)_hr, __FILE__, __LINE__); \
            H_ASSERT(SUCCEEDED(_hr));                                                            \
        }                                                                                        \
    }

namespace RHI
{
    inline std::wstring dx12_debug_name_to_wstring(const char* name)
    {
        if (!name || name[0] == '\0')
        {
            return {};
        }

        const int wideLength = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
        if (wideLength <= 1)
        {
            return {};
        }

        std::wstring result(static_cast<size_t>(wideLength), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name, -1, result.data(), wideLength);
        result.pop_back();
        return result;
    }

    inline void dx12_set_debug_name(ID3D12Object* object, const char* name)
    {
        if (!object || !name || name[0] == '\0')
        {
            return;
        }

        const std::wstring wideName = dx12_debug_name_to_wstring(name);
        if (!wideName.empty())
        {
            object->SetName(wideName.c_str());
        }
    }
}
