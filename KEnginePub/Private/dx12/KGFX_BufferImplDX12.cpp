// ReSharper disable CppClangTidyCppcoreguidelinesProTypeStaticCastDowncast
#include "KGFX_BufferImplDX12.h"

#include "KGFX_DeviceDumpDred.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "Engine/Utf8AndWideChar.h"

namespace gfx
{
    KGFX_BufferImplDX12::~KGFX_BufferImplDX12()
    {
        UnMapCpuData();
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());

        //SAFE_RELEASE(m_pBuffer);
        //SAFE_RELEASE(m_DMAAllocation);




        for (auto& cbv : m_CBVs)
        {
            if (cbv.second)
            {
                pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Free(cbv.second);
            }
        }
        m_CBVs.clear();

        for (auto& srv : m_SRVs)
        {
            if (srv.second)
            {
                pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Free(srv.second);
            }
        }
        m_SRVs.clear();

        for (auto& uav : m_UAVs)
        {
            if (uav.second)
            {
                pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Free(uav.second);
            }
        }
        m_UAVs.clear();

        SAFE_RELEASE(m_pBuffer);
        SAFE_RELEASE(m_DMAAllocation);
    }

    bool KGFX_BufferImplDX12::Create(const KGfxBufferDesc& bufDesc)
    {
        HRESULT hResult = E_FAIL;
        KGFX_GraphicDeviceDx12* pGraphicDevice = (KGFX_GetGraphicDeviceDx12Internal());
        m_pGraphicDeviceWeakPtr = pGraphicDevice;
        D3D12MA::Allocator* pAllocator = pGraphicDevice->GetDX12Allocator();
        m_GfxDesc = bufDesc;

        D3D12MA::ALLOCATION_DESC allocationDesc = {};
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        m_BufferCPUAccesstype = bufDesc.eResAccessFlags;

        allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        m_ResDefaultLayout = KGfxAccess::Unknown;
        bool bAuto = is_set(static_cast<KGfxResourceMiscFlag>(m_GfxDesc.eMiscFlags), KGfxResourceMiscFlag::AutoLayoutTransition);

        switch (bufDesc.eResAccessFlags)
        {
        case KGfxResourceAccessType::KGfxResourceAccess_GPUOnly:
            allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
            initialState = D3D12_RESOURCE_STATE_COMMON;//GetDxResourceLayout(bufDesc.uUsageFlags);
            m_ResDefaultLayout = KGfxAccess::SRVMask;
            break;
        case KGfxResourceAccessType::KGfxResourceAccess_Read:
            allocationDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
            m_ResDefaultLayout = KGfxAccess::CopySrc;
            break;
        case KGfxResourceAccessType::KGfxResourceAccess_Write:
            allocationDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
            m_ResDefaultLayout = KGfxAccess::CopyDst;
            break;
        default:
            break;
        }

        if ((bufDesc.uUsageFlags & BUFFER_USAGE_UNIFORM_BUFFER_BIT) > 0)
        {
            m_bUBO = true;
            m_ResDefaultLayout = KGfxAccess::ConstBuffer;
        }

        /// 由于使用DMA来分配内存，所以对齐Alignment这个值不需要填写，DMA会自己设置
        if (m_bUBO)
        {
            m_DXDesc = KD3DX12_RESOURCE_DESC1::Buffer(STEPIFY(bufDesc.uByteWidth, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
        }
        else
        {
            /// 对于RawBuffer，必须16字节对齐，大小需要向上取整
            m_DXDesc = KD3DX12_RESOURCE_DESC1::Buffer(STEPIFY(bufDesc.uByteWidth, D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT));
        }

        if ((bufDesc.uUsageFlags & BUFFER_USAGE_STORAGE_BUFFER_BIT || bufDesc.uUsageFlags & BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) && (bufDesc.eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly))
        {
            m_bUAV = true;
            m_DXDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        if ((bufDesc.uUsageFlags & BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) > 0)
        {
            initialState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
            m_bAccelerationStructure = true;
        }

        assert(m_DXDesc.Width % 16 == 0);

        hResult = pAllocator->CreateResource2(&allocationDesc, &m_DXDesc, initialState, nullptr, &m_DMAAllocation, IID_PPV_ARGS(&m_pBuffer));
        if (hResult == E_NOTIMPL && bufDesc.eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_Write)
        {
            allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
            hResult = pAllocator->CreateResource2(&allocationDesc, &m_DXDesc, initialState, nullptr, &m_DMAAllocation, IID_PPV_ARGS(&m_pBuffer));
        }
        KGLOG_COM_ASSERT_EXIT(hResult);
        m_ResLayoutTracker = KGFX_ResourceLayoutTracker<true>(initialState, bAuto);

        return true;
    Exit0:
        SAFE_RELEASE(m_DMAAllocation);
        SAFE_RELEASE(m_pBuffer);
        return false;
    }

    ID3D12Resource* KGFX_BufferImplDX12::GetBufResource() const
    {
        return m_pBuffer;
    }

    const KD3DX12_RESOURCE_DESC1& KGFX_BufferImplDX12::GetDXDesc() const
    {
        return m_DXDesc;
    }


    KGFX_ResourceLayoutTracker<true>& KGFX_BufferImplDX12::GetLayoutTracker()
    {
        return m_ResLayoutTracker;
    }

    KGfxResourceAccessType KGFX_BufferImplDX12::GetCpuAccess() const
    {
        return m_BufferCPUAccesstype;
    }

    KGfxAccess KGFX_BufferImplDX12::GetDefaultLayout() const
    {
        return m_ResDefaultLayout;
    }

    uintptr_t KGFX_BufferImplDX12::GetNativeResourceHandle()
    {
        return reinterpret_cast<uintptr_t>(m_pBuffer);
    }

    void KGFX_BufferImplDX12::SetDebugName(const char* name)
    {
        if (m_pBuffer)
        {
            WCHAR buff[128] = {};
            Utf8ToWideChar(buff, 128, name);
            m_pBuffer->SetName(buff);
            m_DMAAllocation->SetName(buff);
        }
        m_szName = name;
    }

    const char* KGFX_BufferImplDX12::GetDebugName() const
    {
        return m_szName.c_str();
    }

    KGfxBufferDesc* KGFX_BufferImplDX12::GetDesc()
    {
        return &m_GfxDesc;
    }

    D3D12Descriptor KGFX_BufferImplDX12::GetCBV(uint32_t offset, uint64_t range)
    {
        if (!m_bUBO)
        {
            /// 当前buf不可以为ubo，检查创建流程
            assert(false);
            return {};
        }

        ID3D12Device* pD3dDevice = m_pGraphicDeviceWeakPtr->GetDXDevice();

        KGFX_BufferViewDesc key = {};
        key.eFormat = TEX_FORMAT_NONE;
        key.uBytesOffset = offset;
        key.uBytesRange = range == 0 ? static_cast<uint32_t>(m_pBuffer->GetDesc().Width) - offset : static_cast<uint32_t>(range);

        D3D12Descriptor& allocation = m_CBVs[key];
        if (allocation)
            return allocation;

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbufDesc = {};
        cbufDesc.SizeInBytes = STEPIFY(static_cast<uint32_t>(key.uBytesRange), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        cbufDesc.BufferLocation = m_pBuffer->GetGPUVirtualAddress() + offset;


        m_pGraphicDeviceWeakPtr->GetDX12SRVAndUAVAndCBVHeap()->Allocate(&allocation);
        pD3dDevice->CreateConstantBufferView(&cbufDesc, allocation.cpuHandle);
        return allocation;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC KGFX_BufferImplDX12::ProcessBufUAVAlignmnet(KGFX_BufferViewDesc key) const
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        viewDesc.Format = GetTexToDxFormat(key.eFormat, false);

        /// structureBuf和typeBuf
        if (key.uStructureStride)
        {
            assert(key.uStructureStride != 0 && key.uBytesOffset % key.uStructureStride == 0);
            assert(key.uStructureStride % 4 == 0);

            viewDesc.Buffer.FirstElement = key.uBytesOffset / key.uStructureStride;
            viewDesc.Buffer.NumElements = static_cast<uint32_t>(key.uBytesRange) / key.uStructureStride;
            viewDesc.Buffer.StructureByteStride = key.uStructureStride;
        }
        else if (key.eFormat == TEX_FORMAT_NONE)
        {
            /// 偏移是底层无法处理的，如果触发此处需要使用者去梳理逻辑
            assert(key.uBytesOffset % D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT == 0);

            /// 对于大小我可以向16字节对齐，向上取整
            //assert(key.uBytesRange % D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT == 0);
            STEPIFY(key.uBytesRange, D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT);

            viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            viewDesc.Buffer.FirstElement = key.uBytesOffset / 4;
            viewDesc.Buffer.NumElements = static_cast<uint32_t>(key.uBytesRange) / 4;
            viewDesc.Buffer.StructureByteStride = 0;
            viewDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
        }
        else
        {
            TextureFormatInfo formatInfo = GetDX12FormatInfo(viewDesc.Format);
            viewDesc.Buffer.FirstElement = key.uBytesOffset / formatInfo.uBytesPerBlock;
            viewDesc.Buffer.NumElements = static_cast<uint32_t>(key.uBytesRange) / formatInfo.uBytesPerBlock;
            assert(key.uBytesRange % formatInfo.uBytesPerBlock == 0);
        }
        return viewDesc;
    }


    D3D12_SHADER_RESOURCE_VIEW_DESC KGFX_BufferImplDX12::ProcessBufSRVAlignmnet(KGFX_BufferViewDesc key) const
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        viewDesc.Format = GetTexToDxFormat(key.eFormat, false);
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        /// structureBuf
        if (key.uStructureStride)
        {
            assert(key.uStructureStride != 0 && key.uBytesOffset % key.uStructureStride == 0);
            assert(key.uStructureStride % 4 == 0);

            viewDesc.Buffer.FirstElement = key.uBytesOffset / key.uStructureStride;
            viewDesc.Buffer.NumElements = static_cast<uint32_t>(key.uBytesRange) / key.uStructureStride;
            viewDesc.Buffer.StructureByteStride = key.uStructureStride;
        }
        else if (key.eFormat == TEX_FORMAT_NONE)    /// raw buf
        {
            /// 偏移是底层无法处理的，如果触发此处需要使用者去梳理逻辑
            assert(key.uBytesOffset % D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT == 0);

            /// 对于大小我可以向16字节对齐，向上取整
            //assert(key.uBytesRange % D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT == 0);
            STEPIFY(key.uBytesRange, D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT);

            viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            viewDesc.Buffer.FirstElement = key.uBytesOffset / 4;
            viewDesc.Buffer.NumElements = static_cast<uint32_t>(key.uBytesRange) / 4;
            viewDesc.Buffer.StructureByteStride = 0;
            viewDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
        }
        else /// typed buf
        {
            TextureFormatInfo formatInfo = GetDX12FormatInfo(viewDesc.Format);
            viewDesc.Buffer.FirstElement = key.uBytesOffset / formatInfo.uBytesPerBlock;
            viewDesc.Buffer.NumElements = static_cast<uint32_t>(key.uBytesRange) / formatInfo.uBytesPerBlock;
            assert(key.uBytesRange % formatInfo.uBytesPerBlock == 0);
        }
        return viewDesc;
    }



    D3D12Descriptor KGFX_BufferImplDX12::GetSRV(enumTextureFormat format, uint32_t stride, uint32_t offset, uint64_t range)
    {
        ID3D12Device* pD3dDevice = m_pGraphicDeviceWeakPtr->GetDXDevice();

        KGFX_BufferViewDesc key = {};
        key.eFormat = format;
        key.uBytesOffset = offset;
        key.uBytesRange = range == 0 ? static_cast<uint32_t>(m_pBuffer->GetDesc().Width) - offset : static_cast<uint32_t>(range);
        key.uStructureStride = stride;

        D3D12Descriptor& allocation = m_SRVs[key];
        if (allocation)
            return allocation;

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = ProcessBufSRVAlignmnet(key);
        m_pGraphicDeviceWeakPtr->GetDX12SRVAndUAVAndCBVHeap()->Allocate(&allocation);

        if (m_bAccelerationStructure)
        {
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            viewDesc.Format = DXGI_FORMAT_UNKNOWN;
            viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            viewDesc.RaytracingAccelerationStructure.Location = m_pBuffer->GetGPUVirtualAddress();
            pD3dDevice->CreateShaderResourceView(nullptr, &viewDesc, allocation.cpuHandle);
        }
        else
        {
            pD3dDevice->CreateShaderResourceView(m_pBuffer, &viewDesc, allocation.cpuHandle);
        }

        return allocation;
    }

    D3D12Descriptor KGFX_BufferImplDX12::GetUAV(enumTextureFormat format, uint32_t stride, uint32_t offset, uint64_t range, ID3D12Resource* counter)
    {
        if (!m_bUAV)
        {
            /// 当前buf不可以为uav，检查创建流程
            assert(false);
            return {};
        }

        ID3D12Device* pD3dDevice = m_pGraphicDeviceWeakPtr->GetDXDevice();
        KGFX_BufferViewDesc key = {};
        key.eFormat = format;
        key.uBytesOffset = offset;
        key.uBytesRange = range == 0 ? static_cast<uint32_t>(m_pBuffer->GetDesc().Width) - offset : static_cast<uint32_t>(range);
        key.uStructureStride = stride;

        D3D12Descriptor& allocation = m_UAVs[key];
        if (allocation)
            return allocation;

        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = ProcessBufUAVAlignmnet(key);
        m_pGraphicDeviceWeakPtr->GetDX12SRVAndUAVAndCBVHeap()->Allocate(&allocation);
        ID3D12Resource* counterResource = counter;
        pD3dDevice->CreateUnorderedAccessView(m_pBuffer, counterResource, &viewDesc, allocation.cpuHandle);

        return allocation;
    }

    int32_t KGFX_BufferImplDX12::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_pBuffer)
            {
                auto piDevice = KGFX_GetGraphicDeviceDx12Internal();
                CHECK_ASSERT(piDevice);

                piDevice->GC_DelayReleaseObject(this);
            }
            else
            {
                // 如果没有创建过纹理，直接释放
                delete this;
            }
        }

        return nRef;
    }

    void* KGFX_BufferImplDX12::MapCpuData()
    {
        HRESULT hresult = S_OK;
        if (!m_CPUMapPoint)
        {
            hresult = m_pBuffer->Map(0, nullptr, &m_CPUMapPoint);
            assert(hresult == S_OK);
            CheckDeviceRemoveReason(hresult);
        }
        return m_CPUMapPoint;
    }

    void KGFX_BufferImplDX12::UnMapCpuData()
    {
        if (m_CPUMapPoint)
        {
            m_pBuffer->Unmap(0, nullptr);
            m_CPUMapPoint = nullptr;
        }
    }


}
