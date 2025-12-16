#pragma once
#include <vector>
#include "KEnginePub/Public/IGFX_Public.h"


namespace gfx
{
    class KGFX_GraphicDevice;
    class KGFX_BufferImplDX12;
}

namespace gfx
{
    class KGFX_StagingBufferPoolDX12 final :public KGfxRef
    {
    public:
        KGFX_StagingBufferPoolDX12() = default;
        KGFX_StagingBufferPoolDX12(const KGFX_StagingBufferPoolDX12&) = delete;
        KGFX_StagingBufferPoolDX12& operator=(const KGFX_StagingBufferPoolDX12&) = delete;

        KGFX_StagingBufferPoolDX12(const KGFX_StagingBufferPoolDX12&&) = delete;
        KGFX_StagingBufferPoolDX12& operator=(const KGFX_StagingBufferPoolDX12&&) = delete;

        ~KGFX_StagingBufferPoolDX12()override = default;

        struct StagingBufferPage
        {
            KAutoRefPtr<KGFX_BufferImplDX12> resource;
            uint32_t size;
        };

        struct Allocation
        {
            KGFX_BufferImplDX12* resource;
            uint32_t offset;
        };

        void Init(KGfxResourceAccessType memoryType, uint32_t alignment, std::string debugName);

        static uint32_t AlignUp(uint32_t value, uint32_t alignment);

        void Reset();

        bool NewStagingBufferPage();

        bool NewLargeBuffer(uint32_t size);

        Allocation Allocate(uint32_t size, uint32_t stride, bool forceLargePage);

    private:
        KGfxResourceAccessType m_MemoryType = {};
        uint32_t m_Alignment = 0;

        std::vector<StagingBufferPage> m_Pages = {};
        std::vector<KAutoRefPtr<KGFX_BufferImplDX12>> m_LargeAllocations = {};

        int m_PageAllocCounter = 0;
        uint32_t m_OffsetAllocCounter = 0;

        uint32_t m_StagingBufferDefaultPageSize = 16 * 1024 * 1024;
#ifdef _DEBUG
        uint32_t m_StagingBufIndex = 0;
        uint32_t m_LargeBufIndex = 0;
        std::string m_DebugName = {};
#endif
    };
}
