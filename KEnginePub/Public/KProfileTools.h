////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KProfileTools.h
//  Creator     : Ant
//  Create Date : 2025
//
////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "optick.h"
#include <vector>
#include <array>
#include <unordered_map>

#ifdef KG_PUBLISH
	#define ENABLE_DEBUG_PROFILE 0
#else
	#define ENABLE_DEBUG_PROFILE 1
#endif

// 在这里切换使用 Optick 还是 MicroProfile，MicroProfile 目前只支持在Engine里使用 (由EnginePub管理，EnginePub目前是以静态库的形式存在)，不支持在Engine外部使用
#define ENABLE_OPTICK_PROFILE 1
#define ENABLE_GPU_PROFILE 1

#if ENABLE_DEBUG_PROFILE && !ENABLE_OPTICK_PROFILE
	#define MICROPROFILE_ENABLED 1
#else
	#define MICROPROFILE_ENABLED 0
#endif

#pragma region MICROPROFILE

// KEnginePub 目前貌似没有需要编成 DLL 的需求，这里暂时不管
#ifdef __GNUC__     // linux
	#ifdef KGCOMMON_EXPORTS
		#define MICROPROFILE_API __attribute__((visibility("default")))
	#else
		#define MICROPROFILE_API
	#endif
#else               // WIN32
	#if defined KGCOMMON_EXPORTS
		#define MICROPROFILE_API __declspec(dllexport)
	#else
		#define MICROPROFILE_API
	#endif
#endif

#if defined(_MSC_VER)
	#define MICROPROFILE_MSVC 1
#elif defined(__clang__) || defined(__GNUC__)
	#define MICROPROFILE_GCC 1
#endif

#if defined(MICROPROFILE_GCC)
	#define MICROPROFILE_FUNC __PERTTY_FUNCTION__
#elif defined(MICROPROFILE_MSVC)
	#define MICROPROFILE_FUNC __FUNCTION__
#endif

#define MICROPROFILE_CONCAT_IMPL(a, b) a##b
#define MICROPROFILE_CONCAT(a, b) MICROPROFILE_CONCAT_IMPL(a, b)

