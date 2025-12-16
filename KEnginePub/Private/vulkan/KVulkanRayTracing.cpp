/**********************************************************
 * File: KVulkanRayTracing.cpp
 * Author: yizhou hu
 * Date: 2025/1/6
 * Description:
 **********************************************************/

#include "KVulkanRayTracing.h"
#include "GFXVulkan.h"
#include "KVulkanFunc.h"
#include "KVulkanDevice.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanRenderContext.h"
#include "kVulkanBuffer.h"
#include "KVulkanBindlessManager.h"
#include "KVulkanPrivate.h"
#include "KVulkanCommandBuffer.h"
#include "Engine/KGLog.h"
#include "KGraphicDevice.h"
#include "KVulkanStagingManager.h"
#include "../loader/KGFX_MemTexture.h"
#include "KSpirv/Private/KSpirvBuilder.h"
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <vector>

////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"

namespace gfx
{

	// AccelerationStructure offset needs to be 256 bytes aligned (official Vulkan specs, don't ask me why).
	const uint64_t AccelerationStructureAlignment = 256;
	uint64_t ScratchAlignment = 0;
	namespace VulkanRayTracingProperties
	{
		VkPhysicalDeviceAccelerationStructurePropertiesKHR accelProps_{};
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR pipelineProps_{};

		inline static uint32_t MaxDescriptorSetAccelerationStructures() { return accelProps_.maxDescriptorSetAccelerationStructures; }
		inline static uint64_t MaxGeometryCount() { return accelProps_.maxGeometryCount; }
		inline static uint64_t MaxInstanceCount() { return accelProps_.maxInstanceCount; }
		inline static uint64_t MaxPrimitiveCount() { return accelProps_.maxPrimitiveCount; }
		inline static uint32_t MaxRayRecursionDepth() { return pipelineProps_.maxRayRecursionDepth; }
		inline static uint32_t MaxShaderGroupStride() { return pipelineProps_.maxShaderGroupStride; }
		inline static uint32_t MinAccelerationStructureScratchOffsetAlignment() { return accelProps_.minAccelerationStructureScratchOffsetAlignment; }
		inline static uint32_t ShaderGroupBaseAlignment() { return pipelineProps_.shaderGroupBaseAlignment; }
		inline static uint32_t ShaderGroupHandleCaptureReplaySize() { return pipelineProps_.shaderGroupHandleCaptureReplaySize; }
		inline static uint32_t ShaderGroupHandleSize() { return pipelineProps_.shaderGroupHandleSize; }
	}
    /***************************************************************************/
    namespace RayTracingHelper
    {
        static VkDescriptorType GetDescriptorType(KRayTracingUniformBlockInfo* pBlockInfo)
        {
            ASSERT(pBlockInfo);
            VkDescriptorType retType = VkDescriptorType::VK_DESCRIPTOR_TYPE_MAX_ENUM;
            switch (pBlockInfo->m_UniformType)
            {
            case enumUniformType::ACCELERATION_STRUCTURE_UNIFORM:
                retType = VkDescriptorType::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                break;
            case enumUniformType::UBO_UNIFORM:
                retType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case enumUniformType::SSBO_UNIFORM:
                retType = VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case enumUniformType::TEXTURE_UNIFORM:
            {
                if (pBlockInfo->m_eTextureType == TextureType::RWBuffer)
                {
                    retType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                }
                else if (pBlockInfo->m_eTextureType == TextureType::CombinedSamplerBuffer)
                {
                    retType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

                }
                else if (pBlockInfo->m_eTextureType == TextureType::TextureImage2D ||
                    pBlockInfo->m_eTextureType == TextureType::TextureImage3D ||
                    pBlockInfo->m_eTextureType == TextureType::TextureImage2DArray
                    )
                {
                    retType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                }
                else
                {
                    retType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                }       
                break;
            } 
            case enumUniformType::SAMPLER_UNIFORM:
                retType = VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            default:
                ASSERT(false);
                break;
            }
            
            return retType;
        }
        
        static enumDescriptorType GetDescriptorTypeFromVKType(VkDescriptorType desc)
        {
            enumDescriptorType ret = (enumDescriptorType)0;
            switch (desc)
            {
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_SAMPLER;
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                break;
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                break;
            case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                ret = enumDescriptorType::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE;
                break;
            default:
                break;
            }
            return ret;
        }

        static VkShaderStageFlagBits GetShaderStageFlag(gfx::ShaderStageType flag)
        {
            VkShaderStageFlagBits ret = VK_SHADER_STAGE_VERTEX_BIT;
            switch (flag)
            {
            case gfx::ShaderStageType::RayGeneration:
                ret = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                break;
            case gfx::ShaderStageType::AnyHit:
                ret = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                break;
            case gfx::ShaderStageType::ClosestHit:
                ret = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                break;
            case gfx::ShaderStageType::Miss:
                ret = VK_SHADER_STAGE_MISS_BIT_KHR;
                break;
            case gfx::ShaderStageType::Intersection:
                ret = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                break;
            case gfx::ShaderStageType::Callable:
                ret = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
                break;
            default:
                break;
            }
            return ret;
        }

