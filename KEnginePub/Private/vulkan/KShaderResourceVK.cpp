#define NV_EXTENSIONS
#define ENABLE_HLSL 1
#include <ShaderLang.h>
#include <Types.h>
#include "KShaderResourceVK.h"
#include "Engine/KGLog.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "KBase/Public/str/KStrSafe.h"
#define AMD_EXTENSIONS
#include "../vulkan/GFXVulkan.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/io/KMetaData.h"
#include "KShaderResourcePoolVK.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KSpirv/Private/KSpirvBuilder.h"
#include "KMaterialSystem/Private/KMaterialSystem.h"
#include "../IGFX_Private.h"
#include "../vulkan/KGraphicDevice.h"
#include "../vulkan/KVulkanRenderContext.h"
#include "../vulkan/KVulkanProgram.h"
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include "../comm/KGFX_ShaderCombinedResult.h"

//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "../../Public/IKEnginePerformance.h"
#include "KBase/Public/KMemLeak.h"



#define SHADER_RESOURCE_REFLECT_DATA_MASK0 123456780
#define SHADER_RESOURCE_REFLECT_DATA_MASK1 123456789
using namespace gfx;
uint32_t To16ByteAlignSize(uint32_t bytes)
{
    uint32_t uSize = 0;
    if (bytes)
    {
        uint32_t n = bytes / 16;
        uint32_t m = bytes % 16;
        if (m > 0)
        {
            n++;
        }
        uSize = n * 16;
    }
    return uSize;
}


std::atomic<uint32_t> gShaderResourceUpdateCheckCode{0};
KShaderResourceVK::KShaderResourceVK()
{
    m_nRef     = 1;
    m_bLoaded  = FALSE;
    m_bCreated = FALSE;
    m_bOrphan  = FALSE;

    for (uint32_t i = 0; i < MAX_SHADER_STAGE_COUNT; ++i)
    {
        m_pShaderStage[i] = nullptr;
    }

    m_pPushConstantBlock = nullptr;
    m_pLayout            = nullptr;
    m_uHashCode          = 0;
    m_bGpuInstance       = false;

    m_id               = ++gShaderResourceUpdateCheckCode;
    m_uUpdateCheckCode = m_id;
    m_bParamInited     = false;
    m_nMaxBinding      = -1;
#if ExternalRenderPassPipeLineGC
    m_nLastGCFrameCount = 0;
#endif
    m_vsPushConstantSize = 0;
    m_pVsTmpData         = nullptr;
    for (uint32_t i = 0; i < SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT; ++i)
    {
        m_bRefelcted[i] = false;
    }

    m_pTempVsSpirv                 = nullptr;
    m_uTempVsSpirvIntCount         = 0;
    m_pCombinedShaderResultVK_HLSL = new gfx::KGFX_CombinedShaderResultVK_HLSL;
    m_bHasPerMtlUBO                = false;
    m_uStageCount                  = 0;
}

KShaderResourceVK::~KShaderResourceVK()
{
    PROF_CPU();
    Clear();
}

int32_t KShaderResourceVK::AddRef()
{
    // ASSERT(m_nRef > 0);
    return ++m_nRef;
}

int32_t KShaderResourceVK::Release()
{
    int nRef = --m_nRef;
    ASSERT(nRef >= 0);
    if (nRef == 0)
    {
        KShaderResourcePoolVK* pResourcePool = NSEngine::GetShaderResroucePoolVK();
        pResourcePool->RemoveShaderResource(this);
    }
    return nRef;
}

int32_t KShaderResourceVK::GetRef()
{
    return m_nRef;
}

BOOL KShaderResourceVK::LoadFromFileVSFS(
    const char*                     szShaderSource,
    const NSKBase::tagFileLocation& sIncludeShaderLoc,
    const char* szShaderDef, const char* szMacro,
    BOOL bByBuildToolCmd /* = false*/, int nPlatform /* = 0*/
)
{
    PROF_CPU();
    m_uStageCount                       = 0;
    static_const_param_name mtlUboName0 = GetParamNameByPool(PER_MTL_UBO_NAME_0);
    static_const_param_name mtlUboName1 = GetParamNameByPool(PER_MTL_UBO_NAME_1);
    BOOL                    bRet        = false;
    BOOL                    bRetCode    = false;

    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    // Clear();
    ASSERT(m_strShaderSource.empty());
    KGLOG_PROCESS_ERROR(m_strShaderSource.empty());

    m_strShaderSource    = szShaderSource;
    m_sIncludedShaderLoc = sIncludeShaderLoc;
    m_strShaderDef       = szShaderDef;
    if (szMacro)
    {
        m_strMacro = szMacro;
    }

    // bRetCode = pGraphicDevice->LoadShader(&m_pShaderStage[0], m_szShaderSource.c_str(), m_szIncludedShaderSource.c_str(), m_szShaderDef.c_str(), szMacro, gfx::SHADER_STAGE_VERTEX_BIT);
    // KGLOG_PROCESS_ERROR(bRetCode);
    // bRetCode = pGraphicDevice->LoadShader(&m_pShaderStage[1], m_szShaderSource.c_str(), m_szIncludedShaderSource.c_str(), m_szShaderDef.c_str(), szMacro, gfx::SHADER_STAGE_FRAGMENT_BIT);
    // KGLOG_PROCESS_ERROR(bRetCode);
    // KGLogPrintf(KGLOG_ERR, "begin load vs fs.....");

    if (strstr(szShaderSource, ".jsontech") || strstr(m_sIncludedShaderLoc.GetFilePath(), ".fx"))
    {
        SetIsHLSL(true);
    }
    else
    {
        SetIsHLSL(false);
    }

    bRetCode = pGraphicDevice->LoadShaderVSAndFS(m_pShaderStage, m_strShaderSource.c_str(), m_sIncludedShaderLoc, m_strShaderDef.c_str(), szMacro, this, false);

    // if(bRetCode && !m_bLoadCachedReflect && !m_buildedReflect0 && !m_buildedReflect1)
    //{
    //	int x = 0;
    // }
    ASSERT(m_bLoadCachedReflect || (m_buildedReflect0 || m_buildedReflect1));
    KGLOG_PROCESS_ERROR(bRetCode);


    for (uint32_t i = 0; i < MAX_SHADER_STAGE_COUNT; ++i)
    {
        if (m_pShaderStage[i])
        {
            m_uStageCount++;
        }
    }

    m_bLoaded = TRUE;
    bRet      = true;
Exit0:
    for (auto it : m_vecUniformBlock)
    {
        if (it->m_szName == mtlUboName0 || it->m_szName == mtlUboName1)
        {
            m_bHasPerMtlUBO = true;
            break;
        }
    }
    return bRet;
}

BOOL KShaderResourceVK::HasPerMtlUBO()
{
    return m_bHasPerMtlUBO;
}

BOOL KShaderResourceVK::IsActiveBlock(const_pool_str pcszName)
{
    auto it = m_setActiveBlock.find(pcszName);
    if (it != m_setActiveBlock.end())
    {
        return true;
    }
    else
    {
        return false;
    }
}

void KShaderResourceVK::AddSamplerState(const_pool_str pName, gfx::KSamplerState& samplerState)
{
    m_mapSamplerState[pName] = samplerState;
}

BOOL KShaderResourceVK::LoadFromFileCS(
    const char*                     szShaderSource,
    const NSKBase::tagFileLocation& sIncludeShaderLoc,
    const char*                     szShaderDef,
    const char*                     szMacro,
    BOOL bByBuildToolCmd /* = false*/, int nPlatform /* = 0*/
)
{
    PROF_CPU();
    static_const_param_name mtlUboName0 = GetParamNameByPool(PER_MTL_UBO_NAME_0);
    static_const_param_name mtlUboName1 = GetParamNameByPool(PER_MTL_UBO_NAME_1);
    m_uStageCount                       = 0;
    BOOL bResult                        = FALSE;
    BOOL bRetCode                       = FALSE;

    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();

    KGLOG_ASSERT_EXIT(pGraphicDevice);
    KGLOG_ASSERT_EXIT(szShaderSource && szShaderSource[0]);
    KGLOG_ASSERT_EXIT(szShaderDef && szShaderDef[0]);

    m_strShaderSource = szShaderSource;
    m_strShaderDef    = szShaderDef;
    if (szMacro)
    {
        m_strMacro = szMacro;
    }

    if (strstr(szShaderSource, ".jsontech") || strstr(m_sIncludedShaderLoc.GetFilePath(), ".fx"))
    {
        SetIsHLSL(true);
    }
    else
    {
        SetIsHLSL(false);
    }

    bRetCode = pGraphicDevice->LoadShaderCS(m_pShaderStage, m_strShaderSource.c_str(), sIncludeShaderLoc, m_strShaderDef.c_str(), m_strMacro.c_str(), this, bByBuildToolCmd, nPlatform);
    KGLOG_PROCESS_ERROR(bRetCode);


    for (auto it : m_vecUniformBlock)
    {
        if (it->m_szName == mtlUboName0 || it->m_szName == mtlUboName1)
        {
            m_bHasPerMtlUBO = true;
            break;
        }
    }

    m_bOrphan     = TRUE;
    m_bLoaded     = TRUE;
    bResult       = TRUE;
    m_uStageCount = 1;
Exit0:
    return bResult;
}

BOOL KShaderResourceVK::IsLoaded()
{
    return m_bLoaded;
}

BOOL KShaderResourceVK::IsCreated()
{
    return m_bCreated;
}

BOOL KShaderResourceVK::IsOrphan()
{
    return m_bOrphan;
}

uint64_t KShaderResourceVK::GetHashCode()
{
    return m_uHashCode;
}

void KShaderResourceVK::SetHashCode(uint64_t uHash)
{
    m_uHashCode = uHash;
}

BOOL KShaderResourceVK::IsGpuInstance()
{
    return m_bGpuInstance;
}

gfx::KVulkanLayout* KShaderResourceVK::GetLayout()
{
    return m_pLayout;
}

BOOL KShaderResourceVK::ApplyPushConstDataDirectly(gfx::IKGFX_RenderContext* pRenderCtx, gfx::ShaderStageType shaderType, uint32_t uSize, void* pData)
{
    BOOL bResult = false;

    KGLOG_ASSERT_EXIT(pRenderCtx && uSize > 0 && pData);

    if (DrvOption::GetRenderApi() == GFX_API::GFX_VULKAN_API)
    {
        KVulkanRenderContext* pRenderCtxVK = dynamic_cast<KVulkanRenderContext*>(pRenderCtx);
        KGLOG_ASSERT_EXIT(pRenderCtxVK);

        KGLOG_ASSERT_EXIT(m_pLayout);

        if (m_pPushConstantBlock)
        {
            for (auto& it : m_pPushConstantBlock->m_vecPushConstantsRangeMap)
            {
                const gfx::KPushContantsRangeMap& rangeMap = it;
                if (rangeMap.shadertype == shaderType || shaderType == gfx::ShaderStageType::AllGraphics)
                {
                    if (uSize <= rangeMap.toRange - rangeMap.offset)
                    {
                        pRenderCtxVK->CmdPushConstants(m_pLayout, rangeMap.shadertype, rangeMap.offset, uSize, pData);
                        bResult = true;
                    }
                    else
                    {
                        ASSERT(0);
                    }
                    break;
                }
            }
        }
        else
        {
            // 反射优化掉了，也算成功
            bResult = TRUE;
        }
    }
Exit0:
    return bResult;
}


uint32_t KShaderResourceVK::GetUpdateCheckCode()
{
    return m_uUpdateCheckCode;
}


BOOL KShaderResourceVK::Create()
{
    BOOL                 bRet           = false;
    BOOL                 bRetCode       = false;
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    KG_PROCESS_SUCCESS(m_bCreated);

    // bRetCode = pGraphicDevice->CreateShader(m_pShaderStage[0], this);
    // KGLOG_PROCESS_ERROR(bRetCode);
    // bRetCode = pGraphicDevice->CreateShader(m_pShaderStage[1], this);
    // KGLOG_PROCESS_ERROR(bRetCode);


    bRetCode = SetupLayout();
    KGLOG_PROCESS_ERROR(bRetCode);

    //{
    // //test
    //  gfx::KRenderState renderState;
    //  renderState.ResetDefaultValue();
    //  gfx::KPipeline *pPipeline = nullptr;
    //  CreatePipeline(renderState, gfx::RENDER_PASS_SCREEN_OFFSET, &pPipeline);
    //}

Exit1:
    m_bCreated = true;
    bRet       = true;
Exit0:

    return bRet;
}

BOOL KShaderResourceVK::UpdateShader(const char* szShaderSource, const NSKBase::tagFileLocation& sIncludedShaderLoc, const char* szShaderDef, const char* szMacro)
{
    BOOL                 bRet           = false;
    BOOL                 bRetCode       = false;
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    m_strShaderSource                   = szShaderSource;
    m_sIncludedShaderLoc                = sIncludedShaderLoc;
    m_strShaderDef                      = szShaderDef;
    m_strMacro                          = szMacro;

    Clear();

    // bRetCode = pGraphicDevice->LoadShader(&m_pShaderStage[0], m_szShaderSource.c_str(), m_szIncludedShaderSource.c_str(), m_szShaderDef.c_str(), m_szMacro.c_str(), gfx::SHADER_STAGE_VERTEX_BIT);
    // KGLOG_PROCESS_ERROR(bRetCode);

    // bRetCode = pGraphicDevice->LoadShader(&m_pShaderStage[1], m_szShaderSource.c_str(), m_szIncludedShaderSource.c_str(), m_szShaderDef.c_str(), m_szMacro.c_str(), gfx::SHADER_STAGE_FRAGMENT_BIT);
    // KGLOG_PROCESS_ERROR(bRetCode);

    // bRetCode = pGraphicDevice->ReCreateShader(m_pShaderStage[0], this);
    // KGLOG_PROCESS_ERROR(bRetCode);

    // bRetCode = pGraphicDevice->ReCreateShader(m_pShaderStage[1], this);
    // KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = pGraphicDevice->LoadShaderVSAndFS(m_pShaderStage, m_strShaderSource.c_str(), sIncludedShaderLoc, m_strShaderDef.c_str(), m_strMacro.c_str(), this, true);
    KGLOG_PROCESS_ERROR(bRetCode);

    SetupLayout();

    m_uUpdateCheckCode++;
    bRet = true;
Exit0:
    return bRet;
}

void KShaderResourceVK::Clear()
{
    ASSERT(IsMainThread());
    PROF_CPU();
    // std::lock_guard<std::mutex> lock(m_lock);
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();

    m_DescriptorPoolContainer.Clear();

    for (auto it : m_vecVertDescriptorCache)
    {
        VertDescriptor* p = it;
        SAFE_DELETE(p);
    }
    m_vecVertDescriptorCache.clear();

    if (m_pLayout)
    {
        pGraphicDevice->DestroyLayout(m_pLayout);
    }

    for (auto i = 0; i < MAX_SHADER_STAGE_COUNT; ++i)
    {
        if (m_pShaderStage[i])
        {
            pGraphicDevice->UnloadShader(m_pShaderStage[i]);
        }
    }

    for (auto it : m_vecPipelineCache)
    {
        PipeLine* p = it;
        SAFE_DELETE(p);
    }
    m_vecPipelineCache.clear();

    for (auto it : m_vecPipelineCache_ExternalPass)
    {
        PipeLine* p = it;
        SAFE_DELETE(p);
    }
    m_vecPipelineCache_ExternalPass.clear();

    SAFE_DELETE(m_pPushConstantBlock);

    for (auto it : m_vecAttribute)
    {
        SAFE_DELETE(it);
    }
    m_vecAttribute.clear();

    for (auto it : m_vecUniformBlock)
    {
        SAFE_DELETE(it);
    }
    m_vecUniformBlock.clear();

    for (auto it : m_vecUniformTexture)
    {
        SAFE_DELETE(it);
    }
    m_vecUniformTexture.clear();

    for (auto it : m_vecUniformSampler)
    {
        SAFE_DELETE(it);
    }
    m_vecUniformSampler.clear();

    ClearVsTmpData();
    m_bParamInited = false;
    m_vecSpecializationConstDefine.clear();
    SAFE_DELETE(m_pCombinedShaderResultVK_HLSL);
}

