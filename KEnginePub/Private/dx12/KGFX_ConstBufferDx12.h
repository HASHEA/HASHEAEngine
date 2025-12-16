#pragma once
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    class KGFX_BufferViewDX12;
    class KGFX_BufferDx12;

    class KGFX_MemoryConstBufferDX12 final : public IKGFX_DynamicConstBuffer
    {
    public:
        KGFX_MemoryConstBufferDX12() = default;

        ~KGFX_MemoryConstBufferDX12() override
        {
            Uninit();
        }

        KGFX_MemoryConstBufferDX12(const KGFX_MemoryConstBufferDX12& other) = delete;
        KGFX_MemoryConstBufferDX12& operator=(const KGFX_MemoryConstBufferDX12&) = delete;

        KGFX_MemoryConstBufferDX12(KGFX_MemoryConstBufferDX12&& other) = delete;
        KGFX_MemoryConstBufferDX12& operator=(KGFX_MemoryConstBufferDX12&&) = delete;

        void Uninit()override;

        /**
         * 
         * @param bufSize 对齐之后的大小，所有cbuf都需要float4对齐
         * @param pcszBufferName
         * @return 
         */
        bool Init(uint32_t bufSize, const char* pcszBufferName = nullptr) override;

        void Update(IKGFX_RenderContext* commandBuffer) override;

        uint32_t GetCBufSize() const override;

        uint8_t* GetCpuData() override;

        IKGFX_BufferView* GetCBV() const override;
        IKGFX_Buffer* GetGfxBuffer() const override;
        void* MapRange() override;

    private:
        uint32_t m_BufSize = 0;
        std::vector<uint8_t> m_CpuData = {};
        KGFX_BufferDx12* m_GpuBuffer = nullptr;
        IKGFX_BufferView* m_CBV = nullptr;
    };
}
