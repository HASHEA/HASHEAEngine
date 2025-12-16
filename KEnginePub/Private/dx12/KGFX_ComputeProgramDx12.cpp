#include "KGFX_ComputeProgramDx12.h"
#include <filesystem>
#include "KGFX_CommandBufferDX12Impl.h"
#include "KGFX_PipelineDX12.h"
#include "KGFX_ProgramBinderDX12.h"
#include "KGFX_ShaderResourceDx12.h"
#include "KMaterialSystem/Public/IKMaterialSystem.h"

namespace gfx
{
    KGFX_ComputeProgramDx12::KGFX_ComputeProgramDx12()
    {
        m_pProgramBinder = new KGFX_ProgramBinderDx12;
    }

    KGFX_ComputeProgramDx12::~KGFX_ComputeProgramDx12()
    {
        SAFE_DELETE(m_pProgramBinder);
        SAFE_DELETE(m_pPipeline);
    }

    BOOL KGFX_ComputeProgramDx12::LoadComputeShader(const char* pcszShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* pcszShaderDef, const char* pcszMacro, BOOL bByBuildToolCmd, int nPlatform, KEnumMtlTaskLevel uThreadLevel)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        Reset();

        const std::filesystem::path& shaderRootPath = GetDX12TechRootPath();

        std::filesystem::path shaderOrigPath(pcszShaderSource);
        if (!shaderOrigPath.lexically_relative(pcszShaderSource).empty() && shaderOrigPath.lexically_relative(shaderRootPath).native()[0] != '.')
        {

        }
        else
        {
            shaderOrigPath = shaderRootPath / shaderOrigPath;
        }

        m_LoadParam.m_szShaderSource = shaderOrigPath.string();
        m_LoadParam.m_sUserShaderLoc = {};
        m_LoadParam.m_szShaderDef = pcszShaderDef;
        m_LoadParam.m_szMacro = pcszMacro;
        m_LoadParam.m_bReCreate = false;
        m_LoadParam.m_bByBuildToolCmd = bByBuildToolCmd;
        m_LoadParam.m_nPlatform = nPlatform;
        m_LoadParam.m_uThreadLevel = uThreadLevel;//KEnumMtlTaskLevel::DISABLE_MTL_THREAD;

        //bRetCode = LoadFromFile();
        //KGLOG_PROCESS_ERROR(bRetCode);

        //bRetCode = PostLoad();
        //KGLOG_PROCESS_ERROR(bRetCode);

