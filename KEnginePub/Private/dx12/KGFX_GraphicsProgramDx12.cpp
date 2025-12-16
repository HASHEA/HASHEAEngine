#ifdef _WIN32
#include "KGFX_GraphicsProgramDx12.h"
#include "KGFX_PipelineDX12.h"
#include "KGFX_ProgramBinderDX12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_RenderFrameBufferDx12.h"
#include "../comm/KGFX_ShaderHelper.h"
#include "KGFX_ShaderResourceDx12.h"
#include "../../Public/IGFX_Public.h"
////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KMaterialSystem/Public/IKMaterialSystem.h"


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace gfx
{
    KGFX_GraphicsProgramDx12::KGFX_GraphicsProgramDx12()
    {
        m_pProgramBinder.Attch(new KGFX_ProgramBinderDx12);
        m_bReady = false;
        m_bLoaded = false;
    }

    KGFX_GraphicsProgramDx12::~KGFX_GraphicsProgramDx12()= default;


    BOOL KGFX_GraphicsProgramDx12::LoadGraphicsShader(
        const char* szShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char* szShaderDef,
        const char* szMacro,
        BOOL                            bReCreate,
        BOOL                            bByBuildToolCmd /*= false*/,
        int                             nPlatform /*= 0*/,
        KEnumMtlTaskLevel               uThreadLevel /*= KEnumMtlTaskLevel::DISABLE_MTL_THREAD*/
    )
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        const auto& ShaderRootPath = GetDX12TechRootPath();

        std::filesystem::path shaderOrigPath(szShaderSource);
        if (!shaderOrigPath.lexically_relative(ShaderRootPath).empty() && shaderOrigPath.lexically_relative(ShaderRootPath).native()[0] != '.')
        {
        }
        else
        {
            shaderOrigPath = ShaderRootPath / shaderOrigPath;
        }

        m_LoadParam.m_szShaderSource = shaderOrigPath.string();
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
        return bResult;
    }

    BOOL KGFX_GraphicsProgramDx12::LoadFromFile()
    {
        bool bResult = false;
        bool bRetCode = false;
        KGFX_ShaderResourcePoolDx12* pShaderFilePool = nullptr;

        if(m_LoadParam.m_sUserShaderLoc.IsValid())
        {
            bRetCode = IncludeFileHelper::ReadUserShaderMtlId(m_LoadParam.m_sUserShaderLoc, m_LoadParam.m_nMaterialID, m_LoadParam.m_nReflectionID, m_LoadParam.m_cVaryingMask);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        pShaderFilePool = KGFX_GetShaderPoolDx12();
        KGLOG_PROCESS_ERROR(pShaderFilePool);

        {
            std::lock_guard lock(pShaderFilePool->m_PoolLock);
            bResult = pShaderFilePool->RequestFromTechFileDXC(
                m_LoadParam.m_szShaderSource.c_str(),
                m_LoadParam.m_szShaderDef.c_str(),
                m_LoadParam.m_sUserShaderLoc,
                m_LoadParam.m_szMacro.c_str(),
                &(m_pProgramBinder->m_pReflector)
            );
            KGLOG_PROCESS_ERROR(bResult);
        }

        m_bLoaded = true;
        bResult = true;
    Exit0:
        if(m_bLoaded == false)
        {
            m_bLoaded = -1;
        }
        return bResult;
    }

    BOOL KGFX_GraphicsProgramDx12::PostLoad()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        bRetCode = m_pProgramBinder->BuildRootSignature();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicsProgramDx12::IsLoaded()
    {
        return (m_bLoaded == 1);
    }

    BOOL KGFX_GraphicsProgramDx12::IsLoadFailed()
    {
        return (m_bLoaded == -1);
    }

    BOOL KGFX_GraphicsProgramDx12::IsLoading()
    {
        return (m_bLoaded == 0);
    }

    void KGFX_GraphicsProgramDx12::ClearVertDesc()
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    IKGFX_GraphicsProgram& KGFX_GraphicsProgramDx12::BeginVertDesc()
    {
        m_vecInputBindDesc.clear();
        m_vecInputeAttribute.clear();
        m_vecInputLayout.clear();
        return *this;
    }

    IKGFX_GraphicsProgram& KGFX_GraphicsProgramDx12::AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate)
    {
        InputBindDesc desc = { binding, stride, inputRate };
        m_vecInputBindDesc.emplace_back(desc);
        return *this;
    }

    IKGFX_GraphicsProgram& KGFX_GraphicsProgramDx12::AddAttribute(KVertexDecl* pDecl, uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset)
    {
        InputeAttribute desc = { pDecl, binding, location, format, offset };
        m_vecInputeAttribute.emplace_back(desc);
        return *this;
    }

    static DXGI_FORMAT GetVertFormatDx12(enumVertexFormat fmt)
    {
        DXGI_FORMAT ret = DXGI_FORMAT_UNKNOWN;
        switch (fmt)
        {
        case VERT_FORMAT_R32G32B32A32_SFLOAT:
            ret = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case VERT_FORMAT_R32G32B32_SFLOAT:
            ret = DXGI_FORMAT_R32G32B32_FLOAT;
            break;
        case VERT_FORMAT_R32G32_SFLOAT:
            ret = DXGI_FORMAT_R32G32_FLOAT;
            break;
        case VERT_FORMAT_R32_SFLOAT:
            ret = DXGI_FORMAT_R32_FLOAT;
            break;
        case VERT_FORMAT_R8G8B8A8_UINT:
            ret = DXGI_FORMAT_R8G8B8A8_UINT;
            break;
        case VERT_FORMAT_R8G8B8A8_SINT:
            ret = DXGI_FORMAT_R8G8B8A8_SINT;
            break;
        case VERT_FORMAT_R8G8B8A8_UNORM:
            ret = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case VERT_FORMAT_R8G8B8A8_SNORM:
            ret = DXGI_FORMAT_R8G8B8A8_SNORM;
            break;
        case VERT_FORMAT_R16G16_UINT:
            ret = DXGI_FORMAT_R16G16_UINT;
            break;
        case VERT_FORMAT_R16G16_SINT:
            ret = DXGI_FORMAT_R16G16_SINT;
            break;
        default:
            ASSERT(0);
            break;
        }
        return ret;
    }

    BOOL KGFX_GraphicsProgramDx12::EndVertDesc()
    {
        BOOL bResult = false;
        for (auto it : m_vecInputeAttribute)
        {
            InputeAttribute& attr = it;
            D3D12_INPUT_ELEMENT_DESC desc;
            desc.SemanticName = attr.m_pDecl->m_Attr[it.m_location].m_szLocationName;
            desc.SemanticIndex = 0;
            desc.Format = GetVertFormatDx12(attr.m_format);
            KGLOG_PROCESS_ERROR(desc.Format != DXGI_FORMAT_UNKNOWN);
            desc.InputSlot = attr.m_binding;
            desc.AlignedByteOffset = attr.m_offset;
            if (it.m_location >= static_cast<uint32_t>(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE))
            {
                desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                desc.InstanceDataStepRate = 1;
            }
            else
            {
                desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                desc.InstanceDataStepRate = 0;
            }
            m_vecInputLayout.emplace_back(desc);
        }
        bResult = true;
        m_bReady = true;
    Exit0:
        return bResult;
    }


    BOOL KGFX_GraphicsProgramDx12::BindVertAttr(KVertexDecl* pDecls[], uint32_t uDeclCount)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        if (m_bLoaded)
        {
            if (!m_bReady)
            {
                bRetCode = PostLoad();
                KGLOG_PROCESS_ERROR(bRetCode);
            }
            m_vecInputLayout.clear();

            auto vsShader = m_pProgramBinder->m_pReflector->GetShaderCode().at(0);
            assert(vsShader && vsShader->GetShaderStage() == ShaderStageType::Vertex);
            for (auto& it : vsShader->GetShaderRefl()->m_vecAttribute)
            {
                KProgramAttribute* pAtt = it;
                BOOL               bFind = false;
                for (uint32_t i = 0; i < uDeclCount; ++i)
                {
                    KVertexDecl* pDecl = pDecls[i];
                    for (uint32_t j = 0; j < static_cast<uint32_t>(KAttribUsage::COUNT); ++j)
                    {
                        auto usage = GetKAttribUsage(pDecl->m_Attr[j].m_szLocationName);
                        if (usage == pAtt->vertexUsage)
                        {
                            D3D12_INPUT_ELEMENT_DESC desc;
                            desc.SemanticName = pAtt->szName;
                            desc.SemanticIndex = 0;
                            desc.Format = GetVertFormatDx12(pAtt->fmt);
                            assert(desc.Format != DXGI_FORMAT_UNKNOWN);
                            desc.InputSlot = pAtt->bInstanceData ? INSTANCE_BUFFER_BIND_ID : i;
                            desc.AlignedByteOffset = pDecl->m_Attr[j].m_offset;
                            if (pAtt->bInstanceData)
                            {
                                desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                                desc.InstanceDataStepRate = 1;
                            }
                            else
                            {
                                desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                                desc.InstanceDataStepRate = 0;
                            }
                            m_vecInputLayout.emplace_back(desc);
                            bFind = true;
                            break;
                        }
                    }
                }

                if (!bFind)
                {
                    assert(false);
                    KGLogPrintf(KGLOG_ERR, "顶点%s没有绑定", pAtt->szName);
                }
            }
        }
        bResult = true;
        m_bReady = true;
    Exit0:
        return bResult;
    }


    BOOL KGFX_GraphicsProgramDx12::BuildPipeline(IKGFX_RenderContext* pRenderCtx, KRenderState& state, KGFX_PipelineDX12** ppPipeline) const
    {
        BOOL               bResult = false;
        KGFX_PipelineDX12* pPipeline = nullptr;
        pPipeline = new KGFX_PipelineDX12();

        auto pFrameBufferDx12 = (const KGFX_RenderFrameBufferDx12*)((KGFX_CommandBufferDX12Impl*)pRenderCtx)->GetCurrentFrameBuffer();
        const KGfxFrameBufferDesc& FBDesc = pFrameBufferDx12->GetFBDesc();
        bResult = pPipeline->CreateGraphicsPipeline(FBDesc, state, m_vecInputLayout, *m_pProgramBinder->m_pReflector);
        KGLOG_PROCESS_ERROR(bResult);

        *ppPipeline = pPipeline;

        bResult = true;
    Exit0:
        if (!bResult)
        {
            SAFE_RELEASE(pPipeline);
        }
        return bResult;
    }


    IKGFX_ProgramBinder* KGFX_GraphicsProgramDx12::GetProgramBinder()
    {
        return m_pProgramBinder;
    }


    void KGFX_GraphicsProgramDx12::AddSamplerState(const char* pName, gfx::KSamplerState& samplerState)
    {
        //throw std::logic_error("The method or operation is not implemented.");
    }

    BOOL KGFX_GraphicsProgramDx12::ApplyRenderState(const std::function<void(KRenderState*)>& fnRenderStateDefineCall, KRenderState* pRenderState)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        if (pRenderState)
        {
            m_dstRenderState = *pRenderState;
        }
        else
        {
            m_dstRenderState = m_srcRenderState;
        }

        if (fnRenderStateDefineCall)
        {
            fnRenderStateDefineCall(&m_dstRenderState);
        }
        bResult = true;

        return bResult;
    }

    BOOL KGFX_GraphicsProgramDx12::PreparePipeline()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        KGLOG_PROCESS_ERROR(m_pCurrentCommonParam);

        if (!m_pPipeline)
        {
            uint32_t uStateHash = m_dstRenderState.GetHash();
            auto     it = m_mapPipline.find(uStateHash);
            if (it == m_mapPipline.end())
            {
                bRetCode = BuildPipeline(m_pCurrentCommonParam->pRenderCtx, m_dstRenderState, &m_pPipeline);
                KG_PROCESS_ERROR(bRetCode);
                KAutoRefPtr<KGFX_PipelineDX12> temp = { m_pPipeline ,{} };
                m_mapPipline.insert(std::make_pair<>(uStateHash, temp));
            }
            else
            {
                m_pPipeline = it->second.Get();
            }
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_GraphicsProgramDx12::ApplyPipeline()
    {
        BOOL                       bResult = false;
        BOOL                       bRetCode = false;
        KGFX_ProgramReflectorDx12* pReflector = nullptr;
        KGLOG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_pReflector && m_pCurrentCommonParam);

        {
            KGFX_CommandBufferDX12Impl* pRenderCtxDx12 = static_cast<KGFX_CommandBufferDX12Impl*>(m_pCurrentCommonParam->pRenderCtx);
            ID3D12GraphicsCommandList* pCmdList = pRenderCtxDx12->GetD3D12CommandList();
            pReflector = m_pProgramBinder->m_pReflector;
            pRenderCtxDx12->CmdBindPipeline(enumPipelineBindPoint::PIPELINE_BIND_POINT_GRAPHICS, m_pPipeline);
            pRenderCtxDx12->CmdSetPrimitiveTopology(m_pPipeline->GetPrimitiveTopology());
            pRenderCtxDx12->CmdSetGraphicsRootSignature(pReflector->GetRootSignature());

            /// 先绑定根heap
            pRenderCtxDx12->CmdSetDescriptorHeaps();

            /// 绑定所有RootConstantBufferView 即所有的CBV(包括pushconst和specialconst和材质cbuf)
            {
                int specialBufIndex = 0;
                for (auto rootCbv : pReflector->m_ShaderRootCBV)
                {
                    if (rootCbv.second.GetBindType() == KGFX_ProgramReflectorDx12::BindIndex::SpecialConst)
                    {
                        pCmdList->SetGraphicsRoot32BitConstants(rootCbv.second[0], m_pProgramBinder->m_SpecialConstCbuf->GetCBufSize() / 4, m_pProgramBinder->m_SpecialConstCbuf->GetCpuData(), 0);
                    }
                    else if (rootCbv.second.GetBindType() == KGFX_ProgramReflectorDx12::BindIndex::PushConst)
                    {
                        pCmdList->SetGraphicsRoot32BitConstants(rootCbv.second[0], m_pProgramBinder->m_PushConstCbuf->GetCBufSize() / 4, m_pProgramBinder->m_PushConstCbuf->GetCpuData(), 0);
                    }
                    else
                    {

                        for (int i = 0; i < rootCbv.second.GetCount(); ++i)
                        {

                            IKGFX_BufferView* cbv = m_pProgramBinder->m_vecBindedCBV.at(rootCbv.second[i]);
                            assert(cbv);
                            D3D12_GPU_VIRTUAL_ADDRESS cbvAdd = cbv->GetResource()->GetBufferDeviceAddress() + cbv->GetViewDesc()->uBytesOffset;
                            pRenderCtxDx12->CmdSetGraphicsRootCbuf(rootCbv.second[i], cbvAdd);
                        }

                    }
                }
            }

            /// 绑定所有的RootDescriptorTable 即所有的SRV和UAV和sampler
            {
                int descriptorSetTable = pReflector->m_ShaderRootConstBufferCount;
                for (int i = 0; i < pReflector->GetShaderStageSRVCount().size(); i++)
                {
                    if (pReflector->GetShaderStageSRVCount().at(i) != 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = m_pProgramBinder->GetGPUDescriptorTable(static_cast<ShaderStageType>(1 << i), CopyDescriptorType::SRV);
                        pRenderCtxDx12->CmdSetGraphicsDescriptorTable(descriptorSetTable, tableHandle);
                        //pCmdList->SetGraphicsRootDescriptorTable(descriptorSetTable, tableHandle);
                        descriptorSetTable++;
                    }
                }

                for (int i = 0; i < pReflector->GetShaderStageUAVCount().size(); i++)
                {
                    if (pReflector->GetShaderStageUAVCount().at(i) != 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = m_pProgramBinder->GetGPUDescriptorTable(static_cast<ShaderStageType>(1 << i), CopyDescriptorType::UAV);
                        pRenderCtxDx12->CmdSetGraphicsDescriptorTable(descriptorSetTable, tableHandle);
                        //pCmdList->SetGraphicsRootDescriptorTable(descriptorSetTable, tableHandle);
                        descriptorSetTable++;
                    }
                }

                for (int i = 0; i < pReflector->GetShaderStageSampleCount().size(); i++)
                {
                    if (pReflector->GetShaderStageSampleCount().at(i) != 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = m_pProgramBinder->GetGPUDescriptorTable(static_cast<ShaderStageType>(1 << i), CopyDescriptorType::SAMPLER);
                        pRenderCtxDx12->CmdSetGraphicsDescriptorTable(descriptorSetTable, tableHandle);
                        //pCmdList->SetGraphicsRootDescriptorTable(descriptorSetTable, tableHandle);
                        descriptorSetTable++;
                    }
                }
            }


            bRetCode = m_pProgramBinder->UpdateMtlData();
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    uint32_t KGFX_GraphicsProgramDx12::GetCurrentPipelineCode()
    {
        if(m_pPipeline)
        {
            return m_pPipeline->GetCreateId();
        }
        else
        {
            return 0;
        }
    }

    BOOL KGFX_GraphicsProgramDx12::UpdateMtlData()
    {
        return m_pProgramBinder->UpdateMtlData();
    }


    IKGFX_ProgramBinder& KGFX_GraphicsProgramDx12::BeginBind(const RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder)
    {
        m_pPipeline = nullptr;
        if (m_pProgramBinder)
        {
            m_pCurrentCommonParam = pRenderCommanParam;
            m_pProgramBinder->BeginBind(pRenderCommanParam, pShareBinder);
        }
        return *m_pProgramBinder;
    }

    BOOL KGFX_GraphicsProgramDx12::EndBind()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        KG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_bBinding);

        bRetCode = PreparePipeline();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = m_pProgramBinder->EndBind();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        m_pProgramBinder->m_bBinding = false;
        return bResult;
    }

    void KGFX_GraphicsProgramDx12::SwapBindData(IKGFX_GraphicsProgram* pProgram)
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    BOOL KGFX_GraphicsProgramDx12::IsTextureBinded(const_pool_str pName)
    {
        BOOL bResult = false;
        if (m_pProgramBinder)
        {
            bResult = m_pProgramBinder->IsTextureBinded(pName);
        }
        return bResult;
    }

    const KRenderState& KGFX_GraphicsProgramDx12::GetSrcRenderState()
    {
        return m_srcRenderState;
    }

    KRenderState* KGFX_GraphicsProgramDx12::GetRenderState()
    {
        return &m_dstRenderState;
    }

    BOOL KGFX_GraphicsProgramDx12::IsBeginBind()
    {
        if (m_pProgramBinder)
        {
            return m_pProgramBinder->m_bBinding;
        }
        return false;
    }

    void KGFX_GraphicsProgramDx12::SetBeginBind(BOOL bBeginBind)
    {
        if (m_pProgramBinder)
        {
            m_pProgramBinder->m_bBinding = false;
        }
    }

    BOOL KGFX_GraphicsProgramDx12::IsActiveBlock(const_pool_str pcszName)
    {
        BOOL bActive = false;

        return bActive;
    }

    void KGFX_GraphicsProgramDx12::GetUserShaderDetail(int32_t& nMaterialID, int32_t& nReflectionID, char& cVaryingMask)
    {
        nMaterialID = m_LoadParam.m_nMaterialID;
        nReflectionID = m_LoadParam.m_nReflectionID;
        cVaryingMask = m_LoadParam.m_cVaryingMask;
    }

    BOOL KGFX_GraphicsProgramDx12::IsReady()
    {
        // throw std::logic_error("The method or operation is not implemented.");
        return m_bReady;
    }


    BOOL KGFX_GraphicsProgramDx12::SetConstDataBlock(uint32_t uSize, void* pData)
    {
        //assert(m_pProgramBinder->m_PushConstCbuf);
        if (m_pProgramBinder->m_PushConstCbuf)
        {
            uint8_t* pDataDst = m_pProgramBinder->m_PushConstCbuf->GetCpuData();
            assert(m_pProgramBinder->m_PushConstCbuf->GetCBufSize() == uSize);
            memcpy(pDataDst, pData, uSize);
            //m_pProgramBinder->m_PushConstCbuf->Update(m_pCurrentCommonParam->pRenderCtx);
            //gfx::KGfxBarrier cbufBarrier = {};
            //cbufBarrier.eType = gfx::KGfxBarrier::EType::Buffer;
            //cbufBarrier.pBuffer = m_pProgramBinder->m_PushConstCbuf->GetGfxBuffer();
            //cbufBarrier.eSRCAccess = gfx::KGfxAccess::Unknown;
            //cbufBarrier.eDSTAccess = gfx::KGfxAccess::ConstBuffer;
            //m_pCurrentCommonParam->pRenderCtx->Transition(cbufBarrier);
        }
        return true;
    }

    int KGFX_GraphicsProgramDx12::AddRef()
    {
        int32_t nRef = ++m_nRef;
        return nRef;
    }

    int KGFX_GraphicsProgramDx12::GetRef()
    {
        return m_nRef;
    }

    int KGFX_GraphicsProgramDx12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            KGFX_GraphicsProgramDx12* p = this;
            SAFE_DELETE(p);
        }
        return nRef;
    }


    BOOL KGFX_GraphicsProgramDx12::SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize)
    {
        BOOL                       bResult = false;
        BOOL                       bRetCode = false;
        KGFX_ProgramReflectorDx12* pReflector = nullptr;
        KGLOG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_pReflector);
        pReflector = m_pProgramBinder->m_pReflector;

        bRetCode = pReflector->SetPerMtlValue(szName, pData, uByteSize);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }
} // namespace gfx
#endif
