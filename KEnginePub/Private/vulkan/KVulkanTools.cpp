#include <string.h>
#include "KVulkanTools.h"
#include "KVulkanInitializers.h"
#include "Engine/KGLog.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/io/KBFReader.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/thread/KThread.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KVulkanDevice.h"
#include "KVulkanDebug.h"
#include "KVulkanGraphicDevice.h"
#include "KSpirv/Public/SpirvFileInterface.h"
#include "Engine/File.h"
#include "KSpirv/Private/KSpirvBuilder.h"

#include <mutex>
#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#endif
#include "KBase/Public/str/KEncoding.h"

#ifdef _WIN32
#include "../recorder/KShaderRecorder.h"
#endif // _WIN32

//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

namespace vks
{
    namespace tools
    {
        bool errorModeSilent = false;

        std::string ErrorString(VkResult errorCode)
        {
            switch (errorCode)
            {
#define STR(r)   \
    case VK_##r: \
        return #r
                STR(NOT_READY);
                STR(TIMEOUT);
                STR(EVENT_SET);
                STR(EVENT_RESET);
                STR(INCOMPLETE);
                STR(ERROR_OUT_OF_HOST_MEMORY);
                STR(ERROR_OUT_OF_DEVICE_MEMORY);
                STR(ERROR_INITIALIZATION_FAILED);
                STR(ERROR_DEVICE_LOST);
                STR(ERROR_MEMORY_MAP_FAILED);
                STR(ERROR_LAYER_NOT_PRESENT);
                STR(ERROR_EXTENSION_NOT_PRESENT);
                STR(ERROR_FEATURE_NOT_PRESENT);
                STR(ERROR_INCOMPATIBLE_DRIVER);
                STR(ERROR_TOO_MANY_OBJECTS);
                STR(ERROR_FORMAT_NOT_SUPPORTED);
                STR(ERROR_SURFACE_LOST_KHR);
                STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
                STR(SUBOPTIMAL_KHR);
                STR(ERROR_OUT_OF_DATE_KHR);
                STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
                STR(ERROR_VALIDATION_FAILED_EXT);
                STR(ERROR_INVALID_SHADER_NV);
#undef STR
            default:
                return "UNKNOWN_ERROR";
            }
        }

        std::string PhysicalDeviceTypeString(VkPhysicalDeviceType type)
        {
            switch (type)
            {
#define STR(r)                        \
    case VK_PHYSICAL_DEVICE_TYPE_##r: \
        return #r
                STR(OTHER);
                STR(INTEGRATED_GPU);
                STR(DISCRETE_GPU);
                STR(VIRTUAL_GPU);
#undef STR
            default:
                return "UNKNOWN_DEVICE_TYPE";
            }
        }

        VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat)
        {
            // Since all depth formats may be optional, we need to find a suitable depth format to use
            // Start with the highest precision packed format
            std::vector<VkFormat> depthFormats = {

                VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D16_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM
            };

            for (auto& format : depthFormats)
            {
                VkFormatProperties formatProps;
                vks::vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
                // Format must support depth stencil attachment for optimal tiling
                if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                {
                    *depthFormat = format;
                    return true;
                }
            }

            return false;
        }

        // Create an image memory barrier for changing the layout of
        // an image and put it into an active command buffer
        // See chapter 11.4 "Image Layout" for details

