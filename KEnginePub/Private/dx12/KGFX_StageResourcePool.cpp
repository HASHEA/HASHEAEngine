#include "KGFX_StageResourcePool.h"
#include "KGFX_BufferImplDX12.h"

namespace gfx
{
    void KGFX_StagingBufferPoolDX12::Init(KGfxResourceAccessType memoryType, uint32_t alignment, std::string debugName)
    {
        m_MemoryType = memoryType;
        m_Alignment = alignment;
        assert(m_Alignment % 16 == 0);
#ifdef _DEBUG
        m_DebugName = std::move(debugName);
#endif
    }

    uint32_t KGFX_StagingBufferPoolDX12::AlignUp(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    void KGFX_StagingBufferPoolDX12::Reset()
    {
        m_PageAllocCounter = 0;
        m_OffsetAllocCounter = 0;
        m_LargeAllocations.clear();
    }

    bool KGFX_StagingBufferPoolDX12::NewStagingBufferPage()
    {
        bool bRet = false;
        StagingBufferPage page = {};
        uint32_t pageSize = m_StagingBufferDefaultPageSize;

        KAutoRefPtr <KGFX_BufferImplDX12> bufferPtr = { new KGFX_BufferImplDX12(),{} };
        KGfxBufferDesc bufferDesc = {};
        bufferDesc.uByteWidth = pageSize;
        bufferDesc.uUsageFlags = BUFFER_USAGE_TRANSFER_SRC_BIT | BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferDesc.eResAccessFlags = m_MemoryType;
        bRet = bufferPtr->Create(bufferDesc);
        KGLOG_PROCESS_ERROR(bRet);
#ifdef _DEBUG
        {
            static int index = 0;
            std::string debugName = m_DebugName + "::PageBuffer_" + std::to_string(m_StagingBufIndex++) + std::to_string(index++);
            bufferPtr->SetDebugName(debugName.c_str());
        }
#endif
        bufferPtr->MapCpuData();
        page.resource = std::move(bufferPtr);
        page.size = pageSize;
        m_Pages.emplace_back(page);

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_StagingBufferPoolDX12::NewLargeBuffer(uint32_t size)
    {
        bool bRet = false;

        KAutoRefPtr<KGFX_BufferImplDX12> bufferPtr = { new KGFX_BufferImplDX12(),{} };
        KGfxBufferDesc bufferDesc = {};
        bufferDesc.uByteWidth = size;
        bufferDesc.uUsageFlags = BUFFER_USAGE_TRANSFER_SRC_BIT | BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferDesc.eResAccessFlags = m_MemoryType;
        bRet = bufferPtr->Create(bufferDesc);
        KGLOG_PROCESS_ERROR(bRet);
#ifdef _DEBUG
        {
            static int index = 0;
            std::string debugName = m_DebugName + "::LargeBuffer_" + std::to_string(m_LargeBufIndex++) + std::to_string(index++);
            bufferPtr->SetDebugName(debugName.c_str());
        }
#endif
        bufferPtr->MapCpuData();
        m_LargeAllocations.emplace_back(std::move(bufferPtr));
        bRet = true;
    Exit0:
        return bRet;
    }

    KGFX_StagingBufferPoolDX12::Allocation KGFX_StagingBufferPoolDX12::Allocate(uint32_t size, uint32_t stride, bool forceLargePage)
    {
        bool bRes = false;
        if (forceLargePage || size >= m_StagingBufferDefaultPageSize >> 2)
        {
            bRes = NewLargeBuffer(size);
            assert(bRes);

            Allocation result;
            result.resource = m_LargeAllocations.back().Get();
            result.offset = 0;
            return result;
        }
        uint32_t alignedSize = stride == 0 ? m_Alignment : stride;
        //assert(alignedSize % D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT == 0);
        uint32_t bufferAllocOffset = AlignUp(m_OffsetAllocCounter, alignedSize);
        int bufferId = -1;
        for (int i = m_PageAllocCounter; i < m_Pages.size(); i++)
        {
            auto cb = m_Pages[i].resource;
            if (bufferAllocOffset + size <= cb->GetDXDesc().Width)
            {
                bufferId = i;
                break;
            }
            bufferAllocOffset = 0;
        }

        if (bufferId == -1)
        {
            bRes = NewStagingBufferPage();
            assert(bRes);

            bufferId = static_cast<int>(m_Pages.size() - 1);
        }

        Allocation result = {};
        result.resource = m_Pages[bufferId].resource.Get();
        result.offset = bufferAllocOffset;

        m_PageAllocCounter = bufferId;
        m_OffsetAllocCounter = bufferAllocOffset + size;
        return result;
    }
}
