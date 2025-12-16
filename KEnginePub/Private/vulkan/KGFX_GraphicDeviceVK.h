////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KGFX_GraphicDeviceVK.h
//  Creator     : HuaFei
//  Create Date : 2024-01
//
////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "../IGFX_Private.h"
#include "KEnginePub/Private/comm/KGFX_GraphicDevice.h"
#include "KVulkanDefine.h"

namespace vks
{
    class KVulkanDevice;
}

namespace gfx
{
    class KGFX_DeviceVK;
    class KVulkanRenderPass;
    class KRenderPassDesc;

    class KGFX_GraphicDeviceVK : public KGFX_GraphicDevice
    {
    public:
        KGFX_GraphicDeviceVK();
        virtual ~KGFX_GraphicDeviceVK();
        virtual BOOL Init(const gfx::RenderSystemInfo& renderSysteInfo) override;
        virtual void UnInit() override;

        virtual void Setup(const KWindow* pWindowInfo) override;
        virtual void DeleteContext(enumGraphicContext contextType) override;
        virtual IKGFX_Swapchain* GetContext(enumGraphicContext eGraphicCtx) override;

        virtual IKGFX_RenderContext* GetRenderContext() override;

        virtual const char* GetDeviceName() const override;
        virtual void DeviceWaitIdle() override;
        virtual TextureFormatInfo GetTextureFormatInfo(enumTextureFormat eFormat) const override;

        virtual IKGFX_GraphicsProgram* CreateGraphicsProgram() override;
        virtual IKGFX_ComputeProgram* CreateComputeProgram() override;

        virtual IKGFX_Sampler* GetSamplerByState(KSamplerState* pSamplerState) override;

        virtual bool CreateTexture(const KGFX_TextureDesc& texDsec, const char* szDebugName, IKGFX_TextureResource** outResource) override;
        virtual bool CreateTextureView(IKGFX_TextureResource* texResource, KGFX_TextureViewDesc const& viewDesc, IKGFX_TextureView** outView) override;

        // buffer
        virtual BOOL CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData) override;
        virtual BOOL CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& sViewDesc, IKGFX_BufferView** pRefBufferView, const char* pcszDebugName) override;

        virtual IKGFX_Buffer* CreateDynamicBuffer(const KGfxBufferDesc& bufDesc, BOOL bShareMode = true) override;
        virtual int GetDynamicBufferCount() override;

        bool CreateDynamicConstBuf(IKGFX_DynamicConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName = nullptr) override;

        // render target
        virtual BOOL CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode) override;

        // framebuffer
        virtual BOOL CreateRenderFrameBuffer(gfx::IKGFX_RenderFrameBuffer** ppRetRenderFrameBuffer, const KGfxFrameBufferDesc& fbDesc) override;

        // fence
        virtual BOOL CreateSignalFence(KSignalFence** ppRetSignalFence) override;

        //support extension
        virtual BOOL IsFp16Supported() const override;
        virtual BOOL IsSubGoupQuadSupported() const override;
        virtual BOOL IsSubGoupF16Supported() const override;
        virtual BOOL IsAtomicUint64Supported() const override;

        //property
        virtual BOOL IsInitedGraphic() override;
        void FrameMove(BOOL bFrameRendered) override;

        virtual void* GetNativeGraphicQueue() const override;
        virtual void* GetNativeGraphicDevice() const override;
        virtual void* GetNativePhysicsDevice() const override;
        virtual void* GetNativeGraphicInstance() const override;

        const KGFX_PHYSICAL_DEVICE_LIMITS& GetPhysicalDeviceLimits() const override;

        void* LoadRayTracShader(
            const char* pMainShader,
            const char* pEnterpoint,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char* szMacro,
            gfx::ShaderStageType eShaderStage,
            BOOL bByBuildToolCmd = false,
            int nPlatform = 0) override;

        IKGFX_PipelineLoadThread* CreatePipelineLoadThread() override;
        BOOL InitShaderResourcePool() override;
        void UnInitShaderResourcePool() override;
        IKRayTracingProxy* CreateRayTracingProxy() override;


        void DumpDeviceMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc) override;
    public:
        // render pass
        BOOL CreateRenderPass(KVulkanRenderPass** ppRetRenderPass, KRenderPassDesc* pDesc);
        BOOL DestroyRenderPass(KVulkanRenderPass*& pRefRenderPass);

    public:
        BOOL GetGfxDevice(vks::KVulkanDevice** ppRetGfxDevice) const;
        void QueueWaitIdle(enumForProcessType commandType);

    private:
        KGFX_DeviceVK* m_pKGFXDevice{ nullptr };
    };

    KGFX_GraphicDeviceVK* KGFX_GetGraphicDeviceVKInternal();
} // namespace gfx
