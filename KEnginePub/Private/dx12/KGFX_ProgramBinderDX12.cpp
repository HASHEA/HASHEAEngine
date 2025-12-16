#include "KGFX_ProgramBinderDX12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_BufViewDX12.h"
#include "KGFX_TransientHeap.h"
#include "KEnginePub/Private/comm/KGFX_StaticConstBuffer.h"
#include "KGFX_HashFunDX12.h"
#include "KGFX_SamplerDX12.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KGFX_RayTracingDx12.h"

namespace gfx
{
    KGFX_ProgramBinderDx12::KGFX_ProgramBinderDx12() = default;

    KGFX_ProgramBinderDx12::~KGFX_ProgramBinderDx12()
    {
        ReSet();
    }

    BOOL KGFX_ProgramBinderDx12::BuildRootSignature()
    {
        bool bRet = false;
        bRet = m_pReflector->BuildRootSignature();
        KGLOG_PROCESS_ERROR(bRet);

        PreparePushconstAndSpecialConstBuf();
        ProcessReflectShaderCursor();

        return true;
    Exit0:
        return false;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::BeginBind(const RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder)
    {
        ASSERT(!m_bBinding);
        m_bBinding = true;
        m_pRenderContext = nullptr;
        KGFX_GraphicDeviceDx12* pDevice = KGFX_GetGraphicDeviceDx12Internal();
        int engineLoopIndex = NSEngine::GetRenderFrameMoveLoopCount();
        if (engineLoopIndex != m_LastUpdateFrameIndex)
        {
            m_LastUpdateFrameIndex = engineLoopIndex;
            //m_bDescriptorGPUVaild = false;
            m_bThisFrameFirstRender = true;
        }


        KGFX_CommandBufferDX12Impl* pDX12Context = nullptr;
        if (pRenderCommanParam != nullptr)
        {
            pDX12Context = static_cast<KGFX_CommandBufferDX12Impl*>(pRenderCommanParam->pRenderCtx);
        }
        else
        {
            pDX12Context = static_cast<KGFX_CommandBufferDX12Impl*>(gfx::GetRenderContext());
        }
        m_pRenderContext = pDX12Context;
        DescriptorHeapReference viewHeap = &pDX12Context->GetUsedTransientHeap()->GetCurrentViewHeap();
        DescriptorHeapReference samplerHeap = &pDX12Context->GetUsedTransientHeap()->GetCurrentSamplerHeap();
        m_DescriptorTableCache->PrepareGPUDescriptorBind(viewHeap, samplerHeap);
        m_pRenderContext->ClearResourceBarriers();
        if (pShareBinder)
        {
            KSharedPreBinder* preBinder = static_cast<KSharedPreBinder*>(pShareBinder);

            for (auto& preBuf : preBinder->m_mapPreBindTexture)
            {
                switch (preBuf.second->GetViewDesc().eViewType)
                {
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                    AddBindSRV(preBuf.first, preBuf.second);
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                    AddBindUAV(preBuf.first, preBuf.second);
                    break;
                default:
                    assert(false);
                }

            }

            for (auto& preBuf : preBinder->m_mapPreBindBufferResourceView)
            {
                switch (preBuf.second->GetViewDesc()->eViewType)
                {
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                    AddBindSRV(preBuf.first, preBuf.second);
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                    AddBindUAV(preBuf.first, preBuf.second);
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
                    AddBindCBV(preBuf.first, preBuf.second);
                    break;
                default:
                    assert(false);
                }
            }


            for (auto& preBuf : preBinder->m_mapPreBindTextures)
            {
                assert(!preBuf.second.vecTextures.empty());

                switch (preBuf.second.vecTextures.at(0)->GetViewDesc().eViewType)
                {
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                    AddBindSRVArray(preBuf.first, static_cast<uint32_t>(preBuf.second.vecTextures.size()), preBuf.second.vecTextures.data());
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                    AddBindUAVArray(preBuf.first, static_cast<uint32_t>(preBuf.second.vecTextures.size()), preBuf.second.vecTextures.data());
                    break;
                default:
                    assert(false);
                }
            }


            if (m_bSamplerPrepared)
            {
                return *this;
            }

            for (auto& preBuf : preBinder->m_mapPreBindSampler)
            {
                AddBindSampler(preBuf.first, preBuf.second);
            }
        }

        return *this;
    }

    BOOL KGFX_ProgramBinderDx12::EndBind()
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        ComputeBindCode();

        DECLARE_PARAM_NAME(cbufPushConsts);
        DECLARE_PARAM_NAME(MaterialLocalParams);

        if (m_pReflector->GetMtlCbuf() != nullptr)
        {
            AddBindCBV(MaterialLocalParams, m_pReflector->GetMtlCbuf()->GetCBV());
        }

        ProcessSlot();

        m_pRenderContext->FlushResourceBarriers();

        m_bBinding = false;
        bResult = true;
        return bResult;
    }