bool compareAttribute(gfx::KProgramAttribute* p0, gfx::KProgramAttribute* p1)
{
    if (p0->nLocation < p1->nLocation)
        return true;
    else
        return false;
}

BOOL KShaderResourceVK::IsReflected(gfx::ShaderStageType shaderType)
{
    uint32_t shaderid = GetGraphicAndComputeShaderId(shaderType);
    return m_bRefelcted[shaderid];
}


////过时了，要干掉的
// BOOL KShaderResourceVK::BuildReflection(void* pProg, gfx::ShaderStageType shaderType)
//{
//	PROF_CPU();
//	// KGLogPrintf(KGLOG_ERR, "begin reflect.....");
//	BOOL bResult  = FALSE;
//	BOOL bRetCode = FALSE;
//
//	if (shaderType == gfx::ShaderStageType::Vertex)
//	{
//		m_bGpuInstance = false;
//	}
//
//	glslang::TProgram* pProgram = (glslang::TProgram*)pProg;
//	// pProgram->buildReflection();
//
//	gfx::KGraphicDevice*    pGfxDevice    = nullptr;
//	gfx::KShaderStage*      pShaderStage  = nullptr;
//	gfx::KSamplerState*     pSamplerState = nullptr;
//	gfx::IKStageSamplerDef* pSamplerDef   = nullptr;
//     uint32_t shaderid;
//
//	int nUniformBlockCount = 0;
//	int nUniformCount      = 0;
//	int nAttributeCount    = 0;
//
//	pGfxDevice = gfx::GetGraphicDevice();
//	KGLOG_ASSERT_EXIT(pGfxDevice);
//
//	//if (shaderType == gfx::SHADER_STAGE_VERTEX_BIT || shaderType == gfx::SHADER_STAGE_COMPUTE_BIT)
//	//{
//	//	pShaderStage = m_pShaderStage[0];
//	//}
//	//else if (shaderType == gfx::SHADER_STAGE_FRAGMENT_BIT)
//	//{
//	//	pShaderStage = m_pShaderStage[1];
//	//}
//
//     for(uint32_t i = 0; i < MAX_SHADER_STAGE_COUNT; ++i)
//     {
//         if(m_pShaderStage[i] && m_pShaderStage[i]->GetShaderInfo()->eShaderStage == shaderType)
//         {
//             pShaderStage = m_pShaderStage[i];
//             break;
//         }
//     }
//
//	KGLOG_ASSERT_EXIT(pShaderStage);
//
//     shaderid = GetGraphicAndComputeShaderId(shaderType);
//	//const auto& symbols = intermediate->getSymbolTable()->getAllSymbols();
//	//for (const auto& symbol : symbols) {
//	//}
//
//	nAttributeCount = KSPV_GetNumLiveAttributes(pProgram);
//	for (int i = 0; i < nAttributeCount; ++i)
//	{
//		gfx::KProgramAttribute*         pAttribue = new gfx::KProgramAttribute;
//		const glslang::TType*      type      = KSPV_GetAttributeTType(pProgram, i);
//		const glslang::TQualifier& q         = KSPV_GetQualifier(type);
//		const_pool_str             pName     = GetParamNameByPool(KSPV_GetAttributeName(pProgram, i));
//		pAttribue->szName                    = pName;
//		pAttribue->nLocation                 = KSPV_layoutLocation(q);
//
//		if (pAttribue->nLocation > gfx::KAttribUsage::COUNT)
//		{
//			// 比如 gl_InstanceIndex这个就比较特殊，nLocation是4095，忽略掉好了
//			SAFE_DELETE(pAttribue);
//			continue;
//		}
//
//		pAttribue->type          = KSPV_GetProgramDataType(type);
//		pAttribue->bInstanceData = false;
//		pAttribue->uSize         = GetProgramDataTypeSize(pAttribue->type, 1);
//		pAttribue->fmt           = GetVertFormat(pAttribue->type, (gfx::KAttribUsage::Enum)pAttribue->nLocation);
//		if (pAttribue->nLocation >= gfx::KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE && pAttribue->nLocation <= gfx::KAttribUsage::VERT_POINT_LIGHT_INDX_INSTANCE)
//		{
//			pAttribue->bInstanceData = true;
//			m_bGpuInstance           = true;
//		}
//		m_vecAttribute.push_back(pAttribue);
//	}
//
//	std::sort(m_vecAttribute.begin(), m_vecAttribute.end(), compareAttribute);
//
//	// 创建所有的uniformblocks
//	nUniformBlockCount = KSPV_GetNumLiveUniformBlocks(pProgram);
//	for (int i = 0; i < nUniformBlockCount; ++i)
//	{
//		const_pool_str pName = GetParamNameByPool(KSPV_GetUniformBlockName(pProgram, i));
//
//		const glslang::TType*      type = KSPV_GetUniformBlockTType(pProgram, i);
//		const glslang::TQualifier& q    = KSPV_GetQualifier(type);
//
//		gfx::KProgramUniformBlock* pBlock    = nullptr;
//		BOOL                  bNewBLock = false;
//		if (KSPV_layoutPushConstant(q) || strcmp(pName, "PushConsts") == 0)
//		{
//			if (!m_pPushConstantBlock)
//			{
//				m_pPushConstantBlock                = new gfx::KProgramUniformBlock;
//				m_pPushConstantBlock->m_szName      = GetParamNameByPool("PushConstant");
//				m_pPushConstantBlock->m_UniformType = gfx::PUSH_CONSTANT_UNIFORM;
//			}
//			pBlock                                  = m_pPushConstantBlock;
//			pBlock->m_block16bytesAlignMemoryForGpu = KSPV_GetUniformBlockSize(pProgram, i);
//
//
//			uint32_t previouseRange = 0;
//			size_t   sz             = pBlock->m_vecPushConstantsRangeMap.size();
//			if (sz)
//			{
//				previouseRange = pBlock->m_vecPushConstantsRangeMap[sz - 1].toRange;
//			}
//
//			gfx::KPushContantsRangeMap rangemap;
//			rangemap.shadertype = shaderType;
//			rangemap.offset     = previouseRange;
//			// rangemap.offset = 0; //重构改成累加push_consts结构，FixParamTerms以后 vs 的字段定义前面修正自动添加到fs里面，就不再需要offset了，都从0开始
//			rangemap.toRange    = KSPV_GetUniformBlockSize(pProgram, i);
//			pBlock->m_vecPushConstantsRangeMap.push_back(rangemap);
//		}
//		else
//		{
//			for (auto it : m_vecUniformBlock)
//			{
//				gfx::KProgramUniformBlock* p = it;
//				if (p->m_szName == pName)
//				{
//					pBlock = p;
//					break;
//				}
//			}
//
//			if (!pBlock)
//			{
//				pBlock = new gfx::KProgramUniformBlock;
//
//				if (KSPV_ISEvqUniform(q))
//				{
//					pBlock->m_UniformType = gfx::UBO_UNIFORM;
//				}
//				else if (KSPV_ISEvqBuffer(q))
//				{
//					pBlock->m_UniformType = gfx::SSBO_UNIFORM;
//				}
//
//				bNewBLock                               = true;
//				pBlock->m_szName                        = pName;
//				pBlock->m_block16bytesAlignMemoryForGpu = KSPV_GetUniformBlockSize(pProgram, i);
//			}
//			else
//			{
//				uint32_t u                              = KSPV_GetUniformBlockSize(pProgram, i);
//				// 更新长度，可能是vs,fs共用的结构，那么以后面累加的长度为准
//				pBlock->m_block16bytesAlignMemoryForGpu = u;
//				// ASSERT(pBlock->m_block16bytesAlignMemoryForGpu == u);
//			}
//		}
//
//		int layoutBinding = KSPV_layoutBinding(q);
//		if (shaderType == gfx::ShaderStageType::Vertex)
//		{
//			pBlock->m_nLayoutBindingVs = layoutBinding;
//		}
//		else if (shaderType == gfx::ShaderStageType::Fragment)
//		{
//			pBlock->m_nLayoutBindingFs = layoutBinding;
//		}
//		else if (shaderType == gfx::ShaderStageType::Compute)
//		{
//			pBlock->m_nLayoutBindingCs = layoutBinding;
//		}
//
//		if (layoutBinding != 0xffff && (int)layoutBinding > m_nMaxBinding)
//		{
//			m_nMaxBinding = layoutBinding;
//		}
//
//		pBlock->m_UniformScopeType = gfx::KGlobalUBO::GetUniformScope(pName);
//
//		if (bNewBLock && (pBlock->m_UniformType == gfx::UBO_UNIFORM || pBlock->m_UniformType == gfx::SSBO_UNIFORM))
//		{
//			m_vecUniformBlock.push_back(pBlock);
//			m_setActiveBlock.insert(pBlock->m_szName);
//		}
//	}
//
//	nUniformCount = KSPV_GetNumLiveUniformVariables(pProgram);
//	for (int i = 0; i < nUniformCount; ++i)
//	{
//		const char *pMemberName = KSPV_GetUniformName(pProgram, i);
//		//const char *p= strrchr(pMemberName, '.');
//		//if(p)
//		//{
//		//	pMemberName = p;
//		//}
//		const_pool_str        pcszUniformName = GetParamNameByPool(pMemberName);
//		const glslang::TType* type            = KSPV_GetUniformTType(pProgram, i);
//
//		{
//			const glslang::TQualifier& q             = KSPV_GetQualifier(type);
//
//			int                        layoutBinding = KSPV_layoutBinding(q);
//			if (layoutBinding != 0xffff && (int)layoutBinding > m_nMaxBinding)
//			{
//				m_nMaxBinding = layoutBinding;
//			}
//		}
//
//		// 数据基础类型
//		int basicType = KSPV_GetTBaseType(type);
//		if (KSPV_ISEbtString(basicType) || KSPV_ISEbtSampler(basicType))
//		{
//			// 如果宏AMD_EXTENSIONS使用不一致，这里会指向glslang::EbtString，本应该是EbtSampler的,这里加了容错处理
//			gfx::KProgramUniformTexture*    pTextureUniform = nullptr;
//			gfx::KProgramUniformSampler*    pSamplerUniform = nullptr;
//			const glslang::TQualifier& q               = KSPV_GetQualifier(type);
//			const glslang::TSampler&   sSampler        = type->getSampler();
//
//			int layoutBinding = KSPV_layoutBinding(q);
//
//			if (sSampler.dim != glslang::TSamplerDim::EsdNone) // textureXXX
//			{
//				for (const auto& it : m_vecUniformTexture)
//				{
//					if (it->m_szName == pcszUniformName)
//					{
//						pTextureUniform = it;
//						break;
//					}
//				}
//
//				if (!pTextureUniform)
//				{
//					pTextureUniform                = new gfx::KProgramUniformTexture;
//					pTextureUniform->m_szName      = pcszUniformName;
//					pTextureUniform->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pcszUniformName);
//					pTextureUniform->m_UniformType = gfx::TEXTURE_UNIFORM;
//					pTextureUniform->m_uArrayCount = KSPV_GetUniformArraySize(pProgram, i);
//					ASSERT(pTextureUniform->m_uArrayCount > 0);
//
//
//
//					if (strstr(pcszUniformName, "tSceneColor"))
//					{
//						int x = 0;
//					}
//
//					switch (sSampler.dim)
//					{
//					case glslang::Esd1D:
//						pTextureUniform->m_eTextureType = sSampler.isArrayed() ? TextureType::Texture1DArray : TextureType::Texture1D;
//						break;
//					case glslang::Esd2D:
//						if (sSampler.isImage())
//						{
//							ASSERT(!sSampler.isArrayed());
//							pTextureUniform->m_eTextureType = TextureType::TextureImage2D;
//						}
//						else
//						{
//							pTextureUniform->m_eTextureType = sSampler.isArrayed() ? TextureType::Texture2DArray : TextureType::Texture2D;
//						}
//						break;
//					case glslang::Esd3D:
//						if (sSampler.isImage())
//						{
//							ASSERT(!sSampler.isArrayed());
//							pTextureUniform->m_eTextureType = TextureType::TextureImage3D;
//						}
//						else
//						{
//							pTextureUniform->m_eTextureType = TextureType::Texture3D;
//						}
//						break;
//					case glslang::EsdCube:
//						pTextureUniform->m_eTextureType = TextureType::Cubemap;
//						break;
//					case glslang::EsdBuffer:
//						if (sSampler.isImage())
//						{
//							pTextureUniform->m_eTextureType = TextureType::RWBuffer;
//						}
//						else if (sSampler.isCombined())
//						{
//							pTextureUniform->m_eTextureType = TextureType::CombinedSamplerBuffer;
//						}
//					default:
//						break;
//					}
//
//					m_vecUniformTexture.push_back(pTextureUniform);
//				}
//
//				if (shaderType == gfx::ShaderStageType::Vertex)
//				{
//					pTextureUniform->m_nLayoutBindingVs = layoutBinding;
//				}
//				else if (shaderType == gfx::ShaderStageType::Fragment)
//				{
//					pTextureUniform->m_nLayoutBindingFs = layoutBinding;
//				}
//				else if (shaderType == gfx::ShaderStageType::Compute)
//				{
//					pTextureUniform->m_nLayoutBindingCs = layoutBinding;
//				}
//			}
//			else // sampler/samplerShadow
//			{
//				for (const auto& it : m_vecUniformSampler)
//				{
//					if (it->m_szName == pcszUniformName)
//					{
//						pSamplerUniform = it;
//						break;
//					}
//				}
//
//				if (!pSamplerUniform)
//				{
//					pSamplerUniform                = new gfx::KProgramUniformSampler;
//					pSamplerUniform->m_szName      = pcszUniformName;
//					pSamplerUniform->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pcszUniformName);
//					pSamplerUniform->m_UniformType = gfx::SAMPLER_UNIFORM;
//
//					pSamplerDef = pShaderStage->GetSamplerDef(pcszUniformName);
//					if (pSamplerDef)
//					{
//						pSamplerState = pSamplerDef->GetSamplerState();
//					}
//					else
//					{
//						if (!bByBuildTool && bForMaterialSystem)
//						{
//							KGLogPrintf(KGLOG_ERR, "[KShaderResourceVK::BuildReflection] failed to get sampler def, uniform name: %s, shader: %s", pcszUniformName, pShaderStage->GetShaderName());
//						}
//
//						static gfx::KSamplerState sDummySamplerState;
//						pSamplerState = &sDummySamplerState;
//					}
//					KGLOG_ASSERT_EXIT(pSamplerState);
//
//					pSamplerUniform->m_SamplerState = *pSamplerState;
//
//					if (!bByBuildTool/* && bForMaterialSystem*/)
//					{
//						pSamplerUniform->m_pSampler = pGfxDevice->GetSamplerByState(pSamplerState);
//						KGLOG_ASSERT_EXIT(pSamplerUniform->m_pSampler);
//					}
//
//					m_vecUniformSampler.push_back(pSamplerUniform);
//				}
//
//				if (shaderType == gfx::ShaderStageType::Vertex)
//				{
//					pSamplerUniform->m_nLayoutBindingVs = layoutBinding;
//				}
//				else if (shaderType == gfx::ShaderStageType::Fragment)
//				{
//					pSamplerUniform->m_nLayoutBindingFs = layoutBinding;
//				}
//				else if (shaderType == gfx::ShaderStageType::Compute)
//				{
//					pSamplerUniform->m_nLayoutBindingCs = layoutBinding;
//				}
//			}
//		}
//		else
//		{
//			gfx::KProgramUniform* pUniform = new gfx::KProgramUniform;
//
//			pUniform->m_szName          = pcszUniformName;
//			pUniform->m_uNameHash       = KSTR_HELPER::GetHashCodeForString32Bit(pcszUniformName);
//			pUniform->m_uVectorSize     = (uint8_t)KSPV_GetVectorSize(type);
//			pUniform->m_uMatcol         = (uint8_t)KSPV_GetMatrixCols(type);
//			pUniform->m_uMatrow         = (uint8_t)KSPV_GetMatrixRows(type);
//			int blockIndex              = (uint8_t)KSPV_GetUniformBlockIndex(pProgram, i);
//			pUniform->m_nOffset         = KSPV_GetUniformBufferOffset(pProgram, i);
//			pUniform->m_uArrayCount     = KSPV_GetUniformArraySize(pProgram, i);
//			// pUniform->m_ShaderType = shaderType;
//			const char* blockName       = GetParamNameByPool(KSPV_GetUniformBlockName(pProgram, blockIndex));
//			pUniform->m_UniformBaseType = KSPV_GetBaseType(basicType);
//			pUniform->m_szBlockName = blockName;
//			const glslang::TType*      blocktype          = KSPV_GetUniformBlockTType(pProgram, blockIndex);
//			gfx::KProgramUniformBlock*      pBlock             = nullptr;
//			const glslang::TQualifier& q                  = KSPV_GetQualifier(type);
//			bool                       layoutPushConstant = KSPV_layoutPushConstant(q);
//			BOOL                       bPushConstant      = (layoutPushConstant || strcmp(blockName, "PushConsts") == 0);
//
//			if (bPushConstant)
//			{
//				pBlock = m_pPushConstantBlock;
//			}
//			else
//			{
//				for (const auto& it : m_vecUniformBlock)
//				{
//					if (it->m_szName == blockName)
//					{
//						pBlock = it;
//						break;
//					}
//				}
//			}
//
//			if (pUniform->m_uMatcol)
//			{
//				pUniform->m_uByteSize = GetBaseTypeSize(pUniform->m_UniformBaseType) * pUniform->m_uMatcol * pUniform->m_uMatrow * pUniform->m_uArrayCount;
//			}
//			else
//			{
//				gfx::enumProgramDataType uniformVarType = KSPV_GetProgramDataType(type);
//
//				if (uniformVarType == gfx::FLOAT1_ARRAY_TYPE ||
//					uniformVarType == gfx::FLOAT2_ARRAY_TYPE ||
//					uniformVarType == gfx::FLOAT3_ARRAY_TYPE ||
//					uniformVarType == gfx::FLOAT4_ARRAY_TYPE)
//				{
//
//					if (q.layoutPacking == glslang::ElpStd140)
//					{
//						//数组元素按vec4对齐
//						pUniform->m_uByteSize = GetBaseTypeSize(pUniform->m_UniformBaseType) * 4 * pUniform->m_uArrayCount;
//					}
//					else
//					{
//						ASSERT(FALSE);
//						KGLogPrintf(KGLOG_WARNING, "float array uniform varable is not std140 layout packing!");
//						pUniform->m_uByteSize = GetBaseTypeSize(pUniform->m_UniformBaseType) * pUniform->m_uVectorSize * pUniform->m_uArrayCount;
//					}
//
//				}
//				else
//				{
//					pUniform->m_uByteSize = GetBaseTypeSize(pUniform->m_UniformBaseType) * pUniform->m_uVectorSize * pUniform->m_uArrayCount;
//				}
//			}
//
//			// 所属block的类别是否为pushcontants
//			if (bPushConstant)
//			{
//				pUniform->m_UniformType = gfx::PUSH_CONSTANT_UNIFORM;
//			}
//			else if (KSPV_ISEvqUniform(q))
//			{
//				pUniform->m_UniformType = gfx::UBO_UNIFORM;
//			}
//			else if (KSPV_ISEvqBuffer(q))
//			{
//				pUniform->m_UniformType = gfx::SSBO_UNIFORM;
//			}
//
//			if (pBlock)
//			{
//				static_const_param_name mtlUboName0 = GetParamNameByPool(PER_MTL_UBO_NAME_0);
//				static_const_param_name mtlUboName1 = GetParamNameByPool(PER_MTL_UBO_NAME_1);
//
//				auto it = pBlock->m_Uniforms.find(pUniform);
//				if (it == pBlock->m_Uniforms.end())
//				{
//					pBlock->m_Uniforms.insert(pUniform);
//					if(pBlock->m_szName == mtlUboName0 || pBlock->m_szName == mtlUboName1)
//					{
//						m_mapMtlParamItem[pUniform->m_szName] = pUniform;
//					}
//				}
//				else
//				{
//					// ps 和 vs 共有的字段，排序是按offset来的，相同说明已经添加过了，直接删除掉，不然就泄露了
//					delete (pUniform);
//				}
//			}
//			else
//			{
//				KGLogPrintf(KGLOG_ERR, "no uniform block found ");
//				ASSERT(0 && "no uniform block found ");
//				delete (pUniform);
//			}
//		}
//	}
//	if (shaderType == gfx::ShaderStageType::Vertex)
//	{
//		m_vsPushConstantSize = GetPushContentAlign16BytesBlockSize();
//	}
//	bResult           = TRUE;
//	m_buildedReflect0 = true;
// Exit0:
//	m_buildedReflect1        = true;
//	m_bRefelcted[shaderid] = true;
//	return bResult;
// }


