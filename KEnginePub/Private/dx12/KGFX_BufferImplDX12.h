#pragma once
#include "KGFX_Dx12Header.h"
#include "KGFX_Dx12Healper.h"
#include "KGFX_ResourceLayoutTracker.h"
#include "DMA_2.0.0/D3D12MemAlloc.h"

namespace gfx
{
    class KGFX_GraphicDeviceDx12;
    /**
     * buffer在DX12的实现
     * 1、在DX12中如果是cbuf需要将对其设置到256，其他的VB IB等不需要对其设置
     * 2、由于DX12中可以将整个cbuf设置到root signature中，其需要GPUVirtualAddress，所以如果是靠偏移的cbuf就需要手动在获取GPU地址之后手动算偏移地址
     */
    class KGFX_BufferImplDX12 final : public  KGFX_DelayReleaseObject, public KGfxRef
    {
    public:
        friend class KGFX_BufferViewDX12;
        KGFX_BufferImplDX12() = default;
        ~KGFX_BufferImplDX12() override;
        KGFX_BufferImplDX12(const KGFX_BufferImplDX12&) = delete;
        KGFX_BufferImplDX12& operator=(const KGFX_BufferImplDX12&) = delete;
        KGFX_BufferImplDX12(KGFX_BufferImplDX12&&) = delete;
        KGFX_BufferImplDX12& operator=(KGFX_BufferImplDX12&&) = delete;

        bool Create(const KGfxBufferDesc& bufDesc);
        ID3D12Resource* GetBufResource() const;
        const KD3DX12_RESOURCE_DESC1& GetDXDesc() const;

        KGFX_ResourceLayoutTracker<true>& GetLayoutTracker();
        KGfxResourceAccessType GetCpuAccess() const;
        KGfxAccess GetDefaultLayout() const;

        uintptr_t GetNativeResourceHandle();
        void SetDebugName(const char* name);
        const char* GetDebugName() const;
        KGfxBufferDesc* GetDesc();

        D3D12Descriptor GetCBV(uint32_t offset, uint64_t range);
        D3D12Descriptor GetSRV(enumTextureFormat format, uint32_t stride, uint32_t offset, uint64_t range);
        D3D12Descriptor GetUAV(enumTextureFormat format, uint32_t stride, uint32_t offset, uint64_t range, ID3D12Resource* counter = nullptr);
        int32_t Release() override;

        void* MapCpuData();
        void UnMapCpuData();

    private:
        D3D12_SHADER_RESOURCE_VIEW_DESC ProcessBufSRVAlignmnet(KGFX_BufferViewDesc key) const;
        D3D12_UNORDERED_ACCESS_VIEW_DESC ProcessBufUAVAlignmnet(KGFX_BufferViewDesc key) const;

        std::string m_szName = {};
        bool m_bUAV = false;
        bool m_bUBO = false;
        bool m_bAccelerationStructure = false;
        void* m_CPUMapPoint = nullptr;
        ID3D12Resource* m_pBuffer = nullptr;
        D3D12MA::Allocation* m_DMAAllocation = nullptr;
        KD3DX12_RESOURCE_DESC1 m_DXDesc = {};
        KGfxBufferDesc m_GfxDesc = {};

        KGFX_ResourceLayoutTracker<true> m_ResLayoutTracker = KGFX_ResourceLayoutTracker<true>{ D3D12_RESOURCE_STATE_COMMON };
        KGfxAccess m_ResDefaultLayout = {};
        KGfxResourceAccessType m_BufferCPUAccesstype = KGfxResourceAccessType::KGfxResourceAccess_Write;
        KGFX_GraphicDeviceDx12* m_pGraphicDeviceWeakPtr = nullptr;
        struct KGfxBufferViewDescHash
        {
            size_t operator()(const KGFX_BufferViewDesc& key) const
            {
                size_t hash = 0;
                HashCombine(hash, key.eFormat);
                HashCombine(hash, key.uBytesOffset);
                HashCombine(hash, key.uBytesRange);
                HashCombine(hash, key.uStructureStride);
                return hash;
            }
        };

        std::unordered_map<KGFX_BufferViewDesc, D3D12Descriptor, KGfxBufferViewDescHash> m_SRVs = {};
        std::unordered_map<KGFX_BufferViewDesc, D3D12Descriptor, KGfxBufferViewDescHash> m_CBVs = {};
        std::unordered_map<KGFX_BufferViewDesc, D3D12Descriptor, KGfxBufferViewDescHash> m_UAVs = {};
    };

}