    void KGFX_ProgramBinderDx12::ProcessSlot()
    {
        if (m_bDescriptorHashVaild)
        {
            int i = 0;
        }
        bool  bDescriptorGPUVaild = m_bDescriptorHashVaild && !m_bThisFrameFirstRender;
        m_DescriptorTableCache->EndCPUDescriptorBind(!m_bDescriptorHashVaild, !bDescriptorGPUVaild, m_bSamplerPrepared);
        m_bThisFrameFirstRender = false;
    }

    void KGFX_ProgramBinderDx12::ProcessReflectShaderCursor()
    {
        m_vecBindedSampler = {};
        m_vecBindedSampler.resize(m_pReflector->m_SamplerCount);
        m_vecBindedCBV = {};
        m_vecBindedCBV.resize(m_pReflector->m_CbufCount);
        m_vecBindedResourceDscriptor = {};
        m_vecBindedResourceDscriptor.resize(m_pReflector->m_SRVCount + m_pReflector->m_UAVCount);

        if (m_DescriptorTableCache == nullptr)
        {
            m_DescriptorTableCache = new KGFX_ProgramDescriptorCacheDX12;
            m_DescriptorTableCache->Init(this, m_pReflector->m_SRVCount + m_pReflector->m_UAVCount, m_pReflector->m_SamplerCount);
        }
        else
        {
            m_DescriptorTableCache->Uninit();
            m_DescriptorTableCache->Init(this, m_pReflector->m_SRVCount + m_pReflector->m_UAVCount, m_pReflector->m_SamplerCount);
        }
    }

    void KGFX_ProgramBinderDx12::PreparePushconstAndSpecialConstBuf()
    {
        if (m_pReflector->GetPushConstRefl().m_uPushConstBufferSize > 0)
        {
            /// pushconst整个pipeline只能有一个，但是不同阶段反射出来的槽位不一定一致
            assert(m_PushConstCbuf == nullptr);
            if (m_PushConstCbuf == nullptr)
            {
                m_PushConstCbuf = new KGFX_MemoryConstBuffer;
                m_PushConstCbuf->Init(m_pReflector->GetPushConstRefl().m_uPushConstBufferSize);
            }
        }

        if (m_pReflector->GetSpecialConstRefl().m_SpecialConstSize > 0)
        {
            m_SpecialConstCbuf = new KGFX_MemoryConstBuffer;
            m_SpecialConstCbuf->Init(m_pReflector->GetSpecialConstRefl().m_SpecialConstSize);
        }

    }


