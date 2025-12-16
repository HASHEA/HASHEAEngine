#include "KVulkanDevice.h"
#include "ShaderLang.h"

#include "KEnginePub/Public/KEsDrv.h"
#include "KEngine/Public/KEngineCore.h"
#include "Engine/KGLog.h"
#include "KSpirv/Private/KSpirvBuilder.h"
#include "KBase/Public/thread/KThread.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include <fstream>
#include <sstream>

#include "KVulkanFunc.h"
#include "KVulkanTools.h"
#include "KVulkanDebug.h"
#include "KVulkanTools.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "KVulkanInitializers.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/thread/KThread.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "GFXVulkan.h"
#include "KVulkan.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#ifdef _WIN32
#include "../recorder/KShaderRecorder.h"
#include "aftermath/NsightAftermathGpuCrashTracker.h"
#endif // _WIN32
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KBase/Public/str/KEncoding.h"
#include "KShaderResourceVK.h"
#include "PakV5/PakV5.h"
#include "Engine/KG_HashFunction.h"
#include "KEnginePub/Public/switchoption/KEngineSwitchOption.h"
#include "KBase/Public/System/ISystem.h"
#include "KBase/Public/KBasePub.h"
#include "KBase/Public/str/KUtf8Convert.h"
#include "KMaterialSystem/Public/IKMaterialTypes.h"
#include "KEnginePub/Private/comm/KGFX_ShaderHelper.h"
#include "KEnginePub/Private/comm/KGFX_ShaderCombinedResult.h"
#include "KEngine/Public/Render/KEngineRender.h"
#include "KBase/Public/mics/KResourceErrorReporter.h"
#include "KEnginePub/Public/KEngineOptionBase.h"

#include "vulkan/vma/vk_mem_alloc_imp.inl"

//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

#define SHADER_RESOURCE_REFLECT_DATA_MASK  123456789
#define MACRO_X3D_PIPELINE_CACHE_FILE_PATH "cache/mb_engine/pipeline_cache.data"
#ifndef __APPLE__
#define MACRO_X3D_ENABLE_PIPELINE_CACHE 0
#else
#define MACRO_X3D_ENABLE_PIPELINE_CACHE 0
#endif

#define BASE_VMEM_NODE_MB             2
#define ENABLE_RENDERDOC_DEBUG_MARKER 0 // for renderdoc debug marker
#define VK_EXT_DEBUG_MARKER_NAME      "VK_EXT_debug_marker"


#ifdef _WIN32
// GPU crash dump tracker using Nsight Aftermath instrumentation
GpuCrashTracker::MarkerMap s_AftermathMarkerMap{};
GpuCrashTracker s_AftermathGpuCrashTracker{ s_AftermathMarkerMap };
uint64_t s_AftermathFrameNumber{0};
#endif

std::vector<const char*> validationLayerNames;
vks::KVulkanDevice*      g_pVulkanDevice = nullptr;

BOOL CreateVulkanDevice(const gfx::RenderSystemInfo& renderSysteInfo, gfx::KGFX_PHYSICAL_DEVICE_LIMITS& physicallimits)
{
    if (!g_pVulkanDevice)
    {
        g_pVulkanDevice = new vks::KVulkanDevice(renderSysteInfo, physicallimits);
    }
    return g_pVulkanDevice->m_bInited;
}

void DestroyVulkanDevice()
{
    SAFE_DELETE(g_pVulkanDevice);
}

VkPhysicalDevice GetVkPhysicalDevice()
{
    return g_pVulkanDevice->m_pPhysicalDevice;
}

VkDevice GetVkDevice()
{
    if (g_pVulkanDevice)
    {
        return g_pVulkanDevice->m_pLogicalDevice;
    }
    else
    {
        return nullptr;
    }
}

vks::KVulkanDevice* GetVulkanDevice()
{
    return g_pVulkanDevice;
}

VkQueue GetGraphicQueue()
{
    return g_pVulkanDevice->m_pGaphicQueue;
}

VkQueue GetComputeQueue()
{
    return g_pVulkanDevice->m_pComputeQueue;
}

VkQueue GetTransferQueue()
{
    return g_pVulkanDevice->m_pTransferQueue;
}

VkQueue GetQueue(gfx::enumForProcessType processType)
{
    VkQueue queue = nullptr;
    switch (processType)
    {
    case gfx::FOR_GRPAHIC:
        queue = g_pVulkanDevice->m_pGaphicQueue;
        break;
    case gfx::FOR_COMPUTE:
        queue = g_pVulkanDevice->m_pComputeQueue;
        break;
    case gfx::FOR_TRANSFER:
        queue = g_pVulkanDevice->m_pTransferQueue;
        break;
    default:
        break;
    }
    return queue;
}

vks::KShaderProgram::KShaderProgram()
    : m_nRef(1)
    , m_bDirty(false)
    , m_pModule(nullptr)
{
    m_uNameHash = 0;

    KX3DEngineMonitor* pEngineMonitor = NSEngine::GetEngineMonitor();
    ++pEngineMonitor->m_sGraphics.nKShaderProgram;
}

vks::KShaderProgram::~KShaderProgram()
{
    VkDevice pDevice = GetVkDevice();
    if (pDevice && m_pModule)
    {
        vks::vkDestroyShaderModule(pDevice, m_pModule, nullptr);
        m_pModule = nullptr;
    }

    KX3DEngineMonitor* pEngineMonitor = NSEngine::GetEngineMonitor();
    --pEngineMonitor->m_sGraphics.nKShaderProgram;
}

int32_t vks::KShaderProgram::AddRef()
{
    // if (strstr(m_shaderInfo.strScPath.c_str(), "meshshader") && m_shaderInfo.eShaderStage == gfx::SHADER_STAGE_VERTEX_BIT)
    //{
    //	int x =0;
    // }
    ++m_nRef;
    return m_nRef;
}

int32_t vks::KShaderProgram::Release()
{
    int n = --m_nRef;
    ASSERT(n >= 0);
    if (n == 0)
    {
        VkDevice             pDevice    = GetVkDevice();
        gfx::KGraphicDevice* pGfxDevice = gfx::GetGraphicDevice();
        if (m_pModule && pGfxDevice && pGfxDevice->bInitedGraphic)
        {
            vks::vkDestroyShaderModule(pDevice, m_pModule, nullptr);
            m_pModule = nullptr;
        }
        delete (this);
    }
    return n;
}

int32_t vks::KShaderProgram::GetRef()
{
    return m_nRef;
}

BOOL vks::KVulkanDevice::_LoadPipelineCache(std::string& strCacheData)
{
#if MACRO_X3D_ENABLE_PIPELINE_CACHE
    PROF_CPU();

    BOOL         bResult        = FALSE;
    BOOL         bRetCode       = FALSE;
    IFile*       pFile          = nullptr;
    unsigned int uFileSize      = 0;
    unsigned int uReadSize      = 0;
    uint64_t     uSaveCacheHash = 0;
    uint64_t     uCacheHash     = 0;
    uint32_t     uCacheSize     = 0;

    pFile = g_OpenAloneFile(MACRO_X3D_PIPELINE_CACHE_FILE_PATH, FALSE);
    KG_PROCESS_ERROR(pFile);

    uFileSize = pFile->Size();
    KG_PROCESS_ERROR(uFileSize > sizeof(uSaveCacheHash));

    uReadSize = pFile->Read(&uSaveCacheHash, sizeof(uSaveCacheHash));
    KGLOG_ASSERT_EXIT(uReadSize == sizeof(uSaveCacheHash));

    uCacheSize = uFileSize - sizeof(uSaveCacheHash);
    strCacheData.resize(uCacheSize);
    uReadSize = pFile->Read((void*)strCacheData.data(), uCacheSize);
    KGLOG_ASSERT_EXIT(uReadSize == uCacheSize);

    uCacheHash = DJB2_64(strCacheData.data(), (unsigned int)strCacheData.size());
    KGLOG_ASSERT_EXIT(uSaveCacheHash == uCacheHash);

    KGLogPrintf(KGLOG_INFO, "[KVulkanDevice] load pipeline cache success");
    bResult = TRUE;
Exit0:
    SAFE_RELEASE(pFile);
    return bResult;
#else
    return FALSE;
#endif
}

BOOL vks::KVulkanDevice::_SavePipelineCache(VkPipelineCache pPipelineCache)
{
#if MACRO_X3D_ENABLE_PIPELINE_CACHE
    PROF_CPU();

    BOOL         bResult         = FALSE;
    BOOL         bRetCode        = FALSE;
    VkResult     vkResult        = VK_RESULT_MAX_ENUM;
    size_t       stCacheDataSize = 0;
    std::string  strCacheData;
    IFile*       pFile      = nullptr;
    unsigned int uWriteSize = 0;
    uint64_t     uCacheHash = 0;

    KGLOG_ASSERT_EXIT(m_pLogicalDevice);
    KGLOG_ASSERT_EXIT(pPipelineCache);

    vkResult = vks::vkGetPipelineCacheData(m_pLogicalDevice, pPipelineCache, &stCacheDataSize, nullptr);
    KGLOG_ASSERT_EXIT(vkResult == VK_SUCCESS);

    strCacheData.resize(stCacheDataSize);
    vkResult = vks::vkGetPipelineCacheData(m_pLogicalDevice, pPipelineCache, &stCacheDataSize, (void*)strCacheData.data());
    KGLOG_ASSERT_EXIT(vkResult == VK_SUCCESS);

    pFile = g_CreateAloneFile(MACRO_X3D_PIPELINE_CACHE_FILE_PATH);
    KGLOG_ASSERT_EXIT(pFile);

    uCacheHash = DJB2_64(strCacheData.data(), (unsigned int)strCacheData.size());
    uWriteSize = pFile->Write(&uCacheHash, (unsigned int)sizeof(uCacheHash));
    KGLOG_ASSERT_EXIT(uWriteSize == sizeof(uCacheHash));

    uWriteSize = pFile->Write((void*)strCacheData.data(), (unsigned int)strCacheData.size());
    KGLOG_ASSERT_EXIT(uWriteSize == strCacheData.size());

    KGLogPrintf(KGLOG_INFO, "[KVulkanDevice] save pipeline cache success");
    bResult = TRUE;
Exit0:
    SAFE_RELEASE(pFile);
    return bResult;
#else
    return TRUE;
#endif
}

BOOL vks::KVulkanDevice::SavePipelineCache()
{
#if MACRO_X3D_ENABLE_PIPELINE_CACHE
    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_ASSERT_EXIT(m_pPipelineCache);

    bRetCode = _SavePipelineCache(m_pPipelineCache);
    KGLOG_ASSERT_EXIT(bRetCode);

    bResult = TRUE;
Exit0:
    return bResult;
#else
    return TRUE;
#endif
}

vks::KVulkanDevice::KVulkanDevice(const gfx::RenderSystemInfo& renderSysteInfo, gfx::KGFX_PHYSICAL_DEVICE_LIMITS& physicallimits)
{
    BOOL     bRetCode = false;
    VkResult hRetCode = VK_INCOMPLETE;

    strncpy(m_szAppName, renderSysteInfo.pAppName.c_str(), 128);

    m_bByShaderBuilderCmdTools = renderSysteInfo.bByShaderBuilderCmdTools;

    KG_PROCESS_ERROR(!renderSysteInfo.bByShaderBuilderCmdTools);

    bRetCode = vks::LoadVulkanLibrary();
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = _CreateInstance(renderSysteInfo);
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = _EnumPhysicalDeviceProperties(physicallimits);
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = _CreateDevice(renderSysteInfo);
    KGLOG_PROCESS_ERROR(bRetCode);

    // #if X3D_VK_USE_VMA
    if (DrvOption::bX3D_VK_USE_VMA)
    {
        bRetCode = _InitVMA();
        KGLOG_PROCESS_ERROR(bRetCode);
    }
    // #endif

    m_bInited = true;
Exit0:
    return;
}

const char* vks::KVulkanDevice::GetGpuName()
{
    return m_Properties.deviceName;
}

VkCommandPool vks::KVulkanDevice::GetCommonPool(gfx::enumForProcessType eProcessType)
{
    VkCommandPool pRetPool = VK_NULL_HANDLE;

    switch (eProcessType)
    {
    case gfx::FOR_GRPAHIC:
        pRetPool = m_pGraphicCommandPool;
        break;
    case gfx::FOR_COMPUTE:
        pRetPool = m_pComputeCommandPool;
        break;
    case gfx::FOR_TRANSFER:
        pRetPool = m_pTransferCommandPool;
        break;
    default:
        break;
    }

    // Exit0:
    return pRetPool;
}

void vks::KVulkanDevice::SetObjectLabel(VkImage image, const char* szName)
{
    static char buffer[1024];
    ConvertGBKToUtf8(buffer, 1024, szName);
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_IMAGE;
    info.objectHandle                  = (uint64_t)image;
    info.pObjectName                   = buffer;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkImageView imageView, const char* szName)
{
    static char buffer[1024];
    ConvertGBKToUtf8(buffer, 1024, szName);
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_IMAGE_VIEW;
    info.objectHandle                  = (uint64_t)imageView;
    info.pObjectName                   = buffer;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkFramebuffer frameBuffer, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_FRAMEBUFFER;
    info.objectHandle                  = (uint64_t)frameBuffer;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkRenderPass pass, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_RENDER_PASS;
    info.objectHandle                  = (uint64_t)pass;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkShaderModule shader, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_SHADER_MODULE;
    info.objectHandle                  = (uint64_t)shader;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkBuffer buffer, const char* szName)
{
    static char utf8[1024];
    ConvertGBKToUtf8(utf8, 1024, szName);
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_BUFFER;
    info.objectHandle                  = (uint64_t)buffer;
    info.pObjectName                   = utf8;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkBufferView bufferView, const char* szName)
{
    static char utf8[1024];
    ConvertGBKToUtf8(utf8, 1024, szName);
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_BUFFER_VIEW;
    info.objectHandle                  = (uint64_t)bufferView;
    info.pObjectName                   = utf8;
}

void vks::KVulkanDevice::SetObjectLabel(VkCommandPool pool, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_COMMAND_POOL;
    info.objectHandle                  = (uint64_t)pool;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkCommandBuffer cmd, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_COMMAND_BUFFER;
    info.objectHandle                  = (uint64_t)cmd;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkFence fence, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_FENCE;
    info.objectHandle                  = (uint64_t)fence;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}

void vks::KVulkanDevice::SetObjectLabel(VkQueryPool pool, const char* szName)
{
    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType                    = VK_OBJECT_TYPE_QUERY_POOL;
    info.objectHandle                  = (uint64_t)pool;
    info.pObjectName                   = szName;

    vks::vkSetDebugUtilsObjectNameEXT(m_pLogicalDevice, &info);
}


/**
 * Default destructor
 *
 * @note Frees the logical device
 */
vks::KVulkanDevice::~KVulkanDevice()
{
    OPTICK_GPU_UNINIT_VULKAN();


#if MICROPROFILE_ENABLED
    MICROPROFILE_GPU_UNINIT();
#endif

#if MACRO_X3D_ENABLE_PIPELINE_CACHE
    if (m_pPipelineCache)
    {
        // m_pLogicalDevice 不能为空
        _SavePipelineCache(m_pPipelineCache);
    }
#endif

    {
        _DelayRemoveShaderModules(true);

        std::lock_guard<std::mutex> lock(m_ShaderModules_lock);
        for (uint32_t i = 0; i < SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT; ++i)
        {
            if (!m_ShaderModules[i].empty())
            {
                KGLogPrintf(KGLOG_WARNING, "there are some shaders not destroy:");
                for (auto it = m_ShaderModules[i].begin(); it != m_ShaderModules[i].end(); ++it)
                {
                    KShaderProgram* pShaderPragram = it->second;
                    // KGLogPrintf(KGLOG_WARNING, "force delete shader ...%s", pShaderPragram->m_ShaderString.c_str());
                    SAFE_RELEASE(pShaderPragram);
                }
                m_ShaderModules[i].clear();
            }
        }
        vks::vkDestroySamplers(m_pLogicalDevice);
    }

    if (m_pPipelineCache)
    {
        vks::vkDestroyPipelineCache(m_pLogicalDevice, m_pPipelineCache, nullptr);
    }

    if (m_pGraphicCommandPool)
    {
        vks::vkDestroyCommandPool(m_pLogicalDevice, m_pGraphicCommandPool, nullptr);
    }

    if (m_pComputeCommandPool)
    {
        vks::vkDestroyCommandPool(m_pLogicalDevice, m_pComputeCommandPool, nullptr);
    }

    if (m_pTransferCommandPool)
    {
        vks::vkDestroyCommandPool(m_pLogicalDevice, m_pTransferCommandPool, nullptr);
    }

    // #if !X3D_VK_USE_VMA
    if (!DrvOption::bX3D_VK_USE_VMA)
    {
        // #if X3D_VK_USE_CUSTOM_ALLOCATOR
        if (DrvOption::bX3D_VK_USE_CUSTOM_ALLOCATOR)
        {
            for (auto it : m_mapMemmory)
            {
                VMemory* pMem = it.second;
                if (pMem->pMemoryHeader)
                {
                    // LOGW("deviceMemory do not proper destroy");
                    VMemoryNode* pNode = pMem->pMemoryHeader;
                    while (pNode)
                    {
                        VMemoryNode* p        = pNode;
                        UseNode*     pUseNode = p->pUseNode;
                        while (pUseNode)
                        {
                            if (pUseNode->bUse)
                            {
                                KGLogPrintf(KGLOG_WARNING, "detect deviceMeory(key:%d index:%d) is not release", it.first, pUseNode->startIndex);
                            }
                            pUseNode = pUseNode->pNext;
                        }
                        pNode = pNode->pNext;
                        // p->pNext = nullptr;
                        // SAFE_DELETE(p);
                    }
                }
                SAFE_DELETE(pMem);
            }
        }
    }
    // #endif
    // #endif

    if (DrvOption::bX3D_VK_USE_VMA)
    {
        _UnInitVMA();
    }

    if (m_pLogicalDevice)
    {
        vks::vkDestroyDevice(m_pLogicalDevice, nullptr);
        m_pLogicalDevice = nullptr;
    }

    if (m_Settings.m_bValidation)
    {
        vks::debug::FreeDebugCallback(m_pInstance);
    }
    if (m_pInstance)
    {
        vks::vkDestroyInstance(m_pInstance, nullptr);
        m_pInstance = nullptr;
    }

    vks::FreeVulkanLibrary();


#if MICROPROFILE_ENABLED
    MICRROPROFILE_UNINIT();
#endif
}

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
uint32_t vks::KVulkanDevice::GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound)
{
    for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount; i++)
    {
        if ((typeBits & 1) == 1)
        {
            if ((m_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                if (memTypeFound)
                {
                    *memTypeFound = true;
                }
                return i;
            }
        }
        typeBits >>= 1;
    }

    if (memTypeFound)
    {
        *memTypeFound = false;
        return 0;
    }
    else
    {
        throw std::runtime_error("Could not find a matching memory type");
    }
}

void vks::KVulkanDevice::GetMemoryTypeProperties(uint32_t uMemoryTypeIndex, VkMemoryPropertyFlags& properties)
{
    if (uMemoryTypeIndex < m_MemoryProperties.memoryTypeCount)
    {
        properties = m_MemoryProperties.memoryTypes[uMemoryTypeIndex].propertyFlags;
    }
    else
    {
        ASSERT(FALSE);
    }
}


/**
 * Get the index of a queue family that supports the requested queue flags
 *
 * @param queueFlags Queue flags to find a queue family index for
 *
 * @return Index of the queue family index that matches the flags
 *
 * @throw Throws an exception if no queue family index could be found that supports the requested flags
 */
uint32_t vks::KVulkanDevice::GetQueueFamilyIndex(VkQueueFlagBits queueFlags)
{
    // Dedicated queue for compute
    // Try to find a queue family index that supports compute but not graphics
    if (queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_QueueFamilyProperties.size()); i++)
        {
            if ((m_QueueFamilyProperties[i].queueFlags & queueFlags) && ((m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
            {
                return i;
                break;
            }
        }
    }

    // Dedicated queue for transfer
    // Try to find a queue family index that supports transfer but not graphics and compute
    if (queueFlags & VK_QUEUE_TRANSFER_BIT)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_QueueFamilyProperties.size()); i++)
        {
            if ((m_QueueFamilyProperties[i].queueFlags & queueFlags) && ((m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
            {
                return i;
                break;
            }
        }
    }

    // For other queue types or if no separate compute queue is present, return the first one to support the requested flags
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_QueueFamilyProperties.size()); i++)
    {
        if (m_QueueFamilyProperties[i].queueFlags & queueFlags)
        {
            return i;
            break;
        }
    }

    // throw std::runtime_error("Could not find a matching queue family index");
    return UINT32_MAX;
}

uint32_t vks::KVulkanDevice::GetQueueFamilyIndexProcessType(gfx::enumForProcessType processType)
{
    switch (processType)
    {
    case gfx::FOR_GRPAHIC:
        return m_QueueFamilyIndices.graphics;
    case gfx::FOR_COMPUTE:
        return m_QueueFamilyIndices.compute;
    case gfx::FOR_TRANSFER:
        return m_QueueFamilyIndices.transfer;
    default:
        return UINT32_MAX;
    }
}

/**
 * Create the logical device based on the assigned physical device, also gets default queue family indices
 *
 * @param enabledFeatures Can be used to enable certain features upon device creation
 * @param useSwapChain Set to false for headless rendering to omit the swapchain device extensions
 * @param requestedQueueTypes Bit flags specifying the queue types to be requested from the device
 *
 * @return VkResult of the device creation call
 */
