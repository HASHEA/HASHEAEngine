////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KGFX_GraphicDeviceVK.cpp
//  Creator     : HuaFei
//  Create Date : 2024-01
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "KGFX_GraphicDeviceVK.h"
#include "KVulkanRenderContext.h"
#include "../IGFX_Private.h"
#include "Engine/KGLog.h"
#include "GFXVulkan.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanRenderFrameBuffer.h"
#include "../comm/KGFX_StaticConstBuffer.h"
#include "KVulkanTexture.h"
#include "KVulkanProgram.h"
#include "../loader/KGFX_MemTexture.h"
#include "../comm/KGFX_ShaderCombinedResult.h"
#include "KShaderResourceVK.h"
#include "KShaderResourcePoolVK.h"
#include "KBase/Public/KMemLeak.h"
#include "KVulkanRayTracing.h"


namespace gfx
{

    class KGFX_DeviceVK
    {
    public:
        KGFX_DeviceVK();
        ~KGFX_DeviceVK();
        void InInit();
        void UnInit();
        void BeginFrame(gfx::enumGraphicContext context);
        void EndFrame();

        void BeginFrameBuffer(IKGFX_RenderFrameBuffer* frameBuffer);
        void EndFrameBuffer();

        IKGFX_Swapchain* GetContext(gfx::enumGraphicContext contextType);
        void DeleteContext(gfx::enumGraphicContext contextType);
        IKGFX_RenderContext* GetRenderContext();

        void Setup(const gfx::KWindow* pWindowInfo);

        IKGFX_GraphicsProgram* CreateProgramVSFS();
        IKGFX_ComputeProgram* CreateProgramCS();

        IKGFX_Sampler* GetSamplerByState(gfx::KSamplerState* pSamplerState);