    void KGFX_ProgramBinderDx12::AddBindView(const char* pcszName, IKGFX_BufferView* pBufView)
    {
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        bool bNeedAutoResTrans = false;

        auto findRes = m_pReflector->m_ShaderNameToCPUBind.find(pcszName);

        if (findRes != m_pReflector->m_ShaderNameToCPUBind.end())
        {
#ifdef _DEBUG
            for (auto& cpuBind : m_pReflector->m_ShaderNameToSlot)
            {
                auto findNameRes = cpuBind.find(pcszName);

                if (findNameRes != cpuBind.end())
                {
                    assert(findNameRes->second.slotType == pBufView->GetViewDesc()->eViewType);
                }
            }
#endif

            for (int i = 0; i < findRes->second.GetCount(); i++)
            {
                m_vecBindedResourceDscriptor[findRes->second[i]].SetDescriptor(pBufView->GetNativeHandle(), reinterpret_cast<size_t>(pBufView));
            }
            bNeedAutoResTrans = pBufView->GetResource()->GetDesc()->IsAutoRes() && pBufView->GetResource()->GetDesc()->eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        }


        if (bNeedAutoResTrans)
        {
            /// buf不需要处理subres，是整个都转
            switch (pBufView->GetViewDesc()->eViewType)
            {
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN:
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
                assert(false);  /// DX12这边CBV都是绑定在root constbufferview上的，直接绑定GPU地址，不需要layout
                break;
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                m_pRenderContext->PipelineBarrier(pBufView->GetResource(), { pBufView->GetResource(),KGfxAccess::Unknown,KGfxAccess::SRVMask });
                break;
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
                assert(false);  /// 这两个是FB才会用到的，走到这里说明外面用的人出错了
                break;
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                m_pRenderContext->PipelineBarrier(pBufView->GetResource(), { pBufView->GetResource(),KGfxAccess::Unknown,KGfxAccess::UAVMask });
                break;
            default:;
            }

        }
    }

    void KGFX_ProgramBinderDx12::AddBindView(const char* pcszName, IKGFX_TextureView* pTexView)
    {
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        bool bNeedAutoResTrans = false;

        auto findRes = m_pReflector->m_ShaderNameToCPUBind.find(pcszName);


        if (findRes != m_pReflector->m_ShaderNameToCPUBind.end())
        {

#ifdef _DEBUG
            for (auto& cpuBind : m_pReflector->m_ShaderNameToSlot)
            {
                auto findNameRes = cpuBind.find(pcszName);

                if (findNameRes != cpuBind.end())
                {
                    assert(findNameRes->second.slotType == pTexView->GetViewDesc().eViewType);
                }
            }
#endif

            for (int i = 0; i < findRes->second.GetCount(); i++)
            {
                m_vecBindedResourceDscriptor.at(findRes->second[i]).SetDescriptor(pTexView->GetNativeHandle(), reinterpret_cast<size_t>(pTexView));
            }

            bNeedAutoResTrans = pTexView->GetResource()->GetDesc()->IsAutoRes();
        }


        if (bNeedAutoResTrans)
        {
            auto tarnsSubRes = pTexView->GetViewDesc().sSubresourceRange;
            switch (pTexView->GetViewDesc().eViewType)
            {
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN:
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
                assert(false);  /// DX12这边CBV都是绑定在root constbufferview上的，直接绑定GPU地址，不需要layout
                break;
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                m_pRenderContext->PipelineBarrier(pTexView->GetResource(), { pTexView->GetResource(),KGfxAccess::Unknown,KGfxAccess::SRVMask ,tarnsSubRes });
                break;
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
                assert(false);  /// 这两个是FB才会用到的，走到这里说明外面用的人出错了
                break;
            case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                m_pRenderContext->PipelineBarrier(pTexView->GetResource(), { pTexView->GetResource(),KGfxAccess::Unknown,KGfxAccess::UAVMask ,tarnsSubRes });
                break;
            default:;
            }

        }
    }