void KShaderResourceVK::DebugPrintSaveReflectorBegin(const char* pFileName)
{
    if (DrvOption::bDebugShaderReflector)
    {
        // if (strstr(pFileName, "shaderbin_windows_d/sspr") &&
        //	strstr(pFileName, "@main.comp"))
        //{
        //	int x = 0;
        // }

        printf("===========================================================================\r\n");
        printf("save shaderbin reflector info %s: \r\n", pFileName);
        printf("===========================================================================\r\n");
    }
}

void KShaderResourceVK::DebugPrintSaveReflectorEnd(const char* pFileName)
{
    if (DrvOption::bDebugShaderReflector)
    {
        // if (strstr(pFileName, "shaderbin_windows_d/sspr") &&
        //	strstr(pFileName, "@main.comp"))
        //{
        //	int x = 0;
        // }
    }
}

BOOL KShaderResourceVK::IsEnableSpirvCrossReflector()
{
    return DrvOption::bEnableSpirvCrossReflector;
    // return true;
    // return true;
}

const char* _DotLeftSubstr(const char* pStr)
{
    const char* pRet = pStr;
    const char* p    = strrchr(pStr, '.');
    if (p)
    {
        pRet = p + 1;
    }
    return pRet;
}

BOOL KShaderResourceVK::BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType)
{
    PROF_CPU();
    static_const_param_name mtlUboName0      = GetParamNameByPool(PER_MTL_UBO_NAME_0);
    static_const_param_name mtlUboName1      = GetParamNameByPool(PER_MTL_UBO_NAME_1);
    static_const_param_name VERTEX_POS_INDEX = GetParamNameByPool("VERTEX_POS_INDX");

    uint32_t                   shaderid      = GetGraphicAndComputeShaderId(shaderType);
    // KGLogPrintf(KGLOG_ERR, "begin reflect.....");
    BOOL                       bResult       = FALSE;
    BOOL                       bRetCode      = FALSE;
    IKSpirvCrossCompilerHandle pCompiler     = KSPV_QueryCrossCompilerShaderSource(pProgramCross);
    gfx::KShaderStage*         pShaderStage  = nullptr;
    gfx::IKStageSamplerDef*    pSamplerDef   = nullptr;
    gfx::KSamplerState*        pSamplerState = nullptr;
    gfx::KGraphicDevice*       pGfxDevice    = gfx::GetGraphicDevice();
    KGLOG_ASSERT_EXIT(pGfxDevice);

    {
        // if (shaderType == gfx::SHADER_STAGE_VERTEX_BIT || shaderType == gfx::SHADER_STAGE_COMPUTE_BIT)
        //{
        //	pShaderStage = m_pShaderStage[0];
        // }
        // else if (shaderType == gfx::SHADER_STAGE_FRAGMENT_BIT)
        //{
        //	pShaderStage = m_pShaderStage[1];
        // }

        for (uint32_t i = 0; i < MAX_SHADER_STAGE_COUNT; ++i)
        {
            if (m_pShaderStage[i] && m_pShaderStage[i]->GetShaderInfo()->eShaderStage == shaderType)
            {
                pShaderStage = m_pShaderStage[i];
                break;
            }
        }

        // vertex
        if (shaderType == gfx::ShaderStageType::Vertex)
        {
            uint32_t uInputAttributeItemCount = KSPV_GetInputResourceSize(pCompiler);
            for (uint32_t i = 0; i < uInputAttributeItemCount; ++i)
            {
                const SPIRV_CROSS_NAMESPACE::Resource* pInputResource = KSPV_GetInputResouce(pCompiler, i);
                const char*                            szAttName      = _DotLeftSubstr(KSPV_GetName(pCompiler, pInputResource));
                int32_t                                location       = KSPV_GetLocation(pCompiler, pInputResource);
                int32_t                                binding        = KSPV_GetBinding(pCompiler, pInputResource);
                const spirv_cross::SPIRType*           type           = KSPV_GetType(pCompiler, pInputResource);
                gfx::enumProgramDataType               eType          = KSPV_GetProgramDataType(type);

                KProgramAttribute* pAttribue = new KProgramAttribute;
                pAttribue->type              = eType;
                const_pool_str pName         = GetParamNameByPool(szAttName);
                pAttribue->szName            = pName;
                pAttribue->uSize             = GetProgramDataTypeSize(pAttribue->type, 1);
                if (VERTEX_POS_INDEX == pName)
                {
                    // ASSERT(pAttribue->uSize != 16); //一般position最后一个w分量不传的，省点显存带宽
                }
                pAttribue->nLocation     = location;
                pAttribue->bInstanceData = false;
                pAttribue->fmt           = GetVertFormat(pAttribue->type, (gfx::KAttribUsage::Enum)pAttribue->nLocation);
                if (pAttribue->nLocation >= gfx::KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE && pAttribue->nLocation <= gfx::KAttribUsage::VERT_POINT_LIGHT_INDX_INSTANCE)
                {
                    pAttribue->bInstanceData = true;
                    m_bGpuInstance           = true;
                }
                m_vecAttribute.push_back(pAttribue);
            }
            std::sort(m_vecAttribute.begin(), m_vecAttribute.end(), compareAttribute);
        }

        // ubo
        uint32_t uUnifromBlockCount = KSPV_GetNumLiveUniformBlocks(pCompiler);
        for (uint32_t i = 0; i < uUnifromBlockCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetUniformResource(pCompiler, i);
            int32_t                                location  = KSPV_GetLocation(pCompiler, pResource);
            int32_t                                binding   = KSPV_GetBinding(pCompiler, pResource);


            const_pool_str pName         = nullptr;//GetParamNameByPool(_DotLeftSubstr(KSPV_GetBlockName(pCompiler, pResource)));
            const_pool_str pinstanceName = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));            
            pName = pinstanceName;

            uint32_t                     uSize     = KSPV_GetStruceSize(pCompiler, pResource);
            const spirv_cross::SPIRType* pBaseType = KSPV_GetBaseType(pCompiler, pResource);

            KProgramUniformBlock* pBlock = nullptr;
            for (auto it : m_vecUniformBlock)
            {
                if (pName == it->m_szName)
                {
                    pBlock = it;
                    break;
                }
            }

            if (!pBlock)
            {
                pBlock                     = new KProgramUniformBlock;
                pBlock->m_szName           = pName;
                pBlock->m_UniformScopeType = 0;
                pBlock->m_UniformType      = gfx::UBO_UNIFORM;
                m_vecUniformBlock.push_back(pBlock);
                m_setActiveBlock.insert(pBlock->m_szName);
            }

            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pBlock->m_nLayoutBindingVs = binding;
                pBlock->m_nSpaceVS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pBlock->m_nLayoutBindingFs = binding;
                pBlock->m_nSpaceFS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pBlock->m_nLayoutBindingCs = binding;
                pBlock->m_nSpaceCS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pBlock->m_nLayoutBindingTc = binding;
                pBlock->m_nSpaceTC         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pBlock->m_nLayoutBindingTe = binding;
                pBlock->m_nSpaceTE         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pBlock->m_nLayoutBindingGs = binding;
                pBlock->m_nSpaceGS         = KSPV_GetSet(pCompiler, pResource);
            }
            else
            {
                ASSERT(0);
            }

            pBlock->m_block16bytesAlignMemoryForGpu = uSize;
            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }


            // m_vecActiveUBORangeId.clear();
            uint32_t uNumbers = KSPV_GetNumMemberType(pBaseType);
            // m_vecActiveUBORangeId.resize(uNumbers);
            // uint32_t uActiveCount = 0;
            // KSPV_GetActiveMemberRange(pCompiler, pResource, m_vecActiveUBORangeId.data(), uActiveCount);
            // int aaa = KSPV_GetActiveNumMember(pCompiler, pResource);
            // auto itt = KSPV_GetActiveMemberRange(pCompiler, pResource);
            // uint32_t rangeSize = KSPV_GetActiveMemberRangeSize(itt);
            for (uint32_t n = 0; n < uNumbers; ++n)
            {
                uint32_t    j     = n; // m_vecActiveUBORangeId[n];
                // uint32_t uNumbers = KSPV_GetNumMemberType(pCompiler, pBaseType);
                // uint32_t uNumbers = KSPV_GetActiveNumMember(pCompiler, pResource);
                // for (uint32_t j = 0; j < uNumbers; ++j)
                //{
                const char* pName = GetParamNameByPool(KSPV_GetMemberName(pCompiler, pBaseType, j));

                BOOL bExisted = false;
                for (auto itt : pBlock->m_Uniforms)
                {
                    if (itt->m_szName == pName)
                    {
                        bExisted = true;
                        break;
                    }
                }

                if (!bExisted)
                {
                    uint32_t                     uOffset   = KSPV_GetMemberOffset(pCompiler, pBaseType, j);
                    uint32_t                     uByteSize = KSPV_GetMemberSize(pCompiler, pBaseType, j);
                    const spirv_cross::SPIRType* type      = KSPV_GetMemberType(pCompiler, pBaseType, j);
                    // gfx::enumProgramDataType eType = KSPV_GetProgramDataType(type);
                    gfx::enumUniformBaseType     eType     = KSPV_GetBaseType(type);
                    uint32_t                     vecsize = 0, cols = 0, arrays = 0;
                    KSPV_GetType_Rows_Cols_Arrays(type, vecsize, cols, arrays);

                    KProgramUniform* pUniform = new KProgramUniform;

                    pUniform->m_szBlockName = pBlock->m_szName;
                    pUniform->m_szName      = pName;
                    pUniform->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                    pUniform->m_uVectorSize = vecsize;
                    pUniform->m_uMatcol     = cols;
                    pUniform->m_uMatrow     = vecsize;

                    if (pUniform->m_uMatcol != 4 || pUniform->m_uMatrow != 4)
                    {
                        pUniform->m_uMatcol = 0;
                        pUniform->m_uMatrow = 0;
                    }

                    if (pUniform->m_uMatcol == 4 && pUniform->m_uMatrow == 4)
                    {
                        pUniform->m_uVectorSize = 0;
                    }

                    pUniform->m_nOffset         = uOffset;
                    pUniform->m_uArrayCount     = std::max<uint32_t>(arrays, 1);
                    pUniform->m_uByteSize       = uByteSize;
                    // pUniform->m_ShaderType = shaderType;
                    pUniform->m_UniformBaseType = eType;
                    pUniform->m_UniformType     = pBlock->m_UniformType;
                    pBlock->m_Uniforms.insert(pUniform);
                    //}
                }
            }
        }

        // storage buffer
        uint32_t uStorageBufferCount = KSPV_GetNumLiveStorageBufferBlocks(pCompiler);
        for (uint32_t i = 0; i < uStorageBufferCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetStorageBufferResource(pCompiler, i);
            const_pool_str                         pName     = GetParamNameByPool(_DotLeftSubstr(KSPV_GetStorageBlockName(pCompiler, pResource)));

            int32_t location = KSPV_GetLocation(pCompiler, pResource);
            int32_t binding  = KSPV_GetBinding(pCompiler, pResource);

            // uint32_t  uSize = KSPV_GetStruceSize(pCompiler, pResource);

            KProgramUniformBlock* pBlock = nullptr;
            for (auto it : m_vecUniformBlock)
            {
                if (pName == it->m_szName)
                {
                    pBlock = it;
                    break;
                }
            }

            if (!pBlock)
            {
                pBlock                     = new KProgramUniformBlock;
                pBlock->m_szName           = pName;
                pBlock->m_UniformScopeType = 0;
                pBlock->m_UniformType      = gfx::SSBO_UNIFORM;
                m_vecUniformBlock.push_back(pBlock);
                m_setActiveBlock.insert(pName);
            }

            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pBlock->m_nLayoutBindingVs = binding;
                pBlock->m_nSpaceVS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pBlock->m_nLayoutBindingFs = binding;
                pBlock->m_nSpaceFS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pBlock->m_nLayoutBindingCs = binding;
                pBlock->m_nSpaceCS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pBlock->m_nLayoutBindingTc = binding;
                pBlock->m_nSpaceTC         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pBlock->m_nLayoutBindingTe = binding;
                pBlock->m_nSpaceTE         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pBlock->m_nLayoutBindingGs = binding;
                pBlock->m_nSpaceGS         = KSPV_GetSet(pCompiler, pResource);
            }
            else
            {
                ASSERT(0);
            }
            // pBlock->m_block16bytesAlignMemoryForGpu = uSize;
            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }


            if (pBlock->m_Uniforms.empty())
            {
                const spirv_cross::SPIRType* pBaseType = KSPV_GetBaseType(pCompiler, pResource);
                uint32_t                     uNumbers  = KSPV_GetNumMemberType(pBaseType);
                for (uint32_t j = 0; j < uNumbers; ++j)
                {
                    const char*                  pName     = GetParamNameByPool(KSPV_GetMemberName(pCompiler, pBaseType, j));
                    uint32_t                     uOffset   = KSPV_GetMemberOffset(pCompiler, pBaseType, j);
                    uint32_t                     uByteSize = KSPV_GetMemberSize(pCompiler, pBaseType, j);
                    const spirv_cross::SPIRType* type      = KSPV_GetMemberType(pCompiler, pBaseType, j);
                    // gfx::enumProgramDataType eType = KSPV_GetProgramDataType(type);
                    gfx::enumUniformBaseType     eType     = KSPV_GetBaseType(type);
                    uint32_t                     vecsize = 0, cols = 0, arrays = 0;
                    KSPV_GetType_Rows_Cols_Arrays(type, vecsize, cols, arrays);
                    KProgramUniform* pUniform = new KProgramUniform;

                    pUniform->m_szBlockName     = pBlock->m_szName;
                    pUniform->m_szName          = pName;
                    pUniform->m_uNameHash       = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                    pUniform->m_uVectorSize     = vecsize;
                    pUniform->m_uMatcol         = cols;
                    pUniform->m_uMatrow         = vecsize;
                    pUniform->m_nOffset         = uOffset;
                    pUniform->m_uArrayCount     = std::max<uint32_t>(arrays, 1);
                    pUniform->m_uByteSize       = uByteSize;
                    // pUniform->m_ShaderType = shaderType;
                    pUniform->m_UniformBaseType = eType;
                    pUniform->m_UniformType     = pBlock->m_UniformType;
                    pBlock->m_Uniforms.insert(pUniform);
                }
            }
        }

        // acceleration structures
        uint32_t uASCount = KSPV_GetNumLiveAccelerationStructures(pCompiler);
        for (uint32_t i = 0; i < uASCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetAccelerationStructuresResource(pCompiler, i);
            const_pool_str                         pName = GetParamNameByPool(_DotLeftSubstr(KSPV_GetStorageBlockName(pCompiler, pResource)));

            int32_t location = KSPV_GetLocation(pCompiler, pResource);
            int32_t binding = KSPV_GetBinding(pCompiler, pResource);

            // uint32_t  uSize = KSPV_GetStruceSize(pCompiler, pResource);

            KProgramUniformBlock* pBlock = nullptr;
            for (auto it : m_vecUniformBlock)
            {
                if (pName == it->m_szName)
                {
                    pBlock = it;
                    break;
                }
            }

            if (!pBlock)
            {
                pBlock = new KProgramUniformBlock;
                pBlock->m_szName = pName;
                pBlock->m_UniformScopeType = 0;
                pBlock->m_UniformType = gfx::ACCELERATION_STRUCTURE_UNIFORM;
                m_vecUniformBlock.push_back(pBlock);
                m_setActiveBlock.insert(pName);
            }

            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pBlock->m_nLayoutBindingVs = binding;
                pBlock->m_nSpaceVS = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pBlock->m_nLayoutBindingFs = binding;
                pBlock->m_nSpaceFS = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pBlock->m_nLayoutBindingCs = binding;
                pBlock->m_nSpaceCS = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pBlock->m_nLayoutBindingTc = binding;
                pBlock->m_nSpaceTC = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pBlock->m_nLayoutBindingTe = binding;
                pBlock->m_nSpaceTE = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pBlock->m_nLayoutBindingGs = binding;
                pBlock->m_nSpaceGS = KSPV_GetSet(pCompiler, pResource);
            }
            else
            {
                ASSERT(0);
            }
            // pBlock->m_block16bytesAlignMemoryForGpu = uSize;
            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }


            if (pBlock->m_Uniforms.empty())
            {
                const spirv_cross::SPIRType* pBaseType = KSPV_GetBaseType(pCompiler, pResource);
                uint32_t                     uNumbers = KSPV_GetNumMemberType(pBaseType);
                for (uint32_t j = 0; j < uNumbers; ++j)
                {
                    const char* pName = GetParamNameByPool(KSPV_GetMemberName(pCompiler, pBaseType, j));
                    uint32_t                     uOffset = KSPV_GetMemberOffset(pCompiler, pBaseType, j);
                    uint32_t                     uByteSize = KSPV_GetMemberSize(pCompiler, pBaseType, j);
                    const spirv_cross::SPIRType* type = KSPV_GetMemberType(pCompiler, pBaseType, j);
                    // gfx::enumProgramDataType eType = KSPV_GetProgramDataType(type);
                    gfx::enumUniformBaseType     eType = KSPV_GetBaseType(type);
                    uint32_t                     vecsize = 0, cols = 0, arrays = 0;
                    KSPV_GetType_Rows_Cols_Arrays(type, vecsize, cols, arrays);
                    KProgramUniform* pUniform = new KProgramUniform;

                    pUniform->m_szBlockName = pBlock->m_szName;
                    pUniform->m_szName = pName;
                    pUniform->m_uNameHash = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                    pUniform->m_uVectorSize = vecsize;
                    pUniform->m_uMatcol = cols;
                    pUniform->m_uMatrow = vecsize;
                    pUniform->m_nOffset = uOffset;
                    pUniform->m_uArrayCount = std::max<uint32_t>(arrays, 1);
                    pUniform->m_uByteSize = uByteSize;
                    // pUniform->m_ShaderType = shaderType;
                    pUniform->m_UniformBaseType = eType;
                    pUniform->m_UniformType = pBlock->m_UniformType;
                    pBlock->m_Uniforms.insert(pUniform);
                }
            }
        }



        // pushconsts
        uint32_t uPushConstBlockCount = KSPV_GetNumLivePushConstsBlock(pCompiler);
        for (uint32_t i = 0; i < uPushConstBlockCount; ++i)
        {
            std::vector<KProgramUniform*> tmp;
            BOOL                          bFirstCreate = false;
            if (!m_pPushConstantBlock)
            {
                m_pPushConstantBlock                = new KProgramUniformBlock;
                m_pPushConstantBlock->m_szName      = GetParamNameByPool("PushConstant");
                m_pPushConstantBlock->m_UniformType = gfx::PUSH_CONSTANT_UNIFORM;
                bFirstCreate                        = true;
            }
            const SPIRV_CROSS_NAMESPACE::Resource* pResource                     = KSPV_GetPushConstsResource(pCompiler, i);
            uint32_t                               block16bytesAlignMemoryForGpu = KSPV_GetStruceSize(pCompiler, pResource);

            const spirv_cross::SPIRType* pBaseType = KSPV_GetBaseType(pCompiler, pResource);
            uint32_t                     uNumbers  = KSPV_GetNumMemberType(pBaseType);

            if (uNumbers)
            {
                uint32_t uOffset = KSPV_GetMemberOffset(pCompiler, pBaseType, 0);
                if (!bFirstCreate && uOffset == 0)
                {
                    // 如果之前已经有解析过m_pPushConstantBlock, 并且这里反射得到的offset为0，那么很明显是share不分段的pushconst,也就可以修改成为SHADER_STAGE_ALL_GRAPHICS的范围了
                    for (auto& it : m_pPushConstantBlock->m_vecPushConstantsRangeMap)
                    {
                        it.shadertype = gfx::ShaderStageType::AllGraphics;
                    }
                    break;
                }
            }

            for (uint32_t j = 0; j < uNumbers; ++j)
            {
                const_pool_str               pName   = GetParamNameByPool(KSPV_GetMemberName(pCompiler, pBaseType, j));
                const spirv_cross::SPIRType* type    = KSPV_GetMemberType(pCompiler, pBaseType, j);
                uint32_t                     uOffset = KSPV_GetMemberOffset(pCompiler, pBaseType, j);


                uint32_t uByteSize = KSPV_GetMemberSize(pCompiler, pBaseType, j);

                gfx::enumUniformBaseType eType   = KSPV_GetBaseType(type);
                uint32_t                 vecsize = 0, cols = 0, arrays = 0;
                KSPV_GetType_Rows_Cols_Arrays(type, vecsize, cols, arrays);


                KProgramUniform* pUniform = new KProgramUniform;

                pUniform->m_szBlockName = m_pPushConstantBlock->m_szName;
                pUniform->m_szName      = pName;
                pUniform->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                pUniform->m_uVectorSize = vecsize;
                pUniform->m_uMatcol     = cols;
                pUniform->m_uMatrow     = vecsize;
                if (pUniform->m_uMatcol != 4 || pUniform->m_uMatrow != 4)
                {
                    pUniform->m_uMatcol = 0;
                    pUniform->m_uMatrow = 0;
                }

                if (pUniform->m_uMatcol == 4 && pUniform->m_uMatrow == 4)
                {
                    pUniform->m_uVectorSize = 0;
                }

                pUniform->m_nOffset         = uOffset;
                pUniform->m_uArrayCount     = std::max<uint32_t>(arrays, 1);
                pUniform->m_uByteSize       = uByteSize;
                pUniform->m_UniformBaseType = eType;
                pUniform->m_UniformType     = m_pPushConstantBlock->m_UniformType;
                m_pPushConstantBlock->m_Uniforms.insert(pUniform);
            }


            m_pPushConstantBlock->m_block16bytesAlignMemoryForGpu = block16bytesAlignMemoryForGpu;

            uint32_t previouseRange = 0;
            size_t   sz             = m_pPushConstantBlock->m_vecPushConstantsRangeMap.size();
            if (sz)
            {
                previouseRange = m_pPushConstantBlock->m_vecPushConstantsRangeMap[sz - 1].toRange;
            }
            gfx::KPushContantsRangeMap rangemap;


            rangemap.shadertype = shaderType;

            rangemap.offset  = previouseRange;
            rangemap.toRange = m_pPushConstantBlock->m_block16bytesAlignMemoryForGpu;
            m_pPushConstantBlock->m_vecPushConstantsRangeMap.emplace_back(rangemap);
            // ExitLoop:
        }

        // storage image
        uint32_t uStorageImageCount = KSPV_GetNumLiveStorageImageBlocks(pCompiler);
        for (uint32_t i = 0; i < uStorageImageCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetStorageImageResource(pCompiler, i);
            const spirv_cross::SPIRType*           type      = KSPV_GetType(pCompiler, pResource);
            int32_t                                location  = KSPV_GetLocation(pCompiler, pResource);
            int32_t                                binding   = KSPV_GetBinding(pCompiler, pResource);
            const_pool_str                         pName     = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
            KProgramUniformTexture*                pTex      = nullptr;
            for (auto it : m_vecUniformTexture)
            {
                if (pName == it->m_szName)
                {
                    pTex = it;
                    break;
                }
            }
            if (!pTex)
            {
                uint32_t arraySize  = KSPV_GetTypeArraySize(type);
                pTex                = new KProgramUniformTexture;
                pTex->m_szName      = pName;
                pTex->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                pTex->m_UniformType = gfx::TEXTURE_UNIFORM;
                pTex->m_uArrayCount = std::max<uint32_t>(arraySize, 1);

                if (KSPV_Is1DTexture(type))
                {
                    pTex->m_eTextureType = TextureType::Texture1D;
                }
                else if (KSPV_Is2DTexture(type))
                {
                    pTex->m_eTextureType = TextureType::TextureImage2D;
                }
                else if (KSPV_Is3DTexture(type))
                {
                    pTex->m_eTextureType = TextureType::TextureImage3D;
                }
                else if (KSPV_IsImageBuffer(type))
                {
                    pTex->m_eTextureType = TextureType::RWBuffer;
                }
                else if(KSPV_IsTextureArray(type))
                {
                    pTex->m_eTextureType = TextureType::TextureImage2DArray;
                }

                m_vecUniformTexture.push_back(pTex);
            }

            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pTex->m_nLayoutBindingVs = binding;
                pTex->m_nSpaceVS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pTex->m_nLayoutBindingFs = binding;
                pTex->m_nSpaceFs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pTex->m_nLayoutBindingCs = binding;
                pTex->m_nSpaceCs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pTex->m_nLayoutBindingTc = binding;
                pTex->m_nSpaceTC         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pTex->m_nLayoutBindingTe = binding;
                pTex->m_nSpaceTE         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pTex->m_nLayoutBindingGs = binding;
                pTex->m_nSpaceGS         = KSPV_GetSet(pCompiler, pResource);
            }

            else
            {
                ASSERT(0);
            }

            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }
        }

        // texture
        uint32_t uTextureCount = KSPV_GetNumLiveTexture(pCompiler);
        for (uint32_t i = 0; i < uTextureCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetTextureResource(pCompiler, i);
            const spirv_cross::SPIRType*           type      = KSPV_GetType(pCompiler, pResource);
            int32_t                                location  = KSPV_GetLocation(pCompiler, pResource);
            int32_t                                binding   = KSPV_GetBinding(pCompiler, pResource);
            const_pool_str                         pName     = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
            KProgramUniformTexture*                pTex      = nullptr;
            for (auto it : m_vecUniformTexture)
            {
                if (pName == it->m_szName)
                {
                    pTex = it;
                    break;
                }
            }
            if (!pTex)
            {
                uint32_t arraySize = KSPV_GetTypeArraySize(type);
                pTex               = new KProgramUniformTexture;
                pTex->m_szName     = pName;

                pTex->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                pTex->m_UniformType = gfx::TEXTURE_UNIFORM;
                pTex->m_uArrayCount = std::max<uint32_t>(arraySize, 1);
                BOOL bArray         = KSPV_IsTextureArray(type);
                if (KSPV_Is1DTexture(type))
                {
                    pTex->m_eTextureType = bArray ? TextureType::Texture1DArray : TextureType::Texture1D;
                }
                else if (KSPV_Is2DTexture(type))
                {
                    pTex->m_eTextureType = bArray ? TextureType::Texture2DArray : TextureType::Texture2D;
                }
                else if (KSPV_Is3DTexture(type))
                {
                    pTex->m_eTextureType = TextureType::Texture3D;
                }
                else if (KSPV_IsCube(type))
                {
                    pTex->m_eTextureType = bArray ? TextureType::CubemapArray : TextureType::Cubemap;
                }
                m_vecUniformTexture.push_back(pTex);
            }
            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pTex->m_nLayoutBindingVs = binding;
                pTex->m_nSpaceVS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pTex->m_nLayoutBindingFs = binding;
                pTex->m_nSpaceFs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pTex->m_nLayoutBindingCs = binding;
                pTex->m_nSpaceCs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pTex->m_nLayoutBindingTc = binding;
                pTex->m_nSpaceTC         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pTex->m_nLayoutBindingTe = binding;
                pTex->m_nSpaceTE         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pTex->m_nLayoutBindingGs = binding;
                pTex->m_nSpaceGS         = KSPV_GetSet(pCompiler, pResource);
            }
            else
            {
                ASSERT(0);
            }

            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }
        }

        // combine sampler texture
        uint32_t uSamplerTextureCount = KSPV_GetNumLiveSamplerTexture(pCompiler);
        for (uint32_t i = 0; i < uSamplerTextureCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetSamplerTextureResource(pCompiler, i);
            const spirv_cross::SPIRType*           type      = KSPV_GetType(pCompiler, pResource);
            int32_t                                location  = KSPV_GetLocation(pCompiler, pResource);
            int32_t                                binding   = KSPV_GetBinding(pCompiler, pResource);
            const_pool_str                         pName     = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
            KProgramUniformTexture*                pTex      = nullptr;
            for (auto it : m_vecUniformTexture)
            {
                if (pName == it->m_szName)
                {
                    pTex = it;
                    break;
                }
            }

            if (!pTex)
            {
                uint32_t arraySize   = KSPV_GetTypeArraySize(type);
                pTex                 = new KProgramUniformTexture;
                pTex->m_szName       = pName;
                pTex->m_uNameHash    = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                pTex->m_UniformType  = gfx::TEXTURE_UNIFORM;
                pTex->m_uArrayCount  = std::max<uint32_t>(arraySize, 1);
                pTex->m_eTextureType = TextureType::CombinedSamplerBuffer;

                BOOL bArray = KSPV_IsTextureArray(type);
                if (KSPV_Is1DTexture(type))
                {
                    pTex->m_eTextureType = bArray ? TextureType::Texture1DArray : TextureType::Texture1D;
                }
                else if (KSPV_Is1DTexture(type))
                {
                    pTex->m_eTextureType = bArray ? TextureType::Texture2DArray : TextureType::Texture2D;
                }
                else if (KSPV_Is3DTexture(type))
                {
                    pTex->m_eTextureType = TextureType::Texture3D;
                }
                else if (KSPV_IsCube(type))
                {
                    pTex->m_eTextureType = TextureType::Cubemap;
                }
                m_vecUniformTexture.push_back(pTex);
            }

            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pTex->m_nLayoutBindingVs = binding;
                pTex->m_nSpaceVS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pTex->m_nLayoutBindingFs = binding;
                pTex->m_nSpaceFs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pTex->m_nLayoutBindingCs = binding;
                pTex->m_nSpaceCs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pTex->m_nLayoutBindingTc = binding;
                pTex->m_nSpaceTC         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pTex->m_nLayoutBindingTe = binding;
                pTex->m_nSpaceTE         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pTex->m_nLayoutBindingGs = binding;
                pTex->m_nSpaceGS         = KSPV_GetSet(pCompiler, pResource);
            }
            else
            {
                ASSERT(0);
            }

            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }
        }

        // sampler
        uint32_t uSamplerCount = KSPV_GetNumLiveSampler(pCompiler);
        for (uint32_t i = 0; i < uSamplerCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::Resource* pResource = KSPV_GetSamplerResource(pCompiler, i);
            int32_t                                location  = KSPV_GetLocation(pCompiler, pResource);
            int32_t                                binding   = KSPV_GetBinding(pCompiler, pResource);
            const_pool_str                         pName     = GetParamNameByPool(KSPV_GetName(pCompiler, pResource));
            gfx::KProgramUniformSampler*           pSampler  = nullptr;
            for (auto it : m_vecUniformSampler)
            {
                if (pName == it->m_szName)
                {
                    pSampler = it;
                    break;
                }
            }
            if (!pSampler)
            {
                pSampler                = new KProgramUniformSampler;
                pSampler->m_szName      = pName;
                pSampler->m_uNameHash   = KSTR_HELPER::GetHashCodeForString32Bit(pName);
                pSampler->m_UniformType = gfx::SAMPLER_UNIFORM;

                auto it = m_mapSamplerState.find(pName);
                if (it != m_mapSamplerState.end())
                {
                    pSamplerState = &it->second;
                }
                else
                {
                    pSamplerDef = pShaderStage->GetSamplerDef(pName);
                    if (pSamplerDef)
                    {
                        pSamplerState = pSamplerDef->GetSamplerState();
                    }
                    else
                    {
                        if (!bByBuildTool && bForMaterialSystem && DrvOption::bEnableShaderSamplerStateFix)
                        {
                            KGLogPrintf(KGLOG_ERR, "[KShaderResourceVK::BuildReflection] failed to get sampler def, uniform name: %s, shader: %s", pName, pShaderStage->GetShaderName());
                        }

                        // static gfx::KSamplerState sDummySamplerState;
                        // pSamplerState = &sDummySamplerState;
                    }
                }

                // KGLOG_ASSERT_EXIT(pSamplerState);
                if (pSamplerState && !pSamplerState->bNeedShaderInit)
                {
                    pSampler->m_SamplerState = *pSamplerState;

                    if (!bByBuildTool)
                    {
                        pSampler->m_pSampler = pGfxDevice->GetSamplerByState(pSamplerState);
                        KGLOG_ASSERT_EXIT(pSampler->m_pSampler);
                    }
                }
                else
                {
                    // ASSERT(pSampler->m_SamplerState.bNeedShaderInit == TRUE);
                }

                m_vecUniformSampler.push_back(pSampler);
            }

            if (shaderType == gfx::ShaderStageType::Vertex)
            {
                pSampler->m_nLayoutBindingVs = binding;
                pSampler->m_nSpaceVS         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Fragment)
            {
                pSampler->m_nLayoutBindingFs = binding;
                pSampler->m_nSpaceFs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Compute)
            {
                pSampler->m_nLayoutBindingCs = binding;
                pSampler->m_nSpaceCs         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Hull)
            {
                pSampler->m_nLayoutBindingTc = binding;
                pSampler->m_nSpaceTC         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Domain)
            {
                pSampler->m_nLayoutBindingTe = binding;
                pSampler->m_nSpaceTE         = KSPV_GetSet(pCompiler, pResource);
            }
            else if (shaderType == gfx::ShaderStageType::Geometry)
            {
                pSampler->m_nLayoutBindingGs = binding;
                pSampler->m_nSpaceGS         = KSPV_GetSet(pCompiler, pResource);
            }
            else
            {
                ASSERT(0);
            }

            if (binding != 0xffff && (int)binding > m_nMaxBinding)
            {
                m_nMaxBinding = binding;
            }
        }


        // specializationConst
        uint32_t uSpecializationConstCount = KSPV_GetNumSpecializationConsts(pCompiler);
        for (uint32_t i = 0; i < uSpecializationConstCount; ++i)
        {
            const SPIRV_CROSS_NAMESPACE::SpecializationConstant* pResource = KSPV_GetSpecializationConstsResource(pCompiler, i);
            const_pool_str                                       pName     = GetParamNameByPool(KSPV_GetSpecializationConstName(pCompiler, pResource));
            uint32_t                                             constid   = KSPV_GetSpecializationConstId(pResource);
            uint32_t                                             stageId   = 0;
            if (shaderType == gfx::ShaderStageType::Fragment)
            {
                stageId = 1;
            }
            AddSpecializationConstDefine(stageId, constid, pName);
        }

        if (shaderType == gfx::ShaderStageType::Vertex)
        {
            m_vsPushConstantSize = GetPushContentAlign16BytesBlockSize();
        }


        for (auto it : m_vecUniformBlock)
        {
            gfx::KProgramUniformBlock* pUniformBlock = it;
            if (pUniformBlock->m_szName == mtlUboName0 || pUniformBlock->m_szName == mtlUboName1)
            {
                for (auto it : pUniformBlock->m_Uniforms)
                {
                    gfx::KProgramUniform* pUniform        = it;
                    m_mapMtlParamItem[pUniform->m_szName] = pUniform;
                }
            }
        }
    }
    bResult           = TRUE;
    m_buildedReflect0 = true;
