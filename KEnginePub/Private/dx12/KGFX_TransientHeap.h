#pragma once
#include <atlcomcli.h>
#include <vector>
#include "KGFX_DescriptorHeapDX12.h"
#include "KGFX_RefPtr.h"
#include "KGFX_StageResourcePool.h"

namespace gfx
{
    class KGFX_CommandBufferDX12Impl;
    class KGFX_GraphicDevice;

    enum class DescriptorType
    {
        ResourceView,
        Sampler
    };

    struct TransientHeapDX12Desc
    {
        bool allowResize;
        uint32_t constantBufferSize;
        uint32_t samplerDescriptorCount;
        uint32_t uavDescriptorCount;
        uint32_t srvDescriptorCount;
        uint32_t constantBufferDescriptorCount;
        uint32_t accelerationStructureDescriptorCount;
    };

    class KGFX_TransientHeapDX12 final :public KGfxRef
    {
        /**
         * 每个queue的waitValue
         */
        struct QueueWaitInfo
        {
            uint64_t waitValue = {};
            HANDLE fenceEvent = {};
            ID3D12CommandQueue* queue = {};
            ID3D12Fence* fence = nullptr;
        };

    public:
        KGFX_TransientHeapDX12() = default;
        ~KGFX_TransientHeapDX12()override = default;

        KGFX_TransientHeapDX12(const KGFX_TransientHeapDX12&) = delete;
        KGFX_TransientHeapDX12& operator=(const KGFX_TransientHeapDX12&) = delete;
        KGFX_TransientHeapDX12(const KGFX_TransientHeapDX12&&) = delete;
        KGFX_TransientHeapDX12& operator=(const KGFX_TransientHeapDX12&&) = delete;

        bool Init(const TransientHeapDX12Desc& desc, KGFX_GraphicDevice* device, uint32_t viewHeapSize, uint32_t samplerHeapSize);

        D3D12DescriptorHeap& GetCurrentViewHeap() const;

        D3D12DescriptorHeap& GetCurrentSamplerHeap() const;

        bool AllocateTransientDescriptorTable(DescriptorType type, uint32_t count, uint32_t& outDescriptorOffset, void** outD3DDescriptorHeapHandle) const;

        bool CanResize() const;

        bool AllocateNewViewDescriptorHeap();

        bool AllocateNewSamplerDescriptorHeap();

        bool AllocateStagingBuffer(uint32_t size, KGFX_BufferImplDX12*& outBufferWeakPtr, uint32_t& offset, KGfxResourceAccessType memoryType, bool forceLargePage = false) const;

        bool AllocateConstBuffer(uint32_t size, KGFX_BufferImplDX12*& outBufferWeakPtr, uint32_t& offset, bool forceLargePage = false) const;

        bool AllocateDynamicBuffer(uint32_t size, uint32_t stride, KGFX_BufferImplDX12*& outBufferWeakPtr, uint32_t& offset, bool forceLargePage = false) const;

        ID3D12GraphicsCommandList* CreateCmdList();

        void Synchronize();

        bool SynchronizeAndReset();

        void Finish() const;

        bool CloseAndFinish();

        QueueWaitInfo& GetQueueWaitInfo(uint32_t queueIndex);

        /**
         * 获取所有已经关闭准备提交的cmd列表，由于并不是所有的m_D3dCommandListPool内的cmd都已经关闭，所以要做区分
         * @return
         */
        std::vector<ID3D12CommandList*> GetSubmitCmdVec();

    private:
        void InitStagePool();

        CComPtr<ID3D12CommandAllocator> m_CommandAllocator = nullptr;

        KAutoRefPtr<KGFX_StagingBufferPoolDX12> m_UploadBufferPool = nullptr;
        KAutoRefPtr<KGFX_StagingBufferPoolDX12> m_ReadbackBufferPool = nullptr;
        KAutoRefPtr<KGFX_StagingBufferPoolDX12> m_ConstBufferPool = nullptr;
        KAutoRefPtr<KGFX_StagingBufferPoolDX12> m_DynamicBufferPool = nullptr;
        std::vector<CComPtr<ID3D12GraphicsCommandList>> m_D3dCommandListPool = {};

        uint32_t m_CommandListAllocId = 0;
        uint32_t m_LastSubmitCmdID = 0;
        /**
         * 如果存在多queue的提交
         */
        std::vector<QueueWaitInfo> m_QueueWaitInfo = {};

        std::vector<HANDLE> m_QueueWaitHandles = {};
        uint64_t m_QueueWaitFenceValue = {};

        std::vector<RefPtr<D3D12DescriptorHeap>> m_GPUViewHeaps = {}; // Cbv, Srv, Uav
        std::vector<RefPtr<D3D12DescriptorHeap>> m_GPUSamplerHeaps = {}; // Heap for samplers
        int m_CurrentViewHeapIndex = -1;
        int m_CurrentSamplerHeapIndex = -1;
        bool m_CanResize = false;

        uint32_t m_ViewHeapSize = 0;
        uint32_t m_SamplerHeapSize = 0;
        KGFX_GraphicDevice* m_Device = nullptr;
        ID3D12Device* m_D3dDevice = nullptr;
    };
}