    void KGFX_ProgramBinderDx12::AddBindViewArray(const char* pcszName, uint32_t num, IKGFX_TextureView** pTexViews)
    {
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        bool bNeedAutoResTrans = false;

        //auto [begin, end] = m_pReflector->m_ShaderNameToCPUBind.equal_range(pcszName);

        //for (auto it = begin; it != end; ++it)
        //{
        //    // m_vecBindedResource.at(it->second) = { reinterpret_cast<KGFX_TextureViewDX12**>(pTexViews) ,num };
        //    bNeedAutoResTrans = true;
        //}


        if (bNeedAutoResTrans)
        {
            for (uint32_t i = 0; i < num; ++i)
            {
                auto pEachView = pTexViews[i];
                if (pEachView->GetResource()->GetDesc()->IsAutoRes())
                {
                    continue;
                }
                auto tarnsSubRes = pEachView->GetViewDesc().sSubresourceRange;

                switch (pEachView->GetViewDesc().eViewType)
                {
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN:
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
                    assert(false);  /// DX12这边CBV都是绑定在root constbufferview上的，直接绑定GPU地址，不需要layout
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                    m_pRenderContext->PipelineBarrier(pEachView->GetResource(), { pEachView->GetResource(),KGfxAccess::Unknown,KGfxAccess::SRVMask ,tarnsSubRes });
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
                    assert(false);  /// 这两个是FB才会用到的，走到这里说明外面用的人出错了
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                    m_pRenderContext->PipelineBarrier(pEachView->GetResource(), { pEachView->GetResource(),KGfxAccess::Unknown,KGfxAccess::UAVMask ,tarnsSubRes });
                    break;
                default:;
                }
            }
        }
    }

    void KGFX_ProgramBinderDx12::AddBindViewArray(const char* pcszName, uint32_t num, IKGFX_BufferView** pBufViews)
    {
        ASSERT(m_bBinding);
        ASSERT(pcszName);

        bool bNeedAutoResTrans = false;

        //auto [begin, end] = m_pReflector->m_ShaderNameToCPUBind.equal_range(pcszName);

        //for (auto it = begin; it != end; ++it)
        //{
        //    // m_vecBindedResource.at(it->second) = { reinterpret_cast<KGFX_BufferViewDX12**>(pBufViews) ,num };
        //    bNeedAutoResTrans = true;
        //}


        if (bNeedAutoResTrans)
        {
            for (uint32_t i = 0; i < num; ++i)
            {
                auto pEachView = pBufViews[i];
                if (pEachView->GetResource()->GetDesc()->IsAutoRes())
                {
                    continue;
                }
                switch (pEachView->GetViewDesc()->eViewType)
                {
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN:
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV:
                    assert(false);  /// DX12这边CBV都是绑定在root constbufferview上的，直接绑定GPU地址，不需要layout
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV:
                    m_pRenderContext->PipelineBarrier(pEachView->GetResource(), { pEachView->GetResource(),KGfxAccess::Unknown,KGfxAccess::SRVMask });
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
                    assert(false);  /// 这两个是FB才会用到的，走到这里说明外面用的人出错了
                    break;
                case KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
                    m_pRenderContext->PipelineBarrier(pEachView->GetResource(), { pEachView->GetResource(),KGfxAccess::Unknown,KGfxAccess::UAVMask });
                    break;
                default:;
                }
            }
        }
    }

    void KGFX_ProgramBinderDx12::AddBindGloablCbufData(const char* pcszName, const void* pData, int dataSize) const
    {
        if (m_pReflector->GetSpecialConstRefl().m_SpecialConstBinds)
        {
            auto findRex = m_pReflector->GetSpecialConstRefl().m_SpecialConstBinds->find(pcszName);
            if (findRex != m_pReflector->GetSpecialConstRefl().m_SpecialConstBinds->end())
            {
                uint8_t* pcpuData = m_SpecialConstCbuf->GetCpuData();
                memcpy(pcpuData + findRex->second.m_uOffset, pData, dataSize);
                return;
            }
        }
    }