Exit0:
    m_buildedReflect1      = true;
    m_bRefelcted[shaderid] = true;
    KSPV_ReleaseCrossCompiler(pCompiler);
    return bResult;
}

gfx::IKGFX_CombinedShaderResult* KShaderResourceVK::GetCombindShaderResult()
{
    return m_pCombinedShaderResultVK_HLSL;
}


BOOL KShaderResourceVK::HasVsShaderTmpData()
{
    if (m_pVsTmpData || m_pTempVsSpirv)
        return true;
    else
        return false;
}

void KShaderResourceVK::SetupVsShaderTmpData(const char* iopath, const char* szShaderName, const char* pString, uint32_t stage, uint32_t shadertype, uint32_t shaderTypeBit)
{
    if (!m_pVsTmpData)
    {
        m_pVsTmpData = new KVsTmpData;
    }
    m_pVsTmpData->iopath        = iopath;
    m_pVsTmpData->szShaderName  = szShaderName;
    m_pVsTmpData->vsString      = pString;
    m_pVsTmpData->stage         = stage;
    m_pVsTmpData->shaderType    = shadertype;
    m_pVsTmpData->shaderTypeBit = shaderTypeBit;
}

void KShaderResourceVK::SetupVsShaderTmpData(uint32_t* pVsSpirv, uint32_t uIntCount)
{
    if (pVsSpirv && uIntCount)
    {
        if (uIntCount != m_uTempVsSpirvIntCount)
        {
            SAFE_DELETE_ARRAY(m_pTempVsSpirv);
            m_pTempVsSpirv         = new uint32_t[uIntCount];
            m_uTempVsSpirvIntCount = uIntCount;
        }
        if (m_uTempVsSpirvIntCount == uIntCount)
        {
            memcpy(m_pTempVsSpirv, pVsSpirv, sizeof(uint32_t) * uIntCount);
        }
    }
}
uint32_t* KShaderResourceVK::GetVsTmpSpirv()
{
    return m_pTempVsSpirv;
};