#if MICROPROFILE_ENABLED
	#define MICROPROFILE_SCOPE_CPU(...) \
		static uint64_t MICROPROFILE_CONCAT(g_mp,__LINE__) = KMicroProfile::CPUMicroProfile::GetToken(MICROPROFILE_FUNC, ##__VA_ARGS__); \
		KMicroProfile::CPUMicroProfile MICROPROFILE_CONCAT(foo, __LINE__)(MICROPROFILE_CONCAT(g_mp,__LINE__));
		//MicroProfileScopeHandler MICROPROFILE_CONCAT(foo,__LINE__)( MICROPROFILE_CONCAT(g_mp,__LINE__)); \
		//KMicroProfile::CPUMicroProfile MICROPROFILE_CONCAT(MICROPROFILE_SCOPE_, __LINE__)(MICROPROFILE_FUNC, ##__VA_ARGS__); \
		//MICROPROFILE_SCOPEI("CPU", MICROPROFILE_GET_NAME(__VA_ARGS__), 0)
	#if ENABLE_GPU_PROFILE
		#define MICROPROFILE_SCOPE_GPU(pCommandBuffer, name) \
			static uint64_t MICROPROFILE_CONCAT(g_mpGPU, __LINE__) = KMicroProfile::GPUProfilerVulkan::GetToken(name); \
			KMicroProfile::GPUProfilerVulkan MICROPROFILE_CONCAT(MICROPROFILE_GPU_SCOPE_, __LINE__)(pCommandBuffer, MICROPROFILE_CONCAT(g_mpGPU, __LINE__)); \
			//MICROPROFILE_SCOPEGPUI(name, 0)
		#define MICROPROFILE_GPU_INIT(DEVICE, PHYSICALDEVICE, CMDQUEUE, CMDQUEUEFAMILY, NODECOUNT, FUNTIONS) \
				::KMicroProfile::GPUProfilerVulkan::Init(DEVICE, PHYSICALDEVICE, CMDQUEUE, CMDQUEUEFAMILY, NODECOUNT, FUNTIONS)
		#define MICROPROFILE_GPU_UNINIT() \
				::KMicroProfile::GPUProfilerVulkan::GPUShutdown()
		#define MICROPROFILE_FLIP(pContext) \
				::KMicroProfile::GPUProfilerVulkan::Flip(pContext)
		#define MICROPROFILE_ENABLE_ALL_GROUP() \
				::KMicroProfile::GPUProfilerVulkan::EnableAllGroup()
		#define MICROPROFILE_BEGIN_GPU(pCommandBuffer) \
				::KMicroProfile::GPUProfilerVulkan::MicroProfileGPUBegin(pCommandBuffer)
		#define MICROPROFILE_END_GPU() \
				::KMicroProfile::GPUProfilerVulkan::MicroProfileGPUEnd()
	#else
		#define MICROPROFILE_SCOPE_GPU(...) do{}while(0)
		//#define MICROPROFILE_SCOPE_CPU_AND_GPU(name) do{}while(0)

		#define MICROPROFILE_GPU_INIT(DEVICE, PHYSICALDEVICE, CMDQUEUE, CMDQUEUEFAMILY, NODECOUNT, FUNTIONS)
		#define MICROPROFILE_GPU_UNINIT()
		#define MICROPROFILE_FLIP(pContext)
		#define MICROPROFILE_ENABLE_ALL_GROUP()
		#define MICROPROFILE_BEGIN_GPU()
		#define MICROPROFILE_END_GPU()
	#endif
	#define MICRROPROFILE_UNINIT() \
		::KMicroProfile::MicroProfile::Shutdown()
	#define MICROPROFILE_THREAD_CREATE(name) \
		::KMicroProfile::MicroProfile::OnThreadCreate(name)
#else
	//#define MICROPROFILE_SCOPE_CPU(...) do{}while(0)
	//#define MICROPROFILE_SCOPE_GPU(...) do{}while(0)
	//#define MICROPROFILE_SCOPE_CPU_AND_GPU(name) do{}while(0)

	#define MICROPROFILE_GPU_INIT(DEVICE, PHYSICALDEVICE, CMDQUEUE, CMDQUEUEFAMILY, NODECOUNT, FUNTIONS)
	#define MICROPROFILE_GPU_UNINIT()
	#define MICROPROFILE_FLIP(pContext)
	#define MICROPROFILE_ENABLE_ALL_GROUP()
	#define MICROPROFILE_BEGIN_GPU()
	#define MICROPROFILE_END_GPU()

	#define MICRROPROFILE_UNINIT() do{}while(0)
	#define MICROPROFILE_THREAD_CREATE(name) do{}while(0)
#endif

#pragma endregion

#define GPU_DEBUG_CONCAT_IMPL(a, b) a##b
#define GPU_DEBUG_CONCAT(a, b)      GPU_DEBUG_CONCAT_IMPL(a, b)

#if ENABLE_DEBUG_PROFILE
	#if ENABLE_OPTICK_PROFILE
		#define PROF_CPU(...) \
			OPTICK_EVENT(__VA_ARGS__)
		#define PROF_CPU_DEEP(...) \
			OPTICK_DEEP_EVENT(__VA_ARGS__)
		#define PROF_CPU_DETAIL(...) \
			OPTICK_DETAIL_EVENT(__VA_ARGS__)
		#define PROF_GPU_SCOPE(pCommandBuffer, name)                                      \
			gfx::GpuProfileScop GPU_DEBUG_CONCAT(PROF_GPU_SCOPE_, __LINE__)(pCommandBuffer, name); \
			OPTICK_GPU_EVENT(name)
        #define PROF_THREAD(name)\
            OPTICK_THREAD(name)

	#elif MICROPROFILE_ENABLED
		#define PROF_CPU(...) \
				MICROPROFILE_SCOPE_CPU(__VA_ARGS__)
		#define PROF_CPU_DEEP(...) \
				// MICROPROFILE_SCOPE_CPU(__VA_ARGS__)
		#define PROF_CPU_DETAIL(...) \
				// MICROPROFILE_SCOPE_CPU(__VA_ARGS__)

		#define PROF_GPU_SCOPE(pCommandBuffer, name) \
				gfx::GpuProfileScop GPU_DEBUG_CONCAT(PROF_GPU_SCOPE_, __LINE__)(pCommandBuffer, name); \
				MICROPROFILE_SCOPE_GPU(pCommandBuffer, name)
        #define PROF_THREAD(name)
	#endif
#else
	#define PROF_CPU(...)
	#define PROF_CPU_DEEP(...)
	#define PROF_CPU_DETAIL(...)
	#define PROF_GPU_SCOPE(pCommandBuffer, name)
    #define PROF_THREAD(name)
#endif

#if MICROPROFILE_ENABLED
struct MicroProfileScopeHandler;
struct MicroProfileScopeGpuHandler;
struct MicroProfileScopeGpuHandler_Vulkan;
struct MicroProfileThreadLogGpu;

// Vulkan Forward Declarations
#pragma region Vulkan

#ifndef VKAPI_PTR
#define MICROPROFILE_VKAPI_PTR_DEFINED 1
#if defined(_WIN32)
	// On Windows, Vulkan commands use the stdcall convention
#define VKAPI_PTR  __stdcall
#else
#define VKAPI_PTR
#endif
#endif

#define MICROPROFILE_DEFINE_HANDLE(object) typedef struct object##_T *object;
MICROPROFILE_DEFINE_HANDLE(VkDevice);
MICROPROFILE_DEFINE_HANDLE(VkPhysicalDevice);
MICROPROFILE_DEFINE_HANDLE(VkQueue);
MICROPROFILE_DEFINE_HANDLE(VkCommandBuffer);
MICROPROFILE_DEFINE_HANDLE(VkQueryPool);
MICROPROFILE_DEFINE_HANDLE(VkCommandPool);
MICROPROFILE_DEFINE_HANDLE(VkFence);

struct VkPhysicalDeviceProperties;
struct VkQueryPoolCreateInfo;
struct VkAllocationCallbacks;
struct VkCommandPoolCreateInfo;
struct VkCommandBufferAllocateInfo;
struct VkFenceCreateInfo;
struct VkSubmitInfo;
struct VkCommandBufferBeginInfo;

typedef void(VKAPI_PTR* PFN_vkGetPhysicalDeviceProperties_)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
typedef int32_t(VKAPI_PTR* PFN_vkCreateQueryPool_)(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool);
typedef int32_t(VKAPI_PTR* PFN_vkCreateCommandPool_)(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);
typedef int32_t(VKAPI_PTR* PFN_vkAllocateCommandBuffers_)(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers);
typedef int32_t(VKAPI_PTR* PFN_vkCreateFence_)(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence);
typedef void(VKAPI_PTR* PFN_vkCmdResetQueryPool_)(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
typedef int32_t(VKAPI_PTR* PFN_vkQueueSubmit_)(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
typedef int32_t(VKAPI_PTR* PFN_vkWaitForFences_)(VkDevice device, uint32_t fenceCount, const VkFence* pFences, uint32_t waitAll, uint64_t timeout);
typedef int32_t(VKAPI_PTR* PFN_vkResetCommandBuffer_)(VkCommandBuffer commandBuffer, uint32_t flags);
typedef void(VKAPI_PTR* PFN_vkCmdWriteTimestamp_)(VkCommandBuffer commandBuffer, uint32_t pipelineStage, VkQueryPool queryPool, uint32_t query);
typedef int32_t(VKAPI_PTR* PFN_vkGetQueryPoolResults_)(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, uint64_t stride, uint32_t flags);
typedef int32_t(VKAPI_PTR* PFN_vkBeginCommandBuffer_)(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
typedef int32_t(VKAPI_PTR* PFN_vkEndCommandBuffer_)(VkCommandBuffer commandBuffer);
typedef int32_t(VKAPI_PTR* PFN_vkResetFences_)(VkDevice device, uint32_t fenceCount, const VkFence* pFences);
typedef void(VKAPI_PTR* PFN_vkDestroyCommandPool_)(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator);
typedef void(VKAPI_PTR* PFN_vkDestroyQueryPool_)(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator);
typedef void(VKAPI_PTR* PFN_vkDestroyFence_)(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator);
typedef void(VKAPI_PTR* PFN_vkFreeCommandBuffers_)(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);

#if MICROPROFILE_VKAPI_PTR_DEFINED
#undef VKAPI_PTR
#endif
#pragma endregion
#endif

namespace gfx
{
	class IKGFX_RenderContext;
	struct GpuProfileScop
	{
        IKGFX_RenderContext* m_pRenderCtx = nullptr;
		const char* m_strName = nullptr;

		GpuProfileScop(IKGFX_RenderContext* pRenderCtx, const char* strName, bool bOptick = true);
		~GpuProfileScop();
	};
}

#if MICROPROFILE_ENABLED
namespace KMicroProfile
{
	// D3D12 Forward Declarations
#pragma region D3D12
	struct ID3D12CommandList;
	struct ID3D12Device;
	struct ID3D12CommandQueue;
	struct ID3D12CommandQueue;
#pragma endregion


	static const int NUM_FRAMES_DELAY = 4;

	struct KMicroProfileVulkanFunctions
	{
		PFN_vkGetPhysicalDeviceProperties_ vkGetPhysicalDeviceProperties;
		PFN_vkCreateQueryPool_ vkCreateQueryPool;
		PFN_vkCreateCommandPool_ vkCreateCommandPool;
		PFN_vkAllocateCommandBuffers_ vkAllocateCommandBuffers;
		PFN_vkCreateFence_ vkCreateFence;
		PFN_vkCmdResetQueryPool_ vkCmdResetQueryPool;
		PFN_vkQueueSubmit_ vkQueueSubmit;
		PFN_vkWaitForFences_ vkWaitForFences;
		PFN_vkResetCommandBuffer_ vkResetCommandBuffer;
		PFN_vkCmdWriteTimestamp_ vkCmdWriteTimestamp;
		PFN_vkGetQueryPoolResults_ vkGetQueryPoolResults;
		PFN_vkBeginCommandBuffer_ vkBeginCommandBuffer;
		PFN_vkEndCommandBuffer_ vkEndCommandBuffer;
		PFN_vkResetFences_ vkResetFences;
		PFN_vkDestroyCommandPool_ vkDestroyCommandPool;
		PFN_vkDestroyQueryPool_ vkDestroyQueryPool;
		PFN_vkDestroyFence_ vkDestroyFence;
		PFN_vkFreeCommandBuffers_ vkFreeCommandBuffers;
	};


	class GPUProfilerVulkan
	{
	private:
		MicroProfileScopeGpuHandler* m_Scope = nullptr;
		MicroProfileScopeGpuHandler_Vulkan* m_ScopeVulkan = nullptr;
		gfx::KVulkanCommandBuffer* m_pCommandBuffer = nullptr;
	public:
		static KMicroProfileVulkanFunctions s_VulkanFunctions;
		static MicroProfileThreadLogGpu* s_pGpuLog;

	public:
		GPUProfilerVulkan() = default;
		GPUProfilerVulkan(gfx::IKGFX_RenderContext* pRenderCtx, uint64_t uToken);
		~GPUProfilerVulkan();

	public:
		static void Init(VkDevice* devices, VkPhysicalDevice* physicalDevices, VkQueue* cmdQueues, uint32_t* cmdQueuesFamily, uint32_t nodeCount, const KMicroProfileVulkanFunctions* functions);
		static void GPUShutdown();

		static void Flip(gfx::IKGFX_RenderContext* pCommand);
		static void EnableAllGroup();

		static uint64_t GetToken(const char* name);

		static void MicroProfileGPUBegin(gfx::IKGFX_RenderContext* pCommand);
		static void MicroProfileGPUEnd();
	};

	class CPUMicroProfile
	{
	private:
		MicroProfileScopeHandler* m_Scope = nullptr;

	public:
		CPUMicroProfile() = default;
		CPUMicroProfile(uint64_t uToken);
		~CPUMicroProfile();

	public:
		static uint64_t GetToken(const char* funcName, const char* name = nullptr);
	};

	class MicroProfile
	{
	public:
		static void Shutdown();

		static void OnThreadCreate(const char* name);
	};
}
#endif
