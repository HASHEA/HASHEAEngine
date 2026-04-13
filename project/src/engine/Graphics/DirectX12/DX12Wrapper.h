#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#ifdef ASH_DEBUG
#include <dxgidebug.h>
#endif

#define DX12_CHECK_RESULT(hr)                                                                   \
    {                                                                                            \
        HRESULT _hr = (hr);                                                                      \
        if (FAILED(_hr))                                                                         \
        {                                                                                        \
            HLogError("Fatal: DX12 CALL ERROR: HRESULT 0x{:08X} in {} at line {}", (uint32_t)_hr, __FILE__, __LINE__); \
            H_ASSERT(SUCCEEDED(_hr));                                                            \
        }                                                                                        \
    }