uint32_t KShaderResourceVK::GetVsShaderTmpSpirvUintCount()
{
    return m_uTempVsSpirvIntCount;
}

const char* KShaderResourceVK::GetVsTmp_iopath()
{
    return m_pVsTmpData->iopath.c_str();
}
const char* KShaderResourceVK::GetVsTmp_szShaderName()
{
    return m_pVsTmpData->szShaderName.c_str();
}
const char* KShaderResourceVK::GetVsTmp_vsString()
{
    return m_pVsTmpData->vsString.c_str();
}
uint32_t KShaderResourceVK::GetVsTmp_stage()
{
    return m_pVsTmpData->stage;
}
uint32_t KShaderResourceVK::GetVsTmp_shaderType()
{
    return m_pVsTmpData->shaderType;
}
uint32_t KShaderResourceVK::GetVsTmp_shaderTypeBit()
{
    return m_pVsTmpData->shaderTypeBit;
}

void KShaderResourceVK::ClearVsTmpData()
{
    if (m_pVsTmpData)
    {
        SAFE_DELETE(m_pVsTmpData);
    }
    SAFE_DELETE_ARRAY(m_pTempVsSpirv);
    m_uTempVsSpirvIntCount = 0;
}

BOOL KShaderResourceVK::FindBindingForFixShaderContent(const char* szBlockName, int32_t& binding)
{
    BOOL bRet = false;
    binding   = 0;
    auto it   = m_blockNameBindingMapForFixShaderContent.find(szBlockName);
    if (it != m_blockNameBindingMapForFixShaderContent.end())
    {
        binding = it->second;
        bRet    = true;
    }
    return bRet;
}
void KShaderResourceVK::InsertBindingForFixShaderContent(const char* szBlockName, int32_t binding)
{
    m_blockNameBindingMapForFixShaderContent.insert({szBlockName, binding});
}

BOOL KShaderResourceVK::IsLogShader()
{
    return DrvOption::bLogShader;
}

gfx::KDescriptorPoolContainer* KShaderResourceVK::GetDescriptorPoolContainer()
{
    return &m_DescriptorPoolContainer;
}

void KShaderResourceVK::AddSpecializationConstDefine(uint32_t uStageType, uint32_t const_id, const char* pName)
{
    KSpecializationConstDefine define;
    define.uStageType = uStageType;
    define.uConstId   = const_id;
    define.pName      = GetParamNameByPool(pName);

    BOOL bFind = false;
    for (const auto& it : m_vecSpecializationConstDefine)
    {
        if (it.uStageType == uStageType && it.uConstId == const_id && it.pName == define.pName)
        {
            bFind = true;
            break;
        }
    }
    if (!bFind)
    {
        m_vecSpecializationConstDefine.emplace_back(define);
    }
}

uint32_t KShaderResourceVK::GetPushContentAlign16BytesBlockSize()
{
    uint32_t uSize = 0;
    if (m_pPushConstantBlock && m_pPushConstantBlock->m_block16bytesAlignMemoryForGpu)
    {
        uint32_t n = m_pPushConstantBlock->m_block16bytesAlignMemoryForGpu / 16;
        uint32_t m = m_pPushConstantBlock->m_block16bytesAlignMemoryForGpu % 16;
        if (m > 0)
        {
            n++;
        }
        uSize = n * 16;
    }
    return uSize;
}

uint32_t KShaderResourceVK::GetVsPushConstantSize()
{
    return m_vsPushConstantSize;
}

void KShaderResourceVK::SetVsPushContantSize(uint32_t uVsPushContantSize)
{
    m_vsPushConstantSize = uVsPushContantSize;
}


