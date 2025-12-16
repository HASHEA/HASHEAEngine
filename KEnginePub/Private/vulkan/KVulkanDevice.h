#pragma once

#include <exception>
#include <cassert>
#include <algorithm>
#include <unordered_set>
#include <string>
#include <thread>
#include "KVulkanFunc.h"
#include "Engine/File.h"
#include "KBase/Public/thread/KThread.h"
#include "../IKShaderreflector.h"
#include "KVulkanDefine.h"
#include "KVulkanPrivate.h"
#include "../IGFX_Private.h"

#define TECH_ROOT_DIR "enginedata/material/tech"
#define TECH_MB_ROOT_DIR "enginedata/material/tech_mb"

struct Options;
struct IPlatformPlugin;
struct IGraphicsPlugin;
struct PlatformData;
struct IOpenXrProgram;

#undef FreeMemory

// class KShaderCheckReflector : public gfx::IShaderReflector
//{
// public:
//	KShaderCheckReflector();
//
//	BOOL BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType) override
//	{
//		return true;
//	}
//
//	uint32_t GetPushContentAlign16BytesBlockSize() override;
//
//	uint32_t GetVsPushConstantSize() override;
//
//	void SetVsPushContantSize(uint32_t uVsPushContantSize) override;
//
//	int32_t GetMaxBinding() override;
//
//	uint32_t m_block16bytesAlignMemoryForGpu;
//	uint32_t m_vsPushConstantSize;
//	int32_t  m_nMaxBinding{};
//
//
//	void SetShaderFileName(const char* pName, gfx::ShaderStageType shaderType) override
//	{
//         uint32_t shaderid = GetGraphicAndComputeShaderId(shaderType);
//		m_ShaderFileName[shaderid] = pName;
//	}
//	const char* GetShaderFileName(gfx::ShaderStageType shaderType) override
//	{
//         uint32_t shaderid = GetGraphicAndComputeShaderId(shaderType);
//		return m_ShaderFileName[shaderid].c_str();
//	}
//	BOOL IsLogShader() override;
//
// private:
//	std::string m_ShaderFileName[SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT];
// };

namespace vks
{
    struct KShaderProgram : public NSKBase::IKRefObject
    {
        VkShaderModule m_pModule = nullptr;

        // std::string m_ShaderString;

        std::atomic<int32_t> m_nRef;
        gfx::KShaderInfo     m_shaderInfo;

        KShaderProgram();
        ~KShaderProgram();

        int32_t           AddRef() override;
        int32_t           Release() override;
        int32_t           GetRef() override;
        uint64_t          m_uNameHash;
        std::atomic<BOOL> m_bDirty;
    };

    struct UseNode
    {
        BOOL     bUse;
        uint32_t startIndex;
        uint32_t count;
        UseNode* pNext;
        UseNode();
        ~UseNode();
    };

#define VMEMORY_LEAK_DETECT 0
    struct VMemoryNode
    {
        uint32_t uInUseTableCount; // perSize / alignment
        size_t   perNodeSize;      // minsize 1 * 1024 * 1024
        uint32_t alignmentSize;
        uint32_t uUsedCount;
        UseNode* pUseNode;

        KVkDeviceMemory pMemory;
        VMemoryNode*    pNext;
        VMemoryNode*    pPreviouse;
        VMemoryNode(uint32_t type);
        ~VMemoryNode();
#if VMEMORY_LEAK_DETECT
        char* pMemDetect;
#endif
    };

    struct VMemory
    {
        VMemoryNode* pMemoryHeader;
        VMemory();
        ~VMemory();
    };

    struct KVulkanDevice : public gfx::KGfxRef
    {
        std::mutex m_devicebufferLock;
        // #if X3D_VK_USE_VMA

    private:
        VmaAllocator m_pVMAllocator = nullptr;

    private:
        BOOL _InitVMA();
        void _UnInitVMA();

