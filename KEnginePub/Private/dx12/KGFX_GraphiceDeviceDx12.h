#pragma once
#ifdef _WIN32
#include <atlcomcli.h>
#include "DMA_2.0.0/D3D12MemAlloc.h"
#include "KGFX_CommandBufferQueueDX12.h"
#include "KGFX_DescriptorHeapDX12.h"
#include "KGFX_BindlessDx12.h"
#include "KEnginePub/Private/comm/KGFX_GraphicDevice.h"
#include "../IGFX_Private.h"
#include <d3d12.h>
//#include <d3d12sdklayers.h>

namespace gfx
{
    class KGFX_PipelineCacheDX12;
    class KGFX_SwapchainDX12;
    class RayTracingRootSignature;
    class RayTracingPipelineCacheDx12;

    struct AdapterFingerprint
    {
        /**
         * 显卡硬指标
         */
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        uint32_t subSysId = 0;
        uint32_t revision = 0;
        /// <summary>
        /// 显卡LUID（重启可能会变）
        /// </summary>
        uint32_t luidLow = 0;
        uint32_t luidHigh = 0;
        /// 表征显卡驱动版本
        uint64_t driverVersion = 0; 
    };

    class KGFX_GraphicDeviceDx12 final : public KGFX_GraphicDevice
    {
        //接口实现
    public:
        KGFX_GraphicDeviceDx12();

        ~KGFX_GraphicDeviceDx12() override;

        BOOL Init(const RenderSystemInfo& renderSysteInfo) override;

        void UnInit() override;

        void Setup(const KWindow* pWindowInfo) override;
        void DeleteContext(enumGraphicContext contextType) override;
        IKGFX_Swapchain* GetContext(enumGraphicContext contextType) override;
        IKGFX_RenderContext* GetRenderContext() override;

        const char* GetDeviceName() const override;

        TextureFormatInfo GetTextureFormatInfo(enumTextureFormat eFormat) const override;

        IKGFX_GraphicsProgram* CreateGraphicsProgram() override;

        IKGFX_ComputeProgram* CreateComputeProgram() override;

        IKGFX_Sampler* GetSamplerByState(KSamplerState* pSamplerState) override;        

        bool CreateTexture(const KGFX_TextureDesc& texDsec, const char* szDebugName, IKGFX_TextureResource** outResource) override;
        bool CreateTextureView(IKGFX_TextureResource* texResource, KGFX_TextureViewDesc const& viewDesc, IKGFX_TextureView** outView) override;

        BOOL CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData) override;

        IKGFX_Buffer* CreateDynamicBuffer(const KGfxBufferDesc& bufDesc, BOOL bShareMode = true) override;


        int GetDynamicBufferCount() override;


        bool CreateDynamicConstBuf(IKGFX_DynamicConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName) override;
        

        BOOL CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode) override;


        BOOL CreateSignalFence(KSignalFence** ppRetSignalFence) override;


        BOOL IsFp16Supported() const override;

        //uint32_t GetMaxWorkGroupInvocations() const override;
        BOOL IsSubGoupQuadSupported() const override;
        BOOL IsSubGoupF16Supported() const override;

        BOOL IsInitedGraphic() override;

        void FrameMove(BOOL bFrameRendered) override;

        void DeviceWaitIdle() override;

        D3D12MA::Allocator* GetDX12Allocator() const;

        D3D12GeneralExpandingDescriptorHeap* GetDX12RTVHeap() const;

        D3D12GeneralExpandingDescriptorHeap* GetDX12SRVAndUAVAndCBVHeap() const;

        D3D12GeneralExpandingDescriptorHeap* GetDX12DSVHeap() const;

        D3D12GeneralExpandingDescriptorHeap* GetDX12SamplerHeap() const;

        D3D12GeneralExpandingDescriptorHeap* GetDX12SRVAndUAVAndCBVCacheHeap() const;

        D3D12GeneralExpandingDescriptorHeap* GetDX12SamplerCacheHeap() const;

        D3D12BindlessDescriptorHeapManager* GetDX12BindlessHeapManager()const;

#if bXR_ON
        XRLocalViewDataLH* XRGetLocalViewData(float* nearSee, float* farSee) override;

        uint32_t GetXRSwapchainWidth() override;

        uint32_t GetXRSwapchainHeight() override;
