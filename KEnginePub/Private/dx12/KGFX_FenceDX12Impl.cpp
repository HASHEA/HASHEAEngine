#include "KGFX_FenceDX12Impl.h"
#include "KGFX_GraphiceDeviceDx12.h"

namespace gfx
{

    KGFX_FenceDX12Impl::~KGFX_FenceDX12Impl()
    {
        SAFE_RELEASE(m_D3d12Fence);
    }

    void KGFX_FenceDX12Impl::Init()
    {
        KGFX_GraphicDeviceDx12* pDX12Device = KGFX_GetGraphicDeviceDx12Internal();
        m_D3d12Fence = pDX12Device->GetDX12CommandQueueImpl()->GetD3D12Fence();
        m_D3d12Fence->AddRef();
        m_FenceValue = pDX12Device->GetDX12CommandQueueImpl()->GetCurrentFenceValue();
    }

	bool KGFX_FenceDX12Impl::IsSubmitted() const
	{
        return m_bSubmitted;
	}

	void KGFX_FenceDX12Impl::Clear()
    {
        m_FenceValue = 0;
        m_bSubmitted = false;
    }

    bool KGFX_FenceDX12Impl::Query()
    {
        KGFX_GraphicDeviceDx12* pDX12Device = KGFX_GetGraphicDeviceDx12Internal();
        uint64_t fenceValue = pDX12Device->GetDX12CommandQueueImpl()->GetD3D12Fence()->GetCompletedValue();
        if (fenceValue > m_FenceValue)
        {
            m_bSubmitted = false;
        }
        return fenceValue > m_FenceValue;
    }

    bool KGFX_FenceDX12Impl::GetCurrentValue(uint64_t* outValue)
    {
        KGFX_GraphicDeviceDx12* pDX12Device = KGFX_GetGraphicDeviceDx12Internal();
        uint64_t fenceValue = pDX12Device->GetDX12CommandQueueImpl()->GetCurrentFenceValue();

        if (outValue)
            *outValue = fenceValue;
        return true;
    }

    void* KGFX_FenceDX12Impl::GetNativeHandle()
    {
        return m_D3d12Fence;
    }

}