        inline static uint64_t RTAlign(uint64_t size, uint64_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        static VkFormat GetVertFormatRT(enumVertexFormat fmt)
        {
            VkFormat ret = VK_FORMAT_UNDEFINED;
            switch (fmt)
            {
            case gfx::VERT_FORMAT_R32G32B32A32_SFLOAT:
                ret = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            case gfx::VERT_FORMAT_R32G32B32_SFLOAT:
                ret = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case gfx::VERT_FORMAT_R32G32_SFLOAT:
                ret = VK_FORMAT_R32G32_SFLOAT;
                break;
            case gfx::VERT_FORMAT_R32_SFLOAT:
                ret = VK_FORMAT_R32_SFLOAT;
                break;
            case gfx::VERT_FORMAT_R8G8B8A8_UINT:
                ret = VK_FORMAT_R8G8B8A8_UINT;
                break;
            case gfx::VERT_FORMAT_R8G8B8A8_SINT:
                ret = VK_FORMAT_R8G8B8A8_SINT;
                break;
            case gfx::VERT_FORMAT_R8G8B8A8_UNORM:
                ret = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            case gfx::VERT_FORMAT_R8G8B8A8_SNORM:
                ret = VK_FORMAT_R8G8B8A8_SNORM;
                break;
            case gfx::VERT_FORMAT_R16G16_UINT:
                ret = VK_FORMAT_R16G16_UINT;
                break;
            case gfx::VERT_FORMAT_R16G16_SINT:
                ret = VK_FORMAT_R16G16_SINT;
                break;
            default:
                break;
            }
            return ret;
        }


        static void AddAccelerationStructureBuildBarrier(VkCommandBuffer CommandBuffer)
        {
            VkMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            Barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

            // TODO: Revisit the compute stages here as we don't always need barrier to compute
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

            vks::vkCmdPipelineBarrier(CommandBuffer, srcStage, dstStage, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
    };
   
   
	/************************ pipeline **********************************/

	KVulkanRayTracingShader::KVulkanRayTracingShader()
	{
	}

	KVulkanRayTracingShader::~KVulkanRayTracingShader()
	{
        auto pGraphicDevice = GetGraphicDevice();
        ASSERT(pGraphicDevice);
        pGraphicDevice->DestroyLayout(m_pLayout);
        for (auto _data : m_vecShaderResouces)
        {
            SAFE_DELETE(_data);
        }
        m_vecShaderResouces.clear();
	}



	auto KVulkanRayTracingShader::GetHash() -> uint64_t
	{
		return m_uUid;
	}

	auto KVulkanRayTracingShader::IsReady() const -> BOOL
	{
		return m_bIsReady;
	}

    const VkPipelineShaderStageCreateInfo& KVulkanRayTracingShader::GetCreateInfo(ERTShaderSubType _type)
	{
        for (const auto& _subData : m_vecShaderResouces)
        {
            if (_subData->m_subType == _type)
            {
                return _subData->m_vkShaderStageCreatInfo;
            }
        }
        CHECK_ASSERT(false);
        static VkPipelineShaderStageCreateInfo ErrorCreateInfo;
        return ErrorCreateInfo;
	}
    KVulkanLayout* KVulkanRayTracingShader::GetDesciptorSetLayout()
    {
        ASSERT(m_type == KRT_ST_RAY_GEN && "must call this interface on ray gen shader !");
        return m_pLayout;
    }
    KRayTracingUniformBlockInfo* KVulkanRayTracingShader::GetLocalMaterialBindlessIDUniformBlockInfo()
    {
        return m_vecShaderResouces[0]->m_pLocalMaterialBindlessIDCbufferBlock;
    }
    KRayTracingUniformBlockInfo* KVulkanRayTracingShader::GetLocalEngineBindlessIDUniformBlockInfo()
    {
        return m_vecShaderResouces[0]->m_pLocalEngineBindlessIDCbufferBlock;
    }
    KRayTracingUniformBlockInfo* KVulkanRayTracingShader::GetLocalMaterialParamUniformBlockInfo()
    {
        return m_vecShaderResouces[0]->m_pLocalMaterialParamCbufferBlock;
    }
    KRayTracingUniformBlockInfo* KVulkanRayTracingShader::GetCommonUniformBlockInfo()
    {
        ASSERT(m_type == KRT_ST_RAY_GEN && "must call this interface on ray gen shader !");
        return m_vecShaderResouces[0]->m_pCommonBindlessIDCbufferBlock;
    }

    static bool IsRayTracingShader(gfx::ShaderStageType shaderType)
    {
        return (shaderType == gfx::ShaderStageType::RayGeneration || shaderType == gfx::ShaderStageType::Intersection || shaderType == gfx::ShaderStageType::AnyHit
            || shaderType == gfx::ShaderStageType::ClosestHit || shaderType == gfx::ShaderStageType::Miss || shaderType == gfx::ShaderStageType::Callable);
    }
    static bool IsHitGroupShader(gfx::ShaderStageType shaderType)
    {
        return (shaderType == gfx::ShaderStageType::Intersection || shaderType == gfx::ShaderStageType::AnyHit
            || shaderType == gfx::ShaderStageType::ClosestHit );
    }


    BOOL KVulkanRayTracingShaderResource::BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType)
    {
        PROF_CPU();
        ASSERT(IsRayTracingShader(shaderType));
        static_const_param_name commonBindlessCbufferName = GetParamNameByPool(RAYTRACING_COMMON_PARAM_BINDLESS_CB_NAME);
        static_const_param_name localMaterialBindlessCbufferName = GetParamNameByPool(RAYTRACING_LOCAL_MATERIAL_BINDLESS_CB_NAME);
        static_const_param_name localEngineBindlessCbufferName = GetParamNameByPool(RAYTRACING_LOCAL_ENGINE_BINDLESS_CB_NAME);
        static_const_param_name localMaterialParamCbufferName = GetParamNameByPool(RAYTRACING_LOCAL_MATERIAL_PARAM_CB_NAME);
        BOOL                       bResult = FALSE;
        BOOL                       bRetCode = FALSE;
    
        IKSpirvCrossCompilerHandle pCompiler = KSPV_QueryCrossCompilerShaderSource(pProgramCross);
        gfx::KGraphicDevice* pGfxDevice = gfx::GetGraphicDevice();

        KGLOG_PROCESS_ERROR(pGfxDevice);
        //ubo
        {
            uint32_t uUnifromBlockCount = KSPV_GetNumLiveUniformBlocks(pCompiler);
            for (uint32_t i = 0; i < uUnifromBlockCount; ++i)
            {
                uint32_t uSet = 0;
                uint32_t uBinding = 0;
                const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetUniformResource(pCompiler, i);
                uSet = KSPV_GetSet(pCompiler, pResource);
                uBinding = KSPV_GetBinding(pCompiler, pResource);
                const_pool_str pBlockName = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
                KRayTracingUniformBlockInfo* l_pCurProcessBlock = nullptr;
                if (pBlockName == localMaterialBindlessCbufferName && IsHitGroupShader(shaderType))
                {
                    ASSERT(!m_pLocalMaterialBindlessIDCbufferBlock);
                    if (!m_pLocalMaterialBindlessIDCbufferBlock)
                    {
                        m_pLocalMaterialBindlessIDCbufferBlock = new KRayTracingUniformBlockInfo;
                    }
                    l_pCurProcessBlock = m_pLocalMaterialBindlessIDCbufferBlock;
                }
                if (pBlockName == localMaterialParamCbufferName && IsHitGroupShader(shaderType))
                {
                    ASSERT(!m_pLocalMaterialParamCbufferBlock);
                    if (!m_pLocalMaterialParamCbufferBlock)
                    {
                        m_pLocalMaterialParamCbufferBlock = new KRayTracingUniformBlockInfo;
                    }
                    l_pCurProcessBlock = m_pLocalMaterialParamCbufferBlock;
                }
                if (pBlockName == localEngineBindlessCbufferName)
                {
                    ASSERT(!m_pLocalEngineBindlessIDCbufferBlock);
                    if (!m_pLocalEngineBindlessIDCbufferBlock)
                    {
                        m_pLocalEngineBindlessIDCbufferBlock = new KRayTracingUniformBlockInfo;
                    }
                    l_pCurProcessBlock = m_pLocalEngineBindlessIDCbufferBlock;
                }
                if (pBlockName == commonBindlessCbufferName && shaderType == ShaderStageType::RayGeneration)
                {
                    ASSERT(!m_pCommonBindlessIDCbufferBlock);
                    if (!m_pCommonBindlessIDCbufferBlock)
                    {
                        m_pCommonBindlessIDCbufferBlock = new KRayTracingUniformBlockInfo;
                    }
                    l_pCurProcessBlock = m_pCommonBindlessIDCbufferBlock;

                }
                if (!l_pCurProcessBlock)
                {
                    if (uSet == 0)
                    {
                        continue;
                    }
                    else
                    {
                        l_pCurProcessBlock = new KRayTracingUniformBlockInfo;
                    }
                }
                l_pCurProcessBlock->m_nBinding = uBinding;
                l_pCurProcessBlock->m_nSpace = uSet;
                m_uMaxSet = uSet > m_uMaxSet ? uSet : m_uMaxSet;
                l_pCurProcessBlock->m_szName = pBlockName;
                l_pCurProcessBlock->m_UniformType = gfx::UBO_UNIFORM;
                uint32_t                     uSize = KSPV_GetStruceSize(pCompiler, pResource);
                const spirv_cross::SPIRType* pBaseType = KSPV_GetBaseType(pCompiler, pResource);
                uint32_t uNumbers = KSPV_GetNumMemberType(pBaseType);
                l_pCurProcessBlock->m_block16bytesAlignMemoryForGpu = uSize;
                for (uint32_t n = 0; n < uNumbers; ++n)
                {
                    uint32_t    j = n; // m_vecActiveUBORangeId[n];
                    const char* pName = GetParamNameByPool(KSPV_GetMemberName(pCompiler, pBaseType, j));
                    uint32_t                     uOffset = KSPV_GetMemberOffset(pCompiler, pBaseType, j);
                    uint32_t                     uByteSize = KSPV_GetMemberSize(pCompiler, pBaseType, j);
                    const spirv_cross::SPIRType* type = KSPV_GetMemberType(pCompiler, pBaseType, j);
                    // gfx::enumProgramDataType eType = KSPV_GetProgramDataType(type);
                    gfx::enumUniformBaseType     eType = KSPV_GetBaseType(type);
                    uint32_t                     vecsize = 0, cols = 0, arrays = 0;
                    KSPV_GetType_Rows_Cols_Arrays(type, vecsize, cols, arrays);

                    KRayTracingUniformInfo* pUniform = new KRayTracingUniformInfo;

                    pUniform->m_szBlockName = pBlockName;
                    pUniform->m_szName = pName;
                    pUniform->m_uNameHash = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                    pUniform->m_uVectorSize = vecsize;
                    pUniform->m_uMatcol = cols;
                    pUniform->m_uMatrow = vecsize;

                    if (pUniform->m_uMatcol != 4 || pUniform->m_uMatrow != 4)
                    {
                        pUniform->m_uMatcol = 0;
                        pUniform->m_uMatrow = 0;
                    }

                    if (pUniform->m_uMatcol == 4 && pUniform->m_uMatrow == 4)
                    {
                        pUniform->m_uVectorSize = 0;
                    }

                    pUniform->m_nOffset = uOffset;
                    pUniform->m_uArrayCount = std::max<uint32_t>(arrays, 1);
                    pUniform->m_uByteSize = uByteSize;
                    pUniform->m_UniformBaseType = eType;
                    pUniform->m_UniformType = gfx::UBO_UNIFORM;
                    l_pCurProcessBlock->m_mapUnifroms.emplace(pUniform->m_szName, pUniform);
                }
                m_mapUniformBlocks.emplace(l_pCurProcessBlock->m_szName, l_pCurProcessBlock);
                m_uUniformBufferCount++;
            }
            ASSERT(m_pLocalMaterialBindlessIDCbufferBlock || m_pLocalEngineBindlessIDCbufferBlock || m_pCommonBindlessIDCbufferBlock && " Fatal : No Valid Bindless ID Cbuffer Found In RayTracing Shader !");

        }
      
        //ssbo
        {
            uint32_t uStorageBufferCount = KSPV_GetNumLiveStorageBufferBlocks(pCompiler);
            for (uint32_t i = 0; i < uStorageBufferCount; ++i)
            {
                uint32_t uSet = 0;
                uint32_t uBinding = 0;
                const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetStorageBufferResource(pCompiler, i);
                uSet = KSPV_GetSet(pCompiler, pResource);
                uBinding = KSPV_GetBinding(pCompiler, pResource);
                const_pool_str pBlockName = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
                KRayTracingUniformBlockInfo* l_pCurProcessBlock = nullptr;
                if (!l_pCurProcessBlock)
                {
                    if (uSet == 0)
                    {
                        continue;
                    }
                    else
                    {
                        l_pCurProcessBlock = new KRayTracingUniformBlockInfo;
                    }
                }
                l_pCurProcessBlock->m_nBinding = uBinding;
                l_pCurProcessBlock->m_nSpace = uSet;
                m_uMaxSet = uSet > m_uMaxSet ? uSet : m_uMaxSet;
                l_pCurProcessBlock->m_szName = pBlockName;
                l_pCurProcessBlock->m_UniformType = gfx::SSBO_UNIFORM;
                uint32_t                     uSize = KSPV_GetStruceSize(pCompiler, pResource);
                const spirv_cross::SPIRType* pBaseType = KSPV_GetBaseType(pCompiler, pResource);
                uint32_t uNumbers = KSPV_GetNumMemberType(pBaseType);
                l_pCurProcessBlock->m_block16bytesAlignMemoryForGpu = uSize;
                for (uint32_t n = 0; n < uNumbers; ++n)
                {
                    uint32_t    j = n; // m_vecActiveUBORangeId[n];
                    const char* pName = GetParamNameByPool(KSPV_GetMemberName(pCompiler, pBaseType, j));
                    uint32_t                     uOffset = KSPV_GetMemberOffset(pCompiler, pBaseType, j);
                    uint32_t                     uByteSize = KSPV_GetMemberSize(pCompiler, pBaseType, j);
                    const spirv_cross::SPIRType* type = KSPV_GetMemberType(pCompiler, pBaseType, j);
                    // gfx::enumProgramDataType eType = KSPV_GetProgramDataType(type);
                    gfx::enumUniformBaseType     eType = KSPV_GetBaseType(type);
                    uint32_t                     vecsize = 0, cols = 0, arrays = 0;
                    KSPV_GetType_Rows_Cols_Arrays(type, vecsize, cols, arrays);

                    KRayTracingUniformInfo* pUniform = new KRayTracingUniformInfo;

                    pUniform->m_szBlockName = pBlockName;
                    pUniform->m_szName = pName;
                    pUniform->m_uNameHash = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                    pUniform->m_uVectorSize = vecsize;
                    pUniform->m_uMatcol = cols;
                    pUniform->m_uMatrow = vecsize;

                    if (pUniform->m_uMatcol != 4 || pUniform->m_uMatrow != 4)
                    {
                        pUniform->m_uMatcol = 0;
                        pUniform->m_uMatrow = 0;
                    }

                    if (pUniform->m_uMatcol == 4 && pUniform->m_uMatrow == 4)
                    {
                        pUniform->m_uVectorSize = 0;
                    }

                    pUniform->m_nOffset = uOffset;
                    pUniform->m_uArrayCount = std::max<uint32_t>(arrays, 1);
                    pUniform->m_uByteSize = uByteSize;
                    pUniform->m_UniformBaseType = eType;
                    pUniform->m_UniformType = l_pCurProcessBlock->m_UniformType;;
                    l_pCurProcessBlock->m_mapUnifroms.emplace(pUniform->m_szName, pUniform);
                }
                m_mapUniformBlocks.emplace(l_pCurProcessBlock->m_szName, l_pCurProcessBlock);
                m_uStorageBufferCount++;
            }
        }

        //storage image
        {
            uint32_t uStorageImageCount = KSPV_GetNumLiveStorageImageBlocks(pCompiler);
            for (uint32_t i = 0; i < uStorageImageCount; ++i)
            {
                const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetStorageImageResource(pCompiler, i);
                const spirv_cross::SPIRType* type = KSPV_GetType(pCompiler, pResource);
                uint32_t                                location = KSPV_GetSet(pCompiler, pResource);
                uint32_t                                binding = KSPV_GetBinding(pCompiler, pResource);
                const_pool_str                         pName = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
                KRayTracingUniformBlockInfo* l_pCurProcessBlock = nullptr;
                if (!l_pCurProcessBlock)
                {
                    if (location == 0)
                    {
                        continue;
                    }
                    else
                    {
                        l_pCurProcessBlock = new KRayTracingUniformBlockInfo;
                    }
                }
                uint32_t arraySize = KSPV_GetTypeArraySize(type);
                l_pCurProcessBlock->m_uArrayCount = std::max<uint32_t>(arraySize, 1);
                l_pCurProcessBlock->m_nBinding = binding;
                l_pCurProcessBlock->m_nSpace = location;
                m_uMaxSet = location > m_uMaxSet ? location : m_uMaxSet;
                l_pCurProcessBlock->m_szName = pName;
                l_pCurProcessBlock->m_UniformType = gfx::TEXTURE_UNIFORM;
                BOOL bArray = KSPV_IsTextureArray(type);
                ASSERT(!bArray);
                if (KSPV_Is1DTexture(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::Texture1D;
                }
                else if (KSPV_Is2DTexture(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::TextureImage2D;
                }
                else if (KSPV_Is3DTexture(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::TextureImage3D;
                }
                else if (KSPV_IsImageBuffer(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::RWBuffer;
                }
                else if(KSPV_IsTextureArray(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::TextureImage2DArray;
                }

                m_mapUniformBlocks.emplace(l_pCurProcessBlock->m_szName, l_pCurProcessBlock);
                m_uStorageImageCount++;
            }
        }

        //texture
        {
            uint32_t uTextureCount = KSPV_GetNumLiveTexture(pCompiler);
            for (uint32_t i = 0; i < uTextureCount; ++i)
            {
                const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetTextureResource(pCompiler, i);
                const spirv_cross::SPIRType* type = KSPV_GetType(pCompiler, pResource);
                uint32_t                                location = KSPV_GetSet(pCompiler, pResource);
                uint32_t                                binding = KSPV_GetBinding(pCompiler, pResource);
                const_pool_str                         pName = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
                KRayTracingUniformBlockInfo* l_pCurProcessBlock = nullptr;
                if (!l_pCurProcessBlock)
                {
                    if (location == 0)
                    {
                        continue;
                    }
                    else
                    {
                        l_pCurProcessBlock = new KRayTracingUniformBlockInfo;
                    }
                }
                uint32_t arraySize = KSPV_GetTypeArraySize(type);
                l_pCurProcessBlock->m_uArrayCount = std::max<uint32_t>(arraySize, 1);
                l_pCurProcessBlock->m_nBinding = binding;
                l_pCurProcessBlock->m_nSpace = location;
                m_uMaxSet = location > m_uMaxSet ? location : m_uMaxSet;
                l_pCurProcessBlock->m_szName = pName;
                l_pCurProcessBlock->m_UniformType = gfx::TEXTURE_UNIFORM;
                BOOL bArray = KSPV_IsTextureArray(type);
                ASSERT(!bArray);
                if (KSPV_Is1DTexture(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::Texture1D;
                }
                else if (KSPV_Is2DTexture(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::TextureImage2D;
                }
                else if (KSPV_Is3DTexture(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::TextureImage3D;
                }
                else if(KSPV_IsTextureArray(type))
                {
                    l_pCurProcessBlock->m_eTextureType = TextureType::TextureImage2DArray;
                }
                else if (KSPV_IsCube(type))
                {
                    l_pCurProcessBlock->m_eTextureType = bArray ? TextureType::CubemapArray : TextureType::Cubemap;
                }
                m_mapUniformBlocks.emplace(l_pCurProcessBlock->m_szName, l_pCurProcessBlock);
                m_uSampledImageCount++;
            }
        }

        //sampler
        {
            uint32_t uSamplerCount = KSPV_GetNumLiveSampler(pCompiler);
            for (uint32_t i = 0; i < uSamplerCount; ++i)
            {
                const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetSamplerResource(pCompiler, i);
                uint32_t                                location = KSPV_GetSet(pCompiler, pResource);
                uint32_t                                binding = KSPV_GetBinding(pCompiler, pResource);
                const_pool_str                         pName = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
                KRayTracingUniformBlockInfo* l_pCurProcessBlock = nullptr;
                if (!l_pCurProcessBlock)
                {
                    if (location == 0)
                    {
                        continue;
                    }
                    else
                    {
                        l_pCurProcessBlock = new KRayTracingUniformBlockInfo;
                    }
                }
                l_pCurProcessBlock->m_nBinding = binding;
                l_pCurProcessBlock->m_nSpace = location;
                m_uMaxSet = location > m_uMaxSet ? location : m_uMaxSet;
                l_pCurProcessBlock->m_szName = pName;
                l_pCurProcessBlock->m_UniformType = gfx::SAMPLER_UNIFORM;
                m_mapUniformBlocks.emplace(l_pCurProcessBlock->m_szName, l_pCurProcessBlock);
                m_uSamplerCount++;
            }
        }

        //acceleration structure
        {
            uint32_t uASCount = KSPV_GetNumLiveAccelerationStructures(pCompiler);
            for (uint32_t i = 0; i < uASCount; ++i)
            {
                const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetAccelerationStructuresResource(pCompiler, i);
                const_pool_str                         pName = GetParamNameByPool(KSPV_GetStorageBlockName(pCompiler, pResource));

                uint32_t location = KSPV_GetLocation(pCompiler, pResource);
                uint32_t binding = KSPV_GetBinding(pCompiler, pResource);

                KRayTracingUniformBlockInfo* l_pCurProcessBlock = nullptr;
                if (!l_pCurProcessBlock)
                {
                    if (location == 0)
                    {
                        continue;
                    }
                    else
                    {
                        l_pCurProcessBlock = new KRayTracingUniformBlockInfo;
                    }
                }
                l_pCurProcessBlock->m_nBinding = binding;
                l_pCurProcessBlock->m_nSpace = location;
                m_uMaxSet = location > m_uMaxSet ? location : m_uMaxSet;
                l_pCurProcessBlock->m_szName = pName;
                l_pCurProcessBlock->m_UniformType = gfx::ACCELERATION_STRUCTURE_UNIFORM;
                m_mapUniformBlocks.emplace(l_pCurProcessBlock->m_szName, l_pCurProcessBlock);
                m_uAccelerationStructureCount++;
            }
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    IKGFX_CombinedShaderResult* KVulkanRayTracingShaderResource::GetCombindShaderResult()
    {
        return nullptr;
    }

    KVulkanRayTracingShaderResource::~KVulkanRayTracingShaderResource()
    {
        SAFE_RELEASE(m_pLocalMaterialBindlessIDCbufferBlock);
        SAFE_RELEASE(m_pCommonBindlessIDCbufferBlock);

        if (m_shaderModule != VK_NULL_HANDLE)
        {
            vks::vkDestroyShaderModule(GetVkDevice(), m_shaderModule, nullptr);
            m_shaderModule = VK_NULL_HANDLE;
        }
    }

    bool KVulkanRayTracingShader::Create(const KRayTracingShaderCreateDesc& ci)
    {
        bool bRetCode = false;
        bool bResult = false;
        m_uUid = ci.inHash;
        eShaderType = ci.sType;
        auto gfxDevice = gfx::KGFX_GetGraphicDevice();
        auto pDevice = GetVkDevice();
        KGLOG_PROCESS_ERROR(pDevice);

        KGLOG_PROCESS_ERROR(gfxDevice);
        //comile Shaders
        for (const auto& subModuleInfo : ci.vecSubmodule)
        {
            auto shaderType = RayTracingHelper::GetGfxShaderStageTypeFromRTShaderType(eShaderType,subModuleInfo.sType);          
            gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
            KShaderStage* pStage = nullptr;
            KVulkanRayTracingShaderResource* pShaderResource = new KVulkanRayTracingShaderResource;
            pShaderResource->m_subType = subModuleInfo.sType;
            bRetCode = pGraphicDevice->LoadShaderWithoutTech(&pStage, subModuleInfo.szMainShaderPath, subModuleInfo.szEntryPoint, *subModuleInfo.userShaderFileLoc, subModuleInfo.szMacroDefine, pShaderResource, shaderType, false, 0);
            if (bRetCode && pStage)
            {
                auto vulkanStage = static_cast<KVulkanShaderStage*>(pStage);
                pShaderResource->m_shaderModule = (VkShaderModule)vulkanStage->MoveOutShaderModule();
                pShaderResource->m_vkShaderStageCreatInfo = vulkanStage->GetCreateInfo();
                vulkanStage = nullptr;
            }
            SAFE_RELEASE(pStage);
            m_vecShaderResouces.push_back(pShaderResource);
        }
        if (eShaderType == KRT_ST_RAY_GEN)
        {
            if (!m_pLayout)
            {
                gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
                bRetCode = pGraphicDevice->CreateLayout(&m_pLayout);
                KGLOG_PROCESS_ERROR(bRetCode);
            }
            KGLOG_PROCESS_ERROR(m_pLayout);
            m_pLayout->Destroy();//clear but named destroy ==
            m_pLayout->Begin();
            auto& mapBlocks = m_vecShaderResouces[0]->m_mapUniformBlocks;
            for (auto& iter : mapBlocks)
            {
                auto pBlock = iter.second;
                m_pLayout->AddLayout(pBlock->m_nSpace, RayTracingHelper::GetDescriptorTypeFromVKType(RayTracingHelper::GetDescriptorType(pBlock)), ShaderStageType::ALLStages, pBlock->m_nBinding, 1);
            }
            bRetCode = m_pLayout->End(false);
            KGLOG_PROCESS_ERROR(bRetCode);
            
        }
        m_bIsReady = true;
        bResult = true;
    Exit0:
        return bResult;
    }


	KVulkanRayTracingPipeline::KVulkanRayTracingPipeline()
	{
		m_pPipeline = VK_NULL_HANDLE;
	}

	KVulkanRayTracingPipeline::~KVulkanRayTracingPipeline()
	{
		Destroy();
	}
	//the order of the stages must be static and accessable by everywhere during rt
    bool KVulkanRayTracingPipeline::Create(const RayTracingPipelineDesc& pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer)
	{
		PROF_CPU();
		bool bResult = false;
        bool bRetCode = false;
		VkResult                    hRetCode = VK_INCOMPLETE;
		VkDevice                    pDevice = GetVkDevice();
		vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
		VkRayTracingPipelineCreateInfoKHR rtPipelineCreateInfo{};
		KSpecializationConstantContainer* pContainer = (KSpecializationConstantContainer*)pSpecializationConstantContainer;
		
		std::vector<VkPipelineShaderStageCreateInfo>			vecShaderStageCreateInfo;
		std::vector<VkRayTracingShaderGroupCreateInfoKHR>		vecShaderGroupCreateInfo;
        uint32_t l_uShaderHandleSize = VulkanRayTracingProperties::ShaderGroupHandleSize();
        size_t l_dataSize = 0;
		//create Shader groups

        RayGen.Shaders.resize(pDesc.vecRayGenShaders.size());
        for (IRayTracingShader* const RayGenShaderRHI : pDesc.vecRayGenShaders)
        {
            CHECK_ASSERT(RayGenShaderRHI->GetType() == enumRayTracingShaderType::KRT_ST_RAY_GEN);
            KVulkanRayTracingShader* const RayGenShader = (KVulkanRayTracingShader*)(RayGenShaderRHI);
            VkPipelineShaderStageCreateInfo ShaderStage = RayGenShader->GetCreateInfo();
            vecShaderStageCreateInfo.push_back(ShaderStage);
            VkRayTracingShaderGroupCreateInfoKHR ShaderGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr };
            ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            ShaderGroup.generalShader = (uint32_t)vecShaderStageCreateInfo.size() - 1;
            ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            vecShaderGroupCreateInfo.push_back(ShaderGroup);
            RayGen.Shaders.push_back(RayGenShader);
        }
        {
            //Get CommonReflection Info From RayGen Shader And Bindless Manager
        //Then Generate Pipeline Layout
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            std::vector<VkDescriptorSetLayout> vecDescriptorLayout;
            vecDescriptorLayout.push_back(pBindlessMgr->GetBindlessSetLayout());
                //auto& vecDecriptorLayout = pDesc.vecRayGenShaders[0]->;
            auto vulkanShaderTemplate = (KVulkanRayTracingShader*)pDesc.vecRayGenShaders[0];
            KVulkanLayout* reflectionLayouts = vulkanShaderTemplate->GetDesciptorSetLayout();
            for (uint32_t i = 0; i < reflectionLayouts->GetLayoutSetCount(); i++)
            {
                vecDescriptorLayout.push_back(reflectionLayouts->GetDesriptorSetLayout(i));
            }
            VkPipelineLayoutCreateInfo         pipelineLayoutCreateInfo = vks::initializers::PipelineLayoutCreateInfo(vecDescriptorLayout.data(), (uint32_t)vecDescriptorLayout.size());
            hRetCode = vks::vkCreatePipelineLayout(pDevice, &pipelineLayoutCreateInfo, nullptr, &m_pPipelineLayout);
            KGLOG_COM_PROCESS_ERROR(hRetCode);
            //create descriptorSets
            {
                gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
                bRetCode = pGraphicDevice->CreateDescriptorPool(&m_DescriptorPoolContainer.m_pDescriptorPool);
                KGLOG_PROCESS_ERROR(bRetCode);
                m_DescriptorPoolContainer.m_pDescriptorPool->Begin();
                auto pShaderResource = vulkanShaderTemplate->GetShaderResource(0);
                if (pShaderResource->m_uAccelerationStructureCount)
                {
                    m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, pShaderResource->m_uAccelerationStructureCount);
                }
                if (pShaderResource->m_uSamplerCount)
                {
                    m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLER, pShaderResource->m_uSamplerCount);
                }
                if (pShaderResource->m_uSampledImageCount)
                {
                    m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE, pShaderResource->m_uSampledImageCount);
                }
                if (pShaderResource->m_uStorageImageCount)
                {
                    m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE, pShaderResource->m_uStorageImageCount);
                }
                if (pShaderResource->m_uUniformBufferCount)
                {
                    m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, pShaderResource->m_uUniformBufferCount);

                }
                if (pShaderResource->m_uStorageBufferCount)
                {
                    m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, pShaderResource->m_uStorageBufferCount);

                }
                bRetCode = m_DescriptorPoolContainer.m_pDescriptorPool->End();
                KGLOG_PROCESS_ERROR(bRetCode);
                if (!m_pDescriptorSet)
                {
                    bRetCode = gfx::GetGraphicDevice()->CreateDescriptorSet(&m_pDescriptorSet, reflectionLayouts, &m_DescriptorPoolContainer);
                    KGLOG_ASSERT_EXIT(m_pDescriptorSet);
                }
            }
        }
        




        Miss.Shaders.resize(pDesc.vecMissShaders.size());
        for (IRayTracingShader* const MissShaderRHI : pDesc.vecMissShaders)
        {
            CHECK_ASSERT(MissShaderRHI->GetType() == enumRayTracingShaderType::KRT_ST_MISS);
            KVulkanRayTracingShader* const MissShader = (KVulkanRayTracingShader*)(MissShaderRHI);
            VkPipelineShaderStageCreateInfo ShaderStage = MissShader->GetCreateInfo();
            vecShaderStageCreateInfo.push_back(ShaderStage);
            VkRayTracingShaderGroupCreateInfoKHR ShaderGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr };
            ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            ShaderGroup.generalShader = (uint32_t)vecShaderStageCreateInfo.size() - 1;
            ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            vecShaderGroupCreateInfo.push_back(ShaderGroup);
            Miss.Shaders.push_back(MissShader);
        }
        HitGroup.Shaders.resize(pDesc.vecHitShaders.size());
        for (IRayTracingShader* const HitGroupShaderRHI : pDesc.vecHitShaders)
        {
            VkRayTracingShaderGroupCreateInfoKHR ShaderGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr };
            ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            ShaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
            CHECK_ASSERT(HitGroupShaderRHI->GetType() == enumRayTracingShaderType::KRT_ST_HIT_GROUP);
            KVulkanRayTracingShader* const HitGroupShader = (KVulkanRayTracingShader*)(HitGroupShaderRHI);
            // Closest Hit, always present
            {
                VkPipelineShaderStageCreateInfo ShaderStage = HitGroupShader->GetCreateInfo(ERTShaderSubType::E_RT_TYPE_CLOSEST_HIT);
                vecShaderStageCreateInfo.push_back(ShaderStage);
                ShaderGroup.closestHitShader = (uint32_t)vecShaderStageCreateInfo.size() - 1;
            }
            // Any Hit, optional
            if (HitGroupShader->IsSubTypePresent(ERTShaderSubType::E_RT_TYPE_ANY_HIT))
            {
                VkPipelineShaderStageCreateInfo ShaderStage = HitGroupShader->GetCreateInfo(ERTShaderSubType::E_RT_TYPE_ANY_HIT);
                vecShaderStageCreateInfo.push_back(ShaderStage);
                ShaderGroup.anyHitShader = (uint32_t)vecShaderStageCreateInfo.size() - 1;
            }
            else
            {
                ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            }
            // Intersection, optional
            if (HitGroupShader->IsSubTypePresent(ERTShaderSubType::E_RT_TYPE_INTERSECTION))
            {
                VkPipelineShaderStageCreateInfo ShaderStage = HitGroupShader->GetCreateInfo(ERTShaderSubType::E_RT_TYPE_INTERSECTION);
                vecShaderStageCreateInfo.push_back(ShaderStage);
                ShaderGroup.intersectionShader = (uint32_t)vecShaderStageCreateInfo.size() - 1;
                // Switch the shader group type given the presence of an intersection shader
                ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            }
            else
            {
                ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            }
            vecShaderGroupCreateInfo.push_back(ShaderGroup);
            HitGroup.Shaders.push_back(HitGroupShader);
        }

        Callable.Shaders.resize(pDesc.vecCallableShaders.size());
        for (IRayTracingShader* const CallableShaderRHI : pDesc.vecCallableShaders)
        {
            CHECK_ASSERT(CallableShaderRHI->GetType() == enumRayTracingShaderType::KRT_ST_CALLABLE);
            KVulkanRayTracingShader* const CallableShader = (KVulkanRayTracingShader*)(CallableShaderRHI);
            VkPipelineShaderStageCreateInfo ShaderStage = CallableShader->GetCreateInfo();
            vecShaderStageCreateInfo.push_back(ShaderStage);
            VkRayTracingShaderGroupCreateInfoKHR ShaderGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR , nullptr};
            ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            ShaderGroup.generalShader = (uint32_t)vecShaderStageCreateInfo.size() - 1;
            ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            vecShaderGroupCreateInfo.push_back(ShaderGroup);
            Callable.Shaders.push_back(CallableShader);
        }
		//// Create ray tracing pipeline
        rtPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        rtPipelineCreateInfo.pNext = nullptr;
        rtPipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        rtPipelineCreateInfo.stageCount = static_cast<uint32_t>(vecShaderStageCreateInfo.size());
        rtPipelineCreateInfo.pStages = vecShaderStageCreateInfo.data();
        rtPipelineCreateInfo.groupCount = static_cast<uint32_t>(vecShaderGroupCreateInfo.size());
        rtPipelineCreateInfo.pGroups = vecShaderGroupCreateInfo.data();
        rtPipelineCreateInfo.maxPipelineRayRecursionDepth = pDesc.uMaxRayRecursionDepth;
        rtPipelineCreateInfo.layout = m_pPipelineLayout;//later get from bindless manager(global)
        rtPipelineCreateInfo.basePipelineHandle = nullptr;
        rtPipelineCreateInfo.basePipelineIndex = 0;
		hRetCode = vks::vkCreateRayTracingPipelinesKHR(pDevice, VK_NULL_HANDLE, pVulkanDevice->m_pPipelineCache, 1, &rtPipelineCreateInfo, nullptr, &m_pPipeline);
		KGLOG_PROCESS_ERROR(hRetCode == VK_SUCCESS);
        m_uShaderGroupCount = static_cast<uint32_t>(vecShaderGroupCreateInfo.size());
        // Grab all shader handles for each stage
        {
            uint32_t HandleOffset = 0;
            auto FetchShaderHandles = [&HandleOffset, l_uShaderHandleSize, pDevice](VkPipeline RTPipeline, uint32_t HandleCount)
                {
                    std::vector<uint8_t> OutHandleStorage;
                    if (HandleCount)
                    {
                        const uint32_t ShaderHandleStorageSize = HandleCount * l_uShaderHandleSize;
                        OutHandleStorage.resize(ShaderHandleStorageSize);

                        vks::vkGetRayTracingShaderGroupHandlesKHR(pDevice, RTPipeline, HandleOffset, HandleCount, ShaderHandleStorageSize, OutHandleStorage.data());

                        HandleOffset += HandleCount;
                    }

                    return OutHandleStorage;
                };

            // NOTE: Must be filled in the same order as created above
            RayGen.ShaderHandles = FetchShaderHandles(m_pPipeline, (uint32_t)pDesc.vecRayGenShaders.size());
            Miss.ShaderHandles = FetchShaderHandles(m_pPipeline, (uint32_t)pDesc.vecMissShaders.size());
            HitGroup.ShaderHandles = FetchShaderHandles(m_pPipeline, (uint32_t)pDesc.vecHitShaders.size());
            Callable.ShaderHandles = FetchShaderHandles(m_pPipeline, (uint32_t)pDesc.vecCallableShaders.size());
        }
        m_uGroupHandleSize = l_uShaderHandleSize;
        m_uGroupHandleSizeAligned = static_cast<uint32_t>(RayTracingHelper::RTAlign(m_uGroupHandleSize, VulkanRayTracingProperties::ShaderGroupBaseAlignment()));
		bResult = TRUE;
	Exit0:
		return bResult;
	}

    BOOL KVulkanRayTracingPipeline::Destroy()
	{
		PROF_CPU();
		BOOL bRet = FALSE;
        if (m_pDescriptorSet)
        {
            gfx::GetGraphicDevice()->DestroyDescriptorSet(m_pDescriptorSet);
        }
        m_DescriptorPoolContainer.Clear();
		if (m_pPipeline)
		{
			VkDevice pDevice = GetVkDevice();
			vks::vkDestroyPipeline(pDevice, m_pPipeline, nullptr);

			m_pPipeline = VK_NULL_HANDLE;
		}
		bRet = TRUE;
		// Exit0:
		return bRet;
	}

	VkPipeline KVulkanRayTracingPipeline::GetPipeline() const
	{
		return m_pPipeline;
	}
    VkPipelineLayout KVulkanRayTracingPipeline::GetPipelineLayout() const
    {
        return m_pPipelineLayout;
    }
	enumForProcessType KVulkanRayTracingPipeline::GetType() const
	{
		return enumForProcessType::FOR_RAYTRACING;
	}
	auto KVulkanRayTracingPipeline::GetShaderGroupCount() -> uint32_t
	{
		return m_uShaderGroupCount;
	}

    uint32_t KVulkanRayTracingPipeline::GetShaderGroupHandleSize() const
    {
        return m_uGroupHandleSize;
    }

    uint32_t KVulkanRayTracingPipeline::GetShaderGroupHandleSizeAligned() const
    {
        return m_uGroupHandleSizeAligned;
    }

    const KVulkanRayTracingPipeline::ShaderData& KVulkanRayTracingPipeline::GetShaderData(enumRayTracingShaderType eShaderType) const
    {
        switch (eShaderType)
        {
        case gfx::KRT_ST_RAY_GEN:
            return RayGen;
            break;
        case gfx::KRT_ST_HIT_GROUP:
            return HitGroup;
            break;
        case gfx::KRT_ST_MISS:
            return Miss;
            break;
        case gfx::KRT_ST_CALLABLE:
            return Callable;
            break;
        default:
            CHECK_ASSERT(false);//invalid shader type
            break;
        }
        static ShaderData EmptyShaderData;
        return EmptyShaderData;
    }
	/***************************************************************************/

	/***************************** ray tracing program *********************************************/
	auto KVulkanRayTracingProgram::Create(const RayTracingProgramDesc& rtpDC) -> bool
	{
		bool bRetCode = false;
        bool bResult = false;
        RayTracingPipelineDesc pipelineDC{};
        bRetCode = KRayTracingProgram::Create(rtpDC);
		gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
		m_pVulkanRayTracingPipeline = new KVulkanRayTracingPipeline;
		////fill a shaderstage array to fit the fucking material system
		
        pipelineDC.vecRayGenShaders = rtpDC.vecRayGenShaders;
        pipelineDC.vecMissShaders = rtpDC.vecMissShaders;
        pipelineDC.vecHitShaders = rtpDC.vecHitShaders;
        pipelineDC.vecCallableShaders = rtpDC.vecCallableShaders;
		pipelineDC.uMaxRayRecursionDepth = rtpDC.uMaxRayRecursionDepth;
        bRetCode = m_pVulkanRayTracingPipeline->Create(pipelineDC);//TODO:collect specializedconst from shaders and pass in
        KG_PROCESS_ERROR(bRetCode);
		bResult = true;
    Exit0:
        if (!bResult)
        {
            Destroy();
        }
		return bResult;
	}

	auto KVulkanRayTracingProgram::Destroy() -> void
	{
		SAFE_DELETE(m_pVulkanRayTracingPipeline);
        KRayTracingProgram::Destroy();
	}

	auto KVulkanRayTracingProgram::GetVulkanPipeline() -> KVulkanRayTracingPipeline*
	{
		return m_pVulkanRayTracingPipeline;
	}

    bool KVulkanRayTracingProgram::AddBindlessUAV(IKGFX_BufferView* pUAV)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        pBindlessMgr->AddBindlessUAV(pUAV);
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindlessUAV(IKGFX_TextureView* pTexView)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        pBindlessMgr->AddBindlessUAV(pTexView);
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindlessSRV(IKGFX_BufferView* pSRV)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        pBindlessMgr->AddBindlessSRV(pSRV);
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindlessSRV(IKGFX_TextureView* pTexView)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        pBindlessMgr->AddBindlessSRV(pTexView);
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindlessCBV(IKGFX_BufferView* pBufView)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        pBindlessMgr->AddBindlessCBV(pBufView);
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindlessSampler(IKGFX_Sampler* pSampler)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        pBindlessMgr->AddBindlessSampler(pSampler->GetBindlessView());
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindlessRayTracingScene(KRayTracingScene* pRTScene)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        KVulkanRayTracingScene* vulkanRayTracingScene = static_cast<KVulkanRayTracingScene*>(pRTScene);
        uint32_t uBindlessSlot = vulkanRayTracingScene->GetBindlessHandle();
        pBindlessMgr->AddBindlessRayTracingScene(uBindlessSlot,pRTScene);
        return true;
    }
    bool KVulkanRayTracingProgram::AddBindCBV(const_pool_str pName, IKGFX_BufferView* pCBV)
    {
        bool ret = false;
        const auto& mapUniformInfo = m_pVulkanRayTracingPipeline->GetUniformMap();
        auto iter = mapUniformInfo.find(pName);
        if (iter != mapUniformInfo.end())
        {
            auto pUniformInfo = iter->second;
            KVulkanDescriptorSet* vkSet = (KVulkanDescriptorSet*)m_pVulkanRayTracingPipeline->GetDescriptorSet();
            if (vkSet && pUniformInfo)
            {
                vkSet->AddBindCBV(pUniformInfo->m_nSpace, pUniformInfo->m_nBinding, pCBV);
                ret = true;
            }
        }
        return ret;
    }