#endif

        //Dx私有函数
        IDXGIFactory4* GetDXGIFactory() const;

        ID3D12Device* GetDXDevice() const;

        //for raytracing
        ID3D12Device5* GetDXDevice5()const;

        ID3D12CommandQueue* GetDXCommandQueue() const;

        KGFX_CommandQueueDX12Impl* GetDX12CommandQueueImpl() const;

        KGFX_PipelineCacheDX12* GetPipelineCache() const;

        void CPUWaitForFence(uint64_t uFenceMoveId) const;

        void GPUWaitForFence(uint64_t uFenceMoveId) const;

        ID3D12CommandSignature* GetDrawIndirectCmdSignature() const;
        ID3D12CommandSignature* GetDrawIndexedIndirectCmdSignature() const;
        ID3D12CommandSignature* GetDispatchIndirectCmdSignature() const;
        //KGFX_TransientHeapDX12* GetResourceTransientHeap() const;

        BOOL CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& sViewDesc, IKGFX_BufferView** pRefBufferView, const char* pcszDebugName) override;
        const KGFX_PHYSICAL_DEVICE_LIMITS& GetPhysicalDeviceLimits() const override;
        BOOL CreateRenderFrameBuffer(IKGFX_RenderFrameBuffer** ppFrameBuffer, const KGfxFrameBufferDesc& fbDesc) override;
        BOOL IsAtomicUint64Supported() const override;
        void* GetNativeGraphicQueue() const override;
        void* GetNativeGraphicDevice() const override;
        virtual IKGFX_PipelineLoadThread* CreatePipelineLoadThread() override;
        virtual BOOL InitShaderResourcePool() override;
        virtual void UnInitShaderResourcePool() override;
        IKRayTracingProxy* CreateRayTracingProxy() override;
        void DumpDeviceMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc) override;

        const AdapterFingerprint& GetAdapterFingerprint() const;
		D3D12DescriptorHeap* GetGPUSamplerHeap();
		
        //raytracing
        RayTracingRootSignature* GetRayTracingGlobalRootSignature()const;
        RayTracingPipelineCacheDx12* GetRayTracingPipelineCache()const;
    private:
        struct ShaderCapabilities
        {
            D3D_SHADER_MODEL shaderModel = static_cast<D3D_SHADER_MODEL>(0);
            bool native16bitOps = false;
        };

        struct StorageBufferCapabilities
        {
            bool storageBuffer16BitAccessIsSupported = false;
        };

        struct BarrierCapabilities
        {
            bool enhancedBarriersSupported = false;
        };

        struct MiscFeaturesSupport
        {
            bool depthBoundsSupported = false;
        };

        /**
         * 这个扩展需要22年的win11版本才支持，所以永远是fasle
         */
        BarrierCapabilities m_BarrierCapabilities = {};
        ShaderCapabilities m_ShaderCapabilities = {};
        StorageBufferCapabilities m_StorageBufferCapabilities = {};
        MiscFeaturesSupport m_MiscFeaturesSupport = {};
        D3D12_FEATURE_DATA_D3D12_OPTIONS9 m_D3D12Option9 = {};
        D3D12_FEATURE_DATA_D3D12_OPTIONS11 m_D3D12Option11 = {};
        D3D12_FEATURE_DATA_D3D12_OPTIONS13 m_D3D12Option13 = {};
        D3D12_FEATURE_DATA_D3D12_OPTIONS16 m_D3D12Option16 = {};
        bool InitDMA();
        void UnInitDMA();

        bool InitPipelineCache();
        void UnInitPipelineCache();

        bool InitALLCPUDescriptorHeap();
        void UnInitALLCPUDescriptorHeap();

        bool InitMainQueue();
        void UnInitMainQueue();

        bool InitAllIndirectCmdSignature();

        void UninitAllIndirectCmdSignature();

        void InitLogDebugMesg();

        void CheckDX12DeviceSuppot();

        void CheckPhysicalDeviceLimits();

        bool QueryAdapterInfo();

        bool InitRayTracingContent();
        void UnInitRayTracingContent();

        virtual void* LoadRayTracShader(
            const char* pMainShader,
            const char* pEnterpoint,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char* szMacro,
            gfx::ShaderStageType eShaderStage,
            BOOL bByBuildToolCmd = false,
            int nPlatform = 0) override;

        bool InitRayTracingGlobalRootSignature();

    private:
        uint32_t m_SelectCardID = -1;

        BOOL m_bInited = false;
        ID3D12Device4* m_pD3dDevice = nullptr;

        //raytracing support device
        ID3D12Device5* m_pD3dDevice5 = nullptr;
        void* raytracingValidation = nullptr;
        /**
         * DXGI会创建adaptor作为d3ddevice附着的对象
         * 所以其层次是高于d3ddevice的
         */
        IDXGIFactory4* m_pDxgiFactory = nullptr;
        std::vector<IDXGIAdapter1*> m_vecAdapter = {};
        std::vector<std::string> m_vecCardName = {};

        /**
         * 这个context表示一个window
         */
        KGFX_SwapchainDX12* m_pContext[CONTEXT_COUNT] = {};

        uint64_t m_uCurrentFenceMoveId = 0;
        KGFX_PHYSICAL_DEVICE_LIMITS m_PhysicalDeviceLimits = {};
        AdapterFingerprint m_AdapterFingerprint = {};

        ID3D12InfoQueue1* m_InfoQueue1 = nullptr;
        ID3D12InfoQueue* m_InfoQueue = nullptr;

        D3D12MA::Allocator* m_DMAAllocator = nullptr;

        KGFX_PipelineCacheDX12* m_PipelineCacheDx12 = nullptr;

        /// 下面的这些heap都只是CPU可见的，离散创建贴图的对应view，只有真正需要绑定到pso上的时候才会从CPU拷贝到GPU可见的heap上
        /// GPU可见的heap由cmdBuf持有
        /// RTV
        D3D12GeneralExpandingDescriptorHeap* m_RtvHeap = nullptr;
        /// DSV
        D3D12GeneralExpandingDescriptorHeap* m_DsvHeap = nullptr;
        /// CBV, SRV, UAV
        D3D12GeneralExpandingDescriptorHeap* m_CpuViewHeap = nullptr;
        /// Samplers
        D3D12GeneralExpandingDescriptorHeap* m_CpuSamplerHeap = nullptr;

        /// 下面的这些和上面的都是CPU可见的
        ///	不过下面的是给pipeline上的descriptorSet作为拷贝对象用的
        ///	其是将在上面分配出来的不连续的descriptor拷贝到连续的descriptorSet上
        /// CBV, SRV, UAV
        D3D12GeneralExpandingDescriptorHeap* m_CpuViewCacheHeap = nullptr;
        /// Samplers
        D3D12GeneralExpandingDescriptorHeap* m_CpuSamplerCacheHeap = nullptr;

		D3D12DescriptorHeap* m_SamplerHeap = nullptr;

        //for raytracing
        D3D12BindlessDescriptorHeapManager* m_pBindlessHeapManager = nullptr;
        RayTracingRootSignature* m_pRayTracingGlobalRootSignature = nullptr;
        RayTracingPipelineCacheDx12* m_pRaytracingPipelineCache = nullptr;

        KGFX_CommandBufferDX12Impl* m_CmdBufManager = nullptr;

        /**
         * 暂时只实现一个main queue的引擎，多queue可能后续会加
         */
        KGFX_CommandQueueDX12Impl* m_MainGraphicQueue = nullptr;

        ID3D12CommandSignature* m_DrawIndirectCmdSignature = nullptr;
        ID3D12CommandSignature* m_DrawIndexedIndirectCmdSignature = nullptr;
        ID3D12CommandSignature* m_DispatchIndirectCmdSignature = nullptr;

        typedef HRESULT(WINAPI* PFN_BeginEventOnCommandList)(ID3D12GraphicsCommandList* commandList, UINT64 color, PCSTR formatString);

        typedef HRESULT(WINAPI* PFN_EndEventOnCommandList)(ID3D12GraphicsCommandList* commandList);

        typedef HRESULT(WINAPI* PFN_SetMarkerOnCommandList)(ID3D12GraphicsCommandList* commandList, UINT64 color, PCSTR formatString);

        PFN_BeginEventOnCommandList m_BeginEventOnCommandList = nullptr;
        PFN_EndEventOnCommandList m_EndEventOnCommandList = nullptr;
        PFN_SetMarkerOnCommandList m_SetMarkerOnCommandList = nullptr;
    public:
        uint32_t m_uDynamicBufferCount = 0;
    };

    KGFX_GraphicDeviceDx12* KGFX_GetGraphicDeviceDx12Internal();
}
#endif
