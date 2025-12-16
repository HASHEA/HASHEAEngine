#pragma once
#ifdef _WIN32
#include "KGFX_BufferImplDX12.h"

namespace gfx
{
    class KGFX_BufferDx12 : public IKGFX_Buffer
    {
    public:
        friend class KGFX_BufferViewDX12;
        KGFX_BufferDx12();
        ~KGFX_BufferDx12() override;
        KGFX_BufferDx12(const KGFX_BufferDx12&) = delete;
        KGFX_BufferDx12& operator=(const KGFX_BufferDx12&) = delete;
        KGFX_BufferDx12(KGFX_BufferDx12&& other) noexcept;
        KGFX_BufferDx12& operator=(KGFX_BufferDx12&& other) noexcept;

        // interface KGfxRef
        int32_t AddRef() override;
        int32_t GetRef() override;
        int32_t Release() override;

        // interface IKGFX_Resource
        uintptr_t GetNativeResourceHandle() override;
        void SetDebugName(const char* szName) override;
        const char* GetDebugName() override;

        // interface IKGFX_Buffer
        const KGfxBufferDesc* GetDesc() const override;
        BOOL Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite) override;
        uint32_t GetDynamicOffset() override;
        void* MapRange() override;
        BOOL IsDynamic() override;
        uint64_t GetBufferDeviceAddress() override;

        // interface KGFX_BufferDx12
        BOOL Create(const KGfxBufferDesc& bufDesc, const void* pData, bool bDynamic);
        /**
         * 持有外部buf的一部分
         * @param pBufRes
         * @param offset 相对于外部buf起始的偏移量
         * @return
         */
        BOOL Create(KGFX_BufferImplDX12* pBufRes, uint32_t offset);
        BOOL Destroy();
        KGFX_BufferImplDX12* GetBufferImpl() const;

    private:
        bool DynamicBufUpdate(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, bool bReAlloc = true);
        bool StaticBufUpdate(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset);
        KGfxBufferDesc m_DynamicDesc = {};
        bool m_bDynamicBuf = false;
        KGFX_BufferImplDX12* m_Resource = nullptr;
        uint32_t m_uAllocId = 0;

        /**
         * 由于buf可能是一个外部大buf上的一部分，这个偏移量是buf在大buf上的偏移量
         */
        uint32_t m_DynamicBufOffset = 0;
        uint32_t m_DynamicBufSize = 0;
        uint32_t m_DynamicBufUpdateTime = 0;
    };
};

#endif