        BOOL CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData);
        BOOL CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode);

    private:
        std::vector<KGFX_SwapchainVK*> m_vContexts;
        KVulkanRenderContext* m_pRenderContext = nullptr;
        // gfx::KVulkanRenderPass *m_RenderPass[gfx::RENDER_PASS_COUNT] = {0};
    };

    KGFX_DeviceVK::KGFX_DeviceVK()
    {
    }

    KGFX_DeviceVK::~KGFX_DeviceVK()
    {
    }

    void KGFX_DeviceVK::InInit()
    {
        auto pContext = new  KGFX_SwapchainVK;
        pContext->Init(nullptr);
        m_vContexts.push_back(pContext);
    }

    void KGFX_DeviceVK::UnInit()
    {
        for (size_t i = 0; i < m_vContexts.size(); ++i)
        {
            if (m_vContexts[i])
            {
                m_vContexts[i]->UnInit();
            }
            SAFE_DELETE(m_vContexts[i]);
        }

        SAFE_DELETE(m_pRenderContext);

        KGraphicDevice* pDevice = GetGraphicDevice();
        pDevice->Uninit();
    }

    void KGFX_DeviceVK::BeginFrame(enumGraphicContext context)
    {
    }
    void KGFX_DeviceVK::EndFrame()
    {
    }

    void KGFX_DeviceVK::BeginFrameBuffer(IKGFX_RenderFrameBuffer* frameBuffer)
    {
    }

    void KGFX_DeviceVK::EndFrameBuffer()
    {
    }

    gfx::IKGFX_Swapchain* KGFX_DeviceVK::GetContext(enumGraphicContext contextType)
    {
        if (contextType >= m_vContexts.size())
        {
            return nullptr;
        }

        if (contextType < m_vContexts.size())
        {
            return m_vContexts[contextType];
        }
        return nullptr;
    }

    void KGFX_DeviceVK::DeleteContext(gfx::enumGraphicContext contextType)
    {
        ASSERT(contextType < m_vContexts.size());
        if (contextType < m_vContexts.size())
        {
            if (m_vContexts[contextType])
            {
                m_vContexts[contextType]->UnInit();
                SAFE_DELETE(m_vContexts[contextType]);
            }
        }
    }

    IKGFX_RenderContext* KGFX_DeviceVK::GetRenderContext()
    {
        if (m_pRenderContext)
            return m_pRenderContext;

        m_pRenderContext = new KVulkanRenderContext;
        BOOL bRetCode = m_pRenderContext->Init();
        ASSERT(bRetCode);
        if (!bRetCode)
        {
            SAFE_DELETE(m_pRenderContext);
        }
        return m_pRenderContext;
    }

    void KGFX_DeviceVK::Setup(const KWindow* pWindowInfo)
    {
        uint32_t contextId = (uint32_t)pWindowInfo->m_uId;
        if (contextId >= m_vContexts.size())
        {
            m_vContexts.resize(contextId + 1);
        }

        if (!m_vContexts[contextId])
        {
            m_vContexts[contextId] = new KGFX_SwapchainVK;
        }
        else
        {
            m_vContexts[contextId]->UnInit();
        }
        m_vContexts[contextId]->Init(pWindowInfo);
    }

    BOOL KGFX_DeviceVK::CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        gfx::KGraphicDevice* pDevice = gfx::GetGraphicDevice();
        bRetCode = pDevice->CreateRenderTarget2D(ppRenderTarget, pRenderTargetDesc, bTileOptimize, pRetCheckCode);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    IKGFX_GraphicsProgram* KGFX_DeviceVK::CreateProgramVSFS()
    {
        return new KGFX_GraphicsProgramVK();
    }

    IKGFX_ComputeProgram* KGFX_DeviceVK::CreateProgramCS()
    {
        return new KGFX_ComputeProgramVK();
    }

    IKGFX_Sampler* KGFX_DeviceVK::GetSamplerByState(gfx::KSamplerState* pSamplerState)
    {
        gfx::KGraphicDevice* pDevice = gfx::GetGraphicDevice();
        return pDevice->GetSamplerByState(pSamplerState);
    }

    BOOL KGFX_DeviceVK::CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData)
    {
        gfx::KGraphicDevice* pDevice = gfx::GetGraphicDevice();
        BOOL bRet = false;
        if (bufDesc.uUsageFlags == gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT &&
            DrvOption::bSupportDynamicUBO &&
            !bufDesc.bForceStatic
            )
        {
            *ppRetBuffer = pDevice->CreateDynamicBuffer(bufDesc.uByteWidth);
            if (*ppRetBuffer)
            {
                bRet = true;
            }
        }
        else
        {
            bRet = pDevice->CreateBuffer(ppRetBuffer, bufDesc, pData);
        }
        return bRet;
    }

    KGFX_GraphicDeviceVK::KGFX_GraphicDeviceVK()
    {
        m_pKGFXDevice = new gfx::KGFX_DeviceVK;
    }

    KGFX_GraphicDeviceVK::~KGFX_GraphicDeviceVK()
    {
        SAFE_DELETE(m_pKGFXDevice);
    }

    extern KGraphicDevice* g_pGraphic;

    BOOL KGFX_GraphicDeviceVK::Init(const gfx::RenderSystemInfo& renderSysteInfo)
    {
        BOOL bResult = false;

        g_pGraphic = new KVulkanGraphicDevice();
        CHECK_ASSERT(g_pGraphic);

        BOOL bRetCode = g_pGraphic->Init(renderSysteInfo);
        KGLOG_PROCESS_ERROR(bRetCode);

        KG_PROCESS_ERROR(!renderSysteInfo.bByShaderBuilderCmdTools);

        gfx::GetGraphicDevice()->bInitedGraphic = true;

        bResult = true;
    Exit0:
        return bResult;
    }

    void KGFX_GraphicDeviceVK::UnInit()
    {
        m_pKGFXDevice->UnInit();
        KGFX_GraphicDevice::UnInit();

        if (g_pGraphic)
        {
            g_pGraphic->Uninit();
        }
        SAFE_DELETE(g_pGraphic);

        DestroyVulkanDevice();
    }

    void KGFX_GraphicDeviceVK::Setup(const KWindow* pWindowInfo)
    {
        m_pKGFXDevice->Setup(pWindowInfo);
        //Exit0:
        //		return;
    }

    void KGFX_GraphicDeviceVK::DeleteContext(enumGraphicContext contextType)
    {
        m_pKGFXDevice->DeleteContext(contextType);
    }

    gfx::IKGFX_Swapchain* KGFX_GraphicDeviceVK::GetContext(enumGraphicContext eGraphicCtx)
    {
        gfx::IKGFX_Swapchain* pRetContext = nullptr;

        pRetContext = m_pKGFXDevice->GetContext(eGraphicCtx);
        //Exit0:
        return pRetContext;
    }

    BOOL KGFX_GraphicDeviceVK::GetGfxDevice(vks::KVulkanDevice** ppRetGfxDevice) const
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->GetGfxDevice(ppRetGfxDevice);
    Exit0:
        return bResult;
    }

    void KGFX_GraphicDeviceVK::QueueWaitIdle(enumForProcessType commandType)
    {
        PROF_CPU_DETAIL("KGFX_GraphicDeviceVK::QueueWaitIdle");
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        VkQueue             pQueue = nullptr;
        switch (commandType)
        {
        case gfx::FOR_GRPAHIC:
            pQueue = ::GetGraphicQueue();
            break;
        case gfx::FOR_COMPUTE:
            pQueue = ::GetComputeQueue();
            break;
        case gfx::FOR_TRANSFER:
            pQueue = ::GetTransferQueue();
            break;
        default:
            break;
        }
        if (pQueue)
        {
            vks::vkQueueWaitIdle(pQueue);
        }
    }

    IKGFX_RenderContext* KGFX_GraphicDeviceVK::GetRenderContext()
    {
        return m_pKGFXDevice->GetRenderContext();
    }

    const char* KGFX_GraphicDeviceVK::GetDeviceName() const
    {
        const char* pRetDeviceName = "";

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        pRetDeviceName = pGraphicDevice->GetDeviceName();
    Exit0:
        return pRetDeviceName;
    }

    void KGFX_GraphicDeviceVK::DeviceWaitIdle()
    {
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        pGraphicDevice->DeviceWaitIdle();
    Exit0:
        return;
    }

    gfx::TextureFormatInfo KGFX_GraphicDeviceVK::GetTextureFormatInfo(enumTextureFormat eFormat) const
    {
        gfx::TextureFormatInfo sRetFormatInfo;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        sRetFormatInfo = pGraphicDevice->GetTextureFormatInfo(eFormat);
    Exit0:
        return sRetFormatInfo;
    }

    BOOL KGFX_GraphicDeviceVK::CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode)
    {
        BOOL bResult = FALSE;

        bResult = m_pKGFXDevice->CreateRenderTarget(ppRenderTarget, pRenderTargetDesc, bTileOptimize, pRetCheckCode);
        //Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::CreateRenderFrameBuffer(IKGFX_RenderFrameBuffer** ppRenderFrameBuffer, const KGfxFrameBufferDesc& fbDesc)
    {
        PROF_CPU();
        BOOL bRet = false;

        KVulkanRenderFrameBuffer* pRenderFrameBuffer = new KVulkanRenderFrameBuffer;
        CHECK_ASSERT(pRenderFrameBuffer);

        BOOL bRetCode = pRenderFrameBuffer->Create(&fbDesc);
        KGLOG_ASSERT_EXIT(bRetCode);

        *ppRenderFrameBuffer = pRenderFrameBuffer;
        pRenderFrameBuffer = nullptr;

        bRet = true;
    Exit0:
        SAFE_DELETE(pRenderFrameBuffer);

        return bRet;
    }

    BOOL KGFX_GraphicDeviceVK::CreateSignalFence(KSignalFence** ppRetSignalFence)
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        KGLOG_ASSERT_EXIT(ppRetSignalFence);
        bResult = pGraphicDevice->CreateSignalFence(ppRetSignalFence);
    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::IsFp16Supported() const
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->IsFp16Supported();

    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::IsSubGoupQuadSupported() const
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->IsSubGoupQuadSupported();

    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::IsSubGoupF16Supported() const
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->IsSubGoupF16Supported();

    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::IsAtomicUint64Supported() const
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->IsAtomicUint64Supported();

    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::IsInitedGraphic()
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->bInitedGraphic;

    Exit0:
        return bResult;
    }

    void KGFX_GraphicDeviceVK::FrameMove(BOOL bFrameRendered)
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        pGraphicDevice->FrameMove(bFrameRendered);
        KGFX_GraphicDevice::FrameMove(bFrameRendered);

    Exit0:
        return;
    }

    IKGFX_GraphicsProgram* KGFX_GraphicDeviceVK::CreateGraphicsProgram()
    {
        IKGFX_GraphicsProgram* pRetProgram = m_pKGFXDevice->CreateProgramVSFS();
        //Exit0:
        return pRetProgram;
    }

    IKGFX_ComputeProgram* KGFX_GraphicDeviceVK::CreateComputeProgram()
    {
        return m_pKGFXDevice->CreateProgramCS();
    }

    //IKGFX_Sampler* KGFX_GraphicDeviceVK::GetSamplerByState(KSamplerState* pSamplerState)
    //{
    //    IKGFX_Sampler* pRetSampler = m_pKGFXDevice->GetSamplerByState(pSamplerState);
    //    //Exit0:
    //    return pRetSampler;
    //}

    IKGFX_Sampler* KGFX_GraphicDeviceVK::GetSamplerByState(KSamplerState* pSamplerState)
    {
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        return pGraphicDevice->GetSamplerByState(pSamplerState);
    }

    bool KGFX_GraphicDeviceVK::CreateTexture(const KGFX_TextureDesc& texDsec, const char* szDebugName, IKGFX_TextureResource** outResource)
    {
        bool bResult = FALSE;
        BOOL bRetCode = FALSE;
        KVulkanTexture* pCreatedTexture = nullptr;

        KGLOG_ASSERT_EXIT(outResource);
        *outResource = nullptr;

        KGLOG_ASSERT_EXIT(szDebugName);

        pCreatedTexture = new KVulkanTexture();
        CHECK_ASSERT(pCreatedTexture);

        bRetCode = pCreatedTexture->Create(texDsec, szDebugName);
        KGLOG_ASSERT_EXIT(bRetCode);

        *outResource = pCreatedTexture;
        pCreatedTexture = nullptr;

        bResult = true;
    Exit0:
        SAFE_RELEASE(pCreatedTexture);
        return bResult;
    }

    bool KGFX_GraphicDeviceVK::CreateTextureView(IKGFX_TextureResource* texResource, KGFX_TextureViewDesc const& viewDesc, IKGFX_TextureView** outView)
    {
        bool bResult = FALSE;
        BOOL bRetCode = FALSE;
        KVulkanTexture* pResource = (KVulkanTexture*)texResource;
        KVulkanTextureView* pCreatedView = nullptr;

        KGLOG_ASSERT_EXIT(outView);
        *outView = nullptr;

        KGLOG_ASSERT_EXIT(pResource);

        pCreatedView = new KVulkanTextureView();
        CHECK_ASSERT(pCreatedView);

        bRetCode = pCreatedView->Create(pResource, &viewDesc, pResource->GetDebugName());
        KGLOG_ASSERT_EXIT(bRetCode);

        *outView = pCreatedView;
        pCreatedView = nullptr;

        bResult = true;
    Exit0:
        SAFE_RELEASE(pCreatedView);
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData)
    {
        BOOL               bResult = FALSE;

        KGLOG_ASSERT_EXIT(ppRetBuffer);

        bResult = m_pKGFXDevice->CreateBuffer(ppRetBuffer, bufDesc, pData);
    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& sViewDesc, IKGFX_BufferView** pRefBufferView, const char* pcszDebugName)
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->CreateBufferView(pBuffer, sViewDesc, pRefBufferView, pcszDebugName);
    Exit0:
        return bResult;
    }

    gfx::IKGFX_Buffer* KGFX_GraphicDeviceVK::CreateDynamicBuffer(const KGfxBufferDesc& bufDesc, BOOL bShareMode)
    {
        gfx::IKGFX_Buffer* pRetBuffer = nullptr;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        pRetBuffer = pGraphicDevice->CreateDynamicBuffer(bufDesc.uByteWidth, bufDesc.uUsageFlags, bShareMode);
    Exit0:
        return pRetBuffer;
    }

    int KGFX_GraphicDeviceVK::GetDynamicBufferCount()
    {
        int nDynamicBufferCount = 0;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        nDynamicBufferCount = pGraphicDevice->GetDynamicBufferCount();
    Exit0:
        return nDynamicBufferCount;
    }

    class KGFX_DynamicMemoryConstBufferVK final : public IKGFX_DynamicConstBuffer
    {
    public:
        KGFX_DynamicMemoryConstBufferVK() = default;
        ~KGFX_DynamicMemoryConstBufferVK() override
        {
            Uninit();
        }
        KGFX_DynamicMemoryConstBufferVK(const KGFX_DynamicMemoryConstBufferVK& other) = delete;
        KGFX_DynamicMemoryConstBufferVK& operator=(const KGFX_DynamicMemoryConstBufferVK&) = delete;
        KGFX_DynamicMemoryConstBufferVK(const KGFX_DynamicMemoryConstBufferVK&& other) = delete;
        KGFX_MemoryConstBuffer& operator=(const KGFX_MemoryConstBuffer&&) = delete;
        void Uninit()
        {
            SAFE_RELEASE(m_GpuBuffer);
            SAFE_RELEASE(m_CBV);
            m_CpuData = {};
            m_BufSize = 0;
        }
        bool Init(uint32_t bufSize, const char* pcszName = nullptr) override
        {
            bool bRet = false;
            assert(bufSize > 0);
            gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
            if (bufSize > 0 && bufSize != m_BufSize)
            {
                Uninit();
                m_BufSize = bufSize;
                m_CpuData.resize(m_BufSize);
            }
            if (m_GpuBuffer == nullptr)
            {
                gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
                ASSERT(pKGFXGraphicDevice);
                ASSERT(bufSize % 16 == 0 && "Uniform buffer must be aligned to 16 byte");

                if (DrvOption::bSupportDynamicUBO)
                {
                    gfx::KGfxBufferDesc bufDesc;
                    bufDesc.eResAccessFlags = gfx::KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
                    bufDesc.uByteWidth = bufSize;
                    bufDesc.uStructureByteStride = 0;
                    bufDesc.uUsageFlags = gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                    m_GpuBuffer = pKGFXGraphicDevice->CreateDynamicBuffer(bufDesc);
                }
                else
                {
                    gfx::KGfxBufferDesc bufDesc;
                    bufDesc.eResAccessFlags = gfx::KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
                    bufDesc.uByteWidth = bufSize;
                    bufDesc.uStructureByteStride = 0;
                    bufDesc.uUsageFlags = gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                    pKGFXGraphicDevice->CreateBuffer(&m_GpuBuffer, bufDesc, nullptr);
                    m_GpuBuffer->SetDebugName(pcszName);
                }
                ASSERT(m_GpuBuffer);
            }
            if (m_CBV == nullptr)
            {
                KGFX_BufferViewDesc viewDesc = {};
                viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV;
                viewDesc.uBytesOffset = 0;
                viewDesc.uBytesRange = m_BufSize;
                bRet = pKGFXGraphicDevice->CreateBufferView(m_GpuBuffer, viewDesc, &m_CBV, pcszName);
                ASSERT(bRet);
            }
            return bRet;
        }
        void Update(IKGFX_RenderContext* commandBuffer) override
        {
            commandBuffer->CmdUpdateSubResource(m_GpuBuffer, 0, m_BufSize, m_CpuData.data());
        }
        uint32_t GetCBufSize() const override
        {
            assert(m_BufSize > 0);
            return m_BufSize;
        }
        uint8_t* GetCpuData() override
        {
            return m_CpuData.data();
        }
        IKGFX_BufferView* GetCBV() const override
        {
            return m_CBV;
        }
        IKGFX_Buffer* GetGfxBuffer() const
        {
            return m_GpuBuffer;
        }

        void* MapRange() override;

    private:
        uint32_t m_BufSize = 0;
        std::vector<uint8_t> m_CpuData = {};
        IKGFX_Buffer* m_GpuBuffer = nullptr;
        IKGFX_BufferView* m_CBV = nullptr;
    };

    void* KGFX_DynamicMemoryConstBufferVK::MapRange()
    {
        return m_GpuBuffer->MapRange();
    }

    bool KGFX_GraphicDeviceVK::CreateDynamicConstBuf(IKGFX_DynamicConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName)
    {
        *ppRetConstBuffer = new KGFX_DynamicMemoryConstBufferVK;
        return (*ppRetConstBuffer)->Init(size, pDebugName);
    }

    BOOL KGFX_GraphicDeviceVK::CreateRenderPass(KVulkanRenderPass** ppRetRenderPass, KRenderPassDesc* pDesc)
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->CreateRenderPass(ppRetRenderPass, pDesc);
    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicDeviceVK::DestroyRenderPass(KVulkanRenderPass*& pRefRenderPass)
    {
        BOOL bResult = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        bResult = pGraphicDevice->DestroyRenderPass(pRefRenderPass);
    Exit0:
        return bResult;
    }

    void* KGFX_GraphicDeviceVK::GetNativeGraphicQueue() const
    {
        return ::GetGraphicQueue();
    }

    void* KGFX_GraphicDeviceVK::GetNativeGraphicDevice() const
    {
        return GetVkDevice();
    }

    void* KGFX_GraphicDeviceVK::GetNativePhysicsDevice() const
    {
        void* pRetDevice = nullptr;

        vks::KVulkanDevice* pGfxDevice = nullptr;
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        pGraphicDevice->GetGfxDevice(&pGfxDevice);
        KGLOG_ASSERT_EXIT(pGfxDevice);

        pRetDevice = pGfxDevice->GetPhysicalDevicePtr();
    Exit0:
        return pRetDevice;
    }

    void* KGFX_GraphicDeviceVK::GetNativeGraphicInstance() const
    {
        void* pRetInstance = nullptr;

        vks::KVulkanDevice* pGfxDevice = nullptr;
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGraphicDevice);

        pGraphicDevice->GetGfxDevice(&pGfxDevice);
        KGLOG_ASSERT_EXIT(pGfxDevice);

        pRetInstance = pGfxDevice->GetInstancePtr();
    Exit0:
        return pRetInstance;
    }

    const KGFX_PHYSICAL_DEVICE_LIMITS& KGFX_GraphicDeviceVK::GetPhysicalDeviceLimits() const
    {
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        return *pGraphicDevice->GetPhysicalDeviceLimits();
    }



    KGFX_GraphicDeviceVK* KGFX_GetGraphicDeviceVKInternal()
    {
        return (KGFX_GraphicDeviceVK*)KGFX_GetGraphicDevice();
    }


    class KGFX_WithoutTechReflector :public gfx::IShaderReflector
    {
    public:
        KGFX_WithoutTechReflector()
        {
            m_pCombindResult = new gfx::KGFX_CombinedShaderResultVK_HLSL;
        }
        ~KGFX_WithoutTechReflector()
        {
            SAFE_DELETE(m_pCombindResult);
        }
        virtual BOOL BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType) override
        {
            return true;
        }
        virtual IKGFX_CombinedShaderResult* GetCombindShaderResult() override
        {
            return m_pCombindResult;
        }

        virtual BOOL IsHLSL() override
        {
            return true;
        }
    private:
        IKGFX_CombinedShaderResult* m_pCombindResult = nullptr;
    };


    void* KGFX_GraphicDeviceVK::LoadRayTracShader(
        const char* pMainShader,
        const char* pEnterpoint,
        const NSKBase::tagFileLocation& sUserShaderLoc,
        const char* szMacro,
        gfx::ShaderStageType eShaderStage,
        BOOL bByBuildToolCmd,
        int nPlatform)
    {
        BOOL bRetCode = false;
        void* pModule = nullptr;
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KShaderStage* pStage = nullptr;
        KGFX_WithoutTechReflector _reflector;
        bRetCode = pGraphicDevice->LoadShaderWithoutTech(&pStage, pMainShader, pEnterpoint, sUserShaderLoc, szMacro, &_reflector, eShaderStage, bByBuildToolCmd, nPlatform);
        if (bRetCode && pStage)
        {
            pModule = pStage->MoveOutShaderModule();
            SAFE_RELEASE(pStage);
        }


        return pModule;
    }

    IKGFX_PipelineLoadThread* KGFX_GraphicDeviceVK::CreatePipelineLoadThread()
    {
        return new KPipelineLoadThread;
    }

    BOOL KGFX_GraphicDeviceVK::InitShaderResourcePool()
    {
        BOOL bRet = false;
        BOOL bResult = false;

        bResult = NSEngine::CreateShaderResourcePoolVK();
        KGLOG_PROCESS_ERROR(bResult);

        bResult = NSEngine::InitShaderResourcePoolVK();
        KGLOG_PROCESS_ERROR(bResult);

        bRet = true;
    Exit0:
        return bRet;
    }
    void KGFX_GraphicDeviceVK::UnInitShaderResourcePool()
    {
        NSEngine::DestroyShaderResourcePoolVK();
    }
    IKRayTracingProxy* KGFX_GraphicDeviceVK::CreateRayTracingProxy()
    {
        IKRayTracingProxy* ret = nullptr;
        ret = new VulkanRayTracingProxy;
        return ret;
    }


    void KGFX_GraphicDeviceVK::DumpDeviceMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc)
    {
        auto pDevice = GetVulkanDevice();
        char* statsString = nullptr;

        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            pDevice->VMABuildStatsString(&statsString);
            {
                size_t len = strlen(statsString);
                outputFunc(statsString, (unsigned)len);
            }
            pDevice->VMAFreeStatsString(statsString);
        }
        // #else
        else
        {
            pDevice->DumpMemoryInfo(outputFunc);
        }
        // #endif
    }

} // namespace gfx