    public:
        VmaAllocator GetVMAllocator() { return m_pVMAllocator; }
        BOOL         VMACreateImage(const VkImageCreateInfo& sImgCreateInfo, VmaMemoryUsage eMemUsage, VkImage& pVkImage, VmaAllocation& pVMAllocation);
        BOOL         VMADestroyImage(VkImage& pVkImage, VmaAllocation& pVMAllocation);
        BOOL         VMACreateBuffer(VkDeviceSize uBufferSize, VkBufferUsageFlags eBufferUsage, VmaMemoryUsage eMemUsage, VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation, void** ppData = nullptr);
        BOOL         VMADestroyBuffer(VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation);
        BOOL         VMAMapMemory(VmaAllocation pVMAllocation, void** ppData);
        BOOL         VMAUnmapMemory(VmaAllocation pVMAllocation);
        uint32_t     VMAGetAllocSize(VmaAllocation pVMAllocation);
        BOOL         VMAFlushAllocation(VmaAllocation pVMAllocation, VkDeviceSize uOffset = 0, VkDeviceSize uSize = VK_WHOLE_SIZE);
        BOOL         VMAInvalidateAllocation(VmaAllocation pVMAllocation, VkDeviceSize uOffset, VkDeviceSize uSize);
        BOOL         VMABuildStatsString(char** ppStatsString);
        BOOL         VMAFreeStatsString(char* ppStatsString);

        bool VMAGetAllocationIsCoherent(VmaAllocation& pVMAllocation);

        // #endif

    public:
        void* GetInstancePtr()
        {
            return m_pInstance;
        }

        void* GetLogiceDevicePtr()
        {
            return m_pLogicalDevice;
        }

        void* GetPhysicalDevicePtr()
        {
            return m_pPhysicalDevice;
        }

        void FrameMove();

        /** @brief Physical device representation */
        VkPhysicalDevice m_pPhysicalDevice = nullptr;
        /** @brief Logical device representation (application's view of the device) */
        VkDevice         m_pLogicalDevice  = nullptr;

        struct
        {
            uint32_t graphics = UINT32_MAX;
            uint32_t compute  = UINT32_MAX;
            uint32_t transfer = UINT32_MAX;
        } m_QueueFamilyIndices;

        // Handle to the device graphics queue that command buffers are submitted to
        VkQueue m_pGaphicQueue = nullptr;

        VkQueue m_pComputeQueue = nullptr;

        VkQueue m_pTransferQueue = nullptr;

        // Vulkan instance, stores all per-application states
        VkInstance m_pInstance = nullptr;

        /** @brief Example settings that can be changed e.g. by command line arguments */
        struct Settings
        {
            /** @brief Activates validation layers (and message output) when set to true */
            BOOL m_bValidation = true;
            /** @brief Set to true if fullscreen mode has been requested via command line */
            BOOL m_bFullscreen = false;
            /** @brief Set to true if v-sync will be forced for the swapchain */
            BOOL m_bVsync      = false;
            /** @brief Enable UI overlay */
            BOOL m_bOverlay    = false;

            BOOL m_bSupportsMultiview = false;

            BOOL m_bSupportsFragmentDensity = false;

            BOOL m_bSupportsDebugMarker = false;
        } m_Settings;


        /** @brief Features of the physical device that an application can use to check if a feature is supported */
        VkPhysicalDeviceFeatures m_Features = {};

        /** @brief Properties of the physical device including limits that the application can check against */
        VkPhysicalDeviceProperties m_Properties = {};

        // Pipeline cache object
        VkPipelineCache m_pPipelineCache = nullptr;

        BOOL m_bInited = false;

    private:
        VkPhysicalDeviceFeatures2          m_Features2          = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceProperties2        m_Properties2        = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        VkPhysicalDeviceSubgroupProperties m_subgroupProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};

        /** @brief Memory types and heaps of the physical device */
        VkPhysicalDeviceMemoryProperties     m_MemoryProperties = {};
        /** @brief Queue family properties of the physical device */
        std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
        /** @brief List of extensions supported by the device */
        std::vector<std::string>             m_SupportedExtensions;

        std::vector<const char*> m_vecInstanceSupportExtensions;
        std::vector<const char*> m_InstanceLayerExtensions;

        /** @brief Default command pool for the graphics queue family index */
        VkCommandPool m_pGraphicCommandPool{nullptr};
        VkCommandPool m_pComputeCommandPool{nullptr};
        VkCommandPool m_pTransferCommandPool{nullptr};

