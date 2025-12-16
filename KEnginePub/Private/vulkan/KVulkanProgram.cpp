#include "KVulkanProgram.h"
#include "KVulkanPrivate.h"
#include "KGraphicDevice.h"
#include "KEnginePub/Private/vulkan/KShaderResourcePoolVK.h"
#include "KEnginePub/Private/vulkan/KShaderResourceVK.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KVulkanBuffer.h"
#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanRenderContext.h"
#include "KMaterialSystem/Public/IKMaterialSystem.h"
#include "../comm/KGFX_ShaderHelper.h"
//////////////////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    KGFX_ProgramBinderVK::KGFX_ProgramBinderVK()
    {
        m_uSceneRenderID = 0;
        m_pMtlBuffer = nullptr;
        m_pMtlCPUBuffer = nullptr;
        m_uMtlCPUBufferSize = 0;
        m_bMtlCPUBufferValueChanged = true;
    }

    KGFX_ProgramBinderVK::~KGFX_ProgramBinderVK()
    {
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        for (auto it : m_mapDescriptorSet)
        {
            ASSERT(it.second);
            if (it.second)
            {
                pGraphicDevice->DestroyDescriptorSet(it.second);
            }
        }
        m_mapDescriptorSet.clear();

        SAFE_RELEASE(m_pShaderResource);
        SAFE_DELETE_ARRAY(m_pMtlCPUBuffer);
        SAFE_RELEASE(m_pMtlBuffer);
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::SetImmutableConstValueInt(const_pool_str pcszName, int32_t value)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        ASSERT(m_pShaderResource);

        for (const auto& it : m_pShaderResource->m_vecSpecializationConstDefine)
        {
            const KSpecializationConstDefine& specializationConstDef = it;
            if (pcszName == specializationConstDef.pName)
            {
                KSpecializationConstItem item;
                item.pName = pcszName;
                item.stage_id = specializationConstDef.uStageType;
                item.uConstId = specializationConstDef.uConstId;
                item.const_type = INT_CONSTANT_TYPE;
                memcpy(&item.uValue, &value, sizeof(int32_t));
                m_vecBindSpecializationConstItem.emplace_back(item);
            }
        }
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::SetImmutableConstValueUInt(const_pool_str pcszName, uint32_t value)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        ASSERT(m_pShaderResource);

        for (const auto& it : m_pShaderResource->m_vecSpecializationConstDefine)
        {
            const KSpecializationConstDefine& specializationConstDef = it;
            if (pcszName == specializationConstDef.pName)
            {
                KSpecializationConstItem item;
                item.pName = pcszName;
                item.stage_id = specializationConstDef.uStageType;
                item.uConstId = specializationConstDef.uConstId;
                item.const_type = UINT_CONSTANT_TYPE;
                memcpy(&item.uValue, &value, sizeof(uint32_t));
                m_vecBindSpecializationConstItem.emplace_back(item);
            }
        }
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::SetImmutableConstValueFloat(const_pool_str pcszName, float value)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        ASSERT(m_pShaderResource);

        for (const auto& it : m_pShaderResource->m_vecSpecializationConstDefine)
        {
            const KSpecializationConstDefine& specializationConstDef = it;
            if (pcszName == specializationConstDef.pName)
            {
                KSpecializationConstItem item;
                item.pName = pcszName;
                item.stage_id = specializationConstDef.uStageType;
                item.uConstId = specializationConstDef.uConstId;
                item.const_type = FLOAT_CONSTANT_TYPE;
                memcpy(&item.uValue, &value, sizeof(float));
                m_vecBindSpecializationConstItem.emplace_back(item);
            }
        }
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder)
    {
        PROF_CPU_DETAIL();
        if (!m_pMtlCPUBuffer && m_pShaderResource && m_pShaderResource->HasPerMtlUBO())
        {
            static_const_param_name mtlUboName0 = GetParamNameByPool(PER_MTL_UBO_NAME_0);
            static_const_param_name mtlUboName1 = GetParamNameByPool(PER_MTL_UBO_NAME_1);
            for (auto it : m_pShaderResource->m_vecUniformBlock)
            {
                gfx::KProgramUniformBlock* pBlock = it;
                if (pBlock->m_UniformType == UBO_UNIFORM && (pBlock->m_szName == mtlUboName0 || pBlock->m_szName == mtlUboName1))
                {
                    m_uMtlCPUBufferSize = pBlock->m_block16bytesAlignMemoryForGpu;
                    m_pMtlCPUBuffer = new uint8_t[pBlock->m_block16bytesAlignMemoryForGpu];
                    memset(m_pMtlCPUBuffer, 0, pBlock->m_block16bytesAlignMemoryForGpu);
                    break;
                }
            }
        }

        ASSERT(!m_bBinding);
        m_bBinding = true;
        m_bBindError = false;
        //m_setBindedTextureRegisterName.clear();
        for(auto &it : m_mapBindedTextureRegisterName)
        {
            it.second = false;
        }
        m_vecBindItem.clear();
        m_vecBindSpecializationConstItem.clear();
        m_vecBindSamplerState.clear();
        ClearTextureArrayBindingSlots();
        if (pRenderCommanParam)
        {
            m_pGlobaUBO = pRenderCommanParam->pGlobalUBO;
            m_uSceneRenderID = pRenderCommanParam->uSceneRenderID;
            m_pSharedPreBinder = (const gfx::KSharedPreBinder*)pShareBinder;
        }
        else
        {
            m_pGlobaUBO = nullptr;
            // throw std::logic_error("Fixme GraphicContext issue");
            m_uSceneRenderID = 0;
            m_pSharedPreBinder = nullptr;
        }
        return *this;
    }

    BOOL KGFX_ProgramBinderVK::EndBind()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;


        bool                    bNewDescriptorSet = false;
        int32_t                 nCurrentFrameId = NSEngine::GetRenderFrameMoveLoopCount();
        IKTexturePool*          pTexturePool = NSEngine::GetTexturePool();
        static_const_param_name mtlUboName0 = GetParamNameByPool(PER_MTL_UBO_NAME_0);
        static_const_param_name mtlUboName1 = GetParamNameByPool(PER_MTL_UBO_NAME_1);

        KGLOG_ASSERT_EXIT(m_pShaderResource);
        KGLOG_ASSERT_EXIT(m_bBinding);
        KG_PROCESS_ERROR(m_bBindError == FALSE);

        bRetCode = m_pShaderResource->SetupLayout(this);
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = ComputeBindCode();
        KG_PROCESS_ERROR(bRetCode);

        if (m_pSharedPreBinder)
        {
            m_uBindCheckCode += m_pSharedPreBinder->GetHash();
        }

        KGLOG_PROCESS_ERROR(m_uBindCheckCode);

        {
            m_pDescriptorSet = nullptr;
            // 首先尝试看有没有checkcode一致的descriptorset，有就直接用，避免重复创建
            // for (auto it : m_mapDescriptorSet)
            //{
            //	gfx::KVulkanDescriptorSet* pDescriptorSet = it.second;
            //	if (pDescriptorSet && pDescriptorSet->m_uProgramCheckCode == m_uBindCheckCode)
            //	{
            //		m_pDescriptorSet = pDescriptorSet;
            //		m_pDescriptorSet->m_uLastUseFrameId = nCurrentFrameId;
            //		break;
            //	}
            // }

            // 多帧不用的给清除掉，省点内存
            for (auto it = m_mapDescriptorSet.begin(), e = m_mapDescriptorSet.end(); it != e;)
            {
                gfx::KVulkanDescriptorSet* pDescriptorSet = it->second;
                if (pDescriptorSet && nCurrentFrameId - pDescriptorSet->m_uLastUseFrameId > DELAY_RELEASE_FRAME_COUNT)
                {
                    gfx::GetGraphicDevice()->DestroyDescriptorSet(pDescriptorSet);
                    it = m_mapDescriptorSet.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            auto itFind = m_mapDescriptorSet.find(m_uBindCheckCode);
            if (itFind != m_mapDescriptorSet.end())
            {
                m_pDescriptorSet = itFind->second;
                m_pDescriptorSet->m_uLastUseFrameId = nCurrentFrameId;
                m_pDescriptorSet->ValidCheck();
            }
            else
            {
                m_pDescriptorSet = nullptr;
                gfx::KVulkanLayout* pLayout = m_pShaderResource->GetLayout();
                gfx::KDescriptorPoolContainer* pDescriptorPoolContainer = m_pShaderResource->GetDescriptorPoolContainer();
                bRetCode = gfx::GetGraphicDevice()->CreateDescriptorSet(&m_pDescriptorSet, pLayout, pDescriptorPoolContainer);
                KGLOG_ASSERT_EXIT(m_pDescriptorSet);
                m_mapDescriptorSet[m_uBindCheckCode] = m_pDescriptorSet;
                m_pDescriptorSet->m_uLastUseFrameId = nCurrentFrameId;
                bNewDescriptorSet = true;
            }

            // 多帧不用的给清除掉，省点内存
        }
        KGLOG_ASSERT_EXIT(m_pDescriptorSet);

        if (m_pDescriptorSet->m_uProgramCheckCode != m_uBindCheckCode || bNewDescriptorSet)
        {
            m_vecBindedUniformBlock.clear();
            m_vecBindedTexture.clear();

            // 绑定资源检查
            ///////////////////////////////
            for (auto it : m_pShaderResource->m_vecUniformSampler)
            {
                KProgramUniformSampler* pUniSampler = it;
                if (!pUniSampler /* || !pUniSampler->m_pSampler*/)
                {
                    KGLogPrintf(KGLOG_ERR, "%s", "KProgramUniformSampler  is nullptr");
                    goto Exit0;
                }
                BOOL bFind = false;
                if (pUniSampler->m_pSampler)
                {
                    bFind = true;
                }
                for (auto& itt : m_vecBindSamplerState)
                {
                    KSamplerItem& samplerItem = itt.second;
                    if (pUniSampler->m_szName == samplerItem.pName)
                    {
                        bFind = true;
                    }
                }


                if (!bFind && m_pSharedPreBinder)
                {
                    auto it = m_pSharedPreBinder->m_mapPreBindSampler.find(pUniSampler->m_szName);
                    if (it != m_pSharedPreBinder->m_mapPreBindSampler.end())
                    {
                        bFind = true;
                    }
                }
                if (!bFind)
                {
                    const char* pVSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Vertex);
                    const char* pFSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Fragment);
                    const char* pCSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Compute);
                    if (pVSName && pVSName[0] && pFSName && pFSName[0])
                    {
                        KGLogPrintf(KGLOG_ERR, "%s或%s[%s]没有被绑定", pVSName, pFSName, it->m_szName);
                    }
                    else if (pCSName && pCSName[0])
                    {
                        KGLogPrintf(KGLOG_ERR, "%s[%s]没有被绑定", pCSName, it->m_szName);
                    }
                    ASSERT(0);
                    goto Exit0;
                }
            }

            for (auto it : m_pShaderResource->m_vecUniformBlock)
            {
                KProgramUniformBlock* pBlock = it;
                BOOL                  bFind = false;
                for (const auto& itt : m_vecBindItem)
                {
                    const BindItem& item = itt;
                    // 外部绑定
                    if (pBlock->m_szName == item.pName)
                    {
                        switch (item.bindtype)
                        {
                        case BindType::SHARE_UBO_BIND_TYPE:
                        {
                            if (!item.shareUBO.pUBO)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.shareUBO.pUBO %s is nullptr", item.pName);
                                ASSERT(0);
                                goto Exit0;
                            }
                            if (pBlock->m_UniformType != UBO_UNIFORM)
                            {
                                KGLogPrintf(KGLOG_ERR, "%s 绑定类型应该是 %s ", item.pName, g_unifromTypeName[pBlock->m_UniformType]);
                                ASSERT(0);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::UBO_BIND_TYPE:
                        {
                            if (!item.ubo.pUBO)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.ubo.pUBO %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            if (pBlock->m_UniformType != UBO_UNIFORM)
                            {
                                KGLogPrintf(KGLOG_ERR, "%s 绑定类型应该是 %s ", item.pName, g_unifromTypeName[pBlock->m_UniformType]);
                                ASSERT(0);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::SSBO_BIND_TYPE:
                        {
                            if (!item.ssbo.pSSBO)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.ssbo.pSSBO %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            if (pBlock->m_UniformType != SSBO_UNIFORM)
                            {
                                KGLogPrintf(KGLOG_ERR, "%s 绑定类型应该是 %s ", item.pName, g_unifromTypeName[pBlock->m_UniformType]);
                                ASSERT(0);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::ACCELERATION_STRUCTURE_TYPE:
                        {
                            if (!item.aso.pAS)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.aso.pAS %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            if (pBlock->m_UniformType != ACCELERATION_STRUCTURE_UNIFORM)
                            {
                                KGLogPrintf(KGLOG_ERR, "%s 绑定类型应该是 %s ", item.pName, g_unifromTypeName[pBlock->m_UniformType]);
                                ASSERT(0);
                                goto Exit0;
                            }
                            break;
                        }
                        }
                        bFind = true;
                        break;
                    }

                    // 材质ubo内部绑定
                    if (pBlock->m_UniformType == UBO_UNIFORM && (pBlock->m_szName == mtlUboName0 || pBlock->m_szName == mtlUboName1))
                    {
                        bFind = true;
                        break;
                    }
                }

                // 如果外部没有绑定，那么搜索全局绑定
                //if (!bFind && pBlock->m_UniformScopeType == GLOBAL_STANDARD_UBO)
                //{
                //    gfx::KEnumGlobalUBOVK      globalUboId;
                //    gfx::KEnumUniformScopeType uBOType;
                //    KGlobalUBOBase*            pUbo       = nullptr;
                //    ASSERT(m_pGlobaUBO);
                //    m_pGlobaUBO->GetGlobalUBOByName(pBlock->m_szName, &pUbo, uBOType, globalUboId);
                //    if (!pUbo)
                //    {
                //        KGLogPrintf(KGLOG_ERR, "global ubo  %s is nullptr", pBlock->m_szName);
                //        goto Exit0;
                //    }
                //    bFind = true;
                //}

                if (!bFind && m_pSharedPreBinder)
                {
                    //auto it = m_pSharedPreBinder->m_mapPreBindBuffer.find(pBlock->m_szName);
                    //if (it != m_pSharedPreBinder->m_mapPreBindBuffer.end())
                    //{
                    //    bFind = true;
                    //}

                    if (!bFind)
                    {
                        auto it = m_pSharedPreBinder->m_mapPreBindBufferResourceView.find(pBlock->m_szName);
                        if (it != m_pSharedPreBinder->m_mapPreBindBufferResourceView.end())
                        {
                            bFind = true;
                        }
                    }
                }

                if (!bFind)
                {
                    const char* pVSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Vertex);
                    const char* pFSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Fragment);
                    const char* pCSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Compute);
                    if (pVSName && pVSName[0] && pFSName && pFSName[0])
                    {
                        KGLogPrintf(KGLOG_ERR, "%s或%s[%s]没有被绑定", pVSName, pFSName, it->m_szName);
                    }
                    else if (pCSName && pCSName[0])
                    {
                        KGLogPrintf(KGLOG_ERR, "%s[%s]没有被绑定", pCSName, it->m_szName);
                    }
                    ASSERT(0);
                    goto Exit0;
                }
            }

            for (auto it : m_pShaderResource->m_vecUniformTexture)
            {
                KProgramUniformTexture* pBlock = it;
                BOOL                    bFind = false;
                for (auto& itt : m_vecBindItem)
                {
                    BindItem& item = itt;
                    if (pBlock->m_szName == item.pName)
                    {
                        bFind = true;
                        switch (item.bindtype)
                        {
                        case BindType::RWBUFFERVIEW_TYPE:
                        {
                            if (!item.rw_buffer_view.pBufView)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.rw_buffer_view.pBufView %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::SAMPLEBUFFERVIEW_TYPE:
                        {
                            if (!item.sample_buffer_view.pBufView)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.sample_buffer_view.pBufView  %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::TEXTURE_SRV_TYPE:
                        {
                            if (!item.texture_srv.pSRV)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.texture_srv.pSRV  %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::TEXTURE_UAV_TYPE:
                        {
                            if (!item.texture_uav.pUAV)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.texture_uav.pUAV  %s is nullptr", item.pName);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::TEXTURE_UAV_ARRAY_TYPE:
                        {
                            if (item.texture_uav_array.uNum == 0)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.texture_uav_array.uNum  %s is 0", item.pName);
                                goto Exit0;
                            }
                            break;
                        }
                        case BindType::TEXTURE_SRV_ARRAY_TYPE:
                        {
                            if (item.texture_srv_array.uNum == 0)
                            {
                                KGLogPrintf(KGLOG_ERR, "item.texture_srv_array.uNum  %s is 0", item.pName);
                                goto Exit0;
                            }
                            break;
                        }
                        }
                    }
                }

                if (!bFind && m_pSharedPreBinder)
                {
                    auto it = m_pSharedPreBinder->m_mapPreBindTexture.find(pBlock->m_szName);
                    if (it != m_pSharedPreBinder->m_mapPreBindTexture.end())
                    {
                        bFind = true;
                    }

                    if (!bFind)
                    {
                        auto it = m_pSharedPreBinder->m_mapPreBindTextures.find(pBlock->m_szName);
                        if (it != m_pSharedPreBinder->m_mapPreBindTextures.end())
                        {
                            bFind = true;
                        }
                    }

                    if (!bFind)
                    {
                        auto it = m_pSharedPreBinder->m_mapPreBindBufferResourceView.find(pBlock->m_szName);
                        if (it != m_pSharedPreBinder->m_mapPreBindBufferResourceView.end())
                        {
                            bFind = true;
                        }
                    }
                }

                if (!bFind)
                {
                    const char* pVSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Vertex);
                    const char* pFSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Fragment);
                    const char* pCSName = m_pShaderResource->GetShaderFileName(ShaderStageType::Compute);
                    if (pVSName && pVSName[0] && pFSName && pFSName[0])
                    {
                        KGLogPrintf(KGLOG_ERR, "%s或%s[%s]没有被绑定", pVSName, pFSName, it->m_szName);
                    }
                    else if (pCSName && pCSName[0])
                    {
                        KGLogPrintf(KGLOG_ERR, "%s[%s]没有被绑定", pCSName, it->m_szName);
                    }

                    // 如果没有绑定贴图，那么赋予错误贴图
                    BindItem item;
                    item.pName = pBlock->m_szName;
                    item.bindtype = BindType::TEXTURE_SRV_TYPE;
                    if (pBlock->m_eTextureType == TextureType::Cubemap)
                    {
                        item.texture_srv.pSRV = pTexturePool->GetErrorTextureCube()->GetSRV();
                    }
                    else if (pBlock->m_eTextureType == TextureType::Texture2DArray)
                    {
                        item.texture_srv.pSRV = pTexturePool->GetErrorTextureArray()->GetSRV();
                    }
                    else
                    {
                        item.texture_srv.pSRV = pTexturePool->GetErrorTexture()->GetSRV();
                    }
                    // m_uBindCheckCode += ((uint64_t)pBlock->m_szName ^ (uint64_t) item.texture.pTexture);
                    m_vecBindItem.push_back(item);
                }
            }

            //////////////////////////////end 资源检查,都检查过了，就开始绑定，下面赋值就相当于锁定，后面的流程不会跳出descriptorset的begin和end,所以前面的资源检查流程就非常关键了，有任何问题在这之前跳出
#ifdef _DEBUG
            m_pDescriptorSet->m_uPreviousCheckCode = m_pDescriptorSet->m_uProgramCheckCode;
#endif
            m_pDescriptorSet->m_uProgramCheckCode = m_uBindCheckCode;


            gfx::KVulkanLayout* pLayout = m_pShaderResource->GetLayout();
            m_pDescriptorSet->Begin();

            // sampler通过反射创建的，不需要外面再去设置绑定了，将不再对外开放了
            for (auto it : m_pShaderResource->m_vecUniformSampler)
            {
                KProgramUniformSampler* pUniSampler = it;
                BOOL                    bFind = false;
                gfx::IKGFX_Sampler* pSampler = pUniSampler->m_pSampler;

                if (m_pSharedPreBinder)
                {
                    auto it = m_pSharedPreBinder->m_mapPreBindSampler.find(pUniSampler->m_szName);
                    if (it != m_pSharedPreBinder->m_mapPreBindSampler.end())
                    {
                        pSampler = it->second;
                        pUniSampler->m_SamplerState.bNeedShaderInit = false;
                        bFind = true;
                    }
                }

                if (!bFind)
                {
                    auto it = m_vecBindSamplerState.find(pUniSampler->m_szName);
                    if (it != m_vecBindSamplerState.end())
                    {
                        pSampler = it->second.pSampler;
                        pUniSampler->m_SamplerState.bNeedShaderInit = false;
                        bFind = true;
                    }
                }

                ASSERT(!pUniSampler->m_SamplerState.bNeedShaderInit);

                if (pUniSampler->m_nLayoutBindingVs >= 0)
                {
                    m_pDescriptorSet->AddBindSampler(0, (uint32_t)pUniSampler->m_nLayoutBindingVs, 1, &pSampler);
                }

                if (pUniSampler->m_nLayoutBindingFs >= 0)
                {
                    m_pDescriptorSet->AddBindSampler(0, (uint32_t)pUniSampler->m_nLayoutBindingFs, 1, &pSampler);
                }

                if (pUniSampler->m_nLayoutBindingCs >= 0)
                {
                    m_pDescriptorSet->AddBindSampler(0, (uint32_t)pUniSampler->m_nLayoutBindingCs, 1, &pSampler);
                }

                if (pUniSampler->m_nLayoutBindingGs >= 0)
                {
                    m_pDescriptorSet->AddBindSampler(0, (uint32_t)pUniSampler->m_nLayoutBindingGs, 1, &pSampler);
                }

                if (pUniSampler->m_nLayoutBindingTc >= 0)
                {
                    m_pDescriptorSet->AddBindSampler(0, (uint32_t)pUniSampler->m_nLayoutBindingTc, 1, &pSampler);
                }

                if (pUniSampler->m_nLayoutBindingTe >= 0)
                {
                    m_pDescriptorSet->AddBindSampler(0, (uint32_t)pUniSampler->m_nLayoutBindingTe, 1, &pSampler);
                }
            }

            for (auto it : m_pShaderResource->m_vecUniformBlock)
            {
                KProgramUniformBlock* pBlock = it;
                BOOL                  bFind = false;
                for (auto& itt : m_vecBindItem)
                {
                    BindItem& item = itt;
                    if (pBlock->m_szName == item.pName)
                    {
                        bFind = true;
                        switch (item.bindtype)
                        {
                        case BindType::ACCELERATION_STRUCTURE_TYPE:
                        {
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {    
                                m_pDescriptorSet->AddBindAccelerationStructure(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.aso.pAS);
                            }

                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                m_pDescriptorSet->AddBindAccelerationStructure(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.aso.pAS);
                            }

                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                m_pDescriptorSet->AddBindAccelerationStructure(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.aso.pAS);
                            }

                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                m_pDescriptorSet->AddBindAccelerationStructure(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.aso.pAS);
                            }

                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                m_pDescriptorSet->AddBindAccelerationStructure(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.aso.pAS);
                            }

                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                m_pDescriptorSet->AddBindAccelerationStructure(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.aso.pAS);
                            }
                            break;
                        }
                        case BindType::SHARE_UBO_BIND_TYPE:
                        {
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {
                                ASSERT(!pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingVs));
                                m_pDescriptorSet->AddBindShareUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.shareUBO.pUBO, item.shareUBO.uSize, item.shareUBO.uOffset, pBlock->m_szName);
                            }

                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                ASSERT(!pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingFs));
                                m_pDescriptorSet->AddBindShareUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.shareUBO.pUBO, item.shareUBO.uSize, item.shareUBO.uOffset, pBlock->m_szName);
                            }

                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                ASSERT(!pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingCs));
                                m_pDescriptorSet->AddBindShareUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.shareUBO.pUBO, item.shareUBO.uSize, item.shareUBO.uOffset, pBlock->m_szName);
                            }

                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                ASSERT(!pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingGs));
                                m_pDescriptorSet->AddBindShareUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.shareUBO.pUBO, item.shareUBO.uSize, item.shareUBO.uOffset, pBlock->m_szName);
                            }

                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                ASSERT(!pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTc));
                                m_pDescriptorSet->AddBindShareUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.shareUBO.pUBO, item.shareUBO.uSize, item.shareUBO.uOffset, pBlock->m_szName);
                            }

                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                ASSERT(!pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTe));
                                m_pDescriptorSet->AddBindShareUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.shareUBO.pUBO, item.shareUBO.uSize, item.shareUBO.uOffset, pBlock->m_szName);
                            }

                            break;
                        }
                        case BindType::UBO_BIND_TYPE:
                        {
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingVs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingFs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingCs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingGs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTc))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTe))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, (IKGFX_Buffer**)&(item.ubo.pUBO), pBlock->m_szName);
                                }
                            }

                            break;
                        }
                        case BindType::SSBO_BIND_TYPE:
                        {
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingVs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingFs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingCs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingGs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTc))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTe))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.ssbo.pSSBO, item.ssbo.uByteOffset, item.ssbo.uByteSize, item.ssbo.bUAV, pBlock->m_szName);
                                }
                            }

                            break;
                        }
                        break;
                        }
                    }
                }

                if (!bFind && m_pSharedPreBinder)
                {
                    auto it = m_pSharedPreBinder->m_mapPreBindBufferResourceView.find(pBlock->m_szName);
                    if (it != m_pSharedPreBinder->m_mapPreBindBufferResourceView.end())
                    {
                        gfx::IKGFX_Buffer* pBuffer = it->second->GetResource();
                        BOOL bUAV = false;
                        if (it->second->GetViewDesc()->eViewType == gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV)
                        {
                            bUAV = true;
                        }
                        if (pBlock->m_UniformType == UBO_UNIFORM)
                        {
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingVs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &pBuffer, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &pBuffer, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingFs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &pBuffer, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &pBuffer, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingCs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &pBuffer, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &pBuffer, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingGs))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &pBuffer, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &pBuffer, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTc))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &pBuffer, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &pBuffer, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTe))
                                {
                                    m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &pBuffer, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &pBuffer, pBlock->m_szName);
                                }
                            }
                        }
                        else if (pBlock->m_UniformType == SSBO_UNIFORM)
                        {
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingVs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingFs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingCs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingGs))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTc))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                }
                            }

                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTe))
                                {
                                    m_pDescriptorSet->AddBindDynamicSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                    m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                                }
                                else
                                {
                                    m_pDescriptorSet->AddBindSSBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, pBuffer, 0, 0, bUAV, pBlock->m_szName);
                                }
                            }
                        }
                        else
                        {
                            ASSERT(0);
                        }
                        bFind = true;
                    }
                }

                if (!bFind)
                {
                    // 材质ubo自动创建
                    if (pBlock->m_UniformType == UBO_UNIFORM && (pBlock->m_szName == mtlUboName0 || pBlock->m_szName == mtlUboName1))
                    {
                        if (!m_pMtlBuffer)
                        {
                            KGraphicDevice* pDevice = GetGraphicDevice();
                            // if (!m_pMtlCPUBuffer)
                            //{
                            //	m_uMtlCPUBufferSize = pBlock->m_block16bytesAlignMemoryForGpu;
                            //	m_pMtlCPUBuffer = new uint8_t[pBlock->m_block16bytesAlignMemoryForGpu];
                            //	memset(m_pMtlCPUBuffer, 0, pBlock->m_block16bytesAlignMemoryForGpu);
                            // }
                            gfx::KGfxBufferDesc bufDesc;
                            bufDesc.eResAccessFlags = gfx::KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
                            bufDesc.uByteWidth = pBlock->m_block16bytesAlignMemoryForGpu;
                            bufDesc.uStructureByteStride = 0;
                            bufDesc.uUsageFlags = gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                            bRetCode = pDevice->CreateBuffer(&m_pMtlBuffer, bufDesc, nullptr);
                            KGLOG_PROCESS_ERROR(bRetCode);
                            m_pMtlBuffer->SetDebugName(pBlock->m_szName);
                        }

                        if (pBlock->m_nLayoutBindingVs >= 0)
                        {
                            if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingVs))
                            {
                                m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &m_pMtlBuffer, pBlock->m_szName);
                                m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                            }
                            else
                            {
                                m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &m_pMtlBuffer, pBlock->m_szName);
                            }
                        }

                        if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                        {
                            if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingFs))
                            {
                                m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &m_pMtlBuffer, pBlock->m_szName);
                                m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                            }
                            else
                            {
                                m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &m_pMtlBuffer, pBlock->m_szName);
                            }
                        }

                        if (pBlock->m_nLayoutBindingCs >= 0)
                        {
                            if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingCs))
                            {
                                m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &m_pMtlBuffer, pBlock->m_szName);
                                m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                            }
                            else
                            {
                                m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &m_pMtlBuffer, pBlock->m_szName);
                            }
                        }

                        if (pBlock->m_nLayoutBindingGs >= 0)
                        {
                            if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingGs))
                            {
                                m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &m_pMtlBuffer, pBlock->m_szName);
                                m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                            }
                            else
                            {
                                m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &m_pMtlBuffer, pBlock->m_szName);
                            }
                        }

                        if (pBlock->m_nLayoutBindingTc >= 0)
                        {
                            if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTc))
                            {
                                m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &m_pMtlBuffer, pBlock->m_szName);
                                m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                            }
                            else
                            {
                                m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &m_pMtlBuffer, pBlock->m_szName);
                            }
                        }

                        if (pBlock->m_nLayoutBindingTe >= 0)
                        {
                            if (pLayout->IsDynamic(0, (uint32_t)pBlock->m_nLayoutBindingTe))
                            {
                                m_pDescriptorSet->AddBindDynamicUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &m_pMtlBuffer, pBlock->m_szName);
                                m_pDescriptorSet->AddBindDynamicUBOIdToOffsetArray(0);
                            }
                            else
                            {
                                m_pDescriptorSet->AddBindUBO(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &m_pMtlBuffer, pBlock->m_szName);
                            }
                        }

                        bFind = true;
                    }
                }

                
                ASSERT(bFind);
            }

            for (auto it : m_pShaderResource->m_vecUniformTexture)
            {
                BOOL                    bFind = false;
                KProgramUniformTexture* pBlock = it;

                if (!bFind && m_pSharedPreBinder)
                {
                    auto it = m_pSharedPreBinder->m_mapPreBindTexture.find(pBlock->m_szName);
                    if (it != m_pSharedPreBinder->m_mapPreBindTexture.end())
                    {
                        if (pBlock->m_nLayoutBindingVs >= 0)
                        {
                            m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingVs, it->second);
                        }
                        if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                        {
                            m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingFs, it->second);
                        }
                        if (pBlock->m_nLayoutBindingCs >= 0)
                        {
                            m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingCs, it->second);
                        }
                        if (pBlock->m_nLayoutBindingGs >= 0)
                        {
                            m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingGs, it->second);
                        }
                        if (pBlock->m_nLayoutBindingTc >= 0)
                        {
                            m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingTc, it->second);
                        }
                        if (pBlock->m_nLayoutBindingTe >= 0)
                        {
                            m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingTe, it->second);
                        }
                        bFind = true;
                    }

                    if (!bFind)
                    {
                        auto it = m_pSharedPreBinder->m_mapPreBindTextures.find(pBlock->m_szName);
                        if (it != m_pSharedPreBinder->m_mapPreBindTextures.end())
                        {
                            const KBindingTextures* pTexs = &it->second;
                            if (pBlock->m_nLayoutBindingVs >= 0)
                            {
                                m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingVs, (uint32_t)pTexs->vecTextures.size(), pTexs->vecTextures.data());
                            }
                            if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                            {
                                m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingFs, (uint32_t)pTexs->vecTextures.size(), pTexs->vecTextures.data());
                            }
                            if (pBlock->m_nLayoutBindingCs >= 0)
                            {
                                m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingCs, (uint32_t)pTexs->vecTextures.size(), pTexs->vecTextures.data());
                            }
                            if (pBlock->m_nLayoutBindingGs >= 0)
                            {
                                m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingGs, (uint32_t)pTexs->vecTextures.size(), pTexs->vecTextures.data());
                            }
                            if (pBlock->m_nLayoutBindingTc >= 0)
                            {
                                m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingTc, (uint32_t)pTexs->vecTextures.size(), pTexs->vecTextures.data());
                            }
                            if (pBlock->m_nLayoutBindingTe >= 0)
                            {
                                m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingTe, (uint32_t)pTexs->vecTextures.size(), pTexs->vecTextures.data());
                            }
                            bFind = true;
                        }
                    }

                    if (!bFind)
                    {
                        auto it = m_pSharedPreBinder->m_mapPreBindBufferResourceView.find(pBlock->m_szName);
                        if (it != m_pSharedPreBinder->m_mapPreBindBufferResourceView.end())
                        {
                            gfx::IKGFX_BufferView* pView = it->second;
                            if (pBlock->m_eTextureType == TextureType::CombinedSamplerBuffer)
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &pView);
                                }
                            }
                            else if (pBlock->m_eTextureType == TextureType::RWBuffer)
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &pView);
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &pView);
                                }
                            }
                            bFind = true;
                        }
                    }
                    // ASSERT(bFind);
                }
                // ASSERT(bFind);
                if (!bFind)
                {
                    for (auto& itt : m_vecBindItem)
                    {
                        BindItem& item = itt;
                        if (pBlock->m_szName == item.pName)
                        {
                            bFind = true;
                            switch (item.bindtype)
                            {
                            case BindType::RWBUFFERVIEW_TYPE:
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &(item.rw_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &(item.rw_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &(item.rw_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &(item.rw_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &(item.rw_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindRWBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &(item.rw_buffer_view.pBufView));
                                }
                                break;
                            }
                            case BindType::SAMPLEBUFFERVIEW_TYPE:
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingVs, 1, &(item.sample_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingFs, 1, &(item.sample_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingCs, 1, &(item.sample_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingGs, 1, &(item.sample_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTc, 1, &(item.sample_buffer_view.pBufView));
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindSampleBufferView(0, (uint32_t)pBlock->m_nLayoutBindingTe, 1, &(item.sample_buffer_view.pBufView));
                                }
                                break;
                            }
                            case BindType::TEXTURE_SRV_TYPE:
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.texture_srv.pSRV);
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.texture_srv.pSRV);
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.texture_srv.pSRV);
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.texture_srv.pSRV);
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.texture_srv.pSRV);
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRV(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.texture_srv.pSRV);
                                }
                                break;
                            }
                            case BindType::TEXTURE_UAV_TYPE:
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAV(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.texture_uav.pUAV);
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAV(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.texture_uav.pUAV);
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAV(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.texture_uav.pUAV);
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAV(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.texture_uav.pUAV);
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAV(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.texture_uav.pUAV);
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAV(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.texture_uav.pUAV);
                                }
                                break;
                            }
                            case BindType::TEXTURE_UAV_ARRAY_TYPE:
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAVArray(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.texture_uav_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_uav_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAVArray(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.texture_uav_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_uav_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAVArray(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.texture_uav_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_uav_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAVArray(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.texture_uav_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_uav_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAVArray(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.texture_uav_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_uav_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindUAVArray(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.texture_uav_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_uav_array.uSlotsOffset);
                                }
                                break;
                            }
                            case BindType::TEXTURE_SRV_ARRAY_TYPE:
                            {
                                if (pBlock->m_nLayoutBindingVs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingVs, item.texture_srv_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_srv_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingVs != pBlock->m_nLayoutBindingFs && pBlock->m_nLayoutBindingFs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingFs, item.texture_srv_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_srv_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingCs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingCs, item.texture_srv_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_srv_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingGs >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingGs, item.texture_srv_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_srv_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingTc >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingTc, item.texture_srv_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_srv_array.uSlotsOffset);
                                }
                                if (pBlock->m_nLayoutBindingTe >= 0)
                                {
                                    m_pDescriptorSet->AddBindSRVArray(0, (uint32_t)pBlock->m_nLayoutBindingTe, item.texture_srv_array.uNum, m_vecTextureViewArraySlots.data() + item.texture_srv_array.uSlotsOffset);
                                }
                                break;
                            }
                            default:
                                break;
                            }
                        }
                    }
                }
            }
            bRetCode = m_pDescriptorSet->End();
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        else
        {
            m_pDescriptorSet->TransBarrier();
        }

        bResult = TRUE;
    Exit0:
        m_bBinding = false;
        return bResult;
    }

    BOOL KGFX_ProgramBinderVK::SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;
        KGLOG_PROCESS_ERROR(m_pShaderResource);
        {
            // if(strstr(szName, "local"))
            //{
            //	int x = 0;
            // }
            auto it = m_pShaderResource->m_mapMtlParamItem.find(szName);
            if (it != m_pShaderResource->m_mapMtlParamItem.end())
            {
                gfx::KProgramUniform* pUniform = it->second;

                ASSERT(uByteSize == pUniform->m_uByteSize);
                if (m_pMtlCPUBuffer && pUniform->m_nOffset + uByteSize <= m_uMtlCPUBufferSize && uByteSize == pUniform->m_uByteSize)
                {
                    //uint8_t* pDst = m_pMtlCPUBuffer + pUniform->m_nOffset;
                    //if (!std::equal(pDst, pDst + uByteSize, static_cast<BYTE*>(pData)))
                    if (memcmp(m_pMtlCPUBuffer + pUniform->m_nOffset, pData, uByteSize) != 0)
                    {
                        m_bMtlCPUBufferValueChanged = true;
                        memcpy(m_pMtlCPUBuffer + pUniform->m_nOffset, pData, uByteSize);
                    }
                }
            }
        }
        bResult = TRUE;
    Exit0:
        return bResult;
    }
    BOOL KGFX_ProgramBinderVK::UpdateMtlData()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;
        if (m_pMtlBuffer && m_bMtlCPUBufferValueChanged)
        {
            m_bMtlCPUBufferValueChanged = false;

            bRetCode = m_pMtlBuffer->Update(m_pMtlCPUBuffer, 0, 0, false);
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_ProgramBinderVK::IsDynamicBlock(const_pool_str szName)
    {
        PROF_CPU_DETAIL();
        if (m_pSharedPreBinder)
        {
            //for (auto it : m_pSharedPreBinder->m_mapPreBindBuffer)
            //{
            //    if (it.first == szName)
            //    {
            //        return it.second->IsDynamicUBO();
            //    }
            //}

            for (auto it : m_pSharedPreBinder->m_mapPreBindBufferResourceView)
            {
                if (it.first == szName)
                {
                    return it.second->GetResource()->IsDynamic();
                }
            }
        }
        for (auto it : m_vecBindItem)
        {
            if (it.pName == szName)
            {
                if (it.bindtype == KGFX_ProgramBinderVK::BindType::UBO_BIND_TYPE)
                {
                    return it.ubo.pUBO->IsDynamic();
                }

                if (it.bindtype == KGFX_ProgramBinderVK::BindType::SSBO_BIND_TYPE)
                {
                    return it.ssbo.pSSBO->IsDynamic();
                }
                if (it.bindtype == KGFX_ProgramBinderVK::BindType::ACCELERATION_STRUCTURE_TYPE)
                {
                    return false;
                }
            }
        }

        // DECLARE_PARAM_NAME(globalParam_mat);
        // DECLARE_PARAM_NAME(commonParam_uniform_block);
        DECLARE_PARAM_NAME(MaterialLocalParams);
        DECLARE_PARAM_NAME(PerMTLUBO);

        // 临时
        if (szName == MaterialLocalParams || szName == PerMTLUBO)
        {
            return false;
        }

        ASSERT(0);
        return false;
    }

    TextureType KGFX_ProgramBinderVK::GetTextureType(const_pool_str szName)
    {
        PROF_CPU_DETAIL();
        TextureType textureType = TextureType::Unknown;
        for (auto it : m_pShaderResource->m_vecUniformTexture)
        {
            if (it->m_szName == szName)
            {
                textureType = it->m_eTextureType;
                break;
            }
        }
        return textureType;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    KGFX_GraphicsProgramVK::KGFX_GraphicsProgramVK()
    {
        m_bLoaded = false;
        m_bPostLoaded = false;
        m_pVertDescriptor = nullptr;
        m_pProgramBinder = new KGFX_ProgramBinderVK;
        m_bVertDescriptorAutoCreated = false;
#if KGFX_ProgramVSFSVK_MEM_LEAK
        static int32_t s = 1;
        m_pMemLeak = new char[s];
        if (s == 2)
        {
            int x = 0;
        }
        s++;
#endif
    }


    KGFX_GraphicsProgramVK::~KGFX_GraphicsProgramVK()
    {
        PROF_CPU_DETAIL();
        KShaderResourcePoolVK* pResourcePool = NSEngine::GetShaderResroucePoolVK();
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        if (!m_bVertDescriptorAutoCreated && m_pVertDescriptor)
        {
            pGraphicDevice->DestroyVertDescriptor(m_pVertDescriptor);
        }
        for (auto it : m_vecPipline)
        {
            pGraphicDevice->DestroyRenderPass(it.m_pRenderPass);
            pGraphicDevice->DestroyPipeline(it.m_pPipline);
        }
        SAFE_DELETE(m_pProgramBinder);

#if KGFX_ProgramVSFSVK_MEM_LEAK
        SAFE_DELETE_ARRAY(m_pMemLeak);
#endif
    }

    BOOL KGFX_GraphicsProgramVK::LoadGraphicsShader(
        const char* szShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char* szShaderDef, const char* szMacro,
        BOOL bReCreate, BOOL bByBuildToolCmd, int nPlatform,
        KEnumMtlTaskLevel uThreadLevel
    )
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        // uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD;

        m_LoadParam.m_szShaderSource = szShaderSource;
        m_LoadParam.m_sUserShaderLoc = sIncludeShaderLoc;
        m_LoadParam.m_szShaderDef = szShaderDef;
        m_LoadParam.m_szMacro = szMacro;
        m_LoadParam.m_bReCreate = bReCreate;
        m_LoadParam.m_bByBuildToolCmd = bByBuildToolCmd;
        m_LoadParam.m_nPlatform = nPlatform;
        m_LoadParam.m_uThreadLevel = uThreadLevel;

        if (uThreadLevel == KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
        {
            bRetCode = LoadFromFile();
            KGLOG_PROCESS_ERROR(bRetCode);

            bRetCode = IsLoaded();
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        else
        {
            IKMaterialSystem* pMtlSys = NSEngine::GetMaterialSystemInterface();
            IKGFX_MaterialLoadThread* pLoadThread = pMtlSys->GetMaterialLoadThread();
            bRetCode = pLoadThread->PushKGFXShaderLoadTask(this, 0, uThreadLevel);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = true;
    Exit0:
        return true;
    }

    BOOL KGFX_GraphicsProgramVK::LoadFromFile()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;
        BOOL bLoading = false;

        KShaderResourcePoolVK* pResourcePool = nullptr;
        if(m_LoadParam.m_sUserShaderLoc.IsValid())
        {
            bRetCode = IncludeFileHelper::ReadUserShaderMtlId(m_LoadParam.m_sUserShaderLoc, m_LoadParam.m_nMaterialID, m_LoadParam.m_nReflectionID, m_LoadParam.m_cVaryingMask);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        pResourcePool = NSEngine::GetShaderResroucePoolVK();
        KGLOG_PROCESS_ERROR(pResourcePool);

        m_pProgramBinder->m_pShaderResource = pResourcePool->RequestShaderResource(
            m_LoadParam.m_szShaderSource.c_str(),
            m_LoadParam.m_sUserShaderLoc,
            m_LoadParam.m_szShaderDef.c_str(),
            m_LoadParam.m_szMacro.c_str(),
            bLoading,
            m_LoadParam.m_uThreadLevel,
            &m_mapSamplerState
        );

        KG_PROCESS_ERROR(m_pProgramBinder->m_pShaderResource);

        m_bLoaded = true;

        bResult = true;
    Exit0:
        if(m_bLoaded == false)
        {
            m_bLoaded = -1;
        }
        return true;
    }

    BOOL KGFX_GraphicsProgramVK::PostLoad()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        KGLOG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_pShaderResource);
        // m_pProgramBinder->m_pShaderResource->IsCreated();
        // KGLOG_PROCESS_ERROR(bRetCode);

        // bRetCode = m_pProgramBinder->m_pShaderResource->SetupLayout();
        // KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return true;
    }

    BOOL KGFX_GraphicsProgramVK::IsLoaded()
    {
        return (m_bLoaded == 1);
    }

    BOOL KGFX_GraphicsProgramVK::IsLoadFailed()
    {
        return (m_bLoaded == -1);
    }

    BOOL KGFX_GraphicsProgramVK::IsLoading()
    {
        return (m_bLoaded == 0);
    }

    void KGFX_GraphicsProgramVK::ClearVertDesc()
    {
        PROF_CPU_DETAIL();
        if (m_pVertDescriptor)
        {
            gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
            pGraphicDevice->DestroyVertDescriptor(m_pVertDescriptor);
        }
    }

    IKGFX_GraphicsProgram& KGFX_GraphicsProgramVK::BeginVertDesc()
    {
        PROF_CPU_DETAIL();
        m_bCreatingVertScriptor = false;
        if (!m_pVertDescriptor)
        {
            gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
            pGraphicDevice->CreateVertDescriptor(&m_pVertDescriptor);
            m_bCreatingVertScriptor = true;
            m_pVertDescriptor->Begin();
        }
        return *this;
    }

    IKGFX_GraphicsProgram& KGFX_GraphicsProgramVK::AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate)
    {
        PROF_CPU_DETAIL();
        if (m_bCreatingVertScriptor)
        {
            m_pVertDescriptor->AddBindDescription(binding, stride, inputRate);
        }
        return *this;
    }

    IKGFX_GraphicsProgram& KGFX_GraphicsProgramVK::AddAttribute(gfx::KVertexDecl* pDecl, uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset)
    {
        PROF_CPU_DETAIL();
        if (m_bCreatingVertScriptor)
        {
            m_pVertDescriptor->AddAttribute(binding, location, format, offset);
        }
        return *this;
    }

    BOOL KGFX_GraphicsProgramVK::EndVertDesc()
    {
        PROF_CPU_DETAIL();
        BOOL bRet = true;
        if (m_bCreatingVertScriptor)
        {
            bRet = m_pVertDescriptor->End();
        }
        return bRet;
    }

    BOOL KGFX_GraphicsProgramVK::BindVertAttr(gfx::KVertexDecl* pDecls[], uint32_t uDeclCount)
    {
        PROF_CPU_DETAIL();
        BOOL bRet = false;
        BOOL bRetCode = false;

        KGLOG_PROCESS_ERROR(m_bLoaded);


        KGLOG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_pShaderResource);
        if (!m_pVertDescriptor && m_pProgramBinder->m_pShaderResource->IsLoaded())
        {
            bRetCode = m_pProgramBinder->m_pShaderResource->RequestVertDescriptor(pDecls, uDeclCount, &m_pVertDescriptor);
            m_bVertDescriptorAutoCreated = true;
            KGLOG_PROCESS_ERROR(bRetCode && m_pVertDescriptor);
        }

        // if (!m_bPostLoaded)
        //{
        //     bRetCode = PostLoad();
        //     KGLOG_PROCESS_ERROR(bRetCode);
        //     m_bPostLoaded = true;
        // }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGFX_GraphicsProgramVK::IsReady()
    {
        return m_bVertDescriptorAutoCreated;
    }

    BOOL KGFX_GraphicsProgramVK::SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;
        KGLOG_PROCESS_ERROR(m_pProgramBinder);

        bRetCode = m_pProgramBinder->SetMtlParamValue(szName, pData, uByteSize);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }


    BOOL KGFX_GraphicsProgramVK::ApplyRenderState(const std::function<void(gfx::KRenderState*)>& fnRenderStateDefineCall, gfx::KRenderState* pRenderState)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;
        if (pRenderState)
        {
            m_RenderState = *pRenderState;
        }
        else
        {
            m_RenderStateTo = m_RenderState;
        }
        if (fnRenderStateDefineCall)
        {
            fnRenderStateDefineCall(&m_RenderStateTo);
        }

        bResult = true;
        return bResult;
    }

    BOOL KGFX_GraphicsProgramVK::PreparePipeline()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;


        if (!m_pCurrentPipeline)
        {
            gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
            gfx::IKGFX_RenderFrameBuffer* pRenderFrame = ((KVulkanRenderContext*)m_pCurrentCommonParam->pRenderCtx)->GetCurrentFrameBuffer();
            uint64_t                               uSpecializationConstHash = 0;

            KGLOG_PROCESS_ERROR(m_pVertDescriptor && m_pProgramBinder && m_pProgramBinder->m_pShaderResource);

            if (!m_pProgramBinder->m_vecBindSpecializationConstItem.empty())
            {
                const char* buffer = (const char*)m_pProgramBinder->m_vecBindSpecializationConstItem.data();
                uint32_t    size = (uint32_t)sizeof(KSpecializationConstItem) * (uint32_t)m_pProgramBinder->m_vecBindSpecializationConstItem.size();
                uSpecializationConstHash = KSTR_HELPER::GetHashCodeForMem64Bit(buffer, size, 0);
            }

            // 复用 pipeline 多线程加载
            auto vulkanRenderFrameBuffer = (KVulkanRenderFrameBuffer*)pRenderFrame;
            auto renderPass = vulkanRenderFrameBuffer->GetRenderPassPtr();
            ASSERT(renderPass);

            {
                KEnumMtlTaskLevel uThreadLevel = m_LoadParam.m_uThreadLevel;
                if(m_bByPreForceLoad)
                {
                    //如果开启ByPreForceLoad加载，pipeline的创建强行改成单线程模式，不然要用的那帧还是画不出东西，也就失去预加载的意义了
                    uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD;
                }                
                bRetCode = m_pProgramBinder->m_pShaderResource->RequestPipeline(m_RenderStateTo, renderPass, m_pVertDescriptor, &m_pCurrentPipeline, uThreadLevel, m_pProgramBinder->m_vecBindSpecializationConstItem.data(), (uint32_t)m_pProgramBinder->m_vecBindSpecializationConstItem.size(), uSpecializationConstHash);                
            }

            KG_PROCESS_ERROR(bRetCode && m_pCurrentPipeline);

            /*
            for (auto it : m_vecPipline)
            {
                if (it.renderState.GetHash() == m_RenderStateTo.GetHash() &&
                    it.m_enumRenderPass == enumRenderPass &&
                    it.uSpecializationConstHash == uSpecializationConstHash &&
                    it.uVertDescriptorAttributeHash == m_pVertDescriptor->GetHashCode())
                {
                    m_pCurrentPipeline = it.m_pPipline;
                    break;
                }
            }

            if (!m_pCurrentPipeline)
            {
                gfx::KVulkanRenderPass* pRenderPass = nullptr;
                if (pGraphicDevice->CreateRenderPass(&pRenderPass, enumRenderPass))
                {
                    gfx::KVulkanLayout* pLayout = m_pProgramBinder->m_pShaderResource->GetLayout();
                    gfx::GraphicsPipelineDesc graphicDesc;
                    graphicDesc.pLayout = pLayout;
                    graphicDesc.pRenderState = &m_RenderStateTo;

                    graphicDesc.pStage = m_pProgramBinder->m_pShaderResource->m_pShaderStage;
                    graphicDesc.pVertexDescriptor = m_pVertDescriptor;
                    graphicDesc.uStageCount = 2;
                    graphicDesc.pRenderPass = pRenderPass;

                    gfx::KSpecializationConstantContainer* pContainer = nullptr;
                    gfx::KSpecializationConstantContainer container;

                    if (DrvOption::bMacroToSpicalizationConstantsEnable)
                    {
                        pContainer = &container;
                        for (const auto& it : m_pProgramBinder->m_vecBindSpecializationConstItem)
                        {
                            const KSpecializationConstItem& item = it;
                            if (item.const_type == UINT_CONSTANT_TYPE)
                            {
                                container.AddUInt(item.stage_id, item.uConstId, item.uValue);
                            }
                            else if (item.const_type == INT_CONSTANT_TYPE)
                            {
                                container.AddInt(item.stage_id, item.uConstId, item.nValue);
                            }
                            else if (item.const_type == FLOAT_CONSTANT_TYPE)
                            {
                                container.AddFloat(item.stage_id, item.uConstId, item.fValue);
                            }
                        }
                    }

                    if (pGraphicDevice->CreateGraphicsPipeline(&m_pCurrentPipeline, &graphicDesc, pContainer))
                    {
                        if (m_pCurrentPipeline)
                        {
                            _pipeline p;
                            p.m_enumRenderPass = enumRenderPass;
                            p.m_pPipline = m_pCurrentPipeline;
                            p.m_pRenderPass = pRenderPass;
                            p.renderState = m_RenderStateTo;
                            p.uSpecializationConstHash = uSpecializationConstHash;
                            p.uVertDescriptorAttributeHash = m_pVertDescriptor->GetHashCode();
                            m_vecPipline.push_back(p);
                        }
                    }
                    else
                    {
                        pGraphicDevice->DestroyRenderPass(pRenderPass);
                    }
                }
            }
            */
        }


        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL gfx::KGFX_GraphicsProgramVK::ApplyPipeline()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        KG_PROCESS_ERROR(m_pCurrentPipeline && m_pProgramBinder->m_pDescriptorSet);
        {
            gfx::KVulkanLayout* pLayout = m_pProgramBinder->m_pShaderResource->GetLayout();
            KG_PROCESS_ERROR(pLayout);
            KVulkanDescriptorSet* pVulkanDescriptorSet = (KVulkanDescriptorSet*)m_pProgramBinder->m_pDescriptorSet;
            VkDescriptorSet       pSet = pVulkanDescriptorSet->GetDescriptorSet(0);
            KVulkanRenderContext* pVKRenderCtx = (KVulkanRenderContext*)m_pCurrentCommonParam->pRenderCtx;
            CHECK_ASSERT(pVKRenderCtx);

            if (pSet != nullptr)
            {
                uint32_t* pDynamicUBOOffsetsArray = m_pProgramBinder->m_pDescriptorSet->GetDynamicUBOOffetArray(0);
                uint32_t  uDynamicUBOOffsetsArrayCounts = m_pProgramBinder->m_pDescriptorSet->GetDynamicUBOOffsetArrayCount(0);
                pVKRenderCtx->CmdBindDescriptorSets(gfx::PIPELINE_BIND_POINT_GRAPHICS, pLayout, 0, m_pProgramBinder->m_pDescriptorSet, uDynamicUBOOffsetsArrayCounts, pDynamicUBOOffsetsArray);
            }

            pVKRenderCtx->CmdBindPipeline(gfx::PIPELINE_BIND_POINT_GRAPHICS, m_pCurrentPipeline);
        }

        bRetCode = m_pProgramBinder->UpdateMtlData();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    uint32_t KGFX_GraphicsProgramVK::GetCurrentPipelineCode()
    {
        if(m_pCurrentPipeline)
        {
            return m_pCurrentPipeline->GetCreateId();
        }
        else
        {
            return 0;
        }
    }

    BOOL gfx::KGFX_GraphicsProgramVK::UpdateMtlData()
    {
        PROF_CPU_DETAIL();
        return m_pProgramBinder->UpdateMtlData();
    }

    gfx::IKGFX_ProgramBinder* gfx::KGFX_GraphicsProgramVK::GetProgramBinder()
    {
        return m_pProgramBinder;
    }

    gfx::IKGFX_ProgramBinder& gfx::KGFX_GraphicsProgramVK::BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder)
    {
        PROF_CPU_DETAIL();
        ASSERT(pRenderCommanParam);
        m_pCurrentPipeline = nullptr;
        if (m_pProgramBinder)
        {
            m_pCurrentCommonParam = pRenderCommanParam;
            m_pProgramBinder->BeginBind(pRenderCommanParam, pShareBinder);
        }
        return *m_pProgramBinder;
    }

    void gfx::KGFX_GraphicsProgramVK::AddSamplerState(const char* pName, gfx::KSamplerState& samplerState)
    {
        PROF_CPU_DETAIL();
        m_mapSamplerState[pName] = samplerState;
    }

    BOOL KGFX_GraphicsProgramVK::SetConstDataBlock(uint32_t uSize, void* pData)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        KGLOG_ASSERT_EXIT(m_pCurrentCommonParam && m_pCurrentCommonParam->pRenderCtx);
        KGLOG_ASSERT_EXIT(uSize > 0 && pData);
        KGLOG_ASSERT_EXIT(m_pProgramBinder->m_pShaderResource && m_pProgramBinder->m_pShaderResource->IsLoaded());

        // 要求在非binding状态下设置
        KGLOG_ASSERT_EXIT(!m_pProgramBinder->m_bBinding);

        bRetCode = m_pProgramBinder->m_pShaderResource->ApplyPushConstDataDirectly(m_pCurrentCommonParam->pRenderCtx, gfx::ShaderStageType::AllGraphics, uSize, pData);
        KGLOG_ASSERT_EXIT(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    void KGFX_GraphicsProgramVK::GetUserShaderDetail(int32_t& nMaterialID, int32_t& nReflectionID, char& cVaryingMask)
    {
        nMaterialID = m_LoadParam.m_nMaterialID;
        nReflectionID = m_LoadParam.m_nReflectionID;
        cVaryingMask = m_LoadParam.m_cVaryingMask;
    }

    BOOL gfx::KGFX_GraphicsProgramVK::EndBind()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        KG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_bBinding);

        if (!m_bPostLoaded)
        {
            bRetCode = PostLoad();
            KGLOG_PROCESS_ERROR(bRetCode);
            m_bPostLoaded = true;
        }

        bRetCode = m_pProgramBinder->EndBind();
        KG_PROCESS_ERROR(bRetCode);

        bRetCode = PreparePipeline();
        KG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        m_pProgramBinder->m_bBinding = false;
        return bResult;
    }

    void gfx::KGFX_GraphicsProgramVK::SwapBindData(IKGFX_GraphicsProgram* pProgram)
    {
        PROF_CPU_DETAIL();
        gfx::KGFX_GraphicsProgramVK* p = (gfx::KGFX_GraphicsProgramVK*)pProgram;
        m_pProgramBinder->SwapBinderData(p->m_pProgramBinder);
    }

    BOOL gfx::KGFX_GraphicsProgramVK::IsTextureBinded(const_pool_str pName)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        KG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_bBinding);

        bRetCode = m_pProgramBinder->IsTextureBinded(pName);
        KG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    const gfx::KRenderState& gfx::KGFX_GraphicsProgramVK::GetSrcRenderState()
    {
        return m_RenderState;
    }

    gfx::KRenderState* gfx::KGFX_GraphicsProgramVK::GetRenderState()
    {
        return &m_RenderStateTo;
    }

    BOOL gfx::KGFX_GraphicsProgramVK::IsBeginBind()
    {
        PROF_CPU_DETAIL();
        if (m_pProgramBinder)
        {
            return m_pProgramBinder->m_bBinding;
        }
        else
        {
            return false;
        }
    }
    void gfx::KGFX_GraphicsProgramVK::SetBeginBind(BOOL bBeginBind)
    {
        PROF_CPU_DETAIL();
        if (m_pProgramBinder)
        {
            m_pProgramBinder->m_bBinding = false;
        }
    }

    BOOL gfx::KGFX_GraphicsProgramVK::IsActiveBlock(const_pool_str pcszName)
    {
        PROF_CPU_DETAIL();
        BOOL bActive = false;
        if (m_pProgramBinder)
        {
            bActive = m_pProgramBinder->m_pShaderResource->IsActiveBlock(pcszName);
        }
        return bActive;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    KGFX_ComputeProgramVK::KGFX_ComputeProgramVK()
    {
        m_bLoaded = false;
        m_pProgramBinder = new KGFX_ProgramBinderVK;
    }

    KGFX_ComputeProgramVK::~KGFX_ComputeProgramVK()
    {
        PROF_CPU_DETAIL();
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        for (auto it : m_vecPipline)
        {
            pGraphicDevice->DestroyPipeline(it.m_pPipline);
        }
        SAFE_DELETE(m_pProgramBinder);
    }

    BOOL KGFX_ComputeProgramVK::LoadFromFile()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        KGLOG_ASSERT_EXIT(!m_pProgramBinder->m_pShaderResource);

        m_pProgramBinder->m_pShaderResource = new KShaderResourceVK();
        KG_PROCESS_ERROR(m_pProgramBinder->m_pShaderResource);

        bRetCode = m_pProgramBinder->m_pShaderResource->LoadFromFileCS(m_LoadParam.m_szShaderSource.c_str(),
            m_LoadParam.m_sUserShaderLoc, m_LoadParam.m_szShaderDef.c_str(), m_LoadParam.m_szMacro.c_str(), m_LoadParam.m_bByBuildToolCmd, m_LoadParam.m_nPlatform);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_bLoaded = true;
        bResult = true;
    Exit0:
        if (!m_pProgramBinder->m_pShaderResource)
        {
            SAFE_RELEASE(m_pProgramBinder->m_pShaderResource);
        }
        if(!m_bLoaded)
        {
            m_bLoaded = -1;
        }
        return bResult;
    }

    BOOL KGFX_ComputeProgramVK::IsLoaded()
    {
        return (m_bLoaded == 1);
    }

    BOOL KGFX_ComputeProgramVK::IsLoadFailed()
    {
        return (m_bLoaded == -1);
    }

    BOOL KGFX_ComputeProgramVK::IsLoading()
    {
        return (m_bLoaded == 0);
    }

    //BOOL KGFX_ComputeProgramVK::LoadComputeShader(const char* pcszShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* pcszShaderDef, const char* pcszMacro, BOOL bByBuildToolCmd /*= false*/, int nPlatform /*= 0*/, KEnumMtlTaskLevel uThreadLevel)
    //{
    //    BOOL bResult = FALSE;
    //    BOOL bRetCode = FALSE;

    //    m_LoadParam.m_szShaderSource = pcszShaderSource;
    //    m_LoadParam.m_sUserShaderLoc = sIncludeShaderLoc;
    //    m_LoadParam.m_szShaderDef = pcszShaderDef;
    //    m_LoadParam.m_szMacro = pcszMacro;
    //    m_LoadParam.m_bReCreate = false;
    //    m_LoadParam.m_bByBuildToolCmd = bByBuildToolCmd;
    //    m_LoadParam.m_nPlatform = nPlatform;
    //    m_LoadParam.m_uThreadLevel = uThreadLevel;

    //    KGLOG_ASSERT_EXIT(!m_pProgramBinder->m_pShaderResource);

    //    m_pProgramBinder->m_pShaderResource = new KShaderResourceVK();
    //    KG_PROCESS_ERROR(m_pProgramBinder->m_pShaderResource);

    //    bRetCode = m_pProgramBinder->m_pShaderResource->LoadFromFileCS(pcszShaderSource, sIncludeShaderLoc, pcszShaderDef, pcszMacro, bByBuildToolCmd, nPlatform);
    //    KGLOG_ASSERT_EXIT(bRetCode);

    //    // bRetCode = m_pProgramBinder->m_pShaderResource->SetupLayout();
    //    // KGLOG_ASSERT_EXIT(bRetCode);
    //    m_bLoaded = true;
    //    bResult = TRUE;
    //Exit0:
    //    if (!m_pProgramBinder->m_pShaderResource)
    //    {
    //        SAFE_RELEASE(m_pProgramBinder->m_pShaderResource);
    //    }
    //    return bResult;
    //}

    BOOL KGFX_ComputeProgramVK::LoadComputeShader(const char* pcszShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char* pcszShaderDef, const char* pcszMacro, BOOL bByBuildToolCmd /*= false*/, int nPlatform /*= 0*/, KEnumMtlTaskLevel uThreadLevel)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        m_LoadParam.m_szShaderSource = pcszShaderSource;
        m_LoadParam.m_sUserShaderLoc = sIncludeShaderLoc;
        m_LoadParam.m_szShaderDef = pcszShaderDef;
        m_LoadParam.m_szMacro = pcszMacro;
        m_LoadParam.m_bReCreate = false;
        m_LoadParam.m_bByBuildToolCmd = bByBuildToolCmd;
        m_LoadParam.m_nPlatform = nPlatform;
        m_LoadParam.m_uThreadLevel = uThreadLevel;

        if (uThreadLevel == KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
        {
            bRetCode = LoadFromFile();
            KGLOG_PROCESS_ERROR(bRetCode);

            bRetCode = IsLoaded();
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        else
        {
            IKMaterialSystem* pMtlSys = NSEngine::GetMaterialSystemInterface();
            IKGFX_MaterialLoadThread* pLoadThread = pMtlSys->GetMaterialLoadThread();
            bRetCode = pLoadThread->PushKGFXShaderLoadTask(this, 0, uThreadLevel);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }


    IKGFX_ProgramBinder& KGFX_ComputeProgramVK::BeginBind()
    {
        PROF_CPU_DETAIL();
        m_pProgramBinder->BeginBind(nullptr, nullptr);
        return *m_pProgramBinder;
    }

    IKGFX_ProgramBinder& KGFX_ComputeProgramVK::BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder)
    {
        PROF_CPU_DETAIL();
        if (m_pProgramBinder)
        {
            m_pProgramBinder->BeginBind(pRenderCommanParam, pShareBinder);
        }
        return *m_pProgramBinder;
    }

    BOOL KGFX_ComputeProgramVK::EndBind()
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        KGLOG_ASSERT_EXIT(m_pProgramBinder->m_pShaderResource && m_pProgramBinder->m_pShaderResource->IsLoaded());

        bRetCode = m_pProgramBinder->EndBind();
        KG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    void KGFX_ComputeProgramVK::SetBeginBind(BOOL bBeginBind)
    {
        PROF_CPU_DETAIL();
        if (m_pProgramBinder)
        {
            m_pProgramBinder->m_bBinding = false;
        }
    }

    BOOL KGFX_ComputeProgramVK::Apply(IKGFX_RenderContext* pRenderCtx)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        BOOL bRetCode = false;

        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        gfx::KPipeline* pPipeline = nullptr;
        uint64_t             uSpecializationConstHash = 0;

        KGLOG_ASSERT_EXIT(m_pProgramBinder->m_pShaderResource && m_pProgramBinder->m_pShaderResource->IsLoaded());

        if (!m_pProgramBinder->m_vecBindSpecializationConstItem.empty())
        {
            const char* buffer = (const char*)m_pProgramBinder->m_vecBindSpecializationConstItem.data();
            uint32_t    size = (uint32_t)sizeof(KSpecializationConstItem) * (uint32_t)m_pProgramBinder->m_vecBindSpecializationConstItem.size();
            uSpecializationConstHash = KSTR_HELPER::GetHashCodeForMem64Bit(buffer, size, 0);
        }

        for (auto it : m_vecPipline)
        {
            if (it.uSpecializationConstHash == uSpecializationConstHash)
            {
                pPipeline = it.m_pPipline;
                break;
            }
        }

        if (!pPipeline)
        {
            gfx::ComputePipelineDesc computePipelineDesc;

            computePipelineDesc.pLayout = m_pProgramBinder->m_pShaderResource->GetLayout();
            KGLOG_ASSERT_EXIT(computePipelineDesc.pLayout);
            computePipelineDesc.pStage = m_pProgramBinder->m_pShaderResource->m_pShaderStage[0];
            KGLOG_ASSERT_EXIT(computePipelineDesc.pStage);

            gfx::KSpecializationConstantContainer* pContainer = nullptr;
            gfx::KSpecializationConstantContainer  container;

            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                pContainer = &container;
                for (const auto& it : m_pProgramBinder->m_vecBindSpecializationConstItem)
                {
                    const KSpecializationConstItem& item = it;
                    if (item.const_type == UINT_CONSTANT_TYPE)
                    {
                        container.AddUInt(item.stage_id, item.uConstId, item.uValue);
                    }
                    else if (item.const_type == INT_CONSTANT_TYPE)
                    {
                        container.AddInt(item.stage_id, item.uConstId, item.nValue);
                    }
                    else if (item.const_type == FLOAT_CONSTANT_TYPE)
                    {
                        container.AddFloat(item.stage_id, item.uConstId, item.fValue);
                    }
                }
            }

            bRetCode = pGraphicDevice->CreateComputePipeline(&pPipeline, &computePipelineDesc, pContainer);
            KGLOG_ASSERT_EXIT(bRetCode && pPipeline);

            _pipeline p;
            p.m_pPipline = pPipeline;
            p.uSpecializationConstHash = uSpecializationConstHash;
            m_vecPipline.push_back(p);
            pPipeline = pPipeline;
        }

        KGLOG_ASSERT_EXIT(pPipeline && m_pProgramBinder->m_pDescriptorSet);

        {
            KVulkanRenderContext* pRenderCtxVK = dynamic_cast<KVulkanRenderContext*>(pRenderCtx);
            CHECK_ASSERT(pRenderCtxVK);

            gfx::KVulkanLayout* pLayout = m_pProgramBinder->m_pShaderResource->GetLayout();
            KG_PROCESS_ERROR(pLayout);

            uint32_t* pDynamicUBOOffsetsArray = m_pProgramBinder->m_pDescriptorSet->GetDynamicUBOOffetArray(0);
            uint32_t  uDynamicUBOOffsetsArrayCounts = m_pProgramBinder->m_pDescriptorSet->GetDynamicUBOOffsetArrayCount(0);
            pRenderCtxVK->CmdBindDescriptorSets(gfx::PIPELINE_BIND_POINT_COMPUTE, pLayout, 0, m_pProgramBinder->m_pDescriptorSet, uDynamicUBOOffsetsArrayCounts, pDynamicUBOOffsetsArray);
            pRenderCtxVK->CmdBindPipeline(gfx::PIPELINE_BIND_POINT_COMPUTE, pPipeline);
            m_uPipelineCode = pPipeline->GetCreateId();
        }

        bRetCode = m_pProgramBinder->UpdateMtlData();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    uint32_t KGFX_ComputeProgramVK::GetCurrentPipelineCode()
    {
        return m_uPipelineCode;
    }

    BOOL KGFX_ComputeProgramVK::SetConstDataBlock(gfx::IKGFX_RenderContext* pRenderCtx, uint32_t uSize, void* pData)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        KGLOG_ASSERT_EXIT(pRenderCtx);
        KGLOG_ASSERT_EXIT(uSize > 0 && pData);
        KGLOG_ASSERT_EXIT(m_pProgramBinder->m_pShaderResource && m_pProgramBinder->m_pShaderResource->IsLoaded());

        // 要求在非binding状态下设置
        KGLOG_ASSERT_EXIT(!m_pProgramBinder->m_bBinding);

        bRetCode = m_pProgramBinder->m_pShaderResource->ApplyPushConstDataDirectly(pRenderCtx, gfx::ShaderStageType::Compute, uSize, pData);
        KGLOG_ASSERT_EXIT(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KGFX_ComputeProgramVK::IsReady()
    {
        throw std::runtime_error("The method or operation is not implemented.");
        return {};
    }


    IKGFX_ProgramBinder* KGFX_ComputeProgramVK::GetProgramBinder()
    {
        return m_pProgramBinder;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pUAV)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        if (pUAV)
        {
            // UAV在Vulkan中用两种表示方式，
            // imageBuffer和SSBO（imageBuffer对应于DX中的RWBuffer<>，SSBO对应于RWStructuredBuffer<>和RWByteAddressBuffer）

            // Vulkan中如果需要使用imageBuffer，需要绑定BufferView
            // Vulkan中如果需要使用SSBO，需要绑定Buffer，其BufferView为nullptr
            ASSERT(pUAV->GetViewDesc()->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV);
            BindItem item;
            auto     pViewHandle = pUAV->GetViewHandle();
            if (pViewHandle)
            {
                // imageBuffer绑定
                item.bindtype = BindType::RWBUFFERVIEW_TYPE;
                item.pName = pcszName;
                item.rw_buffer_view.pBufView = pUAV;
                m_vecBindItem.emplace_back(item);
                //m_setBindedTextureRegisterName.insert(pcszName);
                m_mapBindedTextureRegisterName[pcszName] = true;
            }
            else
            {
                // SSBO绑定
                auto pDesc = pUAV->GetViewDesc();

                item.bindtype = BindType::SSBO_BIND_TYPE;
                item.pName = pcszName;
                item.ssbo.pSSBO = (KVulkanBuffer*)pUAV->GetResource();
                item.ssbo.uByteOffset = pDesc->uBytesOffset;
                item.ssbo.uByteSize = pDesc->uBytesRange;
                item.ssbo.pcszBlockName = nullptr;
                item.ssbo.bUAV = true;
                m_vecBindItem.emplace_back(item);
            }
        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pUAV)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        // if (pUAV)
        {
            if (pUAV)
            {
                ASSERT(pUAV->GetViewDesc().eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV);
            }
            BindItem item;
            item.bindtype = BindType::TEXTURE_UAV_TYPE;
            item.pName = pcszName;
            item.texture_uav.pUAV = pUAV;
            m_vecBindItem.emplace_back(item);
            //m_setBindedTextureRegisterName.insert(pcszName);
            m_mapBindedTextureRegisterName[pcszName] = true;
        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pSRV)
    {
        //PROF_CPU_DETAIL(); 这句本身消耗就很高，先注掉
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        if (pSRV)
        {
            // SRV在Vulkan中用两种表示方式，
            // imageBuffer和SSBO（imageBuffer对应于DX中的Buffer<>，SSBO对应于StructuredBuffer<>和ByteAddressBuffer）

            // Vulkan中如果需要使用imageBuffer，需要绑定BufferView
            // Vulkan中如果需要使用SSBO，需要绑定Buffer，其BufferView为nullptr
            ASSERT(pSRV->GetViewDesc()->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV);
            BindItem item;
            auto     ViewHandle = pSRV->GetViewHandle();
            if (ViewHandle)
            {
                // imageBuffer绑定
                item.bindtype = BindType::SAMPLEBUFFERVIEW_TYPE;
                item.pName = pcszName;
                item.sample_buffer_view.pBufView = pSRV;
                m_vecBindItem.emplace_back(item);
                //m_setBindedTextureRegisterName.insert(pcszName);
                m_mapBindedTextureRegisterName[pcszName] = true;
            }
            else
            {
                // SSBO绑定
                auto pDesc = pSRV->GetViewDesc();

                item.bindtype = BindType::SSBO_BIND_TYPE;
                item.pName = pcszName;
                item.ssbo.pSSBO = (KVulkanBuffer*)pSRV->GetResource();
                item.ssbo.bUAV = false;
                item.ssbo.uByteOffset = pDesc->uBytesOffset;
                item.ssbo.uByteSize = pDesc->uBytesRange;
                item.ssbo.pcszBlockName = nullptr;
                m_vecBindItem.emplace_back(item);
            }
        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews)
    {
        throw std::logic_error("The method or operation is not implemented.");
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews)
    {
        throw std::logic_error("The method or operation is not implemented.");
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        ASSERT(uNum);
        ASSERT(pTexViews);

        BindItem item;
        item.bindtype = BindType::TEXTURE_UAV_ARRAY_TYPE;
        item.pName = pcszName;
        item.texture_uav_array.uNum = uNum;
        item.texture_uav_array.uSlotsOffset = AllocTextureArrayBindingSlots(uNum);
        CHECK_ASSERT(item.texture_uav_array.uSlotsOffset >= 0);

        for (uint32_t i = 0; i < uNum; i++)
        {
            CHECK_ASSERT(pTexViews[i]);
            m_vecTextureViewArraySlots[item.texture_uav_array.uSlotsOffset + i] = pTexViews[i];
        }

        m_vecBindItem.emplace_back(item);
        //m_setBindedTextureRegisterName.insert(pcszName);
        m_mapBindedTextureRegisterName[pcszName] = true;
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pSRV)
    {
        //PROF_CPU_DETAIL(); 这句本身消耗就很高，现注掉
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        if (pSRV)
        {
            ASSERT(pSRV->GetViewDesc().eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV);
            KVulkanTextureView* vkSRV = (KVulkanTextureView*)pSRV;
            if (vkSRV->SupportSampled())
            {
                BindItem item;
                item.bindtype = BindType::TEXTURE_SRV_TYPE;
                item.pName = pcszName;
                item.texture_srv.pSRV = pSRV;
                m_vecBindItem.emplace_back(item);
                //m_setBindedTextureRegisterName.insert(pcszName);
                m_mapBindedTextureRegisterName[pcszName] = true;
            }
            else
            {
                BindItem item;
                item.bindtype = BindType::TEXTURE_UAV_TYPE;
                item.pName = pcszName;
                item.texture_uav.pUAV = pSRV;
                m_vecBindItem.emplace_back(item);
                //m_setBindedTextureRegisterName.insert(pcszName);
                m_mapBindedTextureRegisterName[pcszName] = true;
            }
        }
        else
        {
            TextureType textureType = this->GetTextureType(pcszName);

            if (textureType != TextureType::Unknown)
            {
                m_bBindError = true;
                BindItem item;
                item.bindtype = BindType::TEXTURE_SRV_TYPE;
                item.pName = pcszName;
                item.texture_srv.pSRV = pSRV;
                m_vecBindItem.emplace_back(item);
                //m_setBindedTextureRegisterName.insert(pcszName);
                m_mapBindedTextureRegisterName[pcszName] = true;
            }
        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        ASSERT(uNum);
        ASSERT(pTexViews);

        BindItem item;
        item.bindtype = BindType::TEXTURE_UAV_ARRAY_TYPE;
        item.pName = pcszName;
        item.texture_srv_array.uNum = uNum;
        item.texture_srv_array.uSlotsOffset = AllocTextureArrayBindingSlots(uNum);
        CHECK_ASSERT(item.texture_srv_array.uSlotsOffset >= 0);

        for (uint32_t i = 0; i < uNum; i++)
        {
            CHECK_ASSERT(pTexViews[i]);
            m_vecTextureViewArraySlots[item.texture_srv_array.uSlotsOffset + i] = pTexViews[i];
        }

        m_vecBindItem.emplace_back(item);
        //m_setBindedTextureRegisterName.insert(pcszName);
        m_mapBindedTextureRegisterName[pcszName] = true;
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindSampler(const_pool_str pcszName, gfx::IKGFX_Sampler* pSampler)
    {
        PROF_CPU_DETAIL();
        if (m_vecBindSamplerState.find(pcszName) == m_vecBindSamplerState.end())
        {
            KSamplerItem samplerState;
            samplerState.pName = pcszName;
            samplerState.pSampler = pSampler;
            m_vecBindSamplerState.emplace(pcszName, samplerState);
        }
        return *this;
    }

    BOOL KGFX_ProgramBinderVK::IsTextureBinded(const_pool_str pName)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = false;
        auto it = m_mapBindedTextureRegisterName.find(pName);
        if (it != m_mapBindedTextureRegisterName.end())
        {
            bResult = it->second;
        }
        return bResult;
    }

    void KGFX_ProgramBinderVK::SwapBinderData(KGFX_ProgramBinderVK* pBinder)
    {
        PROF_CPU_DETAIL();
        m_vecBindItem.swap(pBinder->m_vecBindItem);
        m_mapBindedTextureRegisterName.swap(pBinder->m_mapBindedTextureRegisterName);
        m_vecBindSpecializationConstItem.swap(pBinder->m_vecBindSpecializationConstItem);
        m_bBinding = pBinder->m_bBinding;
    }

    BOOL KGFX_ProgramBinderVK::IsBinding()
    {
        return m_bBinding;
    }

    BOOL KGFX_ProgramBinderVK::ComputeBindCode()
    {
        PROF_CPU_DETAIL();
        BOOL     bResult = FALSE;
        BOOL     bRetCode = FALSE;
        uint32_t i = 0;
        m_uBindCheckCode = 0;
        KSTR_HELPER::U64Hash u64hash;
        KGLOG_ASSERT_EXIT(m_bBinding);

        for (const auto& it : m_vecBindItem)
        {
            ++i;
            i = i % 64;
            const BindItem& item = it;
            switch (item.bindtype)
            {
            case BindType::ACCELERATION_STRUCTURE_TYPE:
            {
                if (item.aso.pAS)
                {
                    u64hash.Encode(((uint64_t)item.aso.pAS ^ (uint64_t)item.pName));
                }
            }
            break;
            case BindType::SHARE_UBO_BIND_TYPE:
            {
                if (item.shareUBO.pUBO)
                {
                    u64hash.Encode((item.shareUBO.pUBO->GetCode() ^ (uint64_t)item.pName));
                    u64hash.Encode((uint64_t)item.shareUBO.uSize);
                    u64hash.Encode((uint64_t)item.shareUBO.uOffset);
                }
            }
            break;
            case BindType::UBO_BIND_TYPE:
            {
                if (item.ubo.pUBO)
                {
                    u64hash.Encode((item.ubo.pUBO->GetCode() ^ (uint64_t)item.pName));
                }
            }
            break;
            case BindType::SSBO_BIND_TYPE:
            {
                if (item.ssbo.pSSBO)
                {
                    u64hash.Encode((item.ssbo.pSSBO->GetCode() ^ (uint64_t)item.pName));
                    u64hash.Encode((uint64_t)item.ssbo.uByteOffset);
                    u64hash.Encode((uint64_t)item.ssbo.uByteSize);
                }
            }
            break;
            case BindType::RWBUFFERVIEW_TYPE:
            {
                if (item.rw_buffer_view.pBufView)
                {
                    u64hash.Encode((item.rw_buffer_view.pBufView->GetCode() ^ (uint64_t)item.pName));
                }
            }
            break;
            case BindType::SAMPLEBUFFERVIEW_TYPE:
            {
                if (item.sample_buffer_view.pBufView)
                {
                    u64hash.Encode((item.sample_buffer_view.pBufView->GetCode() ^ (uint64_t)item.pName));
                }
            }
            break;
            case BindType::TEXTURE_SRV_TYPE:
            {
                if (item.texture_srv.pSRV)
                {
                    KVulkanTextureView* vkSRV = (KVulkanTextureView*)item.texture_srv.pSRV;
                    u64hash.Encode((vkSRV->GetCode() ^ (uint64_t)item.pName));
                }
            }
            break;
            case BindType::TEXTURE_UAV_TYPE:
            {
                if (item.texture_uav.pUAV)
                {
                    KVulkanTextureView* vkUAV = (KVulkanTextureView*)item.texture_uav.pUAV;
                    u64hash.Encode((vkUAV->GetCode() ^ (uint64_t)item.pName));
                }
            }
            break;
            case BindType::TEXTURE_SRV_ARRAY_TYPE:
            {
                if (item.texture_srv_array.uSlotsOffset >= 0 && item.texture_srv_array.uNum > 0)
                {
                    uint32_t uSlotsEnd = (uint32_t)item.texture_srv_array.uSlotsOffset + item.texture_srv_array.uNum;
                    for (uint32_t i = (uint32_t)item.texture_srv_array.uSlotsOffset; i < uSlotsEnd; ++i)
                    {
                        KVulkanTextureView* vkSRV = (KVulkanTextureView*)m_vecTextureViewArraySlots[i];
                        u64hash.Encode((vkSRV->GetCode() ^ (uint64_t)item.pName));
                    }
                }
            }
            break;
            case BindType::TEXTURE_UAV_ARRAY_TYPE:
            {
                if (item.texture_uav_array.uSlotsOffset >= 0 && item.texture_uav_array.uNum > 0)
                {
                    uint32_t uSlotsEnd = (uint32_t)item.texture_uav_array.uSlotsOffset + item.texture_uav_array.uNum;
                    for (uint32_t i = (uint32_t)item.texture_uav_array.uSlotsOffset; i < uSlotsEnd; ++i)
                    {
                        KVulkanTextureView* vkUAV = (KVulkanTextureView*)m_vecTextureViewArraySlots[i];
                        u64hash.Encode((vkUAV->GetCode() ^ (uint64_t)item.pName));
                    }
                }
            }
            break;
            default:
                ASSERT(FALSE);
                break;
            }

            // 这两个影响了全局ubo的获取，认为这两个变化了，全局ubo也相应发生了变化
            u64hash.Encode(m_uSceneRenderID * 100);
        }

        for (auto it : m_vecBindSamplerState)
        {
            KSamplerItem& state = it.second;
            if (state.pSampler)
            {
                u64hash.Encode(state.pSampler->GetNativeHandle());
            }
        }
        m_uBindCheckCode = u64hash.GetHash();
        bResult = TRUE;
    Exit0:
        // m_bBinding = false;
        return bResult;
    }

    int KGFX_ProgramBinderVK::AllocTextureArrayBindingSlots(uint32_t uNum)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(uNum > 0);
        int nCurOffset = (int)m_vecTextureViewArraySlots.size();
        m_vecTextureViewArraySlots.push_back_n(uNum);
        return nCurOffset;
    }

    void KGFX_ProgramBinderVK::ClearTextureArrayBindingSlots()
    {
        PROF_CPU_DETAIL();
        m_vecTextureViewArraySlots.clear();
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindCBV(const char* pcszName, IKGFX_BufferView* pBufView)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        CHECK_ASSERT(pBufView);

        BindItem item;

        // UBO绑定
        auto     pDesc = pBufView->GetViewDesc();
        auto     gfxBuffer = (KVulkanBuffer*)pBufView->GetResource();
        uint32_t byteWidth = gfxBuffer->GetDesc()->uByteWidth;
        if ((pDesc->uBytesOffset != 0 || pDesc->uBytesRange < byteWidth) && !gfxBuffer->IsDynamic())
        {
            item.bindtype = BindType::SHARE_UBO_BIND_TYPE;
            item.pName = pcszName;
            item.shareUBO.pUBO = gfxBuffer;
            item.shareUBO.uOffset = pDesc->uBytesOffset;
            item.shareUBO.uSize = pDesc->uBytesRange;
        }
        else
        {
            item.bindtype = BindType::UBO_BIND_TYPE;
            item.pName = pcszName;
            item.ubo.pUBO = gfxBuffer;
            item.ubo.pcszBlockName = nullptr;
        }
        m_vecBindItem.emplace_back(item);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderVK::AddBindAccelerationStructure(const_pool_str pcszName, KRayTracingScene* accelerationStructure)
    {
        PROF_CPU_DETAIL();
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        CHECK_ASSERT(accelerationStructure);

        BindItem item;
        item.bindtype = BindType::ACCELERATION_STRUCTURE_TYPE;
        item.aso.pAS = accelerationStructure;
        item.aso.pcszBlockName = pcszName;
        item.pName = pcszName;
        m_vecBindItem.emplace_back(item);
        return *this;
    }

}
