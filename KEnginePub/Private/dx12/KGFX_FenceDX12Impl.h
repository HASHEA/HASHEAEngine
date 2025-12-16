#pragma once
#include "DMA_2.0.0/D3D12MemAlloc.h"
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    class KGFX_FenceDX12Impl : public KSignalFence
    {
    public:
        KGFX_FenceDX12Impl() = default;
        ~KGFX_FenceDX12Impl()override ;

        void Init();

        virtual bool IsSubmitted() const override;
        void Clear() override;
        bool Query() override;

        bool GetCurrentValue(uint64_t* outValue) override;
        void* GetNativeHandle() override;

        bool m_bSubmitted = false;
        uint64_t m_FenceValue = 0;
        ID3D12Fence* m_D3d12Fence = nullptr;
    };
}