    bool KVulkanRayTracingProgram::BeginBind(IKGFX_RenderContext* pRenderCtx)
    {
        KVulkanDescriptorSet* vkSet = (KVulkanDescriptorSet*)m_pVulkanRayTracingPipeline->GetDescriptorSet();
        if (vkSet)
        {
            vkSet->Begin();
        }
        return true;
    }

    bool KVulkanRayTracingProgram::EndBind(IKGFX_RenderContext* commandBuffer)
    {
        ASSERT(IS_BINDLESS_ENABLED);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        bool bRetCode = pBindlessMgr->Flush();
        ASSERT(bRetCode);
        KVulkanDescriptorSet* vkSet = (KVulkanDescriptorSet*)m_pVulkanRayTracingPipeline->GetDescriptorSet();
        if (vkSet)
        {
            bRetCode = vkSet->End();
            ASSERT(bRetCode);
        }
        return true;
    }
    bool KVulkanRayTracingProgram::Apply(IKGFX_RenderContext* pRenderCtx)
    {
        bool bRetCode = false;
        bool bResult = false;
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        VkDevice pDevice = GetVkDevice();
        KVulkanCommandBuffer* commandBuffer = ((KVulkanRenderContext*)pRenderCtx)->GetVulkanCommandBuffer();
        KGLOG_PROCESS_ERROR(commandBuffer);

        {
            //bind bindless set
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            auto bindlessSet = pBindlessMgr->GetGlobalBindlessSet();
            vks::vkCmdBindDescriptorSets(commandBuffer->GetCommandBuffer(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pVulkanRayTracingPipeline->GetPipelineLayout(), 0, 1, &bindlessSet, 0, nullptr);

            //bind normal sets
            KVulkanDescriptorSet* vkSet = (KVulkanDescriptorSet*)m_pVulkanRayTracingPipeline->GetDescriptorSet();
            if (vkSet)
            {
                uint32_t l_uSetCount = static_cast<uint32_t>(vkSet->GetSetCount());
                for (uint32_t i = 0; i < l_uSetCount; i++)
                {
                    auto vkDescriptorSet = (vkSet->GetDescriptorSet(i));
                    vks::vkCmdBindDescriptorSets(commandBuffer->GetCommandBuffer(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pVulkanRayTracingPipeline->GetPipelineLayout(), 1 + i, 1, &vkDescriptorSet, 0, nullptr);
                }
            }
            vks::vkCmdBindPipeline(commandBuffer->GetCommandBuffer(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pVulkanRayTracingPipeline->GetPipeline());
        }
        bResult = true;
    Exit0:
        return bResult;
    }
	//auto KVulkanRayTracingProgram::BindCommonSampler(uint32_t uSet, uint32_t uBinding, IKGFX_Sampler* pSsampler) -> void
	//{
	//	CHECK_ASSERT(bIsBinding);
	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	CHECK_ASSERT(uSet < m_DescriptorSets->GetSetCount());
	//	m_DescriptorSets->AddBindSampler(uSet, uBinding, 1, &pSsampler);
	//}
	//
	//auto KVulkanRayTracingProgram::RegisterBindlessTexture( uint32_t dstArrayElement, KGfxTexture* pTexture) -> void
	//{
	//	KGfxFileTexture* pVulkanTexture = (KGfxFileTexture*)pTexture;
	//	VkWriteDescriptorSet write{};
	//	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	//	write.dstSet = m_bindlessSet;
	//	write.dstBinding = m_bindless_binding;
	//	write.dstArrayElement = dstArrayElement;
	//	write.descriptorCount = 1;
	//	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	//	m_bindless_update_cur_frame.emplace_back(write);
 //       VkDescriptorImageInfo sDescriptorImageInfo{};
 //       sDescriptorImageInfo.sampler = nullptr;
 //       sDescriptorImageInfo.imageView = (VkImageView)pVulkanTexture->GetSRV()->GetNativeHandle();
 //       sDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	//	m_copy_image_info.push_back(sDescriptorImageInfo);
	//}

	//auto KVulkanRayTracingProgram::BeginBind(IKGFX_RenderContext* pRenderCtx) -> void
	//{
	//	CHECK_ASSERT(!bIsBinding);
	//	m_currentUsingDescriptorSetID = ((KVulkanRenderContext*)pRenderCtx)->GetCommandBufferId();
	//	CHECK_ASSERT(m_currentUsingDescriptorSetID < m_frameDescriptorSets.size());
	//	m_frameDescriptorSets[m_currentUsingDescriptorSetID]->Begin();
	//	bIsBinding = true;
	//}
	//auto KVulkanRayTracingProgram::BindCommonCBV(uint32_t uSet, uint32_t uBinding, gfx::IKGFX_ConstBuffer* pUBO) -> void
	//{
	//	CHECK_ASSERT(bIsBinding);
	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	CHECK_ASSERT(uSet < m_DescriptorSets->GetSetCount());
	//	m_DescriptorSets->AddBindCBV(uSet,uBinding, pUBO->GetCBV());        
	//}
	//auto KVulkanRayTracingProgram::BindCommonSRV(uint32_t uSet, uint32_t uBinding, KGfxTexture* pTexture, bool bReadOnly) -> void
	//{
	//	CHECK_ASSERT(bIsBinding);

	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	CHECK_ASSERT(uSet < m_DescriptorSets->GetSetCount());
	//	if (bReadOnly)
	//	{
	//		m_DescriptorSets->AddBindSRV(uSet, uBinding, pTexture->GetSRV());
	//	}
	//	else
	//	{
	//		m_DescriptorSets->AddBindUAV(uSet, uBinding, pTexture->GetUAV());
	//	}
	//}
	//auto KVulkanRayTracingProgram::BindRayTracingScene(uint32_t uSet, uint32_t uBinding, KRayTracingScene* pRTScene) -> void
	//{
	//	CHECK_ASSERT(bIsBinding);
	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	CHECK_ASSERT(uSet < m_DescriptorSets->GetSetCount());
	//	auto vulkanScene = (KVulkanRayTracingScene*)pRTScene;
	//	CHECK_ASSERT(vulkanScene);
	//	m_DescriptorSets->AddBindAccelerationStructure(uSet, uBinding, vulkanScene->GetAcceleration());
	//}
	//auto KVulkanRayTracingProgram::BindCommonSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO) -> void
	//{
	//	CHECK_ASSERT(bIsBinding);
	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	CHECK_ASSERT(uSet < m_DescriptorSets->GetSetCount());
	//	m_DescriptorSets->AutoBindSSBO(uSet, uBinding, pSSBO, 0, 0, true);
	//}

	//auto KVulkanRayTracingProgram::EndBind(IKGFX_RenderContext* pRenderCtx) -> void
	//{
	//	CHECK_ASSERT(bIsBinding);
	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	m_DescriptorSets->End();
	//	bIsBinding = FALSE;
	//}
	//auto KVulkanRayTracingProgram::Apply(IKGFX_RenderContext* pRenderCtx) -> void
	//{
 //       KVulkanRenderContext* pRenderCtxVK = dynamic_cast<KVulkanRenderContext*>(pRenderCtx);
 //       CHECK_ASSERT(pRenderCtxVK);
	//	CHECK_ASSERT(m_pVulkanRayTracingPipeline);
	//	CHECK_ASSERT(((KVulkanRenderContext*)pRenderCtx)->GetCommandBufferId() == m_currentUsingDescriptorSetID);
	//	auto m_DescriptorSets = m_frameDescriptorSets[m_currentUsingDescriptorSetID];
	//	CHECK_ASSERT(m_DescriptorSets);
	//	CHECK_ASSERT(m_DescriptorSets->IsInited());
	//	//update bindless once per frame here
	//	uint32_t updateCount = (uint32_t)m_bindless_update_cur_frame.size();
	//	for (uint32_t i = 0; i < updateCount; i++)
	//	{
	//		m_bindless_update_cur_frame[i].pImageInfo = &m_copy_image_info[i];
	//	}
	//	vks::vkUpdateDescriptorSets(GetVkDevice(), updateCount, m_bindless_update_cur_frame.data(), 0, nullptr);
	//	m_bindless_update_cur_frame.clear();
	//	m_copy_image_info.clear();
	//	gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();	
	//	{
	//		CHECK_ASSERT(m_Layout);	
	//		for (uint32_t i = 0; i < m_DescriptorSets->GetSetCount(); i++)
	//		{
	//			uint32_t* pDynamicUBOOffsetsArray = m_DescriptorSets->GetDynamicUBOOffetArray(i);
	//			uint32_t  uDynamicUBOOffsetsArrayCounts = m_DescriptorSets->GetDynamicUBOOffsetArrayCount(i);
 //               pRenderCtxVK->CmdBindDescriptorSets(gfx::PIPELINE_BIND_POINT_RAY_TRACNG, m_Layout, i, m_DescriptorSets, uDynamicUBOOffsetsArrayCounts, pDynamicUBOOffsetsArray);
	//		}
	//		auto cb = ((KVulkanRenderContext*)pRenderCtx)->GetVulkanCommandBuffer();
	//		vks::vkCmdBindDescriptorSets(cb->GetCommandBuffer(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, ((KVulkanLayout*)m_Layout)->GetPipelineLayout(), 2, 1, &m_bindlessSet, 0, nullptr);
 //           pRenderCtxVK->CmdBindPipeline(gfx::PIPELINE_BIND_POINT_RAY_TRACNG, m_pVulkanRayTracingPipeline);
	//	}
	//}


	/**************** ray tracing geometry ****************************/
	static void FillBLASBuildData(RayTracingGeomerySegment* pSegments, uint32_t uSegmentsCount, VKBLASBuildData& buildData, enumRayTracingGeometryType eType, IKGFX_Buffer* indexBuffer, uint32_t uIndexOffset, enumIndexType eIndextype, enumAccelerationStructureBuildMode buildMode)
	{
		buildData.offsetInfos.clear();
		buildData.segmentsInfos.clear();
		VkDeviceAddress indexDataAddress{};
		uint32_t IndexStrideInBytes = 0;
		auto segmentCounts = uSegmentsCount;
		//at least one segment per geometry
		CHECK_ASSERT(segmentCounts >= 1);
		std::vector<uint32_t> primitiveCounts;
		for (uint32_t i = 0; i < segmentCounts; i++)
		{
			VkAccelerationStructureGeometryKHR segmentGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR , nullptr };
			const RayTracingGeomerySegment& Segment = pSegments[i];
			CHECK_ASSERT(Segment.pVertexBuffer);

			if (Segment.bOpaque == true)
			{
				segmentGeometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
			}

			uint32_t primitiveOffset = 0;
			switch (eType)
			{
			case KRT_GT_TRIANGLE:
				segmentGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
				segmentGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
				segmentGeometry.geometry.triangles.vertexFormat = RayTracingHelper::GetVertFormatRT(Segment.eVertFormat);
				segmentGeometry.geometry.triangles.vertexData.deviceAddress = (VkDeviceAddress)Segment.pVertexBuffer->GetBufferDeviceAddress() + Segment.uVertexBufferOffset;
				segmentGeometry.geometry.triangles.vertexStride = Segment.uVertexBufferStride;
				segmentGeometry.geometry.triangles.maxVertex = Segment.uVertexCount;
				// No support for segment transform
				segmentGeometry.geometry.triangles.transformData = {};
				if (indexBuffer)
				{
					indexDataAddress = (VkDeviceAddress)indexBuffer->GetBufferDeviceAddress() + uIndexOffset;
					IndexStrideInBytes = eIndextype == INDEX_TYPE_UINT32 ? 4 : 2;
					segmentGeometry.geometry.triangles.indexType = (IndexStrideInBytes == 2) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
					segmentGeometry.geometry.triangles.indexData.deviceAddress = indexDataAddress;
					primitiveOffset = Segment.uFirstPrimitive * IndexStrideInBytes;
				}
				else
				{
					segmentGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
					segmentGeometry.geometry.triangles.indexData.deviceAddress = 0;
					//read from vertex buffer
					primitiveOffset = Segment.uFirstPrimitive * Segment.uVertexBufferStride;
				}
				break;
			case KRT_GT_PROCEDURAL:
				segmentGeometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
				segmentGeometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
				segmentGeometry.geometry.aabbs.data.deviceAddress = (VkDeviceAddress)Segment.pVertexBuffer->GetBufferDeviceAddress() + Segment.uVertexBufferOffset;
				segmentGeometry.geometry.aabbs.stride = Segment.uVertexBufferStride;
				break;
			default:
				break;
			}
			VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{};
			buildOffsetInfo.firstVertex = 0;
			buildOffsetInfo.primitiveCount = Segment.uNumPrimitive;
			buildOffsetInfo.primitiveOffset = primitiveOffset;
			buildOffsetInfo.transformOffset = 0;
			buildData.segmentsInfos.emplace_back(segmentGeometry);
			buildData.offsetInfos.emplace_back(buildOffsetInfo);
			primitiveCounts.push_back(Segment.uNumPrimitive);
		}
		// get memory requirement, in 1.3 use calling vkGetAccelerationStructureBuildSizesKHR
		buildData.buildGeometryInfo_.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
		buildData.buildGeometryInfo_.mode = (buildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD) ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
		buildData.buildGeometryInfo_.geometryCount = (uint32_t)buildData.segmentsInfos.size();
		buildData.buildGeometryInfo_.pGeometries = buildData.segmentsInfos.data();//support in vulkan 1.3
		vks::vkGetAccelerationStructureBuildSizesKHR(
			(VkDevice)GetVulkanDevice()->GetLogiceDevicePtr(),
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&buildData.buildGeometryInfo_,
			primitiveCounts.data(),
			&buildData.sizesInfo);
		buildData.sizesInfo.accelerationStructureSize = RayTracingHelper::RTAlign(buildData.sizesInfo.accelerationStructureSize, AccelerationStructureAlignment);//enabled in vk 1.3
		buildData.sizesInfo.buildScratchSize = RayTracingHelper::RTAlign(buildData.sizesInfo.buildScratchSize, VulkanRayTracingProperties::MinAccelerationStructureScratchOffsetAlignment());//enabled in vk 1.3
		buildData.sizesInfo.updateScratchSize = RayTracingHelper::RTAlign(buildData.sizesInfo.updateScratchSize, VulkanRayTracingProperties::MinAccelerationStructureScratchOffsetAlignment());//enabled in vk 1.3

	}

	KVulkanRayTracingGeometry::~KVulkanRayTracingGeometry()
	{
		Destroy();
	}

	auto KVulkanRayTracingGeometry::Destroy() -> void
	{
		if (blas != VK_NULL_HANDLE)
		{
			vks::vkDestroyAccelerationStructureKHR(GetVkDevice(), blas, KVK_ALLOCATER);
			blas = VK_NULL_HANDLE;
		}
		if (accelerationStructureBuffer)
		{
			accelerationStructureBuffer->Release();
		}
	}

	// Todo: High level API call should have transitioned and verified vb and ib to read for each segment
	auto KVulkanRayTracingGeometry::Init(const RayTracingGeomeryCreateDesc& gdc, IKGFX_RenderContext* commandBuffer) -> BOOL
	{
		BOOL bRet = FALSE;
		geometryDesc = gdc;//simply copy
		FillBLASBuildData(geometryDesc.pSegments, geometryDesc.uSegmentsCount, blasBuildData, geometryDesc.eGeometryType, geometryDesc.pIndexBuffer, geometryDesc.uIndexBufferOffset, geometryDesc.eIndexType, enumAccelerationStructureBuildMode::KRT_BM_BUILD);
		VkDeviceSize structureSize = blasBuildData.sizesInfo.accelerationStructureSize;
		auto pIKGFXDevice = gfx::GetGraphicDevice();
		KGfxBufferDesc bufDesc{};
		bufDesc.uByteWidth = (uint32_t)structureSize;
		bufDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
		bRet = pIKGFXDevice->CreateBuffer(&accelerationStructureBuffer, bufDesc, nullptr);
		CHECK_ASSERT(bRet == TRUE);
		//creat empty
		VkAccelerationStructureCreateInfoKHR CreateInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		CreateInfo.buffer = ((KVulkanBuffer*)accelerationStructureBuffer)->GetVkBuffer();
		CreateInfo.offset = accelerationStructureBuffer->GetDynamicOffset();
		CreateInfo.size = structureSize;
		CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		vks::vkCreateAccelerationStructureKHR(GetVkDevice(), &CreateInfo, KVK_ALLOCATER, &blas);
		bRet = TRUE;
		return bRet;
	}
	//TODO: batch build outside , need to manage the map of true data and the class
	auto KVulkanRayTracingGeometry::Update(const RayTracingGeomeryUpdateParams& updateParam, IKGFX_RenderContext* commandBuffer) -> BOOL
	{
		CHECK_ASSERT(accelerationStructureBuffer);
		BOOL bRet = FALSE;
		if (updateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_UPDATE)
		{
			//validate the segments are legal
			CHECK_ASSERT(updateParam.uSegmentsCount == geometryDesc.uSegmentsCount);// If updated segments are provided, they must exactly match existing geometry segments. Only vertex buffer bindings may change.
		}
		geometryDesc.pSegments = updateParam.pSegments;
        geometryDesc.pIndexBuffer = updateParam.pIndexBuffer;
        geometryDesc.uIndexBufferOffset = updateParam.uIndexBufferOffset;
		FillBLASBuildData(geometryDesc.pSegments, geometryDesc.uSegmentsCount, blasBuildData, geometryDesc.eGeometryType, geometryDesc.pIndexBuffer, geometryDesc.uIndexBufferOffset, geometryDesc.eIndexType, updateParam.eBuildMode);
		CHECK_ASSERT(blasBuildData.sizesInfo.accelerationStructureSize <= accelerationStructureBuffer->GetDesc()->uByteWidth);
		blasBuildData.buildGeometryInfo_.srcAccelerationStructure = updateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? VK_NULL_HANDLE : blas;
		blasBuildData.buildGeometryInfo_.dstAccelerationStructure = blas;
		bRet = TRUE;
		return bRet;
	}
	auto KVulkanRayTracingGeometry::FetchBuildData() -> const VKBLASBuildData&
	{
		return blasBuildData;
	}
	auto KVulkanRayTracingGeometry::CompactAccelerationStructure(IKGFX_RenderContext* commandBuffer, uint64_t uSizeAfterCompaction) -> void
	{
		CHECK_ASSERT(geometryDesc.bAllowUpdate == FALSE);
	}

	/***************************************************************************/

	KVulkanRayTracingScene::~KVulkanRayTracingScene()
	{
		Destroy();
	}

	/**************** ray tracing scene ****************************/
	static void FillTLASBuildData(
		const VkDevice Device,
		const uint32_t NumInstances,
		const VkDeviceAddress InstanceBufferAddress,
		enumAccelerationStructureBuildMode BuildMode,
		VKTLASBuildData& BuildData)
	{
		VkDeviceOrHostAddressConstKHR InstanceBufferDeviceAddress = {};
		InstanceBufferDeviceAddress.deviceAddress = InstanceBufferAddress;
		BuildData.Geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		BuildData.Geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		BuildData.Geometry.geometry.instances.arrayOfPointers = VK_FALSE;
		BuildData.Geometry.geometry.instances.data = InstanceBufferDeviceAddress;
		BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		BuildData.GeometryInfo.mode = BuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
		BuildData.GeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;//TODO: outside param control
		BuildData.GeometryInfo.geometryCount = 1;
		BuildData.GeometryInfo.pGeometries = &BuildData.Geometry;
		vks::vkGetAccelerationStructureBuildSizesKHR(
			Device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&BuildData.GeometryInfo,
			&NumInstances,
			&BuildData.SizesInfo);
	}

	auto KVulkanRayTracingScene::Create(const RayTracingSceneCreateDesc& RTCDC, IKGFX_RenderContext* commandBuffer) -> BOOL
	{
		BOOL bRet = FALSE;
		rtSceneDesc = RTCDC;//simpy copy
		const VkDeviceAddress InstanceBufferAddress = 0;// we don't need an actual device address when just fetch the size infos.
		FillTLASBuildData((VkDevice)GetVulkanDevice()->GetLogiceDevicePtr(), rtSceneDesc.uMaxGeometryInstanceCount, InstanceBufferAddress, enumAccelerationStructureBuildMode::KRT_BM_BUILD, tlasBuildData);
		//create empty
		auto pIKGFXDevice = gfx::GetGraphicDevice();
		auto accelerationStructureSize = RayTracingHelper::RTAlign(tlasBuildData.SizesInfo.accelerationStructureSize, AccelerationStructureAlignment);
		KGfxBufferDesc bufDesc{};
		bufDesc.uByteWidth = (uint32_t)accelerationStructureSize;
		bufDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
		bRet = pIKGFXDevice->CreateBuffer(&accelerationStructureBuffer, bufDesc, nullptr);
		CHECK_ASSERT(bRet == TRUE);
		//creat empty
		VkAccelerationStructureCreateInfoKHR CreateInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		CreateInfo.buffer = ((KVulkanBuffer*)accelerationStructureBuffer)->GetVkBuffer();
		CreateInfo.offset = accelerationStructureBuffer->GetDynamicOffset();//0 for non dynamic buffer
		CreateInfo.size = accelerationStructureSize;
		CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		vks::vkCreateAccelerationStructureKHR(GetVkDevice(), &CreateInfo, KVK_ALLOCATER, &tlas);

		//creat empty instance buffer
		bufDesc.uByteWidth = rtSceneDesc.uMaxGeometryInstanceCount * sizeof(VkAccelerationStructureInstanceKHR);// when exceed this count, need a fully recreate
		bufDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | gfx::BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
		bRet = pIKGFXDevice->CreateBuffer(&instanceBuffer, bufDesc, nullptr);
		CHECK_ASSERT(bRet == TRUE);
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        m_uBindlessHandle = pBindlessMgr->RequestResourceBindlessSolt();
		bRet = TRUE;
		return bRet;
	}
	auto KVulkanRayTracingScene::Destroy() -> void
	{
        if (m_uBindlessHandle != UINT32_MAX)
        {
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            pBindlessMgr->ReleaseResourceBindlessSlot(m_uBindlessHandle);
            m_uBindlessHandle = UINT32_MAX;
        }
		if (instanceBuffer)
		{
			instanceBuffer->Release();
			instanceBuffer = nullptr;
		}
		if (tlas != VK_NULL_HANDLE)
		{
			vks::vkDestroyAccelerationStructureKHR(GetVkDevice(), tlas, KVK_ALLOCATER);
			tlas = VK_NULL_HANDLE;
		}
        SAFE_RELEASE(accelerationStructureBuffer);
	}
	auto KVulkanRayTracingScene::FetchBuildData() -> const VKTLASBuildData&
	{
		return tlasBuildData;
	}
	auto KVulkanRayTracingScene::Update(const RayTracingSceneUpdateParams& params, IKGFX_RenderContext* commandBuffer) -> BOOL
	{
		CHECK_ASSERT(accelerationStructureBuffer);
		CHECK_ASSERT(instanceBuffer);//need a valid instance buffer
		CHECK_ASSERT(params.uInstanceCount <= rtSceneDesc.uMaxGeometryInstanceCount);// must not exceed the max count, or you need a fully rebuild, instead of update.
        CHECK_ASSERT(commandBuffer);
		BOOL bRet = FALSE;
		const bool bIsUpdate = params.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_UPDATE;
		if (bIsUpdate)
		{
			CHECK_ASSERT(params.uInstanceCount == uCurrentNumInstances);
		}
		else
		{
			uCurrentNumInstances = params.uInstanceCount;
		}
		// build or update instance buffer here!
		// Top level acceleration structure
		instances.clear();
		for (uint32_t i = 0; i < params.uInstanceCount; i++)
		{
			instances.emplace_back(CreateInstance(params.pInstance[i]));
		}
		//update the instance buffer with initial data, and add barrier for it. 
		//update the instance buffer

		if (instances.size() > 0)
		{
            commandBuffer->CmdUpdateSubResource(instanceBuffer, 0, (uint32_t)instances.size() * sizeof(instances[0]), instances.data());
		}
		
		FillTLASBuildData((VkDevice)GetVulkanDevice()->GetLogiceDevicePtr(), params.uInstanceCount, (VkDeviceAddress)instanceBuffer->GetBufferDeviceAddress(), params.eBuildMode, tlasBuildData);
		CHECK_ASSERT(tlasBuildData.SizesInfo.accelerationStructureSize <= accelerationStructureBuffer->GetDesc()->uByteWidth);
		tlasBuildData.GeometryInfo.dstAccelerationStructure = tlas;
		tlasBuildData.GeometryInfo.srcAccelerationStructure = bIsUpdate ? tlas : VK_NULL_HANDLE;
		if (!tlasBuildData.pBuildRangeInfos)
		{
			tlasBuildData.pBuildRangeInfos = new VkAccelerationStructureBuildRangeInfoKHR;
		}
		tlasBuildData.pBuildRangeInfos->primitiveCount = params.uInstanceCount;
		tlasBuildData.pBuildRangeInfos->primitiveOffset = 0;
		tlasBuildData.pBuildRangeInfos->transformOffset = 0;
		tlasBuildData.pBuildRangeInfos->firstVertex = 0;
		return TRUE;
	}

	auto KVulkanRayTracingScene::CreateInstance(const RayTracingInstance& instance) -> VkAccelerationStructureInstanceKHR
	{
		VkAccelerationStructureInstanceKHR  retInstance{};
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
		addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		auto geo = (KVulkanRayTracingGeometry*)instance.pGeometry;
		CHECK_ASSERT(geo);
		addressInfo.accelerationStructure = geo->blas;
		const VkDeviceAddress address = vks::vkGetAccelerationStructureDeviceAddressKHR(GetVkDevice(), &addressInfo);
		retInstance.instanceCustomIndex = instance.uInstanceID;
		retInstance.mask = 0xFF;
		retInstance.instanceShaderBindingTableRecordOffset = instance.uHitGroupOffset;
		retInstance.accelerationStructureReference = address;
        if (instance.bForceOpaque)
        {
            retInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        }
        
		// the instance.transform only has 12 value, a 4x3 matrix
		// the last row is always (0,0,0,1)
		//need row major matrix input
		const NSKMath::KMatrix34 trans34Mat = *instance.pTransform;
		std::memcpy(&retInstance.transform, &trans34Mat, sizeof(retInstance.transform));
		return retInstance;
	}
	/***************************************************************************/

	KVulkanShaderBindingTable::~KVulkanShaderBindingTable()
	{
		Destroy();
	}

	auto KVulkanShaderBindingTable::Create(const ShaderBindingTableDesc& SBTDC) -> BOOL
	{
        CHECK_ASSERT(SBTDC.uCallableRecordAlignedSizeInByte >= 16);
        CHECK_ASSERT(SBTDC.uCallableRecordAlignedSizeInByte <= 512);
        CHECK_ASSERT(SBTDC.uHitRecordAlignedSizeInByte >= 16);
        CHECK_ASSERT(SBTDC.uHitRecordAlignedSizeInByte <= 512);
        CHECK_ASSERT(SBTDC.uMissRecordAlignedSizeInByte >= 16);
        CHECK_ASSERT(SBTDC.uMissRecordAlignedSizeInByte <= 512);
        CHECK_ASSERT(SBTDC.uRayGenRecordAlignedSizeInByte >= 16);
        CHECK_ASSERT(SBTDC.uRayGenRecordAlignedSizeInByte <= 512);
        auto InitAlloc = [](KVulkanShaderTableAllocation& Alloc, uint32_t InHandleCount, uint32_t customStride)
            {
                Alloc.handleCount = InHandleCount;
                Alloc.bUseLocalRecord = true;

                if (Alloc.handleCount > 0)
                {
                    Alloc.region.stride = (Alloc.handleCount > 0) ? customStride : 0;
                    Alloc.region.size = Alloc.handleCount * static_cast<VkDeviceSize>(customStride);

                    // Host buffer
                    Alloc.hostBuffer.resize(Alloc.region.size);
                }
            };
        if (SBTDC.uInitHitGroupCount > 0)
        {
            InitAlloc(hitGroup, SBTDC.uInitHitGroupCount, SBTDC.uHitRecordAlignedSizeInByte);
        }
        if (SBTDC.uInitRayGenCount > 0)
        {
            InitAlloc(rayGen, SBTDC.uInitRayGenCount, SBTDC.uRayGenRecordAlignedSizeInByte);
        }
        if (SBTDC.uInitMissCount > 0)
        {
            InitAlloc(miss, SBTDC.uInitMissCount, SBTDC.uMissRecordAlignedSizeInByte);
        }
        if (SBTDC.uInitCallableCount > 0)
        {
            InitAlloc(callable, SBTDC.uInitCallableCount, SBTDC.uCallableRecordAlignedSizeInByte);
        }
        return TRUE;
	}

	auto KVulkanShaderBindingTable::Destroy() -> void
	{
        _ReleaseLocalBuffer(hitGroup);
        _ReleaseLocalBuffer(miss);
        _ReleaseLocalBuffer(rayGen);
        _ReleaseLocalBuffer(callable);
	}

	auto KVulkanShaderBindingTable::GetRayGenShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData) -> void
	{
		
	}

	auto KVulkanShaderBindingTable::GetHitShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData) -> void
	{
	
	}

