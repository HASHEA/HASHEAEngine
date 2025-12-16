// ReSharper disable CppClangTidyCppcoreguidelinesProTypeStaticCastDowncast
#include "KGFX_ConstBufferDX12.h"
#include "KGFX_BufferDx12.h"
#include "KGFX_BufViewDX12.h"
#include "KGFX_CommandBufferDX12Impl.h"
#include "KGFX_TransientHeap.h"
#include "KGFX_GraphiceDeviceDx12.h"

namespace gfx
{

    void KGFX_MemoryConstBufferDX12::Uninit()
    {
        SAFE_RELEASE(m_GpuBuffer);
        SAFE_RELEASE(m_CBV);
        m_CpuData = {};
        m_BufSize = 0;
        KGFX_GetGraphicDeviceDx12Internal()->m_uDynamicBufferCount--;
    }

    bool KGFX_MemoryConstBufferDX12::Init(uint32_t bufSize, const char* pcszBufferName)
    {
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
        bool bRet = false;
        assert(bufSize > 0);
        m_BufSize = bufSize;
        m_CpuData.resize(m_BufSize);

        m_GpuBuffer = new KGFX_BufferDx12;

        KGFX_BufferViewDesc viewDesc = {};
        viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV;
        viewDesc.uBytesOffset = 0;
        viewDesc.uBytesRange = m_BufSize;
        bRet = pKGFXGraphicDevice->CreateBufferView(m_GpuBuffer, viewDesc, &m_CBV, nullptr);
        assert(bRet);
        KGFX_GetGraphicDeviceDx12Internal()->m_uDynamicBufferCount++;
        return true;
    }

    void KGFX_MemoryConstBufferDX12::Update(IKGFX_RenderContext* commandBuffer)
    {
        bool bRet = false;
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();

        {
            uint32_t offset = 0;
            KGFX_BufferImplDX12* GpuCoherentBufWeakPtr = nullptr;
            KGFX_CommandBufferDX12Impl* pdx12 = dynamic_cast<KGFX_CommandBufferDX12Impl*>(commandBuffer);
            pdx12->GetUsedTransientHeap()->AllocateConstBuffer(m_BufSize, GpuCoherentBufWeakPtr, offset);
            assert(GpuCoherentBufWeakPtr);

            if (m_GpuBuffer == nullptr)
            {
                m_GpuBuffer = new KGFX_BufferDx12;
            }

            m_GpuBuffer->Create(GpuCoherentBufWeakPtr, offset);

            UploadSubBufferDataImpl(pdx12->GetD3D12CommandList(), pdx12->GetUsedTransientHeap(), GpuCoherentBufWeakPtr, offset, m_BufSize, m_CpuData.data());

            KGFX_BufferViewDesc viewDesc = {};
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV;
            viewDesc.uBytesOffset = 0;
            viewDesc.uBytesRange = m_BufSize;

            if (m_CBV == nullptr)
            {
                IKGFX_BufferView* cbv = nullptr;
                bRet = pKGFXGraphicDevice->CreateBufferView(m_GpuBuffer, viewDesc, &cbv, nullptr);
                assert(bRet);

                m_CBV = static_cast<KGFX_BufferViewDX12*>(cbv);
            }
            else
            {
                static_cast<KGFX_BufferViewDX12*>(m_CBV)->PlacedCreate(m_GpuBuffer, std::move(viewDesc));
            }
        }
    }

    uint32_t KGFX_MemoryConstBufferDX12::GetCBufSize() const
    {
        assert(m_BufSize > 0);
        return m_BufSize;
    }

    uint8_t* KGFX_MemoryConstBufferDX12::GetCpuData()
    {
        return m_CpuData.data();
    }

    IKGFX_BufferView* KGFX_MemoryConstBufferDX12::GetCBV() const
    {
        return m_CBV;
    }

    IKGFX_Buffer* KGFX_MemoryConstBufferDX12::GetGfxBuffer() const
    {
        return m_GpuBuffer;
    }

    void* KGFX_MemoryConstBufferDX12::MapRange()
    {
        bool bRet = false;
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDeviceDx12Internal();

        uint32_t offset = 0;
        KGFX_BufferImplDX12* GpuCoherentBufWeakPtr = nullptr;
        KGFX_CommandBufferDX12Impl* pdx12 = static_cast<KGFX_CommandBufferDX12Impl*>(gfx::GetRenderContext());
        pdx12->GetUsedTransientHeap()->AllocateConstBuffer(m_BufSize, GpuCoherentBufWeakPtr, offset);
        assert(GpuCoherentBufWeakPtr);

        if (m_GpuBuffer == nullptr)
        {
            m_GpuBuffer = new KGFX_BufferDx12;
        }

        m_GpuBuffer->Create(GpuCoherentBufWeakPtr, offset);

        KGFX_BufferViewDesc viewDesc = {};
        viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV;
        viewDesc.uBytesOffset = 0;
        viewDesc.uBytesRange = m_BufSize;

        if (m_CBV == nullptr)
        {
            IKGFX_BufferView* cbv = nullptr;
            bRet = pKGFXGraphicDevice->CreateBufferView(m_GpuBuffer, viewDesc, &cbv, nullptr);
            assert(bRet);

            m_CBV = static_cast<KGFX_BufferViewDX12*>(cbv);
        }
        else
        {
            static_cast<KGFX_BufferViewDX12*>(m_CBV)->PlacedCreate(m_GpuBuffer, std::move(viewDesc));
        }


        return (uint8_t*)m_GpuBuffer->GetBufferImpl()->MapCpuData() + offset;
    }
}
