#pragma once
#include "KGFX_CommandBufferDX12Impl.h"

namespace gfx
{
    /**
     * 对DX12指令队列的获取，其只负责分配出cmdbuf和提交cmdbuf
     * 分配出的cmdbuf的生命周期不由这个类管理，而是由调用者管理
     */
    class KGFX_CommandQueueDX12Impl final
    {
    public:
        KGFX_CommandQueueDX12Impl() = default;
        ~KGFX_CommandQueueDX12Impl();

        KGFX_CommandQueueDX12Impl(const KGFX_CommandQueueDX12Impl& other) = delete;
        KGFX_CommandQueueDX12Impl(const KGFX_CommandQueueDX12Impl&& other) = delete;

        KGFX_CommandQueueDX12Impl operator = (KGFX_CommandQueueDX12Impl& other) = delete;
        KGFX_CommandQueueDX12Impl operator = (KGFX_CommandQueueDX12Impl&& other) = delete;

        bool Create(D3D12_COMMAND_LIST_TYPE type, uint32_t queueIndex);
        D3D12_COMMAND_LIST_TYPE GetCommandQueueType() const;
  
        uint64_t ExecuteCommandList(KGFX_CommandBufferDX12Impl* commandList);
        uint64_t ExecuteCommandLists(const std::vector<KGFX_CommandBufferDX12Impl*>& commandLists);

        uint64_t GPUSignal();

        bool IsFenceComplete(uint64_t fenceValue) const;
        void CPUWaitForFenceValue(uint64_t fenceValue) const;
        void CPUWaitIdel() ;

        void GPUWaitOtherQueue(const KGFX_CommandQueueDX12Impl& other) const;
        void GPUWait(uint64_t fenceValue) const;

        ID3D12CommandQueue* GetD3D12CommandQueue() const;

       uint64_t GetCurrentFenceValue() const;

       ID3D12Fence* GetD3D12Fence() const;

    private:

        D3D12_COMMAND_LIST_TYPE m_CommandListType = {};
        ID3D12CommandQueue* m_D3d12CommandQueue = nullptr;
        ID3D12Fence* m_D3d12Fence = nullptr;
        std::atomic_uint64_t m_FenceValue = {};
        HANDLE m_GlobalWaitHandle = nullptr;
        uint32_t m_QueueIndex = 0;
    };
}