        /** @brief Set to true when the debug marker extension is detected */
        BOOL enableDebugMarkers = false;

        // List of shader modules created (stored for cleanup)
        std::map<std::string, KShaderProgram*> m_ShaderModules[SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT];
        std::mutex                             m_ShaderModules_lock;
        std::vector<vks::KShaderProgram*>      m_delayDeleteShaderProgramList;

        /** @brief Contains queue family indices */

        char m_szAppName[128] = {0};

        BOOL m_bByShaderBuilderCmdTools = FALSE;

        /**
         * Set of physical device features to be enabled for this example (must be set in the derived constructor)
         *
         * @note By default no phyiscal device features are enabled
         */
        // VkPhysicalDeviceFeatures m_EnabledFeatures{};

        /** @brief Set of device extensions to be enabled for this example (must be set in the derived constructor) */
        std::vector<const char*> m_DeviceExtensions;
        std::vector<const char*> m_InstanceExtensions;

        bool m_bSupportNsightAftermath = false;

    protected:
        BOOL _LoadPipelineCache(std::string& strCacheData);
        BOOL _SavePipelineCache(VkPipelineCache pPipelineCache);

    public:
        /**  @brief Typecast to VkDevice */
        operator VkDevice() { return m_pLogicalDevice; };

        /**
         * Default constructor
         *
         * @param physicalDevice Physical device that is to be used
         */
        KVulkanDevice(const gfx::RenderSystemInfo& renderSysteInfo, gfx::KGFX_PHYSICAL_DEVICE_LIMITS& physicallimits);

        /**
         * Default destructor
         *
         * @note Frees the logical device
         */
        ~KVulkanDevice();