BOOL KShaderResourceVK::RequestVertDescriptor(gfx::KVertexDecl* pDecls[], uint32_t uDeclCount, gfx::KVulkanVertexDescriptor** pDescriptor)
{
    PROF_CPU_DETAIL();
    ASSERT(IsMainThread());

    BOOL                    bRet           = false;
    BOOL                    bRetCode       = false;
    gfx::KVulkanVertexDescriptor* pRetDesc       = nullptr;
    gfx::KGraphicDevice*    pGraphicDevice = gfx::GetGraphicDevice();
    VertDescriptor*         pNewDesc       = nullptr;
    uint32_t                binded         = 0;
    *pDescriptor                           = nullptr;
    int mapIndex[KMAX_BIND_VERT_STREAM];
    ASSERT(uDeclCount <= KMAX_BIND_VERT_STREAM);
    int index = 0;


    for (auto it : m_vecVertDescriptorCache)
    {
        VertDescriptor* p     = it;
        BOOL            bFind = true;
        if (p->m_vecDecls.size() == uDeclCount)
        {
            for (uint32_t i = 0; i < uDeclCount; ++i)
            {
                if (p->m_vecDecls[i] != pDecls[i])
                {
                    bFind = false;
                    break;
                }
            }
        }
        else
        {
            bFind = false;
        }

        if (bFind)
        {
            pRetDesc = p->pVertDescriptor;
            goto Exit1;
        }
    }

    bRetCode = pGraphicDevice->CreateVertDescriptor(&pRetDesc);
    KGLOG_PROCESS_ERROR(bRetCode);

    pRetDesc->Begin();

    for (uint32_t i = 0; i < uDeclCount; ++i)
    {
        if (pDecls[i]->m_ItemTable[0] == gfx::KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE)
        {
            pRetDesc->AddBindDescription(INSTANCE_BUFFER_BIND_ID, pDecls[i]->m_stride, gfx::VERTEX_INPUT_RATE_INSTANCE);
        }
        else
        {
            pRetDesc->AddBindDescription(VERTEX_BUFFER_BIND_ID0 + index, pDecls[i]->m_stride, gfx::VERTEX_INPUT_RATE_VERTEX);
            mapIndex[i] = index;
            index++;
        }
    }

    for (auto attr : m_vecAttribute)
    {
        uint32_t usage = attr->nLocation;

        for (uint32_t i = 0; i < uDeclCount; ++i)
        {
            gfx::KVertexDecl* pDecl = pDecls[i];

            for (uint32_t j = 0; j < pDecl->m_nItem; ++j)
            {
                gfx::KAttribUsage::Enum _usage = pDecl->m_ItemTable[j];
                if (_usage == usage)
                {
                    auto& att = pDecl->m_Attr[_usage];
                    if (_usage >= gfx::KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE && _usage < gfx::KAttribUsage::VERT_INSTANCE_MAX_BOUND)
                    {
                        pRetDesc->AddAttribute(INSTANCE_BUFFER_BIND_ID, _usage, attr->fmt, att.m_offset);
                    }
                    else
                    {
                        index = mapIndex[i];
                        pRetDesc->AddAttribute(VERTEX_BUFFER_BIND_ID0 + index, _usage, attr->fmt, att.m_offset);
                    }
                    ++binded;
                    goto NextAttributeLoop;
                }
            }
        }
    NextAttributeLoop:
        continue;
    }

    if (binded != (uint32_t)m_vecAttribute.size())
    {
        KGLogPrintf(KGLOG_WARNING, "some vertex shader attribute not bind!");
        // goto Exit0;
    }

    bRetCode = pRetDesc->End();
    KGLOG_PROCESS_ERROR(bRetCode);

    pNewDesc                  = new VertDescriptor;
    pNewDesc->pVertDescriptor = pRetDesc;
    for (uint32_t i = 0; i < uDeclCount; ++i)
    {
        pNewDesc->m_vecDecls.push_back(pDecls[i]);
    }
    m_vecVertDescriptorCache.push_back(pNewDesc);

Exit1:
    bRet = true;
Exit0:
    if (bRet)
    {
        *pDescriptor = pRetDesc;
    }
    return bRet;
}


gfx::KShaderStage* KShaderResourceVK::GetVSStage()
{
    gfx::KShaderStage* p = nullptr;
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (m_pShaderStage[i] && m_pShaderStage[i]->GetShaderInfo()->eShaderStage == gfx::ShaderStageType::Vertex)
        {
            p = m_pShaderStage[i];
            break;
        }
    }
    return p;
}

gfx::KShaderStage* KShaderResourceVK::GetFsStage()
{
    gfx::KShaderStage* p = nullptr;
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (m_pShaderStage[i] && m_pShaderStage[i]->GetShaderInfo()->eShaderStage == gfx::ShaderStageType::Fragment)
        {
            p = m_pShaderStage[i];
            break;
        }
    }
    return p;
}

gfx::KSamplerState* KShaderResourceVK::GetSamplerState(const char* pcszSamplerName)
{
    for (uint32_t i = 0; i < MAX_SHADER_STAGE_COUNT; ++i)
    {
        gfx::KShaderStage* pStage           = m_pShaderStage[i];
        uint32_t           uSamplerDefCount = pStage->GetSamplerDefCount();
        for (uint32_t j = 0; j < uSamplerDefCount; ++j)
        {
            gfx::IKStageSamplerDef* p = m_pShaderStage[i]->GetSamplerDef(j);
            if (strcmp(p->GetSamplerName(), pcszSamplerName) == 0)
            {
                return p->GetSamplerState();
            }
        }
    }
    return nullptr;

    // if (pcszSamplerName && pcszSamplerName[0] != '\0')
    //{
    //	if (m_pShaderStage[1])
    //	{
    //		uint32_t uSamplerDefCount = m_pShaderStage[1]->GetSamplerDefCount();
    //		for (uint32_t i = 0; i < uSamplerDefCount; ++i)
    //		{
    //			gfx::IKStageSamplerDef* p = m_pShaderStage[1]->GetSamplerDef(i);
    //			if (strcmp(p->GetSamplerName(), pcszSamplerName) == 0)
    //			{
    //				return p->GetSamplerState();
    //			}
    //		}
    //	}
    //	if (m_pShaderStage[0])
    //	{
    //		uint32_t uSamplerDefCount = m_pShaderStage[0]->GetSamplerDefCount();
    //		for (uint32_t i = 0; i < uSamplerDefCount; ++i)
    //		{
    //			gfx::IKStageSamplerDef* p = m_pShaderStage[0]->GetSamplerDef(i);
    //			if (strcmp(p->GetSamplerName(), pcszSamplerName) == 0)
    //			{
    //				return p->GetSamplerState();
    //			}
    //		}
    //	}
    // }
    // return nullptr;
}

BOOL KShaderResourceVK::SetupLayout(gfx::KGFX_ProgramBinderVK* pBinderVK)
{
    PROF_CPU_DETAIL();
    std::lock_guard<std::mutex> lock(m_lock);
    BOOL                        bRet         = false;
    BOOL                        bRetCode     = false;
    uint32_t                    vertSize     = 0;
    uint32_t                    instanceSize = 0;

    uint32_t uVertoffset        = 0;
    uint32_t uInstanceOffset    = 0;
    uint32_t uPushContantOffset = 0;

    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();

    // int32_t threadid = GetThreadId();
    KG_PROCESS_SUCCESS(m_DescriptorPoolContainer.m_pDescriptorPool);

    bRetCode = pGraphicDevice->CreateDescriptorPool(&m_DescriptorPoolContainer.m_pDescriptorPool);
    KGLOG_PROCESS_ERROR(bRetCode);

    m_DescriptorPoolContainer.m_pDescriptorPool->Begin();
    // if (!m_vecUniformBlock.empty())
    //{
    //   m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)m_vecUniformBlock.size());
    // }
    if (!m_vecUniformBlock.empty())
    {
        uint32_t dyUboNeeded  = 0;
        uint32_t uboNeeded    = 0;
        uint32_t ssboNeeded   = 0;
        uint32_t dySSBONeeded = 0;

        for (auto it : m_vecUniformBlock)
        {
            gfx::KProgramUniformBlock* pBlock = it;


            // if (DrvOption::bSupportDynamicUBO && !m_bForceStaticUBO)
            {
                if (pBlock->m_nLayoutBindingVs >= 0 ||
                    pBlock->m_nLayoutBindingFs >= 0 ||
                    pBlock->m_nLayoutBindingCs >= 0 ||
                    pBlock->m_nLayoutBindingGs >= 0 ||
                    pBlock->m_nLayoutBindingTc >= 0 ||
                    pBlock->m_nLayoutBindingTe >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        if (pBinderVK->IsDynamicBlock(pBlock->m_szName))
                        {
                            ++dyUboNeeded;
                        }
                        else
                        {
                            ++uboNeeded;
                        }
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        if (pBinderVK->IsDynamicBlock(pBlock->m_szName))
                        {
                            ++dySSBONeeded;
                        }
                        else
                        {
                            ++ssboNeeded;
                        }
                    }
                }
            }
            // else
            //{
            //     if (pBlock->m_nLayoutBindingVs >= 0 ||
            //         pBlock->m_nLayoutBindingFs >= 0 ||
            //         pBlock->m_nLayoutBindingCs >= 0 ||
            //         pBlock->m_nLayoutBindingGs >= 0 ||
            //         pBlock->m_nLayoutBindingTc >= 0 ||
            //         pBlock->m_nLayoutBindingTe >= 0
            //         )
            //     {
            //         // m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
            //         if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
            //         {
            //             ++uboNeeded;
            //         }
            //         else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
            //         {
            //             ++ssboNeeded;
            //         }
            //     }
            // }
        }

        if (dyUboNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, dyUboNeeded);
        }

        if (uboNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboNeeded);
        }

        if (ssboNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, ssboNeeded);
        }

        if (dySSBONeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, dySSBONeeded);
        }
    }

    if (!m_vecUniformTexture.empty())
    {
        uint32_t uSampleImageNeeded      = 0;
        uint32_t uStorageImageNeeded     = 0;
        uint32_t uStorageTexelBufferNeed = 0;
        uint32_t uSamplerBufferNeed      = 0;

        for (const auto& it : m_vecUniformTexture)
        {
            gfx::KProgramUniformTexture* pTexture    = it;
            uint32_t                     uArrayCount = it->m_uArrayCount;

            if (pTexture->m_eTextureType == TextureType::RWBuffer)
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uStorageTexelBufferNeed += uArrayCount;
                }
            }
            else if (pTexture->m_eTextureType == TextureType::CombinedSamplerBuffer)
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uSamplerBufferNeed += uArrayCount;
                }
            }
            else if (pTexture->m_eTextureType == TextureType::TextureImage2D ||
                pTexture->m_eTextureType == TextureType::TextureImage3D ||
                pTexture->m_eTextureType == TextureType::TextureImage2DArray
                )
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uStorageImageNeeded += uArrayCount;
                }
            }
            else
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uSampleImageNeeded += uArrayCount;
                }
            }
        }

        if (uSampleImageNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE, uSampleImageNeeded);
        }

        if (uStorageImageNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE, uStorageImageNeeded);
        }

        if (uStorageTexelBufferNeed)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, uStorageTexelBufferNeed);
        }

        if (uSamplerBufferNeed)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, uSamplerBufferNeed);
        }
    }

    if (!m_vecUniformSampler.empty())
    {
        uint32_t uNeeded = 0;
        for (const auto& it : m_vecUniformSampler)
        {
            gfx::KProgramUniformSampler* pSampler = it;
            if (pSampler->m_nLayoutBindingVs >= 0 ||
                pSampler->m_nLayoutBindingFs >= 0 ||
                pSampler->m_nLayoutBindingCs >= 0 ||
                pSampler->m_nLayoutBindingGs >= 0 ||
                pSampler->m_nLayoutBindingTc >= 0 ||
                pSampler->m_nLayoutBindingTe >= 0)
            {
                ++uNeeded;
            }
        }
        if (uNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLER, uNeeded);
        }
    }
    bRetCode = m_DescriptorPoolContainer.m_pDescriptorPool->End();
    KGLOG_PROCESS_ERROR(bRetCode);

    if (!m_pLayout)
    {
        bRetCode = pGraphicDevice->CreateLayout(&m_pLayout);
        KGLOG_PROCESS_ERROR(bRetCode);

        m_pLayout->Begin();

        for (auto it : m_vecUniformBlock)
        {
            gfx::KProgramUniformBlock* pBlock   = it;
            BOOL                       bDynamic = pBinderVK->IsDynamicBlock(pBlock->m_szName);

            enumDescriptorType descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;

            if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
            {
                if (bDynamic)
                {
                    descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                }
                else
                {
                    descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                }
            }
            else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
            {
                if (bDynamic)
                {
                    descriptorType = DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                }
                else
                {
                    descriptorType = gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
            }
            else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
            {
                descriptorType = gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE;
            }
            else
            {
                ASSERT(0);
            }

            // if (DrvOption::bSupportDynamicUBO && !m_bForceStaticUBO)
            {
                if (pBlock->m_nLayoutBindingVs >= 0)
                {
                    m_pLayout->AddLayout(0, descriptorType, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                }

                if (pBlock->m_nLayoutBindingFs >= 0)
                {
                    m_pLayout->AddLayout(0, descriptorType, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingFs);
                }

                if (pBlock->m_nLayoutBindingCs >= 0)
                {
                    m_pLayout->AddLayout(0, descriptorType, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingCs);
                }

                if (pBlock->m_nLayoutBindingGs >= 0)
                {
                    m_pLayout->AddLayout(0, descriptorType, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingGs);
                }

                if (pBlock->m_nLayoutBindingTc >= 0)
                {
                    m_pLayout->AddLayout(0, descriptorType, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingTc);
                }

                if (pBlock->m_nLayoutBindingTe >= 0)
                {
                    m_pLayout->AddLayout(0, descriptorType, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingTe);
                }
            }
        }

        for (auto it : m_vecUniformTexture)
        {
            gfx::KProgramUniformTexture* pTexture  = it;
            gfx::enumDescriptorType      eDescType = gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE;

            if (pTexture->m_eTextureType == TextureType::RWBuffer)
            {
                eDescType = gfx::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            }

            if (pTexture->m_eTextureType == TextureType::CombinedSamplerBuffer)
            {
                eDescType = gfx::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            }

            if (pTexture->m_eTextureType == TextureType::TextureImage2D ||
                pTexture->m_eTextureType == TextureType::TextureImage3D ||
                pTexture->m_eTextureType == TextureType::TextureImage2DArray
                )
            {
                eDescType = gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }

            // DESCRIPTOR_TYPE_STORAGE_IMAGE

            if (pTexture->m_nLayoutBindingVs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Vertex, pTexture->m_nLayoutBindingVs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingFs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Fragment, pTexture->m_nLayoutBindingFs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingCs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Compute, pTexture->m_nLayoutBindingCs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingGs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Geometry, pTexture->m_nLayoutBindingGs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingTc >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Hull, pTexture->m_nLayoutBindingTc, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingTe >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Domain, pTexture->m_nLayoutBindingTe, pTexture->m_uArrayCount);
            }
        }
        for (auto it : m_vecUniformSampler)
        {
            gfx::KProgramUniformSampler* pSampler = it;
            if (pSampler->m_nLayoutBindingVs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Vertex, pSampler->m_nLayoutBindingVs);
            }

            if (pSampler->m_nLayoutBindingFs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Fragment, pSampler->m_nLayoutBindingFs);
            }

            if (pSampler->m_nLayoutBindingCs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Compute, pSampler->m_nLayoutBindingCs);
            }

            if (pSampler->m_nLayoutBindingGs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Geometry, pSampler->m_nLayoutBindingGs);
            }

            if (pSampler->m_nLayoutBindingTc >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Hull, pSampler->m_nLayoutBindingTc);
            }

            if (pSampler->m_nLayoutBindingTe >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Domain, pSampler->m_nLayoutBindingTe);
            }
        }

        if (m_pPushConstantBlock && !m_pPushConstantBlock->m_vecPushConstantsRangeMap.empty())
        {
            // for (auto it : m_pPushConstantBlock->m_Uniforms)
            //{
            //   KProgramUniform* pUniform = it;
            //   m_pLayout->AddPushContantRange(pUniform->m_ShaderType, pUniform->m_uByteSize, pUniform->m_nOffset);
            // }

            for (auto& it : m_pPushConstantBlock->m_vecPushConstantsRangeMap)
            {
                const gfx::KPushContantsRangeMap& rangemap = it;
                m_pLayout->AddPushContantRange(rangemap.shadertype, rangemap.toRange - rangemap.offset, rangemap.offset);
            }
        }

        bRetCode = m_pLayout->End();
        KGLOG_PROCESS_ERROR(bRetCode);
    }
