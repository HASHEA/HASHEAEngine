#ifdef _WIN32
#include "KGFX_DescriptorCacheDX12.h"
#include "KGFX_ProgramBinderDX12.h"
#include "KGFX_PipelineDX12.h"
#include "KGFX_TextureImplDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_StaticConfigDX12.h"


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace gfx
{
    void KGFX_ProgramDescriptorCacheDX12::DoGPUDescriptorCopy(CopyDescriptorType type) const
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();

        switch (type)
        {
        case CopyDescriptorType::SRV:
        case CopyDescriptorType::UAV:
        {
            D3D12_CPU_DESCRIPTOR_HANDLE copySRCHandleStartView = m_CPUDescriptorSetCache.resourceTable.GetCpuHandle();
            D3D12_CPU_DESCRIPTOR_HANDLE copyDSTHandleStartView = m_CurrentGPUDescriptorSetRef.resourceTable.GetCpuHandle();
            int srvCount = m_pProgramBinder->GetSRVStageStartIndex(ShaderStageType::CountOf);
            int uavCount = m_pProgramBinder->GetUAVStageStartIndex(ShaderStageType::CountOf);
            int copyCount = srvCount + uavCount;
            pD3dDevice->CopyDescriptorsSimple(copyCount, copyDSTHandleStartView, copySRCHandleStartView, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        break;
        case CopyDescriptorType::SAMPLER:
        {
            D3D12_CPU_DESCRIPTOR_HANDLE copySRCHandleStartSampler = m_CPUDescriptorSetCache.samplerTable.GetCpuHandle();
            D3D12_CPU_DESCRIPTOR_HANDLE copyDSTHandleStartSampler = m_CurrentGPUDescriptorSetRef.samplerTable.GetCpuHandle();
            int samplerCount = m_pProgramBinder->GetSamplerStageStartIndex(ShaderStageType::CountOf);
            pD3dDevice->CopyDescriptorsSimple(samplerCount, copyDSTHandleStartSampler, copySRCHandleStartSampler, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        break;
        default:;
        }
    }

    void KGFX_ProgramDescriptorCacheDX12::DoCPUDescriptorCopy(CopyDescriptorType copyType) const
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        int DescriptorSizeSampler = pD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        switch (copyType)
        {
        case CopyDescriptorType::SRV:
        case CopyDescriptorType::UAV:
        {
            D3D12_CPU_DESCRIPTOR_HANDLE copyDstHandleStartView = m_CPUDescriptorSetCache.resourceTable.GetCpuHandle();
            for (uint32_t i = 0; i < m_pProgramBinder->m_vecBindedResourceDscriptor.size(); ++i)
            {
                auto offset = m_pProgramBinder->m_pReflector->m_CPUBindToDescriptorTableOffset.at(i);


                auto findRes = m_pProgramBinder->m_vecBindedResourceDscriptor.at(i);
                if (bOpenEveryDescriptorCopyOptimization && findRes.IsNoChange())
                {
                    continue;
                }
                D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = findRes.d3d12Handle;
                assert(srcHandle.ptr);
                D3D12_CPU_DESCRIPTOR_HANDLE copyDstHandleIndex = { copyDstHandleStartView.ptr + static_cast<uint64_t>(offset * DescriptorSizeSampler) };
                pD3dDevice->CopyDescriptorsSimple(1, copyDstHandleIndex, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            }

        }
        break;
        case CopyDescriptorType::SAMPLER:
        {
            D3D12_CPU_DESCRIPTOR_HANDLE copyDstHandleStartSampler = m_CPUDescriptorSetCache.samplerTable.GetCpuHandle();

            for (uint32_t i = 0; i < m_pProgramBinder->m_vecBindedSampler.size(); ++i)
            {
                auto offset = m_pProgramBinder->m_pReflector->m_SamplerBindToDescriptorTableOffset.at(i);


                auto findRes = m_pProgramBinder->m_vecBindedSampler.at(i);
                if (bOpenEveryDescriptorCopyOptimization && findRes.IsNoChange())
                {
                    continue;
                }
                D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = findRes.d3d12Handle;
                assert(srcHandle.ptr);

                D3D12_CPU_DESCRIPTOR_HANDLE copyDstHandleIndex = { copyDstHandleStartSampler.ptr + static_cast<uint64_t>(offset * DescriptorSizeSampler) };
                pD3dDevice->CopyDescriptorsSimple(1, copyDstHandleIndex, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

            }

        }
        break;
        default:;
        }
    }


    KGFX_ProgramDescriptorCacheDX12::~KGFX_ProgramDescriptorCacheDX12()
    {
        Reset();
    }

    void KGFX_ProgramDescriptorCacheDX12::Init(KGFX_ProgramBinderDx12* shaderProgram, int srvAnduavCount, int samplerCount)
    {
        bool bRet = false;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        DescriptorHeapReference srvheapRef = pGraphicDevice->GetDX12SRVAndUAVAndCBVCacheHeap();
        DescriptorHeapReference samheapRef = pGraphicDevice->GetDX12SamplerCacheHeap();

        m_pProgramBinder = shaderProgram;
        m_CPUDescriptorSetCache.resourceTable.SetHeapReference(srvheapRef);
        bRet = m_CPUDescriptorSetCache.resourceTable.Allocate(srvAnduavCount);
        KGLOG_PROCESS_ERROR(bRet);

        m_CPUDescriptorSetCache.samplerTable.SetHeapReference(samheapRef);
        bRet = m_CPUDescriptorSetCache.samplerTable.Allocate(samplerCount);
        KGLOG_PROCESS_ERROR(bRet);

    Exit0:
        return;
    }

    void KGFX_ProgramDescriptorCacheDX12::Reset()
    {
        m_CPUDescriptorSetCache.resourceTable.FreeIfSupported();
        m_CPUDescriptorSetCache.samplerTable.FreeIfSupported();
    }

    void KGFX_ProgramDescriptorCacheDX12::PrepareGPUDescriptorBind(DescriptorHeapReference srvSubHeap, DescriptorHeapReference samSubHeap)
    {
        m_CurrentGPUDescriptorSetRef.resourceTable.SetHeapReference(srvSubHeap);
        m_CurrentGPUDescriptorSetRef.samplerTable.SetHeapReference(samSubHeap);
    }

    void KGFX_ProgramDescriptorCacheDX12::EndCPUDescriptorBind(bool bCPUNeed, bool bGPUNeed, bool& bSamplerPrepared)
    {
        if (m_CPUDescriptorSetCache.resourceTable.m_DescriptorCount > 0 && bCPUNeed)
        {
            DoCPUDescriptorCopy(CopyDescriptorType::SRV);
        }

        if (m_CPUDescriptorSetCache.samplerTable.m_DescriptorCount > 0 && !bSamplerPrepared)
        {
            DoCPUDescriptorCopy(CopyDescriptorType::SAMPLER);
        }

        if (bGPUNeed)
        {
            ProcessGPUDescriptorBind(bSamplerPrepared);
        }

    }

    void KGFX_ProgramDescriptorCacheDX12::ProcessGPUDescriptorBind(bool& bSamplerPrepared)
    {
        if (m_CPUDescriptorSetCache.resourceTable.m_DescriptorCount > 0)
        {
            m_CurrentGPUDescriptorSetRef.resourceTable.Allocate(m_CPUDescriptorSetCache.resourceTable.m_DescriptorCount);
            DoGPUDescriptorCopy(CopyDescriptorType::SRV);
        }

        if (m_CPUDescriptorSetCache.samplerTable.m_DescriptorCount > 0 && !bSamplerPrepared)
        {
            m_CurrentGPUDescriptorSetRef.samplerTable.Allocate(m_CPUDescriptorSetCache.samplerTable.m_DescriptorCount);
            DoGPUDescriptorCopy(CopyDescriptorType::SAMPLER);
            bSamplerPrepared = true;
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE KGFX_ProgramDescriptorCacheDX12::GetGPUDescriptorTable(ShaderStageType stage, CopyDescriptorType type) const
    {
        int offsetCount = 0;
        int resCount = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE startHandle = {};
        switch (type)
        {
        case CopyDescriptorType::SRV:
            offsetCount = 0;
            resCount = m_pProgramBinder->GetSRVStageStartIndex(stage);
            resCount += offsetCount;
            startHandle = m_CurrentGPUDescriptorSetRef.resourceTable.GetGpuHandle(resCount);
            break;
        case CopyDescriptorType::UAV:
            offsetCount = m_pProgramBinder->GetSRVStageStartIndex(ShaderStageType::CountOf);
            resCount = m_pProgramBinder->GetUAVStageStartIndex(stage);
            resCount += offsetCount;
            startHandle = m_CurrentGPUDescriptorSetRef.resourceTable.GetGpuHandle(resCount);
            break;
        case CopyDescriptorType::SAMPLER:
            offsetCount = 0;
            resCount = m_pProgramBinder->GetSamplerStageStartIndex(stage);
            resCount += offsetCount;
            startHandle = m_CurrentGPUDescriptorSetRef.samplerTable.GetGpuHandle(resCount);
            break;
        }

        return startHandle;
    }

    int KGFX_ProgramDescriptorCacheDX12::AddRef()
    {
        int32_t nRef = ++m_nRef;
        return nRef;
    }

    int KGFX_ProgramDescriptorCacheDX12::GetRef()
    {
        return m_nRef;
    }

    int KGFX_ProgramDescriptorCacheDX12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            KGFX_ProgramDescriptorCacheDX12* p = this;
            SAFE_DELETE(p);
        }
        return nRef;
    }
}
#endif