BOOL vks::KVulkanDevice::CreateLogicalDevice(VkPhysicalDeviceFeatures& enabledFeatures, const std::vector<const char*>& enabledExtensions, VkQueueFlags requestedQueueTypes, bool bCreateDevice, VkPhysicalDeviceFeatures2* pFeature2)
{
    BOOL     bResult   = FALSE;
    BOOL     bRetCode  = FALSE;
    VkResult vkRetCode = VK_INCOMPLETE;

    VkDeviceCreateInfo deviceCreateInfo = {};

    // const float defaultQueuePriority = (discreteQueuePriorities <= 2) ? 0.0f : 0.5f;

    // Desired queues need to be requested upon logical device creation
    // Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially if the application
    // requests different queue types
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

    // Get queue family indices for the requested queue family types
    // Note that the indices may overlap depending on the implementation
    const float defaultQueuePriority(0.0f);

    // Graphics queue
    if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
    {
        m_QueueFamilyIndices.graphics = GetQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = m_QueueFamilyIndices.graphics;
        queueInfo.queueCount       = 1;
        queueInfo.pQueuePriorities = &defaultQueuePriority;
        queueCreateInfos.push_back(queueInfo);
    }

    ASSERT(m_QueueFamilyIndices.graphics != UINT32_MAX);

    // Dedicated compute queue
    if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT)
    {
        m_QueueFamilyIndices.compute = GetQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
        if ((m_QueueFamilyIndices.compute != UINT32_MAX) && m_QueueFamilyIndices.compute != m_QueueFamilyIndices.graphics)
        {
            // If compute family index differs, we need an additional queue create info for the compute queue
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = m_QueueFamilyIndices.compute;
            queueInfo.queueCount       = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
        }
    }

    if (m_QueueFamilyIndices.compute == UINT32_MAX)
    {
        // Else we use the same queue
        m_QueueFamilyIndices.compute = m_QueueFamilyIndices.graphics;
    }

    // Dedicated transfer queue
    if (requestedQueueTypes & VK_QUEUE_TRANSFER_BIT)
    {
        m_QueueFamilyIndices.transfer = GetQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
        if ((m_QueueFamilyIndices.transfer != UINT32_MAX) && (m_QueueFamilyIndices.transfer != m_QueueFamilyIndices.graphics) && (m_QueueFamilyIndices.transfer != m_QueueFamilyIndices.compute))
        {
            // If compute family index differs, we need an additional queue create info for the compute queue
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = m_QueueFamilyIndices.transfer;
            queueInfo.queueCount       = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
        }
    }

    if (m_QueueFamilyIndices.transfer == UINT32_MAX)
    {
        m_QueueFamilyIndices.transfer = m_QueueFamilyIndices.graphics;
    }

    // Create the logical device representation
    deviceCreateInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext                = pFeature2;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos    = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures     = pFeature2 != nullptr ? nullptr : &enabledFeatures;

    // Enable the debug marker extension if it is present (likely meaning a debugging tool is present)
    if (enabledExtensions.size() > 0)
    {
        deviceCreateInfo.enabledExtensionCount   = (uint32_t)enabledExtensions.size();
        deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
    }

    // std::vector<const char*> extens = {"VK_KHR_swapchain" };
    // deviceCreateInfo.enabledExtensionCount = (uint32_t)extens.size();
    // deviceCreateInfo.ppEnabledExtensionNames = extens.data();

    if (bCreateDevice)
    {
        vkRetCode = vks::vkCreateDevice(m_pPhysicalDevice, &deviceCreateInfo, nullptr, &m_pLogicalDevice);
        KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

        vks::LoadVulkanDeviceFunctions(m_pLogicalDevice);
    }
    else
    {
        m_pLogicalDevice = GetLoadedDevice();
    }

    // Create a default command pool for graphics command buffers
    m_pGraphicCommandPool = CreateCommandPool(m_QueueFamilyIndices.graphics);
    SetObjectLabel(m_pGraphicCommandPool, "GraphicCommandPool");

    m_pComputeCommandPool = CreateCommandPool(m_QueueFamilyIndices.compute);
    SetObjectLabel(m_pComputeCommandPool, "ComputeCommandPool");

    m_pTransferCommandPool = CreateCommandPool(m_QueueFamilyIndices.transfer);
    SetObjectLabel(m_pTransferCommandPool, "TransferCommandPool_");

    // Get a graphics queue from the device
    vks::vkGetDeviceQueue(m_pLogicalDevice, m_QueueFamilyIndices.graphics, 0, &m_pGaphicQueue);
    vks::vkGetDeviceQueue(m_pLogicalDevice, m_QueueFamilyIndices.compute, 0, &m_pComputeQueue);
    vks::vkGetDeviceQueue(m_pLogicalDevice, m_QueueFamilyIndices.transfer, 0, &m_pTransferQueue);

    {
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        // pipelineCacheCreateInfo.flags                     = VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT; // request vulkan 1.3

#if MACRO_X3D_ENABLE_PIPELINE_CACHE
        std::string strPipelineCache;
        bRetCode = _LoadPipelineCache(strPipelineCache);
        if (bRetCode)
        {
            pipelineCacheCreateInfo.initialDataSize = strPipelineCache.size();
            pipelineCacheCreateInfo.pInitialData    = strPipelineCache.data();
        }
#endif

        vkRetCode = vks::vkCreatePipelineCache(m_pLogicalDevice, &pipelineCacheCreateInfo, nullptr, &m_pPipelineCache);
        if (vkRetCode != VK_SUCCESS && pipelineCacheCreateInfo.pInitialData)
        {
            KGLogPrintf(KGLOG_ERR, "[KVulkanDevice] create pipeline cache with data failed, data size:%d", (int)pipelineCacheCreateInfo.initialDataSize);

            pipelineCacheCreateInfo.initialDataSize = 0;
            pipelineCacheCreateInfo.pInitialData    = nullptr;

            vkRetCode = vks::vkCreatePipelineCache(m_pLogicalDevice, &pipelineCacheCreateInfo, nullptr, &m_pPipelineCache);
            KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);
        }
    }

    bResult = true;
Exit0:
    return bResult;
}

vks::UseNode::UseNode()
{
    bUse       = 0;
    startIndex = 0;
    count      = 0;
    pNext      = nullptr;

    KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
    ++pMonitor->nVMemoryNodeUseNodeCount;
}

vks::UseNode::~UseNode()
{
    KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
    --pMonitor->nVMemoryNodeUseNodeCount;
}
uint32_t vmAllocCounter = 0;
#if VMEMORY_LEAK_DETECT
uint32_t vmDetectNum = 0;
#endif

vks::VMemoryNode::VMemoryNode(uint32_t type)
{
    perNodeSize      = 0;
    alignmentSize    = 0;
    uInUseTableCount = 0;
    pMemory          = nullptr;
    pNext            = nullptr;
    pPreviouse       = nullptr;
    uUsedCount       = 0;

    pUseNode = nullptr;

#if VMEMORY_LEAK_DETECT
    pMemDetect = nullptr;
    if (type == 1)
    {
        vmDetectNum++;
        pMemDetect = new char[vmDetectNum];
    }
#endif

    KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
    ++pMonitor->nVMemoryNodeCount;
}

vks::VMemoryNode::~VMemoryNode()
{
#if VMEMORY_LEAK_DETECT
    SAFE_DELETE_ARRAY(pMemDetect);
#endif

    UseNode* pNode = pUseNode;
    while (pNode)
    {
        UseNode* pCurNode = pNode;
        pNode             = pCurNode->pNext;
        delete (pCurNode);
    }

    VkDevice pDevice = GetVkDevice();
    if (pMemory)
    {
        vks::vkFreeMemory(pDevice, pMemory, nullptr);
        vmAllocCounter--;
        pMemory = nullptr;
    }
    pNext = nullptr;

    KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
    --pMonitor->nVMemoryNodeCount;
}

vks::VMemory::VMemory()
{
    pMemoryHeader = nullptr;
}

vks::VMemory::~VMemory()
{
    VMemoryNode* pNode = pMemoryHeader;
    while (pNode)
    {
        VMemoryNode* p = pNode;
        pNode          = pNode->pNext;
        delete (p);
    }
    pMemoryHeader = nullptr;
}

// #if !X3D_VK_USE_VMA
VkResult vks::KVulkanDevice::AllocateMemory(
    VkDevice                     device,
    const VkMemoryAllocateInfo*  pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    KVkDeviceMemory*             pMemory,
    uint32_t*                    offset,
    uint32_t                     alignSize
)
{
    PROF_CPU();
    //ASSERT(IsMainThread());


    ASSERT(*pMemory == nullptr);


    VkResult hResult  = VK_INCOMPLETE;
    VkResult hRetCode = VK_INCOMPLETE;


    KGLOG_PROCESS_ERROR(alignSize);


    // #if X3D_VK_USE_DEFAULT_ALLOCATOR
    if (DrvOption::bX3D_VK_USE_DEFAULT_ALLOCATOR)
    {
        hRetCode = vks::vkAllocateMemory(device, pAllocateInfo, nullptr, pMemory);
    }
    // #endif

    // #if X3D_VK_USE_CUSTOM_ALLOCATOR
    else if (DrvOption::bX3D_VK_USE_CUSTOM_ALLOCATOR)
    {
        std::lock_guard<std::mutex> lock(m_MemoryContainerLock);
        // base on the 1MB * times cluster linked list
        uint32_t                    MB = (uint32_t)(pAllocateInfo->allocationSize / (BASE_VMEM_NODE_MB * 1024 * 1024) * BASE_VMEM_NODE_MB + BASE_VMEM_NODE_MB);

        // type and one cluster capacity to be a key, to a cluster linklist
        uint32_t key = MB + pAllocateInfo->sType * 1000 + alignSize * 10000 + pAllocateInfo->memoryTypeIndex * 100 + GetThreadId() * 256;
        auto     it  = m_mapMemmory.find(key);

        // if the cluster linklist is created
        if (it != m_mapMemmory.end())
        {
            VMemory*     pMem      = it->second;
            VMemoryNode* pNode     = pMem->pMemoryHeader;
            uint32_t     needCount = (uint32_t)(pAllocateInfo->allocationSize / alignSize + (pAllocateInfo->allocationSize % alignSize > 0 ? 1 : 0));
            uint32_t     findIndex = -1;
            VMemoryNode* pLastNode = pNode;
            while (pNode)
            {
                BOOL bFind = false;

                if (pNode->uInUseTableCount - pNode->uUsedCount >= needCount)
                {
                    UseNode* pUseNode = pNode->pUseNode;
                    while (pUseNode)
                    {
                        if (!pUseNode->bUse && pUseNode->count >= needCount)
                        {
                            if (pUseNode->count > needCount)
                            {
                                // one usenode split to two node
                                UseNode* pNewNode    = new UseNode;
                                pNewNode->bUse       = false;
                                pNewNode->count      = pUseNode->count - needCount;
                                pNewNode->startIndex = pUseNode->startIndex + needCount;

                                pUseNode->bUse  = true;
                                pNewNode->pNext = pUseNode->pNext;
                                pUseNode->pNext = pNewNode;
                                pUseNode->count = needCount;
                                pUseNode->bUse  = true;
                            }
                            else
                            {
                                // rest range exactly as the same as new range need, so needn't split
                                pUseNode->bUse = true;
                            }
                            findIndex = pUseNode->startIndex;
                            bFind     = true;
                            break;
                        }
                        pUseNode = pUseNode->pNext;
                    }
                }
                pLastNode = pNode;
                if (bFind)
                {
                    break;
                }
                pNode = pNode->pNext;
            }

            if (pNode)
            {
                pNode->uUsedCount += needCount;
                *pMemory           = pNode->pMemory;
                *offset            = findIndex * alignSize;
                // KGLogPrintf(KGLOG_INFO, "alloc ... %d", findIndex);
            }
            else
            {
                // this cluster has no effect space, so create next memory node
                size_t       nodeSize   = MB * 1024 * 1024;
                VMemoryNode* pNode      = new VMemoryNode(1);
                pNode->perNodeSize      = nodeSize;
                pNode->alignmentSize    = alignSize;
                pNode->uInUseTableCount = (uint32_t)(nodeSize / alignSize);

                uint32_t needCount = (uint32_t)(pAllocateInfo->allocationSize / alignSize + (pAllocateInfo->allocationSize % alignSize > 0 ? 1 : 0));

                pNode->uUsedCount = needCount;

                //----------------
                UseNode* pUseNode    = new UseNode;
                pUseNode->bUse       = true;
                pUseNode->startIndex = 0;
                pUseNode->count      = needCount;

                UseNode* pNextUseNode    = new UseNode;
                pNextUseNode->bUse       = false;
                pNextUseNode->startIndex = needCount;
                pNextUseNode->count      = pNode->uInUseTableCount - needCount;

                // split two node, one for used one for free;
                pUseNode->pNext = pNextUseNode;
                pNode->pUseNode = pUseNode;
                //---------------------

                VkMemoryAllocateInfo info = *pAllocateInfo;
                info.allocationSize       = nodeSize;
                hRetCode                  = vks::vkAllocateMemory(device, &info, nullptr, pMemory);
                vmAllocCounter++;
                if (hRetCode == VK_SUCCESS)
                {
                    pNode->pMemory               = *pMemory;
                    // ASSERT(m_mapMemoryMapping.find(*pMemory) == m_mapMemoryMapping.end());
                    // m_mapMemoryMapping.insert(std::make_pair<>(*pMemory, pMem));
                    m_mapMemoryMapping[*pMemory] = pMem;
                }
                else
                {
                    ASSERT(0);
                    SAFE_DELETE(pNode);
                    SAFE_DELETE(pMem);
                    goto Exit0;
                }

                *pMemory = pNode->pMemory;
                *offset  = 0;

                if (pLastNode)
                {
                    pLastNode->pNext = pNode;
                    // KGLogPrintf(KGLOG_INFO, "alloc ... %d", 0);
                }
                pNode->pPreviouse = pLastNode;
            }
        }
        else
        {
            // create a new cluster linklist
            uint64_t     nodeSize   = (uint64_t)MB * 1024 * 1024;
            VMemory*     pMem       = new VMemory;
            VMemoryNode* pNode      = new VMemoryNode(0);
            pMem->pMemoryHeader     = pNode;
            pNode->uInUseTableCount = (uint32_t)(nodeSize / alignSize);
            pNode->perNodeSize      = nodeSize;
            pNode->alignmentSize    = alignSize;

            uint32_t needCount = (uint32_t)(pAllocateInfo->allocationSize / alignSize + (pAllocateInfo->allocationSize % alignSize > 0 ? 1 : 0));

            pNode->uUsedCount = needCount;

            //----------------
            UseNode* pUseNode    = new UseNode;
            pUseNode->bUse       = true;
            pUseNode->startIndex = 0;
            pUseNode->count      = needCount;

            UseNode* pNextUseNode    = new UseNode;
            pNextUseNode->bUse       = false;
            pNextUseNode->startIndex = needCount;
            pNextUseNode->count      = pNode->uInUseTableCount - needCount;

            // split two node, one for used one for free;
            pUseNode->pNext = pNextUseNode;
            pNode->pUseNode = pUseNode;
            //----------------

            VkMemoryAllocateInfo info = *pAllocateInfo;
            info.allocationSize       = nodeSize;
            hRetCode                  = vks::vkAllocateMemory(device, &info, nullptr, pMemory);
            vmAllocCounter++;
            if (hRetCode == VK_SUCCESS)
            {
                pNode->pMemory = *pMemory;
                ASSERT(m_mapMemmory.find(key) == m_mapMemmory.end());
                m_mapMemmory.insert(std::make_pair<>(key, pMem));
                m_mapMemoryMapping.insert(std::make_pair<>(*pMemory, pMem));
            }
            else
            {
                // SAFE_DELETE(pNode);
                SAFE_DELETE(pMem);
                goto Exit0;
            }

            *pMemory = pNode->pMemory;
            *offset  = 0;
            // LOGI("alloc ... %d", 0);
        }

        hRetCode = VK_SUCCESS;
    }
    // #endif
Exit0:
    return hRetCode;
}

void vks::KVulkanDevice::FreeMemory(
    VkDevice                     device,
    KVkDeviceMemory              pMemory,
    const VkAllocationCallbacks* pAllocator,
    uint32_t                     uOffset,
    uint32_t                     uRange
)
{
    PROF_CPU();
    //ASSERT(IsMainThread());

    // #if X3D_VK_USE_DEFAULT_ALLOCATOR
    if (DrvOption::bX3D_VK_USE_DEFAULT_ALLOCATOR)
    {
        vks::vkFreeMemory(device, pMemory, pAllocator);
    }
    // #endif

    // #if X3D_VK_USE_CUSTOM_ALLOCATOR
    else if (DrvOption::bX3D_VK_USE_CUSTOM_ALLOCATOR)
    {
        std::lock_guard<std::mutex> lock(m_MemoryContainerLock);
        // ASSERT(IsMainThread());
        auto                        it = m_mapMemoryMapping.find(pMemory);
        if (it != m_mapMemoryMapping.end())
        {
            VMemory*     pMem  = it->second;
            VMemoryNode* pNode = pMem->pMemoryHeader;
            while (pNode)
            {
                if (pNode->pMemory == pMemory)
                {
                    break;
                }
                pNode = pNode->pNext;
            }

            if (pNode)
            {
                uint32_t index     = uOffset / pNode->alignmentSize;
                uint32_t needCount = uRange / pNode->alignmentSize + (uRange % pNode->alignmentSize > 0 ? 1 : 0);

                //-------------------------------------
                UseNode* pUseNode         = pNode->pUseNode;
                UseNode* pPreviousUseNode = nullptr;
                BOOL     bFreed           = false;
                while (pUseNode)
                {
                    UseNode* pNextUseNode = pUseNode->pNext;
                    UseNode* pCurUseNode  = pUseNode;

                    if (pUseNode->startIndex == index)
                    {
                        pCurUseNode->bUse = false;
                        // combine previous and next unused node to one node
                        if (pPreviousUseNode && pPreviousUseNode->bUse == false)
                        {
                            pPreviousUseNode->count += pUseNode->count;
                            pPreviousUseNode->pNext  = pNextUseNode;
                            delete (pCurUseNode);
                            pCurUseNode = pPreviousUseNode;
                        }

                        if (pNextUseNode && pNextUseNode->bUse == false)
                        {
                            pCurUseNode->count += pNextUseNode->count;
                            if (pNextUseNode->pNext)
                            {
                                pCurUseNode->pNext = pNextUseNode->pNext;
                            }
                            else
                            {
                                pCurUseNode->pNext = nullptr;
                            }
                            delete (pNextUseNode);
                        }
                        pNode->uUsedCount -= needCount;
                        // KGLogPrintf(KGLOG_INFO, "release ...%d", index);
                        bFreed             = true;
                        break;
                    }
                    pPreviousUseNode = pUseNode;
                    pUseNode         = pUseNode->pNext;
                }
                // if (!bFreed)
                //{
                //	int x = 0;
                // }

                //-------------------------------------

                if (pNode->uUsedCount == 0)
                {
                    // is head and tail
                    if (pNode == pMem->pMemoryHeader && !pNode->pNext)
                    {
                        pMem->pMemoryHeader = nullptr;
                    }
                    // is head
                    else if (pNode->pPreviouse == nullptr)
                    {
                        if (pNode->pNext)
                        {
                            pMem->pMemoryHeader      = pNode->pNext;
                            pNode->pNext->pPreviouse = nullptr;
                        }
                        else
                        {
                            pMem->pMemoryHeader = nullptr;
                        }
                    }
                    // is tail
                    else if (!pNode->pNext)
                    {
                        pNode->pPreviouse->pNext = nullptr;
                    }
                    else
                    {
                        pNode->pPreviouse->pNext = pNode->pNext;
                        pNode->pNext->pPreviouse = pNode->pPreviouse;
                    }
                    SAFE_DELETE(pNode);

                    m_mapMemoryMapping.erase(pMemory);

                    if (!pMem->pMemoryHeader)
                    {
                        // one hole cluster list is empty, free one
                        for (auto itt = m_mapMemmory.begin(), e = m_mapMemmory.end(); itt != e; ++itt)
                        {
                            if (itt->second == pMem)
                            {
                                m_mapMemmory.erase(itt);
                                break;
                            }
                        }
                        SAFE_DELETE(pMem);
                    }
                }
            }
            else
            {
                KGLogPrintf(KGLOG_ERR, "free vmemory error");
            }
        }
    }
    // #endif
}

void vks::KVulkanDevice::DumpMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc)
{
    if (outputFunc)
    {
        std::string strHead    = "Key\tSumSize(Byte)\tUsedSize(Byte)\tRemainSize(Byte)\tAlignSize(Byte)\n";
        std::string strSubHead = "StartIndex\tUsedSize(Byte)\n";
        for (auto pair : m_mapMemmory)
        {
            std::string  key, sumSize, usedSize, remainSize, alignSize;
            VMemoryNode* pNode = pair.second->pMemoryHeader;
            while (pNode)
            {
                outputFunc(strHead.c_str(), (unsigned int)strHead.size());
                UseNode* pUsed = pNode->pUseNode;
                key            = std::to_string(pair.first);
                sumSize        = std::to_string(pNode->perNodeSize);
                usedSize       = std::to_string(pNode->uUsedCount * pNode->alignmentSize);
                remainSize     = std::to_string((pNode->uInUseTableCount - pNode->uUsedCount) * pNode->alignmentSize);
                alignSize      = std::to_string(pNode->alignmentSize);

                outputFunc(key.c_str(), (unsigned int)key.size());
                outputFunc("\t", 1);
                outputFunc(sumSize.c_str(), (unsigned int)sumSize.size());
                outputFunc("\t", 1);
                outputFunc(usedSize.c_str(), (unsigned int)usedSize.size());
                outputFunc("\t", 1);
                outputFunc(remainSize.c_str(), (unsigned int)remainSize.size());
                outputFunc("\t", 1);
                outputFunc(alignSize.c_str(), (unsigned int)alignSize.size());
                outputFunc("\t", 1);
                outputFunc("\n", 1);

                if (nullptr == pUsed)
                    continue;
                outputFunc(strSubHead.c_str(), (unsigned int)strSubHead.size());
                while (pUsed)
                {
                    std::string startIndex, subUsedSize;
                    if (pUsed->bUse)
                    {
                        startIndex  = std::to_string(pUsed->startIndex);
                        subUsedSize = std::to_string(pUsed->count * pNode->alignmentSize);
                        outputFunc(startIndex.c_str(), (unsigned int)startIndex.size());
                        outputFunc("\t", 1);
                        outputFunc(subUsedSize.c_str(), (unsigned int)subUsedSize.size());
                        outputFunc("\t", 1);
                        outputFunc("\n", 1);
                    }
                    pUsed = pUsed->pNext;
                }

                pNode = pNode->pNext;
            }
        }
    }
}

