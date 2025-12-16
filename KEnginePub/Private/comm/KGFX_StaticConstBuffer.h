#pragma once
#include "KGFX_GraphicDevice.h"
#include "KEnginePub/Private/stdafx.h"

namespace gfx
{
    /// 这个文件定义的cbuf是VK和DX实现是一致的，可以直接使用


    template <typename T>
    class KGFX_StaticConstBuffer final : public IKGFX_ConstBuffer
    {
        //static_assert(alignof(T) == 16, "T must be aligned to 16 bytes");
        static_assert(std::is_standard_layout_v<T>, "T must have a standard layout");
    public:
        KGFX_StaticConstBuffer() = default;

        ~KGFX_StaticConstBuffer() override
        {
            Uninit();
        }

        KGFX_StaticConstBuffer(const KGFX_StaticConstBuffer& other) = delete;
        KGFX_StaticConstBuffer& operator=(const KGFX_StaticConstBuffer&) = delete;

        KGFX_StaticConstBuffer(const KGFX_StaticConstBuffer&& other) = delete;
        KGFX_StaticConstBuffer& operator=(const KGFX_StaticConstBuffer&&) = delete;

        void Uninit() override
        {
            SAFE_RELEASE(m_GpuBuffer);
            SAFE_RELEASE(m_CBV);
        }

        bool Init(uint32_t bufSize = 0, const char* pcszBufferName = nullptr) override;

        void Update(IKGFX_RenderContext* commandBuffer) override;

        T& GetTypeCpuData();

        IKGFX_BufferView* GetCBV() const override;
        uint8_t* GetCpuData() override;
        uint32_t GetCBufSize() const override;
        IKGFX_Buffer* GetGfxBuffer() const override;

    private:
        T m_CpuData = {};
        IKGFX_Buffer* m_GpuBuffer = nullptr;
        IKGFX_BufferView* m_CBV = nullptr;
    };

    template <typename T>
    T& KGFX_StaticConstBuffer<T>::GetTypeCpuData()
    {
        return m_CpuData;
    }

    template <typename T>
    IKGFX_BufferView* KGFX_StaticConstBuffer<T>::GetCBV() const
    {
        assert(m_CBV);
        return m_CBV;
    }

    template <typename T>
    uint8_t* KGFX_StaticConstBuffer<T>::GetCpuData()
    {
        uint8_t* dataPtr = (uint8_t*)(&m_CpuData);

        return dataPtr;
    }

    template <typename T>
    uint32_t KGFX_StaticConstBuffer<T>::GetCBufSize() const
    {
        return (uint32_t) sizeof(T);
    }

    template <typename T>
    IKGFX_Buffer* KGFX_StaticConstBuffer<T>::GetGfxBuffer() const
    {
        return m_GpuBuffer;
    }

    template <typename T>
    bool KGFX_StaticConstBuffer<T>::Init(uint32_t bufSize, const char* pcszBufferName)
    {
        bool bRet = true;
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
        if (m_GpuBuffer == nullptr)
        {
            KGfxBufferDesc desc = {};
            desc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            desc.uByteWidth = sizeof(T);
            desc.uUsageFlags = BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            desc.bForceStatic = true;
           
            bRet = pKGFXGraphicDevice->CreateBuffer(&m_GpuBuffer, desc, nullptr);
            assert(bRet);
            if (pcszBufferName)
            {
                m_GpuBuffer->SetDebugName(pcszBufferName);
            }
        }

        if (m_CBV == nullptr)
        {
            KGFX_BufferViewDesc viewDesc = {};
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV;
            viewDesc.uBytesOffset = 0;
            viewDesc.uBytesRange = sizeof(T);

            bRet = pKGFXGraphicDevice->CreateBufferView(m_GpuBuffer, viewDesc, &m_CBV, nullptr);
            assert(bRet);
        }

        return bRet;
    }

    template <typename T>
    void KGFX_StaticConstBuffer<T>::Update(IKGFX_RenderContext* commandBuffer)
    {
        commandBuffer->CmdUpdateSubResource(m_GpuBuffer, 0, sizeof(T), &m_CpuData);
        KGfxBarrier cbufBarrier = {};
        cbufBarrier.eType = KGfxBarrier::EType::Buffer;
        cbufBarrier.pBuffer = m_GpuBuffer;
        cbufBarrier.eSRCAccess = KGfxAccess::Unknown;
        cbufBarrier.eDSTAccess = KGfxAccess::ConstBuffer;
        commandBuffer->Transition(cbufBarrier);
    }

    class KGFX_MemoryConstBuffer final : public IKGFX_ConstBuffer
    {
    public:
        KGFX_MemoryConstBuffer() = default;
        ~KGFX_MemoryConstBuffer() override;
        KGFX_MemoryConstBuffer(const KGFX_MemoryConstBuffer& other) = delete;
        KGFX_MemoryConstBuffer& operator=(const KGFX_MemoryConstBuffer&) = delete;
        KGFX_MemoryConstBuffer(const KGFX_MemoryConstBuffer&& other) = delete;
        KGFX_MemoryConstBuffer& operator=(const KGFX_MemoryConstBuffer&&) = delete;
        void Reset();
        bool Init(uint32_t bufSize, const char *pcszBufferName = nullptr) override;
        void Update(IKGFX_RenderContext* commandBuffer) override;
        uint32_t GetCBufSize() const override;
        uint8_t* GetCpuData() override;
        IKGFX_BufferView* GetCBV() const override;        
        IKGFX_Buffer* GetGfxBuffer() const override;


    private:
        uint32_t m_BufSize = 0;
        std::vector<uint8_t> m_CpuData = {};
        IKGFX_Buffer* m_GpuBuffer = nullptr;
        IKGFX_BufferView* m_CBV = nullptr;
    };
}
