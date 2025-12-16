#include "KGFX_TextureImplDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "Engine/Utf8AndWideChar.h"

namespace gfx
{
    KGFX_TextureImplDx12::~KGFX_TextureImplDx12()
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        for (auto& srv : m_SRVS)
        {
            if (srv.second)
            {
                pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Free(srv.second);
            }
        }
        m_SRVS.clear();

        for (auto& rtv : m_RTVS)
        {
            if (rtv.second)
            {
                pGraphicDevice->GetDX12RTVHeap()->Free(rtv.second);
            }
        }
        m_RTVS.clear();

        for (auto& dsv : m_DSVS)
        {
            if (dsv.second)
            {
                pGraphicDevice->GetDX12DSVHeap()->Free(dsv.second);
            }
        }
        m_DSVS.clear();

        for (auto& uav : m_UAVS)
        {
            if (uav.second)
            {
                pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Free(uav.second);
            }
        }
        m_UAVS.clear();

        SAFE_RELEASE(m_Resource);
        SAFE_RELEASE(m_DMAAllocation);
    }

    int32_t KGFX_TextureImplDx12::AddRef()
    {
        return KGfxRef::AddRef();
    }

    int32_t KGFX_TextureImplDx12::GetRef()
    {
        return KGfxRef::GetRef();
    }

    int32_t KGFX_TextureImplDx12::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_Resource && !m_bForSwapChain)
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

    uint32_t KGFX_TextureImplDx12::GetWidth() const
    {
        return static_cast<uint32_t>(m_Desc.Width);
    }

    uint32_t KGFX_TextureImplDx12::GetHeight() const
    {
        return m_Desc.Height;
    }


    TextureType KGFX_TextureImplDx12::GetTextureType() const
    {
        switch (m_Desc.Dimension)
        {
        case D3D12_RESOURCE_DIMENSION_UNKNOWN:
            assert(false);
            break;
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            assert(false);
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
            if (m_Desc.ArraySize() > 1)
            {
                return TextureType::Texture1DArray;
            }
            return TextureType::Texture1D;
        }
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
            if (m_bCubeTex && m_Desc.ArraySize() > 6)
            {
                return TextureType::CubemapArray;
            }
            if (m_bCubeTex)
            {
                return TextureType::Cubemap;
            }

            if (m_Desc.ArraySize() > 1)
            {
                return TextureType::Texture2DArray;
            }
            return TextureType::Texture2D;
        }
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            return TextureType::Texture3D;
        default:;
        }

        return TextureType::Count;
    }

    bool KGFX_TextureImplDx12::IsForDepth() const
    {
        return m_CanbDepthTex;
    }

    bool KGFX_TextureImplDx12::IsHasStencil() const
    {
        return m_CanbStencilTex;
    }

    uint32_t KGFX_TextureImplDx12::GetMipMapCount() const
    {
        return m_Desc.MipLevels;
    }

    KGfxSubresourceRange KGFX_TextureImplDx12::ResolveSubresourceRange(const KGfxSubresourceRange& range) const
    {
        KGfxSubresourceRange resolved = range;
        resolved.uBaseMipLevel = std::min<uint32_t>(resolved.uBaseMipLevel, m_Desc.MipLevels);
        resolved.uMipCount = std::min(resolved.uMipCount, m_Desc.MipLevels - resolved.uBaseMipLevel);
        uint32_t arrayLayerCount = m_Desc.ArraySize() * (m_bCubeTex ? 6 : 1);	/// cubeArray的贴图一层是6个面
        resolved.uBaseArraySlice = std::min<uint32_t>(resolved.uBaseArraySlice, arrayLayerCount);
        resolved.uArrayCount = std::min<uint32_t>(resolved.uArrayCount, arrayLayerCount - resolved.uBaseArraySlice);

        return resolved;
    }

    KGFX_ResourceLayoutTracker<false>& KGFX_TextureImplDx12::GetLayoutTracker()
    {
        return m_ResourceLayout;
    }

    KGfxAccess KGFX_TextureImplDx12::GetDefaultLayout() const
    {
        return m_ResDefaultLayout;
    }

    const KD3DX12_RESOURCE_DESC1& KGFX_TextureImplDx12::GetDXResourceDesc() const
    {
        return m_Desc;
    }


    D3D12Descriptor KGFX_TextureImplDx12::GetSRV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect)
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        KGFX_TextureViewDesc key = { KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV,format, type, aspect, range };
        D3D12Descriptor& descriptor = m_SRVS[key];
        if (descriptor)
            return descriptor;

        bool isArray = m_Desc.ArraySize() > 1;
        bool isMultiSample = m_Desc.SampleDesc.Count > 1;

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetTexToDxFormat(format, m_TextureGfxDesc.bSRGB);
        viewDesc.Format = GetTexFormatToSRVFormat(viewDesc.Format);
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        switch (type)
        {
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D:
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY:
            if (isArray)
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.MostDetailedMip = range.uBaseMipLevel;
                viewDesc.Texture1DArray.MipLevels = range.uMipCount;
                viewDesc.Texture1DArray.FirstArraySlice = range.uBaseArraySlice;
                viewDesc.Texture1DArray.ArraySize = range.uArrayCount;
            }
            else
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MostDetailedMip = range.uBaseMipLevel;
                viewDesc.Texture1D.MipLevels = range.uMipCount;
            }
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D:
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY:
            if (isArray)
            {
                if (isMultiSample)
                {
                    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
                    viewDesc.Texture2DMSArray.FirstArraySlice = range.uBaseArraySlice;
                    viewDesc.Texture2DMSArray.ArraySize = range.uArrayCount;
                }
                else
                {
                    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    viewDesc.Texture2DArray.MostDetailedMip = range.uBaseMipLevel;
                    viewDesc.Texture2DArray.MipLevels = range.uMipCount;
                    viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
                    viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
                    viewDesc.Texture2DArray.PlaneSlice = 0; /// 看起来是只有采样stencil才会用到这个，暂时不支持
                }
            }
            else
            {
                if (isMultiSample)
                {
                    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                }
                else
                {
                    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    viewDesc.Texture2D.MostDetailedMip = range.uBaseMipLevel;
                    viewDesc.Texture2D.MipLevels = range.uMipCount;
                    viewDesc.Texture2D.PlaneSlice = 0;
                }
            }
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MostDetailedMip = range.uBaseMipLevel;
            viewDesc.Texture3D.MipLevels = range.uMipCount;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE:
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY:
            if (isArray)
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                viewDesc.TextureCubeArray.MostDetailedMip = range.uBaseMipLevel;
                viewDesc.TextureCubeArray.MipLevels = range.uMipCount;
                viewDesc.TextureCubeArray.First2DArrayFace = range.uBaseArraySlice;
                viewDesc.TextureCubeArray.NumCubes = range.uArrayCount / 6;
            }
            else
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                viewDesc.TextureCube.MostDetailedMip = range.uBaseMipLevel;
                viewDesc.TextureCube.MipLevels = range.uMipCount;
            }
            break;
        default:
            assert(false);
            break;
        }

        pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Allocate(&descriptor);
        pD3dDevice->CreateShaderResourceView(m_Resource, &viewDesc, descriptor.cpuHandle);

        return descriptor;
    }

    D3D12Descriptor KGFX_TextureImplDx12::GetUAV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect)
    {
        if (!m_CanbUAV)
        {
            /// 这个贴图不允许创建UAV，检查贴图创建的流程
            assert(false);
            return {};
        }
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        KGFX_TextureViewDesc key = { KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV,format, type, aspect, range };
        D3D12Descriptor& descriptor = m_UAVS[key];
        if (descriptor)
            return descriptor;

        bool isArray = m_Desc.ArraySize() > 1;
        bool isMultiSample = m_Desc.SampleDesc.Count > 1;
        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetTexToDxFormat(format);
        switch (type)
        {
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = range.uBaseMipLevel;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture2D.PlaneSlice = 0;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture3D.FirstWSlice = range.uBaseArraySlice;
            viewDesc.Texture3D.WSize = -1;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE:
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
            viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
            viewDesc.Texture2DArray.PlaneSlice = 0;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1D.MipSlice = range.uBaseMipLevel;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
            viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
            viewDesc.Texture2DArray.PlaneSlice = 0;

            break;
        default:
            assert(false);
        }


        pGraphicDevice->GetDX12SRVAndUAVAndCBVHeap()->Allocate(&descriptor);
        pD3dDevice->CreateUnorderedAccessView(m_Resource, nullptr, &viewDesc, descriptor.cpuHandle);

        return descriptor;
    }

    D3D12Descriptor KGFX_TextureImplDx12::GetRTV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect)
    {
        if (!m_CanbRenderTarget)
        {
            /// 这个贴图不允许创建RTV，检查贴图创建的流程
            assert(false);
            return {};
        }

        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        KGFX_TextureViewDesc key = { KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV,format, type, aspect, range };
        D3D12Descriptor& descriptor = m_RTVS[key];
        if (descriptor)
            return descriptor;

        bool isArray = m_Desc.ArraySize() > 1;
        bool isMultiSample = m_Desc.SampleDesc.Count > 1;
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetTexToDxFormat(format);
        switch (type)
        {
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = range.uBaseMipLevel;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D:
            if (isMultiSample)
            {
                viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
                viewDesc.Texture2DMS.UnusedField_NothingToDefine = {};
            }
            else
            {
                viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = range.uBaseMipLevel;
                viewDesc.Texture2D.PlaneSlice = 0;
            }
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture3D.FirstWSlice = range.uArrayCount;
            viewDesc.Texture3D.WSize = m_Desc.Depth();
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE:
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
            viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
            viewDesc.Texture2DArray.PlaneSlice = 0;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY:
            if (isMultiSample)
            {
                viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.ArraySize = range.uArrayCount;
                viewDesc.Texture2DMSArray.FirstArraySlice = range.uBaseArraySlice;
            }
            else
            {
                viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.MipSlice = range.uBaseMipLevel;
                viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
                viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
                viewDesc.Texture2DArray.PlaneSlice = 0;

            }
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY:
        {
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture1DArray.FirstArraySlice = range.uBaseArraySlice;
            viewDesc.Texture1DArray.ArraySize = range.uArrayCount;
            break;
        }
        default:
            assert(false);
        }

        pGraphicDevice->GetDX12RTVHeap()->Allocate(&descriptor);
        pD3dDevice->CreateRenderTargetView(m_Resource, &viewDesc, descriptor.cpuHandle);

        return descriptor;
    }

    D3D12Descriptor KGFX_TextureImplDx12::GetDSV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect)
    {
        if (!m_CanbDepthTex)
        {
            /// 这个贴图不允许创建DSV，检查贴图创建的流程
            assert(false);
            return {};
        }

        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        KGFX_TextureViewDesc key = { KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV,format, type, aspect, range };
        D3D12Descriptor& descriptor = m_DSVS[key];
        if (descriptor)
            return descriptor;

        bool isArray = m_Desc.ArraySize() > 1;
        bool isMultiSample = m_Desc.SampleDesc.Count > 1;
        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetTexToDxFormat(format);
        switch (type)
        {
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = range.uBaseMipLevel;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D:
        {
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = range.uBaseMipLevel;
        }
        break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE:
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
            viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1DArray.MipSlice = range.uBaseMipLevel;
            break;
        case ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY:
        {
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = range.uBaseMipLevel;
            viewDesc.Texture2DArray.ArraySize = range.uArrayCount;
            viewDesc.Texture2DArray.FirstArraySlice = range.uBaseArraySlice;
        }
        break;
        default:
            assert(false);
            break;
        }

        pGraphicDevice->GetDX12DSVHeap()->Allocate(&descriptor);
        pD3dDevice->CreateDepthStencilView(m_Resource, &viewDesc, descriptor.cpuHandle);

        return descriptor;
    }

    bool KGFX_TextureImplDx12::Create(const std::optional<KClearValue>& clearValue)
    {
        HRESULT hResult = E_FAIL;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        D3D12MA::Allocator* pAllocator = pGraphicDevice->GetDX12Allocator();

        D3D12MA::ALLOCATION_DESC allocationDesc = {};
        allocationDesc.HeapType = m_ResourceHeapType;
        if (m_Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        {
            allocationDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
        }
        else
        {
            allocationDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
        }

        if (m_Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        {
            allocationDesc.ExtraHeapFlags |= D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS;
        }

        D3D12_CLEAR_VALUE optClear = {};
        D3D12_CLEAR_VALUE* poptClear = {};
        optClear.Format = m_Desc.Format;
        if (clearValue.has_value())
        {
            if (m_CanbDepthTex)
            {
                optClear.DepthStencil = { clearValue.value().depth, static_cast<uint8_t>(clearValue.value().stencil) };
            }
            else
            {
                optClear.Color[0] = clearValue.value().r;
                optClear.Color[1] = clearValue.value().g;
                optClear.Color[2] = clearValue.value().b;
                optClear.Color[3] = clearValue.value().a;
            }
            poptClear = &optClear;
        }


        D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        bool bAuto = is_set(static_cast<KGfxResourceMiscFlag>(m_TextureGfxDesc.eMiscFlags), KGfxResourceMiscFlag::AutoLayoutTransition);
        m_ResDefaultLayout = KGfxAccess::Unknown;
        switch (m_ResourceHeapType)
        {
        case D3D12_HEAP_TYPE_DEFAULT:
            initState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            m_ResDefaultLayout = KGfxAccess::SRVMask;
            break;
        case D3D12_HEAP_TYPE_UPLOAD:
            initState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            m_ResDefaultLayout = KGfxAccess::CopySrc;
            break;
        case D3D12_HEAP_TYPE_READBACK:
            initState = D3D12_RESOURCE_STATE_COPY_DEST;
            m_ResDefaultLayout = KGfxAccess::CopyDst;
            break;
        default:;
        }


        /// 2版本的DMA分配，3版本的目前引擎无法升级去支持
        hResult = pAllocator->CreateResource2(&allocationDesc, &m_Desc, initState, poptClear, &m_DMAAllocation, IID_PPV_ARGS(&m_Resource));
        KGLOG_COM_PROCESS_ERROR(hResult);

        m_ResourceLayout = KGFX_ResourceLayoutTracker<false>(initState, bAuto);

        return true;
    Exit0:
        SAFE_RELEASE(m_Resource);
        SAFE_RELEASE(m_DMAAllocation);
        return false;
    }


    bool KGFX_TextureImplDx12::Create(const KRenderTargetDesc* pDesc)
    {
        bool bResult = false;
        bool bHasStenc = false;
        bool bDepth = false;
        /// 由于使用DMA来分配内存，所以对齐Alignment这个值不需要填写，DMA会自己设置
        m_Desc = {};

        m_Desc.Format = GetTexToDxFormat(pDesc->eFormat, m_CanbDepthTex, m_CanbStencilTex);
        m_Desc.Width = pDesc->uWidth;
        m_Desc.Height = pDesc->uHeight;
        m_Desc.DepthOrArraySize = static_cast<uint16_t>(pDesc->uDepth * pDesc->uArraySize);
        m_Desc.MipLevels = static_cast<uint16_t>(pDesc->uMipLevels);
        m_Desc.Dimension = GetTexDimension(pDesc->uDepth, pDesc->uHeight);
        m_Desc.Flags = To_D3D12_RESOURCE_FLAGS(pDesc->uUsageFlags, pDesc->eFormat);
        m_Desc.SampleDesc.Count = 1;//pDesc->eSampleCount;
        m_Desc.SampleDesc.Quality = 0;//pDesc->sampleQuality;
        m_Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        m_Desc.Alignment = 0;
        m_Desc.SamplerFeedbackMipRegion = {};
        m_bCubeTex = pDesc->eDimension == TextureDimensionType::TextureCube;
        m_ResourceHeapType = D3D12_HEAP_TYPE_DEFAULT;
        m_TextureGfxDesc = static_cast<KGFX_TextureDesc>(*pDesc);
        switch (pDesc->memoryType)
        {
        case KGfxResourceAccessType::KGfxResourceAccess_GPUOnly:
            m_ResourceHeapType = D3D12_HEAP_TYPE_DEFAULT;
            break;
        case KGfxResourceAccessType::KGfxResourceAccess_Read:
            m_ResourceHeapType = D3D12_HEAP_TYPE_READBACK;
            break;
        case KGfxResourceAccessType::KGfxResourceAccess_Write:
            m_ResourceHeapType = D3D12_HEAP_TYPE_UPLOAD;
            break;
        default:;
        }

        if (pDesc->cpNativeHandle == nullptr)
        {
            bResult = Create(KClearValue());
            KG_PROCESS_ERROR(bResult);
        }
        else
        {
            /// 这个是swapchain用的，状态在获取的时候已经设置好了
            // ReSharper disable once CppCStyleCast
            m_Resource = (ID3D12Resource*)pDesc->cpNativeHandle;

            //uint32_t refg = m_Resource->AddRef();

            m_ResourceLayout = KGFX_ResourceLayoutTracker<false>{ D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE };
            m_bForSwapChain = true;
        }

#ifdef _DEBUG
        GetTexToDxFormat(pDesc->eFormat, bDepth, bHasStenc);
        if (bHasStenc)
        {
            assert(m_Desc.DepthOrArraySize <= 1);
        }
#endif

        m_CanbRenderTarget = true;
        m_CanbUAV = true;
        return true;
    Exit0:
        return false;
    }

    bool KGFX_TextureImplDx12::Create(const KGFX_TextureDesc& desc, bool canBUAV, std::optional<DXGI_FORMAT> dxgiFormat)
    {
        assert(desc.uMipLevels < D3D12_REQ_MIP_LEVELS);
        bool bResult = false;
        m_CanbUAV = canBUAV;
        /// 由于使用DMA来分配内存，所以对齐Alignment这个值不需要填写，DMA会自己设置
        m_Desc = {};
        m_Desc.Format = dxgiFormat.has_value() ? dxgiFormat.value() : GetTexToDxFormat(desc.eFormat, m_CanbDepthTex, m_CanbStencilTex);
        m_Desc.Width = desc.uWidth;
        m_Desc.Height = desc.uHeight;
        m_Desc.DepthOrArraySize = static_cast<uint16_t>(desc.uDepth * desc.uArraySize);
        m_Desc.MipLevels = static_cast<uint16_t>(desc.uMipLevels);
        m_Desc.Dimension = GetTexDimension(desc.uDepth, desc.uHeight);
        m_Desc.Flags = To_D3D12_RESOURCE_FLAGS(desc.uUsageFlags, desc.eFormat);
        m_Desc.SampleDesc.Count = 1;
        m_Desc.SampleDesc.Quality = 0;
        m_Desc.Alignment = 0;
        m_Desc.SamplerFeedbackMipRegion = {};
        m_Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        m_bCubeTex = desc.IsCubeTex();
        m_TextureGfxDesc = desc;
        m_ResourceHeapType = D3D12_HEAP_TYPE_DEFAULT;

        switch (desc.memoryType)
        {
        case KGfxResourceAccessType::KGfxResourceAccess_GPUOnly:
            m_ResourceHeapType = D3D12_HEAP_TYPE_DEFAULT;
            break;
        case KGfxResourceAccessType::KGfxResourceAccess_Read:
            m_ResourceHeapType = D3D12_HEAP_TYPE_READBACK;
            break;
        case KGfxResourceAccessType::KGfxResourceAccess_Write:
            m_ResourceHeapType = D3D12_HEAP_TYPE_UPLOAD;
            break;
        default:
            assert(false);
        }

        bResult = Create(std::nullopt);
        KG_PROCESS_ERROR(bResult);

        m_CanbRenderTarget = false;

        return true;
    Exit0:
        return false;
    }

    uintptr_t KGFX_TextureImplDx12::GetNativeResourceHandle()
    {
        return reinterpret_cast<uintptr_t>(m_Resource);
    }

    void KGFX_TextureImplDx12::SetDebugName(const char* name)
    {
        if (m_Resource)
        {
            WCHAR buff[128] = {};
            Utf8ToWideChar(buff, 128, name);
            m_Resource->SetName(buff);

            if (m_DMAAllocation)
            {
                m_DMAAllocation->SetName(buff);
            }
        }
        m_szName = name;
    }

    const char* KGFX_TextureImplDx12::GetDebugName()
    {
        return m_szName.c_str();
    }

    const KGFX_TextureDesc* KGFX_TextureImplDx12::GetDesc() const
    {
        return &m_TextureGfxDesc;
    }

    uint32_t KGFX_TextureImplDx12::GetDeviceMemorySize() const
    {
        if (m_DMAAllocation)
        {
            return static_cast<uint32_t>(m_DMAAllocation->GetSize());
        }
        return 0;
    }
}
