#pragma once

#include "KBase/Public/math/KMathPublic.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "../IGFX_Private.h"
#include <string>
#include <vector>
#include "KVulkanDefine.h"
#include "KGraphicDevice.h"
#include "KVulkan.h"

class IKResource;

namespace gfx
{
    class KVulkanRenderContext;
    class KVulkanBindlessManager;
    class KDynamicBufferRing;

    class KVulkanGraphicDevice : public KGraphicDevice
    {
    public:
        KVulkanGraphicDevice();
        ~KVulkanGraphicDevice();

    public:
        void CmdUpdateSubResource(KVulkanRenderContext* pRenderCtx, IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData, uint32_t option = 0);
        void CmdUpdateSubResource(KVulkanRenderContext* pRenderCtx, IKGFX_TextureResource* pGfxTexure, uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion, const void* pSrcData, uint32_t uSrcRowPitch, uint32_t SrcDepthPitch);
        void CmdUpdateAllResource(KVulkanRenderContext* pRenderCtx, IKGFX_TextureResource* pGfxTexture, std::vector<gfx::KGfxSubResourceData>& data);

    public:
        BOOL Init(const gfx::RenderSystemInfo& renderSysteInfo) override;
        void Uninit() override;

        void Reset() override;

        void         ClearUploadCommandNotifyList() override;
        virtual void TrimCommandPool() override;

        BOOL CreateSwapChain(KVulkanSwapChain** ppSwapChain, KWindow* pWindow, uint32_t viewid) override;
        BOOL DestroySwapChain(KVulkanSwapChain* pSwapChain) override;

        BOOL DestroyCommandBuffer(KVulkanCommandBuffer*& pRefCmd) override;

        void _CmdExecuteCommands(gfx::enumGraphicContext eGraphicContext, KVulkanCommandBuffer* pPrimaryCommand, KVulkanCommandBuffer* pSecondCommands[], uint32_t uCount);
        void CmdExecuteCommands(gfx::enumGraphicContext eGraphicContext, KVulkanCommandBuffer* pPrimaryCommand, KVulkanCommandBuffer* pSecondCommands[], uint32_t uCount, BOOL bCheckCurFrameBuffer = TRUE) override;

        BOOL IsDirtyDescriptorPool(KVulkanDescriptorPool* pPool) override;

        BOOL AcquireNextImage(const KVulkanSwapChain* pSwapChain, KVulkanSemaphore* pSemaphore, KVulkanFence* pFence, uint32_t* imageIndex) override;
        BOOL QueueSubmit(const KVulkanGfxQueue* pQueue, KVulkanCommandBuffer* pCmdBuffer, BOOL bWait, KVulkanSemaphore* pSignalSemaphore) override;
        BOOL FlushUploadCmd() override;
        BOOL QueuePresent(const KVulkanGfxQueue* pQueue, const KVulkanSwapChain* pSwapChain, uint32_t nSwapChainImageIndex, int nWaitSemaphoreCount, KVulkanSemaphore** ppWaitSemaphore) override;
        BOOL WaitForFence(int nFenceCount, KVulkanFence** ppFence, BOOL bWaitAll, uint64_t timeout) override;
        BOOL ResetFences(uint32_t uFenceCount, KVulkanFence* pFencs[]);
        BOOL GetFenceStatus(KVulkanFence* pFence, FenceStatus* pFenceStatus) override;

        BOOL BeginCommandBuffer(const KVulkanCommandBuffer* pCommandBuffer, KVulkanRenderFrameBuffer* pFrameBuffer_WhenIsSecondCommandBufferNeeded = nullptr) override;
        BOOL EndCommandBuffer(KVulkanCommandBuffer* pCommandBuffer, std::function<void()> pfunBeforeEndCall = nullptr) override;

        uint32_t GetQueueFamilyIndex(enumForProcessType commandType) override;