        void SetImageLayout(
            VkCommandBuffer         cmdbuffer,
            VkImage                 image,
            VkImageLayout           oldImageLayout,
            VkImageLayout           newImageLayout,
            VkImageSubresourceRange subresourceRange,
            VkPipelineStageFlags    srcStageMask,
            VkPipelineStageFlags    dstStageMask
        )
        {
            // Create an image barrier object
            VkImageMemoryBarrier imageMemoryBarrier = vks::initializers::ImageMemoryBarrier();
            imageMemoryBarrier.oldLayout            = oldImageLayout;
            imageMemoryBarrier.newLayout            = newImageLayout;
            imageMemoryBarrier.image                = image;
            imageMemoryBarrier.subresourceRange     = subresourceRange;

            // Source layouts (old)
            // Source access mask controls actions that have to be finished on the old layout
            // before it will be transitioned to the new layout
            switch (oldImageLayout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                // Image layout is undefined (or does not matter)
                // Only valid as initial layout
                // No flags required, listed only for completeness
                imageMemoryBarrier.srcAccessMask = 0;
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                // Image is preinitialized
                // Only valid as initial layout for linear images, preserves memory contents
                // Make sure host writes have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                // Image is a color attachment
                // Make sure any writes to the color buffer have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                // Image is a depth/stencil attachment
                // Make sure any writes to the depth/stencil buffer have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Image is a transfer source
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Image is a transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Image is read by a shader
                // Make sure any shader reads from the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;

            default:
                // Other source layouts aren't handled (yet)
                break;
            }

            // Target layouts (new)
            // Destination access mask controls the dependency for the new image layout
            switch (newImageLayout)
            {
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Image will be used as a transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Image will be used as a transfer source
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                // Image will be used as a color attachment
                // Make sure any writes to the color buffer have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                // Image layout will be used as a depth/stencil attachment
                // Make sure any writes to depth/stencil buffer have been finished
                imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Image will be read in a shader (sampler, input attachment)
                // Make sure any writes to the image have been finished
                if (imageMemoryBarrier.srcAccessMask == 0)
                {
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                }
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                break;

            default:
                // Other source layouts aren't handled (yet)
                break;
            }

            // Put barrier inside setup command buffer
            vks::vkCmdPipelineBarrier(
                cmdbuffer,
                srcStageMask,
                dstStageMask,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier
            );
        }

