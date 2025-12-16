////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IGFX_Public.h
//  Creator     : HuaFei
//  Create Date : 2025-01
//
////////////////////////////////////////////////////////////////////////////////

// 这个文件是设计上：
// 1、只包含KEnginePub内部的Vulkan符号定义，不对外开放，只能在KEnginePub内部使用

#pragma once

#include "KBase/Public/math/KMathPublic.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "KEnginePub/Public/IKLoadable.h"
#include <string>
#include <vector>
#include <unordered_map>

class IKResource;

namespace gfx
{
    class KVulkanVertexDescriptor;
    class KShaderStage;
    struct kSpecializationInfoAdapter;
    struct KWindow;
    class KVulkanSwapChain;
    class KVulkanLayout;
    class KVulkanRenderPass;
    class KVulkanSemaphore;
    class KVulkanFence;
    class KVulkanCommandPool;
    class KVulkanCommandBuffer;

    enum enumCommandBufferLevel : uint8_t
    {
        COMMAND_BUFFER_LEVEL_PRIMARY,
        COMMAND_BUFFER_LEVEL_SECONDARY
    };

    enum enumForProcessType : uint8_t
    {
        FOR_GRPAHIC,
        FOR_COMPUTE,
        FOR_TRANSFER,
        FOR_RAYTRACING,
        Count
    };

    typedef enum FenceStatus
    {
        FENCE_STATUS_COMPLETE = 0,
        FENCE_STATUS_INCOMPLETE,
        FENCE_STATUS_NOTSUBMITTED,
    } FenceStatus;

    struct GraphicsPipelineDesc : public IKGraphicsPipelineDesc
    {
        KVulkanVertexDescriptor*  pVertexDescriptor; // 顶点
        KVulkanLayout*            pLayout;           // shader的参数结构
        uint32_t            uStageCount;       // shader数量
        KShaderStage**      pStage;            // shader数组指针
        const KRenderState* pRenderState;      //
        KVulkanRenderPass*        pRenderPass;
    };

    struct ComputePipelineDesc
    {
        KVulkanLayout*      pLayout;
        KShaderStage* pStage;
    };

    class IKSpecializationConstantContainer
    {
    public:
        virtual void                           AddFloat(uint32_t stageId, uint32_t constant_id, float floatValue)  = 0;
        virtual void                           AddInt(uint32_t stageId, uint32_t constant_id, int32_t intValue)    = 0;
        virtual void                           AddUInt(uint32_t stageId, uint32_t constant_id, uint32_t uintValue) = 0;
        virtual const KSpecializationConstant& GetItem(uint32_t i)                                                 = 0;
        virtual uint32_t                       GetItemCount()                                                      = 0;
        virtual ~IKSpecializationConstantContainer() {}
    };

    class KPipeline : public KGfxRef
    {
    private:
        uint32_t m_uCreateId           = 0;
        uint64_t m_uRefFramebufferCode = 0;

    public:
        KPipeline();
        ~KPipeline();

        uint32_t GetCreateId() const { return m_uCreateId; }

        virtual enumForProcessType GetType() const = 0;
        virtual BOOL               Destroy()       = 0;
        virtual void*              GetVkPipeline() = 0;

        virtual uint64_t GetRefFramebufferCode() { return m_uRefFramebufferCode; }
        virtual void     SetRefFramebufferCode(uint64_t uCode) { m_uRefFramebufferCode = uCode; }

    public:
        uint32_t uCreatedRenderStateHash = 0;
    };

    class KGraphicsPipeline : public KPipeline
    {
    public:
        KGraphicsPipeline();
        virtual ~KGraphicsPipeline();
        virtual BOOL       Create(GraphicsPipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer) = 0;
        enumForProcessType GetType() const;
    };

    class KComputePipeline : public KPipeline
    {
    public:
        KComputePipeline();
        virtual ~KComputePipeline();
        virtual BOOL       Create(ComputePipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer) = 0;
        enumForProcessType GetType() const;
    };

    class KGraphicContext : public KGfxRef
    {
    public:
        uint32_t m_uSwapChainLoopCount = 1;

    public:
        KGraphicContext();
        virtual ~KGraphicContext();
        virtual int32_t AddRef() override;
        virtual int32_t GetRef() override;
        virtual int32_t Release() override;
        virtual BOOL    Init(const KWindow* pWindowInfo) = 0;
        virtual void    UnInit()                         = 0;

        virtual KWindow* GetWindowInfo()                             = 0;
        virtual BOOL     ResizeWindow(KWindow* pWindow, BOOL bForce) = 0;
        // virtual uint32_t GetRenderViewWidth()                        = 0;
        // virtual uint32_t GetRenderViewHeight()                       = 0;
        virtual uint32_t GetRenderTargetWith()                       = 0;
        virtual uint32_t GetRenderTargetHeight()                     = 0;

        virtual KVulkanSwapChain* GetSwapChains() = 0;

        virtual enumTextureFormat             GetSwapChainColorFormat()                       = 0;
        virtual enumTextureFormat             GetSwapChainDepthFormat()                       = 0;
        virtual std::vector<KVulkanCommandBuffer*>& GetSwapChainCommandBuffers()                    = 0;
        virtual KVulkanCommandBuffer*         GetSwapChainCommandBuffer(uint32_t id)          = 0;

        virtual KVulkanFence*                GetSwapChainFence(uint32_t uImageSemaphoreId) = 0;
        virtual std::vector<KRenderTarget*>& GetSwapChainRenderTarget()                    = 0;
        virtual KRenderTarget*               GetSwapChainDepthStencilRT()                  = 0;
        virtual uint32_t                     GetSwapChainImageIndex()                      = 0;
        virtual void                         ActiveSwapChainImage(uint32_t id)             = 0;
        virtual uint32_t                     GetSwapChainImageCount()                      = 0;
        virtual KRenderTarget*               GetCurSwapChainRenderTarget()                 = 0; // 获取当前swapchain的RenderTarget

        virtual void               GetRenderCompleteSemaphoreA(KVulkanSemaphore** pRetSemaphoreA, uint32_t uImageSemaphoreId) = 0;
        virtual void               GetImageAcquiredSemaphoreA(KVulkanSemaphore** pRetSemaphoreA, uint32_t uImageSemaphoreId)  = 0;
        virtual enumGraphicContext GetGraphicContextId()                                                                = 0;

        virtual void     ActiveView(uint32_t viewId) = 0;
        virtual uint32_t GetActiveViewId()           = 0;
    };
} // namespace gfx
