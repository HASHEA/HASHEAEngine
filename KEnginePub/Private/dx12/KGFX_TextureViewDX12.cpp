
#include "KGFX_TextureViewDX12.h"
#include "KGFX_RenderTargetDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_BindlessDx12.h"

namespace gfx
{

	gfx::KGFX_TextureViewDX12::KGFX_TextureViewDX12(const KGFX_TextureViewDesc& Desc, IKGFX_TextureResource* pTex)
	{
		assert(pTex);
		m_Desc = Desc;
		m_pTex = pTex;
		//m_pTex->AddRef();
	}

	KGFX_TextureViewDX12::~KGFX_TextureViewDX12()
	{
		//SAFE_RELEASE(m_pTex);
	}

    int32_t KGFX_TextureViewDX12::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            //if (m_pTex)
            //{
            //    auto piDevice = KGFX_GetGraphicDeviceDx12Internal();
            //    CHECK_ASSERT(piDevice);

            //    piDevice->GC_DelayReleaseObject(this, [this]() { SAFE_RELEASE(m_pTex); });
            //}
            //else
            KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
            if (m_BindlessDescriptor.IsValid())
            {
                D3D12BindlessDescriptorHeapManager* pBindlessManager = pGraphicDevice->GetDX12BindlessHeapManager();
                if(pBindlessManager)
                    pBindlessManager->Free(m_BindlessDescriptor.Type, m_BindlessDescriptor.Index);
                m_BindlessDescriptor = {};
            }

            {
                // 如果没有初始化成功，直接释放
                delete this;
            }
        }

        return nRef;
    }

    KGFX_TextureViewDX12::KGFX_TextureViewDX12(KGFX_TextureViewDX12&& other) noexcept
        : m_pTex(other.m_pTex)
        , m_Desc(other.m_Desc)
    {
        m_uNativeHandle = 0;
        other.m_pTex = nullptr;
    }


    KGFX_TextureViewDX12& KGFX_TextureViewDX12::operator=(KGFX_TextureViewDX12&& other) noexcept
    {
        if (this == &other)
            return *this;

        SAFE_RELEASE(m_pTex);
        m_pTex = other.m_pTex;
        m_Desc = other.m_Desc;
        other.m_pTex = nullptr;
        m_uNativeHandle = 0;
        return *this;
    }

    IKGFX_TextureResource* KGFX_TextureViewDX12::GetResource() const
	{
		return m_pTex;
	}

    uintptr_t KGFX_TextureViewDX12::GetNativeHandle()
	{
        if (m_uNativeHandle)
        {
            return m_uNativeHandle;
        }

        uintptr_t ret = {};
		switch (m_Desc.eViewType)
		{
		case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN:
			assert(false);
			break;
		case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
			assert(false);
			break;
		case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
			ret = GetSRV().cpuHandle.ptr;
			break;
		case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
			ret = GetRTV().cpuHandle.ptr;
			break;
		case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
			ret = GetDSV().cpuHandle.ptr;
			break;
		case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
			ret = GetUAV().cpuHandle.ptr;
			break;
		default: 
			assert(false);
		}
        m_uNativeHandle = ret;
		return ret;
	}

    uint32_t KGFX_TextureViewDX12::GetBindlessHandle()
    {
        assert(m_Desc.eViewType != KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV || m_Desc.eViewType != KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV);

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

	const KGFX_TextureViewDesc& KGFX_TextureViewDX12::GetViewDesc() const
	{
		return m_Desc;
	}

    void KGFX_TextureViewDX12::SetDebugName(const char* szDebugName)
    {
        /// DX12的view不支持设置名字，这个是VK的特性
    }

	D3D12Descriptor KGFX_TextureViewDX12::GetSRV() const
	{
		D3D12Descriptor ret = {};
		KGFX_TextureImplDx12* pTex = nullptr;
		pTex = dynamic_cast<KGFX_TextureImplDx12*>(m_pTex);
        assert(pTex);
		ret = pTex->GetSRV(m_Desc.eFormat, m_Desc.eViewDimension, m_Desc.sSubresourceRange);
		return ret;
	}

	D3D12Descriptor KGFX_TextureViewDX12::GetUAV() const
	{
		D3D12Descriptor ret = {};
		KGFX_TextureImplDx12* pTex = nullptr;
		pTex = dynamic_cast<KGFX_TextureImplDx12*>(m_pTex);
        assert(pTex);
		ret = pTex->GetUAV(m_Desc.eFormat, m_Desc.eViewDimension, m_Desc.sSubresourceRange);
		return ret;
	}

	D3D12Descriptor KGFX_TextureViewDX12::GetRTV() const
	{
		D3D12Descriptor ret = {};
		KGFX_TextureImplDx12* pTex = nullptr;
		pTex = dynamic_cast<KGFX_TextureImplDx12*>(m_pTex);
        assert(pTex);
		ret = pTex->GetRTV(m_Desc.eFormat, m_Desc.eViewDimension, m_Desc.sSubresourceRange);
		return ret;
	}

	D3D12Descriptor KGFX_TextureViewDX12::GetDSV() const
	{
		D3D12Descriptor ret = {};
		KGFX_TextureImplDx12* pTex = nullptr;
		pTex = dynamic_cast<KGFX_TextureImplDx12*>(m_pTex);
        assert(pTex);
		ret = pTex->GetDSV(m_Desc.eFormat, m_Desc.eViewDimension, m_Desc.sSubresourceRange);
		return ret;
	}
}