Exit1:
    bRet = true;
Exit0:
    return bRet;
}

BOOL KShaderResourceVK::SetupLayout()
{
    PROF_CPU_DETAIL();
    std::lock_guard<std::mutex> lock(m_lock);
    BOOL                        bRet         = false;
    BOOL                        bRetCode     = false;
    uint32_t                    vertSize     = 0;
    uint32_t                    instanceSize = 0;

    uint32_t uVertoffset        = 0;
    uint32_t uInstanceOffset    = 0;
    uint32_t uPushContantOffset = 0;

    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();

    // int32_t threadid = GetThreadId();
    KG_PROCESS_SUCCESS(m_DescriptorPoolContainer.m_pDescriptorPool);

    bRetCode = pGraphicDevice->CreateDescriptorPool(&m_DescriptorPoolContainer.m_pDescriptorPool);
    KGLOG_PROCESS_ERROR(bRetCode);

    m_DescriptorPoolContainer.m_pDescriptorPool->Begin();
    // if (!m_vecUniformBlock.empty())
    //{
    //   m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)m_vecUniformBlock.size());
    // }
    if (!m_vecUniformBlock.empty())
    {
        uint32_t dyUboNeeded  = 0;
        uint32_t uboNeeded    = 0;
        uint32_t ssboNeeded   = 0;
        uint32_t dySSBONeeded = 0;

        for (auto it : m_vecUniformBlock)
        {
            gfx::KProgramUniformBlock* pBlock = it;

            if (DrvOption::bSupportDynamicUBO && !m_bForceStaticUBO)
            {
                if (pBlock->m_nLayoutBindingVs >= 0 ||
                    pBlock->m_nLayoutBindingFs >= 0 ||
                    pBlock->m_nLayoutBindingCs >= 0 ||
                    pBlock->m_nLayoutBindingGs >= 0 ||
                    pBlock->m_nLayoutBindingTc >= 0 ||
                    pBlock->m_nLayoutBindingTe >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        ++dyUboNeeded;
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        ++dySSBONeeded;
                    }
                }
            }
            else
            {
                if (pBlock->m_nLayoutBindingVs >= 0 ||
                    pBlock->m_nLayoutBindingFs >= 0 ||
                    pBlock->m_nLayoutBindingCs >= 0 ||
                    pBlock->m_nLayoutBindingGs >= 0 ||
                    pBlock->m_nLayoutBindingTc >= 0 ||
                    pBlock->m_nLayoutBindingTe >= 0)
                {
                    // m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        ++uboNeeded;
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        ++ssboNeeded;
                    }
                }
            }
        }

        if (dyUboNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, dyUboNeeded);
        }

        if (uboNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboNeeded);
        }

        if (ssboNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, ssboNeeded);
        }

        if (dySSBONeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, dySSBONeeded);
        }
    }

    if (!m_vecUniformTexture.empty())
    {
        uint32_t uSampleImageNeeded      = 0;
        uint32_t uStorageImageNeeded     = 0;
        uint32_t uStorageTexelBufferNeed = 0;
        uint32_t uSamplerBufferNeed      = 0;

        for (const auto& it : m_vecUniformTexture)
        {
            gfx::KProgramUniformTexture* pTexture    = it;
            uint32_t                     uArrayCount = it->m_uArrayCount;

            if (pTexture->m_eTextureType == TextureType::RWBuffer)
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uStorageTexelBufferNeed += uArrayCount;
                }
            }
            else if (pTexture->m_eTextureType == TextureType::CombinedSamplerBuffer)
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uSamplerBufferNeed += uArrayCount;
                }
            }
            else if (pTexture->m_eTextureType == TextureType::TextureImage2D ||
                pTexture->m_eTextureType == TextureType::TextureImage3D ||
                pTexture->m_eTextureType == TextureType::TextureImage2DArray
                )
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uStorageImageNeeded += uArrayCount;
                }
            }
            else
            {
                if (pTexture->m_nLayoutBindingVs >= 0 ||
                    pTexture->m_nLayoutBindingFs >= 0 ||
                    pTexture->m_nLayoutBindingCs >= 0 ||
                    pTexture->m_nLayoutBindingGs >= 0 ||
                    pTexture->m_nLayoutBindingTc >= 0 ||
                    pTexture->m_nLayoutBindingTe >= 0)
                {
                    uSampleImageNeeded += uArrayCount;
                }
            }
        }

        if (uSampleImageNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE, uSampleImageNeeded);
        }

        if (uStorageImageNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE, uStorageImageNeeded);
        }

        if (uStorageTexelBufferNeed)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, uStorageTexelBufferNeed);
        }

        if (uSamplerBufferNeed)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, uSamplerBufferNeed);
        }
    }

    if (!m_vecUniformSampler.empty())
    {
        uint32_t uNeeded = 0;
        for (const auto& it : m_vecUniformSampler)
        {
            gfx::KProgramUniformSampler* pSampler = it;
            if (pSampler->m_nLayoutBindingVs >= 0 ||
                pSampler->m_nLayoutBindingFs >= 0 ||
                pSampler->m_nLayoutBindingCs >= 0 ||
                pSampler->m_nLayoutBindingGs >= 0 ||
                pSampler->m_nLayoutBindingTc >= 0 ||
                pSampler->m_nLayoutBindingTe >= 0)
            {
                ++uNeeded;
            }
        }
        if (uNeeded)
        {
            m_DescriptorPoolContainer.m_pDescriptorPool->AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLER, uNeeded);
        }
    }
    bRetCode = m_DescriptorPoolContainer.m_pDescriptorPool->End();
    KGLOG_PROCESS_ERROR(bRetCode);

    if (!m_pLayout)
    {
        bRetCode = pGraphicDevice->CreateLayout(&m_pLayout);
        KGLOG_PROCESS_ERROR(bRetCode);

        m_pLayout->Begin();

        for (auto it : m_vecUniformBlock)
        {
            gfx::KProgramUniformBlock* pBlock = it;
            if (DrvOption::bSupportDynamicUBO && !m_bForceStaticUBO)
            {
                if (pBlock->m_nLayoutBindingVs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingFs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingFs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingFs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingCs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingCs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingCs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingGs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingGs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingGs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingTc >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingTc);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingTc);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingTe >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingTe);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingTe);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }
            }
            else
            {
                if (pBlock->m_nLayoutBindingVs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Vertex, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingFs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingFs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingFs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Fragment, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingCs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingCs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingCs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Compute, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingGs >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingGs);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingGs);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Geometry, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingTc >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingTc);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingTc);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Hull, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }

                if (pBlock->m_nLayoutBindingTe >= 0)
                {
                    if (pBlock->m_UniformType == gfx::UBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingTe);
                    }
                    else if (pBlock->m_UniformType == gfx::SSBO_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingTe);
                    }
                    else if (pBlock->m_UniformType == gfx::ACCELERATION_STRUCTURE_UNIFORM)
                    {
                        m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, gfx::ShaderStageType::Domain, pBlock->m_nLayoutBindingVs);
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }
            }

        }

        for (auto it : m_vecUniformTexture)
        {
            gfx::KProgramUniformTexture* pTexture  = it;
            gfx::enumDescriptorType      eDescType = gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE;

            if (pTexture->m_eTextureType == TextureType::RWBuffer)
            {
                eDescType = gfx::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            }

            if (pTexture->m_eTextureType == TextureType::CombinedSamplerBuffer)
            {
                eDescType = gfx::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            }

            if (pTexture->m_eTextureType == TextureType::TextureImage2D ||
                pTexture->m_eTextureType == TextureType::TextureImage3D ||
                pTexture->m_eTextureType == TextureType::TextureImage2DArray)
            {
                eDescType = gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }

            // DESCRIPTOR_TYPE_STORAGE_IMAGE

            if (pTexture->m_nLayoutBindingVs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Vertex, pTexture->m_nLayoutBindingVs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingFs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Fragment, pTexture->m_nLayoutBindingFs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingCs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Compute, pTexture->m_nLayoutBindingCs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingGs >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Geometry, pTexture->m_nLayoutBindingGs, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingTc >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Hull, pTexture->m_nLayoutBindingTc, pTexture->m_uArrayCount);
            }

            if (pTexture->m_nLayoutBindingTe >= 0)
            {
                m_pLayout->AddLayout(0, eDescType, gfx::ShaderStageType::Domain, pTexture->m_nLayoutBindingTe, pTexture->m_uArrayCount);
            }
        }
        for (auto it : m_vecUniformSampler)
        {
            gfx::KProgramUniformSampler* pSampler = it;
            if (pSampler->m_nLayoutBindingVs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Vertex, pSampler->m_nLayoutBindingVs);
            }

            if (pSampler->m_nLayoutBindingFs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Fragment, pSampler->m_nLayoutBindingFs);
            }

            if (pSampler->m_nLayoutBindingCs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Compute, pSampler->m_nLayoutBindingCs);
            }

            if (pSampler->m_nLayoutBindingGs >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Geometry, pSampler->m_nLayoutBindingGs);
            }

            if (pSampler->m_nLayoutBindingTc >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Hull, pSampler->m_nLayoutBindingTc);
            }

            if (pSampler->m_nLayoutBindingTe >= 0)
            {
                m_pLayout->AddLayout(0, gfx::DESCRIPTOR_TYPE_SAMPLER, gfx::ShaderStageType::Domain, pSampler->m_nLayoutBindingTe);
            }
        }

        if (m_pPushConstantBlock && !m_pPushConstantBlock->m_vecPushConstantsRangeMap.empty())
        {
            // for (auto it : m_pPushConstantBlock->m_Uniforms)
            //{
            //   KProgramUniform* pUniform = it;
            //   m_pLayout->AddPushContantRange(pUniform->m_ShaderType, pUniform->m_uByteSize, pUniform->m_nOffset);
            // }

            for (auto& it : m_pPushConstantBlock->m_vecPushConstantsRangeMap)
            {
                const gfx::KPushContantsRangeMap& rangemap = it;
                m_pLayout->AddPushContantRange(rangemap.shadertype, rangemap.toRange - rangemap.offset, rangemap.offset);
            }
        }

        bRetCode = m_pLayout->End();
        KGLOG_PROCESS_ERROR(bRetCode);
    }
Exit1:
    bRet = true;
Exit0:
    return bRet;
}

uint32_t KShaderResourceVK::GetId()
{
    return m_id;
}

PipeLine::PipeLine()
{
    pPipeline                          = nullptr;
    pRenderPass                        = nullptr;
    pVertDescriptor                    = nullptr;
    processState                       = enumPipelineLoadState::PIPLE_PROCESSING;
    nFrameCountLastQuery               = 0;
    m_pSpeicalizationConstantContainer = nullptr;
    uSpecializationConstItemHash       = 0;
}

PipeLine::~PipeLine()
{
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    if (pPipeline)
    {
        pGraphicDevice->DestroyPipeline(pPipeline);
        pPipeline = nullptr;
    }
    // SAFE_RELEASE(pRenderPass);
    SAFE_DELETE(m_pSpeicalizationConstantContainer);
    //  if (pRenderPass)
    //  {
    //      pGraphicDevice->DestroyRenderPass(pRenderPass);
    //      pRenderPass = nullptr;
    //  }
}

void PipeLine::CreateSpecializationConstantContainer()
{
    if (!m_pSpeicalizationConstantContainer)
    {
        m_pSpeicalizationConstantContainer = new gfx::KSpecializationConstantContainer;
    }
}

gfx::IKSpecializationConstantContainer* PipeLine::GetSpecializationConstantContainer()
{
    return m_pSpeicalizationConstantContainer;
}

KShaderResourceVK::VertDescriptor::VertDescriptor()
{
    pVertDescriptor = nullptr;
}

KShaderResourceVK::VertDescriptor::~VertDescriptor()
{
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    pGraphicDevice->DestroyVertDescriptor(pVertDescriptor);
}


//void BuildSpecializationConstantContainer(gfx::IKSpecializationConstantContainer* pContainer, uint32_t uSpecializationConstantsMask)
//{
//    KDynamicMacroToSpecializationConstMask maskobj(uSpecializationConstantsMask);
//    maskobj.BuildSpecializationConstantContainer(pContainer);
//}

