#pragma once

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__APPLE__) && !defined(__MACOS__)
#define VK_USE_PLATFORM_IOS_MVK
#elif defined(__APPLE__) && defined(__MACOS__)
#define VK_USE_PLATFORM_MACOS_MVK
#elif defined(__OHOS__)
#define VK_USE_PLATFORM_OHOS
#endif

#if !defined(VK_USE_PLATFORM_IOS_MVK) && !defined(VK_USE_PLATFORM_MACOS_MVK)
#define VK_NO_PROTOTYPES
#endif

#include "vulkan/vulkan.h"
#include "KEnginePub/Public/KEsDrv.h"

#include <mutex>
#include <atomic>


////下面这三个只能选择开一个,现在迁移到DrvOption里面去做动态开关了，因为不同的配置要动态切，小显存独显还不太适合用vma
////VMA
//#define X3D_VK_USE_VMA 0
////李老师写的分配器
//#define X3D_VK_USE_CUSTOM_ALLOCATOR 1
////系统默认分配器 苹果要开，Metal层有自己的Allocator，没必要用任何第三方分配器
//#define X3D_VK_USE_DEFAULT_ALLOCATOR 0
//
//#if defined(__APPLE__)
//    #undef X3D_VK_USE_VMA
//    #undef X3D_VK_USE_CUSTOM_ALLOCATOR
//    #undef X3D_VK_USE_DEFAULT_ALLOCATOR
//
//    #define X3D_VK_USE_VMA 0
//    #define X3D_VK_USE_CUSTOM_ALLOCATOR 0
//    #define X3D_VK_USE_DEFAULT_ALLOCATOR 1
//#endif

struct KVkDeviceMemory_T
{
	VkDeviceMemory m_pMem;
	std::mutex m_lock;
	std::atomic<int> m_bLocked;
	uint32_t m_threadid;
	void* m_pMappedData;
	int32_t m_mappedCount;
	KVkDeviceMemory_T()
	{
		m_pMem = nullptr;
		m_bLocked = false;
		m_pMappedData = nullptr;
		m_mappedCount = 0;
	}
	void Lock()
	{
//#if !X3D_VK_USE_VMA
		if(!DrvOption::bX3D_VK_USE_VMA)
		{
			m_lock.lock();
		}
//#endif
		m_bLocked = true;
	}
	void Unlock()
	{
//#if !X3D_VK_USE_VMA
		if (!DrvOption::bX3D_VK_USE_VMA)
		{
			m_lock.unlock();
		}
//#endif
		m_bLocked = false;
	}
};

typedef struct KVkDeviceMemory_T* KVkDeviceMemory;


//#if X3D_VK_USE_VMA
typedef VkResult(VKAPI_PTR* PFN_vkAllocateMemory1)(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, KVkDeviceMemory* pMemory);
typedef void (VKAPI_PTR* PFN_vkFreeMemory1)(VkDevice device, KVkDeviceMemory memory, const VkAllocationCallbacks* pAllocator);
typedef VkResult(VKAPI_PTR* PFN_vkMapMemory1)(VkDevice device, KVkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);
typedef void (VKAPI_PTR* PFN_vkUnmapMemory1)(VkDevice device, KVkDeviceMemory memory);
typedef VkResult(VKAPI_PTR* PFN_vkBindBufferMemory1)(VkDevice device, VkBuffer buffer, KVkDeviceMemory memory, VkDeviceSize memoryOffset);
typedef VkResult(VKAPI_PTR* PFN_vkBindImageMemory1)(VkDevice device, VkImage image, KVkDeviceMemory memory, VkDeviceSize memoryOffset);
#include "vulkan/vma/vk_mem_alloc.inl"
//#endif



//struct _VKMemoryLock
//{
//	std::atomic<int> m_bLocked;
//	std::mutex m_MemLock;
//	VkDeviceMemory m_pMem;
//	_VKMemoryLock();
//	~_VKMemoryLock();
//	void Lock();
//	void UnLock();
//};
//
//struct VKMemoryLock
//{
//	_VKMemoryLock* m_pLock;
//	VKMemoryLock();
//	~VKMemoryLock();
//	void Lock(VkDeviceMemory pMem);
//	void Unlock();
//};