// #endif // #if !X3D_VK_USE_VMA

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
VkCommandPool vks::KVulkanDevice::CreateCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags)
{
    VkResult                hRetCode    = VK_INCOMPLETE;
    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex        = queueFamilyIndex;
    cmdPoolInfo.flags                   = createFlags;
    VkCommandPool cmdPool;

    hRetCode = vks::vkCreateCommandPool(m_pLogicalDevice, &cmdPoolInfo, nullptr, &cmdPool);
    KGLOG_COM_PROCESS_ERROR(hRetCode);

Exit0:
    return cmdPool;
}

void vks::KVulkanDevice::TrimCommandPool()
{
    if (!m_pLogicalDevice)
    {
        return;
    }

    if (m_pGraphicCommandPool)
    {
        vks::vkTrimCommandPool(m_pLogicalDevice, m_pGraphicCommandPool, 0);
    }

    if (m_pComputeCommandPool)
    {
        vks::vkTrimCommandPool(m_pLogicalDevice, m_pComputeCommandPool, 0);
    }

    if (m_pTransferCommandPool)
    {
        vks::vkTrimCommandPool(m_pLogicalDevice, m_pTransferCommandPool, 0);
    }
}

/**
 * Allocate a command buffer from the command pool
 *
 * @param level Level of the new command buffer (primary or secondary)
 * @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer) (Defaults to false)
 *
 * @return A handle to the allocated command buffer
 */
VkCommandBuffer vks::KVulkanDevice::CreateCommandBuffer(VkCommandBufferLevel level, BOOL begin, gfx::enumForProcessType commandType)
{
    VkResult      hRetCode = VK_INCOMPLETE;
    VkCommandPool pool     = nullptr;
    ASSERT(IsMainThread());

    switch (commandType)
    {
    case gfx::FOR_GRPAHIC:
        pool = m_pGraphicCommandPool;
        break;
    case gfx::FOR_COMPUTE:
        pool = m_pComputeCommandPool;
        break;
    case gfx::FOR_TRANSFER:
        pool = m_pTransferCommandPool;
        break;
    default:
        break;
    }

    KX3DEngineMonitor*          pPerfMonitor       = NSEngine::GetEngineMonitor();
    VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::CommandBufferAllocateInfo(pool, level, 1);

    VkCommandBuffer cmdBuffer;
    hRetCode = vks::vkAllocateCommandBuffers(m_pLogicalDevice, &cmdBufAllocateInfo, &cmdBuffer);
    KGLOG_COM_PROCESS_ERROR(hRetCode);

    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
        ++pPerfMonitor->m_sGraphics.nVkPrimaryCommandBufferCount;
    }
    else
    {
        ++pPerfMonitor->m_sGraphics.nVkSecondaryCommandBufferCount;
    }

    // If requested, also start recording for the new command buffer
    if (begin)
    {
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();
        hRetCode                            = vks::vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo);
        KGLOG_COM_PROCESS_ERROR(hRetCode);
    }

Exit0:
    return cmdBuffer;
}

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
BOOL vks::KVulkanDevice::FlushCommandBuffer(VkCommandBuffer pCommandBuffer, gfx::enumForProcessType processType, BOOL free, BOOL bEndCommand)
{
    BOOL     bResult       = false;
    VkResult vkRetCode     = VK_INCOMPLETE;
    VkDevice logicalDevice = GetVkDevice();
    VkQueue  pQueue        = GetQueue(processType);

    KGLOG_PROCESS_ERROR(pCommandBuffer != VK_NULL_HANDLE);

    if (bEndCommand)
    {
        vkRetCode = vks::vkEndCommandBuffer(pCommandBuffer);
        KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);
    }

    {
#if 0
		VkSubmitInfo submitInfo = vks::initializers::SubmitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &pCommandBuffer;

		hRetCode = vks::vkQueueSubmit(pQueue, 1, &submitInfo, VK_NULL_HANDLE);
		KGLOG_COM_PROCESS_ERROR(hRetCode);

		hRetCode = vks::vkQueueWaitIdle(pQueue);
		KGLOG_COM_PROCESS_ERROR(hRetCode);

#else
        VkSubmitInfo submitInfo       = vks::initializers::SubmitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &pCommandBuffer;
        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceInfo   = vks::initializers::FenceCreateInfo(VK_FLAGS_NONE);
        VkFence           fence;
        vkRetCode = vks::vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence);
        KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);

        GetVulkanDevice()->SetObjectLabel(fence, "FlushCommandFence");

        // Submit to the queue
        vkRetCode = vks::vkQueueSubmit(pQueue, 1, &submitInfo, fence);
        if (vkRetCode == VK_ERROR_DEVICE_LOST)
        {
            KGLogPrintf(KGLOG_ERR, "完蛋，GPU设备移除了");
#ifdef _WIN32
            MessageBox(NULL, "GPU设备移除了", "错误", MB_OK);
#endif
            RaiseDumpException();
        }
        KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

        // Wait for the fence to signal that command buffer has finished executing
        vkRetCode = vks::vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);
        if (vkRetCode == VK_ERROR_DEVICE_LOST)
        {
            KGLogPrintf(KGLOG_ERR, "完蛋，GPU设备移除了");
#ifdef _WIN32
            MessageBox(NULL, "GPU设备移除了", "错误", MB_OK);
#endif
            RaiseDumpException();
        }
        KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

        vks::vkDestroyFence(logicalDevice, fence, nullptr);
#endif
    }

    if (free)
    {
        // vkFreeCommandBuffers(m_pLogicalDevice, m_pCommandPool, 1, &pCommandBuffer);
        FreeCommandBuffer(pCommandBuffer, processType);
    }

    bResult = true;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::FreeCommandBuffer(VkCommandBuffer commandBuffer, gfx::enumForProcessType processType, VkCommandBufferLevel level)
{
    VkCommandPool pool = nullptr;
    switch (processType)
    {
    case gfx::FOR_GRPAHIC:
        pool = m_pGraphicCommandPool;
        break;
    case gfx::FOR_COMPUTE:
        pool = m_pComputeCommandPool;
        break;
    case gfx::FOR_TRANSFER:
        pool = m_pTransferCommandPool;
        break;
    default:
        break;
    }

    vks::vkFreeCommandBuffers(m_pLogicalDevice, pool, 1, &commandBuffer);

    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
        --pPerfMonitor->m_sGraphics.nVkPrimaryCommandBufferCount;
    }
    else
    {
        --pPerfMonitor->m_sGraphics.nVkSecondaryCommandBufferCount;
    }

    return true;
}

BOOL vks::KVulkanDevice::FreeCommandBuffer(VkCommandBuffer commandBuffer, VkCommandPool vkCmdPool, VkCommandBufferLevel level)
{
    vks::vkFreeCommandBuffers(m_pLogicalDevice, vkCmdPool, 1, &commandBuffer);

    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
        --pPerfMonitor->m_sGraphics.nVkPrimaryCommandBufferCount;
    }
    else
    {
        --pPerfMonitor->m_sGraphics.nVkSecondaryCommandBufferCount;
    }

    return true;
}

/**
 * Check if an extension is supported by the (physical device)
 *
 * @param extension Name of the extension to check
 *
 * @return True if the extension is supported (present in the list read at device creation time)
 */
BOOL vks::KVulkanDevice::ExtensionSupported(std::string extension)
{
    return (std::find(m_SupportedExtensions.begin(), m_SupportedExtensions.end(), extension) != m_SupportedExtensions.end());
}


void vks::KVulkanDevice::CheckInstanceLayerValidation()
{
    // #ifdef _WIN32
    // const char* validationLayerNames[] = {
    //	"VK_LAYER_LUNARG_standard_validation",
    //	"VK_LAYER_KHRONOS_validation",
    //	"VK_LAYER_NV_optimus",
    //	"VK_LAYER_GOOGLE_threading",
    //	"VK_LAYER_LUNARG_parameter_validation",
    //	"VK_LAYER_LUNARG_object_tracker",
    //	"VK_LAYER_LUNARG_image",
    //	"VK_LAYER_LUNARG_core_validation",
    //	"VK_LAYER_LUNARG_swapchain",
    //	"VK_LAYER_GOOGLE_unique_objects",
    //	"VK_LAYER_AMD_switchable_graphics",
    // #if ENABLE_RENDERDOC_DEBUG_MARKER
    //	"VK_LAYER_RENDERDOC_Capture"
    // #endif
    // };
    // #elif defined(__ANDROID__)
    // const char* validationLayerNames[] = {
    //	//"VK_LAYER_GOOGLE_threading",
    //	"VK_LAYER_LUNARG_parameter_validation",
    //	"VK_LAYER_LUNARG_object_tracker",
    //	"VK_LAYER_LUNARG_core_validation",
    //	"VK_LAYER_LUNARG_image",
    //	"VK_LAYER_LUNARG_swapchain",
    //	"VK_LAYER_GOOGLE_unique_objects"
    // #if ENABLE_RENDERDOC_DEBUG_MARKER
    //	"VK_LAYER_RENDERDOC_Capture"
    // #endif
    // };
    // #else
    // const char* validationLayerNames[] = {
    //	"VK_LAYER_LUNARG_standard_validationDrvOption::bVKValidateEnable
    // #if ENABLE_RENDERDOC_DEBUG_MARKER
    //	"VK_LAYER_RENDERDOC_Capture"
    // #endif
    // };
    // #endif

#ifdef _WIN32
    if (DrvOption::bVKValidateEnable)
    {
        validationLayerNames.push_back("VK_LAYER_LUNARG_standard_validation");
        validationLayerNames.push_back("VK_LAYER_KHRONOS_validation");
        validationLayerNames.push_back("VK_LAYER_KHRONOS_synchronization2");
        KGLogPrintf(KGLOG_INFO, "vulkan验证层开启");
    }
    validationLayerNames.push_back("VK_LAYER_NV_optimus");
    validationLayerNames.push_back("VK_LAYER_GOOGLE_threading");
    validationLayerNames.push_back("VK_LAYER_LUNARG_parameter_validation");
    validationLayerNames.push_back("VK_LAYER_LUNARG_object_tracker");
    validationLayerNames.push_back("VK_LAYER_LUNARG_image");
    validationLayerNames.push_back("VK_LAYER_LUNARG_core_validation");
    validationLayerNames.push_back("VK_LAYER_LUNARG_swapchain");
    validationLayerNames.push_back("VK_LAYER_GOOGLE_unique_objects");
    validationLayerNames.push_back("VK_LAYER_AMD_switchable_graphics");
#elif defined(__ANDROID__) || defined(__APPLE__)
    if (DrvOption::bVKValidateEnable)
    {
        validationLayerNames.push_back("VK_LAYER_LUNARG_standard_validation");
        validationLayerNames.push_back("VK_LAYER_KHRONOS_validation");
        validationLayerNames.push_back("VK_LAYER_LUNARG_core_validation");
        validationLayerNames.push_back("VK_LAYER_LUNARG_parameter_validation");
        validationLayerNames.push_back("VK_LAYER_LUNARG_object_tracker");
        KGLogPrintf(KGLOG_INFO, "vulkan验证层开启");
    }
    validationLayerNames.push_back("VK_LAYER_LUNARG_image");
    validationLayerNames.push_back("VK_LAYER_LUNARG_swapchain");
    validationLayerNames.push_back("VK_LAYER_GOOGLE_unique_objects");
#else
    validationLayerNames.push_back("VK_LAYER_LUNARG_standard_validation");
#endif

    if (DrvOption::bVKRenderDocDebugMarkerON)
    {
        validationLayerNames.push_back("VK_LAYER_RENDERDOC_Capture");
    }


    // Determine the number of instance layers that Vulkan reports
    uint32_t numInstanceLayers = 0;
    vks::vkEnumerateInstanceLayerProperties(&numInstanceLayers, nullptr);

    // Enumerate instance layers with valid pointer in last parameter
    VkLayerProperties* layerProperties = (VkLayerProperties*)malloc(numInstanceLayers * sizeof(VkLayerProperties));
    vks::vkEnumerateInstanceLayerProperties(&numInstanceLayers, layerProperties);

    for (uint32_t n = 0; n < numInstanceLayers; ++n)
    {
        KGLogPrintf(KGLOG_INFO, "valid layer: %s", layerProperties[n].layerName);
        //	m_InstanceLayerExtensions.push_back(layerProperties[n].layerName);
    }

    // Make sure the desired instance validation layers are available
    // NOTE:  These are not listed in an arbitrary order.  Threading must be
    //        first, and unique_objects must be last.  This is the order they
    //        will be inserted by the loader.
    uint32_t mEnabledInstanceLayerCount = (uint32_t)validationLayerNames.size(); // sizeof(validationLayerNames) / sizeof(validationLayerNames[0]);
    for (uint32_t i = 0; i < mEnabledInstanceLayerCount; i++)
    {
        // uint32_t nCount = 0;
        // VkExtensionProperties p;
        // vks::vkEnumerateInstanceExtensionProperties(validationLayerNames[i], &nCount, &p);

        // if (nCount)
        //{
        //	int x = 0;
        // }

        bool found = false;
        for (uint32_t j = 0; j < numInstanceLayers; j++)
        {
            if (strcmp(validationLayerNames[i], layerProperties[j].layerName) == 0)
            {
                found = true;
            }
        }
        if (found)
        {
            m_InstanceLayerExtensions.push_back(validationLayerNames[i]);
            KGLogPrintf(KGLOG_INFO, "Instance Layer Supported: %s", validationLayerNames[i]);
        }
        else
        {
            // KGLogPrintf(KGLOG_ERR, "Instance Layer not found: %s", validationLayerNames[i]);
        }
    }

    SAFE_FREE(layerProperties);
}


BOOL vks::KVulkanDevice::_CreateInstance(const gfx::RenderSystemInfo& renderSysteInfo)
{
    BOOL     bResult  = FALSE;
    VkResult vkResult = VkResult::VK_RESULT_MAX_ENUM;
    // Physical device

    CheckInstanceLayerValidation();

    VkApplicationInfo appInfo  = {};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext              = nullptr;
    appInfo.pApplicationName   = m_szAppName;
    appInfo.pEngineName        = m_szAppName;
    appInfo.applicationVersion = 1;
    appInfo.engineVersion      = 1;
    appInfo.apiVersion = VK_API_VERSION_1_1;
#if USE_VK_1_4
#ifdef VK_API_VERSION_1_4
    appInfo.apiVersion = VK_API_VERSION_1_4;
#endif // DEBUG
#endif
    std::vector<std::string> supportedInstanceExtensionsName;

    std::vector<const char*> m_InstanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME};

    // Enable surface extensions depending on os
