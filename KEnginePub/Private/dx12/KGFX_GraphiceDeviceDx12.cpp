#ifdef _WIN32
#include "KGFX_GraphiceDeviceDX12.h"
#include "KGFX_ComputeProgramDx12.h"
#include "KGFX_BufViewDX12.h"
#include "KGFX_TextureViewDX12.h"
#include "KGFX_TransientHeap.h"
#include "Engine/KGLog.h"
#include "KGFX_SwapChainDX12.h"
#include "KGFX_RenderTargetDx12.h"
#include "KGFX_RenderFrameBufferDx12.h"
#include "Engine/Utf8AndWideChar.h"
#include <dxgidebug.h>
#include "nvapi.h"
#include "KGFX_BufferDx12.h"
#include "KGFX_GraphicsProgramDx12.h"
#include "KGFX_SamplerDX12.h"
#include "KGFX_ConstBufferDX12.h"
#include "../loader/KGFX_MemTexture.h"
#include "../loader/KGFX_FileTexture.h"
#include "KGFX_FenceDX12Impl.h"
#include "KGFX_PipelineCacheDX12.h"
#include "KGFX_ShaderResourceDx12.h"
#include "KGFX_RayTracingDx12.h"
#include "KGFX_BindlessDx12.h"

////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 616; }

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

// 开启D3D12调试消息断点
#define DEBUG_BREAK_D3D12_SEVERITY_ERROR        1
#define DEBUG_BREAK_D3D12_SEVERITY_WARNING      0
#define DEBUG_BREAK_D3D12_SEVERITY_CORRUPTION   1

// 屏蔽指定警告
static D3D12_MESSAGE_ID s_MuteD3D12MessageIds[] = {
    D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED,
    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
    D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
    D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,     // This warning occurs when using capture frame while graphics debugging.
    D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,   // This warning occurs when using capture frame while graphics debugging.
};

// Validation callback
static void __stdcall RayTracingValidationMessageCallback(void* pUserData, NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY severity, const char* messageCode, const char* message, const char* messageDetails)
{
    const char* severityString = "unknown";
    switch (severity)
    {
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_ERROR: severityString = "error"; break;
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_WARNING: severityString = "warning"; break;
    }
    KGLogPrintf(KGLOG_ERR, "Ray Tracing Validation message: %s: [%s] %s\n%s", severityString, messageCode, message, messageDetails);
}

static float GetGBf(size_t size)
{
    double dSize = static_cast<double>(size) / 1024.0 / 1024.0 / 1024.0;
    return static_cast<float>(dSize);
}

namespace gfx
{
    KGFX_GraphicDeviceDx12::KGFX_GraphicDeviceDx12() = default;

