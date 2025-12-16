#pragma once
#include "KEnginePub/Public/IGFX_Public.h"
#include "KVulkanDefine.h"
#include "KVulkanPrivate.h"
#include "KVulkanFunc.h"

namespace gfx
{
    class KVulkanRenderFrameBuffer;

    class KVulkanCommandPool : public KGfxRef
    {
    public:
        KVulkanCommandPool();
        virtual ~KVulkanCommandPool();

    private:
        VkCommandPool m_vkCmdPool = 0;

    public:
        VkCommandPool GetCmdPool();
        virtual BOOL  Create(uint32_t uQueueFamilyIndex);
        virtual BOOL  Destroy();
        virtual void  Trim();
        virtual void  SetObjectName(const char* szName);
    };

#define GfxVulkanCommandBufferMemLeakDetect 0
    // todo resource barrier
    class KVulkanCommandBuffer : public KGfxRef
    {
    public:
        KVulkanCommandBuffer();
        virtual ~KVulkanCommandBuffer();
        BOOL Create(enumCommandBufferLevel level, enumForProcessType commandType);
        BOOL Create(enumCommandBufferLevel level, enumForProcessType commandType, KVulkanCommandPool* pCommandPool);
        BOOL Destroy();
        void AddWaitSemaphores(KVulkanSemaphore** ppWaitSemaphores, uint32_t uNumOfWaitSemaphores);
        BOOL WaitForFence(uint64_t nanosecond = UINT64_MAX);
        BOOL IsInitialState();

    public:
        VkCommandBuffer& GetCommandBuffer();
        const VkRenderPass& GetCurrentPass() const;
        void SetCurRenderPass(VkRenderPass vkRenderPass);
        KVulkanFence* GetFence();
        BOOL Begin(KVulkanRenderFrameBuffer* pVulkanRenderFrameBuffer = nullptr);
        BOOL End(std::function<void()> pfunBeforeEndCall = nullptr);

        BOOL IsSubmitted();
        BOOL QuerySignaled();
        BOOL QueryExecutable(BOOL bWait);
        BOOL Reset(BOOL bWait);
        void RefreshPendingState();
        BOOL OnSCBExecute(KVulkanCommandBuffer* pPrimaryCmdBuffer);
        BOOL AfterSCBPending(KVulkanCommandBuffer* pPrimaryCmdBuffer);
        BOOL IsManagedUploadingUsage() const { return m_bManagedUploadingUsage; }
        void MarkManagedUploadingUsage() { m_bManagedUploadingUsage = true; }
        void BeginDebugLabel(const char* szName);
        void EndDebugLabel();
        void SetObjectName(const char* szName);
        void OptickBeginGpuContext();
        void OptickEndGpuContext();
        void* GetCommandPtr();

    public:
        uint32_t     GetId();
        void         SetId(uint32_t id);
        uint32_t     m_uId;

    public:
        uint32_t m_uLastBegunCmdBufferFrame = 0;

        uint32_t m_uRebuildFlag;
        uint32_t m_uFrameCount;

        uint32_t m_uRecordFrameId;
        uint32_t m_uCommitRecordFrameId;
        uint32_t m_uDrawCallCount;
        uint32_t m_uDrawIndexCount;
        uint32_t m_uDrawPointCount;

        uint32_t m_uRedirectFrameId;
        uint32_t m_uRedirectIndexCount;
        uint32_t m_delayReleaseCounter;
        uint32_t m_uReleaseSwapChainLoopCount[gfx::CONTEXT_COUNT] = { 0 };
        int32_t  m_CreateByThreadId;
        // std::vector<KVulkanCommandBuffer*> m_vecSecondCommands;
        // std::vector<IKResource*> m_vecNotifyResourceList;

        bool m_bSubmitOnce = true;

        void ClearCounter()
        {
            m_uDrawCallCount = 0;
            m_uDrawIndexCount = 0;
            m_uRedirectIndexCount = 0;
            m_uDrawPointCount = 0;
        }

        struct _CmmitCode
        {
            uint64_t    _uCmmitCode;
            _CmmitCode& operator=(const _CmmitCode& other);
            _CmmitCode& operator=(const uint64_t& other);
            bool        operator==(const uint64_t& other) const;
            bool        operator!=(const uint64_t& other) const;
            operator uint64_t();
        } m_uCmmitCode;

    public:
        VkCommandBuffer                m_pCommandBuffer;
        VkCommandBufferLevel           m_commandLevel;
        VkCommandBufferInheritanceInfo m_InheritanceInfo;
        VkCommandBufferBeginInfo       m_cmdBufInfo;
        enumForProcessType             m_commandType;
        VkRenderPass                   m_VkActiveRenderPass;
        KVulkanFence* m_fence = nullptr;
        uint64_t                       m_uSubmittedFenceCounter = UINT64_MAX;
        BOOL                           m_bManagedUploadingUsage = false;
#if USE_OPTICK
        Optick::GPUContext m_prevContext;
#endif

#ifdef _WIN32
        std::string m_strName;
#endif
        std::atomic<KCommandBufferStates> m_eLifecycleState;

    public:
        std::vector<KVulkanSemaphore*>    m_WaitSemaphores;
        std::vector<VkPipelineStageFlags> m_WaitDstStageMasks;
        std::vector<KVulkanSemaphore*>    m_SubmittedWaitSemaphores;

        KVulkanSemaphore* m_pSignalSemaphore = nullptr;

        KVulkanCommandBuffer* m_pLastPrimary = nullptr;
        std::vector<KVulkanCommandBuffer*> m_SubmittedSCBs;

    private:
        VkCommandPool m_pCmdPool = nullptr;

#if GfxVulkanCommandBufferMemLeakDetect
        uint8_t* m_pMemLeckDetect;
        uint32_t m_nMemLeckAlloc;
#endif
    };
}
