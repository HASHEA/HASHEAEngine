#include "KGFX_SwapChainDX12.h"

#include <wrl/client.h>

#include "KGFX_DeviceDumpDred.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KEnginePub/Public/IKEngineOption.h"

#define DX12_SWAPCHAIN_COLOR_RT_FORMAT       DXGI_FORMAT_R8G8B8A8_UNORM

namespace gfx
{
    KGFX_SwapchainDX12::KGFX_SwapchainDX12()
    {
        m_bNeedOnResize = false;
        m_bMainCommandBegan = false;
        m_bBeginRender = false;
    }

    KGFX_SwapchainDX12::~KGFX_SwapchainDX12()
    {
        SAFE_RELEASE(m_pSwapChainDX12);
    }

    BOOL KGFX_SwapchainDX12::Init(const KWindow* pWindowInfo)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        m_window = *pWindowInfo;

        bRetCode = CreateSwapChain();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_SwapchainDX12::UnInit()
    {

        return true;
    }

    BOOL KGFX_SwapchainDX12::BeginRender()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        m_bBeginRender = false;

        if (m_bNeedOnResize)
        {
            OnResize();
            m_bNeedOnResize = false;
        }

        AcquireNextImage();

        m_bBeginRender = true;

        bResult = true;
        return bResult;
    }

    void KGFX_SwapchainDX12::AcquireNextImage()
    {
        int engineLoopIndex = NSEngine::GetRenderFrameMoveLoopCount();
        m_uCurrentSwapchainMoveId = m_pSwapChainDX12->AcquireNextImage();
    }

    BOOL KGFX_SwapchainDX12::Present()
    {
        BOOL bResult = false;

        auto piGraphicsCmd = gfx::GetRenderContext();
        piGraphicsCmd->SubmitCommandBuffer();

        bResult = m_pSwapChainDX12->Present();
        KGLOG_ASSERT_EXIT(bResult);

        bResult = true;
    Exit0:
        ASSERT(bResult);
        return bResult;
    }

    bool KGFX_SwapchainDX12::CreateSwapChain()
    {
        bool bRet = false;

        m_pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        m_pD3dDevice = m_pGraphicDevice->GetDXDevice();
        auto mainQueue = m_pGraphicDevice->GetDX12CommandQueueImpl()->GetD3D12CommandQueue();
        auto dxgi = m_pGraphicDevice->GetDXGIFactory();
        if (m_pSwapChainDX12 == nullptr)
        {
            m_pSwapChainDX12 = new KGFX_SwapChainDX12Impl();
        }

        bRet = m_pSwapChainDX12->Init(mainQueue, m_pD3dDevice, m_window, dxgi, DX12_SWAPCHAIN_COLOR_RT_FORMAT);
        KGLOG_PROCESS_ERROR(bRet);

        bRet = true;
    Exit0:
        m_window.m_bWindowInvalidated = false;

        return bRet;
    }

    BOOL KGFX_SwapchainDX12::OnResize()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        HRESULT hResult = E_FAIL;

        bRetCode = m_pSwapChainDX12->Resize(m_nRenderBufferWidth, m_nRenderBufferHeight);
        KGLOG_PROCESS_ERROR(bRetCode);



        bResult = true;
    Exit0:
        return bResult;
    }


    gfx::KRenderTarget* KGFX_SwapchainDX12::GetDepthStencilRT()
    {
        return m_pSwapChainDX12->GetDepthRT();
    }

    KWindow* KGFX_SwapchainDX12::GetWindow()
    {
        return &m_window;
    }

    uint32_t KGFX_SwapchainDX12::GetCurrerntSwapChainID() const
    {
        return  m_uCurrentSwapchainMoveId;
    }

    uint32_t KGFX_SwapchainDX12::GetSwapChainRTCount()
    {
        return DX12_SWAPCHAIN_BUFFER_COUNT;
    }

    gfx::KRenderTarget* KGFX_SwapchainDX12::GetCurerntSwapChainRT()
    {
        CComPtr<ID3D12Resource> pSwapchainBuffer = nullptr;
        m_pSwapChainDX12->GetDXGISwapChain()->GetBuffer(m_pSwapChainDX12->GetDXGISwapChain()->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&pSwapchainBuffer));
        auto id = m_pSwapChainDX12->GetDXGISwapChain()->GetCurrentBackBufferIndex();
        id %= DX12_SWAPCHAIN_BUFFER_COUNT;
        ASSERT(id < DX12_SWAPCHAIN_BUFFER_COUNT);

        return m_pSwapChainDX12->GetRTTexturesNotAddRef().at(id);
    }

    BOOL KGFX_SwapchainDX12::IsBeginRender()
    {
        return m_bBeginRender;
    }

    BOOL KGFX_SwapchainDX12::IsAcquiredImage()
    {
        return m_bBeginRender;
    }

    enumTextureFormat KGFX_SwapchainDX12::GetSwapChainColorFormat()
    {
        ASSERT(m_pSwapChainDX12);
        return gfx::GetDxToTexFormat(m_pSwapChainDX12->GetRenderTargetFormat());
    }

    enumTextureFormat KGFX_SwapchainDX12::GetSwapChainDepthFormat()
    {
        return enumTextureFormat::TEX_FORMAT_NONE;
    }

}


