#pragma once
#include "KGFX_DX12Header.h"

namespace gfx
{
    class KGFX_TransientHeapDX12;
    class KGFX_CommandBufferDX12Impl;
    class KGFX_GraphicDeviceDx12;

    class KGFX_SwapChainDX12Impl final : public KGfxRef
    {
    public:
        KGFX_SwapChainDX12Impl() = default;
        ~KGFX_SwapChainDX12Impl() override;
        KGFX_SwapChainDX12Impl(const KGFX_SwapChainDX12Impl&) = delete;
        KGFX_SwapChainDX12Impl& operator=(const KGFX_SwapChainDX12Impl&) = delete;
        KGFX_SwapChainDX12Impl(const KGFX_SwapChainDX12Impl&&) = delete;
        KGFX_SwapChainDX12Impl& operator=(const KGFX_SwapChainDX12Impl&&) = delete;

        bool Init(ID3D12CommandQueue* MainQueue, ID3D12Device* DXDevice, const KWindow& window, IDXGIFactory4* pDXGI, DXGI_FORMAT SWFormat);

        bool IsFullscreen() const;

        void SetFullscreen(bool fullscreen);

        void ToggleFullscreen();

        void SetVSync(bool vSync);

        bool GetVSync() const;

        void ToggleVSync();

        /**
         * Check to see if tearing is supported.
         *
         * @see https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
         */
        bool IsTearingSupported() const;

        /**
         * 如果要重建swap chain的大小，就需要把之前大小的所有swap chain都执行完，即此函数需要wait device idel
         * @param width
         * @param height
         */
        bool Resize(uint32_t width, uint32_t height);

        bool Present();

        void WaitIdel() const;

        uint32_t AcquireNextImage() const;

        DXGI_FORMAT GetRenderTargetFormat() const;

        IDXGISwapChain3* GetDXGISwapChain() const;

        std::vector<KRenderTarget*>& GetRTTexturesNotAddRef();

        KRenderTarget* GetDepthRT() const;

    private:
        bool CreateSwapChainImage();

        void ReleaseSwapChainImage();

        IDXGIFactory4* m_DXgiFactory = nullptr;

        /**
         * queue是device的成员，此处不持有引用计数
         * 但是swapchain的提交是需要queue的
         */
        ID3D12CommandQueue* m_MainQueue = nullptr;

        IDXGISwapChain3* m_DxgiSwapChain = nullptr;

        std::vector<KRenderTarget*> m_BackBufferTextures = {};

        KRenderTarget* m_DepthRT = nullptr;

        /**
         * 一个queue可以存在多个fence，且多个fence的值和等待都是相互独立的
         * 这个fence是专门用在提交上的
         */
        ID3D12Fence* m_Fence = nullptr;

        /**
         * 提交的fenceValue，fence是和创建fence的queue相关联，与swap chain无关
         */
        uint64_t m_FenceValues = {};

        /**
         * 每个swap chain都有一个独立的event，用于控制该swap chain是否结束绘制
         *
         */
        std::vector<HANDLE> m_FrameEvents = {};

        std::vector<uint64_t> m_FrameFenceValue = {};

        uint32_t m_SwapChainBufferCount = {};

        uint32_t m_Width = {};

        uint32_t m_Height = {};

        DXGI_FORMAT m_RenderTargetFormat = {};

        bool m_VSync = false;

        bool m_TearingSupported = false;

        bool m_Fullscreen = false;

        DXGI_SWAP_EFFECT m_SwapEffect_ = { DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL };
    };

    class KGFX_SwapchainDX12 : public IKGFX_Swapchain
    {
    public:
        KGFX_SwapchainDX12();

        ~KGFX_SwapchainDX12()override;

        /**
         * 1.需要init对应window的swapchain
         * 2.需要init对应swap chain的translate heap来分配cmd和控制提交
         * @param pWindowInfo
         * @return
         */
        BOOL Init(const KWindow* pWindowInfo) override;


        BOOL UnInit() override;


        BOOL BeginRender() override;


        BOOL Present() override;


        BOOL OnResize() override;


        KWindow* GetWindow() override;

        uint32_t GetCurrerntSwapChainID() const;

        uint32_t GetSwapChainRTCount() override;

        gfx::KRenderTarget* GetCurerntSwapChainRT() override;

        ///gfx::KRenderTarget* GetDepthStencilRT() override;

        BOOL IsBeginRender() override;


        BOOL IsAcquiredImage() override;

        enumTextureFormat GetSwapChainColorFormat() override;
        enumTextureFormat GetSwapChainDepthFormat() override;

        gfx::KRenderTarget* GetDepthStencilRT() override;

    private:
        bool CreateSwapChain();
        void AcquireNextImage();

        KWindow m_window = {};
        /**
         * DX12的swapchain
         */
        KGFX_SwapChainDX12Impl* m_pSwapChainDX12 = nullptr;

        uint32_t m_uCurrentSwapchainMoveId = 0;

        BOOL m_bMainCommandBegan = {};

        BOOL m_bBeginRender = {};

        KGFX_GraphicDeviceDx12* m_pGraphicDevice = nullptr;
        ID3D12Device* m_pD3dDevice = nullptr;
    };
}