        if (uThreadLevel == KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
        {
            bRetCode = LoadFromFile();
            KGLOG_PROCESS_ERROR(bRetCode);

            //bRetCode = PostLoad();
            //KGLOG_PROCESS_ERROR(bRetCode);

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

    BOOL KGFX_ComputeProgramDx12::Apply(IKGFX_RenderContext* pRenderCtx)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        KGFX_ProgramReflectorDx12* pReflector = nullptr;
        KGLOG_PROCESS_ERROR(m_pProgramBinder && m_pProgramBinder->m_pReflector);



        {
            KGFX_CommandBufferDX12Impl* pRenderCtxDx12 = static_cast<KGFX_CommandBufferDX12Impl*>(pRenderCtx);
            ID3D12GraphicsCommandList* pCmdList = pRenderCtxDx12->GetD3D12CommandList();
            pReflector = m_pProgramBinder->m_pReflector;


            pRenderCtxDx12->CmdBindPipeline(enumPipelineBindPoint::PIPELINE_BIND_POINT_COMPUTE, m_pPipeline);
            pRenderCtxDx12->CmdSetComputeRootSignature(pReflector->GetRootSignature());

            /// 先绑定根heap
            pRenderCtxDx12->CmdSetDescriptorHeaps();

            /// 绑定所有RootConstantBufferView 即所有的CBV(包括pushconst和specialconst和材质cbuf)
            {
                int specialBufIndex = 0;
                for (auto rootCbv : pReflector->m_ShaderRootCBV)
                {
                    if (rootCbv.second.GetBindType() == KGFX_ProgramReflectorDx12::BindIndex::SpecialConst)
                    {
                        auto Index = rootCbv.second[0];

                        pCmdList->SetComputeRoot32BitConstants(Index, m_pProgramBinder->m_SpecialConstCbuf->GetCBufSize() / 4, m_pProgramBinder->m_SpecialConstCbuf->GetCpuData(), 0);

                    }
                    else if (rootCbv.second.GetBindType() == KGFX_ProgramReflectorDx12::BindIndex::PushConst)
                    {
                        auto Index = rootCbv.second[0];

                        pCmdList->SetComputeRoot32BitConstants(Index, m_pProgramBinder->m_PushConstCbuf->GetCBufSize() / 4, m_pProgramBinder->m_PushConstCbuf->GetCpuData(), 0);
                    }
                    else
                    {
                        IKGFX_BufferView* cbv = m_pProgramBinder->m_vecBindedCBV.at(rootCbv.second[0]);
                        assert(cbv);

                        D3D12_GPU_VIRTUAL_ADDRESS cbvAdd = cbv->GetResource()->GetBufferDeviceAddress() + cbv->GetViewDesc()->uBytesOffset;
                        pRenderCtxDx12->CmdSetComputeRootCbuf(rootCbv.second[0], cbvAdd);
                        //pCmdList->SetComputeRootConstantBufferView(rootCbv.second[0], cbvAdd);
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
                        //pCmdList->SetComputeRootDescriptorTable(descriptorSetTable, tableHandle);
                        pRenderCtxDx12->CmdSetComputeDescriptorTable(descriptorSetTable, tableHandle);
                        descriptorSetTable++;
                    }
                }

                for (int i = 0; i < pReflector->GetShaderStageUAVCount().size(); i++)
                {
                    if (pReflector->GetShaderStageUAVCount().at(i) != 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = m_pProgramBinder->GetGPUDescriptorTable(static_cast<ShaderStageType>(1 << i), CopyDescriptorType::UAV);
                        //pCmdList->SetComputeRootDescriptorTable(descriptorSetTable, tableHandle);
                        pRenderCtxDx12->CmdSetComputeDescriptorTable(descriptorSetTable, tableHandle);
                        descriptorSetTable++;
                    }
                }


                for (int i = 0; i < pReflector->GetShaderStageSampleCount().size(); i++)
                {
                    if (pReflector->GetShaderStageSampleCount().at(i) != 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = m_pProgramBinder->GetGPUDescriptorTable(static_cast<ShaderStageType>(1 << i), CopyDescriptorType::SAMPLER);
                        //pCmdList->SetComputeRootDescriptorTable(descriptorSetTable, tableHandle);
                        pRenderCtxDx12->CmdSetComputeDescriptorTable(descriptorSetTable, tableHandle);
                        descriptorSetTable++;
                    }
                }
            }
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    uint32_t KGFX_ComputeProgramDx12::GetCurrentPipelineCode()
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

    BOOL KGFX_ComputeProgramDx12::SetConstDataBlock(IKGFX_RenderContext* pRenderCtx, uint32_t uSize, void* pData)
    {
        if (m_pProgramBinder->m_PushConstCbuf)
        {
            uint8_t* pDataDst = m_pProgramBinder->m_PushConstCbuf->GetCpuData();
            assert(m_pProgramBinder->m_PushConstCbuf->GetCBufSize() == uSize);
            memcpy(pDataDst, pData, uSize);
        }
        return true;
    }

    IKGFX_ProgramBinder& KGFX_ComputeProgramDx12::BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder)
    {
        if (IsLoaded() && !m_bReady)
        {
            PostLoad();
        }

        if (m_pProgramBinder)
        {
            m_pProgramBinder->BeginBind(pRenderCommanParam, pShareBinder);
        }

        return *m_pProgramBinder;
    }

    BOOL KGFX_ComputeProgramDx12::EndBind()
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

    void KGFX_ComputeProgramDx12::SetBeginBind(BOOL bBeginBind)
    {
        if (m_pProgramBinder)
        {
            m_pProgramBinder->m_bBinding = bBeginBind;
        }
    }

    BOOL KGFX_ComputeProgramDx12::IsLoaded()
    {
        return (m_bLoaded == 1);
    }

    BOOL KGFX_ComputeProgramDx12::IsLoadFailed()
    {
        return (m_bLoaded == -1);
    }

    BOOL KGFX_ComputeProgramDx12::IsLoading()
    {
        return (m_bLoaded == 0);
    }

    BOOL KGFX_ComputeProgramDx12::LoadFromFile()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        KGFX_ShaderResourcePoolDx12* pShaderFilePool = KGFX_GetShaderPoolDx12();
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
        if (m_bLoaded == false)
        {
            m_bLoaded = -1;
        }
        return bResult;
    }

    bool KGFX_ComputeProgramDx12::PostLoad()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        bRetCode = m_pProgramBinder->BuildRootSignature();
        KGLOG_PROCESS_ERROR(bRetCode);

        m_bReady = true;
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KGFX_ComputeProgramDx12::PreparePipeline()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        if (!m_pPipeline)
        {
            bRetCode = BuildPipeline(&m_pPipeline);
            KG_PROCESS_ERROR(bRetCode);
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    bool KGFX_ComputeProgramDx12::BuildPipeline(KGFX_PipelineDX12** ppPipeline) const
    {
        BOOL bResult = false;
        KGFX_PipelineDX12* pPipeline = nullptr;
        pPipeline = new KGFX_PipelineDX12();

        bResult = pPipeline->CreateComputePipeline(m_pProgramBinder->m_pReflector);

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

    BOOL KGFX_ComputeProgramDx12::IsReady()
    {
        return m_bReady;
    }

    void KGFX_ComputeProgramDx12::Reset()
    {
        m_LoadParam = {};
        m_pProgramBinder->ReSet();
    }

    IKGFX_ProgramBinder* KGFX_ComputeProgramDx12::GetProgramBinder()
    {
        return m_pProgramBinder;
    }

    IKGFX_ProgramBinder& KGFX_ComputeProgramDx12::BeginBind()
    {
        return BeginBind(nullptr, nullptr);
    }
}