    KGFX_GraphicDeviceDx12::~KGFX_GraphicDeviceDx12()
    {
        SAFE_RELEASE(m_pD3dDevice);
        SAFE_RELEASE(m_pD3dDevice5);
        IDXGIDebug* pDebug = nullptr;
        HRESULT     hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug));
        // 查显存泄露，有没有release的资源可查
        if (SUCCEEDED(hr) && DrvOption::bVKValidateEnable)
        {
            pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            pDebug->Release();
        }
    }

    static void CALLBACK DebugMessageCallback(
        D3D12_MESSAGE_CATEGORY Category,
        D3D12_MESSAGE_SEVERITY Severity,
        D3D12_MESSAGE_ID       ID,
        LPCSTR                 pDescription,
        void* pContext
    )
    {
        switch (Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            KGLogPrintf(KGLOG_ERR, "[D3DValidation_ERROR:%d]%s", ID, pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
        {
            bool bMult = ID == 820 || ID == 821 || ID == 971;
            if (!bMult)
            {
                KGLogPrintf(KGLOG_WARNING, "[D3DValidation_WARNING:%d]%s", ID, pDescription);
            }
        }
        break;
        case D3D12_MESSAGE_SEVERITY_INFO:
            KGLogPrintf(KGLOG_INFO, "[D3DValidation_INFO:%d]%s", ID, pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
            KGLogPrintf(KGLOG_DEBUG, "[D3DValidation_DEBUG:%d]%s", ID, pDescription);
            break;
        default:
            break;
        }
    }

    BOOL KGFX_GraphicDeviceDx12::Init(const RenderSystemInfo& renderSysteInfo)
    {
        BOOL     bResult = false;
        HRESULT  hResult = E_FAIL;
        size_t   uMaxVideoMemory = 0;
        uint32_t uSelectedCardId = 0;
        char     name[512];
        UINT     createFactoryFlags = 0;
        KG_PROCESS_SUCCESS(m_bInited);

        KGFX_CreateShaderPoolDx12();

#if defined(_DEBUG)
        DrvOption::bVKValidateEnable = true;
        createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#else
        DrvOption::bVKValidateEnable = false;
#endif

        if (DrvOption::bVKValidateEnable)
        {
            CComPtr<ID3D12Debug> pDebugControll = nullptr;
            hResult = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugControll));
            KGLOG_COM_PROCESS_ERROR(hResult);

            if (pDebugControll)
            {
                pDebugControll->EnableDebugLayer();
            }
        }

        hResult = CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&m_pDxgiFactory));
        KGLOG_COM_PROCESS_ERROR(hResult);

        m_vecAdapter.clear();
        m_vecCardName.clear();

        for (uint32_t i = 0;; ++i)
        {
            IDXGIAdapter1* pAdapter = nullptr;
            if (DXGI_ERROR_NOT_FOUND == m_pDxgiFactory->EnumAdapters1(i, &pAdapter))
            {
                // 没有更多的适配器可以枚举。
                break;
            }

            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);

            WideCharToUtf8(name, 512, desc.Description);

            m_vecCardName.emplace_back(name);

            // 优先选物理显存最大的设备，注意不是共享显存，是显卡上的物理显存
            if (desc.DedicatedVideoMemory > uMaxVideoMemory)
            {
                uMaxVideoMemory = desc.DedicatedVideoMemory;
                uSelectedCardId = i;
            }


            KGLogPrintf(KGLOG_INFO, "可用显卡:%s videoMem:%zu(%.3fG) sharedMem:%zu(%.3fG) sysMem:%zu(%.3fG) ", name, desc.DedicatedVideoMemory, GetGBf(desc.DedicatedVideoMemory), desc.SharedSystemMemory, GetGBf(desc.SharedSystemMemory), desc.DedicatedSystemMemory, GetGBf(desc.DedicatedSystemMemory));
            m_vecAdapter.push_back(pAdapter);
        }

        KGLOG_PROCESS_ERROR(!m_vecAdapter.empty());
        KGLogPrintf(KGLOG_INFO, "显卡选择: %s", m_vecCardName[uSelectedCardId].c_str());

        if (DrvOption::bVKValidateEnable)
        {
            // 优先使用 Settings1，可开启上下文
            CComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings1;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings1))))
            {
                dredSettings1->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dredSettings1->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dredSettings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
   
        }



        hResult = D3D12CreateDevice(m_vecAdapter[uSelectedCardId], D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_pD3dDevice));
        KGLOG_COM_PROCESS_ERROR(hResult);

        m_pD3dDevice->QueryInterface(IID_PPV_ARGS(&m_pD3dDevice5));

        if (DrvOption::bEnableRayTracing && DrvOption::bEnableRayTracingValidation)
        {
            auto nv_status = NvAPI_Initialize();
            KGLOG_PROCESS_ERROR(NVAPI_OK == nv_status);
            nv_status = NvAPI_D3D12_EnableRaytracingValidation(m_pD3dDevice5, NVAPI_D3D12_RAYTRACING_VALIDATION_FLAG_NONE);
            KGLOG_PROCESS_ERROR(NVAPI_OK == nv_status);
            nv_status = NvAPI_D3D12_RegisterRaytracingValidationMessageCallback(m_pD3dDevice5, &RayTracingValidationMessageCallback, nullptr, &raytracingValidation);
            KGLOG_PROCESS_ERROR(NVAPI_OK == nv_status);
            //RayTracingValidationMessageCallback
        }

        m_SelectCardID = uSelectedCardId;

        DrvOption::nGfx_api_version_Part0 = 12;
        DrvOption::nGfx_api_version_Part1 = 1;
        DrvOption::nGfx_api_version_Part2 = 0;

        if (DrvOption::bEnableMSAA)
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
            msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            msQualityLevels.SampleCount = DrvOption::uMSAAQulity;
            msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
            msQualityLevels.NumQualityLevels = 0;

            hResult = m_pD3dDevice->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &msQualityLevels,
                sizeof(msQualityLevels)
            );
            assert(SUCCEEDED(hResult));
            DrvOption::uMSAAQulity = msQualityLevels.NumQualityLevels;
            ASSERT(DrvOption::uMSAAQulity > 0 && "Unexpected MSAA quality level.");
        }
        else
        {
            DrvOption::uMSAAQulity = 0;
        }
        CheckPhysicalDeviceLimits();
        CheckDX12DeviceSuppot();

        InitLogDebugMesg();

        InitPipelineCache();

        bResult = InitDMA();
        KGLOG_PROCESS_ERROR(bResult);

        bResult = InitALLCPUDescriptorHeap();
        KGLOG_PROCESS_ERROR(bResult);

        bResult = InitAllIndirectCmdSignature();
        KGLOG_PROCESS_ERROR(bResult);

        bResult = InitMainQueue();
        KGLOG_PROCESS_ERROR(bResult);

        bResult = InitRayTracingContent();
        KGLOG_PROCESS_ERROR(bResult);

    Exit1:
        m_bInited = true;
        bResult = true;
    Exit0:

        return bResult;
    }

    void KGFX_GraphicDeviceDx12::UnInit()
    {
        for (uint32_t i = 0; i < CONTEXT_COUNT; ++i)
        {
            if (m_pContext[i])
            {
                m_pContext[i]->UnInit();
            }
            SAFE_DELETE(m_pContext[i]);
        }

        for (auto it : m_vecAdapter)
        {
            IDXGIAdapter1* pAdapter = it;
            SAFE_RELEASE(pAdapter);
        }
        m_vecAdapter.clear();
        UnInitMainQueue();
        UninitAllIndirectCmdSignature();
        KGFX_SamplerDX12::ClearSamplerPool();
        KGFX_GraphicDevice::UnInit();
        KGFX_DestroyShaderPoolDx12();
        SAFE_RELEASE(m_InfoQueue1);
        SAFE_RELEASE(m_InfoQueue);
        SAFE_RELEASE(m_pDxgiFactory);
        SAFE_DELETE(m_MainGraphicQueue);
        UnInitALLCPUDescriptorHeap();
        UnInitDMA();
        UnInitPipelineCache();
        UnInitRayTracingContent();
        if (DrvOption::bEnableRayTracing && DrvOption::bEnableRayTracingValidation)

        {
            NvAPI_Unload();
        }
    }

    void KGFX_GraphicDeviceDx12::Setup(const KWindow* pWindowInfo)
    {
        uint32_t contextId = pWindowInfo->m_uId;
        ASSERT(contextId < static_cast<uint32_t>(enumGraphicContext::CONTEXT_COUNT));
        if (!m_pContext[contextId])
        {
            m_pContext[contextId] = new KGFX_SwapchainDX12;
        }
        else
        {
            m_pContext[contextId]->UnInit();
        }
        m_pContext[contextId]->Init(pWindowInfo);
    }

    void KGFX_GraphicDeviceDx12::DeleteContext(enumGraphicContext contextType)
    {
        SAFE_DELETE(m_pContext[contextType]);
    }

    BOOL KGFX_GraphicDeviceDx12::IsAtomicUint64Supported() const
    {
        return m_D3D12Option11.AtomicInt64OnDescriptorHeapResourceSupported;
    }


    void* KGFX_GraphicDeviceDx12::GetNativeGraphicQueue() const
    {
        return m_MainGraphicQueue ? m_MainGraphicQueue->GetD3D12CommandQueue() : nullptr;
    }


    void* KGFX_GraphicDeviceDx12::GetNativeGraphicDevice() const
    {
        return GetDXDevice();
    }

    IKGFX_PipelineLoadThread* KGFX_GraphicDeviceDx12::CreatePipelineLoadThread()
    {
        return nullptr;
    }

    BOOL KGFX_GraphicDeviceDx12::InitShaderResourcePool()
    {
        return true;
    }
    void KGFX_GraphicDeviceDx12::UnInitShaderResourcePool()
    {
    }
    IKRayTracingProxy* KGFX_GraphicDeviceDx12::CreateRayTracingProxy()
    {
        RayTracingDx12Proxy* pProxy = new RayTracingDx12Proxy;
        return pProxy;
    }

    void KGFX_GraphicDeviceDx12::DumpDeviceMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc)
    {

    }

    IKGFX_Swapchain* KGFX_GraphicDeviceDx12::GetContext(enumGraphicContext contextType)
    {
        return m_pContext[contextType];
    }

    IKGFX_RenderContext* KGFX_GraphicDeviceDx12::GetRenderContext()
    {
        return m_CmdBufManager;
    }

    const char* KGFX_GraphicDeviceDx12::GetDeviceName() const
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    TextureFormatInfo KGFX_GraphicDeviceDx12::GetTextureFormatInfo(enumTextureFormat eFormat) const
    {
        return  GetDX12FormatInfo(eFormat);

    }

    IKGFX_Sampler* KGFX_GraphicDeviceDx12::GetSamplerByState(KSamplerState* pSamplerState)
    {
        auto findRes = KGFX_SamplerDX12::GetSamplerPool().find(pSamplerState->GetKey());
        if (findRes != KGFX_SamplerDX12::GetSamplerPool().end())
        {
            IKGFX_Sampler* reSampler = findRes->second;
            return reSampler;
        }

        D3D12_FILTER_REDUCTION_TYPE dxReduction = TranslateFilterReduction(pSamplerState->enuTextureReductionOp);
        D3D12_FILTER                dxFilter;

        if (pSamplerState->fMipLodBias > 1)
        {
            dxFilter = D3D12_ENCODE_ANISOTROPIC_FILTER(dxReduction);
        }
        else
        {
            D3D12_FILTER_TYPE dxMin = TranslateFilterMode(pSamplerState->enuMinFilter);
            D3D12_FILTER_TYPE dxMag = TranslateFilterMode(pSamplerState->enuMagFilter);
            D3D12_FILTER_TYPE dxMip = TranslateMipFilterMode(pSamplerState->enuMipmapMode);

            dxFilter = D3D12_ENCODE_BASIC_FILTER(dxMin, dxMag, dxMip, dxReduction);
        }

        D3D12_SAMPLER_DESC dxDesc = {};
        dxDesc.Filter = dxFilter;
        dxDesc.AddressU = TranslateAddressingMode(pSamplerState->enuAddressModeU);
        dxDesc.AddressV = TranslateAddressingMode(pSamplerState->enuAddressModeV);
        dxDesc.AddressW = TranslateAddressingMode(pSamplerState->enuAddressModeW);
        dxDesc.MipLODBias = pSamplerState->fMipLodBias;
        dxDesc.MaxAnisotropy = static_cast<uint32_t>(std::floorf(pSamplerState->fMaxAnisotropy));
        dxDesc.ComparisonFunc = TranslateComparisonFunc(pSamplerState->enuCompareFunc);

        for (int j = 0; j < 4; j++)
        {
            dxDesc.BorderColor[j] = TranslateBorderColor(pSamplerState->enuBorderColor).at(j);
        }

        dxDesc.MinLOD = pSamplerState->fToMinLod;
        dxDesc.MaxLOD = pSamplerState->fToMaxLod;

        KGFX_SamplerDX12* resSampler = nullptr;
        resSampler = new KGFX_SamplerDX12;

        D3D12Descriptor cpuDescriptor;
        bool            bRes = GetDX12SamplerHeap()->Allocate(&cpuDescriptor);
        KGLOG_PROCESS_ERROR(bRes);

        m_pD3dDevice->CreateSampler(&dxDesc, cpuDescriptor.cpuHandle);
        resSampler->Init(cpuDescriptor, *pSamplerState);

        KGFX_SamplerDX12::GetSamplerPool().insert({ pSamplerState->GetKey(), resSampler });

    Exit0:
        return resSampler;
    }


    bool KGFX_GraphicDeviceDx12::CreateTexture(const KGFX_TextureDesc& texDsec, const char* szDebugName, IKGFX_TextureResource** outResource)
    {
        bool bRes = false;
        assert(outResource);

        KGFX_TextureImplDx12* pTexture = new KGFX_TextureImplDx12;
        bRes = pTexture->Create(texDsec, true, std::nullopt);
        KGLOG_PROCESS_ERROR(bRes);

        *outResource = pTexture;

        if (szDebugName)
        {
            pTexture->SetDebugName(szDebugName);
        }

        bRes = true;
    Exit0:
        return bRes;
    }

    BOOL KGFX_GraphicDeviceDx12::CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        ASSERT(ppRetBuffer && *ppRetBuffer == nullptr);

        KGFX_BufferDx12* pBuffer = new KGFX_BufferDx12;
        bRetCode = pBuffer->Create(bufDesc, pData, false);
        KGLOG_PROCESS_ERROR(bRetCode);

        *ppRetBuffer = pBuffer;
        bResult = true;
    Exit0:
        return bResult;
    }

    IKGFX_Buffer* KGFX_GraphicDeviceDx12::CreateDynamicBuffer(const KGfxBufferDesc& bufDesc, BOOL bShareMode)
    {
        KGFX_BufferDx12* pBuffer = new KGFX_BufferDx12;
        KGfxBufferDesc bufDes = bufDesc;
        bufDes.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_Write;
      
        pBuffer->Create(bufDes, nullptr, true);

        return pBuffer;
    }

    int KGFX_GraphicDeviceDx12::GetDynamicBufferCount()
    {
        return m_uDynamicBufferCount;
    }

    bool KGFX_GraphicDeviceDx12::CreateDynamicConstBuf(IKGFX_DynamicConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        ASSERT(ppRetConstBuffer && *ppRetConstBuffer == nullptr);

        IKGFX_DynamicConstBuffer* pBuffer = new KGFX_MemoryConstBufferDX12;
        bRetCode = pBuffer->Init(size, pDebugName);
        KGLOG_PROCESS_ERROR(bRetCode);
        m_uDynamicBufferCount++;
        *ppRetConstBuffer = pBuffer;
        bResult = true;
    Exit0:
        return bResult;
    }


    BOOL KGFX_GraphicDeviceDx12::CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode)
    {
        BOOL           bResult = false;
        BOOL           bRetCode = false;
        HRESULT        hResult = E_FAIL;
        KGFX_RenderTargetDx12* pRt = nullptr;
        ASSERT(*ppRenderTarget == nullptr);

        pRt = new KGFX_RenderTargetDx12;

        if (!pRt->Create(pRenderTargetDesc, bTileOptimize))
        {
            SAFE_DELETE(pRt);
        }
        KGLOG_ASSERT_EXIT(pRt);

        *ppRenderTarget = pRt;
        //(*ppRenderTarget)->SetLoadState(RESOURCE_LOAD_STATE_SUCCESS);

        bResult = true;
    Exit0:
        if (pRetCheckCode && pRt)
        {
            *pRetCheckCode += pRt->GetId();
        }
        return bResult;
    }

    BOOL KGFX_GraphicDeviceDx12::CreateSignalFence(KSignalFence** ppRetSignalFence)
    {
        KGFX_FenceDX12Impl* pFence = new KGFX_FenceDX12Impl;

        pFence->Init();
        *ppRetSignalFence = pFence;

        return true;
    }

    BOOL KGFX_GraphicDeviceDx12::IsFp16Supported() const
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    // uint32_t KGFX_GraphicDeviceDx12::GetMaxWorkGroupInvocations()const
    //{
    //     throw std::logic_error("The method or operation is not implemented.");
    // }

    BOOL KGFX_GraphicDeviceDx12::IsSubGoupQuadSupported() const
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    BOOL KGFX_GraphicDeviceDx12::IsSubGoupF16Supported() const
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    BOOL KGFX_GraphicDeviceDx12::IsInitedGraphic()
    {
        return m_bInited;
    }

    void KGFX_GraphicDeviceDx12::FrameMove(BOOL bFrameRendered)
    {
        KGFX_GraphicDevice::FrameMove(bFrameRendered);
    }

    void KGFX_GraphicDeviceDx12::DeviceWaitIdle()
    {
        if (!m_MainGraphicQueue)
            return;

        m_MainGraphicQueue->CPUWaitIdel();

        //auto* queue = m_MainGraphicQueue->GetD3D12CommandQueue();
        //auto* fence = m_MainGraphicQueue->GetD3D12Fence();

        //const uint64_t value = m_MainGraphicQueue->GetCurrentFenceValue() + 1;
        //queue->Signal(fence, value);

        //m_MainGraphicQueue->CPUWaitForFenceValue(value);
    }

    D3D12MA::Allocator* KGFX_GraphicDeviceDx12::GetDX12Allocator() const
    {
        return m_DMAAllocator;
    }

    D3D12GeneralExpandingDescriptorHeap* KGFX_GraphicDeviceDx12::GetDX12RTVHeap() const
    {
        return m_RtvHeap;
    }

    D3D12GeneralExpandingDescriptorHeap* KGFX_GraphicDeviceDx12::GetDX12SRVAndUAVAndCBVHeap() const
    {
        return m_CpuViewHeap;
    }

    D3D12GeneralExpandingDescriptorHeap* KGFX_GraphicDeviceDx12::GetDX12DSVHeap() const
    {
        return m_DsvHeap;
    }

    D3D12GeneralExpandingDescriptorHeap* KGFX_GraphicDeviceDx12::GetDX12SamplerHeap() const
    {
        return m_CpuSamplerHeap;
    }

    D3D12GeneralExpandingDescriptorHeap* KGFX_GraphicDeviceDx12::GetDX12SRVAndUAVAndCBVCacheHeap() const
    {
        return m_CpuViewCacheHeap;
    }

    D3D12GeneralExpandingDescriptorHeap* KGFX_GraphicDeviceDx12::GetDX12SamplerCacheHeap() const
    {
        return m_CpuSamplerCacheHeap;
    }


    bool KGFX_GraphicDeviceDx12::CreateTextureView(IKGFX_TextureResource* texture, const KGFX_TextureViewDesc& desc, IKGFX_TextureView** outView)
    {
        IKGFX_TextureView* pTextureView = nullptr;
        pTextureView = new KGFX_TextureViewDX12(desc, texture);
        *outView = pTextureView;
        return true;
    }

    IKGFX_GraphicsProgram* KGFX_GraphicDeviceDx12::CreateGraphicsProgram()
    {
        return new KGFX_GraphicsProgramDx12;
    }

    IKGFX_ComputeProgram* KGFX_GraphicDeviceDx12::CreateComputeProgram()
    {
        return new KGFX_ComputeProgramDx12;
    }


    IDXGIFactory4* KGFX_GraphicDeviceDx12::GetDXGIFactory() const
    {
        return m_pDxgiFactory;
    }

    ID3D12Device* KGFX_GraphicDeviceDx12::GetDXDevice() const
    {
        return m_pD3dDevice;
    }

    ID3D12CommandQueue* KGFX_GraphicDeviceDx12::GetDXCommandQueue() const
    {
        return GetDX12CommandQueueImpl()->GetD3D12CommandQueue();
    }

    KGFX_CommandQueueDX12Impl* KGFX_GraphicDeviceDx12::GetDX12CommandQueueImpl() const
    {
        return m_MainGraphicQueue;
    }

    KGFX_PipelineCacheDX12* KGFX_GraphicDeviceDx12::GetPipelineCache() const
    {
        return m_PipelineCacheDx12;
    }


    ID3D12CommandSignature* KGFX_GraphicDeviceDx12::GetDrawIndirectCmdSignature() const
    {
        return m_DrawIndirectCmdSignature;
    }

    ID3D12CommandSignature* KGFX_GraphicDeviceDx12::GetDrawIndexedIndirectCmdSignature() const
    {
        return m_DrawIndexedIndirectCmdSignature;
    }

    ID3D12CommandSignature* KGFX_GraphicDeviceDx12::GetDispatchIndirectCmdSignature() const
    {
        return m_DispatchIndirectCmdSignature;
    }

    bool KGFX_GraphicDeviceDx12::InitMainQueue()
    {
        HRESULT hRes = E_FAIL;
        bool    bRes = false;
        m_MainGraphicQueue = new KGFX_CommandQueueDX12Impl;
        bRes = m_MainGraphicQueue->Create(D3D12_COMMAND_LIST_TYPE_DIRECT, 0);
        KGLOG_PROCESS_ERROR(hRes);

        m_CmdBufManager = new KGFX_CommandBufferDX12Impl;
        m_CmdBufManager->Init();

#if USE_OPTICK
        {
            ID3D12CommandQueue* CmdQueue = m_MainGraphicQueue->GetD3D12CommandQueue();
            OPTICK_GPU_INIT_D3D12(m_pD3dDevice, &CmdQueue, 1);
        }
#endif

        bRes = true;
    Exit0:
        return bRes;
    }

    void KGFX_GraphicDeviceDx12::UnInitMainQueue()
    {
        SAFE_DELETE(m_MainGraphicQueue);
        SAFE_RELEASE(m_CmdBufManager);
    }

    bool KGFX_GraphicDeviceDx12::InitAllIndirectCmdSignature()
    {
        HRESULT HResult = E_FAIL;

        {
            D3D12_INDIRECT_ARGUMENT_DESC args = {};
            args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

            D3D12_COMMAND_SIGNATURE_DESC signatureDesc;
            signatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
            signatureDesc.NumArgumentDescs = 1;
            signatureDesc.pArgumentDescs = &args;
            signatureDesc.NodeMask = 0;

            HResult = m_pD3dDevice->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&m_DrawIndirectCmdSignature));
            KGLOG_COM_PROCESS_ERROR(HResult);
        }

        {
            D3D12_INDIRECT_ARGUMENT_DESC args;
            args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

            D3D12_COMMAND_SIGNATURE_DESC signatureDesc;
            signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
            signatureDesc.NumArgumentDescs = 1;
            signatureDesc.pArgumentDescs = &args;
            signatureDesc.NodeMask = 0;

            HResult = m_pD3dDevice->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&m_DrawIndexedIndirectCmdSignature));
            KGLOG_COM_PROCESS_ERROR(HResult);
        }

        {
            D3D12_INDIRECT_ARGUMENT_DESC args{};
            args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

            D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
            signatureDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
            signatureDesc.NumArgumentDescs = 1;
            signatureDesc.pArgumentDescs = &args;
            signatureDesc.NodeMask = 0;

            HResult = m_pD3dDevice->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&m_DispatchIndirectCmdSignature));
            KGLOG_COM_PROCESS_ERROR(HResult);
        }

        return true;
    Exit0:
        return false;
    }

    void KGFX_GraphicDeviceDx12::UninitAllIndirectCmdSignature()
    {
        SAFE_RELEASE(m_DrawIndirectCmdSignature);
        SAFE_RELEASE(m_DrawIndexedIndirectCmdSignature);
        SAFE_RELEASE(m_DispatchIndirectCmdSignature);
    }

    void KGFX_GraphicDeviceDx12::InitLogDebugMesg()
    {
        HRESULT hResult = E_FAIL;

        if (DrvOption::bVKValidateEnable)
        {
            hResult = m_pD3dDevice->QueryInterface(IID_PPV_ARGS(&m_InfoQueue1));

            /// debug message callback function
            if (hResult == S_OK)
            {
                DWORD callbackCookie = 0;
                hResult = m_InfoQueue1->RegisterMessageCallback(DebugMessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &callbackCookie);
                assert(hResult == S_OK);
            }

            hResult = m_pD3dDevice->QueryInterface(IID_PPV_ARGS(&m_InfoQueue));

            if (hResult == S_OK)
            {
                D3D12_MESSAGE_SEVERITY Severities[] = {
                    D3D12_MESSAGE_SEVERITY_INFO,
                    D3D12_MESSAGE_SEVERITY_MESSAGE
                };

                // 屏蔽特定警告
                D3D12_INFO_QUEUE_FILTER filter = {};
                filter.DenyList.NumIDs = _countof(s_MuteD3D12MessageIds);
                filter.DenyList.pIDList = s_MuteD3D12MessageIds;
                filter.DenyList.NumSeverities = _countof(Severities);
                filter.DenyList.pSeverityList = Severities;

                m_InfoQueue->AddStorageFilterEntries(&filter);

#if DEBUG_BREAK_D3D12_SEVERITY_ERROR
                m_InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
#endif
#if DEBUG_BREAK_D3D12_SEVERITY_WARNING
                m_InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
#endif
#if DEBUG_BREAK_D3D12_SEVERITY_CORRUPTION
                m_InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
#endif
            }
        }
    }

    void KGFX_GraphicDeviceDx12::CheckDX12DeviceSuppot()
    {
        HRESULT hresult = E_FAIL;
        bool bSupport = false;

        D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
        hresult = m_pD3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
        assert(SUCCEEDED(hresult));
        m_D3D12Option9 = options9;

        D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11 = {};
        hresult = m_pD3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS11, &options11, sizeof(options11));
        assert(SUCCEEDED(hresult));
        m_D3D12Option11 = options11;

        D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13 = {};
        hresult = m_pD3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13, &options13, sizeof(options13));
        bSupport = (SUCCEEDED(hresult) && options13.UnrestrictedBufferTextureCopyPitchSupported);
        if (!bSupport)
        {
            assert(false);
            KGLogPrintf(KGLOG_ERR, "当前显卡缺乏必要的驱动支持：%s，请更新显卡驱动", "UnrestrictedBufferTextureCopyPitchSupported");
            MessageBox(nullptr, "当前显卡缺乏必要的驱动支持", "错误", MB_OK);
        }
        m_D3D12Option13 = options13;


        D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
        hresult = m_pD3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16));
        bSupport = (SUCCEEDED(hresult) && options16.DynamicDepthBiasSupported);
        if (!bSupport)
        {
            assert(false);
            KGLogPrintf(KGLOG_ERR, "当前显卡缺乏必要的驱动支持：%s，请更新显卡驱动", "DynamicDepthBiasSupported");
            MessageBox(nullptr, "当前显卡缺乏必要的驱动支持", "错误", MB_OK);
        }
        m_D3D12Option16 = options16;

    }

    void KGFX_GraphicDeviceDx12::CheckPhysicalDeviceLimits()
    {
        m_PhysicalDeviceLimits.maxImageDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        m_PhysicalDeviceLimits.maxImageDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_PhysicalDeviceLimits.maxImageDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        m_PhysicalDeviceLimits.maxImageDimensionCube = D3D12_REQ_TEXTURECUBE_DIMENSION;
        m_PhysicalDeviceLimits.maxImageArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        m_PhysicalDeviceLimits.maxTexelBufferElements = 1u << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP;
        m_PhysicalDeviceLimits.maxUniformBufferRange = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
        m_PhysicalDeviceLimits.maxStorageBufferRange = D3D12_REQ_RESOURCE_SIZE_IN_MEGABYTES_EXPRESSION_A_TERM * 1024 * 1024;
        m_PhysicalDeviceLimits.maxPushConstantsSize = 0; // DX12没有推送常量
        m_PhysicalDeviceLimits.maxMemoryAllocationCount = 0; // DX12没有此限制
        m_PhysicalDeviceLimits.maxSamplerAllocationCount = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
        m_PhysicalDeviceLimits.bufferImageGranularity = 0; // DX12没有此限制
        m_PhysicalDeviceLimits.sparseAddressSpaceSize = 0; // DX12没有此限制
        m_PhysicalDeviceLimits.maxBoundDescriptorSets = 8;
        m_PhysicalDeviceLimits.maxPerStageDescriptorSamplers = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
        m_PhysicalDeviceLimits.maxPerStageDescriptorUniformBuffers = D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        m_PhysicalDeviceLimits.maxPerStageDescriptorStorageBuffers = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxPerStageDescriptorSampledImages = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxPerStageDescriptorStorageImages = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxPerStageDescriptorInputAttachments = 0; // DX12没有input attachment
        m_PhysicalDeviceLimits.maxPerStageResources = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxDescriptorSetSamplers = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
        m_PhysicalDeviceLimits.maxDescriptorSetUniformBuffers = D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        m_PhysicalDeviceLimits.maxDescriptorSetUniformBuffersDynamic = 0; // DX12没有dynamic uniform buffer
        m_PhysicalDeviceLimits.maxDescriptorSetStorageBuffers = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxDescriptorSetStorageBuffersDynamic = 0; // DX12没有dynamic storage buffer
        m_PhysicalDeviceLimits.maxDescriptorSetSampledImages = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxDescriptorSetStorageImages = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
        m_PhysicalDeviceLimits.maxDescriptorSetInputAttachments = 0;
        m_PhysicalDeviceLimits.maxVertexInputAttributes = D3D12_STANDARD_VERTEX_ELEMENT_COUNT;
        m_PhysicalDeviceLimits.maxVertexInputBindings = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        m_PhysicalDeviceLimits.maxVertexInputAttributeOffset = 255; // DX12规范
        m_PhysicalDeviceLimits.maxVertexInputBindingStride = D3D12_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES;
        m_PhysicalDeviceLimits.maxVertexOutputComponents = 128; // DX12规范
        m_PhysicalDeviceLimits.maxTessellationGenerationLevel = 64; // DX12规范
        m_PhysicalDeviceLimits.maxTessellationPatchSize = 32; // DX12规范
        m_PhysicalDeviceLimits.maxTessellationControlPerVertexInputComponents = 32;
        m_PhysicalDeviceLimits.maxTessellationControlPerVertexOutputComponents = 32;
        m_PhysicalDeviceLimits.maxTessellationControlPerPatchOutputComponents = 32;
        m_PhysicalDeviceLimits.maxTessellationControlTotalOutputComponents = 128;
        m_PhysicalDeviceLimits.maxTessellationEvaluationInputComponents = 32;
        m_PhysicalDeviceLimits.maxTessellationEvaluationOutputComponents = 32;
        m_PhysicalDeviceLimits.maxGeometryShaderInvocations = 32;
        m_PhysicalDeviceLimits.maxGeometryInputComponents = 32;
        m_PhysicalDeviceLimits.maxGeometryOutputComponents = 32;
        m_PhysicalDeviceLimits.maxGeometryOutputVertices = 1024;
        m_PhysicalDeviceLimits.maxGeometryTotalOutputComponents = 1024;
        m_PhysicalDeviceLimits.maxFragmentInputComponents = 128;
        m_PhysicalDeviceLimits.maxFragmentOutputAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        m_PhysicalDeviceLimits.maxFragmentDualSrcAttachments = 1;
        m_PhysicalDeviceLimits.maxFragmentCombinedOutputResources = 8;
        m_PhysicalDeviceLimits.maxComputeSharedMemorySize = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP * sizeof(float);
        m_PhysicalDeviceLimits.maxComputeWorkGroupCount[0] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        m_PhysicalDeviceLimits.maxComputeWorkGroupCount[1] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        m_PhysicalDeviceLimits.maxComputeWorkGroupCount[2] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        m_PhysicalDeviceLimits.maxComputeWorkGroupInvocations = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
        m_PhysicalDeviceLimits.maxComputeWorkGroupSize[0] = D3D12_CS_THREAD_GROUP_MAX_X;
        m_PhysicalDeviceLimits.maxComputeWorkGroupSize[1] = D3D12_CS_THREAD_GROUP_MAX_Y;
        m_PhysicalDeviceLimits.maxComputeWorkGroupSize[2] = D3D12_CS_THREAD_GROUP_MAX_Z;
        m_PhysicalDeviceLimits.subPixelPrecisionBits = 8;
        m_PhysicalDeviceLimits.subTexelPrecisionBits = 8;
        m_PhysicalDeviceLimits.mipmapPrecisionBits = 8;
        m_PhysicalDeviceLimits.maxDrawIndexedIndexValue = D3D12_REQ_DRAWINDEXED_INDEX_COUNT_2_TO_EXP;
        m_PhysicalDeviceLimits.maxDrawIndirectCount = 0xFFFFFFFF;
        m_PhysicalDeviceLimits.maxSamplerLodBias = 16.0f;
        m_PhysicalDeviceLimits.maxSamplerAnisotropy = 16.0f;
        m_PhysicalDeviceLimits.maxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        m_PhysicalDeviceLimits.maxViewportDimensions[0] = D3D12_VIEWPORT_BOUNDS_MAX;
        m_PhysicalDeviceLimits.maxViewportDimensions[1] = D3D12_VIEWPORT_BOUNDS_MAX;
        m_PhysicalDeviceLimits.viewportBoundsRange[0] = D3D12_VIEWPORT_BOUNDS_MIN;
        m_PhysicalDeviceLimits.viewportBoundsRange[1] = D3D12_VIEWPORT_BOUNDS_MAX;
        m_PhysicalDeviceLimits.viewportSubPixelBits = 8;
        m_PhysicalDeviceLimits.minMemoryMapAlignment = 65536; // 64KB
        m_PhysicalDeviceLimits.minTexelBufferOffsetAlignment = 256;
        m_PhysicalDeviceLimits.minUniformBufferOffsetAlignment = 256;
        m_PhysicalDeviceLimits.minStorageBufferOffsetAlignment = 256;
        m_PhysicalDeviceLimits.minTexelOffset = -8;
        m_PhysicalDeviceLimits.maxTexelOffset = 7;
        m_PhysicalDeviceLimits.minTexelGatherOffset = -8;
        m_PhysicalDeviceLimits.maxTexelGatherOffset = 7;
        m_PhysicalDeviceLimits.minInterpolationOffset = 0.0f;
        m_PhysicalDeviceLimits.maxInterpolationOffset = 0.9375f;
        m_PhysicalDeviceLimits.subPixelInterpolationOffsetBits = 4;
        m_PhysicalDeviceLimits.maxFramebufferWidth = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_PhysicalDeviceLimits.maxFramebufferHeight = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_PhysicalDeviceLimits.maxFramebufferLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        m_PhysicalDeviceLimits.framebufferColorSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.framebufferDepthSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.framebufferStencilSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.framebufferNoAttachmentsSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        m_PhysicalDeviceLimits.sampledImageColorSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.sampledImageIntegerSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.sampledImageDepthSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.sampledImageStencilSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.storageImageSampleCounts = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
        m_PhysicalDeviceLimits.maxSampleMaskWords = 1;
        m_PhysicalDeviceLimits.timestampComputeAndGraphics = 1;
        m_PhysicalDeviceLimits.timestampPeriod = 1.0f;
        m_PhysicalDeviceLimits.maxClipDistances = 8;
        m_PhysicalDeviceLimits.maxCullDistances = 8;
        m_PhysicalDeviceLimits.maxCombinedClipAndCullDistances = 8;
        m_PhysicalDeviceLimits.discreteQueuePriorities = 1;
        m_PhysicalDeviceLimits.pointSizeRange[0] = 1.0f;
        m_PhysicalDeviceLimits.pointSizeRange[1] = 64.0f;
        m_PhysicalDeviceLimits.lineWidthRange[0] = 1.0f;
        m_PhysicalDeviceLimits.lineWidthRange[1] = 16.0f;
        m_PhysicalDeviceLimits.pointSizeGranularity = 1.0f;
        m_PhysicalDeviceLimits.lineWidthGranularity = 1.0f;
        m_PhysicalDeviceLimits.strictLines = 1;
        m_PhysicalDeviceLimits.standardSampleLocations = 1;
        m_PhysicalDeviceLimits.optimalBufferCopyOffsetAlignment = 512;
        m_PhysicalDeviceLimits.optimalBufferCopyRowPitchAlignment = 256;
        m_PhysicalDeviceLimits.nonCoherentAtomSize = 256;
    }



    bool KGFX_GraphicDeviceDx12::QueryAdapterInfo()
    {
        AdapterFingerprint& retValue = m_AdapterFingerprint;
        LUID luid = m_pD3dDevice->GetAdapterLuid();
        retValue.luidLow = (uint32_t)luid.LowPart;
        retValue.luidHigh = (uint32_t)luid.HighPart;

        CComPtr<IDXGIAdapter> adapter;
        if (FAILED(m_pDxgiFactory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
        {
            return false;
        }

        DXGI_ADAPTER_DESC desc{};
        if (FAILED(adapter->GetDesc(&desc)))
        {
            return false;
        }

        retValue.vendorId = desc.VendorId;
        retValue.deviceId = desc.DeviceId;
        retValue.subSysId = desc.SubSysId;
        retValue.revision = desc.Revision;

        LARGE_INTEGER drv{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &drv)))
        {
            retValue.driverVersion = (static_cast<uint64_t>(static_cast<uint32_t>(drv.HighPart)) << 32) | static_cast<uint32_t>(drv.LowPart);
        }

        return true;
    }

    D3D12DescriptorHeap* KGFX_GraphicDeviceDx12::GetGPUSamplerHeap()
    {
        return m_SamplerHeap;
    }

    const AdapterFingerprint& KGFX_GraphicDeviceDx12::GetAdapterFingerprint() const
    {
        return m_AdapterFingerprint;
    }

    void* KGFX_GraphicDeviceDx12::LoadRayTracShader(
        const char* pMainShader,
        const char* pEnterpoint,
        const NSKBase::tagFileLocation& sUserShaderLoc,
        const char* szMacro,
        gfx::ShaderStageType eShaderStage,
        BOOL bByBuildToolCmd,
        int nPlatform)
    {
        throw std::logic_error("The method is not impl.");
    }

    BOOL KGFX_GraphicDeviceDx12::CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& sViewDesc, IKGFX_BufferView** pRefBufferView, const char* pcszDebugName)
    {
        IKGFX_BufferView* pBufferView = nullptr;
        pBufferView = new KGFX_BufferViewDX12(pBuffer, sViewDesc);
        *pRefBufferView = pBufferView;
        return true;
    }

    const KGFX_PHYSICAL_DEVICE_LIMITS& KGFX_GraphicDeviceDx12::GetPhysicalDeviceLimits() const
    {
        return m_PhysicalDeviceLimits;
    }

    BOOL KGFX_GraphicDeviceDx12::CreateRenderFrameBuffer(IKGFX_RenderFrameBuffer** ppFrameBuffer, const KGfxFrameBufferDesc& fbDesc)
    {
        IKGFX_RenderFrameBuffer* pFb = new KGFX_RenderFrameBufferDx12(fbDesc);
        *ppFrameBuffer = pFb;
        return true;
    }

    void KGFX_GraphicDeviceDx12::CPUWaitForFence(uint64_t uFenceMoveId) const
    {
        GetDX12CommandQueueImpl()->CPUWaitForFenceValue(uFenceMoveId);
    }

    void KGFX_GraphicDeviceDx12::GPUWaitForFence(uint64_t uFenceMoveId) const
    {
        GetDX12CommandQueueImpl()->GPUWait(uFenceMoveId);
    }

    bool KGFX_GraphicDeviceDx12::InitDMA()
    {
        D3D12MA::ALLOCATOR_DESC allocator_desc = {};
        allocator_desc.pDevice = m_pD3dDevice;
        allocator_desc.pAdapter = m_vecAdapter.at(m_SelectCardID);
        allocator_desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

        HRESULT hrres = CreateAllocator(&allocator_desc, &m_DMAAllocator);
        KGLOG_COM_PROCESS_ERROR(hrres);

        return true;
    Exit0:
        return false;
    }

    void KGFX_GraphicDeviceDx12::UnInitDMA()
    {
        SAFE_RELEASE(m_DMAAllocator);
    }

    bool KGFX_GraphicDeviceDx12::InitPipelineCache()
    {
        bool bResult = QueryAdapterInfo();
        assert(bResult);

        if (m_PipelineCacheDx12 == nullptr)
        {
            m_PipelineCacheDx12 = new KGFX_PipelineCacheDX12;
            m_PipelineCacheDx12->Init(m_pD3dDevice);
        }
        return true;
    }

    void KGFX_GraphicDeviceDx12::UnInitPipelineCache()
    {
        if (m_PipelineCacheDx12)
        {
            m_PipelineCacheDx12->WriteCacheFile();
            SAFE_DELETE(m_PipelineCacheDx12);
        }
    }

    bool KGFX_GraphicDeviceDx12::InitALLCPUDescriptorHeap()
    {
        bool bRes = false;

        m_CpuViewHeap = new D3D12GeneralExpandingDescriptorHeap();
        bRes = m_CpuViewHeap->Init(1024 * 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KG_PROCESS_ERROR(bRes);

        m_CpuSamplerHeap = new D3D12GeneralExpandingDescriptorHeap();
        bRes = m_CpuSamplerHeap->Init(1024, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KG_PROCESS_ERROR(bRes);

        m_RtvHeap = new D3D12GeneralExpandingDescriptorHeap();
        bRes = m_RtvHeap->Init(1024, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KG_PROCESS_ERROR(bRes);

        m_DsvHeap = new D3D12GeneralExpandingDescriptorHeap();
        bRes = m_DsvHeap->Init(1024, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KG_PROCESS_ERROR(bRes);

        m_CpuSamplerCacheHeap = new D3D12GeneralExpandingDescriptorHeap();
        bRes = m_CpuSamplerCacheHeap->Init(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KG_PROCESS_ERROR(bRes);

        m_CpuViewCacheHeap = new D3D12GeneralExpandingDescriptorHeap();
        bRes = m_CpuViewCacheHeap->Init(1024 * 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KG_PROCESS_ERROR(bRes);

        m_SamplerHeap = new D3D12DescriptorHeap;
        bRes = m_SamplerHeap->Init(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        KG_PROCESS_ERROR(bRes);

        return true;
    Exit0:
        UnInitALLCPUDescriptorHeap();
        return false;
    }

    void KGFX_GraphicDeviceDx12::UnInitALLCPUDescriptorHeap()
    {
        SAFE_DELETE(m_CpuViewHeap);
        SAFE_DELETE(m_CpuSamplerHeap);
        SAFE_DELETE(m_RtvHeap);
        SAFE_DELETE(m_DsvHeap);
        SAFE_DELETE(m_CpuSamplerCacheHeap);
        SAFE_DELETE(m_CpuViewCacheHeap);
        SAFE_DELETE(m_SamplerHeap);
    }

    KGFX_GraphicDeviceDx12* KGFX_GetGraphicDeviceDx12Internal()
    {
        return (KGFX_GraphicDeviceDx12*)KGFX_GetGraphicDevice();
    }

    bool KGFX_GraphicDeviceDx12::InitRayTracingContent()
    {
        bool bResult = false;
        bool bRetCode = false;

        bRetCode = InitRayTracingGlobalRootSignature();
        KGLOG_PROCESS_ERROR(bRetCode);

        m_pBindlessHeapManager = new D3D12BindlessDescriptorHeapManager;
        bRetCode = m_pBindlessHeapManager->Init();
        KGLOG_PROCESS_ERROR(bRetCode);

        m_pRaytracingPipelineCache = new RayTracingPipelineCacheDx12;
        bRetCode = m_pRaytracingPipelineCache->Init();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    void KGFX_GraphicDeviceDx12::UnInitRayTracingContent()
    {
        SAFE_DELETE(m_pRayTracingGlobalRootSignature);
        SAFE_DELETE(m_pRaytracingPipelineCache);
        SAFE_DELETE(m_pBindlessHeapManager);
    }

    bool KGFX_GraphicDeviceDx12::InitRayTracingGlobalRootSignature()
    {
        bool bResult = false;
        bool bRetCode = false;
        RayTracingSignatureDesc SignatureDesc;

        SignatureDesc.Binding.ConstanBuffers = 8;
        SignatureDesc.Binding.ShaderResourceViews = 128;
        SignatureDesc.Binding.ShaderResourceStartIndex = 0;
        SignatureDesc.Binding.UnorderedResourceViews = 16;
        SignatureDesc.Binding.UnorderedResourceStartIndex = 0;
        SignatureDesc.Binding.Samplers = 128;
        SignatureDesc.Binding.SamplersStartIndex = 0;
        SignatureDesc.Type = RayTracingBindingType::Global;

        m_pRayTracingGlobalRootSignature = new RayTracingRootSignature;
        bRetCode = m_pRayTracingGlobalRootSignature->Init(SignatureDesc);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    RayTracingRootSignature* KGFX_GraphicDeviceDx12::GetRayTracingGlobalRootSignature()const
    {
        return m_pRayTracingGlobalRootSignature;
    }

    RayTracingPipelineCacheDx12* KGFX_GraphicDeviceDx12::GetRayTracingPipelineCache()const
    {
        return m_pRaytracingPipelineCache;
    }

    D3D12BindlessDescriptorHeapManager* KGFX_GraphicDeviceDx12::GetDX12BindlessHeapManager()const
    {
        return m_pBindlessHeapManager;
    }

    ID3D12Device5* KGFX_GraphicDeviceDx12::GetDXDevice5()const
    {
        return m_pD3dDevice5;
    }

} // namespace gfx

#endif
