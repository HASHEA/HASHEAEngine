#ifdef _WIN32
#include "KGFX_BufferDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "Engine/KGLog.h"
#include "KEnginePub/Public/IKEngineOption.h"
////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KGFX_TransientHeap.h"

namespace gfx
{
    static uint32_t g_BufferDx12AllocId = 0;

    KGFX_BufferDx12::KGFX_BufferDx12()
    {
        m_nRef = 1;
        m_uAllocId = g_BufferDx12AllocId++;
    }

    KGFX_BufferDx12::~KGFX_BufferDx12()
    {
        ASSERT(m_nRef == 0);
        SAFE_RELEASE(m_Resource);
    }

    KGFX_BufferDx12::KGFX_BufferDx12(KGFX_BufferDx12&& other) noexcept
        : m_DynamicDesc(other.m_DynamicDesc)
        , m_bDynamicBuf(other.m_bDynamicBuf)
        , m_Resource(other.m_Resource)
        , m_uAllocId(other.m_uAllocId)
        , m_DynamicBufOffset(other.m_DynamicBufOffset)
        , m_DynamicBufSize(other.m_DynamicBufSize)
    {
        other.m_Resource = nullptr;
        other.m_uAllocId = 0;
        other.m_DynamicBufOffset = 0;
        other.m_DynamicBufSize = 0;
        other.m_bDynamicBuf = false;
    }

    KGFX_BufferDx12& KGFX_BufferDx12::operator=(KGFX_BufferDx12&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        assert(other.m_Resource);

        m_Resource = other.m_Resource;
        m_uAllocId = other.m_uAllocId;
        m_DynamicDesc = other.m_DynamicDesc;
        m_bDynamicBuf = other.m_bDynamicBuf;
        m_DynamicBufOffset = other.m_DynamicBufOffset;
        m_DynamicBufSize = other.m_DynamicBufSize;


        other.m_Resource = nullptr;
        other.m_uAllocId = 0;
        other.m_DynamicBufOffset = 0;
        other.m_DynamicBufSize = 0;
        other.m_bDynamicBuf = false;

        return *this;
    }

    int32_t KGFX_BufferDx12::AddRef()
    {
        ASSERT(m_nRef > 0);
        return ++m_nRef;
    }

    int32_t KGFX_BufferDx12::GetRef()
    {
        return m_nRef;
    }

