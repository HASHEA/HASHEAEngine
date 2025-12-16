#pragma once
#include "KGFX_BufferImplDX12.h"

namespace gfx
{
    class KGFX_BufferDx12;
#define DetectKGFX_BufferViewDX12MemLeck 0

    class KGFX_BufferViewDX12 final : public IKGFX_BufferView
    {
    public:
        KGFX_BufferViewDX12(IKGFX_Buffer* pBuf, const KGFX_BufferViewDesc& desc);
        ~KGFX_BufferViewDX12() override;
        KGFX_BufferViewDX12(const KGFX_BufferViewDX12&) = delete;
        KGFX_BufferViewDX12& operator=(const KGFX_BufferViewDX12&) = delete;
        KGFX_BufferViewDX12(KGFX_BufferViewDX12&& other) noexcept;
        KGFX_BufferViewDX12& operator=(KGFX_BufferViewDX12&& other) noexcept;

        IKGFX_Buffer* GetResource() override;

        void* GetViewHandle() override;

        const KGFX_BufferViewDesc* GetViewDesc() const override;

        uint64_t GetCode() override;
        uintptr_t GetNativeHandle() override;
        virtual void SetObjectName(const char* pcszName) override;
        uint32_t GetBindlessHandle() override;
        uint32_t GetViewOffset() const;
        uint32_t GetViewRange() const;

        void PlacedCreate(IKGFX_Buffer* pBuf, const KGFX_BufferViewDesc& desc);
    private:
      

        D3D12Descriptor GetSRV() const;
        D3D12Descriptor GetUAV() const;
        D3D12Descriptor GetCBV() const;

        KGFX_BufferDx12* m_pBuf = nullptr;
        KGFX_BufferViewDesc m_Desc = {};
        BindlessDescriptor m_BindlessDescriptor = {};
#if DetectKGFX_BufferViewDX12MemLeck
        char* m_pMemlectDecter = nullptr;
#endif
        uintptr_t m_uNativeHandle = 0;
    };
}