        /**
         * Get the index of a memory type that has all the requested property bits set
         *
         * @param typeBits Bitmask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
         * @param properties Bitmask of properties for the memory type to request
         * @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
         *
         * @return Index of the requested memory type
         *
         * @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
         */
        uint32_t GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound = nullptr);

        void GetMemoryTypeProperties(uint32_t uMemoryTypeIndex, VkMemoryPropertyFlags& properties);


        /**
         * Get the index of a queue family that supports the requested queue flags
         *
         * @param queueFlags Queue flags to find a queue family index for
         *
         * @return Index of the queue family index that matches the flags
         *
         * @throw Throws an exception if no queue family index could be found that supports the requested flags
         */
        uint32_t GetQueueFamilyIndex(VkQueueFlagBits queueFlags);
        uint32_t GetQueueFamilyIndexProcessType(gfx::enumForProcessType processType);

        /**
         * Create the logical device based on the assigned physical device, also gets default queue family indices
         *
         * @param enabledFeatures Can be used to enable certain features upon device creation
         * @param useSwapChain Set to false for headless rendering to omit the swapchain device extensions
         * @param requestedQueueTypes Bit flags specifying the queue types to be requested from the device
         *
         * @return VkResult of the device creation call
         */
        BOOL CreateLogicalDevice(VkPhysicalDeviceFeatures& enabledFeatures, const std::vector<const char*>& enabledExtensions, VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, bool bCreateDevice = true, VkPhysicalDeviceFeatures2* pNext = nullptr);

        // #if !X3D_VK_USE_VMA
        VkResult AllocateMemory(
            VkDevice                     device,
            const VkMemoryAllocateInfo*  pAllocateInfo,
            const VkAllocationCallbacks* pAllocator,
            KVkDeviceMemory*             pMemory,
            uint32_t*                    offset,
            uint32_t                     alignSize
        );

        void FreeMemory(
            VkDevice                     device,
            KVkDeviceMemory              pMemory,
            const VkAllocationCallbacks* pAllocator,
            uint32_t                     uOffset,
            uint32_t                     uRange
        );
        void DumpMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc);
        // #endif

        /**
         * Allocate a command buffer from the command pool
         *
         * @param level Level of the new command buffer (primary or secondary)
         * @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer) (Defaults to false)
         *
         * @return A handle to the allocated command buffer
         */
        VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, BOOL begin, gfx::enumForProcessType commandType); // begin default:false

        /**
         * Finish command buffer recording and submit it to a queue
         *
         * @param commandBuffer Command buffer to flush
         * @param queue Queue to submit the command buffer to
         * @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
         *
         * @note The queue that the command buffer is submitted to must be from the same family index as the pool it was allocated from
         * @note Uses a fence to ensure command buffer has finished executing
         */
        BOOL FlushCommandBuffer(VkCommandBuffer commandBuffer, gfx::enumForProcessType processType, BOOL free, BOOL bEndCommand); // free defualt:true

        BOOL FreeCommandBuffer(VkCommandBuffer commandBuffer, gfx::enumForProcessType processType, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        BOOL FreeCommandBuffer(VkCommandBuffer commandBuffer, VkCommandPool commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        /**
         * Check if an extension is supported by the (physical device)
         *
         * @param extension Name of the extension to check
         *
         * @return True if the extension is supported (present in the list read at device creation time)
         */
        BOOL ExtensionSupported(std::string extension);

        /////////////////////////////////////////////////////////////////////
        // KVulkanBase();
        // virtual ~KVulkanBase();

        /** @brief (Virtual) Called after the physical device features have been read, can be used to set features to enable on the device */
        virtual void GetEnabledFeatures();

        // Load a text or SPIR-V shader whole combined shader
        BOOL CreateShaderSpv(const char* szShaderName, VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStage, gfx::IShaderReflector* pReflector);

        // Load a text string shader
        BOOL CreateShaderString(vks::KShaderProgram** ppProgram, gfx::KShaderInfo* pShaderInfo, const char* szShaderString, const VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStage, gfx::IShaderReflector* pReflector, uint32_t* pRetHash, BOOL* pRealBuild, std::function<BOOL(void)> postCreateCallBack = nullptr);
        BOOL ReCreateShaderString(gfx::KShaderInfo* pShaderInfo, const char* szShaderString, const VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStage, gfx::IShaderReflector* pReflector, uint32_t* pRetHash, BOOL* pRealBuild, std::function<BOOL(void)> postCreateCallBack = nullptr);

        BOOL LoadShader(
            gfx::KShaderStage*              pShaderStage,
            const char*                     szShaderSource,
            const NSKBase::tagFileLocation& sIncludeShaderLoc,
            const char*                     szShaderDef,
            const char*                     szMacro,
            gfx::ShaderStageType            eShaderStage,
            gfx::IShaderReflector*          pReflector,
            BOOL                            bForceReload = false
        );

        BOOL LoadShaderWithoutTech(
            gfx::KShaderStage* pShaderStage,
            const char* pMainShader,
            const char* pEnterPoint,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char* pcszMacro,
            gfx::ShaderStageType eShaderStage,
            gfx::IShaderReflector* pReflector
        );


        BOOL CreateShader(vks::KShaderProgram** ppProgram, gfx::KShaderStage* pShaderStage, VkPipelineShaderStageCreateInfo& retShaderStage, uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector);
        BOOL ReCreateShader(gfx::KShaderInfo* pShaderInfo, std::string& strShaderContent, VkPipelineShaderStageCreateInfo& retShaderStage, uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector);

    public:
        // BOOL RemoveShaderModuleByName(const char* szShaderName);
        BOOL RemoveShaderModule(vks::KShaderProgram* pShaderProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey);

    private:
        // BOOL _RemoveShaderModule_NoLock(const char* szShaderName);
        BOOL _DelayRemoveShaderModules(BOOL bForce);
        BOOL _RemoveDirtyShaderModules_NoLock(vks::KShaderProgram* pShaderProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey);
        BOOL _RemoveDirtyShaderModules_NoLock_MaybeDirtyDeletedByCreate(vks::KShaderProgram* pProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey);

    public:
        BOOL RemoveDirtyShaderModules_MaybeDirtyDeletedByCreate(vks::KShaderProgram* pProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey);
        BOOL RefreshShaderModules();

        void                CheckInstanceLayerValidation();
        virtual const char* GetGpuName();

        VkPhysicalDeviceProperties GetPhysicalDeviceProperties() { return m_Properties; }

        BOOL IsSubGoupQuadSupported() const
        {
            return m_subgroupProperties.quadOperationsInAllStages != 0;
        }

        BOOL IsSubGoupF16Supported() const
        {
            return m_bSubgroupF16Enabled;
        }

        BOOL IsFp16Supported() const
        {
            return m_bFp16Enabled;
        }

        BOOL IsAtomicUint64Supported() const
        {
            return m_bAtomicUint64Enabled;
        }

        uint32_t GetMinUniformBufferOffsetAlignment()
        {
            return (uint32_t)m_Properties.limits.minUniformBufferOffsetAlignment;
        }

        uint32_t GetStorageBufferOffsetAlignment()
        {
            return (uint32_t)m_Properties.limits.minStorageBufferOffsetAlignment;
        }

        uint32_t GetCoherentAtomSize()
        {
            return (uint32_t)m_Properties.limits.nonCoherentAtomSize;
        }

        VkCommandPool GetCommonPool(gfx::enumForProcessType processType);

        void SetObjectLabel(VkImage image, const char* szName);
        void SetObjectLabel(VkImageView imageView, const char* szName);
        void SetObjectLabel(VkFramebuffer frameBuffer, const char* szName);
        void SetObjectLabel(VkRenderPass pass, const char* szName);
        void SetObjectLabel(VkShaderModule shader, const char* szName);
        void SetObjectLabel(VkBuffer buffer, const char* szName);
        void SetObjectLabel(VkBufferView bufferView, const char* szName);
        void SetObjectLabel(VkCommandPool pool, const char* szName);
        void SetObjectLabel(VkCommandBuffer cmd, const char* szName);
        void SetObjectLabel(VkFence fence, const char* szName);
        void SetObjectLabel(VkQueryPool pool, const char* szName);

        virtual BOOL SavePipelineCache();

    public:
        /**
         * Create a command pool for allocation command buffers from
         *
         * @param queueFamilyIndex Family index of the queue to create the command pool for
         * @param createFlags (Optional) Command pool creation flags (Defaults to VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
         *
         * @note Command buffers allocated from the created pool can only be submitted to a queue with the same family index
         *
         * @return A handle to the created command buffer
         */
        VkCommandPool CreateCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        void TrimCommandPool();

    public:
        BOOL IsRunByShaderBuilderCmdTools() { return m_bByShaderBuilderCmdTools; }

    private:
        std::map<KVkDeviceMemory, VMemory*> m_mapMemoryMapping;
        // #if !X3D_VK_USE_VMA
        std::map<uint32_t, VMemory*>        m_mapMemmory;
        // #endif

        BOOL _EnumPhysicalDeviceProperties(gfx::KGFX_PHYSICAL_DEVICE_LIMITS& physicallimits);

        BOOL _CreateInstance(const gfx::RenderSystemInfo& renderSysteInfo);
        BOOL _CreateDevice(const gfx::RenderSystemInfo& renderSysteInfo);

    private:
        std::map<std::string, std::string> m_mapErrorShader;
        std::mutex                         m_MemoryContainerLock; // m_mapMemmory 操作

        BOOL m_bFp16Enabled         = FALSE;
        BOOL m_bSubgroupF16Enabled  = FALSE;
        BOOL m_bAtomicUint64Enabled = FALSE;

        BOOL m_bRayTracingEnabled   = FALSE;
        BOOL m_bBufferDeviceAddress = FALSE;
    };
}; // namespace vks

extern "C" {
BOOL CreateVulkanDevice(const gfx::RenderSystemInfo& renderSysteInfo, gfx::KGFX_PHYSICAL_DEVICE_LIMITS& physicallimits);

void DestroyVulkanDevice();

vks::KVulkanDevice* GetVulkanDevice();


// Physical device (GPU) that Vulkan will use
VkPhysicalDevice GetVkPhysicalDevice();

// logic device from m_pVulkanDevice
VkDevice GetVkDevice();


VkQueue GetGraphicQueue();
VkQueue GetComputeQueue();
VkQueue GetTransferQueue();

VkQueue GetQueue(gfx::enumForProcessType processType);
}
