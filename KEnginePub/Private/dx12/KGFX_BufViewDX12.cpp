// ReSharper disable CppClangTidyCppcoreguidelinesProTypeStaticCastDowncast
#include "KGFX_BufViewDX12.h"
#include "KGFX_BufferDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    KGFX_BufferViewDX12::KGFX_BufferViewDX12(IKGFX_Buffer* pBuf, const KGFX_BufferViewDesc& desc)
    {
        if (pBuf)
        {
            m_pBuf = static_cast<KGFX_BufferDx12*>(pBuf);
            m_pBuf->AddRef();
        }
        m_Desc = desc;
#if DetectKGFX_BufferViewDX12MemLeck
        static uint32_t allocid = 0;
        allocid++;
        m_pMemlectDecter = new char[allocid];
        if (allocid == 52)
        {
            int x = 0;
        }
#endif
    }

    KGFX_BufferViewDX12::~KGFX_BufferViewDX12()
    {
        SAFE_RELEASE(m_pBuf);
#if DetectKGFX_BufferViewDX12MemLeck
        SAFE_DELETE_ARRAY(m_pMemlectDecter);
#endif
    }

    KGFX_BufferViewDX12::KGFX_BufferViewDX12(KGFX_BufferViewDX12&& other) noexcept
    {
        SAFE_RELEASE(m_pBuf);

        m_pBuf = other.m_pBuf;
        other.m_pBuf = nullptr;
        m_Desc = { std::move(other.m_Desc) };
    }

    KGFX_BufferViewDX12& KGFX_BufferViewDX12::operator=(KGFX_BufferViewDX12&& other) noexcept
    {
        if (this == &other)
            return *this;

        SAFE_RELEASE(m_pBuf);
        m_pBuf = other.m_pBuf;
        other.m_pBuf = nullptr;
        m_Desc = { std::move(other.m_Desc) };
        return *this;
    }

    IKGFX_Buffer* KGFX_BufferViewDX12::GetResource()
    {
        return m_pBuf;
    }

    const KGFX_BufferViewDesc* KGFX_BufferViewDX12::GetViewDesc() const
    {
        return &m_Desc;
    }

    uint64_t KGFX_BufferViewDX12::GetCode()
    {
        throw std::runtime_error("no use");
    }

    uintptr_t KGFX_BufferViewDX12::GetNativeHandle()
    {
        if (!m_pBuf->IsDynamic() && m_uNativeHandle)
        {
            return m_uNativeHandle;
        }

        switch (m_Desc.eViewType)
        {
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN:
            assert(false);
            return {};
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
            m_uNativeHandle = GetCBV().cpuHandle.ptr;
            return m_uNativeHandle;
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
            m_uNativeHandle = GetSRV().cpuHandle.ptr;
            return m_uNativeHandle;
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
            assert(false);
            return {};
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
            assert(false);
            return {};
        case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
            m_uNativeHandle = GetUAV().cpuHandle.ptr;
            return m_uNativeHandle;
        default:
            assert(false);
            return {};
        }
    }

    void KGFX_BufferViewDX12::SetObjectName(const char* pcszName)
    {
        return;
    }

    uint32_t KGFX_BufferViewDX12::GetViewOffset() const
    {
        /// 一个view对于于一个buffer的偏移是由view上声明的偏移和buf自身相对于大buffer的偏移之和
        uint32_t byteOffset = m_Desc.uBytesOffset + m_pBuf->GetDynamicOffset();
        return byteOffset;
    }

    uint32_t KGFX_BufferViewDX12::GetViewRange() const
    {
        /// 我特别讨厌这种做法，按道理是必须自己声明大小，而不是要底层再去获取
        uint32_t byteRange = m_Desc.uBytesRange;
        uint32_t bufSize = m_pBuf->IsDynamic() ? m_pBuf->m_DynamicBufSize : m_pBuf->GetBufferImpl()->GetDesc()->uByteWidth;
        uint32_t updateTime = m_pBuf->m_DynamicBufUpdateTime;
        uint32_t updateTime2 = NSEngine::GetRenderFrameMoveLoopCount();
        byteRange = byteRange == 0 ? bufSize : byteRange;
        assert(byteRange <= bufSize);
        if (updateTime > 0)
        {
            assert(updateTime == updateTime2);
        }

        return byteRange;
    }

    void KGFX_BufferViewDX12::PlacedCreate(IKGFX_Buffer* pBuf, const KGFX_BufferViewDesc& desc)
    {
        if (pBuf)
        {
            SAFE_RELEASE(m_pBuf);
            m_pBuf = static_cast<KGFX_BufferDx12*>(pBuf);
            m_pBuf->AddRef();
            m_Desc = { std::move(desc) };
            m_uNativeHandle = 0;
        }
    }

    void* KGFX_BufferViewDX12::GetViewHandle()
    {
        throw std::runtime_error("GetViewHandle is not implemented");
    }

    D3D12Descriptor KGFX_BufferViewDX12::GetSRV() const
    {
        KGFX_BufferImplDX12* pImplDx12 = m_pBuf->GetBufferImpl();
        assert(pImplDx12);

        uint32_t byteOffset = GetViewOffset();
        uint32_t byteRange = GetViewRange();

        return pImplDx12->GetSRV(m_Desc.eFormat, m_Desc.uStructureStride, byteOffset, byteRange);
    }

    D3D12Descriptor KGFX_BufferViewDX12::GetUAV() const
    {
        KGFX_BufferImplDX12* pImplDx12 = m_pBuf->GetBufferImpl();
        assert(pImplDx12);
        uint32_t byteOffset = GetViewOffset();
        uint32_t byteRange = GetViewRange();

        return pImplDx12->GetUAV(m_Desc.eFormat, m_Desc.uStructureStride, byteOffset, byteRange);
    }

    D3D12Descriptor KGFX_BufferViewDX12::GetCBV() const
    {
        KGFX_BufferImplDX12* pImplDx12 = m_pBuf->GetBufferImpl();
        assert(pImplDx12);

        uint32_t byteOffset = GetViewOffset();
        uint32_t byteRange = GetViewRange();

        return pImplDx12->GetCBV(byteOffset, byteRange);
    }
    uint32_t KGFX_BufferViewDX12::GetBindlessHandle()
    {
        if (m_BindlessDescriptor.IsValid())
        {
            return m_BindlessDescriptor.Index;
        }

        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessManager = pGraphicDevice->GetDX12BindlessHeapManager();
        assert(pBindlessManager != nullptr);

        uint32_t BindlessIndex = pBindlessManager->Allocate(BindlessHeapType::Standard);
        m_BindlessDescriptor.Index = BindlessIndex;
        m_BindlessDescriptor.Type = BindlessHeapType::Standard;

        return BindlessIndex;
    }
}