    int32_t KGFX_BufferDx12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            delete this;
        }
        return nRef;
    }

    BOOL KGFX_BufferDx12::Create(const KGfxBufferDesc& bufDesc, const void* pData, bool bDynamic)
    {
        BOOL bResult = false;
        m_bDynamicBuf = bDynamic;
        gfx::IKGFX_RenderContext* pRenderCtx = gfx::GetRenderContext();
        if (!m_bDynamicBuf)
        {
            m_Resource = new KGFX_BufferImplDX12();
            bResult = m_Resource->Create(bufDesc);
            KGLOG_PROCESS_ERROR(bResult);

            if (pData)
            {
                Update(pData, bufDesc.uByteWidth, 0, false);
            }
        }
        else
        {
            m_DynamicDesc = bufDesc;
            SAFE_RELEASE(m_Resource);

            uint32_t offset = 0;
            KGFX_BufferImplDX12* GpuCoherentBufWeakPtr = nullptr;
            KGFX_CommandBufferDX12Impl* pdx12 = dynamic_cast<KGFX_CommandBufferDX12Impl*>(pRenderCtx);
            bResult = pdx12->GetUsedTransientHeap()->AllocateDynamicBuffer(bufDesc.uByteWidth, bufDesc.uStructureByteStride, GpuCoherentBufWeakPtr, offset);
            assert(bResult);
            assert(GpuCoherentBufWeakPtr);
            m_Resource = GpuCoherentBufWeakPtr;
            m_Resource->AddRef();
            m_DynamicBufOffset = offset;
            m_DynamicBufSize = bufDesc.uByteWidth;

            if (pData)
            {
                UploadSubBufferDataImpl(pdx12->GetD3D12CommandList(), pdx12->GetUsedTransientHeap(), GpuCoherentBufWeakPtr, offset, m_DynamicBufSize, pData);
            }
        }
        if (bDynamic)
        {
            KGFX_GetGraphicDeviceDx12Internal()->m_uDynamicBufferCount++;
        }
        return true;
    Exit0:
        SAFE_RELEASE(m_Resource);
        return false;
    }

    BOOL KGFX_BufferDx12::Create(KGFX_BufferImplDX12* pBufRes, uint32_t offset)
    {
        SAFE_RELEASE(m_Resource);
        m_Resource = (pBufRes);
        KGLOG_PROCESS_ERROR(m_Resource);
        m_Resource->AddRef();

        m_DynamicBufOffset = offset;
        return true;
    Exit0:
        return false;
    }

    BOOL KGFX_BufferDx12::Destroy()
    {
        if (m_bDynamicBuf)
        {
            KGFX_GetGraphicDeviceDx12Internal()->m_uDynamicBufferCount--;
        }
        SAFE_RELEASE(m_Resource);
        return true;
    }

    KGFX_BufferImplDX12* KGFX_BufferDx12::GetBufferImpl() const
    {
        return m_Resource;
    }

    const KGfxBufferDesc* KGFX_BufferDx12::GetDesc() const
    {
        if (m_bDynamicBuf)
        {
            return &m_DynamicDesc;
        }
        return m_Resource->GetDesc();
    }

    uintptr_t KGFX_BufferDx12::GetNativeResourceHandle()
    {
        return reinterpret_cast<uintptr_t>(m_Resource->GetBufResource());
    }

    void KGFX_BufferDx12::SetDebugName(const char* szName)
    {
        if (m_Resource)
        {
            m_Resource->SetDebugName(szName);
        }
    }

    const char* KGFX_BufferDx12::GetDebugName()
    {
        return m_Resource ? m_Resource->GetDebugName() : "_KGFX_BufferDx12::NoName";
    }

    BOOL KGFX_BufferDx12::Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite)
    {
        BOOL bResult = false;
        if (m_bDynamicBuf)
        {
            bResult = DynamicBufUpdate(pSrcData, uSrcDataSize, uDstOffset, bOverWrite);
        }
        else
        {
            bResult = StaticBufUpdate(pSrcData, uSrcDataSize, uDstOffset);
        }

        KGLOG_PROCESS_ERROR(bResult);
        bResult = true;
    Exit0:
        return bResult;
    }

    uint32_t KGFX_BufferDx12::GetDynamicOffset()
    {
        return m_DynamicBufOffset;
    }

    void* KGFX_BufferDx12::MapRange()
    {
        bool bRes = false;
        if (m_bDynamicBuf)
        {
            gfx::IKGFX_RenderContext* pRenderCtx = gfx::GetRenderContext();
            m_DynamicBufUpdateTime = NSEngine::GetRenderFrameMoveLoopCount();

            uint32_t id = (m_Resource)->Release();
            uint32_t offset = 0;
            KGFX_BufferImplDX12* GpuCoherentBufWeakPtr = nullptr;
            KGFX_CommandBufferDX12Impl* pdx12 = static_cast<KGFX_CommandBufferDX12Impl*>(pRenderCtx);
            bRes = pdx12->GetUsedTransientHeap()->AllocateDynamicBuffer(m_DynamicDesc.uByteWidth, m_DynamicDesc.uStructureByteStride, GpuCoherentBufWeakPtr, offset);
            assert(bRes);
            assert(GpuCoherentBufWeakPtr);
            m_Resource = GpuCoherentBufWeakPtr;
            m_Resource->AddRef();
            m_DynamicBufOffset = offset;
            m_DynamicBufSize = m_DynamicDesc.uByteWidth;
        }
        assert(m_Resource);
        return static_cast<byte*>(m_Resource->MapCpuData()) + m_DynamicBufOffset;
    }

    BOOL KGFX_BufferDx12::IsDynamic()
    {
        return m_bDynamicBuf;
    }

    uint64_t KGFX_BufferDx12::GetBufferDeviceAddress()
    {
        return m_Resource->GetBufResource()->GetGPUVirtualAddress() + m_DynamicBufOffset;
    }

    bool KGFX_BufferDx12::DynamicBufUpdate(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, bool bOverWrite)
    {
        gfx::IKGFX_RenderContext* pRenderCtx = gfx::GetRenderContext();
        if (!bOverWrite)
        {
            KGFX_CommandBufferDX12Impl* pdx12 = static_cast<KGFX_CommandBufferDX12Impl*>(pRenderCtx);
            MapRange();
            assert((uDstOffset + uSrcDataSize) <= m_DynamicDesc.uByteWidth);
            UploadSubBufferDataImpl(pdx12->GetD3D12CommandList(), pdx12->GetUsedTransientHeap(), m_Resource, m_DynamicBufOffset + uDstOffset, uSrcDataSize, pSrcData);
        }
        else
        {
            assert((uSrcDataSize + uDstOffset) <= m_DynamicDesc.uByteWidth);
            byte* pDstData = static_cast<byte*>(MapRange()) + uDstOffset;
            memcpy(pDstData, pSrcData, uSrcDataSize);

        }
        return true;
    }

    bool KGFX_BufferDx12::StaticBufUpdate(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset)
    {
        BOOL bResult = false;
        KGfxBarrier updateBarrier = {};
        IKGFX_GraphicDevice* pKGFXGraphicDevice = KGFX_GetGraphicDevice();
        /// 从当前的渲染窗口分配一个cmdBuf出来，生命周期由内部处理，所以不要delete
        KGFX_CommandBufferDX12Impl* renderContext = static_cast<KGFX_CommandBufferDX12Impl*>(pKGFXGraphicDevice->GetRenderContext());
        auto                        pTranslateHeap = renderContext->GetUsedTransientHeap();
        assert(pTranslateHeap);

        ID3D12GraphicsCommandList* pCmdList = renderContext->GetD3D12CommandList();
        assert(pCmdList);

        uint32_t uByteWidth = GetDesc()->uByteWidth;
        ASSERT((uDstOffset + uSrcDataSize) <= uByteWidth);
        if (uSrcDataSize == 0)
        {
            uSrcDataSize = uByteWidth - uDstOffset;
        }


        updateBarrier.eType = KGfxBarrier::EType::Buffer;
        updateBarrier.pBuffer = this;
        updateBarrier.eSRCAccess = KGfxAccess::Unknown;
        updateBarrier.eDSTAccess = KGfxAccess::CopyDst;
        renderContext->Transition(updateBarrier);
        bResult = UploadSubBufferDataImpl(pCmdList, pTranslateHeap, m_Resource, uDstOffset, uSrcDataSize, pSrcData);
        KGLOG_PROCESS_ERROR(bResult);

        bResult = true;
    Exit0:
        return bResult;
    }
} // namespace gfx
#endif
