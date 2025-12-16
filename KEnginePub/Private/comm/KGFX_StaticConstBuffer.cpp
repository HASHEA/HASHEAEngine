#include "KGFX_StaticConstBuffer.h"
namespace gfx
{
    KGFX_MemoryConstBuffer::~KGFX_MemoryConstBuffer()
    {
        Reset();
    }

    void KGFX_MemoryConstBuffer::Reset()
    {
        SAFE_RELEASE(m_GpuBuffer);
        SAFE_RELEASE(m_CBV);
        m_CpuData = {};
        m_BufSize = 0;
    }

    bool KGFX_MemoryConstBuffer::Init(uint32_t bufSize, const char* pcszBufferName)
    {
        bool bRet = false;
        ASSERT(bufSize > 0);
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
        if (bufSize > 0 && bufSize != m_BufSize)
        {
            Uninit();
            m_BufSize = bufSize;
            m_CpuData.resize(m_BufSize);
        }
        if (m_GpuBuffer == nullptr)
        {
            KGfxBufferDesc desc = {};
            desc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            desc.uByteWidth = m_BufSize;
            desc.uUsageFlags = BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            desc.bForceStatic = true;
            bRet = pKGFXGraphicDevice->CreateBuffer(&m_GpuBuffer, desc, nullptr);
            ASSERT(bRet);
            if(pcszBufferName)
                m_GpuBuffer->SetDebugName(pcszBufferName);
        }
        if (m_CBV == nullptr)
        {
            KGFX_BufferViewDesc viewDesc = {};
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV;
            viewDesc.uBytesOffset = 0;
            viewDesc.uBytesRange = m_BufSize;
            bRet = pKGFXGraphicDevice->CreateBufferView(m_GpuBuffer, viewDesc, &m_CBV, nullptr);
            ASSERT(bRet);
        }
        return bRet;
    }

    void KGFX_MemoryConstBuffer::Update(IKGFX_RenderContext* commandBuffer)
    {
        commandBuffer->CmdUpdateSubResource(m_GpuBuffer, 0, m_BufSize, m_CpuData.data());
        KGfxBarrier cbufBarrier = {};
        cbufBarrier.eType = KGfxBarrier::EType::Buffer;
        cbufBarrier.pBuffer = m_GpuBuffer;
        cbufBarrier.eSRCAccess = KGfxAccess::Unknown;
        cbufBarrier.eDSTAccess = KGfxAccess::ConstBuffer;
        commandBuffer->Transition(cbufBarrier);
    }

    uint32_t KGFX_MemoryConstBuffer::GetCBufSize() const
    {
        ASSERT(m_BufSize > 0);
        return m_BufSize;
    }

    uint8_t* KGFX_MemoryConstBuffer::GetCpuData()
    {
        return m_CpuData.data();
    }

    IKGFX_BufferView* KGFX_MemoryConstBuffer::GetCBV() const
    {
        return m_CBV;
    }

    IKGFX_Buffer* KGFX_MemoryConstBuffer::GetGfxBuffer() const
    {
        return m_GpuBuffer;
    }
}