    BOOL KGFX_ProgramBinderDx12::UpdateMtlData()
    {
        if (m_pReflector->GetMtlCbuf())
        {
            if (m_pReflector->m_bMtlCbufNeedUpdate)
            {
                m_pReflector->GetMtlCbuf()->Update(m_pRenderContext);
                m_pReflector->m_bMtlCbufNeedUpdate = false;
            }

        }
        return true;
    }


    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindUAV(const char* pcszName, IKGFX_BufferView* pBufView)
    {
        AddBindView(pcszName, pBufView);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindUAV(const char* pcszName, IKGFX_TextureView* pTexView)
    {
        AddBindView(pcszName, pTexView);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindUAVArray(const char* pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews)
    {
        assert(uNum > 1);
        AddBindViewArray(pcszName, uNum, pTexViews);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindUAVArray(const char* pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews)
    {
        assert(uNum > 1);
        AddBindViewArray(pcszName, uNum, pBufViews);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindSRVArray(const char* pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews)
    {
        assert(uNum > 1);
        AddBindViewArray(pcszName, uNum, pTexViews);
        return *this;
    }

    void KGFX_ProgramBinderDx12::ReSet()
    {
        //m_vecBindedResource.clear();
        m_vecBindedSampler.clear();
        m_bSamplerPrepared = false;
        SAFE_RELEASE(m_SpecialConstCbuf);
        SAFE_RELEASE(m_DescriptorTableCache);
        SAFE_RELEASE(m_PushConstCbuf);
        SAFE_RELEASE(m_pReflector);
    }

    BOOL KGFX_ProgramBinderDx12::SetMtlParamValue(const char* szName, void* pData, uint32_t uByteSize)
    {
        return m_pReflector->SetPerMtlValue(szName, pData, uByteSize);
    }

    BOOL KGFX_ProgramBinderDx12::IsBinding()
    {
        return m_bBinding;
    }

    TextureType KGFX_ProgramBinderDx12::GetTextureType(const_pool_str szName)
    {

        auto FindRes = std::find_if(m_pReflector->GetAllResRefl().begin(), m_pReflector->GetAllResRefl().end(),
            [szName](const KGFX_ProgramUniformTextureDX12& texRefl)
            {
                return texRefl.GetName() == szName;
            });

        //assert(FindRes != m_pReflector->GetAllResRefl().end());
        if (FindRes != m_pReflector->GetAllResRefl().end())
        {
            return FindRes->m_pResStageRefl->m_eTextureType;
        }
        return {};
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufView)
    {
        AddBindView(pcszName, pBufView);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView)
    {
        if (pTexView)
        {
            AddBindView(pcszName, pTexView);
        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pTexViews)
    {
        AddBindViewArray(pcszName, uNum, pTexViews);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindCBV(const char* pcszName, IKGFX_BufferView* pBufView)
    {

        auto findRes = m_pReflector->m_ShaderRootCBV.find(pcszName);
        if (findRes != m_pReflector->m_ShaderRootCBV.end())
        {
            for (int i = 0; i < findRes->second.GetCount(); i++)
            {
                uint32_t index = findRes->second[i];
                m_vecBindedCBV[index] = pBufView;
            }
        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindAccelerationStructure(const_pool_str pcszName, KRayTracingScene* accelerationStructure)
    {
        IKGFX_BufferView* pBufferView = ((RayTracingSceneDx12*)accelerationStructure)->getTopAccelerationStructureBufferView();
        AddBindView(pcszName, pBufferView);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::AddBindSampler(const char* pcszName, IKGFX_Sampler* pSampler)
    {
        ASSERT(m_bBinding);
        ASSERT(pcszName);
        assert(pSampler);

        if (m_bSamplerPrepared)
        {
            return *this;
        }

        auto findRes = m_pReflector->m_ShaderNameToCPUSampler.find(pcszName);

        if (findRes != m_pReflector->m_ShaderNameToCPUSampler.end())
        {
            for (int i = 0; i < 6; i++)
            {
                if (findRes->second(i))
                {
                    m_vecBindedSampler.at(findRes->second[i]).SetDescriptor(((KGFX_SamplerDX12*)pSampler)->GetNativeHandle(), reinterpret_cast<size_t>(pSampler));
                }
            }

        }

        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::SetImmutableConstValueInt(const char* pcszName, int32_t value)
    {
        AddBindGloablCbufData(pcszName, &value);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::SetImmutableConstValueUInt(const char* pcszName, uint32_t value)
    {
        AddBindGloablCbufData(pcszName, &value);
        return *this;
    }

    IKGFX_ProgramBinder& KGFX_ProgramBinderDx12::SetImmutableConstValueFloat(const char* pcszName, float value)
    {
        AddBindGloablCbufData(pcszName, &value);
        return *this;
    }

    BOOL KGFX_ProgramBinderDx12::IsTextureBinded(const char* pName)
    {
        auto findRes = m_pReflector->m_ShaderNameToCPUBind.find(pName);

        if (findRes != m_pReflector->m_ShaderNameToCPUBind.end())
        {
            for (int i = 0; i < findRes->second.GetCount(); i++)
            {
                if (m_vecBindedResourceDscriptor.at(findRes->second[i]).IsVaild())
                {
                    return true;
                }
            }

        }

        return false;
    }

    int KGFX_ProgramBinderDx12::GetSRVStageStartIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = 0;
        int index = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex); value < static_cast<uint32_t>(eStage); value <<= 1)
        {
            if (index < m_pReflector->GetShaderStageSRVCount().size())
            {
                count += m_pReflector->GetShaderStageSRVCount().at(index++);
            }
        }
        return count;
    }

    int KGFX_ProgramBinderDx12::GetSRVStageBaseIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = m_pReflector->GetShaderStageSRVBaseIndex().at(CalculateShaderStageTypeIndex(eStage));
        return count;
    }

    int KGFX_ProgramBinderDx12::GetUAVStageStartIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = 0;
        int index = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex); value < static_cast<uint32_t>(eStage); value <<= 1)
        {
            if (index < m_pReflector->GetShaderStageUAVCount().size())
            {
                count += m_pReflector->GetShaderStageUAVCount().at(index++);
            }
        }
        return count;
    }

    int KGFX_ProgramBinderDx12::GetUAVStageBaseIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = m_pReflector->GetShaderStageUAVBaseIndex().at(CalculateShaderStageTypeIndex(eStage));
        return count;
    }

    int KGFX_ProgramBinderDx12::GetSamplerStageStartIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = 0;
        int index = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex); value < static_cast<uint32_t>(eStage); value <<= 1)
        {
            if (index < m_pReflector->GetShaderStageSampleCount().size())
            {
                count += m_pReflector->GetShaderStageSampleCount().at(index++);
            }
        }
        return count;
    }