namespace gfx
{
    KGFX_SwapChainDX12Impl::~KGFX_SwapChainDX12Impl()
    {
        ReleaseSwapChainImage();
        SAFE_RELEASE(m_DxgiSwapChain);
        SAFE_RELEASE(m_Fence);
        for (auto& event : m_FrameEvents)
        {
            CloseHandle(event);
        }
        m_FrameEvents.clear();
    }

    bool KGFX_SwapChainDX12Impl::Init(ID3D12CommandQueue* MainQueue, ID3D12Device* DXDevice, const KWindow& window, IDXGIFactory4* pDXGI, DXGI_FORMAT SWFormat)
    {
        HRESULT hResult = E_FAIL;
        bool bRet = false;
        m_MainQueue = MainQueue;
        m_DXgiFactory = pDXGI;
        m_SwapChainBufferCount = DX12_SWAPCHAIN_BUFFER_COUNT;
        m_Width = window.m_uWidth;
        m_Height = window.m_uHeight;
        m_RenderTargetFormat = SWFormat;

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = window.m_uWidth;
        swapChainDesc.Height = window.m_uHeight;
        swapChainDesc.Format = SWFormat;
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = DX12_SWAPCHAIN_BUFFER_COUNT;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        IDXGISwapChain1* pSwapChain = nullptr;

        //// Note: Swap chain uses queue to perform flush.
        hResult = m_DXgiFactory->CreateSwapChainForHwnd(m_MainQueue, window.m_window, &swapChainDesc, nullptr, nullptr, &pSwapChain);
        KGLOG_COM_PROCESS_ERROR(hResult);

        /// 所有QueryInterface之类的操作都会导致增加引用计数，是不是使用智能指针更好？
        hResult = pSwapChain->QueryInterface(&m_DxgiSwapChain);
        KGLOG_COM_PROCESS_ERROR(hResult);

        bRet = CreateSwapChainImage();
        KGLOG_PROCESS_ERROR(bRet);

        hResult = DXDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
        KGLOG_COM_PROCESS_ERROR(hResult);

        for (uint32_t i = 0; i < m_SwapChainBufferCount; i++)
        {
            HANDLE fenceEvent = CreateEventEx(nullptr, nullptr, CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
            assert(fenceEvent);

            m_FrameEvents.emplace_back(fenceEvent);
            m_FrameFenceValue.emplace_back(0);
        }

        bRet = true;
    Exit0:
        SAFE_RELEASE(pSwapChain);
        return bRet;
    }

    bool KGFX_SwapChainDX12Impl::IsFullscreen() const
    {
        return m_Fullscreen;
    }

    void KGFX_SwapChainDX12Impl::ToggleFullscreen()
    {
        SetFullscreen(!m_Fullscreen);
    }

    void KGFX_SwapChainDX12Impl::SetVSync(bool vSync)
    {
        m_VSync = vSync;
    }

    bool KGFX_SwapChainDX12Impl::GetVSync() const
    {
        return m_VSync;
    }

    void KGFX_SwapChainDX12Impl::ToggleVSync()
    {
        SetVSync(!m_VSync);
    }

    bool KGFX_SwapChainDX12Impl::IsTearingSupported() const
    {
        return m_TearingSupported;
    }

    DXGI_FORMAT KGFX_SwapChainDX12Impl::GetRenderTargetFormat() const
    {
        return m_RenderTargetFormat;
    }

    IDXGISwapChain3* KGFX_SwapChainDX12Impl::GetDXGISwapChain() const
    {
        return m_DxgiSwapChain;
    }

    std::vector<KRenderTarget*>& KGFX_SwapChainDX12Impl::GetRTTexturesNotAddRef()
    {
        return m_BackBufferTextures;
    }

    KRenderTarget* KGFX_SwapChainDX12Impl::GetDepthRT() const
    {
        return m_DepthRT;
    }

    void KGFX_SwapChainDX12Impl::SetFullscreen(bool fullscreen)
    {
        m_Fullscreen = fullscreen;
    }

    bool KGFX_SwapChainDX12Impl::Resize(uint32_t width, uint32_t height)
    {
        bool bRet = false;
        HRESULT hResult = E_FAIL;
        if (width == m_Width && height == m_Height)
            return true;

        WaitIdel();

        for (auto& value : m_FrameFenceValue)
            value = 0;

        /// 因为发生resize是在device idel时候，虽然所有资源解除使用了，但是由于没有present所以事件未被重置，需要手动调用
        for (auto evt : m_FrameEvents)
            SetEvent(evt);

        m_Width = width;
        m_Height = height;

        ReleaseSwapChainImage();

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        (m_DxgiSwapChain->GetDesc(&swapChainDesc));
        hResult = m_DxgiSwapChain->ResizeBuffers(m_SwapChainBufferCount, width, height, m_RenderTargetFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
        KGLOG_COM_PROCESS_ERROR(hResult);

        bRet = CreateSwapChainImage();
        KGLOG_PROCESS_ERROR(bRet);

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_SwapChainDX12Impl::Present()
    {
        bool bRet = false;
        HRESULT hrRes = E_FAIL;
        /// 当fence到达上一次提交的value之后，就激活对应的cpu事件
        uint32_t backBufID = m_DxgiSwapChain->GetCurrentBackBufferIndex();
        hrRes = m_DxgiSwapChain->Present(m_VSync ? 1 : 0, DXGI_PRESENT_ALLOW_TEARING);
        CheckDeviceRemoveReason(hrRes);

        m_FenceValues++;
        m_MainQueue->Signal(m_Fence, m_FenceValues);

        m_FrameFenceValue.at(backBufID) = m_FenceValues;

        //KGFX_GetGraphicDeviceDx12Internal()->DeviceWaitIdle();

        //m_Fence->SetEventOnCompletion(m_FenceValues, m_FrameEvents.at(backBufID));
        //KGLOG_COM_PROCESS_ERROR(hrRes);

        //AcquireNextImage();

        bRet = true;
        return bRet;
    }

    void KGFX_SwapChainDX12Impl::WaitIdel() const
    {
        KGFX_GraphicDeviceDx12* dxDevice = gfx::KGFX_GetGraphicDeviceDx12Internal();
        KGFX_CommandQueueDX12Impl* cmdQueueImpl = dxDevice->GetDX12CommandQueueImpl();
        cmdQueueImpl->CPUWaitIdel();
    }

    uint32_t KGFX_SwapChainDX12Impl::AcquireNextImage() const
    {
        uint32_t index = m_DxgiSwapChain->GetCurrentBackBufferIndex();
        auto currentFence = m_Fence->GetCompletedValue();


        if (currentFence < m_FrameFenceValue[index] && m_FrameFenceValue[index]>0)
        {
            m_Fence->SetEventOnCompletion(m_FrameFenceValue[index], m_FrameEvents.at(index));
            WaitForSingleObject(m_FrameEvents[index], INFINITE);
            ResetEvent(m_FrameEvents[index]);
        }

        return index;
    }

    bool KGFX_SwapChainDX12Impl::CreateSwapChainImage()
    {
        BOOL bRetCode = false;
        HRESULT hResult = E_FAIL;

        KGFX_GraphicDeviceDx12* pGraphicDevice = static_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        KRenderTargetDesc rendertargetDesc;
        ID3D12Resource* pSwapchainBuffer = nullptr;

        KGLOG_ASSERT_EXIT(m_DxgiSwapChain);
        hResult = m_DxgiSwapChain->ResizeBuffers(m_SwapChainBufferCount, m_Width, m_Height, m_RenderTargetFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
        KGLOG_COM_PROCESS_ERROR(hResult);

        rendertargetDesc.uWidth = m_Width;
        rendertargetDesc.uHeight = m_Height;
        rendertargetDesc.uDepth = 1;
        rendertargetDesc.eFormat = GetDxToTexFormat(m_RenderTargetFormat);
        rendertargetDesc.uMipLevels = 1;
        rendertargetDesc.uArraySize = 1;
        rendertargetDesc.eSampleCount = SAMPLE_COUNT_1_BIT;

        for (uint32_t i = 0; i < m_SwapChainBufferCount; ++i)
        {
            m_DxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&pSwapchainBuffer));

            WCHAR name[64];
            swprintf_s(name, 64, L"SwapchainBuffer_%d", i);
            pSwapchainBuffer->SetName(name);

            rendertargetDesc.cpNativeHandle = static_cast<void*>(pSwapchainBuffer);
            sprintf(rendertargetDesc.m_szRTName, "SwapChainRenderTex_%u", i);

            KRenderTarget* pRenderTarget = nullptr;
            bRetCode = pGraphicDevice->CreateRenderTarget(&pRenderTarget, &rendertargetDesc, true, nullptr);
            KGLOG_PROCESS_ERROR(bRetCode);
            m_BackBufferTextures.push_back(pRenderTarget);
        }

        {
            rendertargetDesc.eFormat = enumTextureFormat::TEX_FORMAT_D24_UNORM_S8_UINT;
            rendertargetDesc.cpNativeHandle = nullptr;
            printf(rendertargetDesc.m_szRTName, "SwapChainDepth");
            bRetCode = pGraphicDevice->CreateRenderTarget(&m_DepthRT, &rendertargetDesc, true, nullptr);
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        return true;
    Exit0:
        SAFE_RELEASE(pSwapchainBuffer);
        return false;
    }

    void KGFX_SwapChainDX12Impl::ReleaseSwapChainImage()
    {
        for (auto& it : m_BackBufferTextures)
        {
            SAFE_RELEASE(it);
        }
        m_BackBufferTextures.clear();

        SAFE_RELEASE(m_DepthRT);
    }
}