	auto KVulkanShaderBindingTable::GetMissShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData) -> void
	{
		
	}

	auto KVulkanShaderBindingTable::GetCallableShaderBindingTable(VkStridedDeviceAddressRegionKHR& outData) -> void
	{
	}

    KVulkanShaderBindingTable::KVulkanShaderTableAllocation& KVulkanShaderBindingTable::_GetAlloc(enumRayTracingShaderType sType)
    {
        switch (sType)
        {
        case gfx::KRT_ST_RAY_GEN:
            return rayGen;
            break;
        case gfx::KRT_ST_HIT_GROUP:
            return hitGroup;
            break;
        case gfx::KRT_ST_MISS:
            return miss;
            break;
        case gfx::KRT_ST_CALLABLE:
            return callable;
            break;
        default:
            CHECK_ASSERT(false);
            break;
        }
        static KVulkanShaderTableAllocation EmptyAlloc;
        return EmptyAlloc;
    }

    void KVulkanShaderBindingTable::_ReleaseLocalBuffer(KVulkanShaderTableAllocation& _alloc)
    {
        if (_alloc.localBuffer != nullptr)
        {
            SAFE_RELEASE(_alloc.localBuffer);
        }
        _alloc.region.deviceAddress = 0;
    }

    bool KVulkanShaderBindingTable::_SetBindingsOnShaderBindingTable(const KRayTracingShaderBinding& binding, KVulkanRayTracingPipeline* pipeline, enumRayTracingShaderType sType)
    {
        bool bResult = false;
        KVulkanShaderTableAllocation& targetAlloc = _GetAlloc(sType);
        uint32_t handleSize = pipeline->GetShaderGroupHandleSize();
        const std::vector<uint8_t>& srcHandleData = pipeline->GetShaderData(sType).ShaderHandles;
        KG_PROCESS_ERROR(pipeline);
        KG_PROCESS_ERROR(handleSize > 0);
        KG_PROCESS_ERROR(!srcHandleData.empty());
        KG_PROCESS_ERROR(targetAlloc.region.stride != 0);//Attempting to index a record in a region without stride
        {
            //set shader slot
            size_t dataOffset = static_cast<size_t>(binding.uShaderIndexInPipeline) * handleSize;
            size_t targetOffset = static_cast<size_t>(binding.uRecordIndex) * targetAlloc.region.stride;
            KG_PROCESS_ERROR(dataOffset + handleSize <= srcHandleData.size());
            KG_PROCESS_ERROR(targetOffset + targetAlloc.region.stride <= targetAlloc.hostBuffer.size());
            memcpy(&targetAlloc.hostBuffer[targetOffset], &srcHandleData[dataOffset], handleSize);
        }
        {
            //set local data
            uint32_t l_uShaderHandleSizeAligned = pipeline->GetShaderGroupHandleSizeAligned();
            uint64_t data[] = { binding.pLocalVertexBuffer->GetBufferDeviceAddress() ,binding.pLocalIndexBuffer->GetBufferDeviceAddress() ,
            binding.pLocalMaterialBindlessIDCbuffer->GetBufferDeviceAddress() ,binding.pLocalEngineBindlessIDCbuffer->GetBufferDeviceAddress(),
            binding.pLocalCustomInfoCbuffer->GetBufferDeviceAddress() };
            KG_PROCESS_ERROR(l_uShaderHandleSizeAligned + sizeof(data) <= targetAlloc.region.stride);
            size_t targetOffset = static_cast<size_t>(binding.uRecordIndex) * targetAlloc.region.stride + l_uShaderHandleSizeAligned;
            memcpy(&targetAlloc.hostBuffer[targetOffset], data, sizeof(data));
        }
        targetAlloc.bIsDirty = true;
        bResult = true;
    Exit0:
        return bResult;
    }