    int KGFX_ProgramBinderDx12::GetSamplerStageBaseIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = m_pReflector->GetShaderStageSamplerIndex().at(CalculateShaderStageTypeIndex(eStage));
        return count;
    }


    D3D12_GPU_DESCRIPTOR_HANDLE KGFX_ProgramBinderDx12::GetGPUDescriptorTable(ShaderStageType stage, CopyDescriptorType type) const
    {
        return m_DescriptorTableCache->GetGPUDescriptorTable(stage, type);
    }

    bool KGFX_ProgramBinderDx12::CalcResourceHash() const
    {
        bool bVaild = true;
        for (auto& res : m_vecBindedResourceDscriptor)
        {
            bVaild &= res.IsNoChange();
        }

        return bVaild;
    }

    void KGFX_ProgramBinderDx12::ComputeBindCode()
    {
        uint64_t checkCode = 0;
        int bindSlotCount = 0;
        assert(m_bBinding);

        for (auto& SRVorUAV : m_vecBindedResourceDscriptor)
        {
            bindSlotCount += SRVorUAV.IsVaild() ? 1 : 0;
        }

        for (auto& sampler : m_vecBindedSampler)
        {
            bindSlotCount += sampler.IsVaild() ? 1 : 0;
        }

        int allSRV = GetSRVStageStartIndex(ShaderStageType::CountOf);
        int allUAV = GetUAVStageStartIndex(ShaderStageType::CountOf);
        int allSampler = GetSamplerStageStartIndex(ShaderStageType::CountOf);

        int allNeedCount = allSRV + allUAV + allSampler;

        /// 有资源忘记绑定了
        assert(allNeedCount == bindSlotCount);

        m_bDescriptorHashVaild = CalcResourceHash();
    }
}
