// ReSharper disable CppClangTidyCppcoreguidelinesProTypeStaticCastDowncast
#include "KGFX_CommandBufferDX12Impl.h"
#include "KGFX_BufferDx12.h"
#include "KGFX_FenceDX12Impl.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_PipelineDX12.h"
#include "KGFX_SwapChainDX12.h"
#include "KGFX_TextureImplDx12.h"
#include "KGFX_TransientHeap.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "WinPixEventRuntime/pix3.h"

namespace gfx
{

#pragma region KGFX_TranslateCommandBufferDX12
    KGFX_TranslateCommandBufferDX12::~KGFX_TranslateCommandBufferDX12()
    {
        Uninit();
    }

    void KGFX_TranslateCommandBufferDX12::Init(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        assert(d3dCommandList);
        assert(transientHeap);
        m_pD3d12CommandList = d3dCommandList;
        m_pTransientHeap = transientHeap;

        m_pD3d12CommandList->AddRef();
        m_pTransientHeap->AddRef();
    }

    void KGFX_TranslateCommandBufferDX12::ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        Uninit();
        Init(d3dCommandList, transientHeap);
        m_BarrierTracker.Reset();
    }

    void KGFX_TranslateCommandBufferDX12::Uninit()
    {
        SAFE_RELEASE(m_pD3d12CommandList);
        SAFE_RELEASE(m_pTransientHeap);
    }

    ID3D12GraphicsCommandList* KGFX_TranslateCommandBufferDX12::GetD3D12CommandList() const
    {
        return m_pD3d12CommandList;
    }

    void KGFX_TranslateCommandBufferDX12::UploadTextureData(IKGFX_TextureResource* pDstTexture, const KTextureCopyRegion& dstRegions, const KGfxSubResourceData& data) const
    {
        KGFX_TextureImplDx12* pDXTex = static_cast<KGFX_TextureImplDx12*>(pDstTexture);
        assert(pDXTex);
        PipelineBarrier(pDXTex, { pDXTex, KGfxAccess::Unknown, KGfxAccess::CopyDst });
        UploadTextureSubDataImpl(GetD3D12CommandList(), m_pTransientHeap, pDXTex, dstRegions, data);
    }


    void KGFX_TranslateCommandBufferDX12::UpdateSubResource(IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData) const
    {
        ID3D12GraphicsCommandList* pCmdList = GetD3D12CommandList();
        KGFX_BufferDx12* pDx12Buffer = static_cast<KGFX_BufferDx12*>(pGfxBuffer);

        KGFX_BufferImplDX12* pDXRes = pDx12Buffer->GetBufferImpl();
        assert(pDXRes);
        PipelineBarrier(pDx12Buffer, { pGfxBuffer, KGfxAccess::Unknown, KGfxAccess::CopyDst });
        UploadSubBufferDataImpl(pCmdList, m_pTransientHeap, pDXRes, uOffset, uSize, pData);

    }

    void KGFX_TranslateCommandBufferDX12::UpdateAllResource(IKGFX_TextureResource* pGfxTexure, const std::vector<KGfxSubResourceData>& data) const
    {
        ID3D12GraphicsCommandList* pCmdList = GetD3D12CommandList();
        KGFX_TextureImplDx12* texImpl = static_cast<KGFX_TextureImplDx12*>(pGfxTexure);
        assert(texImpl);
        PipelineBarrier(pGfxTexure, { pGfxTexure,KGfxAccess::Unknown,KGfxAccess::CopyDst });
        UploadTextureDataImpl(pCmdList, m_pTransientHeap, texImpl, data);

    }

    void KGFX_TranslateCommandBufferDX12::CopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer) const
    {
        KGFX_BufferDx12* pSrcDx12Buffer = static_cast<KGFX_BufferDx12*>(pSrcBuffer);
        KGFX_BufferDx12* pDstDx12Buffer = static_cast<KGFX_BufferDx12*>(pDstBuffer);

        PipelineBarrier(pSrcDx12Buffer, { pSrcBuffer, KGfxAccess::Unknown,KGfxAccess::CopySrc });
        PipelineBarrier(pDstDx12Buffer, { pDstBuffer, KGfxAccess::Unknown,KGfxAccess::CopyDst });

        GetD3D12CommandList()->CopyResource(reinterpret_cast<ID3D12Resource*>(pDstBuffer->GetNativeResourceHandle()), reinterpret_cast<ID3D12Resource*>(pSrcBuffer->GetNativeResourceHandle()));

    }

    void KGFX_TranslateCommandBufferDX12::CopyBufferSubRegions(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer, uint32_t uCopyRegionCount, const KBufferCopyRegion* pCopyRegions) const
    {
        KGFX_BufferDx12* pSrcDx12Buffer = static_cast<KGFX_BufferDx12*>(pSrcBuffer);
        KGFX_BufferDx12* pDstDx12Buffer = static_cast<KGFX_BufferDx12*>(pDstBuffer);

        PipelineBarrier(pSrcDx12Buffer, { pSrcBuffer, KGfxAccess::Unknown,KGfxAccess::CopySrc });
        PipelineBarrier(pDstDx12Buffer, { pDstBuffer, KGfxAccess::Unknown,KGfxAccess::CopyDst });


        for (uint32_t i = 0; i < uCopyRegionCount; i++)
        {
            const KBufferCopyRegion& copyRegion = pCopyRegions[i];

            GetD3D12CommandList()->CopyBufferRegion(reinterpret_cast<ID3D12Resource*>(pDstBuffer->GetNativeResourceHandle()), copyRegion.uDstOffset, reinterpret_cast<ID3D12Resource*>(pSrcBuffer->GetNativeResourceHandle()), copyRegion.uSrcOffset, copyRegion.uSize);
        }
    }

    void KGFX_TranslateCommandBufferDX12::CopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture) const
    {
        PipelineBarrier(pSrcTexture, { pSrcTexture,KGfxAccess::Unknown,KGfxAccess::CopySrc });
        PipelineBarrier(pDstTexture, { pDstTexture, KGfxAccess::Unknown, KGfxAccess::CopyDst });

        GetD3D12CommandList()->CopyResource(reinterpret_cast<ID3D12Resource*>(pDstTexture->GetNativeResourceHandle()), reinterpret_cast<ID3D12Resource*>(pSrcTexture->GetNativeResourceHandle()));

    }

    void KGFX_TranslateCommandBufferDX12::CopyTextureSubRegions(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture, uint32_t uCopyRegionCount, const KTextureCopyRegion* pCopyRegions) const
    {
        if (pSrcTexture->GetDesc()->memoryType == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            for (uint32_t i = 0; i < uCopyRegionCount; i++)
            {
                const KTextureCopyRegion& copyRegion = pCopyRegions[i];
                KGfxBarrier temp = { pSrcTexture ,KGfxAccess::Unknown, KGfxAccess::CopySrc ,copyRegion.srcMipLevel, copyRegion.srcArraySlice, 1, 1 };
                PipelineBarrier(pSrcTexture, temp);
            }
        }

        if (pDstTexture->GetDesc()->memoryType == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            for (uint32_t i = 0; i < uCopyRegionCount; i++)
            {
                const KTextureCopyRegion& copyRegion = pCopyRegions[i];
                KGfxBarrier temp = { pDstTexture ,KGfxAccess::Unknown, KGfxAccess::CopyDst ,copyRegion.dstMipLevel, copyRegion.dstArraySlice, 1, 1 };

                PipelineBarrier(pDstTexture, temp);
            }
        }


        for (uint32_t i = 0; i < uCopyRegionCount; i++)
        {
            const KTextureCopyRegion& copyRegion = pCopyRegions[i];
            D3D12_TEXTURE_COPY_LOCATION srcRegion = {};

            srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcRegion.pResource = reinterpret_cast<ID3D12Resource*>(pSrcTexture->GetNativeResourceHandle());
            srcRegion.SubresourceIndex = GetSubresourceIndex(copyRegion.srcMipLevel, copyRegion.srcArraySlice, 0, 1, 1);

            D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
            dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstRegion.pResource = reinterpret_cast<ID3D12Resource*>(pDstTexture->GetNativeResourceHandle());
            dstRegion.SubresourceIndex = GetSubresourceIndex(copyRegion.dstMipLevel, copyRegion.dstArraySlice, 0, 1, 1);

            D3D12_BOX srcBox = {};
            srcBox.left = copyRegion.srcLeft;
            srcBox.top = copyRegion.srcTop;
            srcBox.front = copyRegion.srcFront;
            srcBox.right = copyRegion.srcLeft + copyRegion.extentWidth;
            srcBox.bottom = copyRegion.srcTop + copyRegion.extentHeight;
            srcBox.back = copyRegion.srcFront + copyRegion.extentDepth;

            assert(srcBox.right > srcBox.left);
            assert(srcBox.bottom > srcBox.top);

            GetD3D12CommandList()->CopyTextureRegion(&dstRegion, copyRegion.dstLeft, copyRegion.dstTop, copyRegion.dstFront, &srcRegion, &srcBox);
        }

    }

    void KGFX_TranslateCommandBufferDX12::CopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) const
    {
        KGfxBarrier srcTexBarrier = { pSrcTexture ,KGfxAccess::Unknown, KGfxAccess::CopySrc };
        PipelineBarrier(pSrcTexture, srcTexBarrier);

        KGfxBarrier dstBufBarrier = { pDstBuffer ,KGfxAccess::Unknown, KGfxAccess::CopyDst };
        KGFX_BufferDx12* dxBuf = static_cast<KGFX_BufferDx12*>(pDstBuffer);
        PipelineBarrier(dxBuf, dstBufBarrier);



        if (NumBufferTextureCopy == 0)
        {
            KGFX_TextureImplDx12* texImpl = static_cast<KGFX_TextureImplDx12*>(pSrcTexture);
            KGFX_BufferDx12* bufImpl = static_cast<KGFX_BufferDx12*>(pDstBuffer);
            CopyTexToBufferImpl(GetD3D12CommandList(), bufImpl->GetBufferImpl(), texImpl);
        }
        else
        {
            KGFX_TextureImplDx12* texImpl = static_cast<KGFX_TextureImplDx12*>(pSrcTexture);
            auto DXtexDesc = texImpl->GetDXResourceDesc();
            for (uint32_t i = 0; i < NumBufferTextureCopy; i++)
            {
                const KBufferTextureCopy& copy = pBufferTextureCopy[i];
                D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
                dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dstRegion.pResource = reinterpret_cast<ID3D12Resource*>(pDstBuffer->GetNativeResourceHandle());
                dstRegion.PlacedFootprint.Footprint.Width = copy.textureCopyRegion.right - copy.textureCopyRegion.left;
                dstRegion.PlacedFootprint.Footprint.Height = copy.textureCopyRegion.bottom - copy.textureCopyRegion.top;
                dstRegion.PlacedFootprint.Footprint.Depth = copy.textureCopyRegion.back - copy.textureCopyRegion.front;
                dstRegion.PlacedFootprint.Footprint.Format = DXtexDesc.Format;
                dstRegion.PlacedFootprint.Footprint.RowPitch = copy.bufferRowLength;;
                dstRegion.PlacedFootprint.Offset = copy.bufferOffset;;

                D3D12_BOX srcBox = {};
                srcBox.left = 0;
                srcBox.top = 0;
                srcBox.front = 0;
                srcBox.right = 0 + copy.textureCopyRegion.right - copy.textureCopyRegion.left;
                srcBox.bottom = 0 + copy.textureCopyRegion.bottom - copy.textureCopyRegion.top;
                srcBox.back = 0 + copy.textureCopyRegion.back - copy.textureCopyRegion.front;

                D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
                srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                srcRegion.SubresourceIndex = i;
                srcRegion.pResource = reinterpret_cast<ID3D12Resource*>(pSrcTexture->GetNativeResourceHandle());


                GetD3D12CommandList()->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, &srcBox);
            }
        }

    }

    void KGFX_TranslateCommandBufferDX12::PipelineBarrier(KGFX_BufferDx12* buf, const KGfxBarrier& pPipelineBarrier) const
    {
        if (buf->GetDesc()->eResAccessFlags != KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            return;
        }
        PipelineBarrierImplT<true>(buf, pPipelineBarrier, [this](uint32_t barrierCount, const D3D12_RESOURCE_BARRIER& pBarrier)
            {
                GetD3D12CommandList()->ResourceBarrier(barrierCount, &pBarrier);
            });

    }

    void KGFX_TranslateCommandBufferDX12::PipelineBarrier(IKGFX_TextureResource* tex, const KGfxBarrier& pPipelineBarrier) const
    {
        KGFX_TextureImplDx12* texImpl = static_cast<KGFX_TextureImplDx12*>(tex);
        if (texImpl->GetDesc()->memoryType != KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            return;
        }
        PipelineBarrierImplT<true>(texImpl, pPipelineBarrier, [this](uint32_t barrierCount, const D3D12_RESOURCE_BARRIER& pBarrier)
            {
                GetD3D12CommandList()->ResourceBarrier(barrierCount, &pBarrier);
            });
    }

    void KGFX_TranslateCommandBufferDX12::ClearTextureView(IKGFX_TextureView* view, const KClearValue& clearValue, KGFX_ClearResourceViewFlags flags) const
    {
        uintptr_t viewHandle = view->GetNativeHandle();
        IKGFX_TextureResource* pgfxTex = view->GetResource();
        ID3D12Resource* d3dResource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { viewHandle };
        float clearValueTemp[4] = { clearValue.r, clearValue.g, clearValue.b, clearValue.a };
        //KGFX_GraphicDeviceDx12* pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        ID3D12Device* pdxDevice = pGraphicDevice->GetDXDevice();

        switch (view->GetViewDesc().eViewType)
        {
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
            PipelineBarrier(pgfxTex, { pgfxTex ,KGfxAccess::Unknown,KGfxAccess::RTV,view->GetViewDesc().sSubresourceRange });
            GetD3D12CommandList()->ClearRenderTargetView(cpuHandle, clearValueTemp, 0, nullptr);
            break;
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
        {
            PipelineBarrier(pgfxTex, { pgfxTex ,KGfxAccess::Unknown,KGfxAccess::DSVWrite,view->GetViewDesc().sSubresourceRange });
            D3D12_CLEAR_FLAGS clearFlags = static_cast<D3D12_CLEAR_FLAGS>(0);
            if ((flags & KGFX_ClearResourceViewFlags::ClearDepth) != KGFX_ClearResourceViewFlags::None)
            {
                clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
            }

            if ((flags & KGFX_ClearResourceViewFlags::ClearStencil) != KGFX_ClearResourceViewFlags::None)
            {
                clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
            }

            GetD3D12CommandList()->ClearDepthStencilView(cpuHandle, clearFlags, clearValue.depth, static_cast<UINT8>(clearValue.stencil), 0, nullptr);
        }
        break;
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV: /// dx12 对UAV的clear比较复杂，
        {
            PipelineBarrier(pgfxTex, { pgfxTex,KGfxAccess::Unknown, KGfxAccess::UAVMask,view->GetViewDesc().sSubresourceRange });
            d3dResource = reinterpret_cast<ID3D12Resource*>(view->GetResource()->GetNativeResourceHandle());
            auto gpuHandleIndex = m_pTransientHeap->GetCurrentViewHeap().Allocate(1);

            if (gpuHandleIndex == -1)
            {
                m_pTransientHeap->AllocateNewViewDescriptorHeap();
                gpuHandleIndex = m_pTransientHeap->GetCurrentViewHeap().Allocate(1);

                ID3D12DescriptorHeap* subViewHeap[] = { m_pTransientHeap->GetCurrentViewHeap().GetHeap(), m_pTransientHeap->GetCurrentSamplerHeap().GetHeap() };
                GetD3D12CommandList()->SetDescriptorHeaps(2, subViewHeap);
            }
            else
            {
                ID3D12DescriptorHeap* subViewHeap[] = { m_pTransientHeap->GetCurrentViewHeap().GetHeap(), m_pTransientHeap->GetCurrentSamplerHeap().GetHeap() };
                GetD3D12CommandList()->SetDescriptorHeaps(2, subViewHeap);
            }

            pdxDevice->CopyDescriptorsSimple(1, m_pTransientHeap->GetCurrentViewHeap().GetCpuHandle(gpuHandleIndex), cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            if ((flags & KGFX_ClearResourceViewFlags::FloatClearValues) > KGFX_ClearResourceViewFlags::None)
            {
                float clearVf[4] = { clearValue.r, clearValue.g, clearValue.b, clearValue.a };
                GetD3D12CommandList()->ClearUnorderedAccessViewFloat(
                    m_pTransientHeap->GetCurrentViewHeap().GetGpuHandle(
                        gpuHandleIndex),
                    cpuHandle,
                    d3dResource,
                    clearVf,
                    0,
                    nullptr);
            }
            else
            {
                uint32_t clearVi[4] = { static_cast<uint32_t>(clearValue.r), static_cast<uint32_t>(clearValue.g), static_cast<uint32_t>(clearValue.b), static_cast<uint32_t>(clearValue.a) };
                GetD3D12CommandList()->ClearUnorderedAccessViewUint(
                    m_pTransientHeap->GetCurrentViewHeap().GetGpuHandle(
                        gpuHandleIndex),
                    cpuHandle,
                    d3dResource,
                    clearVi,
                    0,
                    nullptr);
            }
        }
        break;
        default:
            assert(false);
        }
    }

    void KGFX_TranslateCommandBufferDX12::ClearBufferView(IKGFX_BufferView* view, const KClearValue& clearValue, KGFX_ClearResourceViewFlags flags) const
    {
        SIZE_T viewTemp = view->GetNativeHandle();
        KGFX_BufferDx12* pgfxBuf = static_cast<KGFX_BufferDx12*>(view->GetResource());
        KGFX_BufferImplDX12* pImplDx12 = (pgfxBuf->GetBufferImpl());
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { viewTemp };
        uint32_t gpuHandleIndex = 0;
        ID3D12Resource* d3dResource = nullptr;
        //KGFX_GraphicDeviceDx12* pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        ID3D12Device* pdxDevice = pGraphicDevice->GetDXDevice();

        switch (view->GetViewDesc()->eViewType)
        {
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
        {
            /// dx12 对UAV的clear比较复杂
            cpuHandle = pImplDx12->GetUAV(view->GetViewDesc()->eFormat, 0, view->GetViewDesc()->uBytesOffset, view->GetViewDesc()->uBytesRange).cpuHandle;
            d3dResource = reinterpret_cast<ID3D12Resource*>(view->GetResource()->GetNativeResourceHandle());
            gpuHandleIndex = m_pTransientHeap->GetCurrentViewHeap().Allocate(1);
            KGFX_BufferDx12* Dx12Buffer = (KGFX_BufferDx12*)pgfxBuf;
            PipelineBarrier(Dx12Buffer, { Dx12Buffer, KGfxAccess::Unknown, KGfxAccess::UAVMask });
            if (gpuHandleIndex == -1)
            {
                m_pTransientHeap->AllocateNewViewDescriptorHeap();
                gpuHandleIndex = m_pTransientHeap->GetCurrentViewHeap().Allocate(1);

                ID3D12DescriptorHeap* subViewHeap[] = { m_pTransientHeap->GetCurrentViewHeap().GetHeap(), m_pTransientHeap->GetCurrentSamplerHeap().GetHeap() };
                GetD3D12CommandList()->SetDescriptorHeaps(2, subViewHeap);
            }
            else
            {
                ID3D12DescriptorHeap* subViewHeap[] = { m_pTransientHeap->GetCurrentViewHeap().GetHeap(), m_pTransientHeap->GetCurrentSamplerHeap().GetHeap() };
                GetD3D12CommandList()->SetDescriptorHeaps(2, subViewHeap);
            }


            pdxDevice->CopyDescriptorsSimple(1, m_pTransientHeap->GetCurrentViewHeap().GetCpuHandle(gpuHandleIndex), cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            if ((flags & KGFX_ClearResourceViewFlags::FloatClearValues) > KGFX_ClearResourceViewFlags::None)
            {
                float clearVf[4] = { clearValue.r, clearValue.g, clearValue.b, clearValue.a };
                GetD3D12CommandList()->ClearUnorderedAccessViewFloat(
                    m_pTransientHeap->GetCurrentViewHeap().GetGpuHandle(
                        gpuHandleIndex),
                    cpuHandle,
                    d3dResource,
                    clearVf,
                    0,
                    nullptr);
            }
            else
            {
                uint32_t clearVi[4] = { static_cast<uint32_t>(clearValue.r), static_cast<uint32_t>(clearValue.g), static_cast<uint32_t>(clearValue.b), static_cast<uint32_t>(clearValue.a) };
                GetD3D12CommandList()->ClearUnorderedAccessViewUint(
                    m_pTransientHeap->GetCurrentViewHeap().GetGpuHandle(
                        gpuHandleIndex),
                    cpuHandle,
                    d3dResource,
                    clearVi,
                    0,
                    nullptr);
            }

        }
        break;
        default:
            assert(false);
        }
    }

    void KGFX_TranslateCommandBufferDX12::CommitAllTrackerBarrier()
    {
        m_BarrierTracker.CommitAllBarrier(GetD3D12CommandList());
    }

    void KGFX_TranslateCommandBufferDX12::ClearAllTrackerBarrier()
    {
        m_BarrierTracker.Reset();
    }

    void KGFX_TranslateCommandBufferDX12::PipelineDelayBarrier(KGFX_BufferDx12* buf, const KGfxBarrier& pPipelineBarrier)
    {
        if (buf->GetDesc()->eResAccessFlags != KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            return;
        }

        PipelineBarrierImplT<false>(buf, pPipelineBarrier, [buf, this](uint32_t, const D3D12_RESOURCE_BARRIER& pBarrier)
            {
                KGFX_BufferImplDX12* bufImpl = (buf->GetBufferImpl());
                m_BarrierTracker.ResourceBarrier(pBarrier, bufImpl);
            });

    }

    void KGFX_TranslateCommandBufferDX12::PipelineDelayBarrier(IKGFX_TextureResource* tex, const KGfxBarrier& pPipelineBarrier)
    {

        KGFX_TextureImplDx12* textureImpl = static_cast<KGFX_TextureImplDx12*>(tex);
        if (textureImpl->GetDesc()->memoryType != KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            return;
        }

        PipelineBarrierImplT<false>(textureImpl, pPipelineBarrier, [textureImpl, this](uint32_t, const D3D12_RESOURCE_BARRIER& pBarrier)
            {
                m_BarrierTracker.ResourceBarrier(pBarrier, textureImpl);
            });

    }

    void KGFX_TranslateCommandBufferDX12::BeginDebugLabel(std::string_view name) const
    {
#ifdef _DEBUG
        PIXBeginEvent(GetD3D12CommandList(), 0, name.data());  // NOLINT(bugprone-suspicious-stringview-data-usage)
#endif
    }

    void KGFX_TranslateCommandBufferDX12::EndDebugLabel() const
    {
#ifdef _DEBUG
        PIXEndEvent(GetD3D12CommandList());
#endif
    }


#pragma endregion

#pragma region KGFX_ComputeCommandBufferDX12
    KGFX_ComputeCommandBufferDX12::~KGFX_ComputeCommandBufferDX12()
    {
        Uninit();
    }

    void KGFX_ComputeCommandBufferDX12::Dispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ) const
    {
        GetD3D12CommandList()->Dispatch(nGroupCountX, nGroupCountY, nGroupCountZ);
    }

    void KGFX_ComputeCommandBufferDX12::BindRootSignature(ID3D12RootSignature* pSignature) const
    {
        GetD3D12CommandList()->SetComputeRootSignature(pSignature);
    }

    void KGFX_ComputeCommandBufferDX12::SetPipelineState(ID3D12PipelineState* pso) const
    {
        GetD3D12CommandList()->SetPipelineState(pso);
    }

    void KGFX_ComputeCommandBufferDX12::Init(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();

        assert(d3dCommandList);
        assert(transientHeap);
        m_pD3d12CommandList = d3dCommandList;
        m_pTransientHeap = transientHeap;

        m_pD3d12CommandList->AddRef();
        m_pTransientHeap->AddRef();
    }

    void KGFX_ComputeCommandBufferDX12::ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        Uninit();
        Init(d3dCommandList, transientHeap);
    }

    void KGFX_ComputeCommandBufferDX12::Uninit()
    {
        SAFE_RELEASE(m_pD3d12CommandList);
        SAFE_RELEASE(m_pTransientHeap);
    }

    ID3D12GraphicsCommandList* KGFX_ComputeCommandBufferDX12::GetD3D12CommandList() const
    {
        return m_pD3d12CommandList;
    }

    void KGFX_ComputeCommandBufferDX12::SetDescriptorHeaps() const
    {
        ID3D12DescriptorHeap* subViewHeap[] = { m_pTransientHeap->GetCurrentViewHeap().GetHeap(), m_pTransientHeap->GetCurrentSamplerHeap().GetHeap() };
        GetD3D12CommandList()->SetDescriptorHeaps(2, subViewHeap);
    }

    void KGFX_ComputeCommandBufferDX12::DispatchIndirect(IKGFX_Buffer* pIndirectBuffer, int nOffset) const
    {
        //KGFX_GraphicDeviceDx12* pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        auto indirectCS = pGraphicDevice->GetDispatchIndirectCmdSignature();
        assert(indirectCS);
        GetD3D12CommandList()->ExecuteIndirect(indirectCS, 1, reinterpret_cast<ID3D12Resource*>(pIndirectBuffer->GetNativeResourceHandle()), nOffset, nullptr, 0);
    }
