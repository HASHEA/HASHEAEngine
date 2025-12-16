#pragma once

#include "KBase/Public/math/KMathPublic.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "../IGFX_Private.h"
#include <string>
#include <vector>
#include "KVulkanDefine.h"
#include "KVulkanDevice.h"
#include "KVulkanPrivate.h"
#include <string>
#include <vector>
#include "KBase/Public/io/KFile.h"

namespace gfx
{
    struct KWindow;
    class KVulkanStagingManager;
    class KVulkanCommandBuffer;
    class KVulkanGfxQueue;
    class KVulkanSemaphore;
    class KVulkanUploadCmdBufferManager;
    class KVulkanRenderFrameBuffer;
    class KVulkanRenderPass;
    class KRenderPassDesc;
    class KDynamicBufferRing;

    class KGraphicDevice
    {
        friend struct kSpecializationInfoAdapter;

    public:
        KGraphicDevice();
        virtual ~KGraphicDevice();
        virtual BOOL Init(const gfx::RenderSystemInfo& renderSysteInfo) = 0;
        virtual void Uninit();
        virtual BOOL GetGfxDevice(vks::KVulkanDevice** ppRetGfxDevice) const;
        BOOL         bInitedGraphic;
        // create functions
        void         CopyCurrentSwapChainLoopCount(uint32_t pSwapChainLoopCountArray[], uint32_t uCount);

    public:
        // 先提供一版配对的接口风格供选择
        virtual BOOL CreateGraphicContext(KGraphicContext** ppRetGraphicContext, const KWindow* pWindowInfo);
        virtual BOOL DestroyGraphicContext(KGraphicContext*& pRefGraphicContext);

