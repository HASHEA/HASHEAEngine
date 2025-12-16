#pragma once

#include "KVulkanFunc.h"
#include "../IGFX_Private.h"
#include "GFXVulkan.h"
#include "KVulkanGraphicDevice.h"

namespace gfx
{
	struct KVK_SUBRESOURCE_DATA
	{
		void* DataPtr;
		uint32_t DataRowPitch;
		uint32_t DataSlicePitch;
	};

	class KVulkanStagingBuffer
	{
		friend class KVulkanStagingManager;
	public:
		KVulkanStagingBuffer();
		~KVulkanStagingBuffer();

		VkBuffer GetVkBufferHandle() const { return m_vkBuffer; }
//#if !X3D_VK_USE_VMA
		KVkDeviceMemory GetVkMemoryHandle() const { return m_vkDeviceMemory; }
//#endif
		uint32_t GetMemorySize() const { return m_uMemoryByteWidth; }
		uint32_t GetMemoryOffset() const { return m_uMemoryOffset; }
		bool IsReadUsage() const { return m_bReadOperation; }
		void* GetMappedMemoryPtr() const { return m_pMappedPtr; }
		BOOL IsMapped() const;
		BOOL Map(uint32_t uOffset, uint32_t uSize);
		BOOL Unmap();
		void GetMappedRange(VkDeviceSize& uOffset, VkDeviceSize& uSize);

	private:
		VkBuffer m_vkBuffer = VK_NULL_HANDLE;
//#if X3D_VK_USE_VMA
		VmaAllocation m_pVmaAllocation = nullptr;
//#else
		KVkDeviceMemory m_vkDeviceMemory = VK_NULL_HANDLE;
//#endif
		uint32_t m_uRequiredSize = 0;
		uint32_t m_uMemoryByteWidth = 0;
		uint32_t m_uMemoryOffset = 0;

		void* m_pMappedPtr = nullptr;
        bool m_bReadOperation = false;

		BOOL m_bMapped = false;
		BOOL m_bPersistentMapping = false;
		uint32_t m_uMappedDstOffset = 0;
		uint32_t m_uMappedDstSize = 0;

		KVulkanFence* m_pFence = nullptr;
		uint64_t m_uSignalFenceCounter = UINT64_MAX;
	};

	class KVulkanStagingManager
	{
	public:
		KVulkanStagingManager(KVulkanGraphicDevice* gfxDevice);
		~KVulkanStagingManager();
		void Uninit();

		KVulkanStagingBuffer* AllocBuffer(uint32_t uByteWidth, const KVK_SUBRESOURCE_DATA* pData, bool bReadOperation);
		void FreeBuffer(KVulkanCommandBuffer* pCmdBuffer, KVulkanStagingBuffer* pDstBuffer);
		BOOL WaitStagingTaskFinished(KVulkanStagingBuffer* pStagingBuffer);
		BOOL CanBeMapped(KVulkanStagingBuffer* pStagingBuffer);
		void FrameMove();

	protected:
		void _QueuyPendingFreeBuffers();
		void _ProcessFreeBuffers(uint32_t uNumNeedToFree);
		void _DestroyBuffer(KVulkanStagingBuffer* pStagingBuffer);


	private:
		KVulkanGraphicDevice* m_gfxDevice = nullptr;

		std::vector<KVulkanStagingBuffer*> m_vecUsedBuffers;
		std::vector<KVulkanStagingBuffer*> m_vecPendingFreeBuffers;

		struct FREE_BUFFER_ITEM
		{
			KVulkanStagingBuffer* pStagingBuffer = nullptr;
			uint32_t uFrameNum = 0;
		};
		std::vector<FREE_BUFFER_ITEM> m_vecFreeBuffers;
		std::mutex m_Lock;
	};
}