#if defined(_WIN32)
    m_InstanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    m_InstanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
    m_InstanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_OHOS)
    m_InstanceExtensions.push_back(VK_OHOS_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
    m_InstanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    m_InstanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    m_InstanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
    m_InstanceExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
    m_Settings.m_bValidation = false; // ios does not surport validation
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    m_InstanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif

    m_InstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    // m_InstanceExtensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
    // m_InstanceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    // m_InstanceExtensions.push_back("VK_EXT_debug_marker");

    // vks::vkEnumerateInstanceLayerProperties
    // #ifdef __ANDROID__
    //	{
    //		char sdk[128] = "0";
    //		__system_property_get("ro.build.version.sdk", sdk);
    //
    //		char model[128] = "0";
    //		__system_property_get("ro.product.model",model);
    //
    //		char gpu[128] = "0";
    //		__system_property_get("ro.gpu",gpu);
    //
    //		int x =0;
    //	}
    // #endif
    //

    const void*                  pFeatures = nullptr;
    std::vector<VkValidationFeatureEnableEXT> enableFeature =
        {
            // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
            // VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
        };

    if (DrvOption::bVKValidateEnable && DrvOption::bEnableVKDebugPrintf)
    {
        enableFeature.push_back(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);
    }

    VkValidationFeaturesEXT validationFeatures       = {};
    validationFeatures.sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validationFeatures.enabledValidationFeatureCount = (uint32_t)enableFeature.size();
    validationFeatures.pEnabledValidationFeatures    = enableFeature.data();

    if (DrvOption::bVKValidateEnable && DrvOption::bEnableVKValidateFeature)
    {
        pFeatures = &validationFeatures;
    }

    // Check Extension
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    if (extCount > 0)
    {
        std::vector<VkExtensionProperties> extensions(extCount);
        if (vks::vkEnumerateInstanceExtensionProperties(nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
        {
            for (VkExtensionProperties extension : extensions)
            {
                supportedInstanceExtensionsName.push_back(extension.extensionName);
                KGLogPrintf(KGLOG_INFO, "support extensions: %s", extension.extensionName);
            }
        }
    }

    // 对需要的extension进行筛选
    for (const char* enabledExtension : m_InstanceExtensions)
    {
        if (std::find(supportedInstanceExtensionsName.begin(), supportedInstanceExtensionsName.end(), enabledExtension) == supportedInstanceExtensionsName.end())
        {
            KGLogPrintf(KGLOG_INFO, "not support enabled extensions: %s", enabledExtension);
            if (strcmp(enabledExtension, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME) == 0)
            {
                DrvOption::bEnableAndroidCamera = FALSE;
            }
        }
        else
        {
            m_vecInstanceSupportExtensions.push_back(enabledExtension);
        }
    }


    if (DrvOption::bVKValidateEnable)
    {
        // 先检查VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        if (std::find(supportedInstanceExtensionsName.begin(), supportedInstanceExtensionsName.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == supportedInstanceExtensionsName.end())
        {
            KGLogPrintf(KGLOG_INFO, "not support enabled extensions: %s", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            if (std::find(supportedInstanceExtensionsName.begin(), supportedInstanceExtensionsName.end(), VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == supportedInstanceExtensionsName.end())
            {
                KGLogPrintf(KGLOG_INFO, "not support enabled extensions: %s", VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            }
            else
            {
                m_vecInstanceSupportExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            }
            // 开启的是VK_API_VERSION_1_0， 所以1.1设备查询不支持，但实际上是能支持的，可以尝试强制添加,如果低于1.0的设备，那么就不要强行添加了，否则会宕机
            if (DrvOption::bForceEnableDebugUtileExtension)
            {
                m_vecInstanceSupportExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        }
        else
        {
            m_vecInstanceSupportExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pNext                = pFeatures;
    instanceCreateInfo.pApplicationInfo     = &appInfo;

    if (!m_vecInstanceSupportExtensions.empty())
    {
        instanceCreateInfo.enabledExtensionCount   = (uint32_t)m_vecInstanceSupportExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = m_vecInstanceSupportExtensions.data();
    }

    instanceCreateInfo.enabledLayerCount   = (uint32_t)m_InstanceLayerExtensions.size();
    instanceCreateInfo.ppEnabledLayerNames = m_InstanceLayerExtensions.data();
    // for(const char *str : m_InstanceExtensions )
    //{
    //	KGLogPrintf(KGLOG_INFO, "%s", str);
    // }

    if (renderSysteInfo.bCreateInstance)
    {
        KGLogPrintf(KGLOG_INFO, "enabled extensions:");
        for (uint32_t i = 0; i < instanceCreateInfo.enabledExtensionCount; ++i)
        {
            const char* pName = instanceCreateInfo.ppEnabledExtensionNames[i];
            KGLogPrintf(KGLOG_INFO, "%d: %s", i, pName);
        }

        KGLogPrintf(KGLOG_INFO, "enabled layers:");
        for (uint32_t i = 0; i < instanceCreateInfo.enabledLayerCount; ++i)
        {
            const char* pName = instanceCreateInfo.ppEnabledLayerNames[i];
            KGLogPrintf(KGLOG_INFO, "%d: %s", i, pName);
        }

        vkResult = vks::vkCreateInstance(&instanceCreateInfo, nullptr, &m_pInstance);

        if (vkResult != VK_SUCCESS)
        {
            KGLogPrintf(KGLOG_INFO, "Create vulkan1.1 device failed, recreate to 1.0");
            appInfo.apiVersion              = VK_API_VERSION_1_0;
            DrvOption::uVulkanVersion       = VK_API_VERSION_1_0;
            vkResult                        = vks::vkCreateInstance(&instanceCreateInfo, nullptr, &m_pInstance);
            DrvOption::bEnableAndroidCamera = FALSE;
        }

        KGLOG_COM_PROCESS_ERROR(vkResult);
        LoadVulkanInstanceFunctions(m_pInstance);
    }
    else
    {
        m_pInstance = GetLoadedInstance();
    }

    bResult = true;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::_EnumPhysicalDeviceProperties(gfx::KGFX_PHYSICAL_DEVICE_LIMITS& physicallimits)
{
    BOOL                          result           = false;
    VkResult                      hRetCode         = VK_INCOMPLETE;
    BOOL                          bRetCode         = false;
    uint32_t                      gpuCount         = 0;
    // 默认选第第一张显卡
    uint32_t                      selectedDevice   = 0;
    uint32_t                      queueFamilyCount = 0;
    std::string                   drivername;
    std::vector<VkPhysicalDevice> physicalDevices;
    KEngineSwitchOption*          pEngineSwitchOptions = NSEngine::GetEngineSwitchOptions();

    KGLOG_ASSERT_EXIT(pEngineSwitchOptions);
    KGLOG_PROCESS_ERROR(m_pInstance);

    // Get number of available physical devices
    hRetCode = vks::vkEnumeratePhysicalDevices(m_pInstance, &gpuCount, nullptr);
    KGLOG_COM_PROCESS_ERROR(hRetCode);
    KGLOG_PROCESS_ERROR(gpuCount);
    // Enumerate devices
    physicalDevices.resize(gpuCount);
    hRetCode = vks::vkEnumeratePhysicalDevices(m_pInstance, &gpuCount, physicalDevices.data());
    if (hRetCode != VK_SUCCESS)
    {
        KGLogPrintf(KGLOG_ERR, "Could not enumerate physical devices");
    }
    // KGLOG_COM_PROCESS_ERROR(hRetCode && "Could not enumerate physical devices");
    KGLOG_COM_PROCESS_ERROR(hRetCode);


    if (gpuCount > 1)
    {
        // 优先选N卡
        BOOL bNvidia = false;
#ifdef _WIN32
        for (uint32_t i = 0; i < gpuCount; ++i)
        {
            VkPhysicalDeviceProperties properties;
            vks::vkGetPhysicalDeviceProperties(physicalDevices[i], &properties);
            if (KSTR_HELPER::strstri(properties.deviceName, "nvidia"))
            {
                bNvidia        = true;
                selectedDevice = i;
                break;
            }
        }
#endif
        if (!bNvidia)
        {
            // 如果没有N卡，那么查找第一个独显
            for (uint32_t i = 0; i < gpuCount; ++i)
            {
                VkPhysicalDeviceProperties properties;
                vks::vkGetPhysicalDeviceProperties(physicalDevices[i], &properties);
                if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    selectedDevice = i;
                    break;
                }
            }
        }
    }

    m_pPhysicalDevice = physicalDevices[selectedDevice];
    KGLOG_PROCESS_ERROR(m_pPhysicalDevice);

    // Derived examples can override this to set actual features (based on above readings) to enable for logical device creation
    GetEnabledFeatures();

    {
        // Memory properties are used regularly for creating all kinds of buffers

        vks::vkGetPhysicalDeviceMemoryProperties(m_pPhysicalDevice, &m_MemoryProperties);

        // 打印显存总量和最大分配大小
        // for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i) {
        //	std::cout << "Heap " << i << ":" << std::endl;
        //	std::cout << "  Heap size: " << memoryProperties.memoryHeaps[i].size << " bytes" << std::endl;
        //	std::cout << "  Heap flags: " << memoryProperties.memoryHeaps[i].flags << std::endl;
        //}
        float fGB             = 0.0f;
        bool  bLocalGpuMemory = false;
        for (uint32_t i = 0; i < m_MemoryProperties.memoryHeapCount && i < VK_MAX_MEMORY_HEAPS; ++i)
        {
            if (m_MemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                fGB             += (float)((double)m_MemoryProperties.memoryHeaps[i].size / (double)(1024 * 1024 * 1024));
                bLocalGpuMemory  = true;
            }
        }

        if (bLocalGpuMemory
        {
            DrvOption::fLocalGpuMemoryGB = fGB;
        }

        // for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        //	if (memoryProperties.memoryTypes[i].heapIndex < memoryProperties.memoryHeapCount) {
        //		std::cout << "Type " << i << ":" << std::endl;
        //		std::cout << "  Heap index: " << memoryProperties.memoryTypes[i].heapIndex << std::endl;
        //		std::cout << "  Property flags: " << memoryProperties.memoryTypes[i].propertyFlags << std::endl;
        //	}
        // }
        KGLogPrintf(KGLOG_INFO, "显存大小:%.2f", DrvOption::fLocalGpuMemoryGB);

        for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount; i++)
        {
            if ((m_MemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            {
                KGLogPrintf(KGLOG_INFO, "Device support VK_MEMORY_PROPERTY_HOST_COHERENT_BIT");
                break;
            }
        }

        for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i)
        {
            if (m_MemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            {
                KGLogPrintf(KGLOG_INFO, "Device support VK_MEMORY_PROPERTY_HOST_CACHED_BIT");
                break;
            }
        }

        for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i)
        {
            if ((m_MemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
                (m_MemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
            {
                KGLogPrintf(KGLOG_INFO, "Device Support VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT");
                DrvOption::bSupportHostCoherentCached = true;
                break;
            }
        }
    }

    // Store Properties features, limits and properties of the physical device for later use
    // Device properties also contain limits and sparse properties
    vks::vkGetPhysicalDeviceProperties(m_pPhysicalDevice, &m_Properties);
    if (m_Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        DrvOption::bDiscreteGpu = true;
        KGLogPrintf(KGLOG_INFO, "独显，驱动GPU:%s", m_Properties.deviceName);
    }
    else
    {
        DrvOption::bDiscreteGpu = false;
        KGLogPrintf(KGLOG_INFO, "非独显，驱动GPU:%s", m_Properties.deviceName);
    }

#ifdef _WIN32
    ASSERT(NSKBase::g_X3DSystem);
    if (NSKBase::g_X3DSystem)
    {
        NSKBase::g_X3DSystem->Win_SetIsIntegratedGPU(!DrvOption::bDiscreteGpu);
        NSKBase::g_X3DSystem->Win_SetGPUMemoryGB(DrvOption::fLocalGpuMemoryGB);
    }
#endif

    DrvOption::vendorId      = m_Properties.vendorID;
    DrvOption::deviceId      = m_Properties.deviceID;
    DrvOption::driverVersion = m_Properties.driverVersion;

    KGLogPrintf(KGLOG_INFO, "系统:%s", DrvOption::szProductor);
    if (m_Properties.deviceName[0])
    {
        drivername = m_Properties.deviceName;
        std::transform(drivername.begin(), drivername.end(), drivername.begin(), ::tolower);

        if (NSKBase::g_X3DSystem)
        {
            NSKBase::g_X3DSystem->SetGPUInfo(drivername.c_str());
        }

        const char* pDeviceName = drivername.c_str();
        uint32_t    ulen        = (uint32_t)strlen(pDeviceName);
        int         deviceNo    = 0;
        for (uint32_t i = 0; i < ulen; ++i)
        {
            if (pDeviceName[i] >= '0' && pDeviceName[i] <= '9')
            {
                deviceNo = atoi(&pDeviceName[i]);
                break;
            }
        }
        DrvOption::nDeviceNumber = deviceNo;

        // setup switchoptionconfig
        {
            KEngineSwitchOption* pSwitchOptions = NSEngine::GetEngineSwitchOptions();
            pSwitchOptions->InitSwitchConfig(m_Properties.deviceName);
        }
        {// setup DeviceConfigFile
            NSEngine::InitDeviceConfigFile(m_Properties.deviceName);
        }

        if (strstr(drivername.c_str(), "mali"))
        {
            DrvOption::bIsMaliGPU = true;
        }
        else if (strstr(drivername.c_str(), "adreno"))
        {
            DrvOption::bIsAdrenoGPU = true;
        }
        else if (strstr(drivername.c_str(), "powervr"))
        {
            DrvOption::bIsPvr = true;
        }
        else if (strstr(drivername.c_str(), "intel"))
        {
            DrvOption::bIsIntelGPU = true;
        }
    }
    switch (m_Properties.deviceType)
    {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        KGLogPrintf(KGLOG_INFO, "Type: DEVICE_TYPE_OTHER");
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        KGLogPrintf(KGLOG_INFO, "Type: DEVICE_TYPE_INTEGRATED_GPU");
        break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        KGLogPrintf(KGLOG_INFO, "Type: DEVICE_TYPE_DISCRETE_GPU");
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        KGLogPrintf(KGLOG_INFO, "Type: DEVICE_TYPE_VIRTUAL_GPU");
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        KGLogPrintf(KGLOG_INFO, "Type: DEVICE_TYPE_CPU");
        break;
    default:
        KGLogPrintf(KGLOG_INFO, "Type:UNKNOWN_DEVICE_TYPE");
        break;
    }
    snprintf(DrvOption::szGPU, 128, "%s", drivername.c_str());
    DrvOption::nGfx_api_version_Part0 = m_Properties.apiVersion >> 22;
    DrvOption::nGfx_api_version_Part1 = m_Properties.apiVersion >> 12 & 0x3ff;
    DrvOption::nGfx_api_version_Part2 = m_Properties.apiVersion & 0xfff;
    KGLogPrintf(KGLOG_INFO, "Vulkan API: %d.%d.%d", DrvOption::nGfx_api_version_Part0, DrvOption::nGfx_api_version_Part1, DrvOption::nGfx_api_version_Part2);

    // snprintf(DrvOption::szVulkanAPI, 16, "%d.%d.%d", m_Properties.apiVersion >> 22, m_Properties.apiVersion >> 12 & 0x3ff, m_Properties.apiVersion & 0xfff);
    if (m_Properties.limits.maxUniformBufferRange > 0 && m_Properties.limits.maxDescriptorSetUniformBuffersDynamic > 8 &&
        m_Properties.limits.maxDescriptorSetUniformBuffersDynamic != 0xFFFFFFFF)
    {
        KGLogPrintf(KGLOG_INFO, "This Device Support Dynamic Uniform Buffer Objects:%d, range:%d", m_Properties.limits.maxDescriptorSetUniformBuffersDynamic, m_Properties.limits.maxUniformBufferRange);
    }
    else
    {
#ifndef _WIN32
        if (!pEngineSwitchOptions->GetEnableForceDynamicUBO())
        {
            DrvOption::bSupportDynamicUBO  = false;
            DrvOption::bSupportDynamicSSBO = false;
        }
#endif
        NSEngine::GetEngineOptions()->SetLowQuality();
        KGLogPrintf(KGLOG_INFO, "This Device not Support Dynamic Uniform Buffer Objects, ForceOpen:%d", pEngineSwitchOptions->GetEnableForceDynamicUBO());
    }

    // Features should be checked by the examples before using them
    vks::vkGetPhysicalDeviceFeatures(m_pPhysicalDevice, &m_Features);

    if (!m_Features.fillModeNonSolid)
    {
        // 主要是mali的gpu基本都不支持，所以想调试mesh，画个线框模式的啥的android 下 mali 的 gpu 还是算了吧
        DrvOption::bSupportFillModeNonSolid = false;
    }

    if (!m_Features.pipelineStatisticsQuery)
    {
        DrvOption::bSupportPipelineStatisticsQuery = false;
    }


    // Queue family properties, used for setting up requested queues upon device creation

    vks::vkGetPhysicalDeviceQueueFamilyProperties(m_pPhysicalDevice, &queueFamilyCount, nullptr);
    KGLOG_PROCESS_ERROR(queueFamilyCount > 0);

    m_QueueFamilyProperties.resize(queueFamilyCount);
    vks::vkGetPhysicalDeviceQueueFamilyProperties(m_pPhysicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());

    physicallimits.maxImageDimension1D                             = m_Properties.limits.maxImageDimension1D;
    physicallimits.maxImageDimension2D                             = m_Properties.limits.maxImageDimension2D;
    physicallimits.maxImageDimension3D                             = m_Properties.limits.maxImageDimension3D;
    physicallimits.maxImageDimensionCube                           = m_Properties.limits.maxImageDimensionCube;
    physicallimits.maxImageArrayLayers                             = m_Properties.limits.maxImageArrayLayers;
    physicallimits.maxTexelBufferElements                          = m_Properties.limits.maxTexelBufferElements;
    physicallimits.maxUniformBufferRange                           = m_Properties.limits.maxUniformBufferRange;
    physicallimits.maxStorageBufferRange                           = m_Properties.limits.maxStorageBufferRange;
    physicallimits.maxPushConstantsSize                            = m_Properties.limits.maxPushConstantsSize;
    physicallimits.maxMemoryAllocationCount                        = m_Properties.limits.maxMemoryAllocationCount;
    physicallimits.maxSamplerAllocationCount                       = m_Properties.limits.maxSamplerAllocationCount;
    physicallimits.bufferImageGranularity                          = m_Properties.limits.bufferImageGranularity;
    physicallimits.sparseAddressSpaceSize                          = m_Properties.limits.sparseAddressSpaceSize;
    physicallimits.maxBoundDescriptorSets                          = m_Properties.limits.maxBoundDescriptorSets;
    physicallimits.maxPerStageDescriptorSamplers                   = m_Properties.limits.maxPerStageDescriptorSamplers;
    physicallimits.maxPerStageDescriptorUniformBuffers             = m_Properties.limits.maxPerStageDescriptorUniformBuffers;
    physicallimits.maxPerStageDescriptorStorageBuffers             = m_Properties.limits.maxPerStageDescriptorStorageBuffers;
    physicallimits.maxPerStageDescriptorSampledImages              = m_Properties.limits.maxPerStageDescriptorSampledImages;
    physicallimits.maxPerStageDescriptorStorageImages              = m_Properties.limits.maxPerStageDescriptorStorageImages;
    physicallimits.maxPerStageDescriptorInputAttachments           = m_Properties.limits.maxPerStageDescriptorInputAttachments;
    physicallimits.maxPerStageResources                            = m_Properties.limits.maxPerStageResources;
    physicallimits.maxDescriptorSetSamplers                        = m_Properties.limits.maxDescriptorSetSamplers;
    physicallimits.maxDescriptorSetUniformBuffers                  = m_Properties.limits.maxDescriptorSetUniformBuffers;
    physicallimits.maxDescriptorSetUniformBuffersDynamic           = m_Properties.limits.maxDescriptorSetUniformBuffersDynamic;
    physicallimits.maxDescriptorSetStorageBuffers                  = m_Properties.limits.maxDescriptorSetStorageBuffers;
    physicallimits.maxDescriptorSetStorageBuffersDynamic           = m_Properties.limits.maxDescriptorSetStorageBuffersDynamic;
    physicallimits.maxDescriptorSetSampledImages                   = m_Properties.limits.maxDescriptorSetSampledImages;
    physicallimits.maxDescriptorSetStorageImages                   = m_Properties.limits.maxDescriptorSetStorageImages;
    physicallimits.maxDescriptorSetInputAttachments                = m_Properties.limits.maxDescriptorSetInputAttachments;
    physicallimits.maxVertexInputAttributes                        = m_Properties.limits.maxVertexInputAttributes;
    physicallimits.maxVertexInputBindings                          = m_Properties.limits.maxVertexInputBindings;
    physicallimits.maxVertexInputAttributeOffset                   = m_Properties.limits.maxVertexInputAttributeOffset;
    physicallimits.maxVertexInputBindingStride                     = m_Properties.limits.maxVertexInputBindingStride;
    physicallimits.maxVertexOutputComponents                       = m_Properties.limits.maxVertexOutputComponents;
    physicallimits.maxTessellationGenerationLevel                  = m_Properties.limits.maxTessellationGenerationLevel;
    physicallimits.maxTessellationPatchSize                        = m_Properties.limits.maxTessellationPatchSize;
    physicallimits.maxTessellationControlPerVertexInputComponents  = m_Properties.limits.maxTessellationControlPerVertexInputComponents;
    physicallimits.maxTessellationControlPerVertexOutputComponents = m_Properties.limits.maxTessellationControlPerVertexOutputComponents;
    physicallimits.maxTessellationControlPerPatchOutputComponents  = m_Properties.limits.maxTessellationControlPerPatchOutputComponents;
    physicallimits.maxTessellationControlTotalOutputComponents     = m_Properties.limits.maxTessellationControlTotalOutputComponents;
    physicallimits.maxTessellationEvaluationInputComponents        = m_Properties.limits.maxTessellationEvaluationInputComponents;
    physicallimits.maxTessellationEvaluationOutputComponents       = m_Properties.limits.maxTessellationEvaluationOutputComponents;
    physicallimits.maxGeometryShaderInvocations                    = m_Properties.limits.maxGeometryShaderInvocations;
    physicallimits.maxGeometryInputComponents                      = m_Properties.limits.maxGeometryInputComponents;
    physicallimits.maxGeometryOutputComponents                     = m_Properties.limits.maxGeometryOutputComponents;
    physicallimits.maxGeometryOutputVertices                       = m_Properties.limits.maxGeometryOutputVertices;
    physicallimits.maxGeometryTotalOutputComponents                = m_Properties.limits.maxGeometryTotalOutputComponents;
    physicallimits.maxFragmentInputComponents                      = m_Properties.limits.maxFragmentInputComponents;
    physicallimits.maxFragmentOutputAttachments                    = m_Properties.limits.maxFragmentOutputAttachments;
    physicallimits.maxFragmentDualSrcAttachments                   = m_Properties.limits.maxFragmentDualSrcAttachments;
    physicallimits.maxFragmentCombinedOutputResources              = m_Properties.limits.maxFragmentCombinedOutputResources;
    physicallimits.maxComputeSharedMemorySize                      = m_Properties.limits.maxComputeSharedMemorySize;
    physicallimits.maxComputeWorkGroupCount[0]                     = m_Properties.limits.maxComputeWorkGroupCount[0];
    physicallimits.maxComputeWorkGroupCount[1]                     = m_Properties.limits.maxComputeWorkGroupCount[1];
    physicallimits.maxComputeWorkGroupCount[2]                     = m_Properties.limits.maxComputeWorkGroupCount[2];
    physicallimits.maxComputeWorkGroupInvocations                  = m_Properties.limits.maxComputeWorkGroupInvocations;
    physicallimits.maxComputeWorkGroupSize[0]                      = m_Properties.limits.maxComputeWorkGroupSize[0];
    physicallimits.maxComputeWorkGroupSize[1]                      = m_Properties.limits.maxComputeWorkGroupSize[1];
    physicallimits.maxComputeWorkGroupSize[2]                      = m_Properties.limits.maxComputeWorkGroupSize[2];
    physicallimits.subPixelPrecisionBits                           = m_Properties.limits.subPixelPrecisionBits;
    physicallimits.subTexelPrecisionBits                           = m_Properties.limits.subTexelPrecisionBits;
    physicallimits.mipmapPrecisionBits                             = m_Properties.limits.mipmapPrecisionBits;
    physicallimits.maxDrawIndexedIndexValue                        = m_Properties.limits.maxDrawIndexedIndexValue;
    physicallimits.maxDrawIndirectCount                            = m_Properties.limits.maxDrawIndirectCount;
    physicallimits.maxSamplerLodBias                               = m_Properties.limits.maxSamplerLodBias;
    physicallimits.maxSamplerAnisotropy                            = m_Properties.limits.maxSamplerAnisotropy;
    physicallimits.maxViewports                                    = m_Properties.limits.maxViewports;
    physicallimits.maxViewportDimensions[0]                        = m_Properties.limits.maxViewportDimensions[0];
    physicallimits.maxViewportDimensions[1]                        = m_Properties.limits.maxViewportDimensions[1];
    physicallimits.viewportBoundsRange[0]                          = m_Properties.limits.viewportBoundsRange[0];
    physicallimits.viewportBoundsRange[1]                          = m_Properties.limits.viewportBoundsRange[1];
    physicallimits.viewportSubPixelBits                            = m_Properties.limits.viewportSubPixelBits;
    physicallimits.minMemoryMapAlignment                           = m_Properties.limits.minMemoryMapAlignment;
    physicallimits.minTexelBufferOffsetAlignment                   = m_Properties.limits.minTexelBufferOffsetAlignment;
    physicallimits.minUniformBufferOffsetAlignment                 = m_Properties.limits.minUniformBufferOffsetAlignment;
    physicallimits.minStorageBufferOffsetAlignment                 = m_Properties.limits.minStorageBufferOffsetAlignment;
    physicallimits.minTexelOffset                                  = m_Properties.limits.minTexelOffset;
    physicallimits.maxTexelOffset                                  = m_Properties.limits.maxTexelOffset;
    physicallimits.minTexelGatherOffset                            = m_Properties.limits.minTexelGatherOffset;
    physicallimits.maxTexelGatherOffset                            = m_Properties.limits.maxTexelGatherOffset;
    physicallimits.minInterpolationOffset                          = m_Properties.limits.minInterpolationOffset;
    physicallimits.maxInterpolationOffset                          = m_Properties.limits.maxInterpolationOffset;
    physicallimits.subPixelInterpolationOffsetBits                 = m_Properties.limits.subPixelInterpolationOffsetBits;
    physicallimits.maxFramebufferWidth                             = m_Properties.limits.maxFramebufferWidth;
    physicallimits.maxFramebufferHeight                            = m_Properties.limits.maxFramebufferHeight;
    physicallimits.maxFramebufferLayers                            = m_Properties.limits.maxFramebufferLayers;
    physicallimits.framebufferColorSampleCounts                    = m_Properties.limits.framebufferColorSampleCounts;
    physicallimits.framebufferDepthSampleCounts                    = m_Properties.limits.framebufferDepthSampleCounts;
    physicallimits.framebufferStencilSampleCounts                  = m_Properties.limits.framebufferStencilSampleCounts;
    physicallimits.framebufferNoAttachmentsSampleCounts            = m_Properties.limits.framebufferNoAttachmentsSampleCounts;
    physicallimits.maxColorAttachments                             = m_Properties.limits.maxColorAttachments;
    physicallimits.sampledImageColorSampleCounts                   = m_Properties.limits.sampledImageColorSampleCounts;
    physicallimits.sampledImageIntegerSampleCounts                 = m_Properties.limits.sampledImageIntegerSampleCounts;
    physicallimits.sampledImageDepthSampleCounts                   = m_Properties.limits.sampledImageDepthSampleCounts;
    physicallimits.sampledImageStencilSampleCounts                 = m_Properties.limits.sampledImageStencilSampleCounts;
    physicallimits.storageImageSampleCounts                        = m_Properties.limits.storageImageSampleCounts;
    physicallimits.maxSampleMaskWords                              = m_Properties.limits.maxSampleMaskWords;
    physicallimits.timestampComputeAndGraphics                     = m_Properties.limits.timestampComputeAndGraphics;
    physicallimits.timestampPeriod                                 = m_Properties.limits.timestampPeriod;
    physicallimits.maxClipDistances                                = m_Properties.limits.maxClipDistances;
    physicallimits.maxCullDistances                                = m_Properties.limits.maxCullDistances;
    physicallimits.maxCombinedClipAndCullDistances                 = m_Properties.limits.maxCombinedClipAndCullDistances;
    physicallimits.discreteQueuePriorities                         = m_Properties.limits.discreteQueuePriorities;
    physicallimits.pointSizeRange[0]                               = m_Properties.limits.pointSizeRange[0];
    physicallimits.pointSizeRange[1]                               = m_Properties.limits.pointSizeRange[1];
    physicallimits.lineWidthRange[0]                               = m_Properties.limits.lineWidthRange[0];
    physicallimits.lineWidthRange[1]                               = m_Properties.limits.lineWidthRange[1];
    physicallimits.pointSizeGranularity                            = m_Properties.limits.pointSizeGranularity;
    physicallimits.lineWidthGranularity                            = m_Properties.limits.lineWidthGranularity;
    physicallimits.strictLines                                     = m_Properties.limits.strictLines;
    physicallimits.standardSampleLocations                         = m_Properties.limits.standardSampleLocations;
    physicallimits.optimalBufferCopyOffsetAlignment                = m_Properties.limits.optimalBufferCopyOffsetAlignment;
    physicallimits.optimalBufferCopyRowPitchAlignment              = m_Properties.limits.optimalBufferCopyRowPitchAlignment;
    physicallimits.nonCoherentAtomSize                             = m_Properties.limits.nonCoherentAtomSize;


    KGLogPrintf(KGLOG_INFO, "Max mrt limits is: %d", m_Properties.limits.maxFragmentOutputAttachments);
    KGLogPrintf(KGLOG_INFO, "Max WorkGroup limits is: %d", m_Properties.limits.maxComputeWorkGroupInvocations);

    {
        VkFormatProperties formatProps;
        VkImageTiling      tileInfo;
        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_B10G11R11_UFLOAT_PACK32, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        {
            tileInfo                     = VK_IMAGE_TILING_OPTIMAL;
            DrvOption::bSupportB10G11R11 = true;
        }
        else if (formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        {
            tileInfo                     = VK_IMAGE_TILING_LINEAR;
            DrvOption::bSupportB10G11R11 = true;
        }
        else
        {
            KGLogPrintf(KGLOG_WARNING, "not support B10G11R11_UFLOAT");
            DrvOption::bSupportB10G11R11 = false;
        }

        if (physicallimits.maxPerStageDescriptorSamplers <= 16)
        {
            KGLogPrintf(KGLOG_WARNING, "This device only support 16 samplers perStage");
        }

        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_R32_SFLOAT, &formatProps);

        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        {
            DrvOption::bSupportR32Linear = true;
            KGLogPrintf(KGLOG_INFO, "This device support R32_SFLOAT Linear sampler");
        }
        else
        {
            DrvOption::bSupportR32Linear = false;
            KGLogPrintf(KGLOG_WARNING, "This device not support R32_SFLOAT Linear sampler");
        }

        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, &formatProps);

        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            DrvOption::bSupportD24S8              = true;
            DrvOption::bSupportD24S8OptimalLinear = true;
        }
        else if (formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            DrvOption::bSupportD24S8              = true;
            DrvOption::bSupportD24S8OptimalLinear = false;
        }
        else
        {
            DrvOption::bSupportD24S8              = false;
            DrvOption::bSupportD24S8OptimalLinear = false;
        }

        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            DrvOption::bSupportD32S8              = true;
            DrvOption::bSupportD32S8OptimalLinear = true;
        }
        else if (formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            DrvOption::bSupportD32S8              = true;
            DrvOption::bSupportD32S8OptimalLinear = false;
        }
        else
        {
            DrvOption::bSupportD32S8              = false;
            DrvOption::bSupportD32S8OptimalLinear = false;
        }
    }

    {
        VkFormatProperties formatProps;
        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_R32_SFLOAT, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        {
            DrvOption::bSupportR32Blend = true;
        }
        else
        {
            DrvOption::bSupportR32Blend = false;
        }
    }

    {
        VkFormatProperties formatProps;
        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, &formatProps);

        if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT))
        {
            DrvOption::bCheckYCBCRSupported = false;
        }

        if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT))
        {
            DrvOption::bCheckYCBCRSupported = false;
        }
    }

    {
        DrvOption::maxTexelBufferElements = physicallimits.maxTexelBufferElements;
    }

    result = true;
Exit0:
    return result;
}

static inline bool InsertExtension(std::vector<const char*>& extensions, VkExtensionProperties const& property, char const* pszName)
{
    if (strcmp(property.extensionName, pszName) == 0)
    {
        auto it = std::find(extensions.begin(), extensions.end(), pszName);
        if (it == extensions.end())
        {
            extensions.push_back(pszName);
            return true;
        }
    }

    return false;
}

template <typename MainT, typename NewT>
static inline void PnextChainPushFront(MainT* mainStruct, NewT* newStruct)
{
    newStruct->pNext = mainStruct->pNext;
    mainStruct->pNext = newStruct;
}

BOOL vks::KVulkanDevice::_CreateDevice(const gfx::RenderSystemInfo& renderSysteInfo)
{
    BOOL            result        = false;
    VkResult        hRetCode      = VK_INCOMPLETE;
    BOOL            bRetCode      = false;
    KEngineOptions* pEngineOption = NSEngine::GetEngineOptions();

    uint32_t extCount = 0;
    // Select physical device to be used for the Vulkan example
    // Defaults to the first device unless specified by command line

    // Get list of supported extensions

    vks::vkEnumerateDeviceExtensionProperties(m_pPhysicalDevice, nullptr, &extCount, nullptr);
    if (extCount > 0)
    {
        BOOL bSupportMemExtends              = false;
        bool l_bSupportRayTracingPipeline    = false;
        bool l_bSupportAccelerationStructure = false;
        bool l_bSpportSPV1_4                 = false;
        bool l_bSpportRayQuery               = false;
        KGLogPrintf(KGLOG_INFO, "support extensions:");

        std::vector<VkExtensionProperties> extensions(extCount);
        if (vks::vkEnumerateDeviceExtensionProperties(m_pPhysicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
        {
            bool bSupportAtomicInt64Base  = false;
            bool bSupportImageAtomicInt64 = false;
            bool l_bSupportDescriptorIndexing = false;
            bool l_bSupportMutableDescriptor = false;

            bool bSupportNVDiagnosticsCheckPoints = false;
            bool bSupportNVDiagnosticsConfig = false;

            uint32_t n = 1;
            for (auto& ext : extensions)
            {
                KGLogPrintf(KGLOG_INFO, "%d:  %s", n, ext.extensionName);
                m_SupportedExtensions.push_back(ext.extensionName);
                if (strcmp(ext.extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);
                    bSupportAtomicInt64Base = true;
                    KGLogPrintf(KGLOG_INFO, "Device support shader atomic int64 Ext");
                }

                if (strcmp(ext.extensionName, VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);
                    bSupportImageAtomicInt64 = true;
                    KGLogPrintf(KGLOG_INFO, "Device support shader image atomic int64 Ext");
                }

                // for vma
                {
                    if (strcmp(ext.extensionName, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
                    {
                        m_DeviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
                        DrvOption::bSupportGetMemoryRequirement2 = true;
                    }

                    if (strcmp(ext.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
                    {
                        m_DeviceExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
                        DrvOption::bSupportDedicatedAllocation = true;
                    }

                    if (strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
                    {
                        m_DeviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
                        DrvOption::bSupportMemoryBudget = true;
                    }
                }

#if bOVR_ON
                if (strcmp(ext.extensionName, VK_KHR_MULTIVIEW_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
                    m_Settings.m_bSupportsMultiview = true;
                }

                if (strcmp(ext.extensionName, VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME);
                    m_Settings.m_bSupportsFragmentDensity = true;
                }


                if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
                }

#endif

#if __ANDROID__
                InsertExtension(m_DeviceExtensions, ext, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
                InsertExtension(m_DeviceExtensions, ext, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
                InsertExtension(m_DeviceExtensions, ext, VK_KHR_MAINTENANCE1_EXTENSION_NAME);
                InsertExtension(m_DeviceExtensions, ext, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
                InsertExtension(m_DeviceExtensions, ext, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
                InsertExtension(m_DeviceExtensions, ext, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
#endif

                if (strcmp(ext.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
                    m_Settings.m_bSupportsDebugMarker = true;
                }

                if (strcmp(ext.extensionName, "VK_EXT_load_store_op_none") == 0)
                {
                    m_DeviceExtensions.push_back("VK_EXT_load_store_op_none");
                    DrvOption::bSupportStoreOpNone = TRUE;
                }

                if (strcmp(ext.extensionName, "VK_QCOM_render_pass_store_ops") == 0)
                {
                    m_DeviceExtensions.push_back("VK_QCOM_render_pass_store_ops");
                    DrvOption::bSupportStoreOpNone = TRUE;
                }

                if (strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_EXT_SHADER_SUBGROUP_VOTE_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_SHADER_SUBGROUP_VOTE_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
                    m_bFp16Enabled = true;
                }

                if (strcmp(ext.extensionName, VK_KHR_16BIT_STORAGE_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME);
                    m_bSubgroupF16Enabled = true;
                }

                if (strcmp(ext.extensionName, "VK_EXT_image_compression_control") == 0)
                {
                    m_DeviceExtensions.push_back("VK_EXT_image_compression_control");
                    KGLogPrintf(KGLOG_INFO, "Device support AFBC");
                }

                if (strcmp(ext.extensionName, "VK_EXT_image_compression_control_swapchain") == 0)
                {
                    m_DeviceExtensions.push_back("VK_EXT_image_compression_control_swapchain");
                    KGLogPrintf(KGLOG_INFO, "Device support AFBC Swapchain");
                }

                if (strcmp(ext.extensionName, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support Acceleration structure !");
                    DrvOption::bSupportDeviceAddress = TRUE;
                }
                if (strcmp(ext.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support DESCRIPTOR_INDEXING !");
                    l_bSupportDescriptorIndexing = true;
                }
                if (strcmp(ext.extensionName, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support MUTABLE_DESCRIPTOR_TYPE !");
                    l_bSupportMutableDescriptor = true;

                }
                // VK_KHR_spirv_1_4 depend VK_KHR_shader_float_controls
                if (strcmp(ext.extensionName, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
                }

                if (strcmp(ext.extensionName, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support image format list!");
                }
#if (USE_VK_1_4)
                /****** check ray tracing *****/
                if (strcmp(ext.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support Ray Tracing");
                    l_bSupportRayTracingPipeline = true;
                }
                if (strcmp(ext.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support Acceleration structure !");
                    l_bSupportAccelerationStructure = true;
                }

             
                if (strcmp(ext.extensionName, VK_KHR_SPIRV_1_4_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support spv 1.4 !");
                    l_bSpportSPV1_4 = true;
                }
               
                if (strcmp(ext.extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support DHO !");
                }

                if (strcmp(ext.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support ray query !");
                    l_bSpportRayQuery = true;
                }
#endif
#if (defined(VK_VERSION_1_4) && defined(_DEBUG))
                if (strcmp(ext.extensionName, VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME) == 0)
                {
                    m_DeviceExtensions.push_back(VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME);
                    KGLogPrintf(KGLOG_INFO, "Device support ray tracing validation !");
                    l_bSpportRayQuery = true;
                }
#endif

                if (DrvOption::bEnableNsightAftermath)
                {
                    if (strcmp(ext.extensionName, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) == 0)
                    {
                        bSupportNVDiagnosticsCheckPoints = true;
                        m_DeviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
                        KGLogPrintf(KGLOG_INFO, "Device support VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME !");
                    }

                    if (strcmp(ext.extensionName, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME) == 0)
                    {
                        bSupportNVDiagnosticsConfig = true;
                        m_DeviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
                        KGLogPrintf(KGLOG_INFO, "Device support VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME !");
                    }
                }

                if (DrvOption::bVKValidateEnable && DrvOption::bEnableVKDebugPrintf)
                {
                    if (strcmp(ext.extensionName, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME) == 0)
                    {
                        m_DeviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
                        KGLogPrintf(KGLOG_INFO, "Device support debug printf !");
                    }
                }

                /********************/
                ++n;
            }

            DrvOption::bSupportShaderAtomicInt64 = bSupportAtomicInt64Base && bSupportImageAtomicInt64;
            if (DrvOption::bSupportShaderAtomicInt64)
            {
                m_bAtomicUint64Enabled = true;
            }
            DrvOption::bSupportBindless = l_bSupportDescriptorIndexing && l_bSupportMutableDescriptor;

            m_bSupportNsightAftermath = bSupportNVDiagnosticsCheckPoints && bSupportNVDiagnosticsConfig;
        }
        // 主要是防止不支持addressdevice得情况，其实是可以只用rayquery的(大概吧)
        DrvOption::bSupportRayQuery   = l_bSpportRayQuery && l_bSupportAccelerationStructure && l_bSpportSPV1_4 && DrvOption::bSupportDeviceAddress;
        DrvOption::bSupportRayTracing = l_bSupportRayTracingPipeline && DrvOption::bSupportRayQuery;
    }

    // m_DeviceExtensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
    // m_DeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

    KGLogPrintf(KGLOG_INFO, "enable device extensions:");
    for (auto it : m_DeviceExtensions)
    {
        KGLogPrintf(KGLOG_INFO, "%s", it);
    }

    PnextChainPushFront(&m_Properties2, &m_subgroupProperties);

    {
        // robustBufferAccess 是 Vulkan 提供的一个特性，用于增强缓冲区访问的健壮性。启用此特性后，当着色器代码尝试访问超出缓冲区范围的数据时，驱动程序会返回零值，而不是导致未定义行为或崩溃。
        // 内网关闭查越界，外网开启保证稳定性
#ifndef KG_PUBLISH
        m_Features.robustBufferAccess = false;
#else
        m_Features.robustBufferAccess = true;
#endif
        // VkPhysicalDeviceFeatures
        m_Features2.features = m_Features;
    }


    VkPhysicalDeviceShaderFloat16Int8Features           FP16Features         = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures                Storage16BitFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroupFloat16      = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};

    // VkPhysicalDeviceVulkan11Features vulkan11Features = {}; //for vk1.2
    // vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    // vulkan11Features.pNext = nullptr;
    VkPhysicalDeviceSamplerYcbcrConversionFeatures samplerYcbcrFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};

    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT atomicImage64Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT};
    VkPhysicalDeviceShaderAtomicInt64Features         atomic64Features      = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES};

    PnextChainPushFront(&m_Features2, &samplerYcbcrFeatures);

    if (m_bFp16Enabled)
    {
        PnextChainPushFront(&m_Features2, &subgroupFloat16);
        // Query 16 bit storage
        PnextChainPushFront(&m_Features2, &Storage16BitFeatures);
        // Query 16 bit ops
        PnextChainPushFront(&m_Features2, &FP16Features);
    }

    if (m_bAtomicUint64Enabled)
    {
        PnextChainPushFront(&m_Features2, &atomic64Features);
        PnextChainPushFront(&m_Features2, &atomicImage64Features);
    }

#if bOVR_ON
    VkPhysicalDeviceMultiviewFeatures   deviceMultiviewFeatures   = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    VkPhysicalDeviceMultiviewProperties deviceMultiviewProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};

    if (m_Settings.m_bSupportsMultiview)
    {
        PnextChainPushFront(&m_Features2, &deviceMultiviewFeatures);
        // device properties request, including sample extensions
        PnextChainPushFront(&m_Properties2, &deviceMultiviewProperties);
    }
#endif

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR    rayTracingPipelineFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR              rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceDescriptorIndexingFeatures       indexingFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceScalarBlockLayoutFeatures        scalarBlockLayoutFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES};
    VkPhysicalDeviceBufferDeviceAddressFeatures      bufferDeviceAddressFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    bufferDeviceAddressFeatures.bufferDeviceAddress                              = true;
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptorTypeFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT };
    mutableDescriptorTypeFeatures.mutableDescriptorType = true;
    if (DrvOption::bSupportBindless)
    {
        PnextChainPushFront(&m_Features2, &mutableDescriptorTypeFeatures);
    }
    // 本来这两个是一定兼容的，但是我们试图将光追和bindless绑定，所以分开, rayquery 不需要 bindless
    if (DrvOption::bSupportRayTracing || DrvOption::bSupportRayQuery)
    {
        PnextChainPushFront(&m_Features2, &rayTracingPipelineFeatures);
        PnextChainPushFront(&m_Features2, &rayQueryFeatures);
        PnextChainPushFront(&m_Features2, &accelerationStructureFeatures);
        PnextChainPushFront(&m_Features2, &indexingFeatures);
        PnextChainPushFront(&m_Features2, &scalarBlockLayoutFeatures);
#if (defined(VK_VERSION_1_4) && defined(_DEBUG))
        // ray tracing validation  support in vulkan 1.4
        VkPhysicalDeviceRayTracingValidationFeaturesNV validationFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV};
        PnextChainPushFront(&m_Features2, &validationFeatures);
#else

#endif
    }
    if (DrvOption::bSupportDeviceAddress)
    {
        PnextChainPushFront(&m_Features2, &bufferDeviceAddressFeatures);
    }

#ifdef _WIN32
    // Set up device creation info for Aftermath feature flag configuration.
    VkDeviceDiagnosticsConfigFlagsNV aftermathFlags =
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV |  // Enable automatic call stack checkpoints.
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |      // Enable tracking of resources.
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV |      // Generate debug information for shaders.
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;  // Enable additional runtime shader error reporting.

    VkDeviceDiagnosticsConfigCreateInfoNV aftermathInfo = {};
    aftermathInfo.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV;
    aftermathInfo.flags = aftermathFlags;
    if (m_bSupportNsightAftermath)
    {
        PnextChainPushFront(&m_Features2, &aftermathInfo);

        // Enable Nsight Aftermath GPU crash dump creation.
        // This needs to be done before the Vulkan device is created.
        bool bUseStrippedShaders = false;
        s_AftermathGpuCrashTracker.Initialize(bUseStrippedShaders);
    }
#endif

    if (DrvOption::uVulkanVersion != VK_API_VERSION_1_0)
    {
        vks::vkGetPhysicalDeviceFeatures2KHR(m_pPhysicalDevice, &m_Features2);
    }
    if ((!indexingFeatures.descriptorBindingPartiallyBound) || (!indexingFeatures.runtimeDescriptorArray))
    {
        DrvOption::bSupportBindless = FALSE;
    }

    // 强行将光追和bindless绑定
    DrvOption::bSupportRayTracing = DrvOption::bSupportRayTracing & DrvOption::bSupportBindless;

    m_bFp16Enabled         = m_bFp16Enabled && Storage16BitFeatures.storageBuffer16BitAccess;
    m_bFp16Enabled         = m_bFp16Enabled && FP16Features.shaderFloat16;
    m_bSubgroupF16Enabled  = m_bFp16Enabled && m_bSubgroupF16Enabled && subgroupFloat16.shaderSubgroupExtendedTypes;
    m_bAtomicUint64Enabled = m_bAtomicUint64Enabled && atomic64Features.shaderBufferInt64Atomics && atomic64Features.shaderSharedInt64Atomics && atomicImage64Features.shaderImageInt64Atomics;

    if (m_bAtomicUint64Enabled)
    {
        // 检查R64_UINT格式是否支持，否则AtomicUint64仍然需要判断为不支持
        VkFormatProperties formatProperties;
        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, VK_FORMAT_R64_UINT, &formatProperties);

        if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
        {
            m_bAtomicUint64Enabled = false;
            KGLogPrintf(KGLOG_INFO, "Image format R64 is not supported storage usages, although the atomic64 is supported.");
        }
    }

    // KGLogPrintf(KGLOG_INFO, "Device %s independentBlend ", m_Features2.features.independentBlend ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s samplerYcbcrConversion ", samplerYcbcrFeatures.samplerYcbcrConversion ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s storage InputOutPut16", Storage16BitFeatures.storageInputOutput16 ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s uniformAndStorageBuffer16BitAccess", Storage16BitFeatures.uniformAndStorageBuffer16BitAccess ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s storagePushConstant16", Storage16BitFeatures.storagePushConstant16 ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s storageBuffer16BitAccess", Storage16BitFeatures.storageBuffer16BitAccess ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s float16 features", m_bFp16Enabled ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s subgoup float16 features", m_bSubgroupF16Enabled ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s bindless features", DrvOption::bSupportBindless ? "support" : "not support");
    KGLogPrintf(KGLOG_INFO, "Device %s shader atomic64", m_bAtomicUint64Enabled ? "support" : "not support");

    if (!m_bSubgroupF16Enabled)
    {
        pEngineOption->shaderFeatureMask &= ~(uint8_t)ShaderFeatureMasks::subgroupf16;
    }

    if (!m_bFp16Enabled)
    {
        pEngineOption->shaderFeatureMask &= ~(uint8_t)ShaderFeatureMasks::float16;
    }

    if (!samplerYcbcrFeatures.samplerYcbcrConversion)
    {
        DrvOption::bEnableAndroidCamera = FALSE;
    }

    if (DrvOption::uVulkanVersion != VK_API_VERSION_1_0)
    {
        vks::vkGetPhysicalDeviceProperties2KHR(m_pPhysicalDevice, &m_Properties2);
    }

    KGLogPrintf(KGLOG_INFO, "Device subgoupSize is %d, quadOperationsInAllStages:%d support:%x", m_subgroupProperties.subgroupSize, m_subgroupProperties.quadOperationsInAllStages, m_subgroupProperties.supportedOperations);

#if bOVR_ON
    if (m_Settings.m_bSupportsMultiview)
    {
        KGLogPrintf(KGLOG_INFO, "Device %s multiview rendering, with %d views and %u max instances", deviceMultiviewFeatures.multiview ? "supports" : "does not support", deviceMultiviewProperties.maxMultiviewViewCount, deviceMultiviewProperties.maxMultiviewInstanceIndex);

        // only enable multiview for the app if deviceMultiviewFeatures.multiview is 1.
        ASSERT(deviceMultiviewFeatures.multiview == VK_TRUE);
    }
#endif

    if (renderSysteInfo.bCreateInstance)
    {
        bRetCode = CreateLogicalDevice(m_Features, m_DeviceExtensions, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, true, &m_Features2);
    }
    else
    {
        // 不知道unity是怎么回事，后面获取compute queue 会宕机，只能共用 graphic queue 了
        bRetCode = CreateLogicalDevice(m_Features, m_DeviceExtensions, VK_QUEUE_GRAPHICS_BIT, false);
    }
    KGLOG_PROCESS_ERROR(bRetCode == TRUE && "Could not create Vulkan device");

    // If requested, we enable the default validation layers for debugging
    if (DrvOption::bVKValidateEnable)
    {
        // The report flags determine what type of messages for the layers will be displayed
        // For validating (debugging) an appplication the error and warning bits should suffice
        VkDebugReportFlagsEXT debugReportFlags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                                 VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                                                 VK_DEBUG_REPORT_ERROR_BIT_EXT |
                                                 VK_DEBUG_REPORT_DEBUG_BIT_EXT;
        // Additional flags include performance info, loader and layer debug messages, etc.
        // 检查扩展的支持
        if (std::find(m_vecInstanceSupportExtensions.begin(), m_vecInstanceSupportExtensions.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) != m_vecInstanceSupportExtensions.end())
        {
            vks::debug::SetupDebugging(m_pInstance, debugReportFlags, VK_NULL_HANDLE, true);
        }
        else
        {
            vks::debug::SetupDebugging(m_pInstance, debugReportFlags, VK_NULL_HANDLE, false);
        }
    }

    if (m_Features.shaderStorageImageArrayDynamicIndexing)
    {
        KGLogPrintf(KGLOG_INFO, "Deviece Features support shaderStorageImageArrayDynamicIndexing");
    }

    {
#if USE_OPTICK
        Optick::VulkanFunctions functions;
        functions.vkGetPhysicalDeviceProperties = vks::vkGetPhysicalDeviceProperties;
        functions.vkCreateQueryPool             = (PFN_vkCreateQueryPool_)vks::vkCreateQueryPool;
        functions.vkCreateCommandPool           = (PFN_vkCreateCommandPool_)vks::vkCreateCommandPool;
        functions.vkAllocateCommandBuffers      = (PFN_vkAllocateCommandBuffers_)vks::vkAllocateCommandBuffers;
        functions.vkCreateFence                 = (PFN_vkCreateFence_)vks::vkCreateFence;
        functions.vkCmdResetQueryPool           = vks::vkCmdResetQueryPool;
        functions.vkQueueSubmit                 = (PFN_vkQueueSubmit_)vks::vkQueueSubmit;
        functions.vkWaitForFences               = (PFN_vkWaitForFences_)vks::vkWaitForFences;
        functions.vkResetCommandBuffer          = (PFN_vkResetCommandBuffer_)vks::vkResetCommandBuffer;
        functions.vkCmdWriteTimestamp           = (PFN_vkCmdWriteTimestamp_)vks::vkCmdWriteTimestamp;
        functions.vkGetQueryPoolResults         = (PFN_vkGetQueryPoolResults_)vks::vkGetQueryPoolResults;
        functions.vkBeginCommandBuffer          = (PFN_vkBeginCommandBuffer_)vks::vkBeginCommandBuffer;
        functions.vkEndCommandBuffer            = (PFN_vkEndCommandBuffer_)vks::vkEndCommandBuffer;
        functions.vkResetFences                 = (PFN_vkResetFences_)vks::vkResetFences;
        functions.vkDestroyCommandPool          = vks::vkDestroyCommandPool;
        functions.vkDestroyQueryPool            = vks::vkDestroyQueryPool;
        functions.vkDestroyFence                = vks::vkDestroyFence;
        functions.vkFreeCommandBuffers          = vks::vkFreeCommandBuffers;

        BOOL bSupportTimestampQueries = TRUE;
        if (m_Properties.limits.timestampPeriod == 0)
        {
            bSupportTimestampQueries = FALSE;
            KGLogPrintf(KGLOG_ERR, "The selected device does not support timestamp queries!");
        }

        if (!m_Properties.limits.timestampComputeAndGraphics)
        {
            if (m_QueueFamilyProperties[m_QueueFamilyIndices.graphics].timestampValidBits == 0)
            {
                bSupportTimestampQueries = FALSE;
                KGLogPrintf(KGLOG_ERR, "The selected graphics queue family does not support timestamp queries!");
            }
        }

        if (bSupportTimestampQueries)
        {
            OPTICK_GPU_INIT_VULKAN(&m_pLogicalDevice, &m_pPhysicalDevice, &m_pGaphicQueue, &m_QueueFamilyIndices.graphics, 1, &functions);
        }
#endif
    }

#if MICROPROFILE_ENABLED
    {
        KMicroProfile::KMicroProfileVulkanFunctions functions;
        functions.vkGetPhysicalDeviceProperties = vks::vkGetPhysicalDeviceProperties;
        functions.vkCreateQueryPool             = (PFN_vkCreateQueryPool_)vks::vkCreateQueryPool;
        functions.vkCreateCommandPool           = (PFN_vkCreateCommandPool_)vks::vkCreateCommandPool;
        functions.vkAllocateCommandBuffers      = (PFN_vkAllocateCommandBuffers_)vks::vkAllocateCommandBuffers;
        functions.vkCreateFence                 = (PFN_vkCreateFence_)vks::vkCreateFence;
        functions.vkCmdResetQueryPool           = vks::vkCmdResetQueryPool;
        functions.vkQueueSubmit                 = (PFN_vkQueueSubmit_)vks::vkQueueSubmit;
        functions.vkWaitForFences               = (PFN_vkWaitForFences_)vks::vkWaitForFences;
        functions.vkResetCommandBuffer          = (PFN_vkResetCommandBuffer_)vks::vkResetCommandBuffer;
        functions.vkCmdWriteTimestamp           = (PFN_vkCmdWriteTimestamp_)vks::vkCmdWriteTimestamp;
        functions.vkGetQueryPoolResults         = (PFN_vkGetQueryPoolResults_)vks::vkGetQueryPoolResults;
        functions.vkBeginCommandBuffer          = (PFN_vkBeginCommandBuffer_)vks::vkBeginCommandBuffer;
        functions.vkEndCommandBuffer            = (PFN_vkEndCommandBuffer_)vks::vkEndCommandBuffer;
        functions.vkResetFences                 = (PFN_vkResetFences_)vks::vkResetFences;
        functions.vkDestroyCommandPool          = vks::vkDestroyCommandPool;
        functions.vkDestroyQueryPool            = vks::vkDestroyQueryPool;
        functions.vkDestroyFence                = vks::vkDestroyFence;
        functions.vkFreeCommandBuffers          = vks::vkFreeCommandBuffers;

        BOOL bSupportTimestampQueries = TRUE;
        if (m_Properties.limits.timestampPeriod == 0)
        {
            bSupportTimestampQueries = FALSE;
            KGLogPrintf(KGLOG_ERR, "The selected device does not support timestamp queries!");
        }

        if (!m_Properties.limits.timestampComputeAndGraphics)
        {
            if (m_QueueFamilyProperties[m_QueueFamilyIndices.graphics].timestampValidBits == 0)
            {
                bSupportTimestampQueries = FALSE;
                KGLogPrintf(KGLOG_ERR, "The selected graphics queue family does not support timestamp queries!");
            }
        }

        if (bSupportTimestampQueries)
        {
            MICROPROFILE_GPU_INIT(&m_pLogicalDevice, &m_pPhysicalDevice, &m_pGaphicQueue, &m_QueueFamilyIndices.graphics, 1, &functions);
            MICROPROFILE_ENABLE_ALL_GROUP();
        }
    }
#endif


    if (enableDebugMarkers)
    {
        vks::debugmarker::Setup(m_pLogicalDevice);
    }

    result = true;
Exit0:
    return result;
}

// #if X3D_VK_USE_VMA
BOOL vks::KVulkanDevice::_InitVMA()
{
    BOOL bResult = FALSE;

    VmaVulkanFunctions sVmaVkFuncs                  = {};
    sVmaVkFuncs.vkGetPhysicalDeviceProperties       = vks::vkGetPhysicalDeviceProperties;
    sVmaVkFuncs.vkGetPhysicalDeviceMemoryProperties = vks::vkGetPhysicalDeviceMemoryProperties;
    sVmaVkFuncs.vkAllocateMemory                    = vks::vkAllocateMemory;
    sVmaVkFuncs.vkFreeMemory                        = vks::vkFreeMemory;
    sVmaVkFuncs.vkMapMemory                         = vks::vkMapMemory;
    sVmaVkFuncs.vkUnmapMemory                       = vks::vkUnmapMemory;
    sVmaVkFuncs.vkFlushMappedMemoryRanges           = vks::vkFlushMappedMemoryRanges;
    sVmaVkFuncs.vkInvalidateMappedMemoryRanges      = vks::vkInvalidateMappedMemoryRanges;
    sVmaVkFuncs.vkBindBufferMemory                  = vks::vkBindBufferMemory;
    sVmaVkFuncs.vkBindImageMemory                   = vks::vkBindImageMemory;
    sVmaVkFuncs.vkGetBufferMemoryRequirements       = vks::vkGetBufferMemoryRequirements;
    sVmaVkFuncs.vkGetImageMemoryRequirements        = vks::vkGetImageMemoryRequirements;
    sVmaVkFuncs.vkCreateBuffer                      = vks::vkCreateBuffer;
    sVmaVkFuncs.vkDestroyBuffer                     = vks::vkDestroyBuffer;
    sVmaVkFuncs.vkCreateImage                       = vks::vkCreateImage;
    sVmaVkFuncs.vkDestroyImage                      = vks::vkDestroyImage;
    sVmaVkFuncs.vkCmdCopyBuffer                     = vks::vkCmdCopyBuffer;
#if VMA_DEDICATED_ALLOCATION
    sVmaVkFuncs.vkGetBufferMemoryRequirements2KHR = vks::vkGetBufferMemoryRequirements2KHR;
    sVmaVkFuncs.vkGetImageMemoryRequirements2KHR  = vks::vkGetImageMemoryRequirements2KHR;
#endif
#if VMA_BIND_MEMORY2
    sVmaVkFuncs.vkBindBufferMemory2KHR = vks::vkBindBufferMemory2KHR;
    sVmaVkFuncs.vkBindImageMemory2KHR  = vks::vkBindImageMemory2KHR;
#endif
#if VMA_MEMORY_BUDGET
    sVmaVkFuncs.vkGetPhysicalDeviceMemoryProperties2KHR = vks::vkGetPhysicalDeviceMemoryProperties2KHR;
#endif

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion       = VK_API_VERSION_1_1;
    allocatorInfo.flags                  = 0;

    if (DrvOption::bSupportGetMemoryRequirement2 && DrvOption::bSupportDedicatedAllocation)
    {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
    }

    if (DrvOption::bSupportMemoryBudget)
    {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if (DrvOption::bSupportDeviceAddress)
    {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

    ASSERT(m_pPhysicalDevice);
    ASSERT(m_pLogicalDevice);
    ASSERT(m_pInstance);

    allocatorInfo.physicalDevice   = m_pPhysicalDevice;
    allocatorInfo.device           = m_pLogicalDevice;
    allocatorInfo.instance         = m_pInstance;
    allocatorInfo.pVulkanFunctions = &sVmaVkFuncs;

    // 增大BlockSize会减少vkAllocateMemory的调用次数，但是会增加单次分配的耗时
    // 8MB对于PC平台太小，碎片化严重导致帧率骤降
#ifndef _WIN32
    allocatorInfo.preferredLargeHeapBlockSize = 8ull * 1024 * 1024; /// 8MiB per block
#endif // !_WIN32

    VkResult eRetCode = vmaCreateAllocator(&allocatorInfo, &m_pVMAllocator);
    KGLOG_PROCESS_ERROR(eRetCode == VK_SUCCESS);
    ASSERT(m_pVMAllocator);

    bResult = TRUE;
Exit0:
    return bResult;
}

void vks::KVulkanDevice::_UnInitVMA()
{
    if (m_pVMAllocator)
    {
        vmaDestroyAllocator(m_pVMAllocator);
        m_pVMAllocator = nullptr;
    }
}

BOOL vks::KVulkanDevice::VMAMapMemory(VmaAllocation pVMAllocation, void** ppData)
{
    BOOL     bResult   = FALSE;
    VkResult vkRetCode = VK_INCOMPLETE;

    KGLOG_ASSERT_EXIT(pVMAllocation && ppData);

    vkRetCode = vmaMapMemory(m_pVMAllocator, pVMAllocation, ppData);
    KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMAUnmapMemory(VmaAllocation pVMAllocation)
{
    BOOL bResult = FALSE;

    KGLOG_ASSERT_EXIT(pVMAllocation);
    vmaUnmapMemory(m_pVMAllocator, pVMAllocation);

    bResult = TRUE;
Exit0:
    return bResult;
}

uint32_t vks::KVulkanDevice::VMAGetAllocSize(VmaAllocation pVMAllocation)
{
    uint32_t          uAllocSize = 0;
    VmaAllocationInfo sAllocInfo;

    KGLOG_ASSERT_EXIT(pVMAllocation);
    vmaGetAllocationInfo(m_pVMAllocator, pVMAllocation, &sAllocInfo);

    uAllocSize = (uint32_t)sAllocInfo.size;
Exit0:
    return uAllocSize;
}

BOOL vks::KVulkanDevice::VMAFlushAllocation(VmaAllocation pVMAllocation, VkDeviceSize uOffset, VkDeviceSize uSize)
{
    BOOL     bResult   = FALSE;
    VkResult vkRetCode = VK_INCOMPLETE;

    KGLOG_ASSERT_EXIT(pVMAllocation);

    vkRetCode = vmaFlushAllocation(m_pVMAllocator, pVMAllocation, uOffset, uSize);
    KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMAInvalidateAllocation(VmaAllocation pVMAllocation, VkDeviceSize uOffset, VkDeviceSize uSize)
{
    BOOL     bResult   = FALSE;
    VkResult vkRetCode = VK_INCOMPLETE;

    KGLOG_ASSERT_EXIT(pVMAllocation);

    vkRetCode = vmaInvalidateAllocation(m_pVMAllocator, pVMAllocation, uOffset, uSize);
    KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMABuildStatsString(char** ppStatsString)
{
    BOOL     bResult   = FALSE;
    VkResult vkRetCode = VK_INCOMPLETE;
    KGLOG_ASSERT_EXIT(ppStatsString);

    vmaBuildStatsString(m_pVMAllocator, ppStatsString, VK_TRUE);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMAFreeStatsString(char* pStatsString)
{
    BOOL     bResult   = FALSE;
    VkResult vkRetCode = VK_INCOMPLETE;
    KGLOG_ASSERT_EXIT(pStatsString);

    vmaFreeStatsString(m_pVMAllocator, pStatsString);

    bResult = TRUE;
Exit0:
    return bResult;
}

bool vks::KVulkanDevice::VMAGetAllocationIsCoherent(VmaAllocation& pVMAllocation)
{
    VmaAllocationInfo     allocationInfo      = {};
    VkMemoryPropertyFlags memoryPropertyFlags = {};
    vmaGetAllocationInfo(m_pVMAllocator, pVMAllocation, &allocationInfo);
    KGLOG_ASSERT_EXIT(allocationInfo.memoryType < m_MemoryProperties.memoryTypeCount && allocationInfo.memoryType < VK_MAX_MEMORY_TYPES);
    vmaGetMemoryTypeProperties(m_pVMAllocator, allocationInfo.memoryType, &memoryPropertyFlags);
Exit0:
    return (memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

BOOL vks::KVulkanDevice::VMACreateImage(const VkImageCreateInfo& sImgCreateInfo, VmaMemoryUsage eMemUsage, VkImage& pVkImage, VmaAllocation& pVMAllocation)
{
    PROF_CPU();
    BOOL                    bResult             = FALSE;
    int32_t                 nHeapCount          = 0;
    VkResult                vkRetCode           = VK_INCOMPLETE;
    VmaAllocationCreateInfo sVmaAllocCreateInfo = {};
    sVmaAllocCreateInfo.flags                   = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
    KGLOG_ASSERT_EXIT(!pVkImage);
    KGLOG_ASSERT_EXIT(!pVMAllocation);

    KGLOG_ASSERT_EXIT("FATAL ERROR" && sImgCreateInfo.extent.width > 0 && sImgCreateInfo.extent.height > 0);
    KGLOG_ASSERT_EXIT(eMemUsage > VmaMemoryUsage::VMA_MEMORY_USAGE_UNKNOWN && eMemUsage < VmaMemoryUsage::VMA_MEMORY_USAGE_MAX_ENUM);

    sVmaAllocCreateInfo.usage = eMemUsage;
    // sVmaAllocCreateInfo.requiredFlags = uMemProperties;
    //  allocate and create the image
    vkRetCode                 = vmaCreateImage(m_pVMAllocator, &sImgCreateInfo, &sVmaAllocCreateInfo, &pVkImage, &pVMAllocation, nullptr);


    if (vkRetCode == VK_ERROR_OUT_OF_DEVICE_MEMORY || vkRetCode == VK_ERROR_OUT_OF_HOST_MEMORY)
    {
        std::vector<VmaBudget> budgets;
        vmaGetHeapCount(m_pVMAllocator, nHeapCount);
        budgets.resize(nHeapCount);
        vmaGetBudget(m_pVMAllocator, &budgets[0]);
        const char* pGpuName = "GPU(未知)";

        if (DrvOption::szGPU && DrvOption::szGPU[0])
        {
            pGpuName = DrvOption::szGPU;
        }
        uint32_t uBlockBytes = 0;
        for (auto it : budgets)
        {
            uBlockBytes += (uint32_t)it.blockBytes;
        }
        uint32_t MB                = uBlockBytes / (1024 * 1024);
        float    fLocalGpuMemoryGB = DrvOption::fLocalGpuMemoryGB;
        KGLogPrintf(KGLOG_ERR, "完蛋，%s(%.2fG)当前进程已使用%uM显存,接着申请w:%u h:%u大小贴图失败，爆显存了!", pGpuName, fLocalGpuMemoryGB, MB, sImgCreateInfo.extent.width, sImgCreateInfo.extent.height);

#ifdef _WIN32
        MessageBox(NULL, "显存不足", "错误", MB_OK);
#endif

        // 根据宕机堆栈的行号，对号入座上报信息，大致可以知道是哪一档爆的显存
        if (MB < 1000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==0", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 1500)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==1", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 1800)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==2", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 2000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==3", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 2500)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==4", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 2800)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==5", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 3000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==6", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 3500)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2f)显存爆掉位置==7", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 4000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==8", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 4500)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==9", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 4500)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==10", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 5000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==11", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 5500)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==12", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 6000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==13", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 7000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==14", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 8000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==15", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 10000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==16", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 12000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==17", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 14000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==18", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 18000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==19", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 24000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==20", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else if (MB < 30000)
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==21", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "%s(%.2fG)显存爆掉位置==22", pGpuName, fLocalGpuMemoryGB);
            RaiseDumpException();
        }
    }

    KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMADestroyImage(VkImage& pVkImage, VmaAllocation& pVMAllocation)
{
    PROF_CPU();
    BOOL bResult = FALSE;

    KGLOG_ASSERT_EXIT(pVkImage);
    KGLOG_ASSERT_EXIT(pVMAllocation);

    vmaDestroyImage(m_pVMAllocator, pVkImage, pVMAllocation);
    pVkImage      = nullptr;
    pVMAllocation = nullptr;

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMACreateBuffer(
    VkDeviceSize       uBufferSize,
    VkBufferUsageFlags eBufferUsage,
    VmaMemoryUsage     eMemUsage,
    VkBuffer&          pVkBuffer,
    VmaAllocation&     pVMAllocation,
    void**             ppData
)
{
    PROF_CPU();
    ASSERT(m_pVMAllocator);
    int32_t nHeapCount = 0;

    BOOL                    bResult           = FALSE;
    VkResult                vkRetCode         = VK_INCOMPLETE;
    VkBufferCreateInfo      sBufferCreateInfo = {};
    VmaAllocationCreateInfo vmaallocInfo      = {};
    VmaAllocationInfo       vmaAllocationInfo = {};
    bool                    bMapped           = (eMemUsage == VMA_MEMORY_USAGE_CPU_ONLY && ppData);

    KGLOG_ASSERT_EXIT(!pVkBuffer);
    KGLOG_ASSERT_EXIT(!pVMAllocation);

    sBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sBufferCreateInfo.size  = uBufferSize;
    sBufferCreateInfo.usage = eBufferUsage;

    vmaallocInfo.usage = eMemUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;

    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
    if (bMapped)
    {
        vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }


    // allocate the buffer
    vkRetCode = vmaCreateBuffer(
        m_pVMAllocator,
        &sBufferCreateInfo,
        &vmaallocInfo,
        &pVkBuffer,
        &pVMAllocation,
        &vmaAllocationInfo
    );


   
    KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);

    if (bMapped)
    {
        ASSERT(vmaAllocationInfo.pMappedData);
        *ppData = vmaAllocationInfo.pMappedData;
    }


    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::VMADestroyBuffer(VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation)
{
    PROF_CPU();
    ASSERT(pVkBuffer && pVMAllocation);

    vmaDestroyBuffer(m_pVMAllocator, pVkBuffer, pVMAllocation);
    pVkBuffer     = nullptr;
    pVMAllocation = nullptr;

    return TRUE;
}

// #endif

void vks::KVulkanDevice::GetEnabledFeatures()
{
    // Can be overriden in derived class
    // if (m_Features.samplerAnisotropy) {
    //	m_EnabledFeatures.samplerAnisotropy = TRUE;
    //};
}

BOOL vks::KVulkanDevice::CreateShaderSpv(const char* szShaderName, VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStage, gfx::IShaderReflector* pReflector)
{
    PROF_CPU();

    BOOL bRet = false;
    //	shaderStage = {};
    //	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    //	shaderStage.stage = stage;
    //
    //	shaderStage.pNext = nullptr;
    //	shaderStage.flags = 0;
    //	shaderStage.pSpecializationInfo = nullptr;
    //	shaderStage.pName = "main"; // todo : make param
    //
    //
    ////#if  defined(VK_USE_PLATFORM_IOS_MVK)
    ////	VkShaderModule &module = shaderStage.module;
    ////#else
    //	VkShaderModule& module = shaderStage.module;
    //	//#endif
    //
    //	auto it = m_ShaderModules.find(szShaderName);
    //	if (it != m_ShaderModules.end())
    //	{
    //		VkShaderModule shadermodule = it->second->m_pModule;
    //		module = shadermodule;
    //	}
    //	else
    //	{
    //		module = vks::tools::LoadShader(szShaderName, stage, m_pLogicalDevice, pReflector);
    //		if (module != VK_NULL_HANDLE)
    //		{
    //			KShaderProgram* pProgram = new KShaderProgram;
    //			pProgram->m_pModule = module;
    //			m_ShaderModules.insert(std::pair<std::string, KShaderProgram*>(szShaderName, pProgram));
    //		}
    //		else
    //		{
    //			KGLogPrintf(KGLOG_ERR, "创建shader %s 失败", szShaderName);
    //			goto Exit0;
    //		}
    //	}
    bRet      = true;
    // Exit0:
    return bRet;
}

void PrintToLines(const char* pcszSrc, const std::set<uint32_t>& errorLines)
{
    PROF_CPU();

    KEngineOptions* pEngineOptions = NSEngine::GetEngineOptions();
    if (!pEngineOptions->bDisableShaderErrorLog)
    {
        int   nLen = (int)strlen(pcszSrc);
        char* szContent = new char[nLen + 1];
        char* szContentSt = szContent;
        char* szEnd = NULL;
        strcpy(szContent, pcszSrc);
        szContent[nLen] = '\0';

        int nline = 1;
        while ((szEnd = strstr(szContent, "\n")) != nullptr)
        {
            *szEnd = '\0';


            if (!errorLines.empty() && errorLines.find(nline) != errorLines.end())
            {
                if (nline < 10)
                    KGLogPrintf(KGLOG_ERR, "->%d   %s\r\n", nline, szContent);
                else if (nline < 100)
                    KGLogPrintf(KGLOG_ERR, "->%d  %s\r\n", nline, szContent);
                else
                    KGLogPrintf(KGLOG_ERR, "->%d %s\r\n", nline, szContent);
            }
            else
            {
                if (nline < 10)
                    KGLogPrintf(KGLOG_INFO, "%d   %s\r\n", nline, szContent);
                else if (nline < 100)
                    KGLogPrintf(KGLOG_INFO, "%d  %s\r\n", nline, szContent);
                else
                    KGLogPrintf(KGLOG_INFO, "%d %s\r\n", nline, szContent);
            }
#ifdef __ANDROID__
            // android 刷快了会丢log 出现identical  n lines 这种就是丢行了，下面两条好像并不能执行，貌似手机需要root
            // 既然是刷太快丢行，那么简单加个sleep试了试也能解决问题，既然如此，那先这么处理好了
            // setprop ro.logd.filter disable
            // setprop persist.logd.filter disable
            KSLEEP(2);
#endif
            szContent = szEnd + 1;
            nline++;
        }
        delete[](szContentSt);
    }
}

const char* pcszShaderNameMain = "main";
BOOL        vks::KVulkanDevice::CreateShaderString(
    vks::KShaderProgram**            ppProgram,
    gfx::KShaderInfo*                pShaderInfo,
    const char*                      pcszShaderString,
    const VkShaderStageFlagBits      stage,
    VkPipelineShaderStageCreateInfo& shaderStage,
    gfx::IShaderReflector*           pReflector,
    uint32_t*                        pRetHash,
    BOOL*                            pRealBuild,
    std::function<BOOL(void)>        postCreateCallBack
)
{
    PROF_CPU();

    BOOL bRet         = false;
    shaderStage       = {};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = stage;

    shaderStage.pNext               = nullptr;
    shaderStage.flags               = 0;
    shaderStage.pSpecializationInfo = nullptr;
    if (!pShaderInfo->strEntryPoint.empty())
    {
        shaderStage.pName = GetParamNameByPool(pShaderInfo->strEntryPoint.c_str()); // todo : make param
    }
    else
    {
        shaderStage.pName = pcszShaderNameMain;
    }

    if (m_mapErrorShader.find(pShaderInfo->strGroupkey) != m_mapErrorShader.end())
    {
        goto Exit0;
    }

    {
        std::lock_guard<std::mutex> lock(m_ShaderModules_lock);

        BOOL     bCached  = false;
        uint32_t shaderid = GetGraphicAndComputeShaderId(pShaderInfo->eShaderStage);
        auto     it       = m_ShaderModules[shaderid].find(pShaderInfo->strGroupkey);
        if (it != m_ShaderModules[shaderid].end())
        {
            // KGLogPrintf(KGLOG_ERR, "load cached program..... %p",);
            KShaderProgram* pProgram = it->second;

            // if (!pProgram->m_bDirty)
            //{
            shaderStage.module = pProgram->m_pModule;
            pProgram->AddRef();
            *ppProgram = pProgram;
            bCached    = true;
            //}
            // else
            //{
            //	// 一旦脏了，就要强行从列表移除，即使还有引用计数，也需要从列表移除，新的KShaderResource使用者这边才能触发后面创建反射流程，所以必须重新创建这个shader
            //	// 注意：这里并非完全走池的管理，维护额这个列表，主要是为了fs编译的时候需要FixShaderContent访问vs的shader，存在依赖关系，所以搞的有点复杂
            //	m_ShaderModules[pShaderInfo->eShaderStage].erase(it);
            //	pProgram->Release();
            //}
        }
        if (bCached)
        {
            if (!pReflector->IsReflected(pShaderInfo->eShaderStage))
            {
                PROF_CPU("[KVulkanDevice::CreateShaderString] only refelct shader program");
                // std::set<uint32_t> errorLines;
                // BOOL               bRetCode = vks::tools::OnlyReflectShader(pShaderInfo->strSpvPath.c_str(), pcszShaderString, stage, m_pLogicalDevice, pReflector, errorLines, pRetHash, pRealBuild);
                // KGLOG_PROCESS_ERROR(bRetCode);

                // 统一从 KShaderResourceVK::LoadFromBinData 创建
                std::set<uint32_t> errorLines;
                vks::tools::CreateShaderString(
                    pShaderInfo->strSpvPath.c_str(),
                    pShaderInfo->strEntryPoint.c_str(),
                    pcszShaderString,
                    stage,
                    m_pLogicalDevice,
                    pReflector,
                    errorLines,
                    pRetHash,
                    pRealBuild,
                    false
                );
            }
        }
        else
        {
            PROF_CPU("[KVulkanDevice::CreateShaderString] new shader program");
            if (postCreateCallBack)
            {
                postCreateCallBack();
            }
            std::set<uint32_t> errorLines;
            shaderStage.module = vks::tools::CreateShaderString(
                pShaderInfo->strSpvPath.c_str(),
                pShaderInfo->strEntryPoint.c_str(),
                pcszShaderString,
                stage,
                m_pLogicalDevice,
                pReflector,
                errorLines,
                pRetHash,
                pRealBuild,
                true
            );
            // if (gfx::GetGraphicDevice()->bInitedGraphic)
            if (true)
            {
                if (shaderStage.module != VK_NULL_HANDLE)
                {
                    KShaderProgram* pProgram = new KShaderProgram;
                    // 列表需要单独持有一份，并不完全走池的管理，最终释放还是在release里面delete自己的，m_ShaderModules解析创建的时候，之间有依赖关系，所以这个不完全走池管理，流程有点复杂
                    pProgram->AddRef();
                    *ppProgram                      = pProgram;
                    pShaderInfo->uPushConstantsSize = pReflector->GetPushContentAlign16BytesBlockSize();
                    if (stage == VK_SHADER_STAGE_VERTEX_BIT && !pShaderInfo->uPushConstantsSize && pReflector->GetVsPushConstantSize())
                    {
                        // VS不是反射过来的，直接读的bin文件，那么这里再读一下
                        pShaderInfo->uPushConstantsSize = pReflector->GetVsPushConstantSize();
                    }
                    // pShaderInfo->nMaxBinding = pReflector->GetMaxBinding();
                    pProgram->m_shaderInfo = *pShaderInfo;

                    pProgram->m_pModule = shaderStage.module;
                    // #ifdef _WIN32
                    //           pProgram->m_ShaderString = szShaderString;
                    // #endif
                    m_ShaderModules[GetGraphicAndComputeShaderId(pShaderInfo->eShaderStage)].insert(std::pair<std::string, KShaderProgram*>(pShaderInfo->strGroupkey, pProgram));
                    pProgram->m_uNameHash = KSTR_HELPER::GetHashCodeForString64Bit(pShaderInfo->strGroupkey.c_str(), 0);

                    std::string str       = pShaderInfo->ustrShaderSource.Str();
                    size_t      index0    = str.find_last_of('/') + 1;
                    size_t      index1    = str.find_first_of('.');
                    std::string shortName = str.substr(index0, index1 - index0);

                    if (pShaderInfo->sIncludedShaderLoc.IsValid())
                    {
                        char szPath[MAX_PATH];
                        _ANSIPath(pShaderInfo->sIncludedShaderLoc.GetFilePath().Str(), szPath);
                        std::string str1  = szPath;
                        index0            = str1.find_last_of('/') + 1;
                        index1            = str1.find_first_of('.');
                        shortName        += '[';
                        shortName        += str1.substr(index0, index1 - index0);
                        shortName        += "]_";
                    }
                    else
                    {
                        shortName += "[x]_";
                    }

                    shortName += pShaderInfo->strShaderDef;
#ifdef _WIN32
                    GetVulkanDevice()->SetObjectLabel(shaderStage.module, shortName.c_str());
#else
                    VkDevice device = GetVkDevice();
                    debugmarker::SetShaderModuleName(device, shaderStage.module, shortName.c_str());
#endif

                    // SetObjectLabel(shaderStage.module, )

                    // PrintToLines(szShaderString, errorLines);
                }
                else
                {
                    KGLogPrintf(KGLOG_ERR, "创建shader:%s 失败", pShaderInfo->strSpvPath.c_str());
                    PrintToLines(pcszShaderString, errorLines);

                    m_mapErrorShader.insert({ pShaderInfo->strGroupkey, pShaderInfo->sIncludedShaderLoc.GetFilePath().Str() });
                    NSKBase::KResourceErrorReporter::ReportResourceError("shader", pShaderInfo->sIncludedShaderLoc.GetFilePath().Str(), "");
                    goto Exit0;
                }
            }
            else
            {
                if (errorLines.empty())
                {
                    KShaderProgram* pProgram = new KShaderProgram;
                    // 列表需要单独持有一份，并不完全走池的管理，最终释放还是在release里面delete自己的，m_ShaderModules解析创建的时候，之间有依赖关系，所以这个不完全走池管理，流程有点复杂
                    pProgram->AddRef();
                    *ppProgram                      = pProgram;
                    pShaderInfo->uPushConstantsSize = pReflector->GetPushContentAlign16BytesBlockSize();
                    // pShaderInfo->nMaxBinding = pReflector->GetMaxBinding();
                    pProgram->m_shaderInfo          = *pShaderInfo;

                    pProgram->m_pModule = shaderStage.module;
                    // #ifdef _WIN32
                    //           pProgram->m_ShaderString = szShaderString;
                    // #endif
                    m_ShaderModules[GetGraphicAndComputeShaderId(pShaderInfo->eShaderStage)].insert(std::pair<std::string, KShaderProgram*>(pShaderInfo->strGroupkey, pProgram));
                    pProgram->m_uNameHash = KSTR_HELPER::GetHashCodeForString64Bit(pShaderInfo->strGroupkey.c_str(), 0);
                }
                else
                {
                    KGLogPrintf(KGLOG_ERR, "创建shader:%s 失败", pShaderInfo->strSpvPath.c_str());
                    PrintToLines(pcszShaderString, errorLines);

                    m_mapErrorShader.insert({pShaderInfo->strGroupkey, pShaderInfo->sIncludedShaderLoc.GetFilePath().Str()});
                    NSKBase::KResourceErrorReporter::ReportResourceError("shader", pShaderInfo->sIncludedShaderLoc.GetFilePath().Str(), "");
                    goto Exit0;
                }
            }
        }
    }

    bRet = true;
Exit0:
    return bRet;
}

BOOL vks::KVulkanDevice::_RemoveDirtyShaderModules_NoLock(vks::KShaderProgram* pShaderProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey)
{
    uint32_t shaderType = GetGraphicAndComputeShaderId(eShaderStage);
    auto&    moduleMap  = m_ShaderModules[shaderType];
    auto     it         = moduleMap.find(pGroupKey);
    if (it != moduleMap.end() && it->second == pShaderProgram)
    {
        ASSERT(pShaderProgram->m_bDirty);
        if (pShaderProgram->GetRef() == 1)
        {
            moduleMap.erase(it);
            SAFE_RELEASE(pShaderProgram);
        }
        else if (pShaderProgram->GetRef() == 0)
        {
            moduleMap.erase(it);
        }
    }
    // 删除前，要判断引用计数是否真的是1(m_ShaderModules还有持有一次引用计数)，因为可能删之前又被借出去了，这里就不删除了
    // ASSERT(pShaderProgram->GetRef() > 0);
    // if (pShaderProgram->GetRef() <= 1)
    //{
    //	ASSERT(pShaderProgram->m_bDirty);
    //	// if (strstr(pShaderProgram->m_shaderInfo.strScPath.c_str(), "meshshader") && pShaderProgram->m_shaderInfo.eShaderStage == gfx::SHADER_STAGE_VERTEX_BIT)
    //	//{
    //	//	int x = 0;
    //	// }
    //	uint32_t shaderType = pShaderProgram->m_shaderInfo.eShaderStage;
    //	auto     it         = m_ShaderModules[shaderType].find(pShaderProgram->m_shaderInfo.strGroupkey);
    //	if (it != m_ShaderModules[shaderType].end() && it->second == pShaderProgram)
    //	{
    //		m_ShaderModules[shaderType].erase(it);
    //		SAFE_RELEASE(pShaderProgram);
    //	}
    //}
    return true;
}

BOOL vks::KVulkanDevice::_RemoveDirtyShaderModules_NoLock_MaybeDirtyDeletedByCreate(vks::KShaderProgram* pProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey)
{
    BOOL     bRet     = false;
    uint32_t shaderid = GetGraphicAndComputeShaderId(eShaderStage);
    auto     it       = m_ShaderModules[shaderid].find(pGroupKey);
    if (it != m_ShaderModules[shaderid].end() && it->second == pProgram)
    {
        m_ShaderModules[shaderid].erase(it);
        bRet = true;
    }
    return bRet;
}

BOOL vks::KVulkanDevice::RemoveDirtyShaderModules_MaybeDirtyDeletedByCreate(vks::KShaderProgram* pProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey)
{
    BOOL bRet = false;
    m_ShaderModules_lock.lock();
    uint32_t shaderid = GetGraphicAndComputeShaderId(eShaderStage);
    auto     it       = m_ShaderModules[shaderid].find(pGroupKey);
    if (it != m_ShaderModules[shaderid].end() && it->second == pProgram)
    {
        m_ShaderModules[shaderid].erase(it);
        bRet = true;
    }
    m_ShaderModules_lock.unlock();
    return bRet;
}

BOOL vks::KVulkanDevice::RemoveShaderModule(vks::KShaderProgram* pShaderProgram, gfx::ShaderStageType eShaderStage, const char* pGroupKey)
{
    PROF_CPU();
    BOOL bRet        = false;
    BOOL bMainThread = IsMainThread();

    // if (strstr(pShaderProgram->m_shaderInfo.strScPath.c_str(), "meshshader") && pShaderProgram->m_shaderInfo.eShaderStage == gfx::SHADER_STAGE_VERTEX_BIT)
    //{
    //	int x = 0;
    // }
    if (!bMainThread)
    {
        // 如果不是渲染线程,一定会尝试删除，卡子线程无所谓的
        m_ShaderModules_lock.lock();
        pShaderProgram->m_bDirty = true;
        _RemoveDirtyShaderModules_NoLock(pShaderProgram, eShaderStage, pGroupKey);
        m_ShaderModules_lock.unlock();
    }
    else if (m_ShaderModules_lock.try_lock())
    {
        pShaderProgram->m_bDirty = true;
        // 是主线程，先尝试去加锁，能加成功，就立马删
        _RemoveDirtyShaderModules_NoLock(pShaderProgram, eShaderStage, pGroupKey);
        m_ShaderModules_lock.unlock();
    }
    else
    {
        pShaderProgram->m_bDirty = true;
        // 主线程加锁不成功，那么丢到延时删除列表，这个列表只有主线程能操作，所以不用加锁
        pShaderProgram->AddRef();
        m_delayDeleteShaderProgramList.push_back(pShaderProgram);
    }

    return true;
}


BOOL vks::KVulkanDevice::_DelayRemoveShaderModules(BOOL bForce)
{
    ASSERT(IsMainThread());

    if (bForce)
    {
        // KVulkanDevice 析构走这个流程强制删除
        m_ShaderModules_lock.lock();
        for (auto it : m_delayDeleteShaderProgramList)
        {
            std::string          strGroupkey  = it->m_shaderInfo.strGroupkey;
            gfx::ShaderStageType eShaderStage = it->m_shaderInfo.eShaderStage;
            // BOOL bDirty = it->m_bDirty;
            int32_t              val          = it->Release();
            if (val > 0)
            {
                _RemoveDirtyShaderModules_NoLock(it, eShaderStage, strGroupkey.c_str());
            }
            else
            {
                // 可能是创建流程里面发现pProgram->m_bDirty，主动删除了，导致列表里面没有东西，为了安全还是补一个删除列表成员的逻辑
                _RemoveDirtyShaderModules_NoLock_MaybeDirtyDeletedByCreate(it, eShaderStage, strGroupkey.c_str());
            }
        }
        m_delayDeleteShaderProgramList.clear();
        m_ShaderModules_lock.unlock();
    }
    else if (m_ShaderModules_lock.try_lock())
    {
        // 必须和_DelayRemoveShaderModules接口保持一致在主线程
        // framemove的时候会尝试加锁删除，当前帧锁不到，就等下一帧，总能锁得到，这样就不会卡主线程
        for (auto it : m_delayDeleteShaderProgramList)
        {
            std::string          strGroupkey  = it->m_shaderInfo.strGroupkey;
            gfx::ShaderStageType eShaderStage = it->m_shaderInfo.eShaderStage;
            // BOOL bDirty = it->m_bDirty;
            int32_t              val          = it->Release();
            if (val > 0)
            {
                _RemoveDirtyShaderModules_NoLock(it, eShaderStage, strGroupkey.c_str());
            }
            else
            {
                // 可能是创建流程里面发现pProgram->m_bDirty，主动删除了，导致列表里面没有东西，为了安全还是补一个删除列表成员的逻辑
                _RemoveDirtyShaderModules_NoLock_MaybeDirtyDeletedByCreate(it, eShaderStage, strGroupkey.c_str());
            }
        }
        m_delayDeleteShaderProgramList.clear();
        m_ShaderModules_lock.unlock();
    }
    return true;
}


void vks::KVulkanDevice::FrameMove()
{
    PROF_CPU();
    if (DrvOption::bX3D_VK_USE_VMA)
    {
        int nLoopCount = NSEngine::GetRenderFrameMoveLoopCount();
        vmaSetCurrentFrameIndex(m_pVMAllocator, (uint32_t)nLoopCount);
    }

    _DelayRemoveShaderModules(false);
}

BOOL vks::KVulkanDevice::RefreshShaderModules()
{
    // const char* pKey = "data/shaders/glsl/water___64@water_color.frag";
    // auto it = m_ShaderModules.find(pKey);
    // if (it != m_ShaderModules.end())
    //{
    //	KShaderProgram* pShaderProgram = it->second;
    //	if (pShaderProgram->m_pModule)
    //	{
    //		vkDestroyShaderModule(m_pLogicalDevice, pShaderProgram->m_pModule, nullptr);
    //		pShaderProgram->m_pModule = nullptr;
    //	}
    // }

    return true;
}

BOOL vks::KVulkanDevice::ReCreateShaderString(gfx::KShaderInfo* pShaderInfo, const char* szShaderString, const VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStage, gfx::IShaderReflector* pReflector, uint32_t* pRetHash, BOOL* pRealBuild, std::function<BOOL(void)> postCreateCallBack)
{
    BOOL bRet         = false;
    shaderStage       = {};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = stage;

    shaderStage.pNext               = nullptr;
    shaderStage.flags               = 0;
    shaderStage.pSpecializationInfo = nullptr;
    shaderStage.pName               = "main"; // todo : make param
    const char* szShaderName        = pShaderInfo->strSpvPath.c_str();

    std::set<uint32_t> errorLines;
    VkShaderModule     module = vks::tools::CreateShaderString(
        szShaderName,
        pShaderInfo->strEntryPoint.c_str(),
        szShaderString,
        stage,
        m_pLogicalDevice,
        pReflector,
        errorLines,
        pRetHash,
        pRealBuild,
        TRUE
    );

    if (module != VK_NULL_HANDLE)
    {
        std::lock_guard<std::mutex> lock(m_ShaderModules_lock);
        pShaderInfo->uPushConstantsSize = pReflector->GetPushContentAlign16BytesBlockSize();
        // pShaderInfo->nMaxBinding = pReflector->GetMaxBinding();

        if (postCreateCallBack)
        {
            postCreateCallBack();
        }

        char                 groupkeyName[NSEngine::MAX_PATH_LEN] = "";
        const char*          p                                    = nullptr;
        gfx::ShaderStageType type                                 = gfx::GetShaderTypeFromName(szShaderName);
        uint32_t             shaderid                             = GetGraphicAndComputeShaderId(type);
        KGLOG_PROCESS_ERROR(shaderid < SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT);
        p = strchr(szShaderName, '#');
        KGLOG_PROCESS_ERROR(p);

        g_StrCpyLen(groupkeyName, szShaderName, countof(groupkeyName));
        groupkeyName[p - szShaderName] = '\0';

        auto it = m_ShaderModules[shaderid].find(groupkeyName);
        if (it != m_ShaderModules[shaderid].end())
        {
            KShaderProgram* pProgram = it->second;
            vks::vkDestroyShaderModule(m_pLogicalDevice, pProgram->m_pModule, nullptr);
            pProgram->m_pModule = module;
            // #ifdef _WIN32
            //			pProgram->m_ShaderString = szShaderString;
            // #endif

            pProgram->m_shaderInfo = *pShaderInfo;
        }
        else
        {
            KShaderProgram* pProgram = new KShaderProgram;
            pProgram->m_pModule      = module;
            // #ifdef _WIN32
            //			pProgram->m_ShaderString = szShaderString;
            // #endif
            pProgram->m_shaderInfo   = *pShaderInfo;
            m_ShaderModules[shaderid].insert(std::make_pair<>(groupkeyName, pProgram));
        }

        shaderStage.module = module;
    }
    else
    {
        KGLogPrintf(KGLOG_ERR, "创建shader:%s 失败", szShaderName);
        PrintToLines(szShaderString, errorLines);
        goto Exit0;
    }

    bRet = true;
Exit0:
    return bRet;
}


#define SHADER_STAGE_UNKNOWN                         "unknown"
#define SHADER_STAGE_VERTEX_BIT_STR                  "vertex"
#define SHADER_STAGE_TESSELLATION_CONTROL_BIT_STR    "tessellation_control"
#define SHADER_STAGE_TESSELLATION_EVALUATION_BIT_STR "tessellation_evaluation"
#define SHADER_STAGE_GEOMETRY_BIT_STR                "geometry"
#define SHADER_STAGE_FRAGMENT_BIT_STR                "fragment"
#define SHADER_STAGE_COMPUTE_BIT_STR                 "compute"
const char* GetShaderTypeStr(gfx::ShaderStageType shaderType)
{
    const char* pRet = SHADER_STAGE_UNKNOWN;
    switch (shaderType)
    {
    case gfx::ShaderStageType::Vertex:
        pRet = SHADER_STAGE_VERTEX_BIT_STR;
        break;
    case gfx::ShaderStageType::Hull:
        pRet = SHADER_STAGE_TESSELLATION_CONTROL_BIT_STR;
        break;
    case gfx::ShaderStageType::Domain:
        pRet = SHADER_STAGE_TESSELLATION_EVALUATION_BIT_STR;
        break;
    case gfx::ShaderStageType::Geometry:
        pRet = SHADER_STAGE_GEOMETRY_BIT_STR;
        break;
    case gfx::ShaderStageType::Fragment:
        pRet = SHADER_STAGE_FRAGMENT_BIT_STR;
        break;
    case gfx::ShaderStageType::Compute:
        pRet = SHADER_STAGE_COMPUTE_BIT_STR;
        break;
    default:
        break;
    }
    return pRet;
}

void _ssplitbySpace(const char* str, std::vector<std::string>& list1)
{
    std::istringstream       ss(str);
    std::vector<std::string> words;
    std::string              word;
    while (ss >> word)
    {
        list1.push_back(word);
    }
}

BOOL GetUniformBlockName(std::string& strShaderContent, std::string::size_type pos, std::string& blockName)
{
    BOOL bRet = false;
    char line[1024];

    std::vector<std::string> tokens;
    uint32_t                 posBegin = (uint32_t)strShaderContent.find(")", pos);

    uint32_t    posLineEnd = (uint32_t)strShaderContent.find("\n", pos);
    const char* p          = strShaderContent.c_str() + posBegin;
    uint32_t    uCount     = posLineEnd - posBegin;
    size_t      sz;
    const char* pBlockName = nullptr;

    KGLOG_PROCESS_ERROR(uCount < 1024);

    strncpy(line, p, uCount);
    line[uCount] = '\0';

    _ssplitbySpace(line, tokens);
    sz = tokens.size();
    // KG_PROCESS_ERROR(sz >= 3 && sz < 100);


    if (sz >= 3 && tokens[1] == "uniform")
    {
        pBlockName = tokens[2].c_str(); // uniform xxx
    }
    else if (sz >= 3 && tokens[1] == "buffer")
    {
        pBlockName = tokens[2].c_str(); // buffer xxx
    }
    else if (sz >= 4 && tokens[2] == "buffer")
    {
        pBlockName = tokens[3].c_str(); // maybe  readonly buffer xxx
    }

    //&& tokens[1] == "uniform");
    // pBlockName = tokens[2].c_str();

    KG_PROCESS_ERROR(pBlockName);

    if (strstr(pBlockName, "texture") == pBlockName)
        goto Exit0;

    if (strstr(pBlockName, "sampler") == pBlockName)
        goto Exit0;

    if (strstr(pBlockName, "mediump") == pBlockName)
        goto Exit0;

    if (strstr(pBlockName, "highp") == pBlockName)
        goto Exit0;

    if (strstr(pBlockName, "lowp") == pBlockName)
        goto Exit0;

    blockName = pBlockName;


    bRet = true;
Exit0:
    return bRet;
}

BOOL FixShaderContent(
    std::map<std::string, vks::KShaderProgram*> ShaderModules[],
    gfx::KShaderInfo*                           pShaderInfo,
    std::string&                                strShaderContent,
    gfx::IShaderReflector*                      pReflector
)
{
    PROF_CPU();

    BOOL bResult  = false;
    BOOL bRetCode = false;

    pShaderInfo->uPushConstantsOffset = 0;

    int32_t                maxBinding = -1;
    std::string::size_type pos(0);
    char                   size_str[8] = "";

    if (pShaderInfo->eShaderStage == gfx::ShaderStageType::Fragment)
    {
        auto&       modules = ShaderModules[GetGraphicAndComputeShaderId(gfx::ShaderStageType::Vertex)];
        const auto& it      = modules.find(pShaderInfo->strGroupkey);

        if (it != modules.end())
        {
            vks::KShaderProgram* pPragram     = it->second;
            pShaderInfo->uPushConstantsOffset = pPragram->m_shaderInfo.uPushConstantsSize;
            maxBinding                        = pPragram->m_shaderInfo.nMaxBinding;
            // if (pShaderInfo->uPushConstantsOffset)
            {
                snprintf(size_str, 8, "%d", pShaderInfo->uPushConstantsOffset);
                pos = strShaderContent.find("auto_push", pos);
                if (pos != std::string::npos)
                {
                    strShaderContent.replace(pos, strlen("auto_push"), size_str);
                }
            }
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "create vertex shader %s first", pShaderInfo->strGroupkey.c_str());
            goto Exit0;
        }
    }

    pos = 0;
    while ((pos = strShaderContent.find("auto_bind", pos)) != std::string::npos)
    {
#if MERGE_UNIFORM_BINDING
        std::string blockName;
        bRetCode = GetUniformBlockName(strShaderContent, pos, blockName);
        if (bRetCode && !blockName.empty())
        {
            int32_t binding = -1;
            if (pReflector->FindBindingForFixShaderContent(blockName.c_str(), binding))
            {
                snprintf(size_str, 8, "%d", binding);
                strShaderContent.replace(pos, strlen("auto_bind"), size_str);
            }
            else
            {
                snprintf(size_str, 8, "%d", ++maxBinding);
                strShaderContent.replace(pos, strlen("auto_bind"), size_str);
                pShaderInfo->nMaxBinding = maxBinding;
                pReflector->InsertBindingForFixShaderContent(blockName.c_str(), maxBinding);
            }
        }
        else
        {
            snprintf(size_str, 8, "%d", ++maxBinding);
            strShaderContent.replace(pos, strlen("auto_bind"), size_str);
            pShaderInfo->nMaxBinding = maxBinding;
        }
#else
        snprintf(size_str, 8, "%d", ++maxBinding);
        strShaderContent.replace(pos, strlen("auto_bind"), size_str);
        pShaderInfo->nMaxBinding = maxBinding;
#endif
    }

    if (pShaderInfo->eShaderStage == gfx::ShaderStageType::Fragment)
    {
        // 到这里shaderinfo才拥有完整的内容，因为uPushConstantOffset是从外面之前的 shader create 以后才知道的
        pShaderInfo->szShaderContentHash = KSTR_HELPER::GetHashCodeForString32Bit(strShaderContent.c_str());
    }

    bResult = true;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::CreateShader(
    vks::KShaderProgram**            ppProgram,
    gfx::KShaderStage*               pShaderStage,
    VkPipelineShaderStageCreateInfo& retShaderStage,
    uint32_t*                        pRetHash,
    BOOL*                            pRealBuild,
    gfx::IShaderReflector*           pReflector
)
{
    PROF_CPU();

    BOOL bRet     = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_ASSERT_EXIT(pShaderStage);
    KGLOG_ASSERT_EXIT(pReflector);

    {
        gfx::KShaderInfo*         pShaderInfo        = pShaderStage->GetShaderInfo();
        KEngineOptions*           pEngineOptions     = NSEngine::GetEngineOptions();
        KEngineSwitchOption*      pEngineSwitch      = NSEngine::GetEngineSwitchOptions();
        VkShaderStageFlagBits     vkShaderStage      = VkShaderStageFlagBits::VK_SHADER_STAGE_ALL;
        std::function<BOOL(void)> postCreateCallBack = nullptr;

        pShaderInfo = pShaderStage->GetShaderInfo();
        KGLOG_ASSERT_EXIT(pShaderInfo);
        vkShaderStage = GetShaderStageFlag(pShaderInfo->eShaderStage);

        if (!pShaderStage->IsShaderFileLoadFromCache() && !pReflector->IsHLSL())
        {
            std::string strShaderBody         = pShaderStage->GetShaderBody();
            std::string strShaderFileSaveData = pShaderStage->GetShaderFileSaveData();

            // KGLOG_ASSERT_EXIT(!strShaderBody.empty() && !strShaderFileSaveData.empty() && pShaderInfo);
            KGLOG_ASSERT_EXIT(!strShaderBody.empty() && pShaderInfo);
            {
                std::lock_guard<std::mutex> _lock(m_ShaderModules_lock);
                bRetCode = FixShaderContent(m_ShaderModules, pShaderInfo, strShaderBody, pReflector);
                KGLOG_PROCESS_ERROR(bRetCode);
            }
            pShaderStage->SetShaderBody(std::move(strShaderBody));
        }

        postCreateCallBack = [pShaderInfo, pReflector]() -> BOOL {
            if (DrvOption::bLogShader)
            {
                if (pShaderInfo->eShaderStage == gfx::ShaderStageType::Fragment)
                {
                    char logtext[NSEngine::MAX_PATH_LEN] = "";
                    snprintf(
                        logtext,
                        countof(logtext),
                        "createShader:%s, Main:%s, Include:%s, Def:%s, Macro:%s, PushConstansOffset:%d\r\n\r\n",
                        GetShaderTypeStr(pShaderInfo->eShaderStage),
                        pShaderInfo->ustrShaderSource.Str(),
                        _ANSIPath(pShaderInfo->sIncludedShaderLoc.GetFilePath().Str()),
                        pShaderInfo->strShaderDef.c_str(),
                        pShaderInfo->strMacro.c_str(),
                        pReflector->GetVsPushConstantSize()
                    );
                    KGLogPrintf(KGLOG_WARNING, "X3DEngine: %s", logtext);

                    // #ifdef _WIN32
                    // g_shaderCompileRecord.Recorder(logtext);
                    // #endif // _WIN32
                }
                else
                {
                    char logtext[NSEngine::MAX_PATH_LEN] = "";
                    snprintf(
                        logtext,
                        countof(logtext),
                        "createShader:%s, Main:%s, Include:%s, Def:%s, Macro:%s, PushConstansOffset:%d\r\n",
                        GetShaderTypeStr(pShaderInfo->eShaderStage),
                        pShaderInfo->ustrShaderSource.Str(),
                        _ANSIPath(pShaderInfo->sIncludedShaderLoc.GetFilePath().Str()),
                        pShaderInfo->strShaderDef.c_str(),
                        pShaderInfo->strMacro.c_str(),
                        0
                    );
                    KGLogPrintf(KGLOG_WARNING, "X3DEngine: %s", logtext);

                    // #ifdef _WIN32
                    // g_shaderCompileRecord.Recorder(logtext);
                    // #endif // _WIN32
                }
            }

            return true;
        };

        if (!pEngineOptions->IsLoadShaderSPV())
        {
            const std::string& strShaderContent = pShaderStage->GetShaderContent();

            bRetCode = CreateShaderString(
                ppProgram,
                pShaderInfo,
                strShaderContent.c_str(),
                vkShaderStage,
                retShaderStage,
                pReflector,
                pRetHash,
                pRealBuild,
                postCreateCallBack
            );
            pShaderInfo->uPushConstantsSize   = pReflector->GetPushContentAlign16BytesBlockSize();
            // pShaderInfo->nMaxBinding = pReflector->GetMaxBinding();
            pShaderInfo->uPushConstantsOffset = pReflector->GetVsPushConstantSize();
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        else
        {
            bRetCode = CreateShaderSpv(pShaderInfo->strSpvPath.c_str(), vkShaderStage, retShaderStage, pReflector);
            KGLOG_PROCESS_ERROR(bRetCode);
        }
    }

    bRet = TRUE;
Exit0:
    return bRet;
}

BOOL vks::KVulkanDevice::ReCreateShader(
    gfx::KShaderInfo*                pShaderInfo,
    std::string&                     strShaderContent,
    VkPipelineShaderStageCreateInfo& retShaderStage,
    uint32_t*                        pRetHash,
    BOOL*                            pRealBuild,
    gfx::IShaderReflector*           pReflector
)
{
    BOOL                      bRet               = FALSE;
    BOOL                      bRetCode           = FALSE;
    VkShaderStageFlagBits     stage              = GetShaderStageFlag(pShaderInfo->eShaderStage);
    KEngineOptions*           pEngineOptions     = NSEngine::GetEngineOptions();
    std::function<BOOL(void)> postCreateCallBack = nullptr;

    m_ShaderModules_lock.lock();
    bRetCode = FixShaderContent(m_ShaderModules, pShaderInfo, strShaderContent, pReflector);
    m_ShaderModules_lock.unlock();
    KGLOG_PROCESS_ERROR(bRetCode);

    postCreateCallBack = [pShaderInfo]() -> BOOL {
        if (pShaderInfo->eShaderStage == gfx::ShaderStageType::Fragment)
        {
            char logtext[NSEngine::MAX_PATH_LEN] = "";
            snprintf(
                logtext,
                countof(logtext),
                "ReCreateShader:%s, Main:%s, Include:%s, Def:%s, Macro:%s, PushConstansOffset:%d\r\n\r\n",
                GetShaderTypeStr(pShaderInfo->eShaderStage),
                pShaderInfo->ustrShaderSource.Str(),
                _ANSIPath(pShaderInfo->sIncludedShaderLoc.GetFilePath().Str()),
                pShaderInfo->strShaderDef.c_str(),
                pShaderInfo->strMacro.c_str(),
                pShaderInfo->uPushConstantsOffset
            );
            KGLogPrintf(KGLOG_WARNING, "X3DEngine: %s", logtext);
        }
        else
        {
            char logtext[NSEngine::MAX_PATH_LEN] = "";
            snprintf(
                logtext,
                countof(logtext),
                "ReCreateShader:%s, Main:%s, Include:%s, Def:%s, Macro:%s, PushConstansOffset:%d\r\n",
                GetShaderTypeStr(pShaderInfo->eShaderStage),
                pShaderInfo->ustrShaderSource.Str(),
                _ANSIPath(pShaderInfo->sIncludedShaderLoc.GetFilePath().Str()),
                pShaderInfo->strShaderDef.c_str(),
                pShaderInfo->strMacro.c_str(),
                pShaderInfo->uPushConstantsOffset
            );
            KGLogPrintf(KGLOG_WARNING, "X3DEngine: %s", logtext);
        }
        return true;
    };

    if (!pEngineOptions->IsLoadShaderSPV())
    {
        bRetCode = ReCreateShaderString(pShaderInfo, strShaderContent.c_str(), stage, retShaderStage, pReflector, pRetHash, pRealBuild, postCreateCallBack);
        KGLOG_PROCESS_ERROR(bRetCode);
    }
    else
    {
        KGLogPrintf(KGLOG_WARNING, "dose not support reCreate with spv");
    }

    bRet = TRUE;
Exit0:
    return bRet;
}

void BuildMacro(const char* pcszMacro, std::string& strMacroDefine)
{
    PROF_CPU();

    std::string strMacros = pcszMacro;
    KSTR_HELPER::ReplaceStr(strMacros, "=", " ");

    std::vector<std::string> vecDefineStr;
    KSTR_HELPER::StrSplit(strMacros.c_str(), ";", vecDefineStr);

    strMacroDefine.clear();
    char szDefineLine[MAX_PATH] = "";
    for (std::string& strDefine : vecDefineStr)
    {
        snprintf(szDefineLine, countof(szDefineLine) - 1, "#define %s\r\n", strDefine.c_str());
        szDefineLine[countof(szDefineLine) - 1] = 0;
        strMacroDefine.append(szDefineLine);
    }
}

void PrintToLines3(const char* pcszSrc)
{
    PROF_CPU();

    int   nLen        = (int)strlen(pcszSrc);
    char* szContent   = new char[nLen + 1];
    char* szContentSt = szContent;
    char* szEnd       = NULL;
    strcpy(szContent, pcszSrc);
    szContent[nLen] = '\0';

    int nline = 1;
    while ((szEnd = strstr(szContent, "\n")) != nullptr)
    {
        *szEnd = '\0';
        {
            if (nline < 10)
                KGLogPrintf(KGLOG_INFO, "%d   %s\r\n", nline, szContent);
            else if (nline < 100)
                KGLogPrintf(KGLOG_INFO, "%d  %s\r\n", nline, szContent);
            else
                KGLogPrintf(KGLOG_INFO, "%d %s\r\n", nline, szContent);
        }

        szContent = szEnd + 1;
        nline++;
    }
    delete[] (szContentSt);
}

BOOL vks::KVulkanDevice::LoadShaderWithoutTech(
    gfx::KShaderStage* pShaderStage,
    const char* pMainShader,
    const char* pEnterPoint,
    const NSKBase::tagFileLocation& sUserShaderLoc,
    const char* pcszMacro,
    gfx::ShaderStageType eShaderStage,
    gfx::IShaderReflector* pReflector
)
{
    PROF_CPU();
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    uint32_t uSamplerCount = 0;
    gfx::IKGFX_CombinedShaderResult* pCombinedResult = nullptr;
    gfx::KGFX_ShaderFilePool* pShaderFilePool = gfx::KGFX_GetShaderFilePool();
    gfx::KGFX_ShaderTechItem* pShaderTechItem = pShaderFilePool->RequestFromShaderFile(
        eShaderStage, pMainShader, sUserShaderLoc, nullptr
    );
    KGLOG_PROCESS_ERROR(pShaderTechItem);
    pShaderTechItem->m_szEntryPoint = pEnterPoint;

    pCombinedResult = pReflector->GetCombindShaderResult();
    bRetCode = pShaderTechItem->CombineShader(pcszMacro, pCombinedResult);
    KGLOG_PROCESS_ERROR(bRetCode);

    pShaderStage->SetShaderContent("", "", pCombinedResult->GetShaderResult(eShaderStage));
    pShaderStage->SetEntryPoint(pShaderTechItem->GetEntryPoint());

    if (DrvOption::bEnableShaderSamplerStateFix)
    {
        uSamplerCount = pCombinedResult->GetSamplerCount(eShaderStage);
        for (uint32_t i = 0; i < uSamplerCount; ++i)
        {
            char pName[256];
            pName[0] = '\0';
            gfx::KSamplerState* pSamplerState = nullptr;
            pCombinedResult->GetSamplerState(eShaderStage, i, &pSamplerState, pName, 256);
            pShaderStage->AddSamplerDef(pName, pSamplerState);
        }
    }
    SAFE_RELEASE(pShaderTechItem);
    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL vks::KVulkanDevice::LoadShader(
    gfx::KShaderStage*              pShaderStage,
    const char*                     pcszShaderSource,
    const NSKBase::tagFileLocation& sIncludeShaderLoc,
    const char*                     pcszShaderDef,
    const char*                     pcszMacro,
    gfx::ShaderStageType            eShaderStage,
    gfx::IShaderReflector*          pReflector,
    BOOL                            bForceReload

)
{
    PROF_CPU();

    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;

    KEngineOptions*         pEngineOptions         = NSEngine::GetEngineOptions();
    KEngineSwitchOption*    pEngineSwitch          = NSEngine::GetEngineSwitchOptions();
    const char*             pcszSpecificShaderDef  = nullptr;
    std::string             strMacroFull;

    KG_PROCESS_SUCCESS(pEngineOptions->IsLoadShaderSPV());

    BuildMacro(pcszMacro, strMacroFull);
    KGLOG_ASSERT_EXIT(strstr(pcszShaderSource, ".jsontech"));

    {
        char techPath[MAX_PATH];
        snprintf(techPath, MAX_PATH, "%s/%s", TECH_ROOT_DIR, pcszShaderSource);
        gfx::IKGFX_CombinedShaderResult* pCombinedResult = pReflector->GetCombindShaderResult();

        if (eShaderStage == gfx::ShaderStageType::Fragment)
        {
            pCombinedResult->SetPreviousPushConstsSize(pReflector->GetVsPushConstantSize());
        }

        std::string               szCombineShaderContent;
        gfx::KGFX_ShaderFilePool* pShaderFilePool = gfx::KGFX_GetShaderFilePool();

        gfx::KGFX_ShaderTechItem* pShaderTechItem = pShaderFilePool->RequestFromTechFile(
            eShaderStage, techPath, pcszShaderDef, sIncludeShaderLoc
        );
        KGLOG_PROCESS_ERROR(pShaderTechItem);

        bRetCode = pShaderTechItem->CombineShader(pcszMacro, pCombinedResult);
        KGLOG_PROCESS_ERROR(bRetCode);
        // const char* header = "#version 450\r\n"
        //	"#extension GL_ARB_separate_shader_objects : enable\r\n"
        //	"#extension GL_ARB_shading_language_420pack : enable\r\n"
        //	"#define SHADER_API 450\r\n";

        pShaderStage->SetShaderContent("", "", pCombinedResult->GetShaderResult(eShaderStage));
        pShaderStage->SetEntryPoint(pShaderTechItem->GetEntryPoint());

        if(DrvOption::bEnableShaderSamplerStateFix)
        {
            uint32_t uSamplerCount = pCombinedResult->GetSamplerCount(eShaderStage);
            for (uint32_t i = 0; i < uSamplerCount; ++i)
            {
                char pName[256];
                pName[0]                          = '\0';
                gfx::KSamplerState* pSamplerState = nullptr;
                pCombinedResult->GetSamplerState(eShaderStage, i, &pSamplerState, pName, 256);
                pShaderStage->AddSamplerDef(pName, pSamplerState);
            }
        }

        SAFE_RELEASE(pShaderTechItem);
    }

Exit1:
    bResult = TRUE;
Exit0:
    if (!bResult)
    {
        KGLogPrintf(KGLOG_ERR, "load %s %s failed", pcszShaderSource, sIncludeShaderLoc.GetFilePath().Str());
    }
    return bResult;
}