BOOL KShaderResourceVK::RequestPipeline(const gfx::KRenderState& sRenderstate, gfx::KVulkanRenderPass* pRenderPass, gfx::KVulkanVertexDescriptor* pVertDescriptor, gfx::KPipeline** ppPipeline, KEnumMtlTaskLevel uThreadLevel, gfx::KSpecializationConstItem* pItems, uint32_t uItemCount, uint64_t uSpecializationConstItemHash)
{
    BOOL      bRet         = false;
    BOOL      bRetCode     = false;
    PipeLine* pNewPipeline = nullptr;
    BOOL      bThread      = false;
    *ppPipeline            = nullptr;
    if (uThreadLevel != KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
    {
        bThread = true;
    }

    IKGFX_PipelineLoadThread* pLoadThread = NSEngine::GetMaterialSystem()->GetPipelineLoadThread();
    KGLOG_PROCESS_ERROR(m_pLayout && m_pLayout->IsReady());

    for (auto it : m_vecPipelineCache)
    {
        PipeLine* p = it;
        if (p->renderState == sRenderstate && p->pRenderPass == pRenderPass && p->pVertDescriptor == pVertDescriptor && p->uSpecializationConstItemHash == uSpecializationConstItemHash)
        {
            if (p->processState == enumPipelineLoadState::PIPLE_CREATE_SUCCESS)
            {
                *ppPipeline = p->pPipeline;
                goto Exit1;
            }
            else
            {
                *ppPipeline = nullptr;
                goto Exit0;
            }
        }
    }

    if (!bThread)
    {
        pNewPipeline                                       = new PipeLine;
        pNewPipeline->processState                         = enumPipelineLoadState::PIPLE_PROCESSING;
        pNewPipeline->renderState                          = sRenderstate;
        pNewPipeline->pVertDescriptor                      = pVertDescriptor;
        pNewPipeline->uSpecializationConstItemHash         = uSpecializationConstItemHash;
        gfx::IKSpecializationConstantContainer* pContainer = nullptr;

        if (DrvOption::bMacroToSpicalizationConstantsEnable)
        {
            pNewPipeline->CreateSpecializationConstantContainer();
            pContainer = pNewPipeline->GetSpecializationConstantContainer();
            // pBindFunc(pContainer);
            // BuildSpecializationConstantContainer(pContainer, uSpecializationConstantsId);

            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                for (uint32_t i = 0; i < uItemCount; ++i)
                {
                    const gfx::KSpecializationConstItem& item = pItems[i];
                    if (item.const_type == gfx::UINT_CONSTANT_TYPE)
                    {
                        pContainer->AddUInt(item.stage_id, item.uConstId, item.uValue);
                    }
                    else if (item.const_type == gfx::INT_CONSTANT_TYPE)
                    {
                        pContainer->AddInt(item.stage_id, item.uConstId, item.nValue);
                    }
                    else if (item.const_type == gfx::FLOAT_CONSTANT_TYPE)
                    {
                        pContainer->AddFloat(item.stage_id, item.uConstId, item.fValue);
                    }
                }
            }
        }


        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_PROCESS_ERROR(pGraphicDevice);

        pNewPipeline->pRenderPass = pRenderPass;

        gfx::GraphicsPipelineDesc graphicDesc;
        graphicDesc.pLayout           = m_pLayout;
        graphicDesc.pVertexDescriptor = pVertDescriptor;
        graphicDesc.pStage            = m_pShaderStage;
        graphicDesc.pRenderState      = &pNewPipeline->renderState;
        ASSERT(m_uStageCount);
        graphicDesc.uStageCount = m_uStageCount;
        graphicDesc.pRenderPass = pNewPipeline->pRenderPass;
        m_vecPipelineCache.push_back(pNewPipeline);

        // 同步
        bRetCode = pGraphicDevice->CreateGraphicsPipeline(&pNewPipeline->pPipeline, &graphicDesc, pContainer);
        if (bRetCode)
        {
            pNewPipeline->pPipeline->uCreatedRenderStateHash = pNewPipeline->renderState.GetHash();
            *ppPipeline                                      = pNewPipeline->pPipeline;
            pNewPipeline->processState                       = enumPipelineLoadState::PIPLE_CREATE_SUCCESS;
            goto Exit1;
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "create pipline failed");
            pNewPipeline->processState = enumPipelineLoadState::PIPLE_CREATE_FAILED;
            goto Exit0;
        }
    }
    else if (pLoadThread->IsAllowToPush() || uThreadLevel == KEnumMtlTaskLevel::HIGH_MTL_THREAD_LEVEL)
    {
        pNewPipeline                                       = new PipeLine;
        gfx::IKSpecializationConstantContainer* pContainer = nullptr;

        if (DrvOption::bMacroToSpicalizationConstantsEnable)
        {
            pNewPipeline->CreateSpecializationConstantContainer();
            pContainer = pNewPipeline->GetSpecializationConstantContainer();
            // pBindFunc(pContainer);
            // BuildSpecializationConstantContainer(pContainer, uSpecializationConstantsId);
            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                for (uint32_t i = 0; i < uItemCount; ++i)
                {
                    const gfx::KSpecializationConstItem& item = pItems[i];
                    if (item.const_type == gfx::UINT_CONSTANT_TYPE)
                    {
                        pContainer->AddUInt(item.stage_id, item.uConstId, item.uValue);
                    }
                    else if (item.const_type == gfx::INT_CONSTANT_TYPE)
                    {
                        pContainer->AddInt(item.stage_id, item.uConstId, item.nValue);
                    }
                    else if (item.const_type == gfx::FLOAT_CONSTANT_TYPE)
                    {
                        pContainer->AddFloat(item.stage_id, item.uConstId, item.fValue);
                    }
                }
            }
        }

        pNewPipeline->processState                 = enumPipelineLoadState::PIPLE_PROCESSING;
        pNewPipeline->renderState                  = sRenderstate;
        pNewPipeline->pVertDescriptor              = pVertDescriptor;
        pNewPipeline->uSpecializationConstItemHash = uSpecializationConstItemHash;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        pNewPipeline->pRenderPass           = pRenderPass;

        gfx::GraphicsPipelineDesc graphicDesc;
        graphicDesc.pLayout           = m_pLayout;
        graphicDesc.pVertexDescriptor = pVertDescriptor;
        graphicDesc.pStage            = m_pShaderStage;
        graphicDesc.pRenderState      = &pNewPipeline->renderState;
        ASSERT(m_uStageCount);
        graphicDesc.uStageCount = m_uStageCount;
        graphicDesc.pRenderPass = pNewPipeline->pRenderPass;
        m_vecPipelineCache.push_back(pNewPipeline);

        // 异步线程加载
        pLoadThread->PushLoadTask(this, pNewPipeline, &graphicDesc, uThreadLevel);
    }

Exit1:
    bRet = true;
Exit0:
    return bRet;
}

BOOL KShaderResourceVK::RequestPipeline(const gfx::KRenderState& sRenderstate, const gfx::KVulkanRenderPass* pExternalRenderPass, gfx::KVulkanVertexDescriptor* pVertDescriptor, gfx::KPipeline** ppPipeline, KEnumMtlTaskLevel uThreadLevel, gfx::KSpecializationConstItem* pItems, uint32_t uItemCount, uint64_t uSpecializationConstItemHash)
{
    PROF_CPU_DETAIL();
    ASSERT(IsMainThread());
    BOOL      bRet         = false;
    BOOL      bRetCode     = false;
    PipeLine* pNewPipeline = nullptr;
    BOOL      bThread      = false;

    int nCurFrameCount = NSEngine::GetRenderFrameMoveLoopCount();

    if (uThreadLevel != KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
    {
        bThread = true;
    }

    IKGFX_PipelineLoadThread* pLoadThread = NSEngine::GetMaterialSystem()->GetPipelineLoadThread();
    KGLOG_PROCESS_ERROR(pExternalRenderPass);


    {
        std::vector<PipeLine*> vecSwap;
        unsigned int           uSize = (unsigned int)m_vecPipelineCache_ExternalPass.size();
        for (unsigned int i = 0; i < uSize; i++)
        {
            PipeLine* p = m_vecPipelineCache_ExternalPass[i];
            if (p)
            {
                //if (p->pRenderPass->IsValid() == FALSE && p->processState != enumPipelineLoadState::PIPLE_PROCESSING)
                //{
                //    m_vecPipelineCache_ExternalPass[i] = nullptr;
                //    SAFE_DELETE(p);
                //}
                //else
                {
                    vecSwap.push_back(p);
                }
            }
            else
            {
                KGLOG_CHECK_ERROR(FALSE);
            }
        }
        m_vecPipelineCache_ExternalPass.swap(vecSwap);
    }


    for (auto it : m_vecPipelineCache_ExternalPass)
    {
        PipeLine* p = it;
        if (p->renderState == sRenderstate &&
            p->pRenderPass == pExternalRenderPass // 外部renderpass没有枚举类型，直接比较对象
            && p->pVertDescriptor == pVertDescriptor && p->uSpecializationConstItemHash == uSpecializationConstItemHash)
        {
            if (p->processState == enumPipelineLoadState::PIPLE_CREATE_SUCCESS)
            {
                *ppPipeline             = p->pPipeline;
                p->nFrameCountLastQuery = nCurFrameCount;
                goto Exit1;
            }
            else
            {
                *ppPipeline = nullptr;
                goto Exit0;
            }
        }
    }

    KASSERT(m_vecPipelineCache_ExternalPass.size() < 150); // 如果超过，应该怀疑renderpass是否在频繁创建新的
    if (!bThread)
    {
        pNewPipeline                               = new PipeLine;
        pNewPipeline->processState                 = enumPipelineLoadState::PIPLE_PROCESSING;
        pNewPipeline->renderState                  = sRenderstate;
        pNewPipeline->pVertDescriptor              = pVertDescriptor;
        pNewPipeline->nFrameCountLastQuery         = nCurFrameCount;
        pNewPipeline->uSpecializationConstItemHash = uSpecializationConstItemHash;

        gfx::IKSpecializationConstantContainer* pContainer = nullptr;

        if (DrvOption::bMacroToSpicalizationConstantsEnable)
        {
            pNewPipeline->CreateSpecializationConstantContainer();
            pContainer = pNewPipeline->GetSpecializationConstantContainer();
            // pBindFunc(pContainer);
            // BuildSpecializationConstantContainer(pContainer, uSpecializationConstantsId);

            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                for (uint32_t i = 0; i < uItemCount; ++i)
                {
                    const gfx::KSpecializationConstItem& item = pItems[i];
                    if (item.const_type == gfx::UINT_CONSTANT_TYPE)
                    {
                        pContainer->AddUInt(item.stage_id, item.uConstId, item.uValue);
                    }
                    else if (item.const_type == gfx::INT_CONSTANT_TYPE)
                    {
                        pContainer->AddInt(item.stage_id, item.uConstId, item.nValue);
                    }
                    else if (item.const_type == gfx::FLOAT_CONSTANT_TYPE)
                    {
                        pContainer->AddFloat(item.stage_id, item.uConstId, item.fValue);
                    }
                }
            }
        }

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_PROCESS_ERROR(pGraphicDevice);
        pNewPipeline->pRenderPass = const_cast<gfx::KVulkanRenderPass*>(pExternalRenderPass);
        // pNewPipeline->pRenderPass->AddRef();

        gfx::GraphicsPipelineDesc graphicDesc;
        graphicDesc.pLayout           = m_pLayout;
        graphicDesc.pVertexDescriptor = pVertDescriptor;
        graphicDesc.pStage            = m_pShaderStage;
        graphicDesc.pRenderState      = &pNewPipeline->renderState;
        ASSERT(m_uStageCount);
        graphicDesc.uStageCount = m_uStageCount;
        graphicDesc.pRenderPass = pNewPipeline->pRenderPass;
        m_vecPipelineCache_ExternalPass.push_back(pNewPipeline);

        // 同步
        bRetCode = pGraphicDevice->CreateGraphicsPipeline(&pNewPipeline->pPipeline, &graphicDesc, pContainer);
        if (bRetCode)
        {
            pNewPipeline->pPipeline->uCreatedRenderStateHash = pNewPipeline->renderState.GetHash();
            *ppPipeline                                      = pNewPipeline->pPipeline;
            pNewPipeline->processState                       = enumPipelineLoadState::PIPLE_CREATE_SUCCESS;
            goto Exit1;
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "create pipline failed");
            pNewPipeline->processState = enumPipelineLoadState::PIPLE_CREATE_FAILED;
            goto Exit0;
        }
    }
    else if (pLoadThread->IsAllowToPush() || uThreadLevel == KEnumMtlTaskLevel::HIGH_MTL_THREAD_LEVEL)
    {
        pNewPipeline = new PipeLine;

        gfx::IKSpecializationConstantContainer* pContainer = nullptr;

        if (DrvOption::bMacroToSpicalizationConstantsEnable)
        {
            pNewPipeline->CreateSpecializationConstantContainer();
            pContainer = pNewPipeline->GetSpecializationConstantContainer();
            // pBindFunc(pContainer);
            // BuildSpecializationConstantContainer(pContainer, uSpecializationConstantsId);
            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                for (uint32_t i = 0; i < uItemCount; ++i)
                {
                    const gfx::KSpecializationConstItem& item = pItems[i];
                    if (item.const_type == gfx::UINT_CONSTANT_TYPE)
                    {
                        pContainer->AddUInt(item.stage_id, item.uConstId, item.uValue);
                    }
                    else if (item.const_type == gfx::INT_CONSTANT_TYPE)
                    {
                        pContainer->AddInt(item.stage_id, item.uConstId, item.nValue);
                    }
                    else if (item.const_type == gfx::FLOAT_CONSTANT_TYPE)
                    {
                        pContainer->AddFloat(item.stage_id, item.uConstId, item.fValue);
                    }
                }
            }
        }

        pNewPipeline->processState                 = enumPipelineLoadState::PIPLE_PROCESSING;
        pNewPipeline->renderState                  = sRenderstate;
        pNewPipeline->pVertDescriptor              = pVertDescriptor;
        pNewPipeline->nFrameCountLastQuery         = nCurFrameCount;
        pNewPipeline->uSpecializationConstItemHash = uSpecializationConstItemHash;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_PROCESS_ERROR(pGraphicDevice);
        pNewPipeline->pRenderPass = const_cast<gfx::KVulkanRenderPass*>(pExternalRenderPass);
        // pNewPipeline->pRenderPass->AddRef();

        gfx::GraphicsPipelineDesc graphicDesc;
        graphicDesc.pLayout           = m_pLayout;
        graphicDesc.pVertexDescriptor = pVertDescriptor;
        graphicDesc.pStage            = m_pShaderStage;
        graphicDesc.pRenderState      = &pNewPipeline->renderState;
        ASSERT(m_uStageCount);
        graphicDesc.uStageCount = m_uStageCount;
        graphicDesc.pRenderPass = pNewPipeline->pRenderPass;

        m_vecPipelineCache_ExternalPass.push_back(pNewPipeline);

        // 异步线程加载
        pLoadThread->PushLoadTask(this, pNewPipeline, &graphicDesc, uThreadLevel);
    }

Exit1:
    bRet = true;
Exit0:
    return bRet;
}


void KPipelineLoadThread::EngineThreadCall()
{
    PROF_CPU();
    _Task task;

    int nCount = m_nMaxPushCountPerFrame;
    while (nCount-- > 0)
    {
        m_lck.lock();
        task.pShaderResource = nullptr;
        BOOL bEmpty = m_LoadTask.empty();
        if (!bEmpty)
        {
            task = m_LoadTask.top();
            m_LoadTask.pop();
        }
        m_lck.unlock();

        if (bEmpty)
        {
            return;
        }
        if (task.pShaderResource)
        {
            gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
            gfx::IKSpecializationConstantContainer* pSpecializationConstantContainer = task.pPipeLine->GetSpecializationConstantContainer();
            BOOL                                    bRetCode = pGraphicDevice->CreateGraphicsPipeline(&task.pPipeLine->pPipeline, &task.graphicDesc, pSpecializationConstantContainer);
            if (bRetCode)
            {
                task.pPipeLine->processState = enumPipelineLoadState::PIPLE_CREATE_SUCCESS;
            }
            else
            {
                task.pPipeLine->processState = enumPipelineLoadState::PIPLE_CREATE_FAILED;
            }
            task.pShaderResource->Release();
        }
    }
}

KPipelineLoadThread::KPipelineLoadThread()
{
}

KPipelineLoadThread::~KPipelineLoadThread()
{
    m_lck.lock();
    while (!m_LoadTask.empty())
    {
        _Task task = m_LoadTask.top();
        m_LoadTask.pop();
        SAFE_RELEASE(task.pShaderResource);
    }
    m_lck.unlock();
    FrameMove();
}

BOOL KPipelineLoadThread::IsAllowToPush()
{
    // BOOL bAllow = m_nMaxPushCountPerFrame > 0 ? true : false;
    // return bAllow;

    // 没必要限制push数量，work线程每帧有数量限制，task已经按优先级处理
    return TRUE;
}

BOOL KPipelineLoadThread::PushLoadTask(IKShaderResource* pShaderResource, IKPipeLineData* pPipeLine, IKGraphicsPipelineDesc* pDesc, KEnumMtlTaskLevel uThreadLevel)
{
    static uint32_t TASK_ID = 0;
    m_nMaxPushCountPerFrame--;
    m_lck.lock();
    ((KShaderResourceVK*)pShaderResource)->AddRef();
    _Task task = { (KShaderResourceVK *)pShaderResource, (PipeLine *)pPipeLine, *(GraphicsPipelineDesc *)pDesc, (int)uThreadLevel, TASK_ID++ };
    ((PipeLine*)pPipeLine)->processState = enumPipelineLoadState::PIPLE_PROCESSING;

    m_LoadTask.push(task);

    m_lck.unlock();
    // m_pThread->Notify();
    return true;
}

void KPipelineLoadThread::FrameMove()
{
    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    // 只是读一下，对不对无所谓
    pPerfMonitor->nPipelineLoadTaskCount = (int32_t)m_LoadTask.size();

    m_nMaxPushCountPerFrame = 5;
}