        // buffer
        virtual BOOL CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData);
        virtual BOOL CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& viewDesc, IKGFX_BufferView** pRefBufferView, const char* szDebugName);

        virtual IKGFX_Buffer* CreateDynamicBuffer(uint32_t uSize, gfx::BufferUsageFlags uUsageFlags = gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT, BOOL bShareMode = true) = 0;
        virtual int         GetDynamicBufferCount()                                                                                       = 0;

        // shader
        // load shader data from IO, should use this before CreateShader(...)

        // create real shader module, should use this right after LoadShader(...)
        // virtual BOOL CreateShader(KShaderStage* pShaderStage, gfx::IShaderReflector* pReflector);
        // virtual BOOL ReCreateShader(KShaderStage* pShaderStage, gfx::IShaderReflector* pReflector);
        virtual BOOL UnloadShader(KShaderStage*& pRefShaderStage);

        // cmdpool
        virtual BOOL CreateCommandPool(KVulkanCommandPool** ppRetCmdPool, uint32_t uQueueFamilyIndex);
        virtual BOOL DestroyCommandPool(KVulkanCommandPool*& ppRefCmdPool);
        virtual void TrimCommandPool() = 0;

        // commandbuffer
        virtual BOOL CreateCommandBuffer(KVulkanCommandBuffer** ppRetCmd, enumCommandBufferLevel level, enumForProcessType commandType, KVulkanCommandPool* pCommandPool, const char* szCommandName = nullptr);
        virtual BOOL CreateCommandBuffer(KVulkanCommandBuffer** ppRetCmd, enumCommandBufferLevel level, enumForProcessType commandType, const char* szCommandName = nullptr);
        virtual BOOL DestroyCommandBuffer(KVulkanCommandBuffer*& pRefCmd) = 0;
        // virtual BOOL DestroyCommandBuffer(KVulkanCommandBuffer*& pRefCmd, KVulkanCommandPool* pCommandPool);

        // semaphore
        virtual BOOL CreateSemaphoreA(KVulkanSemaphore** ppRetSem);

        // fence
        virtual BOOL CreateFence(KVulkanFence** ppRetFence, BOOL bInitWithSignaled = false);
        virtual BOOL ResetFences(uint32_t uFenceCount, KVulkanFence* pFencs[]) = 0;
        virtual BOOL CreateSignalFence(KSignalFence** ppRetSignalFence);

        // renderpass
        virtual BOOL CreateRenderPass(KVulkanRenderPass** ppRetRenderPass, KRenderPassDesc* pDesc);
        virtual BOOL DestroyRenderPass(KVulkanRenderPass*& pRefRenderPass);

        // swapchain
        virtual BOOL CreateSwapChain(KVulkanSwapChain** ppRetSwapChain, KWindow* pWindow, uint32_t viewid) = 0;
        virtual BOOL DestroySwapChain(KVulkanSwapChain* pRefSwapChain)                                     = 0;

        // layout (descriptorlayout pipelinelayout)
        virtual BOOL CreateLayout(KVulkanLayout** ppRetLayout);
        virtual BOOL DestroyLayout(KVulkanLayout*& pRefLayout);

        // vertex decl
        virtual BOOL CreateVertDescriptor(KVulkanVertexDescriptor** ppRetVertexDescriptor);
        virtual BOOL DestroyVertDescriptor(KVulkanVertexDescriptor*& pRefVertexDescriptor);

        // descriptorpool (descriptorset allocator)
        virtual BOOL CreateDescriptorPool(KVulkanDescriptorPool** ppDescriptorPool);
        virtual BOOL DestroyDescriptorPool(KVulkanDescriptorPool*& pDescriptorPool);

        virtual KDynamicBufferRing* GetDynamicBufferPool() = 0;

        // descriptorset (shader params)
        virtual BOOL CreateDescriptorSet(KVulkanDescriptorSet** ppRetDescriptorSet, const KVulkanLayout* cpLayout, KDescriptorPoolContainer* pDescriptorPoolContainer);
        virtual BOOL DestroyDescriptorSet(KVulkanDescriptorSet*& pRefDescriptorSet);

        // pipeline
        virtual BOOL CreateGraphicsPipeline(KPipeline** ppRetGraphicPipeline, GraphicsPipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer = nullptr);
        virtual BOOL CreateComputePipeline(KPipeline** ppRetComputePipeline, ComputePipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer = nullptr);
        virtual BOOL DestroyPipeline(KPipeline*& pRefPipeline);
        virtual BOOL SavePipelineCache();

        // rendertarget
        // create rt by copy image from srcRt
        virtual BOOL CreateRenderTarget2D(KRenderTarget** ppRetRenderTarget, KRenderTargetDesc* pDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode);

        virtual uint32_t GetMinUniformBufferOffsetAlignment() = 0;
        virtual BOOL     IsSubGoupQuadSupported() const       = 0;
        virtual BOOL     IsSubGoupF16Supported() const        = 0;
        virtual BOOL     IsFp16Supported() const              = 0;
        virtual BOOL     IsAtomicUint64Supported() const      = 0;

        virtual IKGFX_Sampler* GetSamplerByState(gfx::KSamplerState* pSamplerState)                  = 0;        

        virtual KGFX_PHYSICAL_DEVICE_LIMITS* GetPhysicalDeviceLimits() = 0;
        // cmd functions

    public:
        // interface more close to vulkan
        virtual BOOL AcquireNextImage(const KVulkanSwapChain* cpSwapChain, KVulkanSemaphore* pSemaphore, KVulkanFence* pFence, uint32_t* imageIndex)                                              = 0;
        virtual BOOL QueueSubmit(const KVulkanGfxQueue* pQueue, KVulkanCommandBuffer* pCmdBuffer, BOOL bWait, KVulkanSemaphore* pSignalSemaphore)                                                 = 0;
        virtual BOOL FlushUploadCmd()                                                                                                                                           = 0;
        virtual BOOL QueuePresent(const KVulkanGfxQueue* cpQueue, const KVulkanSwapChain* pSwapChain, uint32_t nSwapChainImageIndex, int nWaitSemaphoreCount, KVulkanSemaphore** ppWaitSemaphore) = 0;
        virtual BOOL WaitForFence(int nFenceCount, KVulkanFence** ppFence, BOOL bWaitAll, uint64_t timeout)                                                                           = 0;
        virtual BOOL GetFenceStatus(KVulkanFence* pFence, FenceStatus* pFenceStatus)                                                                                                  = 0;

        virtual BOOL BeginCommandBuffer(const KVulkanCommandBuffer* cpCommandBuffer, KVulkanRenderFrameBuffer* pFrameBuffer_WhenIsSecondCommandBufferNeeded = nullptr) = 0;
        virtual BOOL EndCommandBuffer(KVulkanCommandBuffer* cpCommandBuffer, std::function<void()> pfunBeforeEndCall = nullptr)                                  = 0;

        virtual void CmdExecuteCommands(gfx::enumGraphicContext eGraphicContext, KVulkanCommandBuffer* cpPrimaryCommand, KVulkanCommandBuffer* pSecondCommands[], uint32_t uCount, BOOL bCheckCurFrameBuffer = TRUE) = 0;
        virtual BOOL IsDirtyDescriptorPool(KVulkanDescriptorPool* pPool)                                                                                                                                       = 0;
        virtual void ClearUploadCommandNotifyList()                                                                                                                                                      = 0;
        // barrier functions

    public:
        virtual KVulkanCommandBuffer* GetUploadCommandBuffer()                                                                                           = 0;
        virtual void            SubmitCommandBuffer(const KVulkanGfxQueue* pQueue, KVulkanCommandBuffer* pCmdBuffer, BOOL bWait, KVulkanSemaphore* pSignalSemaphore) = 0;

    public:
        virtual BOOL              CreateGraphicQueue(KVulkanGfxQueue** ppRetGraphicQueue)   = 0;
        virtual BOOL              CreateComputeQueue(KVulkanGfxQueue** ppRetComputeQueue)   = 0;
        virtual BOOL              CreateTransferQueue(KVulkanGfxQueue** ppRetTransferQueue) = 0;
        virtual KVulkanGfxQueue*  GetGraphicQueue();
        virtual KVulkanGfxQueue*  GetComputeQueue();
        virtual KVulkanGfxQueue*  GetTransferQueue();
        virtual uint32_t          GetQueueFamilyIndex(enumForProcessType commandType)   = 0;
        virtual TextureFormatInfo GetTextureFormatInfo(enumTextureFormat eFormat) const = 0;

        // Gpu Query

    public:
        virtual void GetTimestampFrequency(double* pFrequency)                                                                                            = 0;
        virtual void CreateQueryHeap(const KQueryHeapDesc* pDesc, KQueryHeap** ppQueryHeap)                                                               = 0;
        virtual void DestroyQueryHeap(KQueryHeap* pQueryHeap)                                                                                             = 0;
        virtual void CmdBeginQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, KQueryDesc* pQuery)                                                      = 0;
        virtual void CmdEndQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, KQueryDesc* pQuery)                                                        = 0;
        virtual void CmdResolveQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, IKGFX_Buffer* pReadbackBuffer, uint32_t startQuery, uint32_t queryCount) = 0;

        virtual void Reset()                                       = 0;
        virtual void DeviceWaitIdle()                              = 0;
        virtual void QueueWaitIdle(enumForProcessType commandType) = 0;
        virtual void FrameMove(BOOL bFrameRendered);
        virtual void ClearFrameCounter(gfx::enumGraphicContext id);
        // for debug marker

    public:
        virtual void BeginRegion(gfx::KVulkanCommandBuffer* cmdbuffer, const char* pMarkerName, NSKMath::KVec4& color) = 0;

        // Insert a new debug marker into the command buffer
        virtual void Insert(gfx::KVulkanCommandBuffer* cmdbuffer, std::string markerName, NSKMath::KVec4& color) = 0;

        // End the current debug marker region
        virtual void EndRegion(gfx::KVulkanCommandBuffer* cmdBuffer) = 0;

        virtual void SetCommandBufferName(gfx::KVulkanCommandBuffer* cmdBuffer, const char* name)              = 0;
        virtual void SetImageName(gfx::KGfxTexture* pTexture, const char* szName)                        = 0;
        virtual void SetSamplerName(gfx::IKGFX_Sampler* pSampler, const char* szName)                    = 0;
        virtual void SetBufferName(gfx::IKGFX_Buffer* pGfxBuffer, const char* szName)                      = 0;
        virtual void SetShaderModuleName(gfx::KShaderStage* pShaderStage, const char* szName)            = 0;
        virtual void SetPipelineName(gfx::KPipeline* pPipeline, const char* szName)                      = 0;
        virtual void SetPipelineLayoutName(gfx::KVulkanLayout* pLayout, const char* szName)                    = 0;
        virtual void SetRenderPassName(gfx::KVulkanRenderPass* pRenderPass, const char* szName)                = 0;
        virtual void SetDescriptorSetName(gfx::KVulkanDescriptorSet* pDescriptorSet, const char* szName)       = 0;
        virtual void SetDescriptorSetLayoutName(gfx::KVulkanLayout* pLayout, const char* szName)               = 0;
        virtual void SetSemaphoreName(gfx::KVulkanSemaphore* pSemaphore, const char* szName)                   = 0;
        virtual void SetFenceName(gfx::KVulkanFence* pFence, const char* szName)                               = 0;
        virtual void SetFrameBufferName(gfx::KVulkanRenderFrameBuffer* pRenderFrameBuffer, const char* szName) = 0;
        virtual void SetQueueName(gfx::KVulkanGfxQueue* pQueue, const char* szName)                            = 0;

        // void SetQueueName(KVulkanGfxQueue* queue, const char* name);
        // void SetImageName(VkImage image, const char* name);
        // void SetSamplerName(KSampler* sampler, const char* name);
        // void SetBufferName(IKGFX_Buffer* buffer, const char* name);
        // void SetDeviceMemoryName(VkDeviceMemory memory, const char* name);
        // void SetShaderModuleName(VkShaderModule shaderModule, const char* name);
        // void SetPipelineName(KPipeline* pipeline, const char* name);
        // void SetPipelineLayoutName( pipelineLayout, const char* name);
        // void SetRenderPassName(VkRenderPass renderPass, const char* name);
        // void SetFramebufferName(VkFramebuffer framebuffer, const char* name);
        // void SetDescriptorSetLayoutName(VkDescriptorSetLayout descriptorSetLayout, const char* name);
        // void SetDescriptorSetName(VkDescriptorSet descriptorSet, const char* name);
        // void SetSemaphoreName(VkSemaphore semaphore, const char* name);
        // void SetFenceName(VkFence fence, const char* name);
        // void SetEventName(VkEvent _event, const char* name);

        const char* GetDeviceName();

        void GetDrawCallInfo(gfx::enumGraphicContext id, uint32_t& uBakedDrawCall, uint32_t& uUnBakedDrawCall, uint32_t& uIndexDrawCount, uint32_t& uPointDrawCount);

        void AddBakedDrawCall(gfx::enumGraphicContext id, uint32_t uBakedDrawCall);

        void AddUnBakedDrawCall(gfx::enumGraphicContext id, uint32_t uUnBakedDrawCall);

        void AddIndexDrawCount(gfx::enumGraphicContext id, uint32_t uIndexDrawCount, uint32_t uPointCount);

        void AddIndirectDrawCount(gfx::enumGraphicContext id, uint32_t uIndexDrawCount);

        gfx::KVulkanCommandBuffer*   GetSecondCommandFromCache(gfx::enumGraphicContext graphicContext, const KCommandBufferKey& key, const char* szCommandName);
        void                   DoSecondCommandCacheGC(gfx::enumGraphicContext graphicContext);
        void                   ClearSecondCommandCache();
        gfx::enumTextureFormat GetSwapChainSufaceFormat();
        gfx::KGraphicContext*  GetGraphicContext(enumGraphicContext eContext) const
        {
            return m_pGraphicContext[eContext];
        }

    public:
        virtual BOOL LoadShaderVSAndFS(KShaderStage* ppShaderStage[], const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, gfx::IShaderReflector* pReflector, BOOL bReCreate, BOOL bByBuildToolCmd = false, int nPlatform = 0);
        virtual BOOL LoadShaderCS(KShaderStage** ppShaderStage, const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, BOOL bByBuildToolCmd = false, int nPlatform = 0);
        virtual BOOL LoadShaderCS(KShaderStage* ppShaderStage[], const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, gfx::IShaderReflector* pReflector, BOOL bByBuildToolCmd = false, int nPlatform = 0);
        virtual BOOL LoadShaderWithoutTech(KShaderStage** ppShaderStage, const char* pMainShader, const char* pEnterpoint, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szMacro, gfx::IShaderReflector* pReflector, gfx::ShaderStageType eShaderStage, BOOL bByBuildToolCmd = false, int nPlatform = 0);
    private:
        virtual BOOL _LoadShader(int nPlatform, KShaderStage** ppRetShaderStage, const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, gfx::ShaderStageType shaderType, uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector, BOOL bReCreate);

    protected:
        KVulkanGfxQueue*      m_pGraphicQueue               = nullptr;
        KVulkanGfxQueue*      m_pComputeQueue               = nullptr;
        KVulkanGfxQueue*      m_pTransferQueue              = nullptr;

        uint32_t              m_uBakedDrawCall[gfx::CONTEXT_COUNT];
        uint32_t              m_uUnBakedDrawCall[gfx::CONTEXT_COUNT];
        uint32_t              m_uIndexDrawCount[gfx::CONTEXT_COUNT];
        uint32_t              m_uPointCount[gfx::CONTEXT_COUNT];
        uint32_t              m_uIndirectDrawCount[gfx::CONTEXT_COUNT];
        gfx::KGraphicContext* m_pGraphicContext[gfx::CONTEXT_COUNT];

        std::map<KCommandBufferKey, gfx::KVulkanCommandBuffer*> m_mapSecondCommandCache[gfx::CONTEXT_COUNT][2];
        float                                             m_CacheLifeTime[gfx::CONTEXT_COUNT];
        uint32_t                                          m_AliveCacheId;

        gfx::enumTextureFormat m_swapChainSurfaceFormat;

        // delay release objects
        std::list<KVulkanCommandBuffer*>          m_lsDelayReleasePrimaryCommandBuffer;
        std::list<KVulkanCommandBuffer*>          m_lsDelayReleaseSecondaryCommandBuffer;

        std::mutex m_ShaderLoadLock;
        std::mutex m_ShaderCreateLock;
        std::mutex m_pipelineCreateLock;

        std::mutex           m_lsDelayReleasePrimaryCommandBuffer_Lock;
        std::mutex           m_lsDelayReleaseSecondaryCommandBuffer_Lock;

    protected:
        BOOL             m_bUninitting;
        BOOL             m_bInited;
    };

    KGraphicDevice* GetGraphicDevice();
} // namespace gfx
