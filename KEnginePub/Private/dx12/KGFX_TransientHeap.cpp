#include "KGFX_TransientHeap.h"
#include "KGFX_GraphiceDeviceDx12.h"

namespace gfx
{
    bool KGFX_TransientHeapDX12::Init(const TransientHeapDX12Desc& desc, KGFX_GraphicDevice* device, uint32_t viewHeapSize, uint32_t samplerHeapSize)
    {
        bool bRet = false;
        HRESULT hrResult = S_OK;
        m_CanResize = desc.allowResize;
        m_ViewHeapSize = viewHeapSize;
        m_SamplerHeapSize = samplerHeapSize;
        m_Device = device;

        m_D3dDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(device)->GetDXDevice();
        assert(m_D3dDevice);

        hrResult = m_D3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocator));
        KGLOG_COM_PROCESS_ERROR(hrResult);

        bRet = AllocateNewViewDescriptorHeap();
        KGLOG_PROCESS_ERROR(bRet);

        bRet = AllocateNewSamplerDescriptorHeap();
        KGLOG_PROCESS_ERROR(bRet);

        InitStagePool();

        bRet = true;
    Exit0:
        return bRet;
    }

    D3D12DescriptorHeap& KGFX_TransientHeapDX12::GetCurrentViewHeap() const
    {
        return *m_GPUViewHeaps.at(m_CurrentViewHeapIndex);
    }

    D3D12DescriptorHeap& KGFX_TransientHeapDX12::GetCurrentSamplerHeap() const
    {
        KGFX_GraphicDeviceDx12* pDevice = KGFX_GetGraphicDeviceDx12Internal();
        return  *pDevice->GetGPUSamplerHeap();
        //return *m_GPUSamplerHeaps.at(m_CurrentSamplerHeapIndex);
    }

    bool KGFX_TransientHeapDX12::AllocateTransientDescriptorTable(DescriptorType type, uint32_t count, uint32_t& outDescriptorOffset, void** outD3DDescriptorHeapHandle) const
    {
        auto& heap = type == DescriptorType::ResourceView ? GetCurrentViewHeap() : GetCurrentSamplerHeap();
        int allocResult = heap.Allocate(count);
        if (allocResult == -1)
        {
            return false;
        }
        outDescriptorOffset = allocResult;
        *outD3DDescriptorHeapHandle = heap.GetHeap();
        return true;
    }

    bool KGFX_TransientHeapDX12::CanResize() const
    {
        return m_CanResize;
    }

    bool KGFX_TransientHeapDX12::AllocateNewViewDescriptorHeap()
    {
        auto nextHeapIndex = m_CurrentViewHeapIndex + 1;
        if (nextHeapIndex < m_GPUViewHeaps.size())
        {
            m_GPUViewHeaps.at(nextHeapIndex)->DeallocateAll();
            m_CurrentViewHeapIndex = nextHeapIndex;
            return true;
        }

        RefPtr <D3D12DescriptorHeap> viewHeap(new D3D12DescriptorHeap());
        bool bRet = viewHeap->Init(m_ViewHeapSize, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        KGLOG_PROCESS_ERROR(bRet);

        m_CurrentViewHeapIndex = static_cast<int>(m_GPUViewHeaps.size());
        m_GPUViewHeaps.emplace_back(std::move(viewHeap));

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_TransientHeapDX12::AllocateNewSamplerDescriptorHeap()
    {
        auto nextHeapIndex = m_CurrentSamplerHeapIndex + 1;
        if (nextHeapIndex < m_GPUSamplerHeaps.size())
        {
            m_GPUSamplerHeaps[nextHeapIndex]->DeallocateAll();
            m_CurrentSamplerHeapIndex = nextHeapIndex;
            return true;
        }
        RefPtr<D3D12DescriptorHeap> samplerHeap(new D3D12DescriptorHeap());
        bool bRet = samplerHeap->Init(m_SamplerHeapSize, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        KGLOG_PROCESS_ERROR(bRet);
        m_CurrentSamplerHeapIndex = static_cast<int>(m_GPUSamplerHeaps.size());
        m_GPUSamplerHeaps.emplace_back(std::move(samplerHeap));

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_TransientHeapDX12::AllocateStagingBuffer(uint32_t size, KGFX_BufferImplDX12*& outBufferWeakPtr, uint32_t& offset, KGfxResourceAccessType memoryType, bool forceLargePage) const
    {
        switch (memoryType)
        {
        case KGfxResourceAccessType::KGfxResourceAccess_Read:
        {
            auto allocation = m_ReadbackBufferPool->Allocate(size, 0, forceLargePage);
            outBufferWeakPtr = allocation.resource;
            offset = allocation.offset;
        }
        break;
        case KGfxResourceAccessType::KGfxResourceAccess_Write:
        {
            auto allocation = m_UploadBufferPool->Allocate(size, 0, forceLargePage);
            outBufferWeakPtr = allocation.resource;
            offset = allocation.offset;
        }
        break;
        default:
            assert(false);
        }

        return true;
    }

    bool KGFX_TransientHeapDX12::AllocateConstBuffer(uint32_t size, KGFX_BufferImplDX12*& outBufferWeakPtr, uint32_t& offset, bool forceLargePage) const
    {
        KGFX_StagingBufferPoolDX12::Allocation allocation = m_ConstBufferPool->Allocate(size, 0, forceLargePage);
        outBufferWeakPtr = allocation.resource;
        offset = allocation.offset;

        return true;
    }

    bool KGFX_TransientHeapDX12::AllocateDynamicBuffer(uint32_t size, uint32_t stride, KGFX_BufferImplDX12*& outBufferWeakPtr, uint32_t& offset, bool forceLargePage) const
    {
        KGFX_StagingBufferPoolDX12::Allocation allocation = m_DynamicBufferPool->Allocate(size, stride, forceLargePage);
        outBufferWeakPtr = allocation.resource;
        offset = allocation.offset;

        return true;
    }

    ID3D12GraphicsCommandList* KGFX_TransientHeapDX12::CreateCmdList()
    {
        HRESULT hrResult = S_OK;
        if (m_CommandListAllocId < m_D3dCommandListPool.size())
        {
            ID3D12GraphicsCommandList* result = m_D3dCommandListPool[m_CommandListAllocId];
            result->Reset(m_CommandAllocator, nullptr);
            ++m_CommandListAllocId;
            return result;
        }
        else
        {
            CComPtr<ID3D12GraphicsCommandList> cmdList = nullptr;
            hrResult = m_D3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator, nullptr, IID_PPV_ARGS(&cmdList));
            assert(hrResult == S_OK);
            m_D3dCommandListPool.emplace_back(std::move(cmdList));
            ++m_CommandListAllocId;
            return m_D3dCommandListPool.back();
        }
    }

    void KGFX_TransientHeapDX12::Synchronize()
    {
        m_QueueWaitHandles.clear();
        for (auto& waitInfo : m_QueueWaitInfo)
        {
            auto currentFenceValue = waitInfo.fence->GetCompletedValue();

            if (currentFenceValue < waitInfo.waitValue)
            {
                waitInfo.fence->SetEventOnCompletion(waitInfo.waitValue, waitInfo.fenceEvent);
                m_QueueWaitHandles.emplace_back(waitInfo.fenceEvent);
            }
        }

        if (!m_QueueWaitHandles.empty())
        {
            WaitForMultipleObjects(static_cast<uint32_t>(m_QueueWaitHandles.size()), m_QueueWaitHandles.data(), TRUE, INFINITE);
        }
        m_QueueWaitHandles.clear();
    }

    bool KGFX_TransientHeapDX12::SynchronizeAndReset()
    {
        HRESULT hrResult = S_OK;
        bool bRet = false;
        Synchronize();

        m_CurrentViewHeapIndex = -1;
        m_CurrentSamplerHeapIndex = -1;
        AllocateNewViewDescriptorHeap();
        AllocateNewSamplerDescriptorHeap();

        m_CommandListAllocId = 0;
        m_LastSubmitCmdID = 0;
        hrResult = m_CommandAllocator->Reset();
        KGLOG_COM_PROCESS_ERROR(hrResult);

        if (m_UploadBufferPool)
        {
            m_UploadBufferPool->Reset();
        }

        if (m_ReadbackBufferPool)
        {
            m_ReadbackBufferPool->Reset();
        }

        if (m_ConstBufferPool)
        {
            m_ConstBufferPool->Reset();
        }

        if (m_DynamicBufferPool)
        {
            m_DynamicBufferPool->Reset();
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    void KGFX_TransientHeapDX12::Finish() const
    {
        for (auto& waitInfo : m_QueueWaitInfo)
        {
            if (waitInfo.waitValue == 0)
                continue;
            if (waitInfo.fence)
            {
                /// gpu打点，当
                HRESULT hresult = waitInfo.queue->Signal(waitInfo.fence, waitInfo.waitValue);
                assert(SUCCEEDED(hresult));
            }
        }
    }

    bool KGFX_TransientHeapDX12::CloseAndFinish()
    {
        //m_pCurrentCommandBuffer->CmdClose();

        //IKGFX_GraphicDevice* pKGFXGraphicDevice = KGFX_GetGraphicDevice();
       // auto windowContext = pKGFXGraphicDevice->GetContext();

        //windowContext->CmdExecuteCmdBuf(m_pCurrentCommandBuffer);

        Finish();
        return SynchronizeAndReset();
    }

    KGFX_TransientHeapDX12::QueueWaitInfo& KGFX_TransientHeapDX12::GetQueueWaitInfo(uint32_t queueIndex)
    {
        if (queueIndex < static_cast<uint32_t>(m_QueueWaitInfo.size()))
        {
            return m_QueueWaitInfo[queueIndex];
        }

        auto oldCount = m_QueueWaitInfo.size();
        m_QueueWaitInfo.resize(queueIndex + 1);

        for (auto i = oldCount; i < m_QueueWaitInfo.size(); i++)
        {
            m_QueueWaitInfo[i].waitValue = 0;
            m_QueueWaitInfo[i].fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        }
        return m_QueueWaitInfo[queueIndex];
    }

    std::vector<ID3D12CommandList*> KGFX_TransientHeapDX12::GetSubmitCmdVec()
    {
        std::vector<ID3D12CommandList*> resLists;
        uint32_t needSubCmdCount = m_CommandListAllocId - m_LastSubmitCmdID;
        assert(needSubCmdCount <= m_CommandListAllocId);
        resLists.reserve(needSubCmdCount);
        for (int i = m_LastSubmitCmdID; i < (int)m_CommandListAllocId; ++i)
        {
            resLists.emplace_back(m_D3dCommandListPool.at(i));
        }
        m_LastSubmitCmdID = m_CommandListAllocId;
        return resLists;
    }

    void KGFX_TransientHeapDX12::InitStagePool()
    {
        if (m_UploadBufferPool == nullptr)
        {
            m_UploadBufferPool = { new KGFX_StagingBufferPoolDX12(),{} };

            m_UploadBufferPool->Init(KGfxResourceAccessType::KGfxResourceAccess_Write, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "KGFX_StagingBufferPoolDX12");
        }

        if (m_ReadbackBufferPool == nullptr)
        {
            m_ReadbackBufferPool = { new KGFX_StagingBufferPoolDX12(),{} };

            m_ReadbackBufferPool->Init(KGfxResourceAccessType::KGfxResourceAccess_Read, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "KGFX_FeadBackBufferPoolDX12");
        }

        if (m_ConstBufferPool == nullptr)
        {
            m_ConstBufferPool = { new KGFX_StagingBufferPoolDX12(),{} };

            m_ConstBufferPool->Init(KGfxResourceAccessType::KGfxResourceAccess_Write, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "KGFX_ConstBufPoolDX12");
        }

        if (m_DynamicBufferPool == nullptr)
        {
            m_DynamicBufferPool = { new KGFX_StagingBufferPoolDX12(),{} };

            m_DynamicBufferPool->Init(KGfxResourceAccessType::KGfxResourceAccess_Write, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "KGFX_DynamicBufferPoolDX12");
        }
    }
}
