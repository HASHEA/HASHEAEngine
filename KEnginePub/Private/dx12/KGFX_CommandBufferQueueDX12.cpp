#include "KGFX_CommandBufferQueueDX12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_TransientHeap.h"

namespace gfx
{
    KGFX_CommandQueueDX12Impl::~KGFX_CommandQueueDX12Impl()
    {
        SAFE_RELEASE(m_D3d12CommandQueue);
        SAFE_RELEASE(m_D3d12Fence);
        m_FenceValue = 0;
    }

    bool KGFX_CommandQueueDX12Impl::Create(D3D12_COMMAND_LIST_TYPE type, uint32_t queueIndex)
    {
        HRESULT hresult = E_FAIL;
        m_QueueIndex = queueIndex;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        m_CommandListType = type;
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = type;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 0;

        hresult = pD3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_D3d12CommandQueue));
        KGLOG_COM_PROCESS_ERROR(hresult);

        hresult = (pD3dDevice->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3d12Fence)));
        KGLOG_COM_PROCESS_ERROR(hresult);

        switch (type)
        {
        case D3D12_COMMAND_LIST_TYPE_COPY:
            m_D3d12CommandQueue->SetName(L"Copy Command Queue");
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            m_D3d12CommandQueue->SetName(L"Compute Command Queue");
            break;
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            m_D3d12CommandQueue->SetName(L"Direct Command Queue");
            break;
        default:
            assert(false);		/// 暂时先不支持其他类型的命令队列
        }

        return true;

    Exit0:
        SAFE_RELEASE(m_D3d12CommandQueue);
        SAFE_RELEASE(m_D3d12Fence);
        return false;
    }

    D3D12_COMMAND_LIST_TYPE KGFX_CommandQueueDX12Impl::GetCommandQueueType() const
    {
        return m_CommandListType;
    }

    uint64_t KGFX_CommandQueueDX12Impl::ExecuteCommandList(KGFX_CommandBufferDX12Impl* commandList)
    {
        assert(commandList);
        auto subCommandLists = commandList->GetSubmitCmdLists();
        if (!subCommandLists.empty())
        {
            m_D3d12CommandQueue->ExecuteCommandLists(static_cast<uint32_t>(subCommandLists.size()), subCommandLists.data());

            ++m_FenceValue;

            KGFX_CommandBufferDX12Impl* cmdImpl = commandList;
            auto transientHeap = cmdImpl->GetUsedTransientHeap();
            auto& [waitValue, fenceEvent, queue, fence] = transientHeap->GetQueueWaitInfo(m_QueueIndex);
            waitValue = m_FenceValue;
            fence = m_D3d12Fence;
            queue = m_D3d12CommandQueue;

            return m_FenceValue;
        }
        return {};
    }

    uint64_t KGFX_CommandQueueDX12Impl::ExecuteCommandLists(const std::vector<KGFX_CommandBufferDX12Impl*>& commandLists)
    {
        std::vector<ID3D12CommandList*> subCommandLists = {};
        uint32_t count = static_cast<uint32_t>(commandLists.size());
        for (uint32_t i = 0; i < count; i++)
        {
            auto cmdImpl = commandLists.at(i)->GetD3D12CommandList();
            subCommandLists.emplace_back(cmdImpl);
        }

        if (count > 0)
        {
            m_D3d12CommandQueue->ExecuteCommandLists(count, subCommandLists.data());

            ++m_FenceValue;

            for (uint32_t i = 0; i < count; i++)
            {
                if (i > 0 && commandLists[i] == commandLists[i - 1])
                    continue;

                KGFX_CommandBufferDX12Impl* cmdImpl = (commandLists[i]);
                auto transientHeap = cmdImpl->GetUsedTransientHeap();
                auto& [waitValue, fenceEvent, queue, fence] = transientHeap->GetQueueWaitInfo(m_QueueIndex);
                waitValue = m_FenceValue;
                fence = m_D3d12Fence;
                queue = m_D3d12CommandQueue;
            }
        }

        return m_FenceValue;
    }

    uint64_t KGFX_CommandQueueDX12Impl::GPUSignal()
    {
        uint64_t fenceValue = ++m_FenceValue;
        m_D3d12CommandQueue->Signal(m_D3d12Fence, fenceValue);
        return fenceValue;
    }

    bool KGFX_CommandQueueDX12Impl::IsFenceComplete(uint64_t fenceValue) const
    {
        return m_D3d12Fence->GetCompletedValue() > fenceValue;
    }

    void KGFX_CommandQueueDX12Impl::CPUWaitForFenceValue(uint64_t fenceValue) const
    {
        /// CPU的wait fence操作
        /// 如果当前的fence没有达到目标值，那么就创建一个event然后等待这个event，让cpu空闲
        if (!IsFenceComplete(fenceValue))
        {
            auto event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (event)
            {
                /// 当gpu执行完fenceValue对应的command list之后，会触发这个event
                m_D3d12Fence->SetEventOnCompletion(fenceValue, event);
                ::WaitForSingleObject(event, UINT32_MAX);

                ::CloseHandle(event);
            }
        }
    }

    void KGFX_CommandQueueDX12Impl::CPUWaitIdel()
    {
        ++m_FenceValue;
        m_D3d12CommandQueue->Signal(m_D3d12Fence, m_FenceValue);
        auto event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (event)
        {
            /// 当gpu执行完fenceValue对应的command list之后，会触发这个event
            m_D3d12Fence->SetEventOnCompletion(m_FenceValue, event);
            ::WaitForSingleObject(event, UINT32_MAX);

            ::CloseHandle(event);
        }
    }

    void KGFX_CommandQueueDX12Impl::GPUWaitOtherQueue(const KGFX_CommandQueueDX12Impl& other) const
    {
        /// GPU的wait fence操作，
        m_D3d12CommandQueue->Wait(other.m_D3d12Fence, other.m_FenceValue);
    }

    void KGFX_CommandQueueDX12Impl::GPUWait(uint64_t fenceValue) const
    {
        /// GPU的wait fence操作，
        m_D3d12CommandQueue->Wait(m_D3d12Fence, fenceValue);
    }

    ID3D12CommandQueue* KGFX_CommandQueueDX12Impl::GetD3D12CommandQueue() const
    {
        return m_D3d12CommandQueue;
    }

    uint64_t KGFX_CommandQueueDX12Impl::GetCurrentFenceValue() const
    {
        return m_FenceValue;
    }

    ID3D12Fence* KGFX_CommandQueueDX12Impl::GetD3D12Fence() const
    {
        return m_D3d12Fence;
    }

}