    bool KVulkanShaderBindingTable::SetShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* program, enumRayTracingShaderType sType)
    {
        bool bRetCode = false;
        bool bResult = false;
        KVulkanRayTracingProgram* vulkanProgram = nullptr;
        KVulkanRayTracingPipeline* vulkanPipeline = nullptr;
        KG_PROCESS_ERROR(program);
        vulkanProgram = static_cast<KVulkanRayTracingProgram*>(program);
        vulkanPipeline = vulkanProgram->GetVulkanPipeline();
        KG_PROCESS_ERROR(vulkanPipeline);
        bRetCode = _SetBindingsOnShaderBindingTable(binding, vulkanPipeline, sType);
        KG_PROCESS_ERROR(bRetCode);
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanShaderBindingTable::SetHitGroupBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* program)
    {
        bool bRetCode = false;
        bool bResult = false;
        KVulkanRayTracingProgram* vulkanProgram = nullptr;
        KVulkanRayTracingPipeline* vulkanPipeline = nullptr;
        KG_PROCESS_ERROR(program);
        vulkanProgram = static_cast<KVulkanRayTracingProgram*>(program);
        vulkanPipeline = vulkanProgram->GetVulkanPipeline();
        KG_PROCESS_ERROR(vulkanPipeline);
        bRetCode = _SetBindingsOnShaderBindingTable(binding, vulkanPipeline, enumRayTracingShaderType::KRT_ST_HIT_GROUP);
        KG_PROCESS_ERROR(bRetCode);
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanShaderBindingTable::SetMissShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* program)
    {
        bool bRetCode = false;
        bool bResult = false;
        KVulkanRayTracingProgram* vulkanProgram = nullptr;
        KVulkanRayTracingPipeline* vulkanPipeline = nullptr;
        KG_PROCESS_ERROR(program);
        vulkanProgram = static_cast<KVulkanRayTracingProgram*>(program);
        vulkanPipeline = vulkanProgram->GetVulkanPipeline();
        KG_PROCESS_ERROR(vulkanPipeline);
        bRetCode = _SetBindingsOnShaderBindingTable(binding, vulkanPipeline, enumRayTracingShaderType::KRT_ST_MISS);
        KG_PROCESS_ERROR(bRetCode);
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanShaderBindingTable::SetRayGenShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* program)
    {
        bool bRetCode = false;
        bool bResult = false;
        KVulkanRayTracingProgram* vulkanProgram = nullptr;
        KVulkanRayTracingPipeline* vulkanPipeline = nullptr;
        KG_PROCESS_ERROR(program);
        vulkanProgram = static_cast<KVulkanRayTracingProgram*>(program);
        vulkanPipeline = vulkanProgram->GetVulkanPipeline();
        KG_PROCESS_ERROR(vulkanPipeline);
        bRetCode = _SetBindingsOnShaderBindingTable(binding, vulkanPipeline, enumRayTracingShaderType::KRT_ST_RAY_GEN);
        KG_PROCESS_ERROR(bRetCode);
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanShaderBindingTable::SetCallableShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* program)
    {
        bool bRetCode = false;
        bool bResult = false;
        KVulkanRayTracingProgram* vulkanProgram = nullptr;
        KVulkanRayTracingPipeline* vulkanPipeline = nullptr;
        KG_PROCESS_ERROR(program);
        vulkanProgram = static_cast<KVulkanRayTracingProgram*>(program);
        vulkanPipeline = vulkanProgram->GetVulkanPipeline();
        KG_PROCESS_ERROR(vulkanPipeline);
        bRetCode = _SetBindingsOnShaderBindingTable(binding, vulkanPipeline, enumRayTracingShaderType::KRT_ST_CALLABLE);
        KG_PROCESS_ERROR(bRetCode);
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanShaderBindingTable::CommitShaderBindingTable(IKGFX_RenderContext* commandBuffer)
    {
        bool bRetCode = false;
        bool bResult = false;
        auto l_pGraphicsDevice = KGFX_GetGraphicDevice();
        KG_PROCESS_ERROR(l_pGraphicsDevice);
        {
            auto CommitBuffer = [commandBuffer, l_pGraphicsDevice, this](KVulkanShaderTableAllocation& Alloc)
                {
                    bool bRetCode = false;
                    bool bResult = false;
                    if (Alloc.bIsDirty)
                    {
                        if (!Alloc.hostBuffer.empty())
                        {
                            if (!Alloc.localBuffer)
                            {
                                KGfxBufferDesc l_sBufferDesc{};
                                l_sBufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
                                l_sBufferDesc.uByteWidth = static_cast<uint32_t>(Alloc.region.size);
                                l_sBufferDesc.uUsageFlags = BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | BUFFER_USAGE_TRANSFER_DST_BIT;
                                bRetCode = l_pGraphicsDevice->CreateBuffer(&Alloc.localBuffer, l_sBufferDesc, nullptr);
                                KG_PROCESS_ERROR(bRetCode);
                                Alloc.region.deviceAddress = Alloc.localBuffer->GetBufferDeviceAddress();
                            }
                            bRetCode = Alloc.localBuffer->Update(Alloc.hostBuffer.data(), static_cast<uint32_t>(Alloc.hostBuffer.size()), 0, true);
                            KG_PROCESS_ERROR(bRetCode);
                            bRetCode = commandBuffer->Transition({ Alloc.localBuffer, gfx::KGfxAccess::CopyDst, gfx::KGfxAccess::SBTRead });
                            KG_PROCESS_ERROR(bRetCode);
                        }
                        else
                        {
                            _ReleaseLocalBuffer(Alloc);
                        }
                        Alloc.bIsDirty = false;
                    }
                    bResult = true;
                Exit0:
                    return bResult;
                };
            bRetCode = CommitBuffer(hitGroup);
            KG_PROCESS_ERROR(bRetCode);

            bRetCode = CommitBuffer(miss);
            KG_PROCESS_ERROR(bRetCode);

            bRetCode = CommitBuffer(callable);
            KG_PROCESS_ERROR(bRetCode);

            bRetCode = CommitBuffer(rayGen);
            KG_PROCESS_ERROR(bRetCode);
        }
        bResult = true;
    Exit0:
        return bResult;
    }


	/***************************************************************************/




	/************************************* pub interfaces *******************************************/
    auto VulkanRayTracingProxy::CreateRHIRayTracingGeomtry() -> KRayTracingGeomery*
    {
        KVulkanRayTracingGeometry* retGeo = new KVulkanRayTracingGeometry;
        return retGeo;
    }

	auto VulkanRayTracingProxy::InitRHIRayTracingGeometry(const RayTracingGeomeryCreateDesc& createDesc, KRayTracingGeomery* pRHIGeometry) -> bool
	{
		BOOL bResult = FALSE;
		KVulkanRayTracingGeometry* retGeo = static_cast<KVulkanRayTracingGeometry*>(pRHIGeometry);
		bResult = retGeo->Init(createDesc);
		return bResult;
	}
	static uint64_t totalBufferSize = 0;
	auto VulkanRayTracingProxy::CommitRHIRayTracingGeometries(const RayTracingGeometryUpdateBatch& updateBatch, IKGFX_RenderContext* commandBuffer) -> BOOL
	{
        CHECK_ASSERT(commandBuffer);
		CHECK_ASSERT(updateBatch.uGeometryCount == updateBatch.uUpdateParamCount);
		BOOL bResult = FALSE;
		std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeometryInfos;
		std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangeInfos;
		uint32_t scratchSize = 0;
		for (uint32_t i = 0; i < updateBatch.uGeometryCount; i++)
		{
			const auto& perGeoParam = updateBatch.pPerGeometryUpdateParams[i];
			((KVulkanRayTracingGeometry*)updateBatch.ppGeometries[i])->Update(perGeoParam, commandBuffer);
			auto& buildParam = ((KVulkanRayTracingGeometry*)updateBatch.ppGeometries[i])->FetchBuildData();

			const VkAccelerationStructureBuildRangeInfoKHR* pBuildRanges = buildParam.offsetInfos.data();
			VkAccelerationStructureBuildGeometryInfoKHR geometryInfo = buildParam.buildGeometryInfo_;
			//record a relative offset
			geometryInfo.scratchData.deviceAddress = scratchSize;
			auto l_scratchSize = (perGeoParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? buildParam.sizesInfo.buildScratchSize : buildParam.sizesInfo.updateScratchSize);
			scratchSize += (uint32_t)l_scratchSize;
			buildRangeInfos.emplace_back(pBuildRanges);
			buildGeometryInfos.emplace_back(geometryInfo);
		}
		KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
		IKGFX_Buffer* accelerationStructureScratchBuffer = nullptr;
		//allocate a scratch buffer for building
		//TODO: make it reused or dynamic buffer for reducing VM cost and better performance
		KGfxBufferDesc bufDesc{};
		bufDesc.uByteWidth = scratchSize;
		bufDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | gfx::BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | gfx::BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
		bResult = pGraphicDevice->CreateBuffer(&accelerationStructureScratchBuffer, bufDesc, nullptr);
		totalBufferSize += scratchSize;
		CHECK_ASSERT(bResult == TRUE);
		VkDeviceAddress scratchAddress = (VkDeviceAddress)accelerationStructureScratchBuffer->GetBufferDeviceAddress();
		//fix the true device address for each geometry

		for (uint32_t i = 0; i < updateBatch.uGeometryCount; i++)
		{
			auto offset = buildGeometryInfos[i].scratchData.deviceAddress;
			buildGeometryInfos[i].scratchData.deviceAddress = scratchAddress + offset;
		}
		auto cb = ((KVulkanRenderContext*)commandBuffer)->GetVulkanCommandBuffer();


		CHECK_ASSERT(cb != nullptr);
		vks::vkCmdBuildAccelerationStructuresKHR(cb->GetCommandBuffer(), (uint32_t)buildGeometryInfos.size(), buildGeometryInfos.data(), buildRangeInfos.data());

		//add barrier or something to ensure that all blas is completely built up before any use of it on gpu
        RayTracingHelper::AddAccelerationStructureBuildBarrier(cb->GetCommandBuffer());
		//after build, release the scratch buffer
		//It's a delay release
		accelerationStructureScratchBuffer->Release();
		//TODO : try to compact all the blas after build them
		// it's need to query the actualsize use query pool and adjust the actual buffer size to avoid waste
		// only can be aply on the blas which can not be updated
		// anyway it's an optional op
		bResult = TRUE;
		return bResult;
	}
	auto VulkanRayTracingProxy::CreateRHIRayTracingScene(const RayTracingSceneCreateDesc& createDesc) -> KRayTracingScene*
	{
		BOOL bResult = FALSE;
		KVulkanRayTracingScene* retScene = new KVulkanRayTracingScene;
		bResult = retScene->Create(createDesc);
		if (!bResult)
		{
			retScene->Destroy();
			SAFE_DELETE(retScene);
		}
		return retScene;
	}
	auto VulkanRayTracingProxy::CommitRHIRayTracingScene(const RayTracingSceneUpdateParams& updateParam, IKGFX_RenderContext* commandBuffer) -> BOOL
	{
        CHECK_ASSERT(commandBuffer);
		BOOL bRet = FALSE;
		IKGFX_Buffer* scratchBuffer = nullptr;
		((KVulkanRayTracingScene*)updateParam.pScene)->Update(updateParam, commandBuffer);
		const bool bIsUpdate = updateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_UPDATE;
		auto& tlasBuildData = ((KVulkanRayTracingScene*)(updateParam.pScene))->FetchBuildData();
		uint64_t scratchBufferSize = 0;
		if (bIsUpdate)
		{
			scratchBufferSize = tlasBuildData.SizesInfo.updateScratchSize;
		}
		else
		{
			scratchBufferSize = tlasBuildData.SizesInfo.buildScratchSize;
		}
		KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
		//create scratch buffer
		KGfxBufferDesc bufDesc{};
		bufDesc.uByteWidth = (uint32_t)scratchBufferSize;
        bufDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | gfx::BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | gfx::BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
		bRet = pGraphicDevice->CreateBuffer(&scratchBuffer, bufDesc, nullptr);
		VkAccelerationStructureBuildGeometryInfoKHR geometryInfo;
		geometryInfo = tlasBuildData.GeometryInfo;
		geometryInfo.scratchData.deviceAddress = (VkDeviceAddress)scratchBuffer->GetBufferDeviceAddress();
		//build
		auto cb = ((KVulkanRenderContext*)commandBuffer)->GetVulkanCommandBuffer();

		CHECK_ASSERT(cb != nullptr);
        RayTracingHelper::AddAccelerationStructureBuildBarrier(cb->GetCommandBuffer());
		vks::vkCmdBuildAccelerationStructuresKHR(cb->GetCommandBuffer(), 1, &geometryInfo, &tlasBuildData.pBuildRangeInfos);
        RayTracingHelper::AddAccelerationStructureBuildBarrier(cb->GetCommandBuffer());
		scratchBuffer->Release();
		bRet = TRUE;
		return bRet;
	}

	VulkanRayTracingProxy::VulkanRayTracingProxy()
	{
		//can also late load rt funcs here !
		VulkanRayTracingProperties::accelProps_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		VulkanRayTracingProperties::pipelineProps_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
		VulkanRayTracingProperties::pipelineProps_.pNext = &VulkanRayTracingProperties::accelProps_;

		VkPhysicalDeviceProperties2 props = {};
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props.pNext = &VulkanRayTracingProperties::pipelineProps_;
		vks::vkGetPhysicalDeviceProperties2((VkPhysicalDevice)(GetVulkanDevice()->GetPhysicalDevicePtr()), &props);
	}
	auto VulkanRayTracingProxy::CommitRayTracingProgram(const RayTracingProgramDesc& rtpDC) -> KRayTracingProgram*
	{
		KVulkanRayTracingProgram* vulkanProgram = new KVulkanRayTracingProgram;
		vulkanProgram->Create(rtpDC);
		return vulkanProgram;
	}
	auto VulkanRayTracingProxy::TraceRay(IRayTracingShader* pRayGenShader, IRayTracingShader* pMissShader, IRayTracingShader* pCallableShader, KRayTracingProgram* rayTracingProgram, KShaderBindingTable* shaderBindingTable, uint32_t width, uint32_t height, IKGFX_RenderContext* commandBuffer) -> bool
	{
		KVulkanShaderBindingTable* vulkanSBT = (KVulkanShaderBindingTable*)shaderBindingTable;
		KVulkanRenderContext* ctxVK = (KVulkanRenderContext*)commandBuffer;
		CHECK_ASSERT(vulkanSBT);
		CHECK_ASSERT(ctxVK);
		// Describe the shader binding table.
		VkStridedDeviceAddressRegionKHR raygenShaderBindingTable{};
		vulkanSBT->GetRayGenShaderBindingTable(raygenShaderBindingTable);

		VkStridedDeviceAddressRegionKHR missShaderBindingTable = {};
		vulkanSBT->GetMissShaderBindingTable(missShaderBindingTable);


		VkStridedDeviceAddressRegionKHR hitShaderBindingTable = {};
		vulkanSBT->GetHitShaderBindingTable(hitShaderBindingTable);

		VkStridedDeviceAddressRegionKHR callableShaderBindingTable = {};
		vulkanSBT->GetCallableShaderBindingTable(callableShaderBindingTable);
		// Execute ray tracing shaders.
		vks::vkCmdTraceRaysKHR(ctxVK->GetCommandBufferVk(),
			&raygenShaderBindingTable, &missShaderBindingTable, &hitShaderBindingTable, &callableShaderBindingTable,
			width, height, 1);
        return true;
		// Acquire output image
		/*ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
			VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);*/
	}
	auto VulkanRayTracingProxy::CreateRHIShaderBindingTable(const ShaderBindingTableDesc& SBTDC, KRayTracingProgram* program) -> KShaderBindingTable*
	{
		BOOL bResult = FALSE;
		KVulkanShaderBindingTable* retBindTable = new KVulkanShaderBindingTable;
		bResult = retBindTable->Create(SBTDC/*, program*/);
		if (!bResult)
		{
			SAFE_DELETE(retBindTable);
		}
		return retBindTable;
	}
	auto VulkanRayTracingProxy::CreateRayTracingShader(const KRayTracingShaderCreateDesc& ci)->IRayTracingShader*
	{
		BOOL bResult = FALSE;
		KVulkanRayTracingShader* retShader = new KVulkanRayTracingShader;
		bResult = retShader->Create(ci);
		if (!bResult)
		{
			SAFE_DELETE(retShader);
		}
		return retShader;
	}
   
	/*************************************************************************************************/

   
}