        // Fixed sub resource on first mip level and layer
        void SetImageLayout(
            VkCommandBuffer      cmdbuffer,
            VkImage              image,
            VkImageAspectFlags   aspectMask,
            VkImageLayout        oldImageLayout,
            VkImageLayout        newImageLayout,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        )
        {
            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask              = aspectMask;
            subresourceRange.baseMipLevel            = 0;
            subresourceRange.levelCount              = 1;
            subresourceRange.layerCount              = 1;
            SetImageLayout(cmdbuffer, image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
        }


        void SetImageLayout(
            VkCommandBuffer         cmdBuffer,
            VkImage                 image,
            VkImageAspectFlags      aspectMask,
            VkImageLayout           oldImageLayout,
            VkImageLayout           newImageLayout,
            VkImageSubresourceRange subresourceRange
        )
        {
            // Create an image barrier object
            VkImageMemoryBarrier imageMemoryBarrier = vks::initializers::ImageMemoryBarrier();
            ;
            imageMemoryBarrier.oldLayout        = oldImageLayout;
            imageMemoryBarrier.newLayout        = newImageLayout;
            imageMemoryBarrier.image            = image;
            imageMemoryBarrier.subresourceRange = subresourceRange;

            // Only sets masks for layouts used in this example
            // For a more complete version that can be used with other layouts see vks::tools::setImageLayout

            // Source layouts (old)
            switch (oldImageLayout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                // Only valid as initial layout, memory contents are not preserved
                // Can be accessed directly, no source dependency required
                imageMemoryBarrier.srcAccessMask = 0;
                break;
            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                // Only valid as initial layout for linear images, preserves memory contents
                // Make sure host writes to the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Old layout is transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            }

            // Target layouts (new)
            switch (newImageLayout)
            {
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Transfer source (copy, blit)
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Transfer destination (copy, blit)
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Shader read (sampler, input attachment)
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            }

            // Put barrier on top of pipeline
            VkPipelineStageFlags srcStageFlags  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

            // Put barrier inside setup command buffer
            vks::vkCmdPipelineBarrier(
                cmdBuffer,
                srcStageFlags,
                destStageFlags,
                VK_FLAGS_NONE,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier
            );
        }

        void InsertImageMemoryBarrier(
            VkCommandBuffer         cmdbuffer,
            VkImage                 image,
            VkAccessFlags           srcAccessMask,
            VkAccessFlags           dstAccessMask,
            VkImageLayout           oldImageLayout,
            VkImageLayout           newImageLayout,
            VkPipelineStageFlags    srcStageMask,
            VkPipelineStageFlags    dstStageMask,
            VkImageSubresourceRange subresourceRange
        )
        {
            VkImageMemoryBarrier imageMemoryBarrier = vks::initializers::ImageMemoryBarrier();
            imageMemoryBarrier.srcAccessMask        = srcAccessMask;
            imageMemoryBarrier.dstAccessMask        = dstAccessMask;
            imageMemoryBarrier.oldLayout            = oldImageLayout;
            imageMemoryBarrier.newLayout            = newImageLayout;
            imageMemoryBarrier.image                = image;
            imageMemoryBarrier.subresourceRange     = subresourceRange;

            vks::vkCmdPipelineBarrier(
                cmdbuffer,
                srcStageMask,
                dstStageMask,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier
            );
        }

        void InsertBufferMemoryBarrier(
            VkCommandBuffer      cmdbuffer,
            VkBuffer             buffer,
            VkAccessFlags        srcAccessMask,
            VkAccessFlags        dstAccessMask,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask,
            VkDeviceSize         offset,
            VkDeviceSize         size
        )
        {
            VkBufferMemoryBarrier bufferMemoryBarrier = vks::initializers::BufferMemoryBarrier();
            bufferMemoryBarrier.srcAccessMask         = srcAccessMask;
            bufferMemoryBarrier.dstAccessMask         = dstAccessMask;
            bufferMemoryBarrier.buffer                = buffer;
            bufferMemoryBarrier.offset                = offset;
            bufferMemoryBarrier.size                  = size;

            vks::vkCmdPipelineBarrier(
                cmdbuffer,
                srcStageMask,
                dstStageMask,
                0,
                0, nullptr,
                1, &bufferMemoryBarrier,
                0, nullptr
            );
        }

        void InsertMemoryBarrier(VkCommandBuffer cmdbuffer)
        {
            VkMemoryBarrier barrier    = vks::initializers::MemoryBarrier();
            VkAccessFlags   accessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;

            barrier.srcAccessMask = accessMask;
            barrier.dstAccessMask = accessMask;

            vks::vkCmdPipelineBarrier(
                cmdbuffer,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0,
                1, &barrier,
                0, nullptr,
                0, nullptr
            );
        }

        void ExitFatal(std::string message, int32_t exitCode)
        {
#if defined(_WIN32)
            if (!errorModeSilent)
            {
                MessageBox(NULL, message.c_str(), NULL, MB_OK | MB_ICONERROR);
            }
#elif defined(__ANDROID__)
            KGLogPrintf(KGLOG_ERR, "Fatal error: %s", message.c_str());
#endif
            std::cerr << message << "\n";
            exit(exitCode);
        }

        void ExitFatal(std::string message, VkResult resultCode)
        {
            ExitFatal(message, (int32_t)resultCode);
        }

        std::string readTextFile(const char* fileName)
        {
            std::string   fileContent;
            std::ifstream fileStream(fileName, std::ios::in);
            if (!fileStream.is_open())
            {
                printf("File %s not found\n", fileName);
                return "";
            }
            std::string line = "";
            while (!fileStream.eof())
            {
                getline(fileStream, line);
                fileContent.append(line + "\n");
            }
            fileStream.close();
            return fileContent;
        }

        class KSpirvFileImpl : public NSSpirv::ISpirvFileInterface
        {
        public:
            virtual NSSpirv::ISpirvFileHandle OpenFile(const char* pcszFilePath) override
            {
                PROF_CPU();

                IFile* piFile = nullptr;

                KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
                piFile = g_OpenFile(pcszFilePath);

            Exit0:
                return piFile;
            }
            virtual NSSpirv::ISpirvFileHandle OpenFileV5File(const char* pcszFilePath) override
            {
                PROF_CPU();

                IFile* piFile = nullptr;

                KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
                piFile = g_OpenFileInPak(pcszFilePath);

            Exit0:
                return piFile;
            }
            virtual NSSpirv::ISpirvFileHandle OpenAloneFile(const char* pcszFilePath, BOOL bWritable) override // g_OpenAloneFile
            {
                PROF_CPU();

                IFile* piFile = nullptr;

                KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
                if (bWritable)
                {
                    piFile = g_CreateAloneFile(pcszFilePath);
                }
                else
                {
                    piFile = g_OpenAloneFile(pcszFilePath, bWritable);
                }

            Exit0:
                return piFile;
            }
            virtual bool CloseFile(NSSpirv::ISpirvFileHandle hFile) override
            {
                PROF_CPU();

                IFile* piFile = (IFile*)hFile;
                if (!piFile)
                    return false;

                piFile->Release();
                return true;
            }

            virtual unsigned int Read(NSSpirv::ISpirvFileHandle hFile, void* pBuffer, unsigned int uReadBytes) override
            {
                PROF_CPU();

                unsigned int uResult = 0;
                IFile*       piFile  = nullptr;

                KGLOG_PROCESS_ERROR(hFile && pBuffer && uReadBytes > 0);

                piFile  = (IFile*)hFile;
                uResult = piFile->Read(pBuffer, uReadBytes);
            Exit0:
                return uResult;
            }
            virtual unsigned int Write(NSSpirv::ISpirvFileHandle hFile, const void* pBuffer, unsigned int uWriteBytes) override
            {
                PROF_CPU();

                unsigned int uResult = 0;
                IFile*       piFile  = nullptr;

                KGLOG_PROCESS_ERROR(hFile && pBuffer && uWriteBytes > 0);

                piFile  = (IFile*)hFile;
                uResult = piFile->Write(pBuffer, uWriteBytes);
            Exit0:
                return uResult;
            }
            virtual int Seek(NSSpirv::ISpirvFileHandle hFile, int Offset, int Origin) override
            {
                PROF_CPU();

                IFile* piFile = (IFile*)hFile;
                if (!piFile)
                    return false;

                return piFile->Seek(Offset, Origin);
            }

            bool Exist(const char* pcszFilePath)
            {
                return g_IsAloneFileExist(pcszFilePath);
            }
        };

        void SpvShaderLoadLog(int nLevel, char const* pszModule, char const* pszMessage)
        {
            PROF_CPU();

            if (nLevel >= 0 && nLevel < KGLOG_LEVEL_MAX)
            {
                KGLogPrintf((KGLOG_PRIORITY)nLevel, "%s, %s", pszModule ? pszModule : "", pszMessage ? pszMessage : "");
            }
            else
            {
                KGLogPrintf(KGLOG_ERR, "%s, %s", pszModule ? pszModule : "", pszMessage ? pszMessage : "");
            }
        }

        void ShaderLoaderInit()
        {
            // #if defined(_WIN32) | defined(ANDROID)
            // glslang::InitializeProcess();
            // #endif
            KSPV_SetLogFunc(SpvShaderLoadLog);

            static KSpirvFileImpl sFileImpl;
            KSPV_SetFileInterface(&sFileImpl);

            KSPV_ShaderLoaderInit();
        }

        void ShaderLoaderUnInit()
        {
            // #if defined(_WIN32) | defined(ANDROID)
            // glslang::FinalizeProcess();
            KSPV_ShaderLoaderUnInit();
            // #endif
        }

        //		BOOL OnlyReflectShader(
        //			const char* _szShaderName,
        //			const char* pcszEntrypoint,
        //			const char* pcszShaderText,
        //			VkShaderStageFlagBits  stage,
        //			VkDevice               device,
        //			gfx::IShaderReflector* pReflector,
        //			std::set<uint32_t>& errorLines,
        //			uint32_t* pRetHash,
        //			BOOL* pRealBuild
        //		)
        //		{
        //			assert(pReflector);
        //			BOOL bRetCode = false;
        //
        //			VkShaderModuleCreateInfo moduleCreateInfo{};
        //
        //			uint32_t* pSpirv = nullptr;
        //			uint32_t            sprirv_count = 0;
        //			std::set<uint32_t>* pErrorlines = &errorLines;
        //			const char* szShaderName = _szShaderName;
        //
        //			// 前面把可能含有中文的Include部分转成Hash了，这里不用再转字符串编码
        //			// #ifndef _WIN32
        //			// 			char szUtf8ShaderName[MAX_PATH_LEN] = "";
        //			// 			{
        //			// 				DEBUG_PROFILE_CPU("CreateShaderString_ConvertGBKToUtf8");
        //			// 				// 里面可能有中文，不转utf8写本地文件，有的手机会宕机
        //			// 				ConvertGBKToUtf8(szUtf8ShaderName, countof(szUtf8ShaderName), _szShaderName);
        //			// 				szShaderName = szUtf8ShaderName;
        //			// 			}
        //			// #endif
        //
        // #if defined(_DEBUG)
        //			BOOL bDebug = true;
        // #else
        //			BOOL bDebug = false;
        // #endif
        //			{
        //				PROF_CPU("CreateShaderString_KSPV_GLSLtoSPV");
        //				bRetCode = KSPV_GLSLtoSPV_OnlyReflect(
        //					szShaderName,
        //					stage,
        //					pcszShaderText,
        //					&pSpirv,
        //					sprirv_count,
        //					pReflector,
        //					[=](uint32_t errorline, const char* pcszErrMsg) -> void
        //					{
        //						pErrorlines->insert(errorline);
        // #ifdef _WIN32
        //						g_shaderCompileErrRecord.Recoder(pcszErrMsg);
        // #endif
        //					},
        //					bDebug,
        //						pRetHash,
        //						pRealBuild,
        //					pcszEntrypoint
        //						);
        //			}
        //			return bRetCode;
        //		}

        VkShaderModule CreateShaderString(
            const char*            _szShaderName,
            const char*            pcszEntrypoint,
            const char*            pcszShaderText,
            VkShaderStageFlagBits  stage,
            VkDevice               pvkDevice,
            gfx::IShaderReflector* pReflector,
            std::set<uint32_t>&    errorLines,
            uint32_t*              pRetHash,
            BOOL*                  pRealBuild,
            BOOL                   bCreateModule
        )
        {
            PROF_CPU();

            VkShaderModule pShaderModel = VK_NULL_HANDLE;

            assert(pReflector);
            BOOL bRetCode = false;

            VkShaderModuleCreateInfo moduleCreateInfo{};

            uint32_t*           pSpirv       = nullptr;
            uint32_t            sprirv_count = 0;
            std::set<uint32_t>* pErrorlines  = &errorLines;
            const char*         szShaderName = _szShaderName;

            // 前面把可能含有中文的Include部分转成Hash了，这里不用再转字符串编码
            // #ifndef _WIN32
            // 			char szUtf8ShaderName[MAX_PATH_LEN] = "";
            // 			{
            // 				DEBUG_PROFILE_CPU("CreateShaderString_ConvertGBKToUtf8");
            // 				// 里面可能有中文，不转utf8写本地文件，有的手机会宕机
            // 				ConvertGBKToUtf8(szUtf8ShaderName, countof(szUtf8ShaderName), _szShaderName);
            // 				szShaderName = szUtf8ShaderName;
            // 			}
            // #endif

#if defined(_DEBUG)
            BOOL bDebug = true;
#else
            BOOL bDebug = false;
#endif
            if (DrvOption::bForceEnableDebugUtileExtension)
            {
                bDebug = true;
            }
            {
                PROF_CPU("CreateShaderString_KSPV_GLSLtoSPV");
                bRetCode = KSPV_GLSLtoSPV(
                    szShaderName,
                    stage,
                    pcszShaderText,
                    &pSpirv,
                    sprirv_count,
                    pReflector,
                    [=](uint32_t errorline, const char* pcszErrMsg) -> void {
                        pErrorlines->insert(errorline);
                        KGLogPrintf(KGLOG_ERR, "building %s error:", szShaderName);
                        KGLogPrintf(KGLOG_ERR, "%s", pcszErrMsg);
#ifdef _WIN32
                        g_shaderCompileErrRecord.Recoder(pcszErrMsg);
#endif
                    },
                    bDebug,
                    pRetHash,
                    pRealBuild,
                    pcszEntrypoint,
                    DrvOption::bEnableAfterMathSpirvDebug
                );
            }
            if (bRetCode && bCreateModule)
            {
                moduleCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                moduleCreateInfo.codeSize = sprirv_count * sizeof(uint32_t);
                moduleCreateInfo.pCode    = pSpirv;

                gfx::KGraphicDevice* pGfxDevice = gfx::GetGraphicDevice();
                if (pGfxDevice->bInitedGraphic)
                {
                    bRetCode = vks::vkCreateShaderModule(pvkDevice, &moduleCreateInfo, NULL, &pShaderModel);

                    if (bRetCode == VK_SUCCESS)
                    {
                        KEngineOptions* pEngineOptions = NSEngine::GetEngineOptions();
#ifdef _WIN32
                        if (pEngineOptions->IsLoadShaderSPV())
                        {
                            PROF_CPU("CreateShaderString_SaveSPV");

                            char spvFileName[NSEngine::MAX_PATH_LEN] = "";
                            sprintf(spvFileName, "%s.spv", szShaderName);
                            KGFile* fp = KGFOpen(spvFileName, "wb");
                            KGFWrite(pSpirv, (int)(sprirv_count * sizeof(uint32_t)), fp);
                            KGFClose(fp);
                        }
#endif
                    }
                    else
                    {
                        KGLogPrintf(KGLOG_ERR, "Error: Create shader  %s failed", szShaderName);
                        goto Exit0;
                    }
                }
                else
                {
                    static int g_pShaderModel = 1;
                    pShaderModel = reinterpret_cast<VkShaderModule>(static_cast<uintptr_t>(g_pShaderModel++));
                }
            }
        Exit0:
            KSPV_DeleteSpv(pSpirv);
            return pShaderModel;
        }

        bool FileExists(const std::string& filename)
        {
            std::ifstream f(filename.c_str());
            return !f.fail();
        }

        std::unordered_map<uint64_t, VkSampler> s_mapSamplerCache;

        VkResult vkCreateSampler(
            VkDevice                     device,
            const VkSamplerCreateInfo*   pCreateInfo,
            const VkAllocationCallbacks* pAllocator,
            VkSampler*                   pSampler
        )
        {
            VkResult ret = VK_INCOMPLETE;

            uint64_t hash = KSTR_HELPER::GetHashCodeForMem64Bit((char*)pCreateInfo, sizeof(VkSamplerCreateInfo));
            auto     it   = s_mapSamplerCache.find(hash);
            if (it != s_mapSamplerCache.end())
            {
                *pSampler = it->second;
                ret       = VK_SUCCESS;
            }
            else
            {
                ret = vks::vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
                s_mapSamplerCache.insert(std::make_pair<>(hash, *pSampler));
            }
            // detected device memory lack debug point here;
            return ret;
        }

        void vkDestroySampler(
            VkDevice                     device,
            VkSampler                    sampler,
            const VkAllocationCallbacks* pAllocator
        )
        {
        }

        void vkDestroySamplers(VkDevice device)
        {
            for (auto it : s_mapSamplerCache)
            {
                VkSampler pSampler = it.second;
                vks::vkDestroySampler(device, pSampler, nullptr);
            }
            s_mapSamplerCache.clear();
        }

        void vkCmdDraw(
            VkCommandBuffer commandBuffer,
            uint32_t        vertexCount,
            uint32_t        instanceCount,
            uint32_t        firstVertex,
            uint32_t        firstInstance
        )
        {
            KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
            vks::vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);

            pEnginePerformace->dwDrawCallCount++;
            pEnginePerformace->dwDrawFacesindics += vertexCount * instanceCount;
        }

        void vkCmdDrawIndexed(
            VkCommandBuffer commandBuffer,
            uint32_t        indexCount,
            uint32_t        instanceCount,
            uint32_t        firstIndex,
            int32_t         vertexOffset,
            uint32_t        firstInstance
        )
        {
            KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
            vks::vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);

            pEnginePerformace->dwDrawCallCount++;
            pEnginePerformace->dwDrawFacesindics += indexCount * instanceCount;
        }

        void vkCmdDrawIndirect(
            VkCommandBuffer commandBuffer,
            VkBuffer        buffer,
            VkDeviceSize    offset,
            uint32_t        drawCount,
            uint32_t        stride
        )
        {
            KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
            vks::vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);

            pEnginePerformace->dwDrawCallCount++;
            // pEnginePerformace->dwLastDrawFacesindics[renderThreadId] += indexCount * instanceCount;
        }

        void vkCmdDrawIndexedIndirect(
            VkCommandBuffer commandBuffer,
            VkBuffer        buffer,
            VkDeviceSize    offset,
            uint32_t        drawCount,
            uint32_t        stride
        )
        {
            KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
            vks::vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
            pEnginePerformace->dwDrawCallCount++;
        }

        void vkCmdDispatch(
            VkCommandBuffer commandBuffer,
            uint32_t        groupCountX,
            uint32_t        groupCountY,
            uint32_t        groupCountZ
        )
        {
            KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
            vks::vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);

            pEnginePerformace->dwDrawCallCount++;
            // pEnginePerformace->dwLastDrawFacesindics[renderThreadId] += indexCount * instanceCount;
        }

    } // namespace tools
} // namespace vks
