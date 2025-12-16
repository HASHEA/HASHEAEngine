#pragma once
#include "GFXVulkan.h"

namespace gfx
{
	class KVulkanUploadCmdBufferManager
	{
	public:
		KVulkanUploadCmdBufferManager();
		~KVulkanUploadCmdBufferManager();

		BOOL Init();
		void Uninit();

		KVulkanCommandBuffer* GetUploadCmdBuffer();
		void SubmitUploadCmdBuffer(BOOL bAllocateSemaphores = TRUE);
		void FreeUnusedCmdBuffers(BOOL bWait = FALSE);
		void DependOnUploadSemaphores(KVulkanCommandBuffer* pRenderCmdBuffer);
		void Reset();
		void ClearNotifyList();
		void TrimCommandPool();

	private:
		KVulkanGfxQueue* m_pQueue = nullptr;

        class PoolManager
        {
        public:
            PoolManager() {};
            ~PoolManager() {};

            void Init(KVulkanCommandPool* pCmdPool, enumForProcessType eCommandType);
            void Uninit();
            KVulkanCommandBuffer* AllocCmdBuffer();
            void FreeUnusedCmdBuffers(BOOL bWait = FALSE);
            void ClearNotifyList();
            void Trim();

        private:
            KVulkanCommandPool* m_pCmdPool = nullptr;
            enumForProcessType m_eCommandType = enumForProcessType::FOR_GRPAHIC;

            std::vector<KVulkanCommandBuffer*> m_vecActiveCmdBuffers;
            std::vector<KVulkanCommandBuffer*> m_vecFreeCmdBuffers;
            std::mutex                         m_Lock;
        };
        PoolManager m_UploadCmdPool;

		KVulkanCommandBuffer* m_pUploadCmdBuffer = nullptr;
		KVulkanSemaphore* m_pUploadSemaphore = nullptr;

		std::vector<KVulkanSemaphore*> m_vecUploadSignalSemaphores;
	};
}