#pragma endregion


#pragma region KGFX_GraphicsCommandBufferDX12

    KGFX_GraphicsCommandBufferDX12::~KGFX_GraphicsCommandBufferDX12()
    {
        Uninit();
    }


    void KGFX_GraphicsCommandBufferDX12::Init(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();

        if (m_pTransientHeap == nullptr)
        {
            m_pTransientHeap = transientHeap;
            m_pTransientHeap->AddRef();
        }

        if (std::get<0>(m_D3d12CommandLists) == nullptr)
        {
            std::get<0>(m_D3d12CommandLists) = d3dCommandList;
            d3dCommandList->AddRef();
            std::get<0>(m_D3d12CommandLists)->QueryInterface<ID3D12GraphicsCommandList1>(&std::get<1>(m_D3d12CommandLists));
            std::get<0>(m_D3d12CommandLists)->QueryInterface<ID3D12GraphicsCommandList4>(&std::get<2>(m_D3d12CommandLists));
            std::get<0>(m_D3d12CommandLists)->QueryInterface<ID3D12GraphicsCommandList6>(&std::get<3>(m_D3d12CommandLists));
            std::get<0>(m_D3d12CommandLists)->QueryInterface<ID3D12GraphicsCommandList9>(&std::get<4>(m_D3d12CommandLists));
        }
    }

    void KGFX_GraphicsCommandBufferDX12::ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        Uninit();
        Init(d3dCommandList, transientHeap);
    }

    void KGFX_GraphicsCommandBufferDX12::Uninit()
    {
        SAFE_RELEASE(std::get<0>(m_D3d12CommandLists));
        SAFE_RELEASE(std::get<1>(m_D3d12CommandLists));
        SAFE_RELEASE(std::get<2>(m_D3d12CommandLists));
        SAFE_RELEASE(std::get<3>(m_D3d12CommandLists));
        SAFE_RELEASE(std::get<4>(m_D3d12CommandLists));
        SAFE_RELEASE(m_pTransientHeap);
    }

    ID3D12GraphicsCommandList* KGFX_GraphicsCommandBufferDX12::GetD3D12CommandList() const
    {
        return std::get<0>(m_D3d12CommandLists);
    }

    ID3D12GraphicsCommandList1* KGFX_GraphicsCommandBufferDX12::GetD3D12CommandList1() const
    {
        return std::get<1>(m_D3d12CommandLists);
    }

    ID3D12GraphicsCommandList4* KGFX_GraphicsCommandBufferDX12::GetD3D12CommandList4() const
    {
        return std::get<2>(m_D3d12CommandLists);
    }

    ID3D12GraphicsCommandList6* KGFX_GraphicsCommandBufferDX12::GetD3D12CommandList6() const
    {
        return std::get<3>(m_D3d12CommandLists);
    }

    ID3D12GraphicsCommandList9* KGFX_GraphicsCommandBufferDX12::GetD3D12CommandList9() const
    {
        return std::get<4>(m_D3d12CommandLists);
    }

    void KGFX_GraphicsCommandBufferDX12::SetScissorRects(const std::vector<D3D12_RECT>& scissorRects) const
    {
        GetD3D12CommandList()->RSSetScissorRects(static_cast<uint32_t>(scissorRects.size()), scissorRects.data());
    }

    void KGFX_GraphicsCommandBufferDX12::SetViewPorts(const std::vector<D3D12_VIEWPORT>& scissorRects) const
    {
        GetD3D12CommandList()->RSSetViewports(static_cast<uint32_t>(scissorRects.size()), scissorRects.data());
    }

    void KGFX_GraphicsCommandBufferDX12::BindVertexBuffers(int nFirstBinding, int nBindingCount, const D3D12_VERTEX_BUFFER_VIEW* vertexBufViews) const
    {
        assert(vertexBufViews);
        GetD3D12CommandList()->IASetVertexBuffers(nFirstBinding, nBindingCount, vertexBufViews);
    }

    void KGFX_GraphicsCommandBufferDX12::BindIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pIndexBufferView) const
    {
        assert(pIndexBufferView);
        GetD3D12CommandList()->IASetIndexBuffer(pIndexBufferView);
    }

    void KGFX_GraphicsCommandBufferDX12::DrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance) const
    {

        GetD3D12CommandList()->DrawInstanced(nVertexCount, nInstanceCount, nFirstVertex, nFirstInstance);
    }

    void KGFX_GraphicsCommandBufferDX12::DrawIndirect(int maxDrawCount, ID3D12Resource* argBuf, uint32_t argBufOffset) const
    {
        //KGFX_GraphicDeviceDx12* pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        auto indirectCS = pGraphicDevice->GetDrawIndirectCmdSignature();
        assert(indirectCS);

        GetD3D12CommandList()->ExecuteIndirect(indirectCS, maxDrawCount, argBuf, argBufOffset, nullptr, 0);
    }

    void KGFX_GraphicsCommandBufferDX12::DrawIndexedIndirect(int maxDrawCount, ID3D12Resource* argBuf, uint32_t argBufOffset) const
    {
        //KGFX_GraphicDeviceDx12* pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        auto indirectCS = pGraphicDevice->GetDrawIndexedIndirectCmdSignature();
        assert(indirectCS);

        GetD3D12CommandList()->ExecuteIndirect(indirectCS, maxDrawCount, argBuf, argBufOffset, nullptr, 0);
    }


    void KGFX_GraphicsCommandBufferDX12::DrawIndexedInstanced(int nIndexCount, int nInstanceCount, int nIndexOffset, int nVertexOffset, int nInstanceOffset) const
    {
        GetD3D12CommandList()->DrawIndexedInstanced(nIndexCount, nInstanceCount, nIndexOffset, nVertexOffset, nInstanceOffset);
    }

    void KGFX_GraphicsCommandBufferDX12::SetPipelineState(ID3D12PipelineState* pso) const
    {
        assert(pso);
        GetD3D12CommandList()->SetPipelineState(pso);
    }

    void KGFX_GraphicsCommandBufferDX12::SetDescriptorHeaps() const
    {
        ID3D12DescriptorHeap* subViewHeap[] = { m_pTransientHeap->GetCurrentViewHeap().GetHeap(), m_pTransientHeap->GetCurrentSamplerHeap().GetHeap() };
        GetD3D12CommandList()->SetDescriptorHeaps(2, subViewHeap);
    }

    void KGFX_GraphicsCommandBufferDX12::SetStencilRef(uint32_t stencilRef) const
    {
        GetD3D12CommandList()->OMSetStencilRef(stencilRef);
    }

    void KGFX_GraphicsCommandBufferDX12::SetDipthBias(float DepthBias, float DepthBiasClamp, float SlopeScaledDepthBias) const
    {
        GetD3D12CommandList9()->RSSetDepthBias(DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
    }


#pragma endregion



#pragma region KGFX_CommandBufferDX12Impl
    void KGFX_CommandBufferDX12Impl::CmdCopyBufferSubRegions(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer, uint32_t uCopyRegionCount, const KBufferCopyRegion* pCopyRegions)
    {
        m_TranslateCommandEncoder.CopyBufferSubRegions(pSrcBuffer, pDstBuffer, uCopyRegionCount, pCopyRegions);
    }

    void KGFX_CommandBufferDX12Impl::CmdCopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture)
    {
        m_TranslateCommandEncoder.CopyTexture(pSrcTexture, pDstTexture);
    }

    void KGFX_CommandBufferDX12Impl::CmdCopyTextureSubRegions(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture, uint32_t uCopyRegionCount, const KTextureCopyRegion* pCopyRegions)
    {
        m_TranslateCommandEncoder.CopyTextureSubRegions(pSrcTexture, pDstTexture, uCopyRegionCount, pCopyRegions);
    }

    void KGFX_CommandBufferDX12Impl::CmdCopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy)
    {
        m_TranslateCommandEncoder.CopyTextureToBuffer(pSrcTexture, pDstBuffer, pBufferTextureCopy, NumBufferTextureCopy);
    }

    void KGFX_CommandBufferDX12Impl::CmdCopyBufferToTexture(IKGFX_Buffer* pSrcBuffer, IKGFX_TextureResource* pDstTexture, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy)
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    void KGFX_CommandBufferDX12Impl::CmdUploadTextureData(IKGFX_TextureResource* pDstTexture, const KTextureCopyRegion& dstRegions, const KGfxSubResourceData& data) const
    {
        m_TranslateCommandEncoder.UploadTextureData(pDstTexture, dstRegions, data);
    }


    KGFX_CommandBufferDX12Impl::~KGFX_CommandBufferDX12Impl()
    {
        SAFE_RELEASE(m_d3dCommandList);

        for (auto& tHeap : m_VecTransientHeap)
        {
            SAFE_RELEASE(tHeap);
        }
    }

    void KGFX_CommandBufferDX12Impl::Init()
    {
        pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        bool bRes = CreateTransientHeap();
        assert(bRes);
        m_CmdBufClosed = true;
        //if (m_GraphicsCommandEncoder == nullptr)
        //{
        //    m_GraphicsCommandEncoder.Attch(new KGFX_GraphicsCommandBufferDX12());

        //    m_ComputeCommandEncoder.Attch(new KGFX_ComputeCommandBufferDX12());

        //    m_TranslateCommandEncoder.Attch(new KGFX_TranslateCommandBufferDX12());
        //}

        BeginCommandBuffer();
    }


    void KGFX_CommandBufferDX12Impl::ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap)
    {
        Reset();
        SAFE_RELEASE(m_d3dCommandList);

        m_d3dCommandList = d3dCommandList;
        m_d3dCommandList->AddRef();

        m_GraphicsCommandEncoder.ReInit(d3dCommandList, transientHeap);
        m_ComputeCommandEncoder.ReInit(d3dCommandList, transientHeap);
        m_TranslateCommandEncoder.ReInit(d3dCommandList, transientHeap);
    }

    bool KGFX_CommandBufferDX12Impl::CreateTransientHeap()
    {
        bool bRet = false;

        TransientHeapDX12Desc desc = {};
        desc.allowResize = true;
        desc.srvDescriptorCount = 1024 * 512;
        desc.samplerDescriptorCount = 1024;
        desc.constantBufferDescriptorCount = 1024;
        desc.uavDescriptorCount = 1024;
        desc.accelerationStructureDescriptorCount = 1024;
        desc.constantBufferSize = 1024 * 512;

        for (uint32_t i = 0; i < DX12_SWAPCHAIN_BUFFER_COUNT; ++i)
        {
            KGFX_TransientHeapDX12* pTransientHeap{ new KGFX_TransientHeapDX12() };
            bRet = pTransientHeap->Init(desc, pGraphicDevice, 1024 * 512, 1024);
            KGLOG_PROCESS_ERROR(bRet);
            m_VecTransientHeap.emplace_back(pTransientHeap);
        }
        bRet = true;

    Exit0:
        return bRet;
    }

    BOOL KGFX_CommandBufferDX12Impl::BeginCommandBuffer()
    {
        int engineLoopIndex = NSEngine::GetRenderFrameMoveLoopCount();

        /// 有这个奇怪的逻辑是因为引擎会在启动的时候加两次帧数，然而实际只是一帧
        if (engineLoopIndex == 1)
        {
            engineLoopIndex = 2;
        }

        uint32_t currentHeapIndex = (engineLoopIndex) % DX12_SWAPCHAIN_BUFFER_COUNT;
        assert(currentHeapIndex < m_VecTransientHeap.size());

        /// 新的一帧过来了，要重置一下命令缓冲区
        if (m_uCurrentTransHeapID != currentHeapIndex)
        {
            m_uCurrentTransHeapID = currentHeapIndex;
            m_VecTransientHeap.at(m_uCurrentTransHeapID)->SynchronizeAndReset();
            auto newCmdList = m_VecTransientHeap.at(m_uCurrentTransHeapID)->CreateCmdList();
            ReInit(newCmdList, m_VecTransientHeap.at(m_uCurrentTransHeapID));
            m_CmdBufClosed = false;
        }

        /// 旧的一帧，但是上一个cmd被结束了需要生产一个新的cmd
        if (m_CmdBufClosed)
        {
            auto newCmdList = m_VecTransientHeap.at(m_uCurrentTransHeapID)->CreateCmdList();
            ReInit(newCmdList, m_VecTransientHeap.at(m_uCurrentTransHeapID));
        }
        return true;
    }

    void KGFX_CommandBufferDX12Impl::SubmitCommandBuffer(BOOL bWait, void* pGpuCompletedSignal)
    {
        CmdClose();
        uint64_t fenceValue = pGraphicDevice->GetDX12CommandQueueImpl()->ExecuteCommandList(this);
        if (fenceValue > 0)
        {
            GetUsedTransientHeap()->Finish();

            if (bWait)
            {
                GetUsedTransientHeap()->SynchronizeAndReset();
            }
        }
    }

    void* KGFX_CommandBufferDX12Impl::GetCommandBufferNativeHandle() const
    {
        return m_d3dCommandList;
    }

    void KGFX_CommandBufferDX12Impl::FlushResourceBarriers()
    {
        m_TranslateCommandEncoder.CommitAllTrackerBarrier();
    }

    void KGFX_CommandBufferDX12Impl::ClearResourceBarriers()
    {
        m_TranslateCommandEncoder.ClearAllTrackerBarrier();
    }

    void KGFX_CommandBufferDX12Impl::CmdBindVertexBuffers(int nFirstBinding, int nBindingCount, IKGFX_Buffer* apBuffer[], int anOffsets[], uint32_t* stride)
    {
        if (nBindingCount == 1)
        {
            KGFX_BufferDx12* pBuf = static_cast<KGFX_BufferDx12*>(apBuffer[0]);
            Transition({ pBuf,KGfxAccess::Unknown, KGfxAccess::VertexBuffer });
            const KGfxBufferDesc* bufferDesc = pBuf->GetDesc();
            uint32_t strides = m_DX12Pso->GetVertexBufStrid(nFirstBinding);

            KGFX_BufferImplDX12* pDX12Buf = (pBuf->GetBufferImpl());
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.SizeInBytes = bufferDesc->uByteWidth - anOffsets[0];
            vbv.StrideInBytes = stride ? stride[0] : strides;
            vbv.BufferLocation = pDX12Buf->GetBufResource()->GetGPUVirtualAddress() + anOffsets[0];
            assert(vbv.StrideInBytes > 0);
            assert(vbv.SizeInBytes > 0);
            m_GraphicsCommandEncoder.BindVertexBuffers(nFirstBinding, 1, &vbv);
        }
        else
        {
            std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
            for (int i = 0; i < nBindingCount; ++i)
            {
                KGFX_BufferDx12* pBuf = static_cast<KGFX_BufferDx12*>(apBuffer[i]);
                Transition({ pBuf,KGfxAccess::Unknown, KGfxAccess::VertexBuffer | KGfxAccess::BVHRead });
                const KGfxBufferDesc* bufferDesc = pBuf->GetDesc();

                uint32_t strides = m_DX12Pso->GetVertexBufStrid(i);

                KGFX_BufferImplDX12* pDX12Buf = (pBuf->GetBufferImpl());
                D3D12_VERTEX_BUFFER_VIEW vbv;
                vbv.SizeInBytes = bufferDesc->uByteWidth - anOffsets[i];
                vbv.StrideInBytes = stride ? stride[i] : strides;
                vbv.BufferLocation = pDX12Buf->GetBufResource()->GetGPUVirtualAddress() + anOffsets[i];
                assert(vbv.StrideInBytes > 0);
                assert(vbv.SizeInBytes > 0);
                views.emplace_back(vbv);
            }
            m_GraphicsCommandEncoder.BindVertexBuffers(nFirstBinding, nBindingCount, views.data());
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdBindIndexBuffer(IKGFX_Buffer* pBuffer, int nOffset, enumIndexType indexType)
    {
        D3D12_INDEX_BUFFER_VIEW ibv = {};
        KGFX_BufferDx12* pBuf = static_cast<KGFX_BufferDx12*>(pBuffer);
        const KGfxBufferDesc* bufferDesc = pBuf->GetDesc();
        KGFX_BufferImplDX12* pDX12Buf = (pBuf->GetBufferImpl());
        ibv.BufferLocation = pDX12Buf->GetBufResource()->GetGPUVirtualAddress() + nOffset;
        Transition({ pBuf,KGfxAccess::Unknown, KGfxAccess::IndexBuffer | KGfxAccess::BVHRead });

        if (indexType == INDEX_TYPE_UINT16)
        {
            ibv.Format = DXGI_FORMAT_R16_UINT;
        }
        else
        {
            ibv.Format = DXGI_FORMAT_R32_UINT;
        }
        ibv.SizeInBytes = bufferDesc->uByteWidth - nOffset;
        m_GraphicsCommandEncoder.BindIndexBuffer(&ibv);
    }

    void KGFX_CommandBufferDX12Impl::CmdDraw(int nVertexCount, int nFirstVertex, bool bPoint)
    {
        //FlushResourceBarriers();

        m_GraphicsCommandEncoder.SetStencilRef(m_DX12Pso->GetStencileRef());
        m_GraphicsCommandEncoder.DrawInstanced(nVertexCount, nFirstVertex, 1, 0);
    }

    void KGFX_CommandBufferDX12Impl::CmdDrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance)
    {
        //FlushResourceBarriers();
        m_GraphicsCommandEncoder.SetStencilRef(m_DX12Pso->GetStencileRef());
        m_GraphicsCommandEncoder.DrawInstanced(nVertexCount, nFirstVertex, nInstanceCount, nFirstInstance);
    }

    void KGFX_CommandBufferDX12Impl::CmdDrawIndexed(int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance)
    {
        // FlushResourceBarriers();
        m_GraphicsCommandEncoder.SetStencilRef(m_DX12Pso->GetStencileRef());
        m_GraphicsCommandEncoder.DrawIndexedInstanced(nIndexCount, nInstanceCount, nFirstIndex, nVertexOffset, nFirstInstance);
    }

    void KGFX_CommandBufferDX12Impl::CmdDrawIndexedInstanced(int nIndexCount, int nFirstIndex, int nInstanceCount, int nFirstVertex, int nFirstInstance)
    {
        //FlushResourceBarriers();
        m_GraphicsCommandEncoder.SetStencilRef(m_DX12Pso->GetStencileRef());
        m_GraphicsCommandEncoder.DrawIndexedInstanced(nIndexCount, nInstanceCount, nFirstIndex, nFirstVertex, nFirstInstance);
    }

    void KGFX_CommandBufferDX12Impl::CmdDrawIndexedIndirect(IKGFX_Buffer* pInderiectCommandBuffer, int nOffset, int nDrawCount, int nStride, bool bRecordDrawCall)
    {
        //FlushResourceBarriers();
        Transition({ pInderiectCommandBuffer,KGfxAccess::Unknown, KGfxAccess::IndirectArgs });
        ID3D12Resource* inDirectBuff = reinterpret_cast<ID3D12Resource*>(pInderiectCommandBuffer->GetNativeResourceHandle());
        assert(inDirectBuff);
        m_GraphicsCommandEncoder.SetStencilRef(m_DX12Pso->GetStencileRef());
        m_GraphicsCommandEncoder.DrawIndexedIndirect(nDrawCount, inDirectBuff, nOffset);
    }

    void KGFX_CommandBufferDX12Impl::CmdDrawIndirect(IKGFX_Buffer* pInderiectCommandBuffer, int nOffset, int nDrawCount, int nStride)
    {
        //FlushResourceBarriers();
        Transition({ pInderiectCommandBuffer,KGfxAccess::Unknown, KGfxAccess::IndirectArgs });
        ID3D12Resource* inDirectBuff = reinterpret_cast<ID3D12Resource*>(pInderiectCommandBuffer->GetNativeResourceHandle());
        assert(inDirectBuff);
        m_GraphicsCommandEncoder.SetStencilRef(m_DX12Pso->GetStencileRef());
        m_GraphicsCommandEncoder.DrawIndirect(nDrawCount, inDirectBuff, nOffset);
    }

    void KGFX_CommandBufferDX12Impl::CmdSetLineWidth(float linewidth)
    {
        throw std::logic_error("not impl");
    }

    void KGFX_CommandBufferDX12Impl::CmdBindPipeline(enumPipelineBindPoint eBindPoint, const KGFX_PipelineDX12* pPipeline)
    {
        if (eBindPoint == PIPELINE_BIND_POINT_GRAPHICS)
        {
            auto pPipelineDx12 = (pPipeline->GetPipelineHandle());

            if (m_CurrerntPso != pPipelineDx12)
            {
                m_GraphicsCommandEncoder.SetPipelineState(pPipelineDx12);
                m_CurrerntPso = pPipelineDx12;
                m_DX12Pso = pPipeline;
                //KGLogPrintf(KGLOG_DEBUG, pPipeline->m_DeBugName.c_str());
            }
            else
            {
                int i = 0;
            }
        }
        else
        {
            auto pPipelineDx12 = (pPipeline->GetPipelineHandle());
            if (m_CurrerntPso != pPipelineDx12)
            {
                m_ComputeCommandEncoder.SetPipelineState(pPipelineDx12);
                m_CurrerntPso = pPipelineDx12;
                m_DX12Pso = pPipeline;
                m_PrimitiveState = {};
            }
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdSetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveState)
    {
        assert(PrimitiveState != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED);
        //if (m_PrimitiveState != PrimitiveState)
        {
            m_PrimitiveState = PrimitiveState;
            GetD3D12CommandList()->IASetPrimitiveTopology(m_PrimitiveState);
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdSetGraphicsRootCbuf(uint32_t RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
    {
        auto rootBuf = m_vecGraphicsRootCBV.at(RootParameterIndex);

        if (rootBuf != BufferLocation)
        {
            m_vecGraphicsRootCBV.at(RootParameterIndex) = BufferLocation;
            GetD3D12CommandList()->SetGraphicsRootConstantBufferView(RootParameterIndex, BufferLocation);
        }

    }

    void KGFX_CommandBufferDX12Impl::CmdSetGraphicsDescriptorTable(uint32_t RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
    {
        auto rootTable = m_vecGraphicsDescriptorTable.at(RootParameterIndex);

        if (rootTable.ptr != BaseDescriptor.ptr)
        {
            m_vecGraphicsDescriptorTable.at(RootParameterIndex) = BaseDescriptor;
            GetD3D12CommandList()->SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdSetGraphicsRootSignature(ID3D12RootSignature* pRootSignature)
    {
        if (m_pGraphicsRootSignature != pRootSignature)
        {
            m_pComputeRootSignature = nullptr;
            m_pGraphicsRootSignature = pRootSignature;
            m_vecGraphicsDescriptorTable = {};
            m_vecGraphicsRootCBV = {};
            m_vecComputeDescriptorTable = {};
            m_vecComputeRootCBV = {};
            GetD3D12CommandList()->SetGraphicsRootSignature(pRootSignature);
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdSetComputeRootSignature(ID3D12RootSignature* pRootSignature)
    {
        if (m_pComputeRootSignature != pRootSignature)
        {
            m_pGraphicsRootSignature = nullptr;
            m_pComputeRootSignature = pRootSignature;
            m_vecComputeRootCBV = {};
            m_vecComputeDescriptorTable = {};
            m_vecComputeDescriptorTable = {};
            m_vecComputeRootCBV = {};
            GetD3D12CommandList()->SetComputeRootSignature(pRootSignature);
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdSetComputeRootCbuf(uint32_t RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
    {
        auto rootBuf = m_vecComputeRootCBV.at(RootParameterIndex);

        if (rootBuf != BufferLocation)
        {
            m_vecComputeRootCBV.at(RootParameterIndex) = BufferLocation;
            GetD3D12CommandList()->SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdSetComputeDescriptorTable(uint32_t RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
    {
        auto rootTable = m_vecComputeDescriptorTable.at(RootParameterIndex);

        if (rootTable.ptr != BaseDescriptor.ptr)
        {
            m_vecComputeDescriptorTable.at(RootParameterIndex) = BaseDescriptor;
            GetD3D12CommandList()->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
        }
    }


    void KGFX_CommandBufferDX12Impl::CmdClearAttachment(const KClearAttchment* pAttachment, int nCount, const NSKMath::KVectorInt2& v2Offset, const NSKMath::KVectorUint2& v2Size, uint32_t uBaseArrayLayer, uint32_t uLayerCount)
    {
        throw std::runtime_error("这个接口需要删除.");
    }

    void KGFX_CommandBufferDX12Impl::CmdSetDepthBias(float fConstant, float fClamp, float fSlop, bool bAutoReverse)
    {
        if (DrvOption::bReversePerspectiveDepthZ && bAutoReverse)
        {
            m_GraphicsCommandEncoder.SetDipthBias(-fConstant, -fClamp, -fSlop);
        }
        else
        {
            m_GraphicsCommandEncoder.SetDipthBias(fConstant, fClamp, fSlop);
        }
    }


    void KGFX_CommandBufferDX12Impl::CmdUpdateSubResource(IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData, uint32_t option)
    {
        m_TranslateCommandEncoder.UpdateSubResource(pGfxBuffer, uOffset, uSize, pData);
    }

    void KGFX_CommandBufferDX12Impl::CmdUpdateAllResource(IKGFX_TextureResource* pGfxTexure, std::vector<KGfxSubResourceData>& data)
    {
        m_TranslateCommandEncoder.UpdateAllResource(pGfxTexure, data);
    }


    BOOL KGFX_CommandBufferDX12Impl::CmdDispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ)
    {
        //FlushResourceBarriers();
        m_ComputeCommandEncoder.Dispatch(nGroupCountX, nGroupCountY, nGroupCountZ);
        return TRUE;
    }

    BOOL KGFX_CommandBufferDX12Impl::CmdDispatchIndirect(IKGFX_Buffer* pIndirectBuffer, int nOffset)
    {
        //FlushResourceBarriers();
        Transition({ pIndirectBuffer,KGfxAccess::Unknown, KGfxAccess::IndirectArgs });
        m_ComputeCommandEncoder.DispatchIndirect(pIndirectBuffer, nOffset);
        return TRUE;
    }


    void KGFX_CommandBufferDX12Impl::Reset()
    {
        InvalidateDescriptorHeapBinding();
        m_CmdBufClosed = false;
        m_CurrerntPso = nullptr;
    }

    void KGFX_CommandBufferDX12Impl::CmdSetScissor(int nX, int nY, int nWidth, int nHeight)
    {
        D3D12_RECT d12Rect = {};
        d12Rect.left = nX;
        d12Rect.top = nY;
        d12Rect.right = nWidth;
        d12Rect.bottom = nHeight;
        m_GraphicsCommandEncoder.SetScissorRects({ d12Rect });
    }

    void KGFX_CommandBufferDX12Impl::CmdSetScissor(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer)
    {
        D3D12_RECT d12Rect = {};
        d12Rect.left = 0;
        d12Rect.top = 0;
        d12Rect.right = piRenderFrameBuffer->GetWidth();
        d12Rect.bottom = piRenderFrameBuffer->GetHeight();
        m_GraphicsCommandEncoder.SetScissorRects({ d12Rect });
    }

    void KGFX_CommandBufferDX12Impl::CmdSetViewport(const ViewportDescription& Viewport)
    {
        D3D12_VIEWPORT d12Rect = {};
        d12Rect.TopLeftX = Viewport.TopLeftX;
        d12Rect.TopLeftY = Viewport.TopLeftY;
        d12Rect.Width = static_cast<float>(Viewport.Width);
        d12Rect.Height = static_cast<float>(Viewport.Height);
        d12Rect.MaxDepth = Viewport.MaxDepth;
        d12Rect.MinDepth = Viewport.MinDepth;
        m_GraphicsCommandEncoder.SetViewPorts({ d12Rect });
    }

    void KGFX_CommandBufferDX12Impl::CmdSetViewport(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer)
    {
        D3D12_VIEWPORT d12Rect = {};
        d12Rect.TopLeftX = 0;
        d12Rect.TopLeftY = 0;
        d12Rect.Width = static_cast<float>(piRenderFrameBuffer->GetWidth());
        d12Rect.Height = static_cast<float>(piRenderFrameBuffer->GetHeight());
        d12Rect.MaxDepth = 1;
        d12Rect.MinDepth = 0;
        m_GraphicsCommandEncoder.SetViewPorts({ d12Rect });
    }


    void KGFX_CommandBufferDX12Impl::CmdUpdateSubResource(IKGFX_TextureResource* pGfxTexure, uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion, const void* pSrcData, uint32_t uSrcRowPitch, uint32_t SrcDepthPitch)
    {
        KTextureCopyRegion dstTexRegion = {};
        dstTexRegion.dstMipLevel = uDstMipLevel;
        dstTexRegion.dstArraySlice = uDstArraySlice;

        if (pDstRegion)
        {
            assert(pDstRegion->right >= pDstRegion->left);
            assert(pDstRegion->bottom >= pDstRegion->top);
            assert(pDstRegion->back >= pDstRegion->front);
            dstTexRegion.dstFront = pDstRegion->front;
            dstTexRegion.dstLeft = pDstRegion->left;
            dstTexRegion.dstTop = pDstRegion->top;
            dstTexRegion.extentWidth = pDstRegion->right - pDstRegion->left;
            dstTexRegion.extentHeight = pDstRegion->bottom - pDstRegion->top;
            dstTexRegion.extentDepth = pDstRegion->back - pDstRegion->front;
        }
        KGfxSubResourceData srcData = {};
        srcData.pMemData = pSrcData;
        srcData.uMemByteRowPitch = uSrcRowPitch;
        srcData.uMemByteDepthPitch = SrcDepthPitch;

        m_TranslateCommandEncoder.UploadTextureData(pGfxTexure, dstTexRegion, srcData);
    }

    void KGFX_CommandBufferDX12Impl::BeginDebugLabel(const char* strDebugLabel)
    {
        m_TranslateCommandEncoder.BeginDebugLabel(strDebugLabel);
    }

    void KGFX_CommandBufferDX12Impl::EndDebugLabel()
    {
        m_TranslateCommandEncoder.EndDebugLabel();
    }

    void KGFX_CommandBufferDX12Impl::BeginOptickProfile()
    {
#if USE_OPTICK
        m_prevContext = Optick::SetGpuContext(Optick::GPUContext(m_d3dCommandList));
#endif
    }

    void KGFX_CommandBufferDX12Impl::EndOptickProfile()
    {
#if USE_OPTICK
        Optick::SetGpuContext(m_prevContext);
        m_prevContext = {};
#endif
    }



    void KGFX_CommandBufferDX12Impl::CmdBeginUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count)
    {
        assert(ppResourceUAV && count > 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (auto pTexture = dynamic_cast<KGFX_TextureImplDx12*>(ppResourceUAV[i]))
            {
                pTexture->GetLayoutTracker().m_bUAVOverLap = true;
                Transition(KGfxBarrier(pTexture, KGfxAccess::Unknown, KGfxAccess::UAVMask));
            }
            else if (auto pBuffer = dynamic_cast<KGFX_BufferDx12*>(ppResourceUAV[i]))
            {
                pBuffer->GetBufferImpl()->GetLayoutTracker().m_bUAVOverLap = true;
                Transition(KGfxBarrier(pBuffer, KGfxAccess::Unknown, KGfxAccess::UAVMask));
            }
        }
    }

    void KGFX_CommandBufferDX12Impl::CmdEndUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count)
    {
        assert(ppResourceUAV && count > 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (auto pTexture = dynamic_cast<KGFX_TextureImplDx12*>(ppResourceUAV[i]))
            {
                pTexture->GetLayoutTracker().m_bUAVOverLap = false;
            }
            else if (auto pBuffer = dynamic_cast<KGFX_BufferDx12*>(ppResourceUAV[i]))
            {
                pBuffer->GetBufferImpl()->GetLayoutTracker().m_bUAVOverLap = false;
            }
        }
    }

    BOOL KGFX_CommandBufferDX12Impl::CmdInsertSignalFence(KSignalFence* pSignalFence)
    {
        KGFX_FenceDX12Impl* pFenceImpl = static_cast<KGFX_FenceDX12Impl*>(pSignalFence);
        if (pFenceImpl)
        {
            uint64_t fenceValue = pGraphicDevice->GetDX12CommandQueueImpl()->GetCurrentFenceValue();
            pFenceImpl->m_FenceValue = fenceValue;
            pFenceImpl->m_bSubmitted = true;
            return TRUE;
        }
        return false;
    }

    void KGFX_CommandBufferDX12Impl::CmdClearTextureView(IKGFX_TextureView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags)
    {
        m_TranslateCommandEncoder.ClearTextureView(view, clearValue, flags);
    }

    void KGFX_CommandBufferDX12Impl::CmdClearBufferView(IKGFX_BufferView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags)
    {
        m_TranslateCommandEncoder.ClearBufferView(view, clearValue, flags);
    }

    void KGFX_CommandBufferDX12Impl::CmdClose()
    {
        //FlushResourceBarriers();
        if (!m_CmdBufClosed)
        {
            bool bRet = m_d3dCommandList->Close() == S_OK;
            assert(bRet);
            m_CmdBufClosed = true;
        }
    }

    bool KGFX_CommandBufferDX12Impl::GetIsClose() const
    {
        return m_CmdBufClosed;
    }

    void KGFX_CommandBufferDX12Impl::BeginRenderPass()
    {
        m_TranslateCommandEncoder.m_bBeginRenderPass = true;
    }

    void KGFX_CommandBufferDX12Impl::EndRenderPass()
    {
        m_TranslateCommandEncoder.m_bBeginRenderPass = false;
    }


    KGFX_TransientHeapDX12* KGFX_CommandBufferDX12Impl::GetUsedTransientHeap() const
    {
        return m_VecTransientHeap.at(m_uCurrentTransHeapID);
    }

    void KGFX_CommandBufferDX12Impl::CmdSetDescriptorHeaps()
    {
        //sif (!m_DescriptorHeapsBound)
        {
            m_GraphicsCommandEncoder.SetDescriptorHeaps();
            m_DescriptorHeapsBound = true;
        }
    }

    void KGFX_CommandBufferDX12Impl::InvalidateDescriptorHeapBinding()
    {
        m_DescriptorHeapsBound = false;
    }

    BOOL KGFX_CommandBufferDX12Impl::Transition(const KGfxBarrier& sBarrierInfo)
    {
        switch (sBarrierInfo.eType)
        {
        case KGfxBarrier::EType::Unknown:
            assert(false);
            break;
        case KGfxBarrier::EType::Texture:
            m_TranslateCommandEncoder.PipelineBarrier(sBarrierInfo.pTexture, sBarrierInfo);
            break;
        case KGfxBarrier::EType::TextureView:
            m_TranslateCommandEncoder.PipelineBarrier(sBarrierInfo.pTextureView->GetResource(), sBarrierInfo);
            break;
        case KGfxBarrier::EType::Buffer:
        {
            KGFX_BufferDx12* Dx12Buffer = static_cast<KGFX_BufferDx12*>(sBarrierInfo.pBuffer);
            m_TranslateCommandEncoder.PipelineBarrier(Dx12Buffer, sBarrierInfo);
        }
        break;
        case KGfxBarrier::EType::RenderTarget:
            m_TranslateCommandEncoder.PipelineBarrier(sBarrierInfo.pRenderTarget->GetTextureResource(), sBarrierInfo);
            break;
        default:;
        }

        return true;
    }

    BOOL KGFX_CommandBufferDX12Impl::Transition(const std::initializer_list<KGfxBarrier>& lsBarrierInfosArray)
    {
        for (const auto& barrier : lsBarrierInfosArray)
        {
            Transition(barrier);
        }
        return true;
    }

    BOOL KGFX_CommandBufferDX12Impl::Transition(const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount)
    {
        for (uint32_t i = 0; i < uBarrierCount; i++)
        {
            Transition(pBarrierInfos[i]);
        }
        return true;
    }

    void KGFX_CommandBufferDX12Impl::PipelineBarrier(IKGFX_Buffer* buf, const KGfxBarrier& pPipelineBarrier)
    {
        KGFX_BufferDx12* Dx12Buffer = static_cast<KGFX_BufferDx12*>(buf);
        m_TranslateCommandEncoder.PipelineDelayBarrier(Dx12Buffer, pPipelineBarrier);
    }

    void KGFX_CommandBufferDX12Impl::PipelineBarrier(IKGFX_TextureResource* tex, const KGfxBarrier& pPipelineBarrier)
    {
        m_TranslateCommandEncoder.PipelineDelayBarrier(tex, pPipelineBarrier);
    }

    ID3D12GraphicsCommandList* KGFX_CommandBufferDX12Impl::GetD3D12CommandList() const
    {
        return m_d3dCommandList;
    }

    ID3D12GraphicsCommandList4* KGFX_CommandBufferDX12Impl::GetD3D12CommandList4()const
    {
        return m_GraphicsCommandEncoder.GetD3D12CommandList4();
    }

    std::vector<ID3D12CommandList*> KGFX_CommandBufferDX12Impl::GetSubmitCmdLists() const
    {
        return m_VecTransientHeap.at(m_uCurrentTransHeapID)->GetSubmitCmdVec();
    }

    void KGFX_CommandBufferDX12Impl::SetCurrentFrameBuffer(gfx::IKGFX_RenderFrameBuffer* pRenderFrameBuffer)
    {
        m_pCurrentFrameBuffer = pRenderFrameBuffer;
    }

    gfx::IKGFX_RenderFrameBuffer* KGFX_CommandBufferDX12Impl::GetCurrentFrameBuffer() const
    {
        return m_pCurrentFrameBuffer;
    }

    void KGFX_CommandBufferDX12Impl::CmdCopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer)
    {
        uint32_t srcBufSize = pSrcBuffer->GetDesc()->uByteWidth;
        uint32_t dstBufSize = pDstBuffer->GetDesc()->uByteWidth;
        assert(dstBufSize >= srcBufSize);
        if (dstBufSize == srcBufSize)
        {
            m_TranslateCommandEncoder.CopyBuffer(pSrcBuffer, pDstBuffer);
        }
        else
        {
            KBufferCopyRegion copyRegion = {};
            copyRegion.uSrcOffset = 0;
            copyRegion.uDstOffset = 0;
            copyRegion.uSize = srcBufSize;
            m_TranslateCommandEncoder.CopyBufferSubRegions(pSrcBuffer, pDstBuffer, 1, &copyRegion);
        }

    }

	bool KGFX_CommandBufferDX12Impl::IsValid() const
	{
        return true;
	}
#pragma endregion
}