        void GetTimestampFrequency(double* pFrequency) override;
        void CreateQueryHeap(const KQueryHeapDesc* pDesc, KQueryHeap** ppQueryHeap) override;
        void DestroyQueryHeap(KQueryHeap* pQueryHeap) override;
        void CmdBeginQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, KQueryDesc* pQuery) override;
        void CmdEndQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, KQueryDesc* pQuery) override;
        void CmdResolveQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, IKGFX_Buffer* pReadbackBuffer, uint32_t startQuery, uint32_t queryCount) override;

        void DeviceWaitIdle() override;
        void QueueWaitIdle(enumForProcessType commandType) override;

        BOOL     CreateGraphicQueue(KVulkanGfxQueue** ppGraphicQueue) override;
        BOOL     CreateComputeQueue(KVulkanGfxQueue** ppComputeQueue) override;
        BOOL     CreateTransferQueue(KVulkanGfxQueue** ppTransferQueue) override;
        uint32_t GetMinUniformBufferOffsetAlignment() override;
        BOOL     IsSubGoupQuadSupported() const override;
        BOOL     IsSubGoupF16Supported() const override;
        BOOL     IsFp16Supported() const override;
        BOOL     IsAtomicUint64Supported() const override;

        gfx::IKGFX_Sampler* GetSamplerByState(gfx::KSamplerState* pSamplerState) override;

        KGFX_PHYSICAL_DEVICE_LIMITS* GetPhysicalDeviceLimits() override;

        TextureFormatInfo GetTextureFormatInfo(enumTextureFormat eFormat) const override;

        IKGFX_Buffer* CreateDynamicBuffer(uint32_t uSize, gfx::BufferUsageFlags uUsageFlags = gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT, BOOL bShareMode = true) override;
        int         GetDynamicBufferCount() override;

    public:
        virtual void FrameMove(BOOL bFrameRendered) override;

        // Begin the current debug marker region
        void BeginRegion(gfx::KVulkanCommandBuffer* cmdbuffer, const char* pMarkerName, NSKMath::KVec4& color) override;

        // Insert a new debug marker into the command buffer
        void Insert(gfx::KVulkanCommandBuffer* cmdbuffer, std::string markerName, NSKMath::KVec4& color) override;

        // End the current debug marker region
        void EndRegion(gfx::KVulkanCommandBuffer* cmdBuffer) override;

        void SetCommandBufferName(gfx::KVulkanCommandBuffer* cmdBuffer, const char* name) override;
        void SetImageName(gfx::KGfxTexture* pTexture, const char* szName) override;
        void SetSamplerName(gfx::IKGFX_Sampler* pSampler, const char* szName) override;
        void SetBufferName(gfx::IKGFX_Buffer* pGfxBuffer, const char* szName) override;
        void SetShaderModuleName(gfx::KShaderStage* pShaderStage, const char* szName) override;
        void SetPipelineName(gfx::KPipeline* pPipeline, const char* szName) override;
        void SetPipelineLayoutName(gfx::KVulkanLayout* pLayout, const char* szName) override;
        void SetRenderPassName(gfx::KVulkanRenderPass* pRenderPass, const char* szName) override;
        void SetDescriptorSetName(gfx::KVulkanDescriptorSet* pDescriptorSet, const char* szName) override;
        void SetDescriptorSetLayoutName(gfx::KVulkanLayout* pLayout, const char* szName) override;
        void SetSemaphoreName(gfx::KVulkanSemaphore* pSemaphore, const char* szName) override;
        void SetFenceName(gfx::KVulkanFence* pFence, const char* szName) override;
        void SetFrameBufferName(gfx::KVulkanRenderFrameBuffer* pRenderFrameBuffer, const char* szName) override;
        void SetQueueName(gfx::KVulkanGfxQueue* pQueue, const char* szName) override;

        virtual KVulkanCommandBuffer* GetUploadCommandBuffer() override;
        virtual void            SubmitCommandBuffer(const KVulkanGfxQueue* pQueue, KVulkanCommandBuffer* pCmdBuffer, BOOL bWait, KVulkanSemaphore* pSignalSemaphore) override;

    public:
        KVulkanStagingManager* GetStagingManager() { return m_pStagingMgr; };
        KVulkanBindlessManager* GetBindlessManager() { return m_pVulkanBindlessManager; };
        KVulkanCommandBuffer*  GetUploadCmdBufferInternal();

        BOOL QueueSubmitInternal(
            const KVulkanGfxQueue* pQueue,
            KVulkanCommandBuffer*  pCmdBuffer,
            BOOL                   bWait,
            KVulkanSemaphore*      pSignalSemaphore
        );

        KDynamicBufferRing* GetDynamicBufferPool() override { return m_pGlobalDynamicBufferRing; }
        BOOL ReCreateDynamicBufferPool();
        //void AddPrivateDynamicBufferRing(KDynamicBufferRing* pBufferRing);
        //void RemovePrivateDynamicBufferRing(KDynamicBufferRing* pBufferRing);
    private:
        KGFX_PHYSICAL_DEVICE_LIMITS m_Physicallimits;

        std::vector<VkCommandBuffer> m_vecSecondbuffers;
        std::vector<VkFence>         m_vecFence;
        std::vector<VkSubmitInfo>    m_vecSubmitInfo;

        std::vector<VkMemoryBarrier>       m_vecMemoryBarrier;
        std::vector<VkBufferMemoryBarrier> m_vecBufferMemoryBarrier;
        std::vector<VkImageMemoryBarrier>  m_vecImageMemoryBarrier;

        KVulkanStagingManager*       m_pStagingMgr         = nullptr;
        KVulkanUploadCmdBufferManager* m_pUploadCmdBufferMgr = nullptr;
        KDynamicBufferRing*          m_pGlobalDynamicBufferRing;
        //std::set<KDynamicBufferRing*> m_setPrivateDynamicBufferRing;
        std::vector<KVulkanCommandBuffer*> m_SubmittedPrimaryCommandBuffers;
        //bindless manager
        KVulkanBindlessManager* m_pVulkanBindlessManager = nullptr;
        std::mutex                                           m_mtxMapStateKey2Sampler;
        std::unordered_map<const_pool_str, gfx::IKGFX_Sampler*> m_mapStateKey2Sampler;
        uint32_t m_uMultiple = 0;
    };
} // namespace gfx
